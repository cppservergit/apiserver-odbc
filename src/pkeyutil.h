#ifndef PKEYUTIL_H_
#define PKEYUTIL_H_

#include <utility>
#include <string>
#include <openssl/rsa.h>
#include <openssl/pem.h>

std::pair<bool, std::string> decrypt(std::string_view filename) noexcept;

#endif
