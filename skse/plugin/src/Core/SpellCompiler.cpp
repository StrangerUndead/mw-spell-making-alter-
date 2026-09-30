#include "Core/Spellbook.h"

#include "Core/SpellCompiler.h"

// SpellCompiler: turns a CompilePlan (lostart_core) into the fixed slot records of
// LostArt_Slots.esp. OUTLINE "Components" item 4 and "Why fixed slots".
//
// Engine notes this file relies on:
//  * A spell's effect list is `MagicItem::effects` (BSTArray<Effect*>). Effects are added at
//    runtime with `new RE::Effect()` (game heap via TES_HEAP_REDEFINE_NEW) and pushed into the
//    array, exactly as powerof3's PapyrusExtenderSSE does for AddMagicEffectToSpell
//    (src/Serialization/Manager.cpp, MGEFData::add_magic_effect). Removed effects are never
//    freed there either: an ActiveEffect keeps a raw `Effect*` (ActiveEffect::effect), so freeing
//    one that a running effect still points at is a use-after-free. We go one step further and
//    recycle retired effects only after two reverts (see EffectPool below).
//  * `MagicItem::hostileCount` is the engine's cached hostility (MagicItem::IsHostile() returns
//    hostileCount > 0); it is computed when the form initialises and never refreshed, so the
//    compiler recounts it (OUTLINE "Stacking, hostility and crime").
//  * `MagicItem::avEffectSetting` caches the effect that decides the spell's skill, school and
//    hand art; it is refreshed from the engine's own GetCostliestEffectItem after every write.
namespace LA::Compiler
{
	namespace
	{
		// Recycling of Effect objects (see the note above). An effect removed from a slot during
		// session N may be referenced by live ActiveEffects until the next save load tears them
		// down; it becomes reusable only after the revert that ends session N+1.
		struct EffectPool
		{
			std::vector<RE::Effect*> retired;     // removed since the last revert
			std::vector<RE::Effect*> quarantine;  // removed before the last revert
			std::vector<RE::Effect*> reusable;    // removed two reverts ago: unreferenced

			RE::Effect* Take()
			{
				while (!reusable.empty()) {
					auto* effect = reusable.back();
					reusable.pop_back();
					if (effect && effect->conditions.head == nullptr) {
						*effect = RE::Effect{};
						return effect;
					}
				}
				return new RE::Effect();
			}
		};
		EffectPool g_pool;  // main thread only

		RE::Effect* MakeEffect(RE::EffectSetting* a_base, float a_magnitude, std::uint32_t a_duration, std::uint32_t a_area, float a_cost)
		{
			auto* effect = g_pool.Take();
			effect->effectItem.magnitude = a_magnitude;
			effect->effectItem.area = a_area;
			effect->effectItem.duration = a_duration;
			effect->baseEffect = a_base;
			effect->cost = a_cost;
			return effect;
		}

		void ReplaceEffects(RE::SpellItem* a_record, const std::vector<RE::Effect*>& a_effects)
		{
			for (auto* old : a_record->effects) {
				if (old) {
					g_pool.retired.push_back(old);
				}
			}
			a_record->effects.clear();
			for (auto* effect : a_effects) {
				a_record->effects.push_back(effect);
			}
		}

		void RefreshCaches(RE::SpellItem* a_record)
		{
			std::int32_t hostile = 0;
			for (const auto* effect : a_record->effects) {
				if (effect && effect->baseEffect && effect->baseEffect->IsHostile()) {
					++hostile;
				}
			}
			a_record->hostileCount = hostile;
			// VERIFY(in-game): GetCostliestEffectItem picks the lead effect by Effect::cost, so the
			// magic menu files the spell under plan.school and the hand art follows the lead MGEF.
			if (auto* costliest = a_record->GetCostliestEffectItem(RE::MagicSystem::Delivery::kNone, false); costliest && costliest->baseEffect) {
				a_record->avEffectSetting = costliest->baseEffect;
			} else if (!a_record->effects.empty() && a_record->effects.front()) {
				a_record->avEffectSetting = a_record->effects.front()->baseEffect;
			}
		}

		RE::EffectSetting* ResolveEntry(const PlannedEntry& a_entry, std::string& a_why)
		{
			auto& state = State::Get();
			if (!a_entry.riderId.empty()) {
				auto* rider = FormMap::Resolve<RE::EffectSetting>(a_entry.vanillaEffect);
				if (!rider) {
					a_why = fmt::format("rider {} ({}) is not loaded", a_entry.riderId, a_entry.vanillaEffect.ToString());
				}
				return rider;
			}
			const auto* def = state.catalog.Find(a_entry.effectId);
			if (!def) {
				a_why = fmt::format("effect {} is not in the catalog (effect pack missing?)", a_entry.effectId);
				return nullptr;
			}
			if (def->set == EffectSet::kPassThrough) {
				auto* mgef = def->passThroughForm ? RE::TESForm::LookupByID<RE::EffectSetting>(def->passThroughForm) : nullptr;
				if (!mgef) {
					mgef = FormMap::Resolve<RE::EffectSetting>(def->vanillaEffect);
				}
				if (!mgef) {
					a_why = fmt::format("pass-through effect {} is no longer loaded", a_entry.effectId);
				}
				return mgef;
			}
			auto* mgef = state.forms.Get<RE::EffectSetting>(a_entry.variantEditorId);
			if (!mgef) {
				a_why = fmt::format("variant {} of {} is missing from LostArt.esp", a_entry.variantEditorId, a_entry.effectId);
			}
			return mgef;
		}
	}

	void OnRevert()
	{
		// Must run before the revert blanks the slots (those removals join `retired`).
		g_pool.reusable.insert(g_pool.reusable.end(), g_pool.quarantine.begin(), g_pool.quarantine.end());
		g_pool.quarantine = std::move(g_pool.retired);
		g_pool.retired.clear();
	}

	std::size_t RetiredEffectCount()
	{
		return g_pool.retired.size() + g_pool.quarantine.size();
	}

	std::vector<float> EntryCosts(const SpellDef& a_def)
	{
		auto&      state = State::Get();
		const auto result = Cost::Compute(state.catalog, a_def.effects, state.settings, a_def.costModel, state.BaseCostFn());
		std::vector<float> costs(a_def.effects.size(), 0.0f);
		for (std::size_t i = 0; i < costs.size() && i < result.parts.size(); ++i) {
			const auto& part = result.parts[i];
			costs[i] = static_cast<float>(std::max(0.0, part.effectCost * part.targetMult * state.settings.globalCostMult));
		}
		return costs;
	}

	MagicSystemInfo Expected(Range a_range)
	{
		return { RE::MagicSystem::CastingType::kFireAndForget,
			a_range == Range::kSelf ? RE::MagicSystem::Delivery::kSelf : RE::MagicSystem::Delivery::kAimed };
	}
}

namespace LA::Spellbook
{
	bool CompileInto(RE::SpellItem* a_record, const SpellDef& a_def, const CompilePlan& a_plan, std::size_t a_index, std::string* a_error)
	{
		auto fail = [&](std::string a_text) {
			if (a_error) {
				*a_error = a_text;
			}
			logger::error("compile '{}' (slot {}): {}", a_def.name, a_def.slot, a_text);
			return false;
		};
		auto& state = State::Get();
		if (!a_record) {
			return fail("slot record not loaded");
		}
		if (a_index >= a_plan.spells.size()) {
			return fail(fmt::format("plan has no spell #{}", a_index));
		}
		if (!state.blankEffect) {
			return fail("LA_BlankEffect not loaded (LostArt_Slots.esp missing?)");
		}

		const auto& planned = a_plan.spells[a_index];
		const bool  primary = a_index == 0;
		const auto  costs = Compiler::EntryCosts(a_def);

		std::vector<RE::Effect*> effects;
		std::vector<bool>        costGiven(a_def.effects.size(), false);
		effects.reserve(planned.entries.size());
		for (const auto& entry : planned.entries) {
			std::string why;
			auto*       mgef = Compiler::ResolveEntry(entry, why);
			if (!mgef) {
				if (!entry.riderId.empty()) {
					logger::warn("'{}': {}; rider skipped", a_def.name, why);
					continue;  // riders only feed perks; the spell works without them
				}
				// OUTLINE "Load and save sequence" 4: a missing effect pack downgrades that effect
				// to an inert placeholder instead of crashing.
				logger::warn("'{}': {}; compiled as an inert placeholder", a_def.name, why);
				effects.push_back(Compiler::MakeEffect(state.blankEffect, 0.0f, 0, 0, 0.0f));
				continue;
			}
			float cost = 0.0f;
			if (entry.riderId.empty() && entry.sourceIndex < costs.size() && !costGiven[entry.sourceIndex]) {
				cost = costs[entry.sourceIndex];
				costGiven[entry.sourceIndex] = true;
			}
			// Area stays 0 in the effect item: the plugin's own area resolver applies Morrowind
			// area (entry.area, feet) on impact (OUTLINE "Area").
			effects.push_back(Compiler::MakeEffect(mgef, static_cast<float>(entry.maxMag), entry.duration, 0, cost));
		}
		if (effects.empty()) {
			// A spell record never goes out with an empty effect list.
			effects.push_back(Compiler::MakeEffect(state.blankEffect, 0.0f, 0, 0, 0.0f));
		}
		if (effects.size() > kEngineEffectCeiling) {
			return fail(fmt::format("{} effects exceed the engine ceiling of {}", effects.size(), kEngineEffectCeiling));
		}

		Compiler::ReplaceEffects(a_record, effects);

		const auto expected = Compiler::Expected(planned.range);
		auto&      data = a_record->data;
		data.spellType = RE::MagicSystem::SpellType::kSpell;
		data.castingType = expected.castingType;
		data.delivery = expected.delivery;
		data.flags.set(RE::SpellItem::SpellFlag::kCostOverride);
		if (primary) {
			data.costOverride = static_cast<std::int32_t>(a_def.cost);
			data.castingPerk = state.CastingPerk(a_plan.school, a_plan.rank);
		} else {
			// Linked sub-spells cost nothing; the equipped spell carries the whole cost and the
			// failure check (OUTLINE "Mixed-range spells" 2 and 4).
			data.costOverride = 0;
			data.castingPerk = nullptr;
		}
		// Sub-spells carry the spell's own name so their Self buffs read correctly in the Active
		// Effects list; they are never in a spell list, so the name never shows elsewhere.
		a_record->SetFullName(a_def.name.c_str());
		Compiler::RefreshCaches(a_record);

		if (a_record->IsHostile() != planned.hostile) {
			logger::debug("'{}' spell #{}: engine hostility {} (from MGEF flags) differs from the plan's {}", a_def.name, a_index,
				a_record->IsHostile(), planned.hostile);
		}
		return true;
	}

	RE::SpellItem* Compile(CustomSpell& a_spell)
	{
		auto&       state = State::Get();
		const auto& def = a_spell.def;
		const auto& plan = a_spell.plan;

		if (def.slot >= state.primarySlots.size() || !state.primarySlots[def.slot]) {
			logger::error("compile '{}': primary slot {} is not available", def.name, def.slot);
			return nullptr;
		}
		if (plan.spells.empty()) {
			logger::error("compile '{}': empty plan", def.name);
			return nullptr;
		}
		if (plan.tooComplex) {
			logger::warn("compile '{}': plan is marked too complex; compiling what fits", def.name);
		}
		if (def.subSlots.size() < plan.SubSlotsNeeded()) {
			logger::error("compile '{}': needs {} sub slots, has {}", def.name, plan.SubSlotsNeeded(), def.subSlots.size());
			return nullptr;
		}

		auto* primary = state.primarySlots[def.slot];
		if (!CompileInto(primary, def, plan, 0)) {
			return nullptr;
		}
		a_spell.primary = primary;
		a_spell.subs.clear();
		for (std::size_t i = 1; i < plan.spells.size(); ++i) {
			const auto slot = def.subSlots[i - 1];
			auto*      record = slot < state.subSlots.size() ? state.subSlots[slot] : nullptr;
			if (!CompileInto(record, def, plan, i)) {
				return nullptr;
			}
			a_spell.subs.push_back(record);
		}
		// Sub slots the definition still holds but the plan no longer needs are blanked here;
		// the caller frees them.
		for (std::size_t i = plan.SubSlotsNeeded(); i < def.subSlots.size(); ++i) {
			const auto slot = def.subSlots[i];
			if (slot < state.subSlots.size()) {
				Blank(state.subSlots[slot]);
			}
		}
		logger::debug("compiled '{}' into slot {} (+{} sub): cost {}, school {}, rank {}", def.name, def.slot, a_spell.subs.size(), def.cost,
			ToString(plan.school), ToString(plan.rank));
		return primary;
	}

	void Blank(RE::SpellItem* a_slot)
	{
		auto& state = State::Get();
		if (!a_slot || !state.blankEffect) {
			return;
		}
		Compiler::ReplaceEffects(a_slot, { Compiler::MakeEffect(state.blankEffect, 0.0f, 0, 0, 0.0f) });
		auto& data = a_slot->data;
		data.spellType = RE::MagicSystem::SpellType::kSpell;
		data.castingType = RE::MagicSystem::CastingType::kFireAndForget;
		data.delivery = RE::MagicSystem::Delivery::kSelf;
		data.flags.set(RE::SpellItem::SpellFlag::kCostOverride);
		data.costOverride = 0;
		data.castingPerk = nullptr;
		a_slot->SetFullName("");
		a_slot->hostileCount = 0;
		a_slot->avEffectSetting = state.blankEffect;
	}

	bool IsBlank(const RE::SpellItem* a_record)
	{
		const auto& state = State::Get();
		if (!a_record || a_record->effects.size() != 1) {
			return false;
		}
		const auto* effect = a_record->effects.front();
		return effect && effect->baseEffect == state.blankEffect && a_record->data.costOverride == 0 && a_record->data.castingPerk == nullptr;
	}
}
