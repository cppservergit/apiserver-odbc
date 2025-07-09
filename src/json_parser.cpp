#include "json_parser.h"
#include <format>
#include <iostream>
#include <memory>
#include <utility>

namespace json {

// parsing_error implementation
parsing_error::parsing_error(const std::string& msg)
    : std::runtime_error(msg) {}

// jsonoutput_error implementation
output_error::output_error(const std::string& msg)
    : std::runtime_error(msg) {}

// json_parser implementation
json_parser::json_parser(std::string_view json_str) {
    // A smart pointer is not used here because json_tokener_free does not match
    // the required signature for a std::unique_ptr deleter.
    auto* tok = json_tokener_new();
    if (!tok) {
        throw std::bad_alloc{};
    }

    obj_ = json_tokener_parse_ex(tok, json_str.data(), static_cast<int>(json_str.size()));

    if (json_tokener_get_error(tok) != json_tokener_success || obj_ == nullptr) {
        std::string err = json_tokener_error_desc(json_tokener_get_error(tok));
        json_tokener_free(tok);
        throw parsing_error(std::format("JSON parsing error: {} payload: {}", err, json_str));
    }

    json_tokener_free(tok);
}

json_parser::~json_parser() noexcept {
    if (obj_) {
        json_object_put(obj_);
    }
}

json_parser::json_parser(const json_parser& other)
    : obj_(json_object_get(other.obj_)) {}

json_parser& json_parser::operator=(const json_parser& other) {
    if (this != &other) {
        json_object_put(obj_); // Free the existing object
        obj_ = json_object_get(other.obj_); // Get a new reference from the other object
    }
    return *this;
}

json_parser::json_parser(json_parser&& other) noexcept
    : obj_(other.obj_) {
    other.obj_ = nullptr;
}

json_parser& json_parser::operator=(json_parser&& other) noexcept {
    if (this != &other) {
        json_object_put(obj_); // Free the existing object
        obj_ = other.obj_;
        other.obj_ = nullptr;
    }
    return *this;
}

std::string json_parser::build(const std::map<std::string, std::string>& data) {
    auto* obj = json_object_new_object();
    if (!obj) {
        throw std::bad_alloc{};
    }
    // Use a unique_ptr for exception safety during the build process.
    // It ensures json_object_put is called even if an exception is thrown.
    std::unique_ptr<json_object, decltype(&json_object_put)> obj_ptr(obj, &json_object_put);

    for (const auto& [key, value] : data) {
        auto* j_value = json_object_new_string(value.c_str());
        if (!j_value) {
            // This could be due to memory allocation failure or invalid UTF-8 in the value.
            throw output_error("json build: failed to create json string for value: " + value);
        }

        // json_object_object_add takes ownership of j_value upon success.
        if (json_object_object_add(obj_ptr.get(), key.c_str(), j_value) != 0) {
            // If adding fails, we are still responsible for freeing j_value.
            json_object_put(j_value);
            throw output_error("json build: failed to add key to json object: " + key);
        }
    }

    const char* json_str = json_object_to_json_string_ext(obj_ptr.get(), JSON_C_TO_STRING_PLAIN);
    if (!json_str) {
        // This should not happen with a valid object, but we check for completeness.
        throw output_error("json build: failed to convert json object to string");
    }

    return std::string{json_str};
}

std::string_view json_parser::get_string(std::string_view key) const {
    // This creates a temporary std::string to ensure the key is null-terminated.
    auto* tmp = json_object_object_get(obj_, std::string(key).c_str());
    return tmp ? std::string_view(json_object_get_string(tmp)) : std::string_view{};
}

bool json_parser::has_key(std::string_view key) const noexcept {
    return json_object_object_get_ex(obj_, std::string(key).c_str(), nullptr);
}

json_parser json_parser::at(std::string_view key) const {
    if (!obj_ || !json_object_is_type(obj_, json_type_object)) {
        throw parsing_error("json value is not an object");
    }
    auto* child = json_object_object_get(obj_, std::string(key).c_str());
    if (!child) {
        throw std::out_of_range("json object missing key: " + std::string(key));
    }
    return json_parser{json_object_get(child)}; // Return a new parser with an incremented ref count.
}

json_parser json_parser::at(size_t index) const {
    if (!obj_ || !json_object_is_type(obj_, json_type_array)) {
        throw parsing_error("json value is not an array");
    }
    auto* item = json_object_array_get_idx(obj_, index);
    if (!item) {
        throw std::out_of_range("json array index out of range");
    }
    return json_parser{json_object_get(item)}; // Return a new parser with an incremented ref count.
}

size_t json_parser::size() const noexcept {
    if (json_object_is_type(obj_, json_type_array)) {
        return json_object_array_length(obj_);
    }
    return 0;
}

std::string json_parser::to_string() const {
    if (!obj_) {
        return "";
    }
    return json_object_to_json_string_ext(obj_, JSON_C_TO_STRING_PLAIN);
}

std::map<std::string, std::string, std::less<>> json_parser::get_map() const {
    std::map<std::string, std::string, std::less<>> fields;

    if (!obj_ || !json_object_is_type(obj_, json_type_object)) {
        return fields;
    }

    json_object_object_foreach(obj_, key, val) {
        // This implementation skips nested objects and arrays.
        if (!val || json_object_is_type(val, json_type_object) || json_object_is_type(val, json_type_array)) {
            continue;
        }
        if (const char* val_ptr = json_object_get_string(val)) {
            fields.try_emplace(key, val_ptr);
        } else {
            // Could happen for non-string types like numbers or booleans.
            // This implementation treats them as empty strings.
            fields.try_emplace(key, "");
        }
    }
    return fields;
}

json_parser::json_parser(struct json_object* obj) noexcept : obj_(obj) {}

} // namespace json
