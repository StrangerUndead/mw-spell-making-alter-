#pragma once

#include "lostart/Types.h"
#include "lostart/Util.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace LA
{
	struct FitFrom
	{
		FormRef     spell;
		std::string label;
		double      magnitude{ 0 };
		double      duration{ 0 };
		double      area{ 0 };
		double      cost{ 0 };
	};

	struct EffectDef
	{
		std::string id;
		std::string nameKey;
		std::string pascal;  // derived from id when not given
		EffectSet   set{ EffectSet::kMorrowind };
		Tier        tier{ Tier::kNative };

		// Morrowind
		int      mwIndex{ -1 };
		MwSchool mwSchool{ MwSchool::kAlteration };
		double   mwBaseCost{ 1.0 };

		// Skyrim
		School                 school{ School::kAlteration };
		double                 skBaseCost{ 1.0 };
		std::optional<FitFrom> fitFrom;
		bool                   vanillaTargetPriced{ false };
		std::string            archetype;
		FormRef                vanillaEffect;

		RangeMask  ranges{ 0 };
		bool       hasMagnitude{ true };
		Unit       unit{ Unit::kPoints };
		bool       ticking{ false };
		bool       hasDuration{ true };
		bool       hasArea{ true };
		TargetKind target{ TargetKind::kNone };

		std::vector<std::string>    keywords;
		std::vector<std::string>    riders;
		std::map<Rank, double>      rankLadder;  // magnitude thresholds
		bool                        reflectable{ true };
		bool                        hostile{ false };
		std::string                 itemCardKey;
		std::vector<FormRef>        sources;

		// Runtime binding for pass-through effects (full FormID of the modded MGEF).
		std::uint32_t passThroughForm{ 0 };

		bool Allows(Range a_range) const { return (ranges & Bit(a_range)) != 0; }
		bool ShowsArea(Range a_range) const { return hasArea && a_range != Range::kSelf; }
		std::vector<Range> AllowedRanges() const;
	};

	struct AttributeStat
	{
		std::string stat;      // e.g. "CarryWeight", "MeleeDamagePercent"
		double      perPoint{ 0.0 };
	};

	struct AttributeDef
	{
		int                        index{ 0 };
		std::string                name;     // "Strength"
		std::string                nameKey;  // "$LA_Attr_Strength"
		std::vector<AttributeStat> stats;    // Default profile, first entry is the visible stat
	};

	struct SkillTarget
	{
		int    skyrimSkill{ 0 };  // 0-17, or -1 for a special target
		std::string special;     // "Jump", "Speed", "UnarmedDamage", "Unarmored"...
		double weight{ 1.0 };
	};

	struct MwSkillDef
	{
		int                      index{ 0 };  // 0-26
		std::string              name;
		std::string              nameKey;
		std::vector<SkillTarget> targets;
	};

	class Catalog
	{
	public:
		// Loads every *.json array in a_effectsDir. Returns false and fills a_errors on problems.
		bool LoadEffects(const std::filesystem::path& a_effectsDir, std::vector<std::string>& a_errors);
		bool LoadEffectsFromString(std::string_view a_json, std::vector<std::string>& a_errors, std::string_view a_source = "<string>");
		bool LoadAttributes(const std::filesystem::path& a_file, std::vector<std::string>& a_errors);
		bool LoadSkills(const std::filesystem::path& a_file, std::vector<std::string>& a_errors);

		void Add(EffectDef a_def);
		const EffectDef* Find(std::string_view a_id) const;
		const std::vector<EffectDef>& All() const { return _effects; }
		std::size_t Size() const { return _effects.size(); }

		const std::vector<AttributeDef>& Attributes() const { return _attributes; }
		const std::vector<MwSkillDef>&   MwSkills() const { return _mwSkills; }

		static std::string PascalFromId(std::string_view a_id);
		static std::string_view SkyrimSkillName(int a_index);
		static std::string_view AttributeName(int a_index);

	private:
		std::vector<EffectDef>                       _effects;
		std::unordered_map<std::string, std::size_t> _index;
		std::vector<AttributeDef>                    _attributes;
		std::vector<MwSkillDef>                      _mwSkills;
	};
}
