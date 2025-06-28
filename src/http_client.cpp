/**
 * @file http_client.cpp
 * @brief Implements the http_client class, a modern C++ wrapper for libcurl.
 * @author Your Name
 * @date 2025-06-26
 */

#include "http_client.h"
#include <curl/curl.h>
#include <iostream>
#include <utility>
#include <limits>
#include <functional> // For std::less
#include <cstdlib>    // for std::abort

namespace {

class curl_global_initializer {
public:
    curl_global_initializer() {
        if (CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT); res != CURLE_OK) {
            std::cerr << "Fatal: Failed to initialize libcurl globally." << std::endl;
            std::abort();
        }
    }
    ~curl_global_initializer() {
        curl_global_cleanup();
    }
    curl_global_initializer(const curl_global_initializer&) = delete;
    curl_global_initializer& operator=(const curl_global_initializer&) = delete;
    curl_global_initializer(curl_global_initializer&&) = delete;
    curl_global_initializer& operator=(curl_global_initializer&&) = delete;
};

const curl_global_initializer g_curl_initializer;

} // namespace


curl_exception::curl_exception(const std::string& msg)
    : std::runtime_error(msg) {}

class http_client::impl {
public:
    explicit impl(http_client_config config) : m_config(std::move(config)) {}

    [[nodiscard]] http_response perform_request(const std::string& url,
                                              const std::optional<std::string>& post_body,
                                              const std::optional<std::vector<http_form_part>>& form_parts,
                                              const std::map<std::string, std::string, std::less<>>& headers) const;
private:
    http_client_config m_config;

    void configure_common_options(/* NOSONAR */ CURL* curl, const std::string& url, http_response& response) const;
    [[nodiscard]] curl_slist* build_headers(const std::map<std::string, std::string, std::less<>>& headers) const;
    void configure_post_body(/* NOSONAR */ CURL* curl, const std::string& post_body) const;
    [[nodiscard]] curl_mime* build_multipart_form(/* NOSONAR */ CURL* curl, const std::vector<http_form_part>& form_parts) const;
    
    static size_t write_callback(const char* ptr, size_t size, size_t nmemb, /* NOSONAR */ void* userdata);
    static size_t header_callback(const char* buffer, size_t size, size_t nitems, /* NOSONAR */ void* userdata);
};

size_t http_client::impl::write_callback(const char* ptr, size_t size, size_t nmemb, /* NOSONAR */ void* userdata) {
    if (userdata == nullptr) return 0;
    auto* response_body = static_cast<std::string*>(userdata);
    try {
        response_body->append(ptr, size * nmemb);
    } catch (const std::bad_alloc&) {
        return 0; 
    }
    return size * nmemb;
}

size_t http_client::impl::header_callback(const char* buffer, size_t size, size_t nitems, /* NOSONAR */ void* userdata) {
    if (userdata == nullptr) return 0;
    auto* response_headers = static_cast<std::map<std::string, std::string, std::less<>>*>(userdata);
    std::string header(buffer, size * nitems);
    if (const size_t colon_pos = header.find(':'); colon_pos != std::string::npos) {
        std::string key = header.substr(0, colon_pos);
        std::string value = header.substr(colon_pos + 1);
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t\r\n") + 1);
        (*response_headers)[key] = value;
    }
    return size * nitems;
}

void http_client::impl::configure_common_options(/* NOSONAR */ CURL* curl, const std::string& url, http_response& response) const {
    static constexpr const char* user_agent = "cpp-http-client/1.0";
    static constexpr long follow_redirects = 1L;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, m_config.connect_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, m_config.request_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, follow_redirects);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);
    
    if (m_config.client_cert_path) curl_easy_setopt(curl, CURLOPT_SSLCERT, m_config.client_cert_path->c_str());
    if (m_config.client_key_path) curl_easy_setopt(curl, CURLOPT_SSLKEY, m_config.client_key_path->c_str());
    if (m_config.client_key_password) curl_easy_setopt(curl, CURLOPT_KEYPASSWD, m_config.client_key_password->c_str());
}

[[nodiscard]] curl_slist* http_client::impl::build_headers(const std::map<std::string, std::string, std::less<>>& headers) const {
    curl_slist* header_list = nullptr;
    for (const auto& [key, value] : headers) {
        std::string header_string = key + ": " + value;
        header_list = curl_slist_append(header_list, header_string.c_str());
    }
    return header_list;
}

void http_client::impl::configure_post_body(/* NOSONAR */ CURL* curl, const std::string& post_body) const {
    if (const size_t body_length = post_body.length(); body_length > static_cast<size_t>(std::numeric_limits<long>::max())) {
        throw curl_exception("POST body is too large to be handled by libcurl.");
    }
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(post_body.length()));
}

[[nodiscard]] curl_mime* http_client::impl::build_multipart_form(/* NOSONAR */ CURL* curl, const std::vector<http_form_part>& form_parts) const {
    curl_mime* mime = curl_mime_init(curl);
    if (!mime) {
        throw curl_exception("curl_mime_init() failed.");
    }

    for (const auto& part : form_parts) {
        curl_mimepart* mime_part = curl_mime_addpart(mime);
        curl_mime_name(mime_part, part.name.c_str());

        if (std::holds_alternative<std::string>(part.contents)) {
            const auto& value = std::get<std::string>(part.contents);
            curl_mime_data(mime_part, value.c_str(), value.length());
        } else {
            const auto& file = std::get<http_form_file>(part.contents);
            curl_mime_filedata(mime_part, file.file_path.c_str());
            if (file.content_type) {
                curl_mime_type(mime_part, file.content_type->c_str());
            }
        }
    }
    return mime;
}

[[nodiscard]] http_response http_client::impl::perform_request(const std::string& url,
                                                            const std::optional<std::string>& post_body,
                                                            const std::optional<std::vector<http_form_part>>& form_parts,
                                                            const std::map<std::string, std::string, std::less<>>& headers) const {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw curl_exception("Failed to create CURL easy handle.");
    }

    auto curl_deleter = [](CURL* c) { curl_easy_cleanup(c); };
    std::unique_ptr<CURL, decltype(curl_deleter)> curl_ptr(curl, curl_deleter);

    auto slist_deleter = [](curl_slist* sl) { curl_slist_free_all(sl); };
    std::unique_ptr<curl_slist, decltype(slist_deleter)> header_list_ptr(build_headers(headers), slist_deleter);
    
    auto mime_deleter = [](curl_mime* m) { if(m) curl_mime_free(m); };
    std::unique_ptr<curl_mime, decltype(mime_deleter)> mime_ptr(nullptr, mime_deleter);
    
    http_response response{};
    
    configure_common_options(curl, url, response);

    if (header_list_ptr) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list_ptr.get());
    }

    if (form_parts) {
        mime_ptr.reset(build_multipart_form(curl, *form_parts));
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime_ptr.get());
    } else if (post_body) {
        configure_post_body(curl, *post_body);
    }

    if (CURLcode res = curl_easy_perform(curl); res != CURLE_OK) {
        throw curl_exception(std::string("curl_easy_perform() failed: ") + curl_easy_strerror(res));
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
    
    return response;
}

http_client::http_client(http_client_config config) : pimpl_(std::make_unique<impl>(std::move(config))) {}
http_client::~http_client() = default;
http_client::http_client(http_client&&) noexcept = default;
http_client& http_client::operator=(http_client&&) noexcept = default;

[[nodiscard]] http_response http_client::get(const std::string& url, const std::map<std::string, std::string, std::less<>>& headers) const {
    return pimpl_->perform_request(url, std::nullopt, std::nullopt, headers);
}

[[nodiscard]] http_response http_client::post(const std::string& url, const std::string& body, const std::map<std::string, std::string, std::less<>>& headers) const {
    return pimpl_->perform_request(url, body, std::nullopt, headers);
}

[[nodiscard]] http_response http_client::post(const std::string& url, const std::vector<http_form_part>& form_parts, const std::map<std::string, std::string, std::less<>>& headers) const {
    return pimpl_->perform_request(url, std::nullopt, form_parts, headers);
}
