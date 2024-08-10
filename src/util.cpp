#include "util.h"

namespace util
{
	std::string today() noexcept
	{
		return std::format("{:%F}", std::chrono::system_clock::now());
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
						out << "\\u" << std::setw(4) << std::setfill('0') << std::hex << static_cast<int>(c);
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
}
