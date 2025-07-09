#include "util.h"

namespace {
	
	size_t get_proc_info(const std::string& filename, const std::string& token) noexcept
	{
		size_t total_memory = 0;
		if (std::ifstream meminfo_file(filename); meminfo_file.is_open()) {
			std::string line;
			while (std::getline(meminfo_file, line)) {
				if (line.starts_with(token)) {
					std::istringstream iss{line};
					std::string label;
					iss >> label >> total_memory;
					break;
				}
			}
		} 
		return total_memory;
	}
	
}

namespace util
{
	std::string today() noexcept
	{
		return std::format("{:%F}", std::chrono::system_clock::now());
	}
	
	std::string current_timestamp() noexcept
	{
		return std::format("{:%FT%T}", std::chrono::get_tzdb().current_zone()->to_local(std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now())));
	}	
	
	std::string encode_json(const std::string& s) noexcept
	{
		std::ostringstream out;
		for (char c : s) {
			switch (c) {
				case '\\': out << R"(\\)"; break;
				case '\"': out << R"(\")"; break;
				case '\b': out << "\\b";  break;
				case '\f': out << "\\f";  break;
				case '\n': out << "\\n";  break;
				case '\r': out << "\\r";  break;
				case '\t': out << "\\t";  break;
				default:
					if (' ' <= c && c <= '~') {
						out << c;
					} else {
						out << std::format("\\u{:04x}", static_cast<unsigned char>(c));
					}
			}
		}
		return out.str();
	}
	
	std::string encode_sql(std::string_view s) noexcept
	{
		std::ostringstream out;
		for (char c : s) {
			switch (c) {
				case '\\': out << R"(\\)"; break;
				case '\'': out << R"('')"; break;
				default:
					out << c;
			}
		}
		return out.str();
	}
	
	size_t get_total_memory() noexcept
	{
		return get_proc_info("/proc/meminfo", "MemTotal:");
	}
	
	size_t get_memory_usage() noexcept
	{
		return get_proc_info("/proc/self/status", "VmRSS:");
	}	

	std::string decode_base64(const std::string& base64) {
		static const std::string base64Chars =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			"abcdefghijklmnopqrstuvwxyz"
			"0123456789+/";

		// Static decoding table to avoid recomputation
		static std::vector<int> decodingTable = []() {
			std::vector<int> table(256, -1);
			for (size_t i = 0; i < base64Chars.size(); ++i) {
				table[static_cast<unsigned char>(base64Chars[i])] = static_cast<int>(i);
			}
			return table;
		}();

		if (base64.empty()) {
			return ""; // Return early for empty input
		}

		// Reserve capacity for the decoded string
		std::string decodedString;
		decodedString.reserve((base64.length() * 3) / 4);

		int bitGroup = 0; // Accumulates bits from the input
		int bitCount = 0; // Tracks how many bits are in the accumulator

		for (char c : base64) {
			if (std::isspace(c)) continue; // Skip whitespace
			if (c == '=') break;          // Stop at padding

			int value = decodingTable[static_cast<unsigned char>(c)];
			if (value == -1) {
				throw std::invalid_argument("Invalid Base64 character encountered.");
			}

			// Accumulate bits and extract bytes
			bitGroup = (bitGroup << 6) | value;
			bitCount += 6;

			while (bitCount >= 8) {
				bitCount -= 8;
				decodedString += static_cast<char>((bitGroup >> bitCount) & 0xFF);
			}
		}

		return decodedString;
	}
	
}
