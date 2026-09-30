#include "lostart/Settings.h"

#include "lostart/Util.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>

namespace LA
{
	namespace
	{
		using Ini = std::map<std::string, std::map<std::string, std::string>>;

		const std::string* Find(const Ini& a_ini, std::string_view a_section, std::string_view a_key)
		{
			const auto sec = a_ini.find(ToLower(a_section));
			if (sec == a_ini.end()) {
				return nullptr;
			}
			const auto key = sec->second.find(ToLower(a_key));
			return key == sec->second.end() ? nullptr : &key->second;
		}

		void Read(const Ini& a_ini, std::string_view a_section, std::string_view a_key, bool& a_out)
		{
			if (const auto* v = Find(a_ini, a_section, a_key)) {
				const auto text = ToLower(Trim(*v));
				a_out = !(text == "0" || text == "false" || text == "off" || text.empty());
			}
		}

		void Read(const Ini& a_ini, std::string_view a_section, std::string_view a_key, int& a_out)
		{
			if (const auto* v = Find(a_ini, a_section, a_key)) {
				const auto text = Trim(*v);
				// Accept "3" and "3.0" alike: MCM Helper writes sliders as floats.
				try {
					a_out = static_cast<int>(std::lround(std::stod(text)));
				} catch (...) {
				}
			}
		}

		void Read(const Ini& a_ini, std::string_view a_section, std::string_view a_key, double& a_out)
		{
			if (const auto* v = Find(a_ini, a_section, a_key)) {
				try {
					a_out = std::stod(Trim(*v));
				} catch (...) {
				}
			}
		}

		template <class E>
		void ReadEnum(const Ini& a_ini, std::string_view a_section, std::string_view a_key, E& a_out, int a_max)
		{
			int value = static_cast<int>(a_out);
			Read(a_ini, a_section, a_key, value);
			a_out = static_cast<E>(std::clamp(value, 0, a_max));
		}
	}

	Ini ParseIni(std::string_view a_text)
	{
		Ini         ini;
		std::string section;
		// Tolerate a UTF-8 BOM.
		if (a_text.size() >= 3 && static_cast<unsigned char>(a_text[0]) == 0xEF && static_cast<unsigned char>(a_text[1]) == 0xBB && static_cast<unsigned char>(a_text[2]) == 0xBF) {
			a_text.remove_prefix(3);
		}
		for (const auto& rawLine : Split(a_text, '\n')) {
			auto line = Trim(rawLine);
			if (line.empty() || line[0] == ';' || line[0] == '#') {
				continue;
			}
			if (line.front() == '[') {
				const auto close = line.find(']');
				section = ToLower(Trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1)));
				continue;
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos) {
				continue;
			}
			auto value = Trim(line.substr(eq + 1));
			// Strip trailing comments (MCM Helper never writes them, users might).
			if (const auto comment = value.find_first_of(";#"); comment != std::string::npos) {
				value = Trim(value.substr(0, comment));
			}
			ini[section][ToLower(Trim(line.substr(0, eq)))] = value;
		}
		return ini;
	}

	bool LoadIniFile(const std::filesystem::path& a_path, Ini& a_out)
	{
		std::ifstream file(a_path, std::ios::binary);
		if (!file) {
			return false;
		}
		std::ostringstream ss;
		ss << file.rdbuf();
		for (auto& [section, values] : ParseIni(ss.str())) {
			for (auto& [key, value] : values) {
				a_out[section][key] = value;
			}
		}
		return true;
	}

	void Settings::Apply(const Ini& a_ini)
	{
		Read(a_ini, "General", "bAltars", altars);
		Read(a_ini, "General", "bSpellmakers", spellmakers);
		Read(a_ini, "General", "iEffectNames", effectNames);

		ReadEnum(a_ini, "Costs", "iCostModel", costModel, 2);
		Read(a_ini, "Costs", "bTargetRunningTotal", targetRunningTotal);
		Read(a_ini, "Costs", "fGlobalCostMult", globalCostMult);
		Read(a_ini, "Costs", "fPriceMult", priceMult);
		Read(a_ini, "Costs", "fPriceMultClassic", priceMultClassic);
		Read(a_ini, "Costs", "bHaggling", haggling);
		ReadEnum(a_ini, "Costs", "iAltarFee", altarFee, 3);

		Read(a_ini, "Menu", "iMaxEffects", maxEffects);
		Read(a_ini, "Menu", "iMagnitudeCap", magnitudeCap);
		Read(a_ini, "Menu", "iDurationCap", durationCap);
		Read(a_ini, "Menu", "iAreaCap", areaCap);
		Read(a_ini, "Menu", "bCloseAfterCreate", closeAfterCreate);
		Read(a_ini, "Menu", "iStartingRange", startingRange);
		Read(a_ini, "Menu", "bShowCostMath", showCostMath);

		Read(a_ini, "Casting", "bCastingFailure", castingFailure);
		Read(a_ini, "Casting", "bMinMaxRolls", minMaxRolls);
		Read(a_ini, "Casting", "bElementalRiders", elementalRiders);
		Read(a_ini, "Casting", "iTouchReach", touchReach);
		Read(a_ini, "Casting", "bAreaLOS", areaLOS);
		Read(a_ini, "Casting", "iSummonLimit", summonLimit);
		Read(a_ini, "Casting", "bGraceSlowfall", graceSlowfall);
		Read(a_ini, "Casting", "fJumpPerPoint", jumpPerPoint);
		Read(a_ini, "Casting", "iMarks", marks);
		Read(a_ini, "Casting", "iCastingBonus", castingBonus);
		Read(a_ini, "Casting", "bNPCFailure", npcFailure);
		Read(a_ini, "Casting", "iExperienceSchool", experienceSchool);

		Read(a_ini, "Effects", "iAvailability", availability);
		Read(a_ini, "Effects", "bSkyrimOnly", skyrimOnly);
		Read(a_ini, "Effects", "bExtended", extended);
		Read(a_ini, "Effects", "bPassThrough", passThrough);
		ReadEnum(a_ini, "Effects", "iAttributeProfile", attributeProfile, 2);
		Read(a_ini, "Effects", "iSkillPicker", skillPicker);
		Read(a_ini, "Effects", "iFortifySkillTarget", fortifySkillTarget);
		Read(a_ini, "Effects", "bTomes", tomes);
		Read(a_ini, "Effects", "bBoundSkillBonus", boundSkillBonus);

		Read(a_ini, "Services", "bCollegeMembership", collegeMembership);
		Read(a_ini, "Services", "bRefuseWanted", refuseWanted);
		Read(a_ini, "Services", "bPerNPCPrices", perNPCPrices);

		Read(a_ini, "Spellbook", "iDeletable", deletable);
		Read(a_ini, "Spellbook", "iLoadPricing", loadPricing);
		Read(a_ini, "Spellbook", "bReplaceOnLoad", replaceOnLoad);

		Read(a_ini, "Maintenance", "iLogLevel", logLevel);
		Clamp();
	}

	void Settings::Clamp()
	{
		effectNames = std::clamp(effectNames, 0, 1);
		globalCostMult = std::clamp(globalCostMult, 0.25, 4.0);
		priceMult = std::clamp(priceMult, 1.0, 20.0);
		priceMultClassic = std::clamp(priceMultClassic, 1.0, 20.0);
		maxEffects = std::clamp(maxEffects, 1, 8);
		magnitudeCap = std::clamp(magnitudeCap, 100, 500);
		durationCap = std::clamp(durationCap, 60, 3600);
		areaCap = std::clamp(areaCap, 0, 100);
		startingRange = std::clamp(startingRange, 0, 1);
		touchReach = std::clamp(touchReach, 128, 320);
		summonLimit = std::clamp(summonLimit, 0, 1);
		jumpPerPoint = std::clamp(jumpPerPoint, 1.0, 10.0);
		marks = std::clamp(marks, 1, 10);
		castingBonus = std::clamp(castingBonus, 0, 30);
		experienceSchool = std::clamp(experienceSchool, 0, 1);
		availability = std::clamp(availability, 0, 1);
		skillPicker = std::clamp(skillPicker, 0, 1);
		fortifySkillTarget = std::clamp(fortifySkillTarget, 0, 1);
		deletable = std::clamp(deletable, 0, 1);
		loadPricing = std::clamp(loadPricing, 0, 1);
		logLevel = std::clamp(logLevel, 0, 2);
	}

	Settings LoadSettings(const std::filesystem::path& a_defaults, const std::filesystem::path& a_user)
	{
		Ini ini;
		LoadIniFile(a_defaults, ini);
		LoadIniFile(a_user, ini);
		Settings settings;
		settings.Apply(ini);
		return settings;
	}
}
