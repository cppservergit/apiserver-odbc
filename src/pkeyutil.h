#ifndef PKEYUTIL_H_
#define PKEYUTIL_H_

#include <utility>
#include <string>
#include <format>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <liboath/oath.h>
#include <ctime>

// read an encrypted file using RSA asymetric encryption
// requires the private key private.pem present in the working directory
// if true then the text-clear content is returned in the 2nd value of the std::pair
std::pair<bool, std::string> decrypt(std::string_view filename) noexcept;

//validates a TOTP soft token given the token and the secret encoded in base32
// if false then the error description is returned in the 2nd value of the std::pair
std::pair<bool, std::string> is_valid_token(const std::string& token, const std::string& secretb32) noexcept;

#endif
