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
extern const char* LOGGER_SRC;

// Type aliases for API definitions
using rules = std::vector<http::input_rule>;
using roles = std::vector<std::string>;

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
    
    std::string get() const noexcept {
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

private:
    // --- Private Methods ---
    void send_options(http::request& req);
    void send_error(http::request& req, int status, std::string_view msg);
    void save_audit_trail(const audit_trail& at);
    void execute_service(http::request& req, const std::shared_ptr<const webapi>& api_ptr);
    void process_request(http::request& req, const std::shared_ptr<const webapi>& api_ptr) noexcept;
    void log_request(const http::request& req, double duration) noexcept;
    void http_server(http::request& req, const std::shared_ptr<const webapi>& api_ptr) noexcept;
    bool read_request(http::request& req, int bytes) noexcept;
    int get_signalfd() noexcept;
    int get_listenfd(int port) noexcept;
    void epoll_add_event(int fd, int epoll_fd, uint32_t event_flags) noexcept;
    std::string get_socket_error(const int& fd);
    void epoll_handle_error(epoll_event& ev) noexcept;
    void epoll_handle_close(epoll_event& ev) noexcept;
    void epoll_handle_connect(const int& listen_fd, const int& epoll_fd) noexcept;
    void epoll_abort_request(http::request& req, int status_code) noexcept;
    void check_ready_queue() noexcept;
    void producer(worker_params& wp) noexcept;
    void run_async_task(http::request& req) noexcept;
    void epoll_send_ping(http::request& req);
    void epoll_send_sysinfo(http::request& req) noexcept;
    void epoll_handle_read(epoll_event& ev) noexcept;
    void epoll_handle_write(epoll_event& ev) noexcept;
    void epoll_handle_IO(epoll_event& ev) noexcept;
    void epoll_loop(int listen_fd, int epoll_fd) noexcept;
    void start_epoll(int port) noexcept;
    void print_server_info() noexcept;
    void register_diagnostic_services();
    void prebuilt_services();
    std::string get_pod_name();

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
    
    int m_signal;
    const std::string pod_name;
    const std::string server_start_date;
    bool enable_audit {false};
    std::chrono::time_point<std::chrono::high_resolution_clock> _start_init{};
    
    // Allow consumer and audit lambdas to access private members
    friend auto consumer(std::stop_token, server*) noexcept;
    friend auto audit(std::stop_token, server*) noexcept;
};

#endif // SERVER_H_