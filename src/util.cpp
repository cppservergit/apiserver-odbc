#include "util.h"

namespace {
	
	size_t get_proc_info(const std::string& filename, const std::string& token) noexcept
	{
		size_t total_memory = 0;
		std::ifstream meminfo_file(filename);
		if (meminfo_file.is_open()) {
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
	
}
