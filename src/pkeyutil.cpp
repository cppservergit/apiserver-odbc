#include "pkeyutil.h"

#include <vector>
#include <fstream>
#include <iterator>
#include <memory>
#include <format>

#include <openssl/pem.h>
#include <openssl/evp.h>
#include <liboath/oath.h>

// Custom deleters for OpenSSL and liboath resources
namespace {
    struct EVP_PKEY_Deleter {
        void operator()(EVP_PKEY* pkey) const { EVP_PKEY_free(pkey); }
    };

    struct EVP_PKEY_CTX_Deleter {
        void operator()(EVP_PKEY_CTX* ctx) const { EVP_PKEY_CTX_free(ctx); }
    };

    struct FILE_Deleter {
        void operator()(FILE* f) const { fclose(f); }
    };

    struct OATH_Free_Deleter {
        void operator()(void* ptr) const { free(ptr); }
    };

    using unique_EVP_PKEY = std::unique_ptr<EVP_PKEY, EVP_PKEY_Deleter>;
    using unique_EVP_PKEY_CTX = std::unique_ptr<EVP_PKEY_CTX, EVP_PKEY_CTX_Deleter>;
    using unique_FILE = std::unique_ptr<FILE, FILE_Deleter>;
    using unique_OATH_secret = std::unique_ptr<char, OATH_Free_Deleter>;
}

DecryptionResult decrypt(std::string_view filename) noexcept {
    // Use std::ifstream for safe and automatic file handling
    std::ifstream encrypted_file(filename.data(), std::ios::binary);
    if (!encrypted_file) {
        return {false, "Error: Could not open encrypted file."};
    }

    // Read the entire file into a vector to avoid buffer overflows
    std::vector<unsigned char> encrypted_data(
        (std::istreambuf_iterator<char>(encrypted_file)),
        std::istreambuf_iterator<char>()
    );

    unique_FILE private_key_file(fopen("private.pem", "r"));
    if (!private_key_file) {
        return {false, "Error: Could not open private key file."};
    }

    unique_EVP_PKEY evp_private_key(PEM_read_PrivateKey(private_key_file.get(), nullptr, nullptr, nullptr));
    if (!evp_private_key) {
        return {false, "Error: Failed to read private key."};
    }

    unique_EVP_PKEY_CTX dec_ctx(EVP_PKEY_CTX_new(evp_private_key.get(), nullptr));
    if (!dec_ctx) {
        return {false, "Error: Failed to create EVP_PKEY_CTX."};
    }

    if (EVP_PKEY_decrypt_init(dec_ctx.get()) <= 0) {
        return {false, "Error: Failed to initialize decryption."};
    }
    if (EVP_PKEY_CTX_set_rsa_padding(dec_ctx.get(), RSA_PKCS1_PADDING) <= 0) {
        return {false, "Error: Failed to set RSA padding."};
    }

    size_t decrypted_len;
    // Determine the required buffer size for the decrypted data
    if (EVP_PKEY_decrypt(dec_ctx.get(), nullptr, &decrypted_len, encrypted_data.data(), encrypted_data.size()) <= 0) {
        return {false, "Error: Failed to determine decrypted data length."};
    }

    std::vector<unsigned char> decrypted(decrypted_len);
    if (EVP_PKEY_decrypt(dec_ctx.get(), decrypted.data(), &decrypted_len, encrypted_data.data(), encrypted_data.size()) <= 0) {
        return {false, "Error: Decryption failed."};
    }
    
    // Trim potential null terminators or padding
    decrypted.resize(decrypted_len);

    return {true, std::string(reinterpret_cast<const char*>(decrypted.data()), decrypted.size())};
}

TokenValidationResult is_valid_token(const int seconds, const std::string& token, const std::string& secretb32) noexcept {
    if (token.empty() || secretb32.empty()) {
        return {false, "Invalid parameters: token or secret are empty"};
    }
    if (token.size() != 6 && token.size() != 8) {
        return {false, "Invalid token size"};
    }

    if (oath_init() != OATH_OK) {
        return {false, "Failed to initialize liboath."};
    }

    char* raw_secret = nullptr;
    size_t secretlen = 0;
    auto rc = oath_base32_decode(secretb32.c_str(), secretb32.size(), &raw_secret, &secretlen);
    unique_OATH_secret secret(raw_secret); // RAII for the secret

    if (rc != OATH_OK) {
        oath_done();
        return {false, std::format("liboath oath_base32_decode() failed: {}", oath_strerror(rc))};
    }

    rc = oath_totp_validate(secret.get(), secretlen, time(NULL), seconds, 0, 0, token.c_str());
    if (rc != OATH_OK) {
        oath_done();
        return {false, std::format("liboath oath_totp_validate() failed: {}", oath_strerror(rc))};
    }

    oath_done();
    return {true, ""};
}
