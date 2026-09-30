#include "Core/Spellbook.h"

#include "Core/SpellCompiler.h"
#include "Effects/EffectSystems.h"
#include "Papyrus/Natives.h"

// The spellbook (OUTLINE "Spell management"): creating, replacing, deleting and rebuilding custom
// spells, plus the discovery view of the player's known spells.
//
// Locking: every function here runs on the main thread, the only thread that mutates per-save
// state, so it reads State without the lock and takes the unique lock only around writes. The
// lock is never held across a call into the engine: AddSpell, RemoveSpell and Dispel can fire
// events synchronously whose sinks (Casting, Effects, UI) call State::FindBySpell.
namespace LA::Spellbook
{
	namespace
	{
		RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

		float DaysPassed()
		{
			const auto* calendar = RE::Calendar::GetSingleton();
			return calendar ? calendar->GetDaysPassed() : 0.0f;
		}

		// Every record a custom spell occupies (primary first).
		std::vector<RE::SpellItem*> RecordsOf(const CustomSpell& a_spell)
		{
			auto&                       state = State::Get();
			std::vector<RE::SpellItem*> out;
			if (a_spell.def.slot < state.primarySlots.size() && state.primarySlots[a_spell.def.slot]) {
				out.push_back(state.primarySlots[a_spell.def.slot]);
			}
			for (const auto sub : a_spell.def.subSlots) {
				if (sub < state.subSlots.size() && state.subSlots[sub]) {
					out.push_back(state.subSlots[sub]);
				}
			}
			return out;
		}

		// Makes the definition hold exactly a_need sub slots (allocating the missing ones). Slots
		// beyond a_need stay in the definition until ReleaseExtraSubs, after Compile blanked them.
		bool ReserveSubs(CustomSpell& a_spell, std::size_t a_need)
		{
			auto& state = State::Get();
			auto& subs = a_spell.def.subSlots;
			if (subs.size() >= a_need) {
				return true;
			}
			std::unique_lock guard(state.lock);
			auto extra = state.slots.AllocateSubs(a_need - subs.size());
			if (extra.size() < a_need - subs.size()) {
				for (const auto slot : extra) {
					state.slots.FreeSub(slot);
				}
				return false;
			}
			for (const auto slot : extra) {
				if (slot < state.subOwner.size()) {
					state.subOwner[slot] = a_spell.def.slot;
				}
				subs.push_back(slot);
			}
			return true;
		}

		void ReleaseExtraSubs(CustomSpell& a_spell, std::size_t a_need)
		{
			auto& state = State::Get();
			auto& subs = a_spell.def.subSlots;
			if (subs.size() <= a_need) {
				return;
			}
			std::unique_lock guard(state.lock);
			for (std::size_t i = a_need; i < subs.size(); ++i) {
				state.slots.FreeSub(subs[i]);
				if (subs[i] < state.subOwner.size()) {
					state.subOwner[subs[i]] = kNoSlot;
				}
			}
			subs.resize(a_need);
		}

		// Frees every slot of a definition and forgets it (records must already be blank).
		void Forget(std::uint16_t a_slot)
		{
			auto&            state = State::Get();
			std::unique_lock guard(state.lock);
			const auto       it = state.spells.find(a_slot);
			if (it == state.spells.end()) {
				return;
			}
			for (const auto sub : it->second.def.subSlots) {
				state.slots.FreeSub(sub);
				if (sub < state.subOwner.size()) {
					state.subOwner[sub] = kNoSlot;
				}
			}
			state.slots.FreePrimary(a_slot);
			state.spells.erase(it);
		}

		// Re-plans and recompiles one definition in place (load, rebuild, rebalance).
		bool Rebuild(CustomSpell& a_spell)
		{
			a_spell.plan = PlanFor(a_spell.def);
			if (a_spell.plan.spells.empty()) {
				logger::warn("'{}' (slot {}) has no effects left; compiled as an inert spell", a_spell.def.name, a_spell.def.slot);
				// Keep the record valid and castable-but-inert: a primary with the placeholder.
				a_spell.plan.spells.push_back(PlannedSpell{ Range::kSelf, true, false, {} });
			}
			const auto need = a_spell.plan.SubSlotsNeeded();
			if (!ReserveSubs(a_spell, need)) {
				logger::error("'{}' (slot {}) needs {} sub slots but the pool is full; linked effects dropped", a_spell.def.name,
					a_spell.def.slot, need);
				a_spell.plan.spells.resize(1 + a_spell.def.subSlots.size());
			}
			const bool ok = Compile(a_spell) != nullptr;
			ReleaseExtraSubs(a_spell, a_spell.plan.SubSlotsNeeded());
			return ok;
		}

		bool IsEquippedBy(RE::Actor* a_actor, const RE::SpellItem* a_spell)
		{
			const auto& runtime = a_actor->GetActorRuntimeData();
			for (const auto* selected : runtime.selectedSpells) {
				if (selected == a_spell) {
					return true;
				}
			}
			return false;
		}

		void DispelOn(RE::Actor* a_actor, const std::unordered_set<const RE::MagicItem*>& a_spells)
		{
			auto* target = a_actor ? a_actor->AsMagicTarget() : nullptr;
			auto* list = target ? target->GetActiveEffectList() : nullptr;
			if (!list) {
				return;
			}
			// Collect first: dispelling edits the list we would be walking.
			std::vector<RE::ActiveEffect*> doomed;
			for (auto* effect : *list) {
				if (effect && a_spells.contains(effect->spell)) {
					doomed.push_back(effect);
				}
			}
			for (auto* effect : doomed) {
				effect->Dispel(true);
			}
		}

		const char* ArchetypeName(RE::EffectArchetype a_archetype)
		{
			// Creation Kit archetype names without spaces (discovery rules use these; the engine's
			// own GetArchetypeAsString abbreviates some, e.g. "ValueMod").
			using A = RE::EffectArchetype;
			switch (a_archetype) {
			case A::kValueModifier: return "ValueModifier";
			case A::kScript: return "Script";
			case A::kDispel: return "Dispel";
			case A::kCureDisease: return "CureDisease";
			case A::kAbsorb: return "Absorb";
			case A::kDualValueModifier: return "DualValueModifier";
			case A::kCalm: return "Calm";
			case A::kDemoralize: return "Demoralize";
			case A::kFrenzy: return "Frenzy";
			case A::kDisarm: return "Disarm";
			case A::kCommandSummoned: return "CommandSummoned";
			case A::kInvisibility: return "Invisibility";
			case A::kLight: return "Light";
			case A::kDarkness: return "Darkness";
			case A::kNightEye: return "NightEye";
			case A::kLock: return "Lock";
			case A::kOpen: return "Open";
			case A::kBoundWeapon: return "BoundWeapon";
			case A::kSummonCreature: return "SummonCreature";
			case A::kDetectLife: return "DetectLife";
			case A::kTelekinesis: return "Telekinesis";
			case A::kParalysis: return "Paralysis";
			case A::kReanimate: return "Reanimate";
			case A::kSoulTrap: return "SoulTrap";
			case A::kTurnUndead: return "TurnUndead";
			case A::kGuide: return "Guide";
			case A::kWerewolfFeed: return "WerewolfFeed";
			case A::kCureParalysis: return "CureParalysis";
			case A::kCureAddiction: return "CureAddiction";
			case A::kCurePoison: return "CurePoison";
			case A::kConcussion: return "Concussion";
			case A::kValueAndParts: return "ValueAndParts";
			case A::kAccumulateMagnitude: return "AccumulateMagnitude";
			case A::kStagger: return "Stagger";
			case A::kPeakValueModifier: return "PeakValueModifier";
			case A::kCloak: return "Cloak";
			case A::kWerewolf: return "Werewolf";
			case A::kSlowTime: return "SlowTime";
			case A::kRally: return "Rally";
			case A::kEnhanceWeapon: return "EnhanceWeapon";
			case A::kSpawnHazard: return "SpawnHazard";
			case A::kEtherealize: return "Etherealize";
			case A::kBanish: return "Banish";
			case A::kSpawnScriptedRef: return "SpawnScriptedRef";
			case A::kDisguise: return "Disguise";
			case A::kGrabActor: return "GrabActor";
			case A::kVampireLord: return "VampireLord";
			default: return "";
			}
		}

		std::string ActorValueName(RE::ActorValue a_value)
		{
			if (a_value == RE::ActorValue::kNone) {
				return {};
			}
			// The engine's ActorValueInfo enum names ("Health", "FireResist", "ElectricResist"),
			// which are the Creation Kit names the discovery rules are written in.
			const char* name = RE::ActorValueList::GetActorValueName(a_value);
			return name ? std::string(name) : std::string();
		}

		const char* SchoolName(RE::ActorValue a_skill)
		{
			switch (a_skill) {
			case RE::ActorValue::kAlteration: return "Alteration";
			case RE::ActorValue::kConjuration: return "Conjuration";
			case RE::ActorValue::kDestruction: return "Destruction";
			case RE::ActorValue::kIllusion: return "Illusion";
			case RE::ActorValue::kRestoration: return "Restoration";
			default: return "";
			}
		}

		const char* CastingTypeName(RE::MagicSystem::CastingType a_type)
		{
			switch (a_type) {
			case RE::MagicSystem::CastingType::kConstantEffect: return "ConstantEffect";
			case RE::MagicSystem::CastingType::kFireAndForget: return "FireAndForget";
			case RE::MagicSystem::CastingType::kConcentration: return "Concentration";
			case RE::MagicSystem::CastingType::kScroll: return "Scroll";
			default: return "";
			}
		}

		const char* DeliveryName(RE::MagicSystem::Delivery a_delivery)
		{
			switch (a_delivery) {
			case RE::MagicSystem::Delivery::kSelf: return "Self";
			case RE::MagicSystem::Delivery::kTouch: return "Touch";
			case RE::MagicSystem::Delivery::kAimed: return "Aimed";
			case RE::MagicSystem::Delivery::kTargetActor: return "TargetActor";
			case RE::MagicSystem::Delivery::kTargetLocation: return "TargetLocation";
			default: return "";
			}
		}

		FormRef RefOf(const RE::TESForm* a_form)
		{
			FormRef ref;
			const auto* file = a_form ? a_form->GetFile(0) : nullptr;
			if (!file) {
				return ref;  // runtime-created form
			}
			ref.plugin = std::string(file->GetFilename());
			ref.localId = a_form->GetFormID() & (file->IsLight() ? 0xFFFu : 0xFFFFFFu);
			return ref;
		}

		// Spells that arrive with the character's race (racial powers and spells, e.g. the
		// Breton Dragonskin or the Altmer's extra magicka) or that the base record lists (the
		// player's starting Flames and Healing): the engine's RemoveSpell cannot remove base-list
		// spells from one actor, so both count as inherent. Standing stones and birthsign-like
		// sources are abilities or powers, caught by the spell-type check.
		bool IsInherent(RE::Actor* a_actor, const RE::SpellItem* a_spell)
		{
			auto listed = [a_spell](const RE::TESSpellList* a_list) {
				const auto* data = a_list ? a_list->actorEffects : nullptr;
				if (!data || !data->spells) {
					return false;
				}
				for (std::uint32_t i = 0; i < data->numSpells; ++i) {
					if (data->spells[i] == a_spell) {
						return true;
					}
				}
				return false;
			};
			if (!a_actor) {
				return false;
			}
			if (listed(a_actor->GetRace())) {
				return true;
			}
			return listed(a_actor->GetActorBase());
		}
	}

	// --- Planning ------------------------------------------------------------------------------

	bool EnsureEffect(std::string_view a_effectId)
	{
		auto& state = State::Get();
		if (state.catalog.Find(a_effectId)) {
			return true;
		}
		if (!a_effectId.starts_with("pt.")) {
			return false;
		}
		const auto ref = ParseFormRef(a_effectId.substr(3));
		auto*      mgef = FormMap::Resolve<RE::EffectSetting>(ref);
		if (!mgef) {
			return false;
		}
		auto def = MakePassThrough(Describe(mgef));
		def.id = std::string(a_effectId);  // keep the saved id verbatim
		state.AddPassThrough(std::move(def));
		return true;
	}

	CompilePlan PlanFor(const SpellDef& a_def)
	{
		auto& state = State::Get();
		for (const auto& effect : a_def.effects) {
			if (!EnsureEffect(effect.effectId)) {
				logger::warn("'{}' (slot {}): effect {} is unavailable (effect pack or plugin missing)", a_def.name, a_def.slot,
					effect.effectId);
			}
		}
		return Compiler::Plan(state.catalog, a_def.effects, state.settings, state.content.Riders());
	}

	// --- Create / replace / delete ------------------------------------------------------------

	RE::SpellItem* Create(SpellDef a_def, const CompilePlan& a_plan)
	{
		auto& state = State::Get();
		auto* player = Player();
		if (!state.dataReady || !player) {
			logger::error("Create: plugin data not ready");
			return nullptr;
		}
		if (a_plan.spells.empty() || a_plan.tooComplex) {
			logger::error("Create '{}': plan is empty or too complex", a_def.name);
			return nullptr;
		}

		std::uint16_t slot = kNoSlot;
		{
			std::unique_lock guard(state.lock);
			const auto primary = state.slots.AllocatePrimary();
			if (!primary) {
				logger::warn("Create '{}': no free spell slot", a_def.name);
				return nullptr;
			}
			std::vector<std::uint16_t> subs;
			if (a_plan.SubSlotsNeeded() > 0) {
				subs = state.slots.AllocateSubs(a_plan.SubSlotsNeeded());
				if (subs.size() < a_plan.SubSlotsNeeded()) {
					state.slots.FreePrimary(*primary);
					logger::warn("Create '{}': not enough free sub-spell slots ({} needed)", a_def.name, a_plan.SubSlotsNeeded());
					return nullptr;
				}
			}
			slot = *primary;
			a_def.slot = slot;
			a_def.subSlots = subs;
			a_def.created = DaysPassed();
			a_def.name = Utf8Truncate(a_def.name, kMaxNameLength);
			for (const auto sub : subs) {
				if (sub < state.subOwner.size()) {
					state.subOwner[sub] = slot;
				}
			}
			state.spells[slot] = CustomSpell{ a_def, nullptr, {}, a_plan };
		}

		auto& custom = state.spells.at(slot);
		auto* spell = Compile(custom);
		if (!spell) {
			for (auto* record : RecordsOf(custom)) {
				Blank(record);
			}
			Forget(slot);
			return nullptr;
		}

		player->AddSpell(spell);
		logger::info("created '{}' in slot {} (+{} sub): cost {}, price {}", custom.def.name, slot, custom.def.subSlots.size(),
			custom.def.cost, custom.def.pricePaid);
		Papyrus::SendSpellEvent("LostArt_SpellCreated"sv, spell);
		return spell;
	}

	RE::SpellItem* Replace(std::uint16_t a_slot, SpellDef a_def, const CompilePlan& a_plan)
	{
		auto& state = State::Get();
		auto  it = state.spells.find(a_slot);
		if (it == state.spells.end()) {
			logger::error("Replace: slot {} holds no custom spell", a_slot);
			return nullptr;
		}
		if (a_plan.spells.empty() || a_plan.tooComplex) {
			logger::error("Replace '{}': plan is empty or too complex", a_def.name);
			return nullptr;
		}
		auto&      custom = it->second;
		const auto previous = custom;

		// Active effects of the old version would keep running against rewritten records.
		std::unordered_set<const RE::MagicItem*> records;
		for (auto* record : RecordsOf(custom)) {
			records.insert(record);
		}
		if (auto* player = Player()) {
			DispelOn(player, records);
		}

		{
			std::unique_lock guard(state.lock);
			a_def.slot = a_slot;
			a_def.subSlots = custom.def.subSlots;
			a_def.flags |= SpellFlags::kReplaced;
			a_def.created = DaysPassed();
			a_def.name = Utf8Truncate(a_def.name, kMaxNameLength);
			custom.def = std::move(a_def);
			custom.plan = a_plan;
		}
		if (!ReserveSubs(custom, a_plan.SubSlotsNeeded())) {
			logger::warn("Replace '{}': not enough free sub-spell slots", custom.def.name);
			std::unique_lock guard(state.lock);
			custom = previous;
			return nullptr;
		}
		auto* spell = Compile(custom);
		if (!spell) {
			logger::error("Replace '{}': compile failed; restoring the previous version", custom.def.name);
			{
				std::unique_lock guard(state.lock);
				const auto held = custom.def.subSlots;
				custom = previous;
				custom.def.subSlots = held;
			}
			Rebuild(custom);
			return nullptr;
		}
		ReleaseExtraSubs(custom, a_plan.SubSlotsNeeded());

		if (auto* player = Player(); player && !player->HasSpell(spell)) {
			player->AddSpell(spell);
		}
		logger::info("replaced slot {} with '{}' (cost {})", a_slot, custom.def.name, custom.def.cost);
		Papyrus::SendSpellEvent("LostArt_SpellCreated"sv, spell);
		return spell;
	}

	void RemoveEverywhere(const std::vector<RE::SpellItem*>& a_spells)
	{
		std::unordered_set<const RE::MagicItem*> set(a_spells.begin(), a_spells.end());
		set.erase(nullptr);
		if (set.empty()) {
			return;
		}
		auto strip = [&](RE::Actor* a_actor) {
			if (!a_actor) {
				return;
			}
			for (auto* spell : a_spells) {
				if (!spell) {
					continue;
				}
				if (IsEquippedBy(a_actor, spell)) {
					// VERIFY(in-game): DeselectSpell clears the hand selection and its hand art.
					a_actor->DeselectSpell(spell);
				}
				if (a_actor->HasSpell(spell)) {
					a_actor->RemoveSpell(spell);
				}
			}
			DispelOn(a_actor, set);
		};

		auto* player = Player();
		strip(player);
		if (auto* favorites = RE::MagicFavorites::GetSingleton()) {
			for (auto* spell : a_spells) {
				if (spell) {
					favorites->RemoveFavorite(spell);
				}
			}
		}
		// Followers and anyone else loaded who was taught or hit by the spell.
		if (auto* lists = RE::ProcessLists::GetSingleton()) {
			lists->ForEachHighActor([&](RE::Actor* a_actor) {
				if (a_actor != player) {
					strip(a_actor);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}
	}

	bool Delete(std::uint16_t a_slot)
	{
		auto&      state = State::Get();
		const auto it = state.spells.find(a_slot);
		if (it == state.spells.end()) {
			logger::warn("Delete: slot {} holds no custom spell", a_slot);
			return false;
		}
		const auto records = RecordsOf(it->second);
		const auto name = it->second.def.name;
		RE::SpellItem* primary = records.empty() ? nullptr : records.front();

		RemoveEverywhere(records);
		for (auto* record : records) {
			Blank(record);
		}
		Forget(a_slot);
		logger::info("deleted '{}' (slot {})", name, a_slot);
		Papyrus::SendSpellEvent("LostArt_SpellDeleted"sv, primary);
		return true;
	}

	DeleteCheck CanDelete(RE::SpellItem* a_spell)
	{
		auto& state = State::Get();
		if (!a_spell) {
			return DeleteCheck::kInherent;
		}
		{
			// Also called from the magic menu's delete hotkey (UI task): look up under the lock.
			std::shared_lock guard(state.lock);
			if (const auto* custom = state.FindBySpellLocked(a_spell)) {
				// Linked sub-spells are never in a spell list; only the equipped spell is deletable.
				return custom->primary == a_spell || (custom->def.slot < state.primarySlots.size() &&
				                                         state.primarySlots[custom->def.slot] == a_spell) ?
				           DeleteCheck::kOk :
				           DeleteCheck::kInherent;
			}
		}
		if (state.slotIndex.contains(a_spell)) {
			return DeleteCheck::kNotCustom;  // an unused slot record
		}
		if (state.settings.deletable == 0) {
			return DeleteCheck::kNotCustom;
		}
		// Powers, lesser powers, abilities (standing stones, racial abilities), diseases, shouts.
		if (a_spell->GetSpellType() != RE::MagicSystem::SpellType::kSpell) {
			return DeleteCheck::kInherent;
		}
		if (IsInherent(Player(), a_spell)) {
			return DeleteCheck::kInherent;
		}
		if (state.protectedSpells.contains(a_spell->GetFormID())) {
			return DeleteCheck::kQuestSpell;
		}
		return DeleteCheck::kOk;
	}

	bool DeleteAny(RE::SpellItem* a_spell)
	{
		auto& state = State::Get();
		if (CanDelete(a_spell) != DeleteCheck::kOk) {
			return false;
		}
		if (const auto slot = state.SlotOf(a_spell)) {
			return Delete(*slot);
		}
		// A vanilla or modded spell: Morrowind removes it from the spellbook only; effects that
		// are already running finish normally.
		auto* player = Player();
		if (!player) {
			return false;
		}
		if (IsEquippedBy(player, a_spell)) {
			player->DeselectSpell(a_spell);
		}
		player->RemoveSpell(a_spell);
		if (auto* favorites = RE::MagicFavorites::GetSingleton()) {
			favorites->RemoveFavorite(a_spell);
		}
		const bool removed = !player->HasSpell(a_spell);
		logger::info("deleted spell {} ({:08X}): {}", a_spell->GetName(), a_spell->GetFormID(), removed ? "ok" : "the engine kept it");
		if (removed) {
			Papyrus::SendSpellEvent("LostArt_SpellDeleted"sv, a_spell);
		}
		return removed;
	}

	// --- Maintenance --------------------------------------------------------------------------

	int RebalanceAll()
	{
		auto&      state = State::Get();
		const auto model = state.settings.costModel;
		int        changed = 0;
		for (auto& [slot, custom] : state.spells) {
			const auto result = Cost::Compute(state.catalog, custom.def.effects, state.settings, model, state.BaseCostFn());
			const bool stale = (custom.def.flags & SpellFlags::kNeedsRebalance) != 0;
			if (result.cost == custom.def.cost && custom.def.costModel == model && !stale) {
				continue;
			}
			{
				std::unique_lock guard(state.lock);
				custom.def.cost = result.cost;
				custom.def.costModel = model;
				custom.def.flags &= ~SpellFlags::kNeedsRebalance;
			}
			if (Rebuild(custom)) {
				++changed;
			}
		}
		logger::info("rebalance: {} of {} spells changed", changed, state.spells.size());
		return changed;
	}

	int RebuildAll()
	{
		auto& state = State::Get();
		int   built = 0;
		for (auto& [slot, custom] : state.spells) {
			if (Rebuild(custom)) {
				++built;
			}
		}
		logger::info("rebuild: {} of {} spells compiled", built, state.spells.size());
		return built;
	}

	int Restore(std::vector<SpellDef> a_defs)
	{
		auto&                      state = State::Get();
		std::vector<std::uint16_t> claimed;
		{
			std::unique_lock guard(state.lock);
			for (auto& def : a_defs) {
				if (def.slot >= state.slots.PrimaryCapacity() || def.slot >= state.primarySlots.size() || !state.primarySlots[def.slot]) {
					logger::error("co-save: '{}' refers to slot {} which this installation does not have; dropped", def.name, def.slot);
					continue;
				}
				if (!state.slots.ClaimPrimary(def.slot)) {
					logger::error("co-save: slot {} is claimed twice ('{}'); dropped", def.slot, def.name);
					continue;
				}
				std::vector<std::uint16_t> subs;
				for (const auto sub : def.subSlots) {
					if (sub < state.slots.SubCapacity() && state.slots.ClaimSub(sub)) {
						subs.push_back(sub);
						if (sub < state.subOwner.size()) {
							state.subOwner[sub] = def.slot;
						}
					} else {
						logger::warn("co-save: '{}' sub slot {} is invalid or shared; reallocating", def.name, sub);
					}
				}
				def.subSlots = std::move(subs);
				const auto slot = def.slot;
				state.spells[slot] = CustomSpell{ std::move(def), nullptr, {}, {} };
				claimed.push_back(slot);
			}
		}
		int built = 0;
		for (const auto slot : claimed) {
			if (Rebuild(state.spells.at(slot))) {
				++built;
			}
		}
		return built;
	}

	void Revert()
	{
		auto&       state = State::Get();
		std::size_t blanked = 0;
		for (auto* records : { &state.primarySlots, &state.subSlots }) {
			for (auto* record : *records) {
				if (record && !IsBlank(record)) {
					Blank(record);
					++blanked;
				}
			}
		}
		{
			std::unique_lock guard(state.lock);
			state.spells.clear();
			state.slots.Reset();
			std::fill(state.subOwner.begin(), state.subOwner.end(), kNoSlot);
			state.marks.clear();
			state.ledger.clear();
			state.conditions.clear();
			state.version = VersionInfo{};
		}
		logger::debug("revert: {} slot records blanked", blanked);
	}

	bool PrepareForUninstall()
	{
		auto&                       state = State::Get();
		std::vector<RE::SpellItem*> records;
		for (const auto& [slot, custom] : state.spells) {
			for (auto* record : RecordsOf(custom)) {
				records.push_back(record);
			}
		}
		RemoveEverywhere(records);
		Effects::PrepareForUninstall();
		for (auto* record : records) {
			Blank(record);
		}
		const auto count = state.spells.size();
		{
			std::unique_lock guard(state.lock);
			state.spells.clear();
			state.slots.Reset();
			std::fill(state.subOwner.begin(), state.subOwner.end(), kNoSlot);
			state.marks.clear();
			state.ledger.clear();
			state.conditions.clear();
		}
		logger::info("prepare for uninstall: {} custom spells removed from every loaded actor and blanked", count);
		return true;
	}

	// --- Discovery view -----------------------------------------------------------------------

	std::string SpellTypeName(RE::MagicSystem::SpellType a_type)
	{
		using T = RE::MagicSystem::SpellType;
		switch (a_type) {
		case T::kSpell: return "Spell";
		case T::kDisease: return "Disease";
		case T::kPower: return "Power";
		case T::kLesserPower: return "LesserPower";
		case T::kAbility: return "Ability";
		case T::kPoison: return "Poison";
		case T::kEnchantment: return "Enchantment";
		case T::kPotion: return "Potion";
		case T::kIngredient: return "Ingredient";
		case T::kLeveledSpell: return "LeveledSpell";
		case T::kAddiction: return "Addiction";
		case T::kVoicePower: return "Voice";
		case T::kStaffEnchantment: return "StaffEnchantment";
		case T::kScroll: return "Scroll";
		default: return "Unknown";
		}
	}

	EffectDescriptor Describe(const RE::EffectSetting* a_effect)
	{
		EffectDescriptor out;
		if (!a_effect) {
			return out;
		}
		using Flag = RE::EffectSetting::EffectSettingData::Flag;
		const auto& data = a_effect->data;
		out.form = RefOf(a_effect);
		out.fullFormId = a_effect->GetFormID();
		if (const char* editorId = a_effect->GetFormEditorID()) {
			out.editorId = editorId;  // usually empty at runtime for MGEFs
		}
		if (const char* name = a_effect->GetFullName()) {
			out.name = name;
		}
		out.archetype = ArchetypeName(data.archetype);
		out.actorValue = ActorValueName(data.primaryAV);
		out.secondAV = ActorValueName(data.secondaryAV);
		out.resist = ActorValueName(data.resistVariable);
		for (std::uint32_t i = 0; i < a_effect->numKeywords; ++i) {
			const auto* keyword = a_effect->keywords[i];
			// Keywords keep their EditorID at runtime (BGSKeyword::formEditorID).
			if (keyword && keyword->GetFormEditorID() && *keyword->GetFormEditorID()) {
				out.keywords.emplace_back(keyword->GetFormEditorID());
			}
		}
		out.hostile = data.flags.all(Flag::kHostile);
		out.detrimental = data.flags.all(Flag::kDetrimental);
		out.castingType = CastingTypeName(data.castingType);
		out.delivery = DeliveryName(data.delivery);
		out.hideInUI = data.flags.all(Flag::kHideInUI);
		out.noMagnitude = data.flags.all(Flag::kNoMagnitude);
		out.noDuration = data.flags.all(Flag::kNoDuration);
		out.noArea = data.flags.all(Flag::kNoArea);
		out.scripted = data.archetype == RE::EffectArchetype::kScript;
		out.baseCost = data.baseCost;
		out.school = SchoolName(data.associatedSkill);
		return out;
	}

	std::vector<KnownSpell> KnownSpells(RE::Actor* a_actor)
	{
		auto&                               state = State::Get();
		std::vector<KnownSpell>             out;
		std::unordered_set<RE::SpellItem*> seen;
		auto add = [&](RE::SpellItem* a_spell) {
			if (!a_spell || !seen.insert(a_spell).second || state.slotIndex.contains(a_spell)) {
				return;  // our own slots never teach effects
			}
			KnownSpell known;
			known.type = SpellTypeName(a_spell->GetSpellType());
			for (const auto* effect : a_spell->effects) {
				if (effect && effect->baseEffect) {
					known.effects.push_back(Describe(effect->baseEffect));
				}
			}
			out.push_back(std::move(known));
		};
		auto addList = [&](const RE::TESSpellList* a_list) {
			const auto* data = a_list ? a_list->actorEffects : nullptr;
			if (!data || !data->spells) {
				return;
			}
			for (std::uint32_t i = 0; i < data->numSpells; ++i) {
				add(data->spells[i]);
			}
		};
		if (!a_actor) {
			return out;
		}
		// The same three sources the Magic menu lists: race, base record, spells added in play.
		addList(a_actor->GetRace());
		addList(a_actor->GetActorBase());
		for (auto* spell : a_actor->GetActorRuntimeData().addedSpells) {
			add(spell);
		}
		return out;
	}

	std::set<std::string> KnownEffects(RE::Actor* a_actor)
	{
		auto&                 state = State::Get();
		std::set<std::string> ids;
		if (state.settings.availability == 1) {
			ids = Discovery::Everything(state.catalog, state.settings);
		} else {
			auto result = state.discovery.Discover(state.catalog, KnownSpells(a_actor), state.settings);
			for (auto& def : result.passThrough) {
				state.AddPassThrough(std::move(def));
			}
			for (const auto& form : result.unmatched) {
				logger::debug("discovery: {} matched no catalog effect", form);
			}
			ids = std::move(result.effects);
		}
		std::erase_if(ids, [&](const std::string& a_id) { return !state.EffectUsable(a_id); });
		return ids;
	}

	std::vector<const CustomSpell*> List()
	{
		auto&                           state = State::Get();
		std::shared_lock                guard(state.lock);
		std::vector<const CustomSpell*> out;
		out.reserve(state.spells.size());
		for (const auto& [slot, custom] : state.spells) {
			out.push_back(&custom);
		}
		return out;
	}
}
