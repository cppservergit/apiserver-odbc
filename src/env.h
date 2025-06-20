/**
 * @file env.h
 * @brief Read environment variables including values stored in encrypted files.
 */

#ifndef ENV_H_
#define ENV_H_

#include <string>
#include <cstdlib>
#include <charconv>
#include <format>
#include "logger.h"
#include "pkeyutil.h"

/**
 * @brief Read environment variables including values stored in encrypted files.
 *
 * Provides direct access to specific APIServer environment variables and a utility function 
 * to read arbitrary environment variables or encrypted values stored in .enc files using RSA encryption.
 * @date Feb 21, 2023.
 * @author Martin Cordova cppserver@martincordova.com
 */
namespace env 
{
	/** @brief returns CPP_PORT environment variable */
	unsigned short int port() noexcept;
	
	/** @brief returns CPP_HTTP_LOG environment variable */
	unsigned short int http_log_enabled() noexcept;
	
	/** @brief returns CPP_HTTP_LOG environment variable */
	unsigned short int disable_ping_log() noexcept;

	/** @brief returns CPP_POOL_SIZE environment variable */
	unsigned short int pool_size() noexcept;
	
	/** @brief returns CPP_LOGIN_LOG environment variable */
	unsigned short int login_log_enabled() noexcept;

	/** @brief returns CPP_JWT_EXP environment variable */
	unsigned short int jwt_expiration() noexcept;
	
	/** @brief returns CPP_ENABLE_AUDIT environment variable */
	unsigned short int enable_audit() noexcept;
	
	/** 
		@brief Read variable of string type, returns value or empty string if not found.
		
		If the name ends with .enc it will be assumed to be an RSA encrypted file that must be
		on the same directory as the apiserver executable and the private.pem key file will be used to decrypt it, 
		this key must be located on the same directory.
		@param name name of the environment variable, case-sensitive, if it ends with .enc it will be assumed as a file name
	*/
	std::string get_str(const std::string& name) noexcept;
}

#endif /* ENV_H_ */
