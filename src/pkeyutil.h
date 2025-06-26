#ifndef PKEYUTIL_H_
#define PKEYUTIL_H_

#include <string>
#include <string_view>

// Result of a decryption operation
struct DecryptionResult {
    bool success;
    std::string content; // On success, contains the decrypted text. On failure, an error message.
};

// Result of a token validation
struct TokenValidationResult {
    bool isValid;
    std::string message; // Empty on success, contains an error message on failure.
};

/**
 * @brief Decrypts a file encrypted with an RSA private key.
 *
 * This function reads a file encrypted with an RSA public key and decrypts it
 * using the corresponding private key, which must be named "private.pem" and
 * located in the same directory.
 *
 * @param filename The path to the encrypted file.
 * @return A DecryptionResult struct.
 */
DecryptionResult decrypt(std::string_view filename) noexcept;

/**
 * @brief Validates a TOTP token.
 *
 * @param seconds The step for the TOTP algorithm (e.g., 30).
 * @param token The token to validate (6 or 8 digits).
 * @param secretb32 The base32-encoded secret key.
 * @return A TokenValidationResult struct.
 */
TokenValidationResult is_valid_token(int seconds, const std::string& token, const std::string& secretb32) noexcept;

#endif // PKEYUTIL_H_
