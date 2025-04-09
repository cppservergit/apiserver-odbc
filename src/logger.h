/**
 * @file logger.h
 * @brief Provides centralized log with Loki compatible format.
 */

#ifndef LOGGER_H_
#define LOGGER_H_

#include <string>
#include <string_view>
#include <thread>
#include <iostream>
#include <format>

/**
 * @brief Provides centralized log with Loki compatible format.
 *
 * Prints log trace to sdterr using a Grafana Loki compatible JSON format,
 * the log trace includes the current thread-id
 *  @date Feb 21, 2023
 *  @author Martin Cordova cppserver@martincordova.com
 */
namespace logger
{
	void log(std::string_view source, std::string_view level, std::string_view msg, std::string_view x_request_id = "") noexcept;
}

#endif /* LOGGER_H_ */
