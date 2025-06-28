#include "json_parser.h"
#include <format>
#include <iostream>

namespace json {

// parsing_error implementation
parsing_error::parsing_error(const std::string& msg)
    : std::runtime_error("json parsing error: " + msg) {}

// json_parser implementation
json_parser::json_parser(std::string_view json_str) {
    auto* tok = json_tokener_new();
    if (!tok) throw std::bad_alloc{};

    obj_ = json_tokener_parse_ex(tok, json_str.data(), static_cast<int>(json_str.size()));

    if (json_tokener_get_error(tok) != json_tokener_success || obj_ == nullptr) {
        std::string err = json_tokener_error_desc(json_tokener_get_error(tok));
        json_tokener_free(tok);
        std::clog << std::format("[DEBUG][JSON] invalid JSON format: {} payload: {}\n", err, json_str);
        throw parsing_error(std::format("JSON parsing error: {}", err));
    }

    json_tokener_free(tok);
}

json_parser::~json_parser() noexcept {
    if (obj_) json_object_put(obj_);
}

json_parser::json_parser(const json_parser& other)
    : obj_(json_object_get(other.obj_)) {}

json_parser& json_parser::operator=(const json_parser& other) {
    if (this != &other) {
        json_object_put(obj_);
        obj_ = json_object_get(other.obj_);
    }
    return *this;
}

json_parser::json_parser(json_parser&& other) noexcept
    : obj_(other.obj_) {
    other.obj_ = nullptr;
}

json_parser& json_parser::operator=(json_parser&& other) noexcept {
    if (this != &other) {
        json_object_put(obj_);
        obj_ = other.obj_;
        other.obj_ = nullptr;
    }
    return *this;
}

std::string_view json_parser::get_string(std::string_view key) const {
    auto* tmp = json_object_object_get(obj_, std::string(key).c_str());
    return tmp ? std::string_view(json_object_get_string(tmp)) : std::string_view{};
}

bool json_parser::has_key(std::string_view key) const noexcept {
    return json_object_object_get_ex(obj_, std::string(key).c_str(), nullptr) != 0;
}

json_parser json_parser::at(std::string_view key) const {
	if (!obj_ || !json_object_is_type(obj_, json_type_object))
        throw std::logic_error("json value is not an object");
    auto* child = json_object_object_get(obj_, std::string(key).c_str());
    if (!child) throw std::out_of_range("json object missing key: " + std::string(key));
    return json_parser{json_object_get(child)};
}

json_parser json_parser::at(size_t index) const {
	if (!obj_ || !json_object_is_type(obj_, json_type_array))
        throw std::logic_error("json value is not an array");	
    auto* item = json_object_array_get_idx(obj_, index);
    if (!item) throw std::out_of_range("json array index out of range");
    return json_parser{json_object_get(item)};
}

size_t json_parser::size() const noexcept {
    return json_object_is_type(obj_, json_type_array)
        ? json_object_array_length(obj_) : 0;
}

std::string json_parser::to_string() const {
    return json_object_to_json_string_ext(obj_, JSON_C_TO_STRING_PLAIN);
}

std::map<std::string, std::string, std::less<>> json_parser::get_map() const {
    std::map<std::string, std::string, std::less<>> fields;

    if (!obj_ || !json_object_is_type(obj_, json_type_object))
        return fields;

    json_object_object_foreach(obj_, key, val) {
        if (!val || json_object_is_type(val, json_type_object) || json_object_is_type(val, json_type_array))
            continue;
        if (const char* val_ptr {json_object_get_string(val)})
            fields.try_emplace(key, val_ptr);
        else
            fields.try_emplace(key, "");
    }
    return fields;
}

json_parser::json_parser(struct json_object* obj) noexcept : obj_(obj) {}

} // namespace json