#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace LA
{
	// Floors with a tiny tolerance so values that are integral on paper (41.0 computed as
	// 40.99999999) don't lose a point to binary rounding.
	inline std::int64_t FloorTol(double a_value)
	{
		return static_cast<std::int64_t>(std::floor(a_value + 1e-9));
	}

	bool        IEquals(std::string_view a_lhs, std::string_view a_rhs);
	bool        IContains(std::string_view a_haystack, std::string_view a_needle);
	std::string ToLower(std::string_view a_text);
	std::string Trim(std::string_view a_text);
	std::vector<std::string> Split(std::string_view a_text, char a_delim);

	// UTF-16 LE (optional BOM) to UTF-8.
	std::string Utf16LeToUtf8(const std::vector<std::uint8_t>& a_bytes);
	// UTF-8 to UTF-16 LE with BOM.
	std::vector<std::uint8_t> Utf8ToUtf16Le(std::string_view a_text, bool a_bom = true);

	// Number of Unicode code points in a UTF-8 string.
	std::size_t Utf8Length(std::string_view a_text);
	// Truncates to at most a_maxChars code points without splitting a sequence.
	std::string Utf8Truncate(std::string_view a_text, std::size_t a_maxChars);

	// Parses "Skyrim.esm|0x012FD0" into its plugin and local id parts.
	struct FormRef
	{
		std::string   plugin;
		std::uint32_t localId{ 0 };

		bool        Valid() const { return !plugin.empty(); }
		std::string ToString() const;
		bool        operator==(const FormRef&) const = default;
	};
	FormRef ParseFormRef(std::string_view a_text);

	// Substitutes {0}, {1}, ... in a_pattern.
	std::string FormatPattern(std::string_view a_pattern, const std::vector<std::string>& a_args);
}
