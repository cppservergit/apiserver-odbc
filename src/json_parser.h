#pragma once

#include <json-c/json.h>
#include <string_view>
#include <stdexcept>
#include <string>
#include <map>

namespace json {

class parsing_error : public std::runtime_error {
public:
    explicit parsing_error(const std::string& msg);
};

class json_parser {
public:
    explicit json_parser(std::string_view json_str);
    ~json_parser() noexcept;

    json_parser(const json_parser& other);
    json_parser& operator=(const json_parser& other);

    json_parser(json_parser&& other) noexcept;
    json_parser& operator=(json_parser&& other) noexcept;

    [[nodiscard]] std::string_view get_string(std::string_view key) const;
    [[nodiscard]] bool has_key(std::string_view key) const noexcept;
    [[nodiscard]] json_parser at(std::string_view key) const;
    [[nodiscard]] json_parser at(size_t index) const;
    [[nodiscard]] size_t size() const noexcept;
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::map<std::string, std::string, std::less<>> get_map() const;

private:
    explicit json_parser(struct json_object* obj) noexcept;

    struct json_object* obj_;
};

} // namespace json