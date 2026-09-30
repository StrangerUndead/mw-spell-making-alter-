#include "lostart/Compiler.h"

#include "lostart/CostEngine.h"

#include <algorithm>
#include <cmath>

namespace LA::Compiler
{
	std::string VariantEditorId(const EffectDef& a_def, std::string_view a_target, Range a_range)
	{
		std::string id = "LA_" + a_def.pascal;
		if (!a_target.empty()) {
			id += "_";
			id += a_target;
		}
		id += "_";
		id += ToString(a_range);
		return id;
	}

	Range PrimaryRange(const std::vector<SpellEffect>& a_effects)
	{
		Range best = Range::kSelf;
		for (const auto& effect : a_effects) {
			if (Reach(effect.range) > Reach(best)) {
				best = effect.range;
			}
		}
		return best;
	}

	namespace
	{
		struct Target
		{
			std::string name;
			double      weight{ 1.0 };
		};

		// Resolves the sub-index into the variant target name(s).
		std::vector<Target> Targets(const Catalog& a_catalog, const EffectDef& a_def, std::int16_t a_sub)
		{
			if (a_def.target == TargetKind::kAttribute) {
				return { { std::string(Catalog::AttributeName(a_sub)), 1.0 } };
			}
			if (a_def.target == TargetKind::kSkill) {
				if (a_sub >= kMwSkillBase) {
					std::vector<Target> out;
					for (const auto& skill : a_catalog.MwSkills()) {
						if (skill.index != a_sub - kMwSkillBase) {
							continue;
						}
						for (const auto& target : skill.targets) {
							if (target.skyrimSkill >= 0) {
								out.push_back({ std::string(Catalog::SkyrimSkillName(target.skyrimSkill)), target.weight });
							} else {
								// Special targets (Acrobatics -> jump, Unarmored...) have their own variants.
								out.push_back({ skill.name, target.weight });
							}
						}
					}
					// De-duplicate special targets that share the Morrowind skill's own variant.
					std::vector<Target> unique;
					for (auto& t : out) {
						auto it = std::find_if(unique.begin(), unique.end(), [&](const Target& u) { return u.name == t.name; });
						if (it == unique.end()) {
							unique.push_back(t);
						}
					}
					return unique;
				}
				return { { std::string(Catalog::SkyrimSkillName(a_sub)), 1.0 } };
			}
			return { { std::string(), 1.0 } };
		}

		std::uint16_t Scale(std::uint16_t a_value, double a_weight)
		{
			if (a_value == 0) {
				return 0;
			}
			return static_cast<std::uint16_t>(std::max(1.0, std::round(a_value * a_weight)));
		}

		struct Unit
		{
			std::vector<PlannedEntry> entries;  // one effect plus its riders, kept together
		};

		Unit Expand(const Catalog& a_catalog, const SpellEffect& a_effect, std::size_t a_index, const Settings& a_settings,
			const RiderLookup& a_riders)
		{
			Unit        unit;
			const auto* def = a_catalog.Find(a_effect.effectId);
			if (!def) {
				return unit;
			}
			std::uint32_t duration = 0;
			if (def->hasDuration) {
				duration = (def->ticking && a_effect.duration <= 1) ? 0u : a_effect.duration;
			}
			const std::uint16_t area = def->ShowsArea(a_effect.range) ? a_effect.area : 0;
			const std::uint16_t lo = def->hasMagnitude ? a_effect.minMag : 0;
			const std::uint16_t hi = def->hasMagnitude ? std::max(a_effect.minMag, a_effect.maxMag) : 0;

			for (const auto& target : Targets(a_catalog, *def, a_effect.sub)) {
				PlannedEntry entry;
				entry.sourceIndex = a_index;
				entry.effectId = def->id;
				entry.sub = a_effect.sub;
				entry.variantEditorId = VariantEditorId(*def, target.name, a_effect.range);
				entry.range = a_effect.range;
				entry.minMag = Scale(lo, target.weight);
				entry.maxMag = Scale(hi, target.weight);
				entry.duration = duration;
				entry.area = area;
				entry.ticking = def->ticking;
				entry.hostile = def->hostile;
				// Pass-through effects have no LA_ variant; the plugin uses the modded MGEF.
				if (def->set == EffectSet::kPassThrough) {
					entry.variantEditorId.clear();
				}
				unit.entries.push_back(std::move(entry));
			}

			for (const auto& riderId : def->riders) {
				const RiderInfo* rider = a_riders ? a_riders(riderId) : nullptr;
				if (rider && rider->elemental && !a_settings.elementalRiders) {
					continue;
				}
				PlannedEntry entry;
				entry.sourceIndex = a_index;
				entry.effectId = def->id;
				entry.sub = a_effect.sub;
				entry.riderId = riderId;
				if (rider) {
					entry.vanillaEffect = rider->effect;
				}
				entry.range = a_effect.range;
				const double scale = rider ? rider->magnitudeScale : 1.0;
				entry.minMag = Scale(lo, scale);
				entry.maxMag = Scale(hi, scale);
				entry.duration = duration;
				entry.area = area;
				entry.ticking = def->ticking;
				entry.hostile = def->hostile;
				unit.entries.push_back(std::move(entry));
			}
			return unit;
		}

		// Packs units into spells of at most kEngineEffectCeiling entries, primary first.
		bool Pack(std::vector<Unit>& a_units, Range a_range, bool a_primary, std::vector<PlannedSpell>& a_out)
		{
			PlannedSpell current;
			current.range = a_range;
			current.primary = a_primary;
			for (auto& unit : a_units) {
				if (unit.entries.size() > kEngineEffectCeiling) {
					return false;
				}
				if (current.entries.size() + unit.entries.size() > kEngineEffectCeiling) {
					a_out.push_back(std::move(current));
					current = PlannedSpell{};
					current.range = a_range;
					current.primary = false;
				}
				for (auto& entry : unit.entries) {
					current.hostile = current.hostile || entry.hostile;
					current.entries.push_back(std::move(entry));
				}
			}
			if (!current.entries.empty() || a_primary) {
				a_out.push_back(std::move(current));
			}
			return true;
		}
	}

	CompilePlan Plan(const Catalog& a_catalog, const std::vector<SpellEffect>& a_effects, const Settings& a_settings,
		const RiderLookup& a_riders)
	{
		CompilePlan plan;
		if (a_effects.empty()) {
			return plan;
		}
		const Range primary = PrimaryRange(a_effects);

		// Group by range, keeping definition order.
		std::vector<std::size_t> byRange[3];
		for (std::size_t i = 0; i < a_effects.size(); ++i) {
			byRange[static_cast<int>(a_effects[i].range)].push_back(i);
		}

		// The lead effect of a Target spell provides the projectile: list the costliest first.
		auto& lead = byRange[static_cast<int>(primary)];
		if (primary == Range::kTarget && lead.size() > 1) {
			auto cost = [&](std::size_t a_i) {
				const auto* def = a_catalog.Find(a_effects[a_i].effectId);
				return def ? Cost::BalancedEffect(*def, a_effects[a_i]) : 0.0;
			};
			const auto it = std::max_element(lead.begin(), lead.end(), [&](std::size_t a, std::size_t b) { return cost(a) < cost(b); });
			std::rotate(lead.begin(), it, it + 1);
		}

		auto units = [&](Range a_range) {
			std::vector<Unit> out;
			for (auto index : byRange[static_cast<int>(a_range)]) {
				out.push_back(Expand(a_catalog, a_effects[index], index, a_settings, a_riders));
			}
			return out;
		};

		auto primaryUnits = units(primary);
		if (!Pack(primaryUnits, primary, true, plan.spells)) {
			plan.tooComplex = true;
		}
		// Linked sub-spells: the other ranges, nearest last (Touch before Self).
		for (Range range : { Range::kTouch, Range::kSelf }) {
			if (range == primary || byRange[static_cast<int>(range)].empty()) {
				continue;
			}
			auto sub = units(range);
			if (!Pack(sub, range, false, plan.spells)) {
				plan.tooComplex = true;
			}
		}

		plan.school = Cost::CostliestSchool(a_catalog, a_effects, a_settings);
		plan.rank = Cost::SpellRank(a_catalog, a_effects);
		return plan;
	}
}
