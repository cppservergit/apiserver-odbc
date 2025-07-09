#include "jwt.h"

namespace 
{
	constexpr const char* LOGGER_SRC {"jwt"};
	
	struct jwt_config {
		std::string secret;
		unsigned short int duration;
		jwt_config() {
			duration = env::jwt_expiration();
			secret = env::get_str("CPP_JWT_SECRET");
			if (secret.empty())
				logger::log(LOGGER_SRC, "error", "environment variable CPP_JWT_SECRET not defined");
		}
	};
	
	struct json_token
	{
		std::string header;
		std::string header_encoded;
		std::string payload;
		std::string payload_encoded;
		std::string signature;
	};	
	

	/**
	 * @brief Base64 URL-safe encoding alphabet (no padding).
	 */
	consteval std::string_view base64url_alphabet() {
		return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	}

	/**
	 * @brief Maps a single Base64URL character to its 6-bit value.
	 * @param c The Base64URL character.
	 * @return The decoded 6-bit value, or 0xFF if invalid.
	 */
	consteval unsigned char make_decode_table_entry(char c) {
		if (c >= 'A' && c <= 'Z') return static_cast<unsigned char>(c - 'A');
		if (c >= 'a' && c <= 'z') return static_cast<unsigned char>(c - 'a' + 26);
		if (c >= '0' && c <= '9') return static_cast<unsigned char>(c - '0' + 52);
		if (c == '-') return 62;
		if (c == '_') return 63;
		return 0xFF;
	}

	/**
	 * @brief Generates a lookup table for decoding Base64URL characters.
	 * @return An array mapping each byte to its decoded 6-bit value or 0xFF if invalid.
	 */
	consteval auto generate_decode_table() {
		std::array<unsigned char, 256> table{};
		for (auto& v : table) v = 0xFF;
		for (char c : base64url_alphabet())
			table[static_cast<unsigned char>(c)] = make_decode_table_entry(c);
		return table;
	}

	/**
	 * @brief Precomputed decoding lookup table, stored at compile time.
	 */
	constexpr auto decode_table = generate_decode_table();

	/**
	 * @brief Concept for containers that support push_back with byte values.
	 */
	template<typename T>
	concept PushBackByteContainer = requires(T c, typename T::value_type val) {
		{ c.push_back(val) } -> std::same_as<void>;
		typename T::value_type;
		requires std::is_same_v<typename T::value_type, char> ||
				 std::is_same_v<typename T::value_type, unsigned char>;
	};

	/**
	 * @brief Encodes a sequence of bytes to a Base64URL string.
	 * @param input Input byte span.
	 * @return Encoded Base64URL string without padding.
	 */
	[[nodiscard]] inline std::string base64_encode_impl(std::span<const std::byte> input) {
		std::string output;
		output.reserve((input.size() * 4 + 2) / 3);

		for (size_t i = 0; i < input.size(); i += 3) {
			uint32_t n = std::to_integer<uint32_t>(input[i]) << 16;
			if (i + 1 < input.size()) n |= std::to_integer<uint32_t>(input[i + 1]) << 8;
			if (i + 2 < input.size()) n |= std::to_integer<uint32_t>(input[i + 2]);

			const auto& alphabet = base64url_alphabet();
			output.push_back(alphabet[(n >> 18) & 63]);
			output.push_back(alphabet[(n >> 12) & 63]);
			if (i + 1 < input.size()) output.push_back(alphabet[(n >> 6) & 63]);
			if (i + 2 < input.size()) output.push_back(alphabet[n & 63]);
		}

		return output;
	}

	/**
	 * @brief Encodes a string view to Base64URL.
	 * @param input UTF-8 input string.
	 * @return Encoded Base64URL string.
	 */
	[[nodiscard]] inline std::string base64_encode(std::string_view input) {
		return base64_encode_impl(std::as_bytes(std::span(input)));
	}

	/**
	 * @brief Encodes binary data to Base64URL.
	 * @param input A vector of unsigned bytes.
	 * @return Encoded Base64URL string.
	 */
	[[nodiscard]] inline std::string base64_encode(const std::vector<unsigned char>& input) {
		return base64_encode_impl(std::as_bytes(std::span(input)));
	}

	/**
	 * @brief Validates whether the input is well-formed Base64URL.
	 * @param input Input string.
	 * @return True if valid characters and length mod 4 != 1.
	 */
	[[nodiscard]] inline bool is_valid_base64url(std::string_view input) {
		if (input.size() % 4 == 1) return false;

		return std::ranges::all_of(input, [](char c) {
			return decode_table[static_cast<unsigned char>(c)] != 0xFF;
		});
	}

	/**
	 * @brief Shared decoding implementation.
	 * @tparam Output A container with push_back and value_type as char or unsigned char.
	 * @param input Input Base64URL string.
	 * @return Optional output container or nullopt if invalid input.
	 */
	template<PushBackByteContainer Output>
	[[nodiscard]] inline std::optional<Output> base64_decode_impl(std::string_view input) {
		if (!is_valid_base64url(input)) return std::nullopt;

		Output output;
		output.reserve((input.size() * 3) / 4);

		uint32_t buffer = 0;
		int bits_collected = 0;

		for (char c : input) {
			const unsigned char value = decode_table[static_cast<unsigned char>(c)];
			buffer = (buffer << 6) | value;
			bits_collected += 6;
			if (bits_collected >= 8) {
				bits_collected -= 8;
				output.push_back(static_cast<typename Output::value_type>(
					(buffer >> bits_collected) & 0xFF));
			}
		}

		return output;
	}

	/**
	 * @brief Decodes a Base64URL string into a UTF-8 string.
	 * @param input Encoded Base64URL input.
	 * @return Optional decoded string or nullopt on failure.
	 */
	[[nodiscard]] inline std::optional<std::string> base64_decode(std::string_view input) {
		return base64_decode_impl<std::string>(input);
	}

	/**
	 * @brief Decodes a Base64URL string into raw binary.
	 * @param input Encoded Base64URL input.
	 * @return Optional decoded byte vector or nullopt on failure.
	 */
	[[nodiscard]] inline std::optional<std::vector<unsigned char>> base64_decode_bytes(std::string_view input) {
		return base64_decode_impl<std::vector<unsigned char>>(input);
	}



	std::string sign(std::string_view message, std::string_view secret) 
	{
		std::vector<unsigned char> msg {message.begin(), message.end()};
		std::vector<unsigned char> signature_bytes(EVP_MAX_MD_SIZE);
		unsigned int signature_length {0};
		HMAC(EVP_sha256(), secret.data(), secret.size(), msg.data(), msg.size(), signature_bytes.data(), &signature_length);
		signature_bytes.resize(signature_length);
		return base64_encode(signature_bytes);
	}
	
	json_token parse(std::string_view token)
	{
		json_token jt;
		size_t pos {0};
		if (auto pos1 = token.find(".", pos); pos1 != std::string::npos) {
            jt.header_encoded = token.substr(pos,  pos1);
			pos = pos1 + 1;
			if (auto pos2 = token.find(".", pos + 1); pos2 != std::string::npos) {
				jt.payload_encoded = token.substr(pos,  pos2 - pos);
				pos = pos2 + 1;
				jt.signature = token.substr(pos);
				if (auto decoded = base64_decode(jt.header_encoded))
					jt.header = std::move(*decoded);
				else
					logger::log(LOGGER_SRC, "warning", "error decoding header");
				if (auto decoded = base64_decode(jt.payload_encoded))
					jt.payload = std::move(*decoded);
				else
					logger::log(LOGGER_SRC, "warning", "error decoding payload");
			} else
				logger::log(LOGGER_SRC, "warning", "invalid token format - cannot find second '.'");
		} else
			logger::log(LOGGER_SRC, "warning", "invalid token format - cannot find first '.'");
		return jt;
	}
	
	jwt::user_info parse_payload(const std::string& payload)
	{
		json::json_parser p(payload);
		auto fields {p.get_map()};
		return jwt::user_info {
				fields["sid"], 
				fields["login"], 
				fields["mail"], 
				fields["roles"], 
				std::stol(fields["exp"])
			};
	}
}

namespace jwt
{
	std::string get_token(std::string_view sessionid, std::string_view username, std::string_view mail, std::string_view roles) 
	{
		static jwt_config config;
		const time_t now {std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) + config.duration}; 
		const std::string json_header {R"({"alg":"HS256","typ":"JWT"})"};
		//const std::string json_payload {std::format(R"({{"sid":"{}","login":"{}","mail":"{}","roles":"{}","exp":{}}})", sessionid, username, mail, roles, now)};
		const std::string json_payload = std::format(
			R"({{"sid":"{}","login":"{}","mail":"{}","roles":"{}","exp":{}}})",
			sessionid,
			username,
			mail,
			roles,
			now);		
		std::string buffer {base64_encode(json_header) + "." + base64_encode(json_payload)};
		auto signature {sign(buffer, config.secret)};
		return buffer.append(".").append(signature);
	}
	
	std::pair<bool, user_info> is_valid(const std::string& token)	
	{
		static jwt_config config;
		auto jt {parse(token)};
		
		if (jt.header.empty() || jt.payload.empty()) //decoding error
			return std::make_pair(false, user_info());
		
		if (const std::string test{jt.header_encoded + "." + jt.payload_encoded}; jt.signature != sign(test, config.secret)) {
			logger::log(LOGGER_SRC, "warning", "invalid signature");
			return std::make_pair(false, user_info());
		}
		auto user {parse_payload(jt.payload)};
		const time_t now {std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())};
		if (now < user.exp)
			return std::make_pair(true, user);
		else {
			logger::log(LOGGER_SRC, "warning", "expired token");
			return std::make_pair(false, user_info());
		}
	}
	
	std::string get_signature(std::string_view message) 
	{
		static jwt_config config;
		return sign(base64_encode(message), config.secret);
	}
}
