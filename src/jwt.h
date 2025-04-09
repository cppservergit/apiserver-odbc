/**
 * @file jwt.h
 * @brief Implements JSON Web Token.
 */
 
#ifndef JWT_H_
#define JWT_H_

#include <string>
#include <vector>
#include <string_view>
#include <ctime>
#include <chrono>
#include <algorithm>
#include <array>
#include <openssl/hmac.h>
#include "logger.h"
#include "env.h"
#include "json.h"

/**
 * @brief Implements JSON Web Token.
 *
 * Environment variables CPP_JWT_SECRET and CPP_JWT_EXP are used to control the signing and the expiration
 * of the token. Provides support to generate a TOKEN with SHA2 signature and token validation,
 * openssl library is used to sign with SHA2.
 * @date July 22, 2023
 * @author Martin Cordova cppserver@martincordova.com
 */
namespace jwt
{
	struct user_info 
	{
		std::string sessionid;
		std::string login;
		std::string mail;
		std::string roles;
		time_t exp{0};
	};
	
	/** 
		@brief Returns a JSON web token given the parameters

		@param sessionid uuid generated after successful login
		@param username user login
		@param mail user's email
		@param roles user's security role names separated by comma
	*/	
	std::string get_token(std::string_view sessionid, std::string_view username, std::string_view mail, std::string_view roles) noexcept;
	
	/** 
		@brief Validates a token's signature using the secret and expiration
		
		Returns a pair, if the first value is true then the token is valid and the second value contains the fields of the token
		@param token JSON web token
	*/		
	std::pair<bool, user_info> is_valid(const std::string& token);

	/** 
		@brief Returns an SHA2 signature encoded in BASE64URL for the given string

		@param message value to sign
	*/
	std::string get_signature(std::string_view message); 
}

#endif /* JWT_H_ */
