#include "server.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/signalfd.h>
#include <netinet/tcp.h>
#include <array>
#include <iostream>
#include <charconv>
#include <format>
#include <system_error>
#include <expected>

namespace {
	std::string str_error_cpp(int err_num) 
	{
		std::error_code ec(err_num, std::system_category());
		return ec.message();
	}
	
	std::string get_peer_ip_ipv4(int sockfd) 
	{
		// Use sockaddr_in directly as we only expect IPv4
		sockaddr_in addr{};
		socklen_t addr_len = sizeof(addr);

		if (getpeername(sockfd, reinterpret_cast<sockaddr*>(&addr), &addr_len) == 0) {
			std::array<char, INET_ADDRSTRLEN> buffer{};
			// Convert the binary IPv4 address to a string
			if (inet_ntop(AF_INET, &addr.sin_addr, buffer.data(), buffer.size())) {
				return std::string{buffer.data()};
			}
		}
		// Return an empty string on failure
		return "";
	}
	
	std::string_view trim_whitespace(std::string_view str) 
	{
		const auto first = str.find_first_not_of(" \t\n\r\f\v");
		if (std::string_view::npos == first) {
			return {}; // Return empty view if string is all whitespace
		}
		const auto last = str.find_last_not_of(" \t\n\r\f\v");
		return str.substr(first, (last - first + 1));
	}	
	
	std::unordered_set<std::string> parse_allowed_origins(std::string_view origins_str) 
	{
		std::unordered_set<std::string> allowed_origins;

		for (const auto& range : origins_str | std::views::split(',')) {
			// The view needs to be converted to a string_view to be trimmed
			std::string_view token_view(&*range.begin(), std::ranges::distance(range));
			std::string_view trimmed_origin = trim_whitespace(token_view);
			if (!trimmed_origin.empty()) {
				allowed_origins.insert(std::string(trimmed_origin));
			}
		}
		return allowed_origins;
	}	
	
	std::string get_signal_name(int fd) {
		signalfd_siginfo info {};
		ssize_t bytesRead = read(fd, &info, sizeof(info));
		if (bytesRead != sizeof(info)) {
			return "read_error";
		}

		switch (info.ssi_signo) {
			case SIGINT:  return "SIGINT (Interrupt)";
			case SIGTERM: return "SIGTERM (Termination)";
			case SIGQUIT: return "SIGQUIT (Quit)";
			default:      return "Unknown signal";
		}
	}
	
	inline int get_fd(const epoll_event& ev) {
		return static_cast<int>(ev.data.fd);
	}
	
	constexpr std::string_view get_reason_phrase(http::status s) {
			switch (s) {
				case http::status::ok:                  return "OK";
				case http::status::no_content:          return "No Content";
				case http::status::bad_request:         return "Bad Request";
				case http::status::unauthorized:        return "Unauthorized";
				case http::status::forbidden:           return "Forbidden";
				case http::status::not_found:           return "Not Found";
				case http::status::method_not_allowed:  return "Method Not Allowed";
				// Add other status codes used in your application here.
				default:                                return "Internal Server Error";
			}
	}
}

// --- Global Constants ---
const char SERVER_VERSION[] = "API-Server++ v1.4.2";
const char* const LOGGER_SRC {"server"};

// --- Free Functions (Workers and Helpers) ---
// Note: This function can now use 'server::audit_trail' because it's public in server.h
void audit_task(const server::audit_trail& params)  {
    constexpr auto sql {"sp_audit_trail ?, ?, ?, ?, ?, ?, ?, ?"};
    if (auto res = sql::exec_sqlp("CPP_AUDITDB", sql, params.path, params.username, params.remote_ip, 
                                    params.payload, params.sessionid, 
                                    params.useragent, params.nodename, params.x_request_id); !res)
    {
        logger::log("audit", "error", std::format("could not save audit record in database: {}", res.error()));
    }
	return;
}

auto audit(std::stop_token tok, server* srv)  {
    logger::log("pool", "info", "starting audit thread");
    while(!tok.stop_requested()) {
        std::unique_lock lock{srv->m_audit_mutex};
        srv->m_audit_cond.wait(lock, [&tok, &srv]() { return (!srv->m_audit_queue.empty() || tok.stop_requested()); });
        if (tok.stop_requested()) { lock.unlock(); break; }
        auto params = std::move(srv->m_audit_queue.front());
        srv->m_audit_queue.pop();
        lock.unlock();
        audit_task(params);
    }
    logger::log("pool", "info", "stopping audit thread");
}

auto consumer(std::stop_token tok, server* srv)  {
    while(!tok.stop_requested()) {
        std::unique_lock lock{srv->m_mutex};
        srv->m_cond.wait(lock, [&tok, &srv]() { return (!srv->m_queue.empty() || tok.stop_requested()); });
        if (tok.stop_requested()) { lock.unlock(); break; }
        auto params = std::move(srv->m_queue.front());
        srv->m_queue.pop();
        lock.unlock();
        srv->http_server(params.req, params.api);
        std::scoped_lock ready_lock{srv->m_ready_mutex};
        srv->m_ready_queue.push(std::move(params.req));
    }
}

// --- `server` Member Function Definitions ---
server::webapi::webapi(
    std::string _description, http::verb _verb, 
    std::vector<http::input_rule> _rules, std::vector<std::string> _roles, 
    std::function<void(http::request&)> _fn, bool _is_secure)
: description{std::move(_description)}, verb{_verb}, rules{std::move(_rules)}, 
  roles{std::move(_roles)}, fn{std::move(_fn)}, is_secure{_is_secure} {}

server::server() : 	m_signal{get_signalfd()}, 
					pod_name{get_pod_name()},
					server_start_date{util::current_timestamp()},
					ALLOWED_ORIGINS{parse_allowed_origins(env::get_str("CPP_ALLOW_ORIGINS"))}
{ }

std::string server::get_pod_name() 
{
	long host_name_max = sysconf(_SC_HOST_NAME_MAX);
    if (host_name_max <= 0) 
        host_name_max = HOST_NAME_MAX;
	
    std::string hostname(host_name_max, '\0');

    if (gethostname(hostname.data(), hostname.size()) != 0)
        return "hostname not available";

    hostname.resize(std::strlen(hostname.data()));
    
    return hostname;	
}

bool server::is_origin_allowed(const std::string& origin) {
    if (origin.empty()) {
        return true; // No CORS header needed
    }
    return ALLOWED_ORIGINS.contains(origin);
}

void server::send_options(http::request& req) {
    constexpr auto res {
        "HTTP/1.1 204 No Content\r\n"
        "Date: {:%a, %d %b %Y %H:%M:%S GMT}\r\n"
        "Access-Control-Allow-Origin: {}\r\n"
        "Access-Control-Allow-Methods: GET, POST\r\n"
        "Access-Control-Allow-Headers: {}\r\n"
        "Access-Control-Max-Age: 600\r\n"
        "Vary: origin\r\n"
        "Connection: close\r\n"
        "\r\n"
    };
    std::string _origin {req.get_header("origin")};
    req.response << std::format(res, 
		std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()),
		_origin,
		req.get_header("access-control-request-headers")
    );
}

/**
 * @brief Sends a complete HTTP error response to the client.
 *
 * This version uses std::format for efficient string construction and derives the
 * reason phrase from the status code, ensuring standard-compliant responses.
 *
 * @param req The http::request object, which contains the connection and response buffer.
 * @param status The HTTP status code to send.
 * @param body The message to be sent as the response body. If empty, the standard
 * reason phrase for the status code will be used as the body.
 */
void server::send_error(http::request& req, const http::status status, std::string_view body) {
    // Log specific errors for internal diagnostics.
    if (status == http::status::bad_request) {
        logger::log(LOGGER_SRC, "error", 
            std::format("HTTP status: {} IP: {} {} description: Bad request - {}", 
                        static_cast<int>(status), req.remote_ip, req.path, req.internals.errmsg), 
            req.get_header("x-request-id"));
    }

    // If no custom body is provided, use the standard reason phrase as the body for convenience.
    if (body.empty()) {
        body = get_reason_phrase(status);
    }

    // Conditionally generate CORS headers if the origin is allowed.
    std::string cors_headers;
    const std::string origin = req.get_header("origin");
    if (is_origin_allowed(origin)) {
        cors_headers = std::format(
            "Access-Control-Allow-Origin: {}\r\n"
            "Vary: Origin\r\n",
            origin
        );
    }
    
    // Define the response template as a constant for clarity and efficiency.
    constexpr std::string_view RESPONSE_TEMPLATE =
        "HTTP/1.1 {0} {1}\r\n"
        "Content-Length: {2}\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Date: {3:%a, %d %b %Y %H:%M:%S GMT}\r\n"
        "{4}" // Conditional CORS headers
        "Strict-Transport-Security: max-age=31536000; includeSubDomains; preload\r\n"
        "X-Frame-Options: SAMEORIGIN\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Referrer-Policy: no-referrer\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "\r\n"
        "{5}"; // Response body

    // Build the final response string using std::format.
    const auto response_str = std::format(
        RESPONSE_TEMPLATE,
        static_cast<int>(status),       // {0}: Status code (e.g., 404)
        get_reason_phrase(status),      // {1}: Reason phrase (e.g., "Not Found")
        body.length(),                  // {2}: Length of the response body
        std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()), // {3}: Current GMT date
        cors_headers,                   // {4}: CORS headers (or empty string)
        body                            // {5}: The actual response body
    );

    // Append the fully formed response to the request's response buffer.
    req.response << response_str;
}

void server::save_audit_trail(const audit_trail& at) {
    std::scoped_lock lock{m_audit_mutex};
    m_audit_queue.push(std::move(at));
    m_audit_cond.notify_one();
}

void server::execute_service(http::request& req, const std::shared_ptr<const webapi>& api_ptr) {
    if (!api_ptr) {
        throw http::resource_not_found_exception("execute_service was called with a null API handler pointer.");
    }
    req.enforce(api_ptr->verb);
    if (!api_ptr->rules.empty()) {
        req.enforce(api_ptr->rules);
    }
    if (api_ptr->is_secure) {
        req.check_security(api_ptr->roles);
        if (enable_audit) {
            std::string payload {req.isMultipart ? "multipart-form-data" : req.get_body()};
            audit_trail at{req.user_info.login, req.remote_ip, req.path, 
                        payload, req.user_info.sessionid, req.get_header("user-agent"), 
                        pod_name, req.get_header("x-request-id")};
            save_audit_trail(at);
        }
    }
    api_ptr->fn(req);
}

void server::process_request(http::request& req, const std::shared_ptr<const webapi>& api_ptr)  {
    std::string error_msg;
    try {
        if (req.method == "OPTIONS")
            send_options(req);
        else 
            execute_service(req, api_ptr);
    } catch (const http::invalid_input_exception& e) {
        error_msg = e.what();
        req.response.set_body(std::format(R"({{"status":"INVALID","validation":{{"id":"{}","description":"{}"}}}})", e.get_field_name(), e.get_error_description()));
    } catch (const http::access_denied_exception& e) { 
        error_msg = e.what();
        req.response.set_body(std::format(R"({{"status":"INVALID","validation":{{"id":"{}","description":"{}"}}}})", "_dialog_", "err.accessdenied"));
    } catch (const http::login_required_exception& e) { 
        error_msg = e.what();
        send_error(req, http::status::unauthorized, "Unauthorized");
    } catch (const http::resource_not_found_exception& e) { 
        error_msg = e.what();
        send_error(req, http::status::not_found, "Resource not found");
    } catch (const http::method_not_allowed_exception& e) { 
        error_msg = e.what();
        send_error(req, http::status::method_not_allowed, "Method not allowed"); 
    } catch (const sql::database_exception& e) { 
        error_msg = e.what();
        req.response.set_body(R"({"status":"ERROR","description":"Service error"})");
    } catch (const json::parsing_error& e) {
        error_msg = e.what();
        req.response.set_body(R"({"status":"ERROR","description":"Service error"})");
	} catch (const curl_exception& e) {
        error_msg = e.what();
        req.response.set_body(R"({"status":"ERROR","description":"Service error"})");		
    } catch (const std::exception& e) {
        error_msg = e.what();
        req.response.set_body(R"({"status":"ERROR","description":"Service error"})");
    }
    if (!error_msg.empty()) {
        req.delete_blobs();
        logger::log("service", "error", std::format("{} {}", req.path, error_msg), req.get_header("x-request-id"));
    }
}

void server::log_request(const http::request& req, double duration)  {
    constexpr auto msg {"fd={} remote-ip={} {} path={} elapsed-time={:f} user={}"};
    logger::log("access-log", "info", std::format(msg, req.fd, req.remote_ip, req.method, req.path, duration, req.user_info.login), req.get_header("x-request-id"));
}

void server::http_server(http::request& req, const std::shared_ptr<const webapi>& api_ptr)  {
    ++g_active_threads;
    auto start = std::chrono::high_resolution_clock::now();
    process_request(req, api_ptr);
    auto finish = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = finish - start;
    if (env::http_log_enabled())
        log_request(req, elapsed.count());
    g_total_time += elapsed.count();
    ++g_counter;
    --g_active_threads;
}

bool server::read_request(http::request& req, int bytes)  {
    bool first_packet {req.payload.empty()};
    req.payload.update_pos(bytes);
    if (first_packet) {
        req.parse();
        if (req.method == "GET" || req.method == "OPTIONS" || req.internals.errcode == -1)
            return true;
    }
    return req.eof();
}

int server::get_signalfd() {
    signal(SIGPIPE, SIG_IGN);
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGINT);
    sigaddset(&sigset, SIGTERM);
    sigaddset(&sigset, SIGQUIT);
    sigprocmask(SIG_BLOCK, &sigset, nullptr);
    int sfd { signalfd(-1, &sigset, 0) };
    logger::log("signal", "info", "signal interceptor registered");
    return sfd;
}

int server::get_listenfd(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    struct sockaddr_in addr{};
    addr.sin_port = htons(port);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htons(INADDR_ANY);
    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1) {
		auto error_msg = std::format("bind() failed  port: {} description: {}", port, str_error_cpp(errno));
		throw server_startup_exception(error_msg);
    }
    listen(fd, SOMAXCONN);
    logger::log("epoll", "info", std::format("listen non-blocking socket FD: {} port: {}", fd, port));
    return fd;
}

void server::epoll_add_event(int fd, int epoll_fd, uint32_t event_flags) {
    epoll_event event;
    event.data.fd = fd;
    event.events = event_flags;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
}

std::string server::get_socket_error(const int& fd) {
    int error = 0;
    socklen_t errlen = sizeof(error);
    
    // getsockopt is thread-safe.
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &errlen) == 0 && error != 0) {
        // std::error_code is the thread-safe C++ way to handle system error codes.
        std::error_code ec(error, std::system_category());
        return ec.message();
    }
    
    return "`no error message available";
}

void server::epoll_handle_error(epoll_event& ev) {
    --g_connections;
    if (auto it = buffers.find(ev.data.fd); it == buffers.end()) { 
		logger::log("epoll", "error", std::format("EPOLLERR unable to retrieve request object for fd {}", get_fd(ev)));
    } else {
        http::request& req = it->second;
        logger::log("epoll", "error", std::format("error on connection for FD: {} {} - closing it", req.fd, get_socket_error(req.fd)));
        if (close(req.fd) == -1)
            logger::log("epoll", "error", std::format("close FAILED for FD: {} description: {}", req.fd, str_error_cpp(errno)));
        buffers.erase(ev.data.fd);
    }
}

void server::epoll_handle_close(epoll_event& ev) {
    --g_connections;
    if (auto it = buffers.find(ev.data.fd); it == buffers.end()) { 
        logger::log("epoll", "error", std::format("EPOLLRDHUP unable to retrieve request object for fd {}", get_fd(ev)));
    } else {
        http::request& req = it->second;
        if (close(req.fd) == -1)
            logger::log("epoll", "error", std::format("close FAILED for FD: {} description: {}", req.fd, str_error_cpp(errno)));
        buffers.erase(ev.data.fd);
    }
}

void server::epoll_handle_connect(const int& listen_fd, const int& epoll_fd) {
    while (true) {
        struct sockaddr addr;
        socklen_t len = sizeof(addr);
        int fd { accept4(listen_fd, &addr, &len, SOCK_NONBLOCK) };
        if (fd == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) 
                logger::log("epoll", "error", std::format("connection accept FAILED for epoll FD: {} description: {}", epoll_fd, str_error_cpp(errno)));
            return;
        }
        ++g_connections;
		auto remote_ip{get_peer_ip_ipv4(fd)};
        if (auto [iter, success] {buffers.try_emplace(fd, epoll_fd, fd, remote_ip)}; success) {
            epoll_event ev; 
            ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
            ev.data.fd = fd;
            epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
        } else {
            logger::log("epoll", "error", std::format("error creating a new request object into the hashmap with fd: {}", fd));
            break;
        }
    }
}

void server::epoll_abort_request(http::request& req, const http::status status_code, const std::string& msg_) {
    std::string msg {"Bad request"};
    if (status_code == http::status::not_found) {
        logger::log("epoll", "error", std::format("API not found: {} from IP {}", req.path, req.remote_ip), req.get_header("x-request-id"));
        msg = "Resource not found";
    }
	if (!msg_.empty())
		msg = msg_;
	if (status_code == http::status::forbidden) 
        logger::log("security", "warn", std::format("{}: {} from IP {}", msg, req.path, req.remote_ip), req.get_header("x-request-id"));
    send_error(req, status_code, msg);
    epoll_event event;
    event.events = EPOLLOUT | EPOLLET | EPOLLRDHUP;
    event.data.fd = req.fd;
    epoll_ctl(req.epoll_fd, EPOLL_CTL_MOD, req.fd, &event);
}

void server::check_ready_queue()  {
    std::lock_guard lock(m_ready_mutex);
    while (!m_ready_queue.empty()) {
        http::request req = std::move(m_ready_queue.front());
        m_ready_queue.pop();
        const auto fd {req.fd};
        const auto epoll_fd {req.epoll_fd};
        if (auto [iter, success] = buffers.insert_or_assign(fd, std::move(req)); success) {
			epoll_event event;
			event.events = EPOLLOUT | EPOLLET | EPOLLRDHUP; 
			event.data.fd = fd; 
			if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1) 
				logger::log("epoll", "error", std::format("epoll_ctl ADD failed for FD: {} description: {}", fd, str_error_cpp(errno)));
		}
    }
}

void server::producer(worker_params& wp) {
    std::scoped_lock lock{m_mutex};
    m_queue.push(std::move(wp));
    m_cond.notify_one();
}

void server::run_async_task(http::request& req) {
    if (req.internals.errcode) {
        req.delete_blobs();
        epoll_abort_request(req, http::status::bad_request);
        return;
    }
	if (!is_origin_allowed(req.origin)) {
		epoll_abort_request(req, http::status::forbidden, std::format("CORS origin denied: {}", req.origin));
		return;
	}
    if (req.path.ends_with("/api/ping")) {
        epoll_send_ping(req);
        return;
    }
    if (req.path.ends_with("/api/sysinfo")) {
        epoll_send_sysinfo(req);
        return;
    }
    if (auto obj = webapi_catalog.find(req.path); obj != webapi_catalog.end()) {
        epoll_ctl(req.epoll_fd, EPOLL_CTL_DEL, req.fd, nullptr);
        auto request_node = buffers.extract(req.fd);
        worker_params wp {std::move(request_node.mapped()), obj->second};
        producer(wp);
    } else {
        epoll_abort_request(req, http::status::not_found);
    }
}

void server::epoll_send_ping(http::request& req) {
    req.response.set_body(R"({"status": "OK"})");
    epoll_event event;
    event.events = EPOLLOUT | EPOLLET | EPOLLRDHUP;
    event.data.fd = req.fd;
    epoll_ctl(req.epoll_fd, EPOLL_CTL_MOD, req.fd, &event);
}

void server::epoll_send_sysinfo(http::request& req)  {
    static const auto pool_size {env::pool_size()};
    static const size_t total_ram {util::get_total_memory()};
    const size_t requests_total = g_counter.load(std::memory_order_relaxed);
    const double total_processing_time = g_total_time.load(std::memory_order_relaxed);
    const int active_threads_count = g_active_threads.load(std::memory_order_relaxed);
    const size_t connections_count = g_connections.load(std::memory_order_relaxed);
    const auto mem_usage {util::get_memory_usage()};
    const double avg_time_per_request = (requests_total > 0) ? (total_processing_time / requests_total) : 0.0;
    constexpr auto json_template {
        R"({{"status":"OK","data":[{{"pod":"{}","startDate":"{}","totalRequests":{},"avgTimePerRequest":{:f},"connections":{},"activeThreads":{},"poolSize":{},"totalRam":{},"memoryUsage":{}}}]}})"
    };
    req.response.set_body(std::format(json_template, pod_name, server_start_date, 
        requests_total, avg_time_per_request, connections_count, 
        active_threads_count, pool_size, total_ram, mem_usage));
    epoll_event event;
    event.events = EPOLLOUT | EPOLLET | EPOLLRDHUP;
    event.data.fd = req.fd;
    epoll_ctl(req.epoll_fd, EPOLL_CTL_MOD, req.fd, &event);
}

void server::epoll_handle_read(http::request& req)  {
    while (true) {
        int count = read(req.fd, req.payload.data(), req.payload.available_size());
        if (count == 0) break;
        if (count == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                logger::log("epoll", "error", std::format("read failed for FD: {} description: {}", req.fd, str_error_cpp(errno)));
                close(req.fd);
            }
            return;
        }
        if (count > 0 && read_request(req, count)) {
            run_async_task(req);
            return;
        }
    }
}

void server::epoll_handle_write(http::request& req)  {
	if (req.response.write(req.fd)) {
        epoll_event event;
        event.events = EPOLLET | EPOLLRDHUP;
		event.data.fd = req.fd;
        epoll_ctl(req.epoll_fd, EPOLL_CTL_MOD, req.fd, &event);
	}
}

void server::epoll_handle_IO(epoll_event& ev) {
    if (auto it = buffers.find(ev.data.fd); it == buffers.end()) {
        logger::log("epoll", "error", std::format("epoll_handle_IO() - unable to retrieve request object for fd {}", get_fd(ev)));
        return;
    } else {
		http::request& req = it->second;
		if (ev.events & EPOLLIN) {
			epoll_handle_read(req);
		} else {
			epoll_handle_write(req);
		}
	}
}


void server::epoll_loop(int listen_fd, int epoll_fd)  {
    constexpr int MAXEVENTS = 1024;
    constexpr int EPOLL_TIMEOUT_MS = 5;
    std::array<epoll_event, MAXEVENTS> events;
    while (true) {
        int n_events = epoll_wait(epoll_fd, events.data(), MAXEVENTS, EPOLL_TIMEOUT_MS);
        check_ready_queue();
        if (n_events < 0) continue;
        for (int i = 0; i < n_events; i++) {
            if (events[i].events & EPOLLRDHUP || events[i].events & EPOLLHUP) {
                epoll_handle_close(events[i]);
            } else if (events[i].events & EPOLLERR) {
                epoll_handle_error(events[i]);
            } else if (m_signal == events[i].data.fd) {
                logger::log("signal", "info", std::format("stop signal received via epoll: {}", get_signal_name(m_signal)));
                return;
            } else if (listen_fd == events[i].data.fd) {
                epoll_handle_connect(listen_fd, epoll_fd);
            } else {
                epoll_handle_IO(events[i]);
            }
        }
    }
}

void server::start_epoll(int port)  {
    file_descriptor epoll_fd {epoll_create1(0)};
    logger::log("epoll", "info", std::format("starting epoll FD: {}", epoll_fd));
    file_descriptor listen_fd {get_listenfd(port)};
    epoll_add_event(listen_fd, epoll_fd, EPOLLIN);
    epoll_add_event(m_signal, epoll_fd, EPOLLIN);
    epoll_loop(listen_fd, epoll_fd);
	logger::log("epoll", "info", "closing file descriptors");
}

void server::print_server_info()  {
    logger::log("env", "info", std::format("port: {}", env::port()));
    logger::log("env", "info", std::format("pool size: {}", env::pool_size()));
    logger::log("env", "info", std::format("login log: {}", env::login_log_enabled()));
    logger::log("env", "info", std::format("http log: {}", env::http_log_enabled()));
    logger::log("env", "info", std::format("jwt exp: {}", env::jwt_expiration()));
    logger::log("env", "info", std::format("enable audit: {}", env::enable_audit()));
    logger::log("server", "info", std::format("Pod: {} PID: {} starting {}-{}", pod_name, getpid(), SERVER_VERSION, CPP_BUILD_DATE));
    logger::log("server", "info", std::format("hardware threads: {} GCC: {}", std::thread::hardware_concurrency(), __VERSION__));
}

void server::register_diagnostic_services() {
    register_webapi(webapi_path("/api/version"), "Get API-Server version and build date", http::verb::GET, 
        [this](http::request& req) {
            constexpr auto json {R"({{"status":"OK","data":[{{"pod":"{}","server":"{}-{}","compiler":"{}"}}]}})"};
            req.response.set_body(std::format(json, pod_name, SERVER_VERSION, CPP_BUILD_DATE, __VERSION__));
        }, false);

    register_webapi(webapi_path("/api/sysdate"), "Return server timestamp in local timezone", http::verb::GET,
        [this](http::request& req) {
            const auto now {std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now())};
            const auto server_ts {std::format("{:%FT%T}", std::chrono::get_tzdb().current_zone()->to_local(now))};
            constexpr auto json {R"({{"status": "OK", "data":[{{"pod":"{}","time":"{}"}}]}})"};
            req.response.set_body(std::format(json, pod_name, server_ts));
        }, false);

    register_webapi(webapi_path("/api/metrics"), "Return metrics in Prometheus format", http::verb::GET,
        [this](http::request& req) {
            const size_t requests_total = g_counter.load(std::memory_order_relaxed);
            const double total_processing_time = g_total_time.load(std::memory_order_relaxed);
            const int active_threads_count = g_active_threads.load(std::memory_order_relaxed);
            const size_t connections_count = g_connections.load(std::memory_order_relaxed);
            const double avg_time = (requests_total > 0) ? total_processing_time / requests_total : 0.0;
            static const auto pool_size {env::pool_size()};
            std::string body;
            body.reserve(512);
            constexpr auto str_tpl {"# HELP {0} {1}.\n# TYPE {0} gauge\n{0}{{pod=\"{2}\"}} {3}\n"};
            constexpr auto flt_tpl {"# HELP {0} {1}.\n# TYPE {0} gauge\n{0}{{pod=\"{2}\"}} {3:f}\n"};
            body.append(std::format(str_tpl, "cpp_requests_total", "The number of HTTP requests processed", pod_name, requests_total));
            body.append(std::format(str_tpl, "cpp_connections_current", "Current client tcp-ip connections", pod_name, connections_count));
            body.append(std::format(str_tpl, "cpp_active_threads_current", "Current active threads", pod_name, active_threads_count));
            body.append(std::format(str_tpl, "cpp_pool_size", "Thread pool size", pod_name, pool_size));
            body.append(std::format(flt_tpl, "cpp_request_duration_avg_seconds", "Average request processing time in seconds", pod_name, avg_time));
            req.response.set_body(body, "text/plain; version=0.0.4");
        }, false);
}

void server::prebuilt_services() {
	
    logger::log("server", "info", "registering built-in diagnostic and security services...");
    
	register_diagnostic_services();
    register_webapi(webapi_path("/api/login"), "Default Login service using a database", http::verb::POST, 
        rules{{"username", http::field_type::STRING, true}, {"password", http::field_type::STRING, true}}, roles{},
        [](http::request& req) {
            std::string login{req.get_param("username")};
            std::string password{req.get_param("password")};
            const std::string sid {http::get_uuid()};
            if (const auto lr {login::bind(login, password, sid, req.remote_ip)}; lr.ok()) {
                const std::string token {jwt::get_token(sid, login, lr.get_email(), lr.get_roles())};
                constexpr auto json {R"({{"status":"OK","data":[{{"displayname":"{}","token_type":"bearer","id_token":"{}"}}]}})"};
                const std::string login_ok {std::format(json, lr.get_display_name(), token)};
                req.response.set_body(login_ok);
                if (env::login_log_enabled())
                    logger::log("security", "info", std::format("login OK - SID: {} user: {} IP: {} token: {} roles: {}", sid, login, req.remote_ip, token, lr.get_roles()), req.get_header("x-request-id"));
            } else {
                logger::log("security", "warn", std::format("login failed - user: {} IP: {}", login, req.remote_ip), req.get_header("x-request-id"));
                constexpr auto json = R"({{"status":"INVALID","validation":{{"id":"login","code":"{}","description":"{}"}}}})";
                req.response.set_body(std::format(json, lr.get_error_code(), lr.get_error_description()));
            }
        }, false);
		
    register_webapi(webapi_path("/api/totp"), "Validate TOTP token given a base32 encoded secret", http::verb::POST,
        rules{{"duration", http::field_type::INTEGER, true}, {"token", http::field_type::STRING, true}, {"secret", http::field_type::STRING, true}}, roles{},
        [](http::request& req) {
            const int s{std::stoi(req.get_param("duration"))};
            if (auto[result, error_msg]{is_valid_token(s, req.get_param("token"), req.get_param("secret"))}; result)
                req.response.set_body(R"({"status":"OK"})");
            else
                req.response.set_body(std::format(R"({{"status":"INVALID","validation":{{"id":"token","description":"{}"}}}})", error_msg));
        }, false);
}

void server::shutdown() {
	logger::log("server", "info", std::format("{} shutting down...", pod_name));
	for (const auto& s: m_stops) {
        s.request_stop();
        {
            std::scoped_lock lock {m_mutex};
            m_cond.notify_all();
        }
    }
    for (auto& t:m_pool)
        t.join();
    m_audit_stop.request_stop();
    m_audit_cond.notify_all();
    m_audit_engine.join();
	m_pool.clear();
}

void server::start() {
	auto init_time = std::chrono::high_resolution_clock::now();
    prebuilt_services();
    enable_audit = env::enable_audit();
    print_server_info();
    const auto pool_size {env::pool_size()};
    const auto port {env::port()};

    m_stops.resize(pool_size);
    for (int i = 0; i < pool_size; i++) {
        m_stops[i] = std::stop_source();
        m_pool.emplace_back(consumer, m_stops[i].get_token(), this);
    }
    m_audit_stop = std::stop_source();
    m_audit_engine = std::jthread(audit, m_audit_stop.get_token(), this);
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - init_time).count();
    logger::log("server", "info", std::format("server started in {} microseconds", elapsed));
	try {
		start_epoll(port); //blocks here
	} catch (const server_startup_exception& e) {
		logger::log("epoll", "error", e.what());
		shutdown();
		return;
	}
	shutdown();
	logger::log("server", "info", "all resources were released, server stopped");
}
