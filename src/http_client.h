#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

/**
 * @file http_client.hpp
 * @brief Defines the interface for a modern C++ HTTP client wrapper for libcurl.
 * @author Your Name
 * @date 2025-06-26
 */

#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <optional>
#include <memory>
#include <functional> // For std::less
#include <variant>    // For std::variant

/**
 * @class curl_exception
 * @brief Custom exception for libcurl-related errors.
 */
class curl_exception : public std::runtime_error {
public:
    explicit curl_exception(const std::string& msg);
};

/**
 * @struct http_response
 * @brief Represents an HTTP response from a server.
 */
struct http_response {
    /// @brief The HTTP status code (e.g., 200 for OK, 404 for Not Found).
    long status_code;
    /// @brief The body of the HTTP response.
    std::string body;
    /// @brief A map of response headers.
    std::map<std::string, std::string, std::less<>> headers;
};

/**
 * @struct http_client_config
 * @brief Configuration options for an http_client instance.
 */
struct http_client_config {
    /// @brief Connection timeout in milliseconds. Defaults to 10000ms.
    long connect_timeout_ms = 10000L;
    /// @brief Total request/read timeout in milliseconds. Defaults to 30000ms.
    long request_timeout_ms = 30000L;
    /// @brief Optional path to the client SSL certificate file.
    std::optional<std::string> client_cert_path;
    /// @brief Optional path to the client SSL private key file.
    std::optional<std::string> client_key_path;
    /// @brief Optional password for the client SSL private key.
    std::optional<std::string> client_key_password;
};

/**
 * @struct http_form_file
 * @brief Represents a file to be sent as part of a multipart form.
 */
struct http_form_file {
    /// @brief The local path to the file.
    std::string file_path;
    /// @brief The optional MIME type of the file (e.g., "image/jpeg").
    std::optional<std::string> content_type;
};

/**
 * @struct http_form_part
 * @brief Represents a single part of a multipart/form-data request.
 */
struct http_form_part {
    /// @brief The name of the form field.
    std::string name;
    /// @brief The content of the part, which can be a simple string value or a file.
    std::variant<std::string, http_form_file> contents;
};


/**
 * @class http_client
 * @brief A modern, thread-safe C++23 wrapper for libcurl for making REST API calls.
 */
class http_client {
public:
    /**
     * @brief Constructs an http_client with optional custom configuration.
     * @param config A http_client_config struct with desired settings.
     */
    explicit http_client(http_client_config config = {});

    /**
     * @brief Destructor.
     */
    ~http_client();

    http_client(const http_client&) = delete;
    http_client& operator=(const http_client&) = delete;
    http_client(http_client&&) noexcept;
    http_client& operator=(http_client&&) noexcept;

    /**
     * @brief Performs an HTTP GET request.
     * @param url The target URL for the GET request.
     * @param headers A map of request headers to be sent.
     * @return An http_response struct containing the server's response.
     * @throws curl_exception on failure.
     */
    [[nodiscard]] http_response get(const std::string& url, const std::map<std::string, std::string, std::less<>>& headers = {}) const;

    /**
     * @brief Performs an HTTP POST request with a raw string body.
     * @param url The target URL for the POST request.
     * @param body The data to be sent in the request body.
     * @param headers A map of request headers.
     * @return An http_response struct containing the server's response.
     * @throws curl_exception on failure.
     */
    [[nodiscard]] http_response post(const std::string& url, const std::string& body, const std::map<std::string, std::string, std::less<>>& headers = {}) const;
    
    /**
     * @brief Performs a multipart/form-data HTTP POST request.
     * @param url The target URL for the POST request.
     * @param form_parts A vector of http_form_part objects.
     * @param headers A map of request headers.
     * @return An http_response struct containing the server's response.
     * @throws curl_exception on failure.
     */
    [[nodiscard]] http_response post(const std::string& url, const std::vector<http_form_part>& form_parts, const std::map<std::string, std::string, std::less<>>& headers = {}) const;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

#endif // HTTP_CLIENT_H
