#pragma once

#include "lostart/Catalog.h"
#include "lostart/Types.h"

#include <filesystem>
#include <string>
#include <unordered_map>

namespace LA
{
	// Skyrim translation file: UTF-16 LE, "$KEY<TAB>Value" per line.
	class StringTable
	{
	public:
		bool LoadFile(const std::filesystem::path& a_path);
		void LoadUtf8(std::string_view a_text);  // same format, already UTF-8
		void Set(std::string a_key, std::string a_value) { _strings[std::move(a_key)] = std::move(a_value); }

		// Returns the translation, or the key itself (as Skyrim does) when missing.
		std::string Get(std::string_view a_key) const;
		bool        Has(std::string_view a_key) const { return _strings.contains(std::string(a_key)); }
		std::size_t Size() const { return _strings.size(); }

	private:
		std::unordered_map<std::string, std::string> _strings;
	};

	// Builds Morrowind-style effect lines: "Fire Damage 5 to 10 pts for 3 secs in 10 ft on Target".
	class EffectFormatter
	{
	public:
		EffectFormatter(const Catalog& a_catalog, const StringTable& a_strings, bool a_skyrimNames = false) :
			_catalog(a_catalog), _strings(a_strings), _skyrimNames(a_skyrimNames)
		{}

		std::string EffectName(const EffectDef& a_def) const;
		std::string EffectName(const EffectDef& a_def, std::int16_t a_sub) const;  // "Fortify Strength"
		std::string TargetName(TargetKind a_kind, std::int16_t a_sub) const;
		std::string Line(const SpellEffect& a_effect) const;
		std::string MagnitudeText(const EffectDef& a_def, std::uint16_t a_min, std::uint16_t a_max) const;
		std::string RangeName(Range a_range) const;
		std::string SchoolName(School a_school) const;
		std::string RankName(Rank a_rank) const;
		std::string Message(std::string_view a_name) const;  // "$LA_Msg_<name>"

	private:
		const Catalog&     _catalog;
		const StringTable& _strings;
		bool               _skyrimNames;
	};
}
