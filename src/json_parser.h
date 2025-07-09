#pragma once

#include <json-c/json.h>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

/**
 * @file json_parser.h
 * @brief A C++ wrapper for the json-c library to parse and serialize JSON data.
 */

namespace json {

/**
 * @brief Exception thrown for errors during JSON parsing.
 */
class parsing_error : public std::runtime_error {
public:
    explicit parsing_error(const std::string& msg);
};

/**
 * @brief Exception thrown for errors during JSON object building/serialization.
 */
class output_error : public std::runtime_error {
public:
    explicit output_error(const std::string& msg);
};

/**
 * @brief A C++ wrapper for the json-c library to parse and access JSON data.
 * This class manages the lifecycle of a json_object pointer using RAII.
 */
class json_parser {
public:
    /**
     * @brief Constructs a json_parser by parsing a JSON string.
     * @param json_str The JSON string to parse.
     * @throws json::parsing_error if the string is not valid JSON.
     * @throws std::bad_alloc if memory allocation fails.
     */
    explicit json_parser(std::string_view json_str);

    /**
     * @brief Destructor that safely releases the underlying json_object.
     */
    ~json_parser() noexcept;

    json_parser(const json_parser& other);
    json_parser& operator=(const json_parser& other);
    json_parser(json_parser&& other) noexcept;
    json_parser& operator=(json_parser&& other) noexcept;

    /**
     * @brief Builds a JSON string from a map of key-value pairs.
     * @param data A map of string keys and string values.
     * @return A string containing the JSON representation of the map.
     * @throws json::output_error if building the JSON object fails.
     * @throws std::bad_alloc if memory allocation fails.
     */
    [[nodiscard]] static std::string build(const std::map<std::string, std::string>& data);

    /**
     * @brief Retrieves a string value for a given key.
     * @param key The key to look up.
     * @return A string_view of the value if the key exists and the value is a string, otherwise an empty view.
     */
    [[nodiscard]] std::string_view get_string(std::string_view key) const;

    /**
     * @brief Checks if a key exists in the JSON object.
     * @param key The key to check.
     * @return True if the key exists, false otherwise.
     */
    [[nodiscard]] bool has_key(std::string_view key) const noexcept;

    /**
     * @brief Accesses a nested JSON object by key.
     * @param key The key of the nested object.
     * @return A new json_parser instance for the nested object.
     * @throws std::out_of_range if the key does not exist.
     * @throws json::parsing_error if the current value is not an object.
     */
    [[nodiscard]] json_parser at(std::string_view key) const;

    /**
     * @brief Accesses a nested JSON object by array index.
     * @param index The index in the JSON array.
     * @return A new json_parser instance for the nested object.
     * @throws std::out_of_range if the index is out of bounds.
     * @throws json::parsing_error if the current value is not an array.
     */
    [[nodiscard]] json_parser at(size_t index) const;

    /**
     * @brief Gets the number of elements if the JSON value is an array.
     * @return The array length, or 0 if not an array.
     */
    [[nodiscard]] size_t size() const noexcept;

    /**
     * @brief Serializes the contained JSON object back to a string.
     * @return A string representation of the JSON object.
     */
    [[nodiscard]] std::string to_string() const;

    /**
     * @brief Converts a JSON object into a map of key-value strings.
     * @note This is a shallow conversion. Nested objects/arrays are skipped. Non-string values are converted to empty strings.
     * @return A map of the object's top-level string fields.
     */
    [[nodiscard]] std::map<std::string, std::string, std::less<>> get_map() const;

private:
    explicit json_parser(struct json_object* obj) noexcept;
    struct json_object* obj_;
};

} // namespace json