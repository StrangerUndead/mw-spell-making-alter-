// Combat systems: elemental shields (resistance, retaliation, shell shader), the miss system
// (Blind, Sanctuary, Chameleon evasion, Fortify Attack, attribute evasion), melee damage
// modifiers (Fortify Attack, Strength, weapon condition, Resist/Weakness to Normal Weapons,
// damaged armor and Unarmored), Chameleon's detection bonus and translucency, and the
// Disintegrate condition pools.
//
// Hooks and sources:
//  * Melee/ranged weapon hit processing: the call to the "process hit" function inside
//    RELOCATION_ID(37673, 38627) at +0x3C0 (SE) / +0x4A8 (AE), thunk void(Actor* victim,
//    HitData&). Hooked the same way by D7ry/valhallaCombat (src/include/Hooks.h, hitEventHook:
//    `_ProcessHit = trampoline.write_call<5>(hook.address() + RELOCATION_OFFSET(0x3C0, 0x4A8),
//    processHit)`), whose parry/stamina-block features rely on skipping or editing the hit there.
//    Not calling the original makes the attack a miss: no damage, no stagger, no on-hit effects.
//    VERIFY(in-game): the call site bytes are checked for E8 before patching.
//  * TESHitEvent (CommonLib RE/T/TESHitEvent.h) for shield retaliation: projectile 0 and a
//    melee weapon (or none: unarmed) as source; kHitBlocked hits don't retaliate.
//  * Skyrim armor formula: damage x (1 - min(fMaxArmorRating, AR x fArmorScalingFactor) / 100)
//    with the GMSTs read at runtime (vanilla 80 and 0.12).

#include "Effects/EffectsInternal.h"

namespace LA::Effects::Internal
{
	namespace
	{
		// ---- shields ----------------------------------------------------------------------
		struct ShieldInfo
		{
			std::string_view id;
			RE::ActorValue   resist;
			std::string_view shader;  // vanilla EnchArmor*FXS shell shaders
		};
		constexpr std::array kShields{
			ShieldInfo{ "mw.fire_shield", RE::ActorValue::kResistFire, "Skyrim.esm|0x092DE7" },       // EnchArmorFireFXS
			ShieldInfo{ "mw.frost_shield", RE::ActorValue::kResistFrost, "Skyrim.esm|0x092DE8" },     // EnchArmorFrostFXS
			ShieldInfo{ "mw.lightning_shield", RE::ActorValue::kResistShock, "Skyrim.esm|0x092DE9" },  // EnchArmorShockFXS
		};

		const ShieldInfo* Shield(std::string_view a_id)
		{
			for (const auto& s : kShields) {
				if (s.id == a_id) {
					return &s;
				}
			}
			return nullptr;
		}

		void StopShader(RE::Actor* a_actor, RE::TESEffectShader* a_shader)
		{
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!a_actor || !a_shader || !lists) {
				return;
			}
			const auto handle = a_actor->GetHandle();
			lists->ForEachShaderEffect([&](RE::ShaderReferenceEffect* a_effect) {
				if (a_effect && a_effect->effectData == a_shader && a_effect->target == handle) {
					a_effect->finished = true;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}

		void ShieldShader(Instance& a_inst)
		{
			const auto* info = Shield(a_inst.key->id);
			auto*       shader = info ? Vanilla<RE::TESEffectShader>(info->shader) : nullptr;
			if (auto* target = a_inst.Target(); target && shader && !HasInstanceOther(a_inst, a_inst.key->id)) {
				target->ApplyEffectShader(shader, -1.0f);
			}
		}

		void ShieldStart(Instance& a_inst)
		{
			const auto* info = Shield(a_inst.key->id);
			if (!info) {
				return;
			}
			ApplyAVDeltas(a_inst.Target(), a_inst.avTarget, { { info->resist, a_inst.magnitude } });
			ShieldShader(a_inst);
		}

		void ShieldResume(Instance& a_inst)
		{
			const auto* info = Shield(a_inst.key->id);
			if (!info) {
				return;
			}
			a_inst.avTarget = { { info->resist, a_inst.magnitude } };
			ShieldShader(a_inst);  // temp effects are not saved
		}

		void ShieldFinish(Instance& a_inst)
		{
			RevertAVDeltas(a_inst.Target(), a_inst.avTarget);
			const auto* info = Shield(a_inst.key->id);
			if (info && !HasInstanceOther(a_inst, a_inst.key->id)) {
				StopShader(a_inst.Target(), Vanilla<RE::TESEffectShader>(info->shader));
			}
		}

		void Retaliate(RE::Actor* a_defender, RE::Actor* a_attacker)
		{
			const auto& settings = State::Get().settings;
			for (const auto& shield : kShields) {
				const double magnitude = SumMagnitude(a_defender, shield.id);
				if (magnitude <= 0.0) {
					continue;
				}
				// Morrowind (OpenMW applyElementalShields): the attacker's save uses its Destruction
				// skill plus the Willpower/Luck stand-in, scaled by fatigue; its resistance adds.
				const double skill = a_attacker->AsActorValueOwner()->GetActorValue(RE::ActorValue::kDestruction);
				const double resist = a_attacker->AsActorValueOwner()->GetActorValue(shield.resist);
				const double damage = Mech::ShieldRetaliation(magnitude, skill, settings.castingBonus, StaminaRatio(a_attacker),
					std::clamp(resist, 0.0, 100.0), Roll0to99());
				if (damage > 0.0) {
					a_attacker->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kHealth, static_cast<float>(damage));
				}
			}
		}

		class HitSink final : public RE::BSTEventSink<RE::TESHitEvent>
		{
		public:
			static HitSink* Get()
			{
				static HitSink sink;
				return &sink;
			}
			RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>*) override
			{
				if (!a_event || !a_event->target || !a_event->cause || a_event->projectile != 0 ||
					a_event->flags.any(RE::TESHitEvent::Flag::kHitBlocked)) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* defender = a_event->target->As<RE::Actor>();
				auto* attacker = a_event->cause->As<RE::Actor>();
				if (!defender || !attacker || defender == attacker) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* source = a_event->source ? RE::TESForm::LookupByID(a_event->source) : nullptr;
				auto* weapon = source ? source->As<RE::TESObjectWEAP>() : nullptr;
				if (source && (!weapon || !weapon->IsMelee())) {
					return RE::BSEventNotifyControl::kContinue;  // spells, bows, explosions
				}
				if (!HasInstance(defender, "mw.fire_shield") && !HasInstance(defender, "mw.frost_shield") &&
					!HasInstance(defender, "mw.lightning_shield")) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto d = defender->GetHandle();
				auto a = attacker->GetHandle();
				RunOnMainThread([d, a]() {
					auto defenderPtr = d.get();
					auto attackerPtr = a.get();
					if (defenderPtr && attackerPtr && !attackerPtr->IsDead()) {
						Retaliate(defenderPtr.get(), attackerPtr.get());
					}
				});
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// ---- Blind / Chameleon / Sanctuary / Fortify Attack --------------------------------------
		// Blind lowers the subject's sight in detection through the Blindness actor value.
		// VERIFY(in-game): detection of a sneaking player by a Blinded NPC drops with M.
		void BlindStart(Instance& a_inst)
		{
			ApplyAVDeltas(a_inst.Target(), a_inst.avTarget, { { RE::ActorValue::kBlindness, a_inst.magnitude } });
		}
		void BlindResume(Instance& a_inst) { a_inst.avTarget = { { RE::ActorValue::kBlindness, a_inst.magnitude } }; }
		void AVFinish(Instance& a_inst) { RevertAVDeltas(a_inst.Target(), a_inst.avTarget); }

		// Chameleon: stealth bonus on detection (Sneak skill and movement noise) that attacking
		// doesn't break, translucency scaled by M, and 0.2 x M % evasion (miss system).
		// VERIFY(in-game): the detection weights (M/2 Sneak, -M/200 MovementNoiseMult) against
		// Morrowind's feel; Skyrim has no chance-based detection value to feed directly.
		std::vector<std::pair<RE::ActorValue, float>> ChameleonDeltas(float a_magnitude)
		{
			const float m = std::clamp(a_magnitude, 0.0f, 100.0f);
			return { { RE::ActorValue::kSneak, m * 0.5f }, { RE::ActorValue::kMovementNoiseMult, -m / 200.0f } };
		}

		void ChameleonAlpha(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return;
			}
			const double m = std::clamp(SumMagnitude(a_actor, "mw.chameleon"), 0.0, 100.0);
			a_actor->SetAlpha(static_cast<float>(1.0 - 0.75 * m / 100.0));
		}

		void ChameleonStart(Instance& a_inst)
		{
			ApplyAVDeltas(a_inst.Target(), a_inst.avTarget, ChameleonDeltas(a_inst.magnitude));
			ChameleonAlpha(a_inst.Target());
		}
		void ChameleonResume(Instance& a_inst)
		{
			a_inst.avTarget = ChameleonDeltas(a_inst.magnitude);
			ChameleonAlpha(a_inst.Target());
		}
		void ChameleonFinish(Instance& a_inst)
		{
			RevertAVDeltas(a_inst.Target(), a_inst.avTarget);
			if (auto* target = a_inst.Target()) {
				const double rest = SumMagnitude(target, "mw.chameleon") - a_inst.magnitude;
				target->SetAlpha(static_cast<float>(1.0 - 0.75 * std::clamp(rest, 0.0, 100.0) / 100.0));
			}
		}

		void Nothing(Instance&) {}

		// ---- Disintegrate -------------------------------------------------------------------
		using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
		// Morrowind damages the shield first, then the other pieces.
		constexpr std::array kArmorOrder{ Slot::kShield, Slot::kBody, Slot::kHead, Slot::kHands, Slot::kFeet };

		bool IsBoundItem(const RE::TESForm* a_item)
		{
			const auto editorId = a_item ? State::Get().forms.EditorIdOf(a_item->GetFormID()) : std::string_view{};
			return editorId.starts_with("LA_Bound_");
		}

		float& ConditionRef(RE::FormID a_actor, RE::FormID a_item)
		{
			auto& pools = State::Get().conditions;
			for (auto& entry : pools) {
				if (entry.actor == a_actor && entry.item == a_item) {
					return entry.condition;
				}
			}
			pools.push_back(ConditionEntry{ a_actor, a_item, 100.0f });
			return pools.back().condition;
		}

		void Unequip(RE::Actor* a_actor, RE::TESBoundObject* a_item, bool a_notify)
		{
			if (!a_actor || !a_item) {
				return;
			}
			const auto actor = a_actor->GetHandle();
			const auto itemId = a_item->GetFormID();
			SKSE::GetTaskInterface()->AddTask([actor, itemId, a_notify]() {
				auto  actorPtr = actor.get();
				auto* item = RE::TESForm::LookupByID<RE::TESBoundObject>(itemId);
				if (!actorPtr || !item) {
					return;
				}
				RE::ActorEquipManager::GetSingleton()->UnequipObject(actorPtr.get(), item, nullptr, 1, nullptr, false, true, true, true);
				if (a_notify && actorPtr->IsPlayerRef()) {
					Notify(FormatPattern(LocalText("$LA_Msg_ItemRuined", "{0} is ruined and needs repair."), { item->GetName() }));
				}
			});
		}

		// Damages a_item's pool by a_points; returns true when it reached 0.
		bool Wear(RE::Actor* a_actor, RE::TESBoundObject* a_item, float a_points)
		{
			std::unique_lock lock(State::Get().lock);
			auto& condition = ConditionRef(a_actor->GetFormID(), a_item->GetFormID());
			if (condition <= 0.0f) {
				return false;
			}
			condition = static_cast<float>(Mech::ConditionAfter(condition, a_points, 1.0));
			return condition <= 0.0f;
		}

		void DisintegrateArmorApply(Instance& a_inst, float a_points)
		{
			auto* target = a_inst.Target();
			if (!target || a_points <= 0.0f) {
				return;
			}
			for (auto slot : kArmorOrder) {
				auto* armor = target->GetWornArmor(slot);
				if (!armor || IsBoundItem(armor) || ConditionOf(target->GetFormID(), armor->GetFormID()) <= 0.0) {
					continue;
				}
				if (Wear(target, armor, a_points)) {
					Unequip(target, armor, true);
				}
				return;  // one piece at a time
			}
		}

		void DisintegrateWeaponApply(Instance& a_inst, float a_points)
		{
			auto* target = a_inst.Target();
			if (!target || a_points <= 0.0f) {
				return;
			}
			for (bool left : { false, true }) {
				auto* form = target->GetEquippedObject(left);
				auto* weapon = form ? form->As<RE::TESObjectWEAP>() : nullptr;
				if (!weapon || weapon->IsHandToHandMelee() || IsBoundItem(weapon) || weapon->IsBound()) {
					continue;
				}
				if (Wear(target, weapon, a_points)) {
					Unequip(target, weapon, true);  // disarmed
				}
				return;
			}
		}

		template <void (*Apply)(Instance&, float)>
		struct Ticking
		{
			static void Start(Instance& a_inst)
			{
				if (a_inst.duration <= 0.0f) {
					Apply(a_inst, a_inst.magnitude);
				}
			}
			static void Update(Instance& a_inst, float a_delta)
			{
				if (a_inst.duration > 0.0f) {
					Apply(a_inst, a_inst.magnitude * a_delta);
				}
			}
		};

		class EquipSink final : public RE::BSTEventSink<RE::TESEquipEvent>
		{
		public:
			static EquipSink* Get()
			{
				static EquipSink sink;
				return &sink;
			}
			RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>*) override
			{
				if (!a_event || !a_event->equipped || !a_event->actor) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* actor = a_event->actor->As<RE::Actor>();
				if (actor && ConditionOf(actor->GetFormID(), a_event->baseObject) <= 0.0) {
					Unequip(actor, RE::TESForm::LookupByID<RE::TESBoundObject>(a_event->baseObject), true);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			static MenuSink* Get()
			{
				static MenuSink sink;
				return &sink;
			}
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (!a_event || !a_event->opening || a_event->menuName != RE::CraftingMenu::MENU_NAME) {
					return RE::BSEventNotifyControl::kContinue;
				}
				// Repairs happen at the bench that improves the item: workbench for armor, grindstone
				// for weapons (OUTLINE catalog rows for Disintegrate). Opening the bench repairs.
				RunOnMainThread([]() {
					auto* player = Player();
					auto  furniture = player ? player->GetOccupiedFurniture().get() : RE::NiPointer<RE::TESObjectREFR>{};
					auto* base = furniture ? furniture->GetBaseObject() : nullptr;
					auto* bench = base ? base->As<RE::TESFurniture>() : nullptr;
					if (!bench) {
						return;
					}
					using Bench = RE::TESFurniture::WorkBenchData::BenchType;
					const auto type = bench->workBenchData.benchType.get();
					if (type == Bench::kSmithingArmor) {
						OnDisintegrateRepair(player, true, false);
					} else if (type == Bench::kSmithingWeapon) {
						OnDisintegrateRepair(player, false, true);
					}
				});
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// ---- melee hook -------------------------------------------------------------------------
		float GMST(const char* a_name, float a_default)
		{
			auto* settings = RE::GameSettingCollection::GetSingleton();
			auto* setting = settings ? settings->GetSetting(a_name) : nullptr;
			return setting ? setting->GetFloat() : a_default;
		}

		// Armor-rating change for the defender: pieces lost to Disintegrate (negative) and the
		// Unarmored special target (positive while no body armor is worn).
		float ArmorDelta(RE::Actor* a_defender)
		{
			float delta = 0.0f;
			for (auto slot : kArmorOrder) {
				auto* armor = a_defender->GetWornArmor(slot);
				if (!armor) {
					continue;
				}
				const double condition = ConditionOf(a_defender->GetFormID(), armor->GetFormID());
				if (condition < 100.0) {
					// VERIFY(in-game): base rating only; skill and perk multipliers are not re-applied.
					delta -= static_cast<float>(armor->armorRating / 100.0 * (1.0 - Mech::ConditionScale(condition)));
				}
			}
			if (!a_defender->GetWornArmor(Slot::kBody)) {
				ForEachOn(a_defender, [&](const Instance& a_inst) {
					if (a_inst.key->special != "Unarmored") {
						return;
					}
					delta += (a_inst.key->id == "mw.fortify_skill" ? 1.0f : -1.0f) * a_inst.magnitude;
				});
			}
			return delta;
		}

		float ArmorMultiplier(RE::Actor* a_defender)
		{
			const float delta = ArmorDelta(a_defender);
			if (delta == 0.0f) {
				return 1.0f;
			}
			const float scaling = GMST("fArmorScalingFactor", 0.12f);
			const float maxRating = GMST("fMaxArmorRating", 80.0f);
			const float rating = a_defender->AsActorValueOwner()->GetActorValue(RE::ActorValue::kDamageResist);
			auto        reduction = [&](float a_ar) { return std::clamp(a_ar * scaling, 0.0f, maxRating) / 100.0f; };
			const float before = 1.0f - reduction(rating);
			const float after = 1.0f - reduction(rating + delta);
			return before > 0.0f ? after / before : 1.0f;
		}

		bool IsEnchantedWeapon(RE::Actor* a_attacker, RE::TESObjectWEAP* a_weapon)
		{
			if (!a_weapon) {
				return false;
			}
			if (a_weapon->formEnchanting) {
				return true;
			}
			for (bool left : { false, true }) {
				auto* entry = a_attacker ? a_attacker->GetEquippedEntryData(left) : nullptr;
				if (entry && entry->object == a_weapon && entry->GetEnchantment()) {
					return true;
				}
			}
			return false;
		}

		struct ProcessHitHook
		{
			static void thunk(RE::Actor* a_victim, RE::HitData& a_hitData)
			{
				// Fast path: nothing of ours can affect this hit (the vector sizes are read without the
				// lock on purpose; a stale answer only costs one hit's worth of modifiers).
				const auto& state = State::Get();
				const bool  relevant = InstanceCount() > 0 || !state.conditions.empty() || !state.ledger.empty();
				if (!a_victim || !state.dataReady || !relevant) {
					return func(a_victim, a_hitData);
				}
				auto  attackerPtr = a_hitData.aggressor.get();
				auto* attacker = attackerPtr.get();

				const double miss = attacker ? LA::Effects::MissChance(attacker, a_victim) : 0.0;
				if (miss > 0.0 && Roll100() <= miss) {
					logger::debug("effects: {:08X} missed {:08X} ({:.0f}%)", attacker->GetFormID(), a_victim->GetFormID(), miss);
					return;  // a miss: the hit never happens
				}

				double mult = 1.0;
				if (attacker) {
					double pct = SumMagnitude(attacker, "mw.fortify_attack");
					if (a_hitData.flags.any(RE::HitData::Flag::kMeleeAttack)) {
						pct += MeleeDamagePercent(attacker);
					}
					mult *= std::max(0.0, 1.0 + pct / 100.0);
					if (a_hitData.weapon) {
						mult *= Mech::ConditionScale(ConditionOf(attacker->GetFormID(), a_hitData.weapon->GetFormID()));
					}
				}
				if (!IsEnchantedWeapon(attacker, a_hitData.weapon)) {
					const double resist = SumMagnitude(a_victim, "mwx.resist_normal_weapons") -
					                      SumMagnitude(a_victim, "mwx.weakness_to_normal_weapons");
					mult *= std::max(0.0, 1.0 - resist / 100.0);
				}
				mult *= ArmorMultiplier(a_victim);
				if (mult != 1.0) {
					a_hitData.totalDamage = static_cast<float>(a_hitData.totalDamage * mult);
					a_hitData.physicalDamage = static_cast<float>(a_hitData.physicalDamage * mult);
				}
				func(a_victim, a_hitData);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		SKSE::Trampoline& Trampoline()
		{
			// Never destroyed: the patched call sites keep jumping through it until the process exits.
			static auto* trampoline = new SKSE::Trampoline(std::string_view{ "LostArt.Combat" });
			return *trampoline;
		}

		bool g_hitHook{ false };
	}

	double ConditionOf(RE::FormID a_actor, RE::FormID a_item)
	{
		std::shared_lock lock(State::Get().lock);
		for (const auto& entry : State::Get().conditions) {
			if (entry.actor == a_actor && entry.item == a_item) {
				return entry.condition;
			}
		}
		return 100.0;
	}

	void OnDisintegrateRepair(RE::Actor* a_actor, bool a_armor, bool a_weapons)
	{
		if (!a_actor) {
			return;
		}
		int repaired = 0;
		{
			std::unique_lock lock(State::Get().lock);
			std::erase_if(State::Get().conditions, [&](const ConditionEntry& e) {
				if (e.actor != a_actor->GetFormID()) {
					return false;
				}
				auto* item = RE::TESForm::LookupByID(e.item);
				const bool isArmor = item && item->Is(RE::FormType::Armor);
				const bool isWeapon = item && item->Is(RE::FormType::Weapon);
				if ((a_armor && isArmor) || (a_weapons && isWeapon) || !item) {
					++repaired;
					return true;
				}
				return false;
			});
		}
		if (repaired > 0 && a_actor->IsPlayerRef()) {
			Notify(a_armor ? LocalText("$LA_Msg_ArmorRepaired", "Your armor has been repaired.") :
			                 LocalText("$LA_Msg_WeaponsRepaired", "Your weapons have been repaired."));
		}
	}

	void InstallCombat()
	{
		const Handler shield{ ShieldStart, ShieldResume, ShieldFinish, nullptr };
		for (const auto& s : kShields) {
			Register(s.id, shield);
		}
		Register("mw.blind", { BlindStart, BlindResume, AVFinish, nullptr });
		Register("mw.chameleon", { ChameleonStart, ChameleonResume, ChameleonFinish, nullptr });
		Register("mw.sanctuary", { Nothing, nullptr, nullptr, nullptr });      // read by MissChance
		Register("mw.fortify_attack", { Nothing, nullptr, nullptr, nullptr }); // read by the hit hook
		Register("mwx.resist_normal_weapons", { Nothing, nullptr, nullptr, nullptr });
		Register("mwx.weakness_to_normal_weapons", { Nothing, nullptr, nullptr, nullptr });
		Register("mw.disintegrate_armor",
			{ Ticking<DisintegrateArmorApply>::Start, nullptr, nullptr, Ticking<DisintegrateArmorApply>::Update });
		Register("mw.disintegrate_weapon",
			{ Ticking<DisintegrateWeaponApply>::Start, nullptr, nullptr, Ticking<DisintegrateWeaponApply>::Update });
		for (auto id : { "mw.fortify_skill", "mw.drain_skill", "mwx.damage_skill", "mwx.restore_skill", "mwx.absorb_skill" }) {
			Register(std::string(id) + "#Unarmored", { Nothing, nullptr, nullptr, nullptr });  // read by ArmorDelta
		}

		if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
			holder->AddEventSink<RE::TESHitEvent>(HitSink::Get());
			holder->AddEventSink<RE::TESEquipEvent>(EquipSink::Get());
		}
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::Get());
		}

		Trampoline().create(32);
		REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(37673, 38627), REL::VariantOffset(0x3C0, 0x4A8, 0) };
		if (*reinterpret_cast<const std::uint8_t*>(target.address()) == 0xE8) {
			ProcessHitHook::func = Trampoline().write_call<5>(target.address(), ProcessHitHook::thunk);
			g_hitHook = true;
			logger::info("effects: melee hit hook installed (miss system, damage modifiers)");
		} else {
			logger::warn("effects: melee hit hook site is not a call; miss system and melee modifiers are off");
		}
	}
}

namespace LA::Effects
{
	double MissChance(RE::Actor* a_attacker, RE::Actor* a_defender)
	{
		if (!a_attacker || !a_defender) {
			return 0.0;
		}
		Mech::MissInputs in;
		in.attackerBlind = Internal::SumMagnitude(a_attacker, "mw.blind");
		in.attackerFortifyAttack = Internal::SumMagnitude(a_attacker, "mw.fortify_attack");
		in.defenderSanctuary = Internal::SumMagnitude(a_defender, "mw.sanctuary");
		in.defenderChameleon = Internal::SumMagnitude(a_defender, "mw.chameleon");
		in.defenderEvasion = Internal::EvasionPercent(a_defender);
		return Mech::MissChance(in);
	}
}
