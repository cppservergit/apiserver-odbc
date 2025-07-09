#ifndef SERVER_H_
#define SERVER_H_

#include <sys/epoll.h>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <stop_token>
#include <unordered_map>
#include <functional>
#include <chrono>
#include <memory>
#include <atomic>
#include <unordered_set>
#include "util.h"
#include "env.h"
#include "logger.h"
#include "login.h"
#include "sql.h"
#include "httputils.h"
#include "jwt.h"
#include "email.h"
#include "http_client.h"

extern const char SERVER_VERSION[];
extern const char* const LOGGER_SRC;

// Type aliases for API definitions
using rules = std::vector<http::input_rule>;
using roles = std::vector<std::string>;

class file_descriptor {
public:
    // Constructor takes ownership of the file descriptor.
    explicit file_descriptor(int fd = -1) : m_fd(fd) {}

    // Destructor automatically closes the file descriptor.
    ~file_descriptor() {
        if (m_fd != -1) {
            close(m_fd);
        }
    }

    // --- Rule of Five: Non-copyable, but movable ---
    file_descriptor(const file_descriptor&) = delete;
    file_descriptor& operator=(const file_descriptor&) = delete;

    file_descriptor(file_descriptor&& other) noexcept : m_fd(other.m_fd) {
        // The moved-from object no longer owns the descriptor.
        other.m_fd = -1;
    }

    file_descriptor& operator=(file_descriptor&& other) noexcept {
        if (this != &other) {
            // Close the current descriptor if it's valid.
            if (m_fd != -1) {
                close(m_fd);
            }
            // Take ownership from the other object.
            m_fd = other.m_fd;
            other.m_fd = -1;
        }
        return *this;
    }

    // Allow implicit conversion to int for easy use with C-style APIs.
    operator int() const {
        return m_fd;
    }

private:
    int m_fd;
};

template<>
struct std::formatter<file_descriptor> : std::formatter<int> {
	auto format(const file_descriptor& fd, std::format_context& ctx) const {
		return std::formatter<int>::format(static_cast<int>(fd), ctx);
	}
};

// Compile-time validated path for WebAPIs
struct webapi_path {
public:
    consteval explicit webapi_path(std::string_view _path): m_path{_path} {
        if (_path.contains(" ")) {
            throw std::string("Invalid WebAPI path -> contains space");
        }
        if (!_path.starts_with("/")) {
            throw std::string("Invalid WebAPI path -> must start with '/'");
        }
        if (_path.ends_with("/")) {
            throw std::string("Invalid WebAPI path -> cannot end with '/'");
        }
        std::string_view valid_chars{"abcdefghijklmnopqrstuvwxyz_-0123456789/"};
        for(const char& c: _path)
            if (!valid_chars.contains(c))
                throw std::string("Invalid WebAPI path -> contains an invalid character");
    }
    
    std::string get() const  {
        return std::string(m_path);
    }

private: 
    std::string_view m_path;
};

// Main server class
struct server {
public:
    // Represents a single WebAPI endpoint
    struct webapi {
        std::string description;
        http::verb verb;
        std::vector<http::input_rule> rules;
        std::vector<std::string> roles;
        std::function<void(http::request&)> fn;
        bool is_secure {true};

        webapi(std::string _description, http::verb _verb, 
               std::vector<http::input_rule> _rules, std::vector<std::string> _roles, 
               std::function<void(http::request&)> _fn, bool _is_secure);
    };
    
    // --- Public Structs (Moved from private section) ---
    struct worker_params {
        http::request req;
        std::shared_ptr<const webapi> api;
    };
    struct audit_trail {
        std::string username;
        std::string remote_ip;
        std::string path;
        std::string payload;
        std::string sessionid;
        std::string useragent;
        std::string nodename;
        std::string x_request_id;
    };


    // --- Public Methods ---
    server();
    void start();

    // Overload 1: For calls that specify rules and roles
    template<typename DescType, typename RulesType, typename RolesType, typename FnType>
    void register_webapi(
        const webapi_path& _path,
        DescType&& _description,
        const http::verb& _verb,
        RulesType&& _rules,
        RolesType&& _roles,
        FnType&& _fn,
        const bool _is_secure = true)
    {
        webapi_catalog.try_emplace(
            _path.get(),
            std::make_shared<const webapi>(
                std::forward<DescType>(_description),
                _verb,
                std::forward<RulesType>(_rules),
                std::forward<RolesType>(_roles),
                std::forward<FnType>(_fn),
                _is_secure
            )
        );
    }

    // Overload 2: For calls that omit rules and roles
    template<typename DescType, typename FnType>
    void register_webapi(
        const webapi_path& _path,
        DescType&& _description,
        const http::verb& _verb,
        FnType&& _fn,
        const bool _is_secure = true)
    {
        register_webapi(
            _path,
            std::forward<DescType>(_description),
            _verb,
            std::vector<http::input_rule>{},
            std::vector<std::string>{},
            std::forward<FnType>(_fn),
            _is_secure
        );
    }
	
	class server_startup_exception : public std::runtime_error {
	public:
		explicit server_startup_exception(const std::string& message)
			: std::runtime_error(message) {}
	};	
	

private:
    // --- Private Methods ---
    void send_options(http::request& req);
    void send_error(http::request& req, http::status status, std::string_view msg);
    void save_audit_trail(const audit_trail& at);
    void execute_service(http::request& req, const std::shared_ptr<const webapi>& api_ptr);
    void process_request(http::request& req, const std::shared_ptr<const webapi>& api_ptr) ;
    void log_request(const http::request& req, double duration) ;
    void http_server(http::request& req, const std::shared_ptr<const webapi>& api_ptr) ;
    bool read_request(http::request& req, int bytes) ;
    int get_signalfd() ;
    int get_listenfd(int port) ;
    void epoll_add_event(int fd, int epoll_fd, uint32_t event_flags) ;
    std::string get_socket_error(const int& fd);
    void epoll_handle_error(epoll_event& ev) ;
    void epoll_handle_close(epoll_event& ev) ;
    void epoll_handle_connect(const int& listen_fd, const int& epoll_fd) ;
    void epoll_abort_request(http::request& req, http::status status_code, const std::string& msg_ = "") ;
    void check_ready_queue() ;
    void producer(worker_params& wp) ;
    void run_async_task(http::request& req) ;
    void epoll_send_ping(http::request& req);
    void epoll_send_sysinfo(http::request& req) ;
    void epoll_handle_read(http::request& req) ;
    void epoll_handle_write(http::request& req) ;
    void epoll_handle_IO(epoll_event& ev) ;
    void epoll_loop(int listen_fd, int epoll_fd) ;
    void start_epoll(int port) ;
    void print_server_info() ;
    void register_diagnostic_services();
    void prebuilt_services();
    std::string get_pod_name();
	bool is_origin_allowed(const std::string& origin);
	void shutdown();

    // --- Private Members ---
    std::unordered_map<std::string, std::shared_ptr<const webapi>, util::string_hash, std::equal_to<>> webapi_catalog;
    std::unordered_map<int, http::request> buffers;
    
    std::atomic<size_t> g_counter{0};
    std::atomic<double> g_total_time{0};
    std::atomic<int> g_active_threads{0};
    std::atomic<size_t> g_connections{0};

    std::queue<worker_params> m_queue;
    std::condition_variable m_cond;
    std::mutex m_mutex;

    std::queue<audit_trail> m_audit_queue;
    std::condition_variable m_audit_cond;
    std::mutex m_audit_mutex;

    std::queue<http::request> m_ready_queue;
    std::mutex m_ready_mutex;
    
    file_descriptor m_signal;
    const std::string pod_name;
    const std::string server_start_date;
    bool enable_audit {false};
    
	const std::unordered_set<std::string> ALLOWED_ORIGINS;
	
	//thread pool and shutdown support
	std::vector<std::stop_source> m_stops;
	std::vector<std::jthread> m_pool;
	std::stop_source m_audit_stop;
    std::jthread m_audit_engine;
	
    // Allow consumer and audit lambdas to access private members
    friend auto consumer(std::stop_token, server*) ;
    friend auto audit(std::stop_token, server*) ;
};

#endif // SERVER_H_