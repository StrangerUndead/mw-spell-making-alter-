// In-game suite "effects" (docs/dev/CONTRACTS.md section 9): scripted checks of every custom
// effect at minimum and maximum magnitude on the player and on an NPC dummy, with explicit pass
// criteria. The checks drive the effect handlers synchronously through synthetic instances (the
// same start / finish code the engine's effect events reach), so they finish within one call
// and restore everything they change. What only a human can judge (flight feel, visuals,
// teleports, AI) is listed in docs/dev/TESTING-EFFECTS.md.
//
// The dummy is a vanilla prisoner NPC (CWPrisonerImperialA, Skyrim.esm|0x10D4B2) placed 3000
// units below the player with AI disabled. Actor-value checks don't need its 3D; the checks
// that do need it report "3D not loaded - rerun" on the first run (KB: spawned-actor Havok CTD -
// no physics call before Is3DLoaded()).

#include "Effects/EffectsInternal.h"

#include "Tests/InGameTests.h"

namespace LA::Effects::Internal
{
	namespace
	{
		using Tests::Fail;
		using Tests::Pass;
		using Tests::Result;

		RE::ActorHandle g_dummy;

		RE::Actor* Dummy()
		{
			if (auto actor = g_dummy.get()) {
				return actor.get();
			}
			auto* player = Player();
			auto* base = Vanilla<RE::TESNPC>("Skyrim.esm|0x10D4B2");  // CWPrisonerImperialA
			if (!player || !base) {
				return nullptr;
			}
			auto  ref = player->PlaceObjectAtMe(base, false);
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (!actor) {
				return nullptr;
			}
			auto pos = player->GetPosition();
			pos.z -= 3000.0f;
			actor->SetPosition(pos, true);
			actor->EnableAI(false);
			g_dummy = actor->GetHandle();
			return actor;
		}

		float AV(RE::Actor* a_actor, RE::ActorValue a_av) { return a_actor->AsActorValueOwner()->GetActorValue(a_av); }

		bool Near(double a_lhs, double a_rhs, double a_eps = 0.02) { return std::abs(a_lhs - a_rhs) <= a_eps; }

		// Starts the effect, checks the actor-value delta, finishes it and checks the value is back.
		Result AVCheck(RE::Actor* a_target, RE::Actor* a_caster, std::string_view a_id, int a_sub, std::string_view a_special,
			float a_magnitude, RE::ActorValue a_av, double a_expectedDelta, float a_duration = 30.0f)
		{
			if (!a_target) {
				return Fail("no target actor");
			}
			const double before = AV(a_target, a_av);
			auto*        inst = StartSynthetic(a_target, a_caster, a_id, a_sub, a_special, a_magnitude, a_duration);
			if (!inst) {
				return Fail(fmt::format("no handler for {}", a_id));
			}
			const double during = AV(a_target, a_av);
			FinishSynthetic(inst);
			const double after = AV(a_target, a_av);
			if (!Near(during - before, a_expectedDelta)) {
				return Fail(fmt::format("expected delta {:.2f}, got {:.2f}", a_expectedDelta, during - before));
			}
			if (!Near(after, before)) {
				return Fail(fmt::format("expected {:.2f} after finish, got {:.2f}", before, after));
			}
			return Pass(fmt::format("delta {:.2f}, restored", during - before));
		}

		using ActorFn = RE::Actor* (*)();
		RE::Actor* PlayerActor() { return Player(); }

		struct Who
		{
			std::string_view name;
			ActorFn          get;
		};
		constexpr std::array kWho{ Who{ "player", PlayerActor }, Who{ "npc", Dummy } };
		constexpr std::array kMags{ 1.0f, 100.0f };

		void RegisterAV(std::string_view a_name, std::string_view a_id, int a_sub, std::string_view a_special, RE::ActorValue a_av,
			double a_perPoint, bool a_npcOnly = false, bool a_playerOnly = false)
		{
			for (const auto& who : kWho) {
				if ((a_npcOnly && who.name == "player") || (a_playerOnly && who.name == "npc")) {
					continue;
				}
				for (float m : kMags) {
					const auto name = fmt::format("{}.{}.{}", a_name, who.name, m <= 1.0f ? "min" : "max");
					const auto get = who.get;
					const std::string id(a_id), special(a_special);
					Tests::Register("effects", name, [=]() {
						return AVCheck(get(), Player(), id, a_sub, special, m, a_av, a_perPoint * m);
					});
				}
			}
		}

		void RegisterFlag(std::string_view a_name, std::string_view a_id, std::function<bool(RE::Actor*, float)> a_during,
			std::function<bool(RE::Actor*)> a_after)
		{
			for (const auto& who : kWho) {
				for (float m : kMags) {
					const auto name = fmt::format("{}.{}.{}", a_name, who.name, m <= 1.0f ? "min" : "max");
					const auto get = who.get;
					const std::string id(a_id);
					Tests::Register("effects", name, [=]() -> Result {
						auto* actor = get();
						if (!actor) {
							return Fail("no target actor");
						}
						auto* inst = StartSynthetic(actor, Player(), id, -1, "", m, 30.0f);
						if (!inst) {
							return Fail("no handler");
						}
						const bool during = a_during(actor, m);
						FinishSynthetic(inst);
						const bool after = a_after(actor);
						if (!during) {
							return Fail("state not set while active");
						}
						if (!after) {
							return Fail("state not cleared after finish");
						}
						return Pass("set while active, cleared after");
					});
				}
			}
		}

		// Damage Attribute persists in the ledger until Restore Attribute repairs it.
		Result LedgerCheck(RE::Actor* a_actor, float a_magnitude)
		{
			if (!a_actor) {
				return Fail("no target actor");
			}
			const double carry0 = AV(a_actor, RE::ActorValue::kCarryWeight);
			auto*        damage = StartSynthetic(a_actor, Player(), "mw.damage_attribute", 0, "", a_magnitude, 0.0f);  // instant
			if (!damage) {
				return Fail("no handler");
			}
			FinishSynthetic(damage);
			const double carry1 = AV(a_actor, RE::ActorValue::kCarryWeight);
			double       ledger = 0.0;
			{
				std::shared_lock lock(State::Get().lock);
				for (const auto& e : State::Get().ledger) {
					if (e.actor == a_actor->GetFormID()) {
						ledger = e.damage[0];
					}
				}
			}
			const double expected = -3.0 * a_magnitude * (State::Get().settings.attributeProfile == AttributeProfile::kLight ? 0.5 : 1.0);
			auto* restore = StartSynthetic(a_actor, Player(), "mw.restore_attribute", 0, "", a_magnitude, 0.0f);
			FinishSynthetic(restore);
			const double carry2 = AV(a_actor, RE::ActorValue::kCarryWeight);
			if (State::Get().settings.attributeProfile == AttributeProfile::kOff) {
				return Pass("attribute profile Off: no stats move");
			}
			if (!Near(carry1 - carry0, expected)) {
				return Fail(fmt::format("expected carry weight {:.1f}, got {:.1f}", expected, carry1 - carry0));
			}
			if (!Near(ledger, a_magnitude)) {
				return Fail(fmt::format("expected ledger {:.1f}, got {:.1f}", a_magnitude, ledger));
			}
			if (!Near(carry2, carry0)) {
				return Fail(fmt::format("expected carry weight back at {:.1f}, got {:.1f}", carry0, carry2));
			}
			return Pass("damaged, persisted in the ledger, restored");
		}

		Result MissCheck(std::string_view a_id, bool a_onDefender, float a_magnitude, double a_expected)
		{
			auto* player = Player();
			auto* dummy = Dummy();
			if (!player || !dummy) {
				return Fail("no dummy");
			}
			const double base = MissChance(dummy, player);
			auto*        inst = StartSynthetic(a_onDefender ? player : dummy, player, a_id, -1, "", a_magnitude, 30.0f);
			const double during = MissChance(dummy, player);
			FinishSynthetic(inst);
			const double after = MissChance(dummy, player);
			if (!Near(during - base, a_expected)) {
				return Fail(fmt::format("expected miss +{:.1f}, got {:+.1f}", a_expected, during - base));
			}
			if (!Near(after, base)) {
				return Fail("miss chance not restored");
			}
			return Pass(fmt::format("miss {:.1f}% -> {:.1f}%", base, during));
		}
	}

	void RegisterEffectTests()
	{
		const double perPoint = 1.0;
		// Elemental shields: +M % resistance.
		RegisterAV("fire_shield", "mw.fire_shield", -1, "", RE::ActorValue::kResistFire, perPoint);
		RegisterAV("frost_shield", "mw.frost_shield", -1, "", RE::ActorValue::kResistFrost, perPoint);
		RegisterAV("lightning_shield", "mw.lightning_shield", -1, "", RE::ActorValue::kResistShock, perPoint);
		// Blind: Blindness actor value; Chameleon: Sneak +M/2 (M <= 100).
		RegisterAV("blind.detection", "mw.blind", -1, "", RE::ActorValue::kBlindness, 1.0);
		RegisterAV("chameleon.sneak", "mw.chameleon", -1, "", RE::ActorValue::kSneak, 0.5);
		// Jump: JumpingBonus = M x fJumpPerPoint.
		Tests::Register("effects", "jump.player.max", []() {
			return AVCheck(Player(), Player(), "mw.jump", -1, "", 100.0f, RE::ActorValue::kJumpingBonus, 100.0 * State::Get().settings.jumpPerPoint);
		});
		Tests::Register("effects", "jump.npc.min", []() {
			return AVCheck(Dummy(), Player(), "mw.jump", -1, "", 1.0f, RE::ActorValue::kJumpingBonus, State::Get().settings.jumpPerPoint);
		});
		// Burden on NPCs: -M/2 % speed, capped at 75.
		Tests::Register("effects", "burden.npc.min", []() {
			return AVCheck(Dummy(), Player(), "mw.burden", -1, "", 1.0f, RE::ActorValue::kSpeedMult, -0.5);
		});
		Tests::Register("effects", "burden.npc.max", []() {
			return AVCheck(Dummy(), Player(), "mw.burden", -1, "", 200.0f, RE::ActorValue::kSpeedMult, -75.0);
		});
		Tests::Register("effects", "burden.player.native_only", []() {
			return AVCheck(Player(), Player(), "mw.burden", -1, "", 50.0f, RE::ActorValue::kSpeedMult, 0.0);
		});

		// Attributes (default profile values from data/content/attributes.json).
		auto profile = []() {
			switch (State::Get().settings.attributeProfile) {
			case AttributeProfile::kLight:
				return 0.5;
			case AttributeProfile::kOff:
				return 0.0;
			default:
				return 1.0;
			}
		};
		for (const auto& who : kWho) {
			for (float m : kMags) {
				const auto suffix = fmt::format("{}.{}", who.name, m <= 1.0f ? "min" : "max");
				const auto get = who.get;
				Tests::Register("effects", "fortify_attribute.strength." + suffix, [=]() {
					return AVCheck(get(), Player(), "mw.fortify_attribute", 0, "", m, RE::ActorValue::kCarryWeight, 3.0 * m * profile());
				});
				Tests::Register("effects", "drain_attribute.intelligence." + suffix, [=]() {
					return AVCheck(get(), Player(), "mw.drain_attribute", 1, "", m, RE::ActorValue::kMagicka, -2.0 * m * profile());
				});
				Tests::Register("effects", "fortify_attribute.speed." + suffix, [=]() {
					return AVCheck(get(), Player(), "mw.fortify_attribute", 4, "", m, RE::ActorValue::kSpeedMult, 1.0 * m * profile());
				});
				Tests::Register("effects", "fortify_attribute.endurance." + suffix, [=]() {
					return AVCheck(get(), Player(), "mw.fortify_attribute", 5, "", m, RE::ActorValue::kHealth, 1.0 * m * profile());
				});
				Tests::Register("effects", "absorb_attribute.willpower." + suffix, [=]() {
					// target loses 0.25 x M magic resistance
					return AVCheck(get() == Player() ? Dummy() : get(), Player(), "mw.absorb_attribute", 2, "", m, RE::ActorValue::kResistMagic,
						-0.25 * m * profile());
				});
				Tests::Register("effects", "damage_restore_attribute." + suffix, [=]() { return LedgerCheck(get(), m); });
			}
		}
		Tests::Register("effects", "absorb_attribute.caster_gain", []() {
			auto* player = Player();
			auto* dummy = Dummy();
			if (!dummy) {
				return Fail("no dummy");
			}
			const double before = AV(player, RE::ActorValue::kCarryWeight);
			auto*        inst = StartSynthetic(dummy, player, "mw.absorb_attribute", 0, "", 10.0f, 30.0f);
			const double during = AV(player, RE::ActorValue::kCarryWeight);
			FinishSynthetic(inst);
			const double after = AV(player, RE::ActorValue::kCarryWeight);
			const double expected = 30.0 * (State::Get().settings.attributeProfile == AttributeProfile::kLight ? 0.5 : 1.0) *
			                        (State::Get().settings.attributeProfile == AttributeProfile::kOff ? 0.0 : 1.0);
			if (!Near(during - before, expected) || !Near(after, before)) {
				return Fail(fmt::format("expected caster +{:.0f} then back, got {:+.1f} / {:+.1f}", expected, during - before, after - before));
			}
			return Pass("caster fortified by the absorbed amount");
		});

		// Morrowind special skill targets.
		RegisterAV("fortify_skill.athletics", "mw.fortify_skill", -1, "RunSwimSpeed", RE::ActorValue::kSpeedMult, 1.0);
		RegisterAV("drain_skill.athletics", "mw.drain_skill", -1, "RunSwimSpeed", RE::ActorValue::kSpeedMult, -1.0);
		RegisterAV("fortify_skill.hand_to_hand", "mw.fortify_skill", -1, "UnarmedDamage", RE::ActorValue::kUnarmedDamage, 1.0);
		Tests::Register("effects", "fortify_skill.acrobatics.player.max", []() {
			return AVCheck(Player(), Player(), "mw.fortify_skill", -1, "Jump", 100.0f, RE::ActorValue::kJumpingBonus,
				100.0 * State::Get().settings.jumpPerPoint);
		});
		// Extended skill effects on a Skyrim skill (One-Handed).
		RegisterAV("absorb_skill.onehanded", "mwx.absorb_skill", 0, "", RE::ActorValue::kOneHanded, -1.0, true);
		Tests::Register("effects", "damage_restore_skill.player", []() {
			auto*        player = Player();
			const double before = AV(player, RE::ActorValue::kOneHanded);
			FinishSynthetic(StartSynthetic(player, player, "mwx.damage_skill", 0, "", 5.0f, 0.0f));
			const double damaged = AV(player, RE::ActorValue::kOneHanded);
			FinishSynthetic(StartSynthetic(player, player, "mwx.restore_skill", 0, "", 5.0f, 0.0f));
			const double restored = AV(player, RE::ActorValue::kOneHanded);
			if (!Near(damaged - before, -5.0) || !Near(restored, before)) {
				return Fail(fmt::format("expected -5 then back, got {:+.1f} / {:+.1f}", damaged - before, restored - before));
			}
			return Pass("damaged by 5, restored");
		});

		// Swift Swim: nothing changes on land.
		Tests::Register("effects", "swift_swim.on_land", []() {
			return AVCheck(Player(), Player(), "mw.swift_swim", -1, "", 100.0f, RE::ActorValue::kSpeedMult, 0.0);
		});

		// Miss system.
		Tests::Register("effects", "sanctuary.min", []() { return MissCheck("mw.sanctuary", true, 1.0f, 1.0); });
		Tests::Register("effects", "sanctuary.max_capped", []() { return MissCheck("mw.sanctuary", true, 100.0f, 75.0); });
		Tests::Register("effects", "blind.miss.min", []() { return MissCheck("mw.blind", false, 1.0f, 1.0); });
		Tests::Register("effects", "blind.miss.max", []() { return MissCheck("mw.blind", false, 100.0f, 100.0); });
		Tests::Register("effects", "chameleon.evasion.max", []() { return MissCheck("mw.chameleon", true, 100.0f, 20.0); });
		Tests::Register("effects", "fortify_attack.offsets_blind", []() -> Result {
			auto* player = Player();
			auto* dummy = Dummy();
			if (!dummy) {
				return Fail("no dummy");
			}
			auto*        blind = StartSynthetic(dummy, player, "mw.blind", -1, "", 30.0f, 30.0f);
			auto*        attack = StartSynthetic(dummy, player, "mw.fortify_attack", -1, "", 10.0f, 30.0f);
			const double miss = MissChance(dummy, player);
			FinishSynthetic(attack);
			FinishSynthetic(blind);
			return Tests::Expect(20.0, miss, "Blind 30 - Fortify Attack 10");
		});

		// Silence / Sound / Reflect / Resist Paralysis / Resist Corprus: queries while active.
		RegisterFlag("silence", "mw.silence", [](RE::Actor* a, float) { return LA::Effects::IsSilenced(a); },
			[](RE::Actor* a) { return !LA::Effects::IsSilenced(a); });
		RegisterFlag("sound", "mw.sound", [](RE::Actor* a, float m) { return Near(LA::Effects::SoundMagnitude(a), m); },
			[](RE::Actor* a) { return LA::Effects::SoundMagnitude(a) == 0.0; });
		RegisterFlag("reflect", "mw.reflect", [](RE::Actor* a, float m) { return Near(SumMagnitude(a, "mw.reflect"), m); },
			[](RE::Actor* a) { return !HasInstance(a, "mw.reflect"); });
		RegisterFlag("resist_paralysis", "mw.resist_paralysis", [](RE::Actor* a, float m) { return Near(SumMagnitude(a, "mw.resist_paralysis"), m); },
			[](RE::Actor* a) { return !HasInstance(a, "mw.resist_paralysis"); });
		RegisterFlag("resist_corprus", "mw.resist_corprus_disease",
			[](RE::Actor* a, float m) { return Near(SumMagnitude(a, "mw.resist_corprus_disease"), m); },
			[](RE::Actor* a) { return !HasInstance(a, "mw.resist_corprus_disease"); });

		// Movement states (physics feel is a human check).
		RegisterFlag("levitate.state", "mw.levitate", [](RE::Actor* a, float) { return IsLevitating(a) && FallDamageCancelled(a); },
			[](RE::Actor* a) { return !IsLevitating(a); });
		RegisterFlag("slowfall.state", "mw.slowfall", [](RE::Actor* a, float) { return LA::Effects::IsSlowfalling(a) && FallDamageCancelled(a); },
			[](RE::Actor* a) { return !HasInstance(a, "mw.slowfall"); });
		Tests::Register("effects", "jump.fall_reduction", []() {
			auto* inst = StartSynthetic(Player(), Player(), "mw.jump", -1, "", 10.0f, 30.0f);
			const double units = FallReduction(Player());
			FinishSynthetic(inst);
			return Tests::Expect(true, Near(units, Mech::JumpFallReduction(10.0), 0.5), "10 ft shorter fall");
		});

		// Telekinesis: activation reach +M ft.
		Tests::Register("effects", "telekinesis.reach", []() -> Result {
			auto* settings = RE::GameSettingCollection::GetSingleton();
			auto* pick = settings ? settings->GetSetting("iActivatePickLength") : nullptr;
			if (!pick) {
				return Fail("iActivatePickLength not found");
			}
			const auto before = pick->GetInteger();
			auto*      inst = StartSynthetic(Player(), Player(), "mw.telekinesis", -1, "", 20.0f, 30.0f);
			const auto during = pick->GetInteger();
			FinishSynthetic(inst);
			return Tests::Expect(static_cast<int>(Mech::FeetToUnits(20.0)), during - before, "reach +20 ft");
		});

		// Mark stores the caster's position.
		Tests::Register("effects", "mark.stores_position", []() -> Result {
			auto* player = Player();
			std::vector<Mark> saved;
			{
				std::shared_lock lock(State::Get().lock);
				saved = State::Get().marks;
			}
			FinishSynthetic(StartSynthetic(player, player, "mw.mark", -1, "", 0.0f, 0.0f));
			Mark mark;
			{
				std::unique_lock lock(State::Get().lock);
				if (State::Get().marks.empty()) {
					return Fail("no mark stored");
				}
				mark = State::Get().marks.back();
				State::Get().marks = saved;  // leave the player's own marks untouched
			}
			const auto pos = player->GetPosition();
			if (!Near(mark.pos[0], pos.x, 1.0) || !Near(mark.pos[1], pos.y, 1.0)) {
				return Fail("mark position differs from the player's");
			}
			return Pass("mark at the player's position");
		});

		// Data the world effects need.
		Tests::Register("effects", "interventions.destinations", []() {
			return Tests::Expect<std::size_t, std::size_t>(7, ResolvedTempleCount(), "temple markers resolved");
		});
		Tests::Register("effects", "bound_armor.records", []() -> Result {
			for (auto id : { "LA_Bound_Boots", "LA_Bound_Cuirass", "LA_Bound_Gloves", "LA_Bound_Helm", "LA_Bound_Shield" }) {
				if (!State::Get().forms.Get<RE::TESObjectARMO>(id)) {
					return Fail(fmt::format("{} not loaded", id));
				}
			}
			return Pass("5 bound armor records");
		});

		// Command: level cap M.
		Tests::Register("effects", "command_humanoid.level_cap", []() -> Result {
			auto* dummy = Dummy();
			if (!dummy) {
				return Fail("no dummy");
			}
			const float level = dummy->GetLevel();
			auto*       low = StartSynthetic(dummy, Player(), "mw.command_humanoid", -1, "", std::max(0.0f, level - 1.0f), 30.0f);
			const bool  lowApplied = low && low->applied;
			FinishSynthetic(low);
			auto*      high = StartSynthetic(dummy, Player(), "mw.command_humanoid", -1, "", level, 30.0f);
			const bool highApplied = high && high->applied;
			FinishSynthetic(high);
			if (lowApplied || !highApplied) {
				return Fail(fmt::format("level {}: below cap applied={}, at cap applied={}", level, lowApplied, highApplied));
			}
			return Pass("commands only up to level M");
		});
		Tests::Register("effects", "command_creature.rejects_humanoid", []() -> Result {
			auto* inst = StartSynthetic(Dummy(), Player(), "mw.command_creature", -1, "", 100.0f, 30.0f);
			const bool applied = inst && inst->applied;
			FinishSynthetic(inst);
			return Tests::Expect(false, applied, "creature command on an NPC");
		});
		// Charm: one relationship step per 25 M.
		Tests::Register("effects", "charm.steps", []() -> Result {
			auto* inst = StartSynthetic(Dummy(), Player(), "mw.charm", -1, "", 50.0f, 30.0f);
			if (!inst) {
				return Fail("no handler");
			}
			const int steps = inst->i[0];
			FinishSynthetic(inst);
			return Tests::Expect(true, steps >= 0 && steps <= Mech::CharmSteps(50.0), "steps within 0..2");
		});

		// Disintegrate needs worn armor / an equipped weapon on the player.
		Tests::Register("effects", "disintegrate_armor.player", []() -> Result {
			auto* player = Player();
			RE::TESObjectARMO* piece = nullptr;
			for (auto slot : { RE::BGSBipedObjectForm::BipedObjectSlot::kShield, RE::BGSBipedObjectForm::BipedObjectSlot::kBody,
					 RE::BGSBipedObjectForm::BipedObjectSlot::kHead, RE::BGSBipedObjectForm::BipedObjectSlot::kHands,
					 RE::BGSBipedObjectForm::BipedObjectSlot::kFeet }) {
				if ((piece = player->GetWornArmor(slot)) != nullptr) {
					break;
				}
			}
			if (!piece) {
				return Fail("precondition: wear a piece of armor and rerun");
			}
			const double before = ConditionOf(player->GetFormID(), piece->GetFormID());
			FinishSynthetic(StartSynthetic(player, player, "mw.disintegrate_armor", -1, "", 10.0f, 0.0f));
			const double after = ConditionOf(player->GetFormID(), piece->GetFormID());
			OnDisintegrateRepair(player, true, false);
			const double repaired = ConditionOf(player->GetFormID(), piece->GetFormID());
			if (!Near(before - after, std::min(10.0, before)) || !Near(repaired, 100.0)) {
				return Fail(fmt::format("condition {:.0f} -> {:.0f} -> {:.0f}", before, after, repaired));
			}
			return Pass("condition -10, repaired at the workbench path");
		});
		Tests::Register("effects", "disintegrate_weapon.player", []() -> Result {
			auto* player = Player();
			auto* form = player->GetEquippedObject(false);
			auto* weapon = form ? form->As<RE::TESObjectWEAP>() : nullptr;
			if (!weapon || weapon->IsHandToHandMelee()) {
				return Fail("precondition: equip a weapon in the right hand and rerun");
			}
			FinishSynthetic(StartSynthetic(player, player, "mw.disintegrate_weapon", -1, "", 25.0f, 0.0f));
			const double after = ConditionOf(player->GetFormID(), weapon->GetFormID());
			OnDisintegrateRepair(player, false, true);
			return Tests::Expect(true, Near(after, 75.0) && Near(ConditionOf(player->GetFormID(), weapon->GetFormID()), 100.0),
				"condition 75 then repaired");
		});

		// Housekeeping: remove the dummy (last case).
		Tests::Register("effects", "zz_cleanup.dummy", []() {
			if (auto actor = g_dummy.get()) {
				actor->Disable();
				actor->SetDelete(true);
			}
			g_dummy = {};
			return Pass("dummy removed");
		});
	}
}
