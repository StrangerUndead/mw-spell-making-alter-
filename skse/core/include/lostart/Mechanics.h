#pragma once

#include "lostart/Catalog.h"
#include "lostart/Settings.h"
#include "lostart/Types.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <vector>

// Pure game-rule helpers used by the plugin's cast router and effect systems. Every number here
// is from OUTLINE.md (catalog tables, "Casting behavior", appendix constants).
namespace LA::Mech
{
	inline constexpr double kUnitsPerFootArea = 22.0;  // Morrowind: a foot rounds up to 22 units
	inline constexpr double kUnitsPerFoot = 21.33;

	// Area radius in game units for A feet.
	inline double AreaRadius(std::uint16_t a_feet) { return a_feet * kUnitsPerFootArea; }

	using Rng = std::function<int(int, int)>;  // inclusive uniform integer
	Rng DefaultRng();

	// Magnitude applied when an effect lands (OUTLINE "Magnitude rolls"): one-time effects roll
	// once per target; effects ticking for 2 s or more use the average; rolls off = average.
	double LandedMagnitude(const EffectDef& a_def, const SpellEffect& a_effect, bool a_rollsEnabled, const Rng& a_rng);
	// Factor applied to the engine's magnitude (the effect item carries the maximum).
	double MagnitudeScale(double a_landed, std::uint16_t a_itemMagnitude);

	// --- Alteration ------------------------------------------------------------------------
	// Burden on NPCs: -M/2 % speed, capped at 75 %.
	double BurdenSpeedPenalty(double a_magnitude);
	// Slowfall: fall speed x (1 - M/200); any active Slowfall cancels fall damage.
	double SlowfallFactor(double a_magnitude);
	// Jump: +jumpPerPoint % jump height per point; fall height counts M ft shorter.
	double JumpBonusPercent(double a_magnitude, double a_perPoint);
	double JumpFallReduction(double a_magnitude);  // game units
	// Levitate: air speed (units/s) scaled by M.
	double LevitateSpeed(double a_magnitude);

	inline constexpr std::array<int, 5> kLockTiers{ 1, 25, 50, 75, 100 };
	inline constexpr int                kRequiresKey = 255;
	// Lock raises to the Skyrim tier at or below M; never touches Requires Key locks; never lowers.
	std::optional<int> LockTarget(int a_currentLevel, double a_magnitude);
	// Open unlocks when lock level <= M (1-100 in both games); Requires Key never opens.
	bool CanOpen(int a_lockLevel, double a_magnitude);

	// Elemental shields (Morrowind's applyElementalShields): damage = 0.1 * M * (1 - x/100),
	// x = min(100, max(0, save - roll) + resistance); save = (skill + bonus) * 1.25 * fatigueRatio.
	double ShieldRetaliation(double a_magnitude, double a_attackerSkill, double a_bonus, double a_fatigueRatio,
		double a_resistancePercent, int a_roll0to99);

	// --- Illusion / miss system --------------------------------------------------------------
	struct MissInputs
	{
		double attackerBlind{ 0 };        // Blind on the attacker: M % miss
		double attackerFortifyAttack{ 0 };// points off the miss chance
		double defenderSanctuary{ 0 };    // min(M, 75) %
		double defenderChameleon{ 0 };    // 0.2 * M % evasion
		double defenderEvasion{ 0 };      // attribute translation (Agility/Luck) %
	};
	double MissChance(const MissInputs& a_in);

	// Charm: one relationship step per 25 M; prices improve by M/2 %.
	int    CharmSteps(double a_magnitude);
	double CharmPriceBonus(double a_magnitude);

	// Light / Night Eye strength tier (0-2) by M; thresholds are tunable data.
	int StrengthTier(double a_magnitude, double a_low = 20, double a_high = 50);

	// --- Mysticism ---------------------------------------------------------------------------
	// Detect and Telekinesis radii (feet -> units).
	inline double FeetToUnits(double a_feet) { return a_feet * kUnitsPerFoot; }
	// Dispel: each active spell has an M % chance to end.
	bool DispelRoll(double a_magnitude, int a_roll1to100);
	// Reflect: M % chance, only for reflectable effects.
	bool ReflectRoll(double a_magnitude, bool a_reflectable, int a_roll1to100);
	// Divine Intervention: nearest candidate by squared distance (same worldspace filtering is the caller's).
	struct Destination
	{
		std::string          id;
		std::uint32_t        worldspace{ 0 };
		std::array<float, 3> pos{};
	};
	std::optional<std::size_t> Nearest(const std::vector<Destination>& a_candidates, std::uint32_t a_worldspace,
		const std::array<float, 3>& a_from);

	// --- Destruction -------------------------------------------------------------------------
	// Disintegrate: condition loses M points per second from 100; rating scales with condition.
	double ConditionAfter(double a_condition, double a_magnitude, double a_seconds);
	double ConditionScale(double a_condition);  // 0..1

	// --- Restoration ------------------------------------------------------------------------
	bool ChanceRoll(double a_percent, int a_roll1to100);  // Resist Paralysis / Corprus, Sound fizzles

	// --- Attributes --------------------------------------------------------------------------
	struct StatDelta
	{
		std::string stat;
		double      value{ 0 };
	};
	// Stats moved by M points of an attribute effect under a profile.
	std::vector<StatDelta> AttributeStats(const AttributeDef& a_attribute, double a_magnitude, AttributeProfile a_profile);

	// --- Services ----------------------------------------------------------------------------
	enum class RefusalRule : std::uint8_t
	{
		kNone,
		kCollege,
		kWanted,
		kQuest
	};

	struct RefusalFacts
	{
		bool servicesEnabled{ true };
		bool collegeMember{ false };
		bool wantedInHold{ false };
		bool questDone{ true };
		bool vampireStage4{ false };
		int  relationshipRank{ 0 };  // Skyrim: -4 archnemesis .. 4 lover; 0 acquaintance
	};

	// CONTRACTS.md section 8 reasons: 0 serves, 1 not a member, 2 wanted, 3 quest, 4 vampire,
	// 5 relationship, 6 disabled.
	int RefusalReason(RefusalRule a_rule, const RefusalFacts& a_facts, const Settings& a_settings);
	RefusalRule ParseRefusalRule(std::string_view a_text);

	// --- Casting failure module ----------------------------------------------------------------
	// Morrowind difficulty (Classic cost, whatever the magicka model) of a spell.
	double MorrowindDifficulty(const Catalog& a_catalog, const std::vector<SpellEffect>& a_effects, const Settings& a_settings);
}
