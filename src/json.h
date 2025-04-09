/**
 * @file json.h
 * @brief minimal JSON parser for simple JSON objects
 */


 
#ifndef JSON_H_
#define JSON_H_

#include <string_view>
#include <ranges>
#include <unordered_map>
#include <json-c/json.h>
#include "util.h"

/**
 *  @brief minimal JSON parser for simple JSON objects
 * 
 *  This is a wrapper of the json-c library, provides a C++ API to easily parse
 *  simple JSON objects like {"a":"value_a","b":"value_b","c":195.76}
 *  @date Feb 21, 2023
 *  @author Martin Cordova cppserver@martincordova.com
 */
namespace json
{
	class invalid_json_exception
	{
		public:
			explicit invalid_json_exception(const std::string& _msg): m_msg {_msg} {}
			std::string what() const noexcept {
				return m_msg;
			}
		private:
            std::string m_msg;
	};		

	/**
	* @brief For internal use, parses the JSON string, returns error information in case it fails
	*/
	inline struct json_object* json_tokener_parse_verbose2(std::string_view str, enum json_tokener_error *error)
	{
		struct json_tokener *tok;
		struct json_object *obj;

		tok = json_tokener_new();
		if (!tok)
		{
			*error = json_tokener_error_memory;
			return nullptr;
		}
		json_tokener_set_flags(tok, JSON_TOKENER_STRICT | JSON_TOKENER_ALLOW_TRAILING_CHARS);
		obj = json_tokener_parse_ex(tok, str.data(), str.size());
		*error = tok->err;
		if (tok->err != json_tokener_success)
		{
			if (obj != nullptr)
				json_object_put(obj);
			obj = nullptr;
		}

		json_tokener_free(tok);
		return obj;
	}

	/**
	* @brief Returns a std::unordered_map<std::string, std::string> that contains the attribute names and the values of the JSON object
	* 
	* throws json::invalid_json_exception if the parser fails to parse the JSON string, also
	* a log trace will be printed to stderr using the prefix [DEBUG][JSON] with the error description returned by the native json-c library
	*/
	inline auto parse(std::string_view json)
	{
		enum json_tokener_error jerr;
		std::unordered_map<std::string, std::string, util::string_hash, std::equal_to<>> fields;
		json_object * jobj = json_tokener_parse_verbose2(json.data(), &jerr);
		if (jobj == nullptr) {
			std::string json_error {json_tokener_error_desc(jerr)};
			std::clog << std::format("[DEBUG][JSON] invalid JSON format: {} payload: {}\n", json_error, json);
			throw invalid_json_exception("invalid JSON format, check stderr log for details");
		}
		json_object_object_foreach(jobj, key, val) {
			if (const char* val_ptr {json_object_get_string(val)})
				fields.try_emplace(key, val_ptr);
			else
				fields.try_emplace(key, "");
		}
		json_object_put(jobj);
		return fields;
	}
}

#endif /* JSON_H_ */
