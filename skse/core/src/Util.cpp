#include "lostart/Util.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>

namespace LA
{
	bool IEquals(std::string_view a_lhs, std::string_view a_rhs)
	{
		return a_lhs.size() == a_rhs.size() &&
		       std::equal(a_lhs.begin(), a_lhs.end(), a_rhs.begin(), [](char a, char b) {
				   return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
			   });
	}

	std::string ToLower(std::string_view a_text)
	{
		std::string out(a_text);
		std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return out;
	}

	bool IContains(std::string_view a_haystack, std::string_view a_needle)
	{
		if (a_needle.empty()) {
			return true;
		}
		return ToLower(a_haystack).find(ToLower(a_needle)) != std::string::npos;
	}

	std::string Trim(std::string_view a_text)
	{
		const auto first = a_text.find_first_not_of(" \t\r\n");
		if (first == std::string_view::npos) {
			return {};
		}
		const auto last = a_text.find_last_not_of(" \t\r\n");
		return std::string(a_text.substr(first, last - first + 1));
	}

	std::vector<std::string> Split(std::string_view a_text, char a_delim)
	{
		std::vector<std::string> parts;
		std::size_t              start = 0;
		while (true) {
			const auto pos = a_text.find(a_delim, start);
			parts.emplace_back(a_text.substr(start, pos == std::string_view::npos ? std::string_view::npos : pos - start));
			if (pos == std::string_view::npos) {
				break;
			}
			start = pos + 1;
		}
		return parts;
	}

	namespace
	{
		void AppendUtf8(std::string& a_out, std::uint32_t a_cp)
		{
			if (a_cp < 0x80) {
				a_out.push_back(static_cast<char>(a_cp));
			} else if (a_cp < 0x800) {
				a_out.push_back(static_cast<char>(0xC0 | (a_cp >> 6)));
				a_out.push_back(static_cast<char>(0x80 | (a_cp & 0x3F)));
			} else if (a_cp < 0x10000) {
				a_out.push_back(static_cast<char>(0xE0 | (a_cp >> 12)));
				a_out.push_back(static_cast<char>(0x80 | ((a_cp >> 6) & 0x3F)));
				a_out.push_back(static_cast<char>(0x80 | (a_cp & 0x3F)));
			} else {
				a_out.push_back(static_cast<char>(0xF0 | (a_cp >> 18)));
				a_out.push_back(static_cast<char>(0x80 | ((a_cp >> 12) & 0x3F)));
				a_out.push_back(static_cast<char>(0x80 | ((a_cp >> 6) & 0x3F)));
				a_out.push_back(static_cast<char>(0x80 | (a_cp & 0x3F)));
			}
		}

		// Decodes one code point; returns bytes consumed (at least 1).
		std::size_t DecodeUtf8(std::string_view a_text, std::size_t a_pos, std::uint32_t& a_cp)
		{
			const auto c = static_cast<unsigned char>(a_text[a_pos]);
			std::size_t len = 1;
			if (c >= 0xF0) {
				len = 4;
				a_cp = c & 0x07;
			} else if (c >= 0xE0) {
				len = 3;
				a_cp = c & 0x0F;
			} else if (c >= 0xC0) {
				len = 2;
				a_cp = c & 0x1F;
			} else {
				a_cp = c;
				return 1;
			}
			if (a_pos + len > a_text.size()) {
				a_cp = 0xFFFD;
				return 1;
			}
			for (std::size_t i = 1; i < len; ++i) {
				a_cp = (a_cp << 6) | (static_cast<unsigned char>(a_text[a_pos + i]) & 0x3F);
			}
			return len;
		}
	}

	std::string Utf16LeToUtf8(const std::vector<std::uint8_t>& a_bytes)
	{
		std::string out;
		std::size_t i = 0;
		if (a_bytes.size() >= 2 && a_bytes[0] == 0xFF && a_bytes[1] == 0xFE) {
			i = 2;
		}
		out.reserve(a_bytes.size() / 2);
		for (; i + 1 < a_bytes.size(); i += 2) {
			std::uint32_t unit = a_bytes[i] | (a_bytes[i + 1] << 8);
			if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < a_bytes.size()) {
				const std::uint32_t low = a_bytes[i + 2] | (a_bytes[i + 3] << 8);
				if (low >= 0xDC00 && low <= 0xDFFF) {
					unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
					i += 2;
				}
			}
			AppendUtf8(out, unit);
		}
		return out;
	}

	std::vector<std::uint8_t> Utf8ToUtf16Le(std::string_view a_text, bool a_bom)
	{
		std::vector<std::uint8_t> out;
		if (a_bom) {
			out.push_back(0xFF);
			out.push_back(0xFE);
		}
		auto put = [&](std::uint32_t a_unit) {
			out.push_back(static_cast<std::uint8_t>(a_unit & 0xFF));
			out.push_back(static_cast<std::uint8_t>((a_unit >> 8) & 0xFF));
		};
		for (std::size_t pos = 0; pos < a_text.size();) {
			std::uint32_t cp = 0;
			pos += DecodeUtf8(a_text, pos, cp);
			if (cp >= 0x10000) {
				cp -= 0x10000;
				put(0xD800 + (cp >> 10));
				put(0xDC00 + (cp & 0x3FF));
			} else {
				put(cp);
			}
		}
		return out;
	}

	std::size_t Utf8Length(std::string_view a_text)
	{
		std::size_t count = 0;
		for (std::size_t pos = 0; pos < a_text.size(); ++count) {
			std::uint32_t cp = 0;
			pos += DecodeUtf8(a_text, pos, cp);
		}
		return count;
	}

	std::string Utf8Truncate(std::string_view a_text, std::size_t a_maxChars)
	{
		std::size_t pos = 0;
		for (std::size_t count = 0; pos < a_text.size() && count < a_maxChars; ++count) {
			std::uint32_t cp = 0;
			pos += DecodeUtf8(a_text, pos, cp);
		}
		return std::string(a_text.substr(0, pos));
	}

	std::string FormRef::ToString() const
	{
		char buffer[16];
		std::snprintf(buffer, sizeof(buffer), "0x%06X", static_cast<unsigned>(localId));
		return plugin + "|" + buffer;
	}

	FormRef ParseFormRef(std::string_view a_text)
	{
		FormRef    ref;
		const auto bar = a_text.find('|');
		if (bar == std::string_view::npos) {
			return ref;
		}
		auto idText = Trim(a_text.substr(bar + 1));
		if (idText.size() > 2 && idText[0] == '0' && (idText[1] == 'x' || idText[1] == 'X')) {
			idText = idText.substr(2);
		}
		std::uint32_t id = 0;
		const auto [ptr, ec] = std::from_chars(idText.data(), idText.data() + idText.size(), id, 16);
		if (ec != std::errc{} || ptr != idText.data() + idText.size()) {
			return ref;
		}
		ref.plugin = Trim(a_text.substr(0, bar));
		ref.localId = id & 0x00FFFFFF;
		return ref;
	}

	std::string FormatPattern(std::string_view a_pattern, const std::vector<std::string>& a_args)
	{
		std::string out;
		out.reserve(a_pattern.size() + 16);
		for (std::size_t i = 0; i < a_pattern.size(); ++i) {
			if (a_pattern[i] == '{' && i + 2 < a_pattern.size() && std::isdigit(static_cast<unsigned char>(a_pattern[i + 1])) && a_pattern[i + 2] == '}') {
				const auto index = static_cast<std::size_t>(a_pattern[i + 1] - '0');
				if (index < a_args.size()) {
					out += a_args[index];
				}
				i += 2;
			} else {
				out.push_back(a_pattern[i]);
			}
		}
		return out;
	}
}
