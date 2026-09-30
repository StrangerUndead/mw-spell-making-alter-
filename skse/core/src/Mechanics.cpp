#include "lostart/Mechanics.h"

#include "lostart/CostEngine.h"
#include "lostart/Util.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace LA::Mech
{
	Rng DefaultRng()
	{
		return [engine = std::make_shared<std::mt19937>(std::random_device{}())](int a_lo, int a_hi) {
			std::uniform_int_distribution<int> dist(std::min(a_lo, a_hi), std::max(a_lo, a_hi));
			return dist(*engine);
		};
	}

	double LandedMagnitude(const EffectDef& a_def, const SpellEffect& a_effect, bool a_rollsEnabled, const Rng& a_rng)
	{
		if (!a_def.hasMagnitude) {
			return 0.0;
		}
		const double lo = a_effect.minMag;
		const double hi = std::max(a_effect.minMag, a_effect.maxMag);
		const double average = (lo + hi) / 2.0;
		if (!a_rollsEnabled || lo == hi) {
			return average;
		}
		if (a_def.ticking && a_effect.duration >= 2) {
			return average;
		}
		return a_rng ? static_cast<double>(a_rng(static_cast<int>(lo), static_cast<int>(hi))) : average;
	}

	double MagnitudeScale(double a_landed, std::uint16_t a_itemMagnitude)
	{
		return a_itemMagnitude == 0 ? 1.0 : a_landed / static_cast<double>(a_itemMagnitude);
	}

	double BurdenSpeedPenalty(double a_magnitude) { return std::clamp(a_magnitude / 2.0, 0.0, 75.0); }

	double SlowfallFactor(double a_magnitude) { return std::clamp(1.0 - a_magnitude / 200.0, 0.0, 1.0); }

	double JumpBonusPercent(double a_magnitude, double a_perPoint) { return std::max(0.0, a_magnitude * a_perPoint); }

	double JumpFallReduction(double a_magnitude) { return std::max(0.0, a_magnitude) * kUnitsPerFoot; }

	double LevitateSpeed(double a_magnitude)
	{
		// Morrowind: fLevitateSpeed-style linear scale; 10 pts ~ walking pace, capped for sanity.
		return std::clamp(100.0 + 8.0 * a_magnitude, 100.0, 900.0);
	}

	std::optional<int> LockTarget(int a_currentLevel, double a_magnitude)
	{
		if (a_currentLevel >= kRequiresKey) {
			return std::nullopt;
		}
		int tier = 0;
		for (int t : kLockTiers) {
			if (a_magnitude + 1e-9 >= t) {
				tier = t;
			}
		}
		if (tier == 0 || tier <= a_currentLevel) {
			return std::nullopt;
		}
		return tier;
	}

	bool CanOpen(int a_lockLevel, double a_magnitude)
	{
		return a_lockLevel < kRequiresKey && a_lockLevel <= a_magnitude + 1e-9;
	}

	double ShieldRetaliation(double a_magnitude, double a_attackerSkill, double a_bonus, double a_fatigueRatio,
		double a_resistancePercent, int a_roll0to99)
	{
		const double save = (a_attackerSkill + a_bonus) * 1.25 * std::clamp(a_fatigueRatio, 0.0, 1.0);
		double       x = std::max(0.0, save - a_roll0to99);
		x = std::min(100.0, x + a_resistancePercent);
		return std::max(0.0, 0.1 * a_magnitude * (1.0 - 0.01 * x));
	}

	double MissChance(const MissInputs& a_in)
	{
		const double miss = a_in.attackerBlind + std::min(a_in.defenderSanctuary, 75.0) + 0.2 * a_in.defenderChameleon +
		                    a_in.defenderEvasion - a_in.attackerFortifyAttack;
		return std::clamp(miss, 0.0, 100.0);
	}

	int    CharmSteps(double a_magnitude) { return static_cast<int>(std::floor(std::max(0.0, a_magnitude) / 25.0)); }
	double CharmPriceBonus(double a_magnitude) { return std::max(0.0, a_magnitude) / 2.0; }

	int StrengthTier(double a_magnitude, double a_low, double a_high)
	{
		return a_magnitude < a_low ? 0 : (a_magnitude < a_high ? 1 : 2);
	}

	bool DispelRoll(double a_magnitude, int a_roll1to100) { return a_roll1to100 <= a_magnitude; }

	bool ReflectRoll(double a_magnitude, bool a_reflectable, int a_roll1to100)
	{
		return a_reflectable && a_roll1to100 <= a_magnitude;
	}

	std::optional<std::size_t> Nearest(const std::vector<Destination>& a_candidates, std::uint32_t a_worldspace,
		const std::array<float, 3>& a_from)
	{
		std::optional<std::size_t> best;
		double                     bestDist = 0;
		for (std::size_t i = 0; i < a_candidates.size(); ++i) {
			const auto& c = a_candidates[i];
			if (c.worldspace != a_worldspace) {
				continue;
			}
			const double dx = c.pos[0] - a_from[0];
			const double dy = c.pos[1] - a_from[1];
			const double dz = c.pos[2] - a_from[2];
			const double d = dx * dx + dy * dy + dz * dz;
			if (!best || d < bestDist) {
				best = i;
				bestDist = d;
			}
		}
		return best;
	}

	double ConditionAfter(double a_condition, double a_magnitude, double a_seconds)
	{
		return std::clamp(a_condition - a_magnitude * a_seconds, 0.0, 100.0);
	}

	double ConditionScale(double a_condition) { return std::clamp(a_condition, 0.0, 100.0) / 100.0; }

	bool ChanceRoll(double a_percent, int a_roll1to100) { return a_roll1to100 <= a_percent; }

	std::vector<StatDelta> AttributeStats(const AttributeDef& a_attribute, double a_magnitude, AttributeProfile a_profile)
	{
		std::vector<StatDelta> out;
		if (a_profile == AttributeProfile::kOff) {
			return out;
		}
		const double scale = a_profile == AttributeProfile::kLight ? 0.5 : 1.0;
		for (const auto& stat : a_attribute.stats) {
			out.push_back({ stat.stat, stat.perPoint * a_magnitude * scale });
		}
		return out;
	}

	RefusalRule ParseRefusalRule(std::string_view a_text)
	{
		if (a_text.starts_with("college")) {
			return RefusalRule::kCollege;
		}
		if (a_text.starts_with("wanted")) {
			return RefusalRule::kWanted;
		}
		if (a_text.starts_with("quest")) {
			return RefusalRule::kQuest;
		}
		return RefusalRule::kNone;
	}

	int RefusalReason(RefusalRule a_rule, const RefusalFacts& a_facts, const Settings& a_settings)
	{
		if (!a_facts.servicesEnabled || !a_settings.spellmakers) {
			return 6;
		}
		switch (a_rule) {
		case RefusalRule::kCollege:
			if (a_settings.collegeMembership && !a_facts.collegeMember) {
				return 1;
			}
			break;
		case RefusalRule::kWanted:
			if (a_settings.refuseWanted && a_facts.wantedInHold) {
				return 2;
			}
			break;
		case RefusalRule::kQuest:
			if (!a_facts.questDone) {
				return 3;
			}
			break;
		case RefusalRule::kNone:
			break;
		}
		if (a_facts.vampireStage4) {
			return 4;
		}
		if (a_facts.relationshipRank < 0) {
			return 5;
		}
		return 0;
	}

	double MorrowindDifficulty(const Catalog& a_catalog, const std::vector<SpellEffect>& a_effects, const Settings& a_settings)
	{
		Settings classic = a_settings;
		classic.costModel = CostModel::kClassic;
		classic.globalCostMult = 1.0;
		return static_cast<double>(Cost::Compute(a_catalog, a_effects, classic).cost);
	}
}
