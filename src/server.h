/*
 * server - epoll single-thread, workers-pool server
 *
 *  Created on: July 18, 2023
 *      Author: Martin Cordova cppserver@martincordova.com - https://cppserver.com
 *      Disclaimer: some parts of this library may have been taken from sample code publicly available
 *		and written by third parties. Free to use in commercial projects, no warranties and no responsabilities assumed 
 *		by the author, use at your own risk. By using this code you accept the forementioned conditions.
 */
#ifndef SERVER_H_
#define SERVER_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <sys/stat.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/signalfd.h>
#include <netinet/tcp.h>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <stop_token>
#include <unordered_map>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <charconv>
#include <functional>
#include <chrono>
#include <format>
#include "util.h"
#include "env.h"
#include "logger.h"
#include "login.h"
#include "sql.h"
#include "httputils.h"
#include "jwt.h"
#include "email.h"

constexpr char SERVER_VERSION[] = "API-Server++ v1.3.1";
constexpr const char* LOGGER_SRC {"server"};

struct webapi_path
{
	public:
		consteval explicit webapi_path(std::string_view _path): m_path{_path} 
		{
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
		
	std::string get() const noexcept
	{
		return std::string(m_path);
	}

	private: 
		std::string_view m_path;
};

constexpr void audit_task(auto& params) noexcept
{
		constexpr auto tpl {"sp_audit_trail '{}', '{}', '{}','{}', '{}', '{}', '{}', '{}'"};
		const auto sql {std::format(tpl, params.path, params.username, params.remote_ip, 
										util::encode_sql(params.payload), params.sessionid, 
										params.useragent, params.nodename, params.x_request_id)};
		try {
			sql::exec_sql("CPP_AUDITDB", sql);
		} catch (const sql::database_exception& e) {
			logger::log("audit", "error", std::format("could not save audit record in database: {}", e.what()));
		}
};

constexpr auto audit = [](std::stop_token tok, auto srv) noexcept 
{
	logger::log("pool", "info", "starting audit thread");
	
	while(!tok.stop_requested())
	{
		//prepare lock
		std::unique_lock lock{srv->m_audit_mutex}; 
		
		//release lock, reaquire it if conditions met
		srv->m_audit_cond.wait(lock, [&tok, &srv]() { return (!srv->m_audit_queue.empty() || tok.stop_requested()); }); 
		
		//stop requested?
		if (tok.stop_requested()) { lock.unlock(); break; }
		
		//get task
		auto params = srv->m_audit_queue.front();
		srv->m_audit_queue.pop();
		lock.unlock();
		
		audit_task(params);
	}
	
	//ending task - free resources
	logger::log("pool", "info", "stopping audit thread");
};

constexpr auto consumer = [](std::stop_token tok, auto srv) noexcept 
{
	while(!tok.stop_requested())
	{
		//prepare lock
		std::unique_lock lock{srv->m_mutex}; 
		
		//release lock, reaquire it if conditions met
		srv->m_cond.wait(lock, [&tok, &srv]() { return (!srv->m_queue.empty() || tok.stop_requested()); }); 
		
		//stop requested?
		if (tok.stop_requested()) { lock.unlock(); break; }
		
		//get task
		auto params = srv->m_queue.front();
		srv->m_queue.pop();
		lock.unlock();
		
		//run task
		srv->http_server(params.req, params.api);
		
		epoll_event event;
		event.events = EPOLLOUT | EPOLLET | EPOLLRDHUP;
		event.data.ptr = &params.req;
		epoll_ctl(params.req.epoll_fd, EPOLL_CTL_MOD, params.req.fd, &event);
		
	}
	//ending task - free resources
};	

struct server
{
	struct webapi 
	{
		std::string description;
		http::verb verb;
		std::vector<http::input_rule> rules;
		std::vector<std::string> roles;
		std::function<void(http::request&)> fn;
		bool is_secure {true};
		webapi(	
				const std::string& _description,
				const http::verb _verb,
				const std::vector<http::input_rule>& _rules,
				const std::vector<std::string>& _roles,
				const std::function<void(http::request&)>& _fn,
				bool _is_secure
			): description{_description}, verb{_verb}, rules{_rules}, roles{_roles}, fn{_fn}, is_secure{_is_secure}
		{ }
	};
	
	std::unordered_map<std::string, webapi, util::string_hash, std::equal_to<>> webapi_catalog;
	std::unordered_map<int, http::request> buffers;
	
	std::atomic<size_t> g_counter{0};
	std::atomic<double> g_total_time{0};
	std::atomic<int>	g_active_threads{0};
	std::atomic<size_t> g_connections{0};

	struct worker_params {
		http::request& req;
		const webapi& api;
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

	std::queue<worker_params> m_queue;
	std::condition_variable m_cond;
	std::mutex m_mutex;
	std::condition_variable m_audit_cond;
	std::queue<audit_trail> m_audit_queue;
	std::mutex m_audit_mutex;
	int m_signal {get_signalfd()};
	
	constexpr std::string get_pod_name() {
		std::array<char, 128> hostname{0};
		gethostname(hostname.data(), hostname.size());
		std::string temp;
		temp.append(hostname.data());
		return temp;
	}
	
	const std::string pod_name{get_pod_name()};
	const std::string server_start_date {std::format("{:%FT%T}", std::chrono::get_tzdb().current_zone()->to_local(std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now())))};
	bool enable_audit {false};
	
	std::chrono::time_point<std::chrono::high_resolution_clock> _start_init{};
	
	server() {
			_start_init = std::chrono::high_resolution_clock::now();
	}
	
	constexpr void send_options(http::request& req)
	{
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
		if (_origin.empty()) {
			logger::log("server", "error", "send_options() - origin is empty, setting its value to *");
			_origin = "*";
		}
		
		req.response << std::format(res, 
			std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()),
			_origin,
			req.get_header("access-control-request-headers")
		);
	}

	constexpr void send_error(http::request& req, const int status, std::string_view msg) 
	{
		if (status == 400)
			logger::log(LOGGER_SRC, "error", std::format("HTTP status: {} IP: {} {} description: Bad request - {}", status, req.remote_ip, req.path, req.internals.errmsg), req.get_header("x-request-id"));
		
		constexpr auto res {
			"HTTP/1.1 {} {}\r\n"
			"Content-Length: {}\r\n"
			"Content-Type: text/plain\r\n"
			"Date: {:%a, %d %b %Y %H:%M:%S GMT}\r\n"
			"Access-Control-Allow-Origin: {}\r\n"
			"Access-Control-Allow-Credentials: true\r\n"
			"Strict-Transport-Security: max-age=31536000; includeSubDomains; preload;\r\n"
			"X-Frame-Options: SAMEORIGIN\r\n"
			"Connection: close\r\n"
			"\r\n"
			"{}"
		};
		
		req.response << std::format(res, 
			status,
			msg,
			msg.size(),
			std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()),
			req.get_header("origin"),
			msg
		);
	}

	constexpr void save_audit_trail (const audit_trail& at)
	{
		std::scoped_lock lock{m_audit_mutex};
		m_audit_queue.push(at);
		m_audit_cond.notify_all();		
	}
	
	constexpr void execute_service(http::request& req, const webapi& api)
	{
		req.enforce(api.verb);
		if (!api.rules.empty())
			req.enforce(api.rules);
		if (api.is_secure) {
			req.check_security(api.roles);
			if (enable_audit) {
				std::string payload {req.isMultipart ? "multipart-form-data" : req.get_body()};
				audit_trail at{req.user_info.login, req.remote_ip, req.path, 
							payload, req.user_info.sessionid, req.get_header("user-agent"), pod_name, req.get_header("x-request-id")};
				save_audit_trail(at);
			}
		}
		api.fn(req);
	}

	constexpr void process_request(http::request& req, const webapi& api) noexcept
	{
		std::string error_msg;
		try {
			if (req.method == "OPTIONS") //preflight request
				send_options(req);
			else 
				execute_service(req, api); //run lambda
		} catch (const http::invalid_input_exception& e) {
			error_msg = e.what();
			req.response.set_body(std::format(R"({{"status":"INVALID","validation":{{"id":"{}","description":"{}"}}}})", e.get_field_name(), e.get_error_description()));
		} catch (const http::access_denied_exception& e) { 
			error_msg = e.what();
			req.response.set_body(std::format(R"({{"status":"INVALID","validation":{{"id":"{}","description":"{}"}}}})", "_dialog_", "err.accessdenied"));
		} catch (const http::login_required_exception& e) { 
			error_msg = e.what();
			send_error(req, 401, "Unauthorized");
		} catch (const http::resource_not_found_exception& e) { 
			error_msg = e.what();
			send_error(req, 404, "Resource not found");
		} catch (const http::method_not_allowed_exception& e) { 
			error_msg = e.what();
			send_error(req, 405, "Method not allowed"); 
		} catch (const sql::database_exception& e) { 
			error_msg = e.what();
			req.response.set_body(R"({"status":"ERROR","description":"Service error"})");
		} catch (const json::invalid_json_exception& e) {
			error_msg = e.what();
			req.response.set_body(R"({"status":"ERROR","description":"Service error"})");
		}
		if (!error_msg.empty()) {
			req.delete_blobs(); //in case request left orphan blobs
			logger::log("service", "error", std::format("{} {}", req.path, error_msg), req.get_header("x-request-id"));
		}
	}

	constexpr void log_request(const http::request& req, double duration) noexcept
	{
		constexpr auto msg {"fd={} remote-ip={} {} path={} elapsed-time={:f} user={}"};
		logger::log("access-log", "info", std::format(msg, req.fd, req.remote_ip, req.method, req.path, duration, req.user_info.login), req.get_header("x-request-id"));
	}

	constexpr void http_server (http::request& req, const webapi& api) noexcept
	{
		++g_active_threads;	

		auto start = std::chrono::high_resolution_clock::now();

		process_request(req, api);
		
		auto finish = std::chrono::high_resolution_clock::now();
		std::chrono::duration <double>elapsed = finish - start;				

		if (env::http_log_enabled())
			log_request(req, elapsed.count());

		g_total_time += elapsed.count();
		++g_counter;
		--g_active_threads;
	}

	constexpr bool read_request(http::request& req, int bytes) noexcept
	{
		bool first_packet { (req.payload.empty()) ? true : false };
		req.payload.update_pos(bytes);
		if (first_packet) {
			req.parse();
			if (req.method == "GET" || req.method == "OPTIONS" || req.internals.errcode ==  -1)
				return true;
		}
		if (req.eof())
			return true;
		return false;
	}

	constexpr int get_signalfd() noexcept 
	{
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

	constexpr int get_listenfd(int port) noexcept 
	{
		int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		int on = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
		fcntl(fd, F_SETFD, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_port = htons(port);
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htons(INADDR_ANY);
		
		if (int rc = bind(fd, (struct sockaddr *) &addr, sizeof(addr)); rc == -1) {
			logger::log("epoll", "error", std::format("bind() failed  port: {} description: {}", port, strerror(errno)));
			exit(-1);
		}
		listen(fd, SOMAXCONN);
		logger::log("epoll", "info", std::format("listen socket FD: {} port: {}", fd, port));
		return fd;
	}

	constexpr void epoll_add_event(int fd, int epoll_fd, uint32_t event_flags) noexcept
	{
		epoll_event event;
		event.data.fd = fd;
		event.events = event_flags;
		epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
	}

	constexpr std::string get_socket_error(const int& fd) {
		int error = 0;
		socklen_t errlen = sizeof(error);
		std::string errdesc{""};
		if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &errlen) == 0 && error != 0)
			errdesc.append(std::strerror(error));
		return errdesc;
	}

	constexpr void epoll_handle_error(epoll_event& ev) noexcept
	{
		--g_connections;
		if (ev.data.ptr == nullptr)
			logger::log("epoll", "error", "EPOLLERR epoll data ptr is null - unable to retrieve request object");
		else {
			http::request& req = *static_cast<http::request*>(ev.data.ptr);
			logger::log("epoll", "error", std::format("error on connection for FD: {} {} - closing it", req.fd, get_socket_error(req.fd)));
			if (int rc {close(req.fd)}; rc == -1)
				logger::log("epoll", "error", std::format("close FAILED for FD: {} description: {}", req.fd, std::strerror(errno)));
		}
	}

	constexpr void epoll_handle_close(epoll_event& ev) noexcept
	{
		--g_connections;
		if (ev.data.ptr == nullptr) 
			logger::log("epoll", "error", "EPOLLRDHUP epoll data ptr is null - unable to retrieve request object");
		else {
			http::request& req = *static_cast<http::request*>(ev.data.ptr);
			if (int rc {close(req.fd)}; rc == -1)
				logger::log("epoll", "error", std::format("close FAILED for FD: {} description: {}", req.fd, strerror(errno)));
		}
	}

	constexpr void epoll_handle_connect(int listen_fd, int epoll_fd) noexcept
	{
		struct sockaddr addr;
		socklen_t len;
		len = sizeof addr;
		int fd { accept4(listen_fd, &addr, &len, SOCK_NONBLOCK) };
		if (fd == -1) {
			logger::log("epoll", "error", std::format("connection accept FAILED for epoll FD: {} description: {}", epoll_fd, strerror(errno)));
		} else {
			++g_connections;
			const char* remote_ip = inet_ntoa(((struct sockaddr_in*)&addr)->sin_addr);
			epoll_event event;
			if (buffers.contains(fd)) {
				http::request& req = buffers[fd];
				req.clear();
				req.remote_ip = std::string(remote_ip);
				req.fd = fd;
				req.epoll_fd = epoll_fd;
				event.data.ptr = &req;
			} else {
				auto [iter, success] {buffers.try_emplace(fd, epoll_fd, fd, remote_ip)};
				if (success)
					event.data.ptr = &iter->second;
				else {
					logger::log("epoll", "error", std::format("error creating a new request object into the hashmap with fd: {}", fd));
					return;
				}
			}
			event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
			epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
		}
	}
	
	constexpr void epoll_abort_request(http::request& req, const int status_code) noexcept 
	{
		std::string msg {"Bad request"};
		if (status_code==404) {
			logger::log("epoll", "error", std::format("API not found: {} from IP {}", req.path, req.remote_ip), req.get_header("x-request-id"));
			msg = "Resource not found";
		}
		send_error(req, status_code, msg);
		epoll_event event;
		event.events = EPOLLOUT | EPOLLET | EPOLLRDHUP;
		event.data.ptr = &req;
		epoll_ctl(req.epoll_fd, EPOLL_CTL_MOD, req.fd, &event);		
	}


	constexpr void producer(const worker_params& wp) noexcept
	{
		std::scoped_lock lock{m_mutex};
		m_queue.push(wp);
		m_cond.notify_all();
	}

	constexpr void run_async_task(http::request& req) noexcept
	{
		if (req.internals.errcode) {
			req.delete_blobs();
			epoll_abort_request(req, 400);
			return;
		} 
				
		if (auto obj = webapi_catalog.find(req.path); obj != webapi_catalog.end()) 
		{
			worker_params wp {req, obj->second};
			producer(wp);
		} else 
			epoll_abort_request(req, 404);
	}

	constexpr void epoll_handle_read(epoll_event& ev) noexcept
	{
		http::request& req = *static_cast<http::request*>(ev.data.ptr);
		while (true) 
		{
			int count = read(req.fd, req.payload.data(), req.payload.available_size());
			if (count == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
				return;
			if (count > 0 && read_request(req, count)) {
					run_async_task(req);
					break;
			}
		}
	}
	
	constexpr void epoll_handle_write(epoll_event& ev) noexcept
	{
		http::request& req = *static_cast<http::request*>(ev.data.ptr);
		if (req.response.write(req.fd)) {
			epoll_event event;
			event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
			event.data.ptr = &req;
			epoll_ctl(req.epoll_fd, EPOLL_CTL_MOD, req.fd, &event);
		}
	}

	constexpr void epoll_handle_IO(epoll_event& ev) noexcept
	{
		if (ev.data.ptr == nullptr) {
			logger::log("epoll", "error", "epoll_handle_IO() - epoll data ptr is null");
			return;
		}
		if (ev.events & EPOLLIN) 
			epoll_handle_read(ev);
		else 
			epoll_handle_write(ev);
	}

	constexpr void epoll_loop(int listen_fd, int epoll_fd) noexcept
	{
		constexpr int MAXEVENTS = 64;
		std::array<epoll_event, MAXEVENTS> events;

		while (true)
		{
			int n_events = epoll_wait(epoll_fd, events.data(), MAXEVENTS, -1);
			for (int i = 0; i < n_events; i++)
			{
				if (events[i].events & EPOLLRDHUP || events[i].events & EPOLLHUP)
				{
					epoll_handle_close(events[i]);
					continue;
				}
				else if (events[i].events & EPOLLERR)
				{
					epoll_handle_error(events[i]);
					continue;
				}
				else if (m_signal == events[i].data.fd) //shutdown
				{
					logger::log("signal", "info", "stop signal received via epoll");
					return;
				}
				else if (listen_fd == events[i].data.fd) // new connection.
				{
					epoll_handle_connect(listen_fd, epoll_fd);
				}
				else // read/write
				{
					epoll_handle_IO(events[i]);
				}
			}
		}		
	}

	constexpr void start_epoll(int port) noexcept 
	{
		int epoll_fd {epoll_create1(0)};
		logger::log("epoll", "info", std::format("starting epoll FD: {}", epoll_fd));

		int listen_fd {get_listenfd(port)};
		
		epoll_add_event(listen_fd, epoll_fd, EPOLLIN);
		epoll_add_event(m_signal, epoll_fd, EPOLLIN);

		epoll_loop(listen_fd, epoll_fd);

		close(listen_fd);
		logger::log("epoll", "info", std::format("closing listen socket FD: {}", listen_fd));
		close(epoll_fd);
		logger::log("epoll", "info", std::format("closing epoll FD: {}", epoll_fd));
	}

	constexpr void print_server_info() noexcept
	{
		logger::log("env", "info", std::format("port: {}", env::port()));
		logger::log("env", "info", std::format("pool size: {}", env::pool_size()));
		logger::log("env", "info", std::format("login log: {}", env::login_log_enabled()));
		logger::log("env", "info", std::format("http log: {}", env::http_log_enabled()));
		logger::log("env", "info", std::format("jwt exp: {}", env::jwt_expiration()));
		logger::log("env", "info", std::format("enable audit: {}", env::enable_audit()));
		logger::log("server", "info", std::format("Pod: {} PID: {} starting {}-{}", pod_name, getpid(), SERVER_VERSION, CPP_BUILD_DATE));
		logger::log("server", "info", std::format("hardware threads: {} GCC: {}", std::thread::hardware_concurrency(), __VERSION__));
	}

	constexpr void register_diagnostic_services()
	{
		register_webapi
		(
			webapi_path("/api/ping"), 
			"Healthcheck service for Ingress and Load Balancer",
			http::verb::GET, 
			[](http::request& req) 
			{
				req.response.set_body( R"({"status": "OK"})" );
			},
			false /* no security */
		);
				
		register_webapi
		(
			webapi_path("/api/version"), 
			"Get API-Server version and build date",
			http::verb::GET, 
			[this](http::request& req) 
			{
				constexpr auto json {R"({{"status": "OK", "data":[{{"pod": "{}", "server": "{}-{}","compiler":"{}"}}]}})"};
				req.response.set_body(std::format(json, pod_name, SERVER_VERSION, CPP_BUILD_DATE, __VERSION__));
			},
			false /* no security */
		);

		register_webapi
		(
			webapi_path("/api/sysdate"), 
			"Return server timestamp in local timezone",
			http::verb::GET, 
			[this](http::request& req) 
			{
				const auto now {std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now())};
				const auto server_ts {std::format("{:%FT%T}", std::chrono::get_tzdb().current_zone()->to_local(now))};
				constexpr auto json {R"({{"status": "OK", "data":[{{"pod":"{}","time":"{}"}}]}})"};
				req.response.set_body(std::format(json, pod_name, server_ts));
			},
			false /* no security */
		);

		register_webapi
		(
			webapi_path("/api/sysinfo"), 
			"Return global system diagnostics",
			http::verb::GET, 
			[this](http::request& req) 
			{
				static const auto pool_size {env::pool_size()};
				const double avg{ ( g_counter > 0 ) ? g_total_time / g_counter : 0 };
				const size_t _counter = g_counter;
				const int _active_threads = g_active_threads;
				const size_t _connections = g_connections;
				static const auto ram {util::get_total_memory()};
				const auto mem_usage {util::get_memory_usage()};
				constexpr auto json {R"({{"status": "OK", "data":[{{"pod":"{}","startDate":"{}","totalRequests":{},"avgTimePerRequest":{:f},"connections":{},"activeThreads":{},"poolSize":{},"totalRam":{},"memoryUsage":{}}}]}})"};
				req.response.set_body(std::format(json, pod_name, server_start_date, _counter, avg, _connections, _active_threads, pool_size, ram, mem_usage));
			},
			false /* no security */
		);
		
		register_webapi
		(
			webapi_path("/api/metrics"), 
			"Return metrics in Prometheus format",
			http::verb::GET, 
			[this](http::request& req) 
			{
				std::string body;
				body.reserve(2047);
				const double avg{ ( g_counter > 0 ) ? g_total_time / g_counter : 0 };
				size_t _counter = g_counter;
				int _active_threads = g_active_threads;
				size_t _connections = g_connections;
				static const auto pool_size {env::pool_size()};
				constexpr auto str {"# HELP {0} {1}.\n# TYPE {0} counter\n{0}{{pod=\"{2}\"}} {3}\n"};
				constexpr auto str_avg {"# HELP {0} {1}.\n# TYPE {0} counter\n{0}{{pod=\"{2}\"}} {3:f}\n"};
				body.append(std::format(str, "cpp_requests_total", "The number of HTTP requests processed by this container", pod_name, _counter));
				body.append(std::format(str, "cpp_connections", "Client tcp-ip connections", pod_name, _connections));
				body.append(std::format(str, "cpp_active_threads", "Active threads", pod_name, _active_threads));
				body.append(std::format(str, "cpp_pool_size", "Thread pool size", pod_name, pool_size));
				body.append(std::format(str_avg, "cpp_avg_time", "Average request processing time in milliseconds", pod_name, avg));
				req.response.set_body(body, "text/plain; version=0.0.4");
			},
			false /* no security */
		);		
	}


	constexpr void prebuilt_services()
	{
		logger::log("server", "info", "registering built-in diagnostic and security services...");
		
		register_diagnostic_services();

		register_webapi
		(
			webapi_path("/api/login"), 
			"Default Login service using a database",
			http::verb::POST, 
			{
				{"username", http::field_type::STRING, true},
				{"password", http::field_type::STRING, true}
			},
			{} /* roles */,
			[](http::request& req) 
			{
				std::string login{req.get_param("username")};
				std::string password{req.get_param("password")};
				const std::string sid {http::get_uuid()};
				if (const auto lr {login::bind(login, password, sid, req.remote_ip)}; lr.ok()) {
					const std::string token {jwt::get_token(sid, login, lr.get_email(), lr.get_roles())};
					constexpr auto json {R"({{"status":"OK","data":[{{"displayname":"{}","token_type":"bearer","id_token":"{}"}}]}})"};
					const std::string login_ok {std::format(json, lr.get_display_name(), token)};
					req.response.set_body(login_ok);
					if (env::login_log_enabled())
						logger::log("security", "info", std::format("login OK - SID: {} user: {} IP: {} token: {} roles: {}", 
								sid, login, req.remote_ip, token, lr.get_roles()), req.get_header("x-request-id"));
				} else {
					logger::log("security", "warn", std::format("login failed - user: {} IP: {}", login, req.remote_ip), req.get_header("x-request-id"));
					constexpr auto json = R"({{"status":"INVALID","validation":{{"id":"login","code":"{}","description":"{}"}}}})";
					req.response.set_body(std::format(json, lr.get_error_code(), lr.get_error_description()));
				}
			},
			false /* no security */
		);
		
		register_webapi
		(
			webapi_path("/api/totp"), 
			"Validate TOTP token given a base32 encoded secret",
			http::verb::POST, 
			{
				{"duration", http::field_type::INTEGER, true}, 
				{"token", http::field_type::STRING, true}, 
				{"secret", http::field_type::STRING, true}
			},
			{ },		
			[](http::request& req) 
			{
				const int s{std::stoi(req.get_param("duration"))};
				if (auto[result, error_msg]{is_valid_token(s, req.get_param("token"), req.get_param("secret"))}; result )
					req.response.set_body(R"({"status":"OK"})");
				else
					req.response.set_body(
						std::format(R"({{"status":"INVALID","validation":{{"id":"token","description":"{}"}}}})", error_msg)
					);
			},
			false /* no security */
		);			
	}
	
	constexpr void register_webapi(
						const webapi_path& _path, 
						const std::string& _description,
						const http::verb& _verb,
						const std::vector<http::input_rule>& _rules,
						const std::vector<std::string>& _roles,
						const std::function<void(http::request&)>& _fn,
						const bool _is_secure = true
						)
	{
		webapi_catalog.try_emplace
		(
			_path.get(),
			_description,
			_verb,
			_rules,
			_roles,
			_fn,
			_is_secure
		);
		std::string msg {_is_secure ? "" : "(insecure) "};
		logger::log("server", "info", std::format("registered {}WebAPI for path: {}", msg, _path.get()));
	}

	constexpr void register_webapi(
						const webapi_path& _path, 
						const std::string& _description, 
						const http::verb& _verb, 
						const std::function<void(http::request&)>& _fn, 
						const bool _is_secure = true
						)
	{
		register_webapi(_path, _description, _verb, {}, {}, _fn, _is_secure);
	}

	constexpr void start()
	{
		prebuilt_services();
		enable_audit = env::enable_audit();
		print_server_info();

		const auto pool_size {env::pool_size()};
		const auto port {env::port()};

		//create workers pool - consumers
		std::vector<std::stop_source> stops(pool_size);
		std::vector<std::jthread> pool(pool_size);
		for (int i = 0; i < pool_size; i++) {
			stops[i] = std::stop_source();
			pool[i] = std::jthread(consumer, stops[i].get_token(), this);
		}
		
		std::stop_source audit_stop;
		std::jthread audit_engine(audit, audit_stop.get_token(), this);
		
		std::chrono::duration<double> elapsed = std::chrono::high_resolution_clock::now() - _start_init;
		logger::log("server", "info", std::format("server started in {}s", elapsed.count()));
		
		start_epoll(port);

		logger::log("server", "info", std::format("{} shutting down...", pod_name));
		
		//shutdown workers
		for (const auto& s: stops) {
			s.request_stop();
			{
				std::scoped_lock lock {m_mutex};
				m_cond.notify_all();
			}
		}
		
		for (auto& t:pool)
			t.join();
		
		audit_stop.request_stop();
		m_audit_cond.notify_all();
		audit_engine.join();
		
	}
	
};


#endif /* SERVER_H_ */
