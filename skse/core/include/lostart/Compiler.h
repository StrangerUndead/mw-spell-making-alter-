#pragma once

#include "lostart/Catalog.h"
#include "lostart/Settings.h"
#include "lostart/Types.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace LA
{
	// Skyrim's practical ceiling on effects per spell (OUTLINE "Perks and riders").
	inline constexpr std::size_t kEngineEffectCeiling = 15;

	struct RiderInfo
	{
		std::string id;
		FormRef     effect;          // vanilla MGEF appended hidden
		FormRef     perk;            // perk that reads it (informational)
		bool        elemental{ false };  // Frost stamina/slow, Shock magicka: dropped when "Skyrim elemental riders" is off
		double      magnitudeScale{ 1.0 }; // rider magnitude = parent magnitude * scale
		bool        replacesPrimary{ false };  // vanilla swaps the main effect (Elemental Potency, Mystic Binding)
		std::map<std::string, FormRef> variants;  // "aimed", "aimedArea", "touch", ... (data/content/riders.json)

		// The rider MGEF for a delivery: touch, aimed-with-area and aimed variants when present.
		const FormRef& EffectFor(Range a_range, bool a_hasArea) const;
	};

	using RiderLookup = std::function<const RiderInfo*(std::string_view)>;

	struct PlannedEntry
	{
		std::size_t   sourceIndex{ 0 };  // index into SpellDef::effects
		std::string   effectId;
		std::int16_t  sub{ kNoSub };
		std::string   variantEditorId;   // LA_* MGEF, or empty for vanilla riders
		FormRef       vanillaEffect;     // set for riders
		std::string   riderId;           // empty for the effect itself
		Range         range{ Range::kSelf };
		std::uint16_t minMag{ 0 };
		std::uint16_t maxMag{ 0 };       // magnitude written into the Skyrim effect item
		std::uint32_t duration{ 0 };     // Skyrim duration (0 = instant)
		std::uint16_t area{ 0 };         // Morrowind feet, resolved by the plugin's area resolver
		bool          ticking{ false };
		bool          hostile{ false };
	};

	struct PlannedSpell
	{
		Range                     range{ Range::kSelf };
		bool                      primary{ false };
		bool                      hostile{ false };
		std::vector<PlannedEntry> entries;
	};

	struct CompilePlan
	{
		std::vector<PlannedSpell> spells;  // [0] is the equipped spell
		School                    school{ School::kAlteration };
		Rank                      rank{ Rank::kNovice };
		bool                      tooComplex{ false };
		std::size_t               SubSlotsNeeded() const { return spells.empty() ? 0 : spells.size() - 1; }
		const PlannedSpell&       Primary() const { return spells.front(); }
	};

	namespace Compiler
	{
		// "LA_FireDamage_Target", "LA_FortifyAttribute_Strength_Self", "LA_FortifySkill_OneHanded_Touch".
		std::string VariantEditorId(const EffectDef& a_def, std::string_view a_target, Range a_range);

		// Farthest range present (Target > Touch > Self).
		Range PrimaryRange(const std::vector<SpellEffect>& a_effects);

		CompilePlan Plan(const Catalog& a_catalog, const std::vector<SpellEffect>& a_effects, const Settings& a_settings,
			const RiderLookup& a_riders = {});
	}
}
