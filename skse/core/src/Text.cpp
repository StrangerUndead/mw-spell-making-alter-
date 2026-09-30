#include "lostart/Text.h"

#include "lostart/Util.h"

#include <fstream>
#include <iterator>

namespace LA
{
	bool StringTable::LoadFile(const std::filesystem::path& a_path)
	{
		std::ifstream file(a_path, std::ios::binary);
		if (!file) {
			return false;
		}
		std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		// Accept UTF-8 too (with or without BOM) for development convenience.
		const bool utf16 = bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE;
		if (utf16) {
			LoadUtf8(Utf16LeToUtf8(bytes));
		} else {
			std::string text(bytes.begin(), bytes.end());
			if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF) {
				text.erase(0, 3);
			}
			LoadUtf8(text);
		}
		return true;
	}

	void StringTable::LoadUtf8(std::string_view a_text)
	{
		for (const auto& raw : Split(a_text, '\n')) {
			std::string line = raw;
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			const auto tab = line.find('\t');
			if (tab == std::string::npos || line.empty() || line[0] != '$') {
				continue;
			}
			_strings[line.substr(0, tab)] = line.substr(tab + 1);
		}
	}

	std::string StringTable::Get(std::string_view a_key) const
	{
		const auto it = _strings.find(std::string(a_key));
		return it != _strings.end() ? it->second : std::string(a_key);
	}

	std::string EffectFormatter::EffectName(const EffectDef& a_def) const
	{
		if (_skyrimNames) {
			const auto key = "$LA_EffectSk_" + a_def.pascal;
			if (_strings.Has(key)) {
				return _strings.Get(key);
			}
		}
		return a_def.nameKey.empty() ? a_def.id : _strings.Get(a_def.nameKey);
	}

	std::string EffectFormatter::TargetName(TargetKind a_kind, std::int16_t a_sub) const
	{
		if (a_kind == TargetKind::kAttribute) {
			for (const auto& attr : _catalog.Attributes()) {
				if (attr.index == a_sub) {
					return _strings.Get(attr.nameKey);
				}
			}
			return _strings.Get("$LA_Attr_" + std::string(Catalog::AttributeName(a_sub)));
		}
		if (a_kind == TargetKind::kSkill) {
			if (a_sub >= kMwSkillBase) {
				for (const auto& skill : _catalog.MwSkills()) {
					if (skill.index == a_sub - kMwSkillBase) {
						return _strings.Get(skill.nameKey);
					}
				}
				return {};
			}
			return _strings.Get("$LA_Skill_" + std::string(Catalog::SkyrimSkillName(a_sub)));
		}
		return {};
	}

	std::string EffectFormatter::EffectName(const EffectDef& a_def, std::int16_t a_sub) const
	{
		if (a_def.target == TargetKind::kNone || a_sub == kNoSub) {
			return EffectName(a_def);
		}
		const auto target = TargetName(a_def.target, a_sub);
		const auto fmtKey = "$LA_EffectFmt_" + a_def.pascal;
		if (_strings.Has(fmtKey)) {
			return FormatPattern(_strings.Get(fmtKey), { target });
		}
		// Fallback: replace the family word ("Fortify Attribute" -> "Fortify Strength").
		auto       name = EffectName(a_def);
		const auto word = a_def.target == TargetKind::kAttribute ? std::string("Attribute") : std::string("Skill");
		if (const auto pos = name.rfind(word); pos != std::string::npos) {
			name.replace(pos, word.size(), target);
		}
		return name;
	}

	std::string EffectFormatter::MagnitudeText(const EffectDef& a_def, std::uint16_t a_min, std::uint16_t a_max) const
	{
		const auto hi = std::max(a_min, a_max);
		std::string text = std::to_string(a_min);
		if (hi != a_min) {
			text += " " + _strings.Get("$LA_Fmt_To") + " " + std::to_string(hi);
		}
		switch (a_def.unit) {
		case Unit::kPercent:
			text += _strings.Get("$LA_Unit_percent");
			break;
		case Unit::kPoints:
			text += " " + _strings.Get(hi == 1 ? "$LA_Unit_pt" : "$LA_Unit_pts");
			break;
		case Unit::kFeet:
			text += " " + _strings.Get("$LA_Unit_ft");
			break;
		case Unit::kLevel:
			text += " " + _strings.Get(hi == 1 ? "$LA_Unit_Level" : "$LA_Unit_Levels");
			break;
		case Unit::kNone:
			break;
		}
		return text;
	}

	std::string EffectFormatter::RangeName(Range a_range) const
	{
		return _strings.Get("$LA_Range_" + std::string(ToString(a_range)));
	}

	std::string EffectFormatter::SchoolName(School a_school) const
	{
		return _strings.Get("$LA_School_" + std::string(ToString(a_school)));
	}

	std::string EffectFormatter::RankName(Rank a_rank) const
	{
		return _strings.Get("$LA_Rank_" + std::string(ToString(a_rank)));
	}

	std::string EffectFormatter::Message(std::string_view a_name) const
	{
		return _strings.Get("$LA_Msg_" + std::string(a_name));
	}

	std::string EffectFormatter::Line(const SpellEffect& a_effect) const
	{
		const auto* def = _catalog.Find(a_effect.effectId);
		if (!def) {
			return a_effect.effectId;
		}
		std::string line = EffectName(*def, a_effect.sub);
		if (def->hasMagnitude) {
			line += " " + MagnitudeText(*def, a_effect.minMag, a_effect.maxMag);
		}
		// Morrowind hides a 1-second duration ("Fire Damage 3 to 10 pts on Touch").
		if (def->hasDuration && a_effect.duration > 1) {
			line += " " + _strings.Get("$LA_Fmt_For") + " " + std::to_string(a_effect.duration) + " " + _strings.Get("$LA_Unit_secs");
		}
		if (def->ShowsArea(a_effect.range) && a_effect.area > 0) {
			line += " " + _strings.Get("$LA_Fmt_In") + " " + std::to_string(a_effect.area) + " " + _strings.Get("$LA_Unit_ft");
		}
		line += " " + _strings.Get("$LA_Fmt_On") + " " + RangeName(a_effect.range);
		return line;
	}
}
