#pragma once

#include "lostart/Catalog.h"
#include "lostart/Settings.h"
#include "lostart/Types.h"

#include <array>
#include <functional>
#include <vector>

namespace LA
{
	// One line of "Show cost math".
	struct CostPart
	{
		double effectCost{ 0 };    // C_i (Classic) or c_i (balanced), before multipliers
		double targetMult{ 1.0 };  // 1.5 when a Target multiplier applied, else 1
		double runningTotal{ 0 };  // y_i
	};

	struct CostResult
	{
		std::uint32_t         cost{ 0 };      // magicka, floored, after the global multiplier
		double                y{ 0 };         // unrounded total before the global multiplier
		std::uint32_t         price{ 0 };     // gold, before provider fee rules and haggling
		std::vector<CostPart> parts;
	};

	// Pure functions implementing OUTLINE.md "Cost, price & casting chance".
	namespace Cost
	{
		// Morrowind: C_i = B[(Mmin+Mmax)(D+1) + max(1,A)] / 40 ; magnitude and duration count
		// as 1 when the effect has none.
		double ClassicEffect(const EffectDef& a_def, const SpellEffect& a_effect);

		// Skyrim-balanced: c_i = B_sk * T^1.1 * (1 + A/16) [* 1.5 on Target when vanilla has no
		// priced Target version]. a_baseCost overrides the catalog's B_sk (live refit).
		double BalancedEffect(const EffectDef& a_def, const SpellEffect& a_effect, std::optional<double> a_baseCost = std::nullopt);
		double BalancedT(const EffectDef& a_def, const SpellEffect& a_effect);

		// Rough stand-in for the engine's own autocalc (the plugin asks the engine for the real
		// number): B_sk * max(1,Mmax)^1.1 * (max(D,10)/10)^1.1 for held effects.
		double AutocalcEffect(const EffectDef& a_def, const SpellEffect& a_effect);

		// Fits B_sk from a vanilla spell: cost / T(fit)^1.1 / (1 + A/16).
		double FitBaseCost(const EffectDef& a_def, const FitFrom& a_fit);

		using BaseCostOverride = std::function<std::optional<double>(const EffectDef&)>;

		CostResult Compute(const Catalog& a_catalog, const std::vector<SpellEffect>& a_effects, const Settings& a_settings,
			CostModel a_model, const BaseCostOverride& a_baseCost = {});
		inline CostResult Compute(const Catalog& a_catalog, const std::vector<SpellEffect>& a_effects, const Settings& a_settings)
		{
			return Compute(a_catalog, a_effects, a_settings, a_settings.costModel);
		}

		// Price = max(1, floor(k * y)).
		std::uint32_t Price(double a_y, double a_k);

		// Morrowind hardest-effect school: lowest 2*skill - x, x = B[(Mmin+Mmax)*max(1,D) + A]/40,
		// *1.5 on Target. Returns the index into a_def list (or -1 if empty).
		int HardestEffect(const Catalog& a_catalog, const std::vector<SpellEffect>& a_effects,
			const std::function<double(const EffectDef&)>& a_skillOf);

		// Morrowind: (2S + W/5 + L/10 - Cost - Sound) * (0.75 + 0.5 F/Fmax), truncated, clamped 0..100.
		int MorrowindChance(double a_skill, double a_willpower, double a_luck, double a_cost, double a_sound, double a_fatigueRatio);

		// Module formula: (2S + bonus - D_mw - Sound) * (0.75 + 0.5 St/StMax).
		int SkyrimChance(double a_skill, double a_bonus, double a_mwDifficulty, double a_sound, double a_staminaRatio);

		// Rank of one effect: its magnitude ladder, else cost thresholds on its balanced cost.
		Rank EffectRank(const EffectDef& a_def, const SpellEffect& a_effect, double a_balancedCost);
		Rank SpellRank(const Catalog& a_catalog, const std::vector<SpellEffect>& a_effects);

		// The Skyrim school the magic menu files the spell under: its costliest effect.
		School CostliestSchool(const Catalog& a_catalog, const std::vector<SpellEffect>& a_effects, const Settings& a_settings);

		// Default rank cost thresholds (data/content/ranks.json may override).
		inline std::array<double, 4> kRankCostThresholds{ 60.0, 130.0, 250.0, 500.0 };
	}
}
