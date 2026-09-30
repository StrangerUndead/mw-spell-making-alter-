#include "lostart/CostEngine.h"

#include "lostart/Util.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace LA::Cost
{
	namespace
	{
		struct Terms
		{
			double minMag;
			double maxMag;
			double duration;
			double area;
		};

		// Magnitude and duration count as 1 when the effect has none; area only exists off Self.
		Terms Normalize(const EffectDef& a_def, const SpellEffect& a_effect)
		{
			Terms t{};
			t.minMag = a_def.hasMagnitude ? a_effect.minMag : 1.0;
			t.maxMag = a_def.hasMagnitude ? std::max(a_effect.maxMag, a_effect.minMag) : 1.0;
			t.duration = a_def.hasDuration ? std::max<double>(a_effect.duration, 1.0) : 1.0;
			t.area = a_def.ShowsArea(a_effect.range) ? a_effect.area : 0.0;
			return t;
		}

		double MeanMagnitude(const Terms& a_t) { return std::max(1.0, (a_t.minMag + a_t.maxMag) / 2.0); }
	}

	double ClassicEffect(const EffectDef& a_def, const SpellEffect& a_effect)
	{
		const auto t = Normalize(a_def, a_effect);
		return a_def.mwBaseCost * ((t.minMag + t.maxMag) * (t.duration + 1.0) + std::max(1.0, t.area)) / 40.0;
	}

	double BalancedT(const EffectDef& a_def, const SpellEffect& a_effect)
	{
		const auto t = Normalize(a_def, a_effect);
		const double m = MeanMagnitude(t);
		if (a_def.ticking) {
			return m * std::max(1.0, t.duration);
		}
		return m * std::max(t.duration, 10.0) / 10.0;
	}

	double BalancedEffect(const EffectDef& a_def, const SpellEffect& a_effect, std::optional<double> a_baseCost)
	{
		const auto   t = Normalize(a_def, a_effect);
		const double base = a_baseCost.value_or(a_def.skBaseCost);
		double       c = base * std::pow(BalancedT(a_def, a_effect), 1.1) * (1.0 + t.area / 16.0);
		if (a_effect.range == Range::kTarget && !a_def.vanillaTargetPriced) {
			c *= 1.5;
		}
		return c;
	}

	double AutocalcEffect(const EffectDef& a_def, const SpellEffect& a_effect)
	{
		const auto t = Normalize(a_def, a_effect);
		double     c = a_def.skBaseCost * std::pow(std::max(1.0, t.maxMag), 1.1);
		if (!a_def.ticking && a_def.hasDuration) {
			c *= std::pow(std::max(t.duration, 10.0) / 10.0, 1.1);
		}
		return c * (1.0 + t.area * 0.15);
	}

	double FitBaseCost(const EffectDef& a_def, const FitFrom& a_fit)
	{
		SpellEffect probe;
		probe.effectId = a_def.id;
		probe.range = Range::kTarget;
		probe.minMag = static_cast<std::uint16_t>(std::max(1.0, a_fit.magnitude));
		probe.maxMag = probe.minMag;
		probe.duration = static_cast<std::uint16_t>(std::max(1.0, a_fit.duration));
		probe.area = static_cast<std::uint16_t>(a_fit.area);
		const double t = BalancedT(a_def, probe);
		const double area = a_def.hasArea ? 1.0 + a_fit.area / 16.0 : 1.0;
		const double denom = std::pow(t, 1.1) * area;
		return denom > 0.0 ? a_fit.cost / denom : a_def.skBaseCost;
	}

	std::uint32_t Price(double a_y, double a_k)
	{
		const auto price = FloorTol(a_k * a_y);
		return static_cast<std::uint32_t>(std::max<std::int64_t>(1, price));
	}

	CostResult Compute(const Catalog& a_catalog, const std::vector<SpellEffect>& a_effects, const Settings& a_settings,
		CostModel a_model, const BaseCostOverride& a_baseCost)
	{
		CostResult result;
		double     y = 0.0;
		for (const auto& effect : a_effects) {
			const auto* def = a_catalog.Find(effect.effectId);
			if (!def) {
				result.parts.push_back({});
				continue;
			}
			CostPart part;
			switch (a_model) {
			case CostModel::kClassic:
				{
					part.effectCost = ClassicEffect(*def, effect);
					const bool target = effect.range == Range::kTarget;
					part.targetMult = target ? 1.5 : 1.0;
					if (a_settings.targetRunningTotal) {
						y = (y + std::max(1.0, part.effectCost)) * part.targetMult;
					} else {
						y += std::max(1.0, part.effectCost) * part.targetMult;
					}
					break;
				}
			case CostModel::kSkyrimBalanced:
				{
					std::optional<double> base;
					if (a_baseCost) {
						base = a_baseCost(*def);
					}
					const double raw = BalancedEffect(*def, effect, base);
					part.targetMult = (effect.range == Range::kTarget && !def->vanillaTargetPriced) ? 1.5 : 1.0;
					part.effectCost = raw / part.targetMult;
					y += raw;
					break;
				}
			case CostModel::kEngineAutocalc:
				part.effectCost = AutocalcEffect(*def, effect);
				y += part.effectCost;
				break;
			}
			part.runningTotal = y;
			result.parts.push_back(part);
		}
		result.y = y;
		result.cost = static_cast<std::uint32_t>(std::max<std::int64_t>(0, FloorTol(y * a_settings.globalCostMult)));
		result.price = a_effects.empty() ? 0 : Price(y, a_settings.PriceMultiplier());
		return result;
	}

	int HardestEffect(const Catalog& a_catalog, const std::vector<SpellEffect>& a_effects,
		const std::function<double(const EffectDef&)>& a_skillOf)
	{
		int    best = -1;
		double bestValue = std::numeric_limits<double>::max();
		for (std::size_t i = 0; i < a_effects.size(); ++i) {
			const auto* def = a_catalog.Find(a_effects[i].effectId);
			if (!def) {
				continue;
			}
			const auto t = Normalize(*def, a_effects[i]);
			// No +1 on duration and no 1-ft minimum area here (OpenMW getSpellSchool).
			double x = def->mwBaseCost * ((t.minMag + t.maxMag) * std::max(1.0, t.duration) + t.area) / 40.0;
			if (a_effects[i].range == Range::kTarget) {
				x *= 1.5;
			}
			const double value = 2.0 * a_skillOf(*def) - x;
			if (value < bestValue) {
				bestValue = value;
				best = static_cast<int>(i);
			}
		}
		return best;
	}

	int MorrowindChance(double a_skill, double a_willpower, double a_luck, double a_cost, double a_sound, double a_fatigueRatio)
	{
		const double base = 2.0 * a_skill + a_willpower / 5.0 + a_luck / 10.0 - a_cost - a_sound;
		const double chance = base * (0.75 + 0.5 * std::clamp(a_fatigueRatio, 0.0, 1.0));
		return static_cast<int>(std::clamp<std::int64_t>(FloorTol(chance), 0, 100));
	}

	int SkyrimChance(double a_skill, double a_bonus, double a_mwDifficulty, double a_sound, double a_staminaRatio)
	{
		const double base = 2.0 * a_skill + a_bonus - a_mwDifficulty - a_sound;
		const double chance = base * (0.75 + 0.5 * std::clamp(a_staminaRatio, 0.0, 1.0));
		return static_cast<int>(std::clamp<std::int64_t>(FloorTol(chance), 0, 100));
	}

	Rank EffectRank(const EffectDef& a_def, const SpellEffect& a_effect, double a_balancedCost)
	{
		if (!a_def.rankLadder.empty() && a_def.hasMagnitude) {
			Rank rank = Rank::kNovice;
			const double magnitude = std::max(a_effect.maxMag, a_effect.minMag);
			for (const auto& [tier, threshold] : a_def.rankLadder) {
				if (magnitude + 1e-9 >= threshold && tier > rank) {
					rank = tier;
				}
			}
			return rank;
		}
		for (std::size_t i = 0; i < kRankCostThresholds.size(); ++i) {
			if (a_balancedCost < kRankCostThresholds[i]) {
				return static_cast<Rank>(i);
			}
		}
		return Rank::kMaster;
	}

	Rank SpellRank(const Catalog& a_catalog, const std::vector<SpellEffect>& a_effects)
	{
		Rank rank = Rank::kNovice;
		for (const auto& effect : a_effects) {
			if (const auto* def = a_catalog.Find(effect.effectId)) {
				rank = std::max(rank, EffectRank(*def, effect, BalancedEffect(*def, effect)));
			}
		}
		return rank;
	}

	School CostliestSchool(const Catalog& a_catalog, const std::vector<SpellEffect>& a_effects, const Settings& a_settings)
	{
		School school = School::kAlteration;
		double best = -1.0;
		for (const auto& effect : a_effects) {
			const auto* def = a_catalog.Find(effect.effectId);
			if (!def) {
				continue;
			}
			double cost = 0.0;
			switch (a_settings.costModel) {
			case CostModel::kClassic:
				cost = ClassicEffect(*def, effect) * (effect.range == Range::kTarget ? 1.5 : 1.0);
				break;
			case CostModel::kEngineAutocalc:
				cost = AutocalcEffect(*def, effect);
				break;
			default:
				cost = BalancedEffect(*def, effect);
				break;
			}
			if (cost > best) {
				best = cost;
				school = def->school;
			}
		}
		return school;
	}
}
