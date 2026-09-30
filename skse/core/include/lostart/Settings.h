#pragma once

#include "lostart/Types.h"

#include <filesystem>
#include <map>
#include <string>

namespace LA
{
	enum class AltarFee : std::uint8_t
	{
		kFull = 0,
		kHalf = 1,
		kFree = 2,
		kFilledSoulGem = 3
	};

	enum class AttributeProfile : std::uint8_t
	{
		kDefault = 0,
		kLight = 1,
		kOff = 2
	};

	// Every MCM option (CONTRACTS.md section 5), with the outline's defaults.
	struct Settings
	{
		// General
		bool altars{ true };
		bool spellmakers{ true };
		int  effectNames{ 0 };  // 0 Morrowind, 1 Skyrim

		// Costs
		CostModel costModel{ CostModel::kSkyrimBalanced };
		bool      targetRunningTotal{ true };
		double    globalCostMult{ 1.0 };
		double    priceMult{ 3.0 };
		double    priceMultClassic{ 7.0 };
		bool      haggling{ false };
		AltarFee  altarFee{ AltarFee::kFull };

		// Menu
		int  maxEffects{ 8 };
		int  magnitudeCap{ 100 };
		int  durationCap{ 1440 };
		int  areaCap{ 50 };
		bool closeAfterCreate{ true };
		int  startingRange{ 0 };  // 0 OpenMW rule, 1 first allowed
		bool showCostMath{ false };

		// Casting
		bool   castingFailure{ false };
		bool   minMaxRolls{ true };
		bool   elementalRiders{ true };
		int    touchReach{ 192 };
		bool   areaLOS{ true };
		int    summonLimit{ 0 };  // 0 Skyrim, 1 Morrowind
		bool   graceSlowfall{ false };
		double jumpPerPoint{ 3.0 };
		int    marks{ 1 };
		int    castingBonus{ 15 };
		bool   npcFailure{ false };
		int    experienceSchool{ 0 };  // 0 costliest, 1 hardest

		// Effects
		int              availability{ 0 };  // 0 known, 1 sandbox
		bool             skyrimOnly{ true };
		bool             extended{ false };
		bool             passThrough{ true };
		AttributeProfile attributeProfile{ AttributeProfile::kDefault };
		int              skillPicker{ 0 };        // 0 Skyrim 18, 1 Morrowind 27
		int              fortifySkillTarget{ 0 }; // 0 level, 1 percent
		bool             tomes{ true };
		bool             boundSkillBonus{ false };

		// Services
		bool collegeMembership{ true };
		bool refuseWanted{ true };
		bool perNPCPrices{ false };

		// Spellbook
		int  deletable{ 1 };    // 0 custom only, 1 any non-inherent
		int  loadPricing{ 0 };  // 0 full, 1 difference
		bool replaceOnLoad{ false };

		// Maintenance
		int logLevel{ 0 };

		double PriceMultiplier() const { return costModel == CostModel::kClassic ? priceMultClassic : priceMult; }

		// Applies "[Section] key=value" pairs; unknown keys are ignored. Values are clamped
		// to the ranges the MCM offers.
		void Apply(const std::map<std::string, std::map<std::string, std::string>>& a_ini);
		void Clamp();
	};

	// Minimal INI reader for MCM Helper files: sections, key=value, ';' and '#' comments.
	// Keys and sections are matched case-insensitively (stored lowercased).
	std::map<std::string, std::map<std::string, std::string>> ParseIni(std::string_view a_text);
	bool LoadIniFile(const std::filesystem::path& a_path, std::map<std::string, std::map<std::string, std::string>>& a_out);

	// Loads defaults (MCM/Config/LostArt/settings.ini) then user values (MCM/Settings/LostArt.ini).
	Settings LoadSettings(const std::filesystem::path& a_defaults, const std::filesystem::path& a_user);
}
