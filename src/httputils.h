
/*
 * httputils - provides http utility abstractions for epoll server and microservice engine
 *
 *  Created on: Feb 21, 2023
 *      Author: Martin Cordova cppserver@martincordova.com - https://cppserver.com
 *      Free to use in commercial projects, no warranties and no responsabilities assumed 
 *		by the author, use at your own risk. By using this code you accept the forementioned conditions.
 */
#ifndef HTTPUTILS_H_
#define HTTPUTILS_H_

#include <string>
#include <string_view>
#include <unordered_map>
#include <algorithm>
#include <fstream>
#include <vector>
#include <sstream>
#include <cerrno>
#include <cstring>
#include <array>
#include <ranges>
#include <format>
#include <iterator>
#include <concepts>
#include <charconv>
#include <chrono>
#include <utility>
#include <sys/socket.h>
#include <uuid/uuid.h>
#include "util.h"
#include "logger.h"
#include "jwt.h"
#include "email.h"

namespace http
{
	const std::string blob_path {"/var/blobs/"};

	enum class status {
        ok = 200,
        no_content = 204,
        bad_request = 400,
        unauthorized = 401,
        forbidden = 403,
        not_found = 404,
        method_not_allowed = 405
    };
	
	inline std::ostream& operator<<(std::ostream& os, status s) {
		using namespace std::string_view_literals;
		using enum status;
		switch (s) {
			case ok:                 return os << "200"sv;
			case no_content:         return os << "204"sv;
			case bad_request:        return os << "400"sv;
			case unauthorized:       return os << "401"sv;
			case forbidden:          return os << "403"sv;
			case not_found:          return os << "404"sv;
			case method_not_allowed: return os << "405"sv;
			default: return os << "Unknown Status";
		}
	}
	
	std::string get_uuid() noexcept;
	
	enum class verb {GET, POST};

	class invalid_payload_exception
	{ 
		public:
			std::string what() const noexcept
			{
				return error_description;
			}
			explicit invalid_payload_exception(const std::string& _errmsg): 
				error_description{_errmsg} { }
		private:
			std::string error_description;
	};

	class invalid_input_exception
	{
		public:
			std::string what() const noexcept
			{
				return "Invalid HTTP request input parameter: " + field_name;
			}
			explicit invalid_input_exception(const std::string& _name, const std::string& _errmsg): 
				field_name{_name}, error_description{_errmsg} { }
			auto get_field_name() const { return field_name; }
			auto get_error_description() const { return error_description; }
		private:
			std::string field_name;
			std::string error_description;
	};

	class login_required_exception
	{
		public:
			explicit login_required_exception(const std::string& _remote_ip, const std::string& _reason)
			: m_remote_ip {_remote_ip}, m_reason(_reason) {}
			std::string what() const noexcept {
				return "Authentication required from IP: " + m_remote_ip + " reason: " + m_reason;
			}
		private:
            std::string m_remote_ip;
			std::string m_reason;
	};

	class access_denied_exception
	{
		public:
			explicit access_denied_exception(const std::string& _user, const std::string& _remote_ip, const std::string& _reason)
			: m_user {_user}, m_remote_ip {_remote_ip}, m_reason {_reason} {}
			std::string what() const noexcept {
				return "Access denied for user: " + m_user + " from IP: " + m_remote_ip + " reason: " + m_reason;
			}
		private:
			std::string m_user;
            std::string m_remote_ip;
            std::string m_reason;
	};

	class method_not_allowed_exception
	{
		public:
			explicit method_not_allowed_exception(const std::string& _method): m_method {_method} {}
			std::string what() const noexcept {
				std::string error_msg{"HTTP method not allowed: " + m_method};
				return error_msg;
			}
		private:
            std::string m_method;
	};
	
	class save_blob_exception
	{
		public:
			explicit save_blob_exception(const std::string& _msg): m_msg {_msg} {}
			std::string what() const noexcept {
				return m_msg;
			}
		private:
            std::string m_msg;
	};	
	
	class resource_not_found_exception
	{
		public:
			explicit resource_not_found_exception(const std::string& _msg): m_message {_msg} {}
			std::string what() const noexcept {
				std::string error_msg{"Resource not found: " + m_message};
				return error_msg;
			}
		private:
            std::string m_message;
	};	
	
	enum class field_type {
							INTEGER = 1,
							DOUBLE = 2,
							STRING = 3,
							DATE = 4 //yyyy-mm-dd
						};

	struct input_rule {
		public:
			input_rule(const std::string& n, field_type d, bool r) noexcept: name{n}, datatype{d}, required{r} {  }
			auto get_name() const {return name;}
			auto get_type() const {return datatype;}
			auto get_required() const {return required;}
		private:
			std::string name;
			field_type datatype;
			bool required;
	};
	
	struct form_field 
	{
		std::string name;
		std::string filename;
		std::string content_type;
		std::string data;
	};

	struct request_internals {
		size_t bodyStartPos{0};
		size_t contentLength{0};
		int errcode{0};
		std::string errmsg;
	};

	struct socket_buffer {
	private:
		constexpr static int _buffer_size {2048};
		constexpr static double _threshold {0.75};
		std::vector<char> _buffer;
		int _pos{0};
	public:
		socket_buffer() {
			_buffer.resize(_buffer_size, 0);
		}
		
		constexpr void update_pos(int n) noexcept {
			if ( n > 0) {
				_pos += n;
				if (_pos > int(double(_buffer.size()) * _threshold))
					_buffer.resize(_buffer.size() + _buffer_size, 0);
			}
		}
		
		constexpr int available_size() const noexcept {
			return int(_buffer.size() - _pos);
		}

		constexpr auto buffer_size() const noexcept {
			return _buffer.size();
		}

		constexpr auto size() const noexcept {
			return _pos;
		}

		constexpr char* data() noexcept {
			return &_buffer[_pos];
		}
		
		constexpr std::string_view view() const noexcept {
			return std::string_view{&_buffer[0], &_buffer[_pos]};
		}
		
		constexpr bool empty() const noexcept {
			return _pos == 0;
		}
		
		constexpr void clear() noexcept {
			_pos = 0;
			_buffer.resize(_buffer_size, 0);
		}
	};

	struct response_stream {
	  public:	
		response_stream();
		response_stream& operator <<(std::string_view data);
		void set_body(std::string_view body, std::string_view content_type = "application/json");
		void set_body_blob(std::string_view body, std::string_view content_type);
		void set_content_disposition(std::string_view disposition);
		void set_origin(std::string_view origin);
		void set_request_id(std::string_view req_id);
		std::string_view view() const noexcept;
		size_t size() const noexcept;
		const char* data() const noexcept;
		void clear() noexcept;
		bool write(int fd) noexcept; 
	  private:
		int _pos1 {0};
		std::string _buffer{""};
		std::string _content_disposition{""};
		std::string _origin{""};
		std::string _x_request_id{""};
	};
		
	struct line_reader {
	  public:
		explicit line_reader(std::string_view str);
		bool eof() const noexcept;
		std::string_view getline();
		
	  private:
		bool _eof{false};
		std::string_view buffer;
		int pos{0};
		const std::string line_sep{"\r\n"};
	};	
	
	struct request {
	  public:
		int epoll_fd;
		int fd;
		std::string remote_ip;
		request_internals internals;
		bool isMultipart{false};
		bool save_blob_failed{false};
		std::string method;
		std::string queryString;
		std::string path;
		std::string boundary;
		std::string token;
		std::string origin;
		socket_buffer payload;
		std::map<std::string, std::string, std::less<>> headers;
		std::map<std::string, std::string, std::less<>> params;
		std::vector<input_rule> input_rules;
		jwt::user_info user_info;
		response_stream response;
		
		explicit request(int epollfd, int fdes, const std::string& ip): epoll_fd{epollfd}, fd {fdes}, remote_ip {ip}
		{ }

		request() = default;
		void parse();
		bool eof();
		std::string get_header(const std::string& name) const;
		std::string get_param(const std::string& name) const;
		void enforce(verb v) const;
		void enforce(const std::vector<input_rule>& rules);
		
		template<class FN>
		void enforce(const std::string& id, const std::string& error_description, FN fn) const
		{
			if (!fn())
				throw invalid_input_exception(id, error_description);
		}
		
		std::string get_sql(std::string sql);
		void check_security(const std::vector<std::string>& roles = {});
		void log(std::string_view source, std::string_view level, const std::string& msg) noexcept;
		
		void send_mail(const std::string& to, std::string& subject, const std::string& body);
		void send_mail(const std::string& to, std::string& cc, std::string& subject, const std::string& body);
		void send_mail(const std::string& to, std::string& cc, std::string& subject, const std::string& body, std::string& attachment, std::string& attachment_filename);
		
		std::string_view get_body() const noexcept;

		void delete_blobs();
		
	  private:
		void test_field(const http::input_rule& r, std::string& value);
		constexpr std::string decode_param(std::string_view value) const noexcept;
		void parse_param(std::string_view param) noexcept; 
		void parse_query_string(std::string_view qs) noexcept;	
		bool parse_headers(line_reader& lr);
		bool parse_read_boundary(std::string_view value);
		std::pair<std::string, std::string> split_header_line(std::string_view line);
		bool set_content_length(std::string_view value);
		bool add_header(const std::string& header, const std::string& value);
		bool parse_uri(line_reader& lr);
		void set_parse_error(std::string_view msg);
		bool validate_header(std::string_view header, std::string_view value);
		void parse_form();
	};
}

template <>
struct std::formatter<http::status, char> {
    constexpr auto parse(const std::format_parse_context& ctx) const { return ctx.begin(); }
    auto format(http::status s, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "{}", std::to_underlying(s));
    }
};

#endif /* HTTPUTILS_H_ */

