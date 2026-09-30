#pragma once

#include "lostart/Catalog.h"
#include "lostart/Settings.h"
#include "lostart/Text.h"

#include <cmath>
#include <filesystem>

// A small hand-built catalog with Morrowind's base costs, so the golden vectors don't depend on
// the data files. TestData.cpp re-runs the same vectors against data/effects.
namespace LA::Test
{
	inline EffectDef Make(std::string a_id, double a_mwCost, School a_school, RangeMask a_ranges,
		bool a_magnitude = true, bool a_duration = true, bool a_area = true, bool a_ticking = false)
	{
		EffectDef def;
		def.id = std::move(a_id);
		def.pascalName = Catalog::PascalFromId(def.id);
		def.nameKey = "$LA_Effect_" + def.pascalName;
		def.mwBaseCost = a_mwCost;
		def.school = a_school;
		def.ranges = a_ranges;
		def.hasMagnitude = a_magnitude;
		def.unit = a_magnitude ? Unit::kPoints : Unit::kNone;
		def.hasDuration = a_duration;
		def.hasArea = a_area;
		def.ticking = a_ticking;
		return def;
	}

	inline constexpr RangeMask kAll = 0b111;
	inline constexpr RangeMask kSelfOnly = 0b001;
	inline constexpr RangeMask kTouchTarget = 0b110;

	// Skyrim-balanced fire base cost fitted so a rebuilt Firebolt (25 pts, instant) costs 41.
	inline double FireBase() { return 41.0 / std::pow(25.0, 1.1); }

	inline Catalog MakeCatalog()
	{
		Catalog c;
		auto fire = Make("mw.fire_damage", 5, School::kDestruction, kAll, true, true, true, true);
		fire.skBaseCost = FireBase();
		fire.vanillaTargetPriced = true;
		fire.hostile = true;
		fire.riders = { "IntenseFlames", "Impact" };
		fire.rankLadder = { { Rank::kApprentice, 25 }, { Rank::kAdept, 40 }, { Rank::kExpert, 60 }, { Rank::kMaster, 100 } };
		c.Add(fire);
		auto frost = Make("mw.frost_damage", 5, School::kDestruction, kAll, true, true, true, true);
		frost.skBaseCost = FireBase();
		frost.vanillaTargetPriced = true;
		frost.hostile = true;
		frost.riders = { "FrostStaminaSlow", "DeepFreeze" };
		c.Add(frost);
		auto shock = Make("mw.shock_damage", 7, School::kDestruction, kAll, true, true, true, true);
		shock.skBaseCost = FireBase();
		shock.vanillaTargetPriced = true;
		shock.hostile = true;
		c.Add(shock);
		auto fortifyHealth = Make("mw.fortify_health", 1, School::kRestoration, kAll);
		fortifyHealth.skBaseCost = 0.5;
		c.Add(fortifyHealth);
		auto restoreHealth = Make("mw.restore_health", 5, School::kRestoration, kAll, true, true, true, true);
		restoreHealth.skBaseCost = 0.7;
		c.Add(restoreHealth);
		c.Add(Make("mw.levitate", 3, School::kAlteration, kAll));
		auto absorb = Make("mw.absorb_health", 8, School::kDestruction, kTouchTarget, true, true, true, true);
		absorb.hostile = true;
		c.Add(absorb);
		auto mark = Make("mw.mark", 350, School::kConjuration, kSelfOnly, false, false, false);
		mark.unit = Unit::kNone;
		c.Add(mark);
		c.Add(Make("mw.recall", 350, School::kConjuration, kSelfOnly, false, false, false));
		c.Add(Make("mw.divine_intervention", 150, School::kConjuration, kSelfOnly, false, false, false));
		auto shield = Make("mw.shield", 2, School::kAlteration, kAll);
		shield.rankLadder = { { Rank::kNovice, 40 }, { Rank::kApprentice, 60 }, { Rank::kAdept, 80 }, { Rank::kExpert, 100 } };
		c.Add(shield);
		auto paralyze = Make("mw.paralyze", 40, School::kAlteration, kAll, false);
		paralyze.hostile = true;
		c.Add(paralyze);
		c.Add(Make("mw.invisibility", 20, School::kIllusion, kAll, false));
		c.Add(Make("mw.soultrap", 2, School::kConjuration, kTouchTarget, false));
		c.Add(Make("mw.bound_longsword", 2, School::kConjuration, kSelfOnly, false, true, false));
		c.Add(Make("mw.summon_dremora", 28, School::kConjuration, kSelfOnly, false, true, false));
		c.Add(Make("mw.summon_flame_atronach", 23, School::kConjuration, kSelfOnly, false, true, false));
		auto open = Make("mw.open", 6, School::kAlteration, kTouchTarget, true, false, true);
		open.unit = Unit::kLevel;
		c.Add(open);
		auto blind = Make("mw.blind", 1, School::kIllusion, kAll);
		blind.unit = Unit::kPercent;
		c.Add(blind);
		auto fortAttr = Make("mw.fortify_attribute", 1, School::kRestoration, kAll);
		fortAttr.target = TargetKind::kAttribute;
		c.Add(fortAttr);
		auto fortSkill = Make("mw.fortify_skill", 1, School::kRestoration, kAll);
		fortSkill.target = TargetKind::kSkill;
		c.Add(fortSkill);
		auto burden = Make("mw.burden", 1, School::kAlteration, kAll);
		burden.hostile = true;
		c.Add(burden);
		return c;
	}

	inline SpellEffect E(std::string a_id, Range a_range, std::uint16_t a_min, std::uint16_t a_max, std::uint16_t a_dur = 1, std::uint16_t a_area = 0, std::int16_t a_sub = kNoSub)
	{
		SpellEffect e;
		e.effectId = std::move(a_id);
		e.range = a_range;
		e.minMag = a_min;
		e.maxMag = a_max;
		e.duration = a_dur;
		e.area = a_area;
		e.sub = a_sub;
		return e;
	}

	inline Settings Classic()
	{
		Settings s;
		s.costModel = CostModel::kClassic;
		return s;
	}

	inline StringTable EnglishStrings()
	{
		StringTable t;
		t.LoadUtf8(
			"$LA_Effect_FireDamage\tFire Damage\n$LA_Effect_FrostDamage\tFrost Damage\n$LA_Effect_FortifyHealth\tFortify Health\n"
			"$LA_Effect_Levitate\tLevitate\n$LA_Effect_Mark\tMark\n$LA_Effect_Blind\tBlind\n$LA_Effect_Open\tOpen\n"
			"$LA_Effect_FortifyAttribute\tFortify Attribute\n$LA_EffectFmt_FortifyAttribute\tFortify {0}\n"
			"$LA_Effect_FortifySkill\tFortify Skill\n$LA_Attr_Strength\tStrength\n$LA_Skill_OneHanded\tOne-Handed\n"
			"$LA_Fmt_To\tto\n$LA_Fmt_For\tfor\n$LA_Fmt_In\tin\n$LA_Fmt_On\ton\n$LA_Unit_pt\tpt\n$LA_Unit_pts\tpts\n"
			"$LA_Unit_percent\t%\n$LA_Unit_ft\tft\n$LA_Unit_Level\tlevel\n$LA_Unit_Levels\tlevels\n$LA_Unit_sec\tsec\n$LA_Unit_secs\tsecs\n"
			"$LA_Range_Self\tSelf\n$LA_Range_Touch\tTouch\n$LA_Range_Target\tTarget\n");
		return t;
	}

	inline std::filesystem::path SourceDir() { return LOSTART_SOURCE_DIR; }
}
