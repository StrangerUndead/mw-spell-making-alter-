// Attribute translation layer (OUTLINE "Attributes & skills"), the persistent Damage ledger, the
// Extended skill effects and the Morrowind-skill special targets that are plain actor values
// (Athletics -> movement speed, Hand-to-Hand -> unarmed damage). Jump (Acrobatics) lives in
// Movement.cpp and Unarmored in Combat.cpp; they read the same instances.
//
// Every change goes through the Temporary ("magic") actor-value modifier, the modifier vanilla
// Fortify/Drain value-modifier effects use, so it is saved with the actor and undone exactly by
// finish(). Evasion and melee damage are not actor values: the miss system and the melee hook
// (Combat.cpp) read them from the live instances and the ledger instead.

#include "Effects/EffectsInternal.h"

namespace LA::Effects::Internal
{
	namespace
	{
		struct StatAV
		{
			std::string_view stat;
			RE::ActorValue   av;
			float            scale;  // value per stat unit
		};

		// Stat vocabulary of data/content/attributes.json (docs/data-notes.md 1.10).
		constexpr std::array kStatAVs{
			StatAV{ "CarryWeight", RE::ActorValue::kCarryWeight, 1.0f },
			StatAV{ "Magicka", RE::ActorValue::kMagicka, 1.0f },
			StatAV{ "MagickaRatePercent", RE::ActorValue::kMagickaRateMult, 1.0f },  // base 100 = 100 % (VERIFY: Fortify Magicka Rate enchantments use it)
			StatAV{ "MagicResist", RE::ActorValue::kResistMagic, 1.0f },
			StatAV{ "AttackSpeedPercent", RE::ActorValue::kWeaponSpeedMult, 0.01f },  // multiplier, 1.0 = normal
			StatAV{ "SpeedMult", RE::ActorValue::kSpeedMult, 1.0f },
			StatAV{ "Health", RE::ActorValue::kHealth, 1.0f },
			StatAV{ "StaminaRatePercent", RE::ActorValue::kStaminaRateMult, 1.0f },
			StatAV{ "Speech", RE::ActorValue::kSpeech, 1.0f },
			StatAV{ "PricePercent", RE::ActorValue::kSpeechcraftModifier, 1.0f },  // the Fortify Barter AV
			StatAV{ "PickpocketPercent", RE::ActorValue::kPickpocketModifier, 1.0f },
		};

		const AttributeDef* Attribute(int a_index)
		{
			for (const auto& attr : State::Get().catalog.Attributes()) {
				if (attr.index == a_index) {
					return &attr;
				}
			}
			return nullptr;
		}

		double NonAVStat(int a_attribute, double a_points, std::string_view a_stat)
		{
			const auto* attr = Attribute(a_attribute);
			if (!attr) {
				return 0.0;
			}
			double sum = 0.0;
			for (const auto& d : Mech::AttributeStats(*attr, a_points, State::Get().settings.attributeProfile)) {
				if (d.stat == a_stat) {
					sum += d.value;
				}
			}
			return sum;
		}

		void EnsureWeaponSpeedBase(RE::Actor* a_actor)
		{
			// WeaponSpeedMult defaults to 0, which the engine reads as 1.0; a modifier on top of a
			// 0 base would slow attacks to a crawl, so give the actor an explicit 1.0 base first.
			// VERIFY(in-game): attack speed with Fortify Agility; compare with Skyrim's own
			// WeaponSpeedMult effects (Elemental Fury uses it too).
			auto* owner = a_actor->AsActorValueOwner();
			if (owner->GetBaseActorValue(RE::ActorValue::kWeaponSpeedMult) == 0.0f) {
				owner->SetBaseActorValue(RE::ActorValue::kWeaponSpeedMult, 1.0f);
			}
		}

		// ---- ledger (State::ledger, persisted by Core/Persistence) --------------------------
		LedgerEntry* LedgerFor(RE::FormID a_actor, bool a_create)
		{
			auto& ledger = State::Get().ledger;
			for (auto& entry : ledger) {
				if (entry.actor == a_actor) {
					return &entry;
				}
			}
			if (!a_create) {
				return nullptr;
			}
			ledger.push_back(LedgerEntry{ a_actor, {} });
			return &ledger.back();
		}

		constexpr float kLedgerCap = 100.0f;  // an attribute cannot lose more than Morrowind's 100 points

		// Applies (positive a_points) or removes (negative) attribute points on the actor's
		// actor values. Used by the ledger, which keeps no per-instance list.
		void ApplyPoints(RE::Actor* a_actor, int a_attribute, float a_points)
		{
			std::vector<std::pair<RE::ActorValue, float>> scratch;
			ApplyAVDeltas(a_actor, scratch, AttributeAVDeltas(a_attribute, a_points));
		}

		// ---- attribute handlers --------------------------------------------------------------
		int SignOf(std::string_view a_id)
		{
			if (a_id == "mw.fortify_attribute") {
				return 1;
			}
			return -1;  // drain, absorb (target side)
		}

		void HeldStart(Instance& a_inst)
		{
			auto* target = a_inst.Target();
			if (!target || a_inst.key->attribute < 0) {
				return;
			}
			const float points = SignOf(a_inst.key->id) * a_inst.magnitude;
			if (std::ranges::any_of(AttributeAVDeltas(a_inst.key->attribute, points),
					[](const auto& p) { return p.first == RE::ActorValue::kWeaponSpeedMult; })) {
				EnsureWeaponSpeedBase(target);
			}
			ApplyAVDeltas(target, a_inst.avTarget, AttributeAVDeltas(a_inst.key->attribute, points));
			if (a_inst.key->id == "mw.absorb_attribute") {
				if (auto* caster = a_inst.Caster(); caster && caster != target) {
					ApplyAVDeltas(caster, a_inst.avCaster, AttributeAVDeltas(a_inst.key->attribute, a_inst.magnitude));
				}
			}
		}

		void HeldResume(Instance& a_inst)
		{
			// The modifiers are in the save; only rebuild the undo lists.
			if (a_inst.key->attribute < 0) {
				return;
			}
			const float points = SignOf(a_inst.key->id) * a_inst.magnitude;
			a_inst.avTarget = AttributeAVDeltas(a_inst.key->attribute, points);
			if (a_inst.key->id == "mw.absorb_attribute" && a_inst.casterId != a_inst.targetId) {
				a_inst.avCaster = AttributeAVDeltas(a_inst.key->attribute, a_inst.magnitude);
			}
		}

		void HeldFinish(Instance& a_inst)
		{
			RevertAVDeltas(a_inst.Target(), a_inst.avTarget);
			if (!a_inst.avCaster.empty()) {
				auto* caster = a_inst.Caster();
				if (!caster && a_inst.casterId) {
					caster = RE::TESForm::LookupByID<RE::Actor>(a_inst.casterId);
				}
				RevertAVDeltas(caster, a_inst.avCaster);
			}
		}

		// Damage Attribute: M points per second into the ledger (a 1-second effect is one hit).
		void DamageApply(Instance& a_inst, float a_points)
		{
			auto* target = a_inst.Target();
			if (!target || a_inst.key->attribute < 0 || a_points <= 0.0f) {
				return;
			}
			float applied = 0.0f;
			{
				std::unique_lock lock(State::Get().lock);
				auto*       entry = LedgerFor(target->GetFormID(), true);
				auto&       damage = entry->damage[a_inst.key->attribute];
				applied = std::clamp(kLedgerCap - damage, 0.0f, a_points);
				damage += applied;
			}
			if (applied > 0.0f) {
				ApplyPoints(target, a_inst.key->attribute, -applied);
			}
		}

		void DamageStart(Instance& a_inst)
		{
			if (a_inst.duration <= 0.0f) {
				DamageApply(a_inst, a_inst.magnitude);
			}
		}

		void DamageUpdate(Instance& a_inst, float a_delta)
		{
			if (a_inst.duration <= 0.0f) {
				return;
			}
			DamageApply(a_inst, a_inst.magnitude * a_delta);
		}

		void RestoreApply(Instance& a_inst, float a_points)
		{
			auto* target = a_inst.Target();
			if (!target || a_inst.key->attribute < 0 || a_points <= 0.0f) {
				return;
			}
			float repaired = 0.0f;
			{
				std::unique_lock lock(State::Get().lock);
				if (auto* entry = LedgerFor(target->GetFormID(), false)) {
					auto& damage = entry->damage[a_inst.key->attribute];
					repaired = std::min(damage, a_points);
					damage -= repaired;
				}
			}
			if (repaired > 0.0f) {
				ApplyPoints(target, a_inst.key->attribute, repaired);
			}
		}

		void RestoreStart(Instance& a_inst)
		{
			if (a_inst.duration <= 0.0f) {
				RestoreApply(a_inst, a_inst.magnitude);
			}
		}

		void RestoreUpdate(Instance& a_inst, float a_delta)
		{
			if (a_inst.duration > 0.0f) {
				RestoreApply(a_inst, a_inst.magnitude * a_delta);
			}
		}

		// ---- Extended skill effects on Skyrim skills -----------------------------------------
		void SkillDamageApply(Instance& a_inst, float a_points)
		{
			auto* target = a_inst.Target();
			const auto av = SkillAV(a_inst.key->skill);
			if (!target || av == RE::ActorValue::kNone || a_points <= 0.0f) {
				return;
			}
			target->AsActorValueOwner()->DamageActorValue(av, a_points);  // Damage modifier: persists until restored
		}

		void SkillDamageStart(Instance& a_inst)
		{
			if (a_inst.duration <= 0.0f) {
				SkillDamageApply(a_inst, a_inst.magnitude);
			}
		}

		void SkillDamageUpdate(Instance& a_inst, float a_delta)
		{
			if (a_inst.duration > 0.0f) {
				SkillDamageApply(a_inst, a_inst.magnitude * a_delta);
			}
		}

		void SkillRestoreApply(Instance& a_inst, float a_points)
		{
			auto* target = a_inst.Target();
			const auto av = SkillAV(a_inst.key->skill);
			if (!target || av == RE::ActorValue::kNone) {
				return;
			}
			const float damage = -target->GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kDamage, av);
			const float amount = std::min(std::max(0.0f, damage), a_points);
			if (amount > 0.0f) {
				target->AsActorValueOwner()->RestoreActorValue(av, amount);
			}
		}

		void SkillRestoreStart(Instance& a_inst)
		{
			if (a_inst.duration <= 0.0f) {
				SkillRestoreApply(a_inst, a_inst.magnitude);
			}
		}

		void SkillRestoreUpdate(Instance& a_inst, float a_delta)
		{
			if (a_inst.duration > 0.0f) {
				SkillRestoreApply(a_inst, a_inst.magnitude * a_delta);
			}
		}

		void SkillAbsorbStart(Instance& a_inst)
		{
			const auto av = SkillAV(a_inst.key->skill);
			if (auto* target = a_inst.Target()) {
				ApplyAVDeltas(target, a_inst.avTarget, { { av, -a_inst.magnitude } });
			}
			if (auto* caster = a_inst.Caster(); caster && caster != a_inst.Target()) {
				ApplyAVDeltas(caster, a_inst.avCaster, { { av, a_inst.magnitude } });
			}
		}

		void SkillAbsorbResume(Instance& a_inst)
		{
			const auto av = SkillAV(a_inst.key->skill);
			a_inst.avTarget = { { av, -a_inst.magnitude } };
			if (a_inst.casterId != a_inst.targetId && a_inst.casterId) {
				a_inst.avCaster = { { av, a_inst.magnitude } };
			}
		}

		// ---- special targets that are plain actor values ---------------------------------------
		// Sign of a skill-family effect on its target (fortify raises, everything else lowers;
		// restore is a no-op on special targets).
		float SkillSign(std::string_view a_id)
		{
			if (a_id == "mw.fortify_skill") {
				return 1.0f;
			}
			if (a_id == "mwx.restore_skill") {
				return 0.0f;
			}
			return -1.0f;  // drain, damage (held for the duration on special targets), absorb
		}

		template <RE::ActorValue AV, int PerPoint>
		struct SpecialAV
		{
			static std::vector<std::pair<RE::ActorValue, float>> Deltas(const Instance& a_inst, float a_sign)
			{
				return { { AV, a_sign * a_inst.magnitude * static_cast<float>(PerPoint) } };
			}
			static void Start(Instance& a_inst)
			{
				const float sign = SkillSign(a_inst.key->id);
				if (sign == 0.0f) {
					return;
				}
				ApplyAVDeltas(a_inst.Target(), a_inst.avTarget, Deltas(a_inst, sign));
				if (a_inst.key->id == "mwx.absorb_skill") {
					if (auto* caster = a_inst.Caster(); caster && caster != a_inst.Target()) {
						ApplyAVDeltas(caster, a_inst.avCaster, Deltas(a_inst, 1.0f));
					}
				}
			}
			static void Resume(Instance& a_inst)
			{
				const float sign = SkillSign(a_inst.key->id);
				if (sign == 0.0f) {
					return;
				}
				a_inst.avTarget = Deltas(a_inst, sign);
				if (a_inst.key->id == "mwx.absorb_skill" && a_inst.casterId && a_inst.casterId != a_inst.targetId) {
					a_inst.avCaster = Deltas(a_inst, 1.0f);
				}
			}
		};

		void RegisterSkillFamily(std::string_view a_id)
		{
			// Athletics: running and swimming speed, 1 % per point (SpeedMult is a percentage).
			Register(std::string(a_id) + "#RunSwimSpeed",
				{ SpecialAV<RE::ActorValue::kSpeedMult, 1>::Start, SpecialAV<RE::ActorValue::kSpeedMult, 1>::Resume, HeldFinish, nullptr });
			// Hand-to-Hand: unarmed damage, 1 point per point.
			Register(std::string(a_id) + "#UnarmedDamage",
				{ SpecialAV<RE::ActorValue::kUnarmedDamage, 1>::Start, SpecialAV<RE::ActorValue::kUnarmedDamage, 1>::Resume, HeldFinish, nullptr });
		}
	}

	// ---------------------------------------------------------------------------------------
	std::vector<std::pair<RE::ActorValue, float>> AttributeAVDeltas(int a_attribute, float a_points)
	{
		std::vector<std::pair<RE::ActorValue, float>> out;
		const auto* attr = Attribute(a_attribute);
		if (!attr) {
			return out;
		}
		for (const auto& d : Mech::AttributeStats(*attr, a_points, State::Get().settings.attributeProfile)) {
			for (const auto& s : kStatAVs) {
				if (s.stat == d.stat) {
					out.emplace_back(s.av, static_cast<float>(d.value) * s.scale);
				}
			}
		}
		return out;
	}

	namespace
	{
		double AttributeAggregate(const RE::Actor* a_actor, std::string_view a_stat)
		{
			if (!a_actor) {
				return 0.0;
			}
			const auto id = a_actor->GetFormID();
			double     total = 0.0;
			ForEachInstance([&](const Instance& a_inst) {
				if (a_inst.key->attribute < 0) {
					return;
				}
				const auto& effect = a_inst.key->id;
				double      points = 0.0;
				if (a_inst.targetId == id) {
					if (effect == "mw.fortify_attribute") {
						points = a_inst.magnitude;
					} else if (effect == "mw.drain_attribute" || effect == "mw.absorb_attribute") {
						points = -a_inst.magnitude;
					}
				}
				if (effect == "mw.absorb_attribute" && a_inst.casterId == id && a_inst.targetId != id) {
					points += a_inst.magnitude;
				}
				if (points != 0.0) {
					total += NonAVStat(a_inst.key->attribute, points, a_stat);
				}
			});
			std::shared_lock lock(State::Get().lock);
			for (const auto& entry : State::Get().ledger) {
				if (entry.actor != id) {
					continue;
				}
				for (int a = 0; a < static_cast<int>(kAttributeCount); ++a) {
					if (entry.damage[a] > 0.0f) {
						total += NonAVStat(a, -entry.damage[a], a_stat);
					}
				}
			}
			return total;
		}
	}

	double EvasionPercent(const RE::Actor* a_actor) { return AttributeAggregate(a_actor, "EvasionPercent"); }
	double MeleeDamagePercent(const RE::Actor* a_actor) { return AttributeAggregate(a_actor, "MeleeDamagePercent"); }

	void ClearLedger(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return;
		}
		std::array<float, kAttributeCount> damage{};
		{
			std::unique_lock lock(State::Get().lock);
			auto& ledger = State::Get().ledger;
			for (auto it = ledger.begin(); it != ledger.end(); ++it) {
				if (it->actor == a_actor->GetFormID()) {
					damage = it->damage;
					ledger.erase(it);
					break;
				}
			}
		}
		for (int a = 0; a < static_cast<int>(kAttributeCount); ++a) {
			if (damage[a] > 0.0f) {
				ApplyPoints(a_actor, a, damage[a]);
			}
		}
	}

	void OnLoadedStats()
	{
		// The ledger's actor-value modifiers were saved with each actor; nothing to re-apply.
		// Only fully repaired entries are dropped: an unloaded NPC is not in memory, so a failed
		// lookup doesn't mean the actor is gone (Persistence drops entries whose plugin is gone).
		std::unique_lock lock(State::Get().lock);
		std::erase_if(State::Get().ledger, [](const LedgerEntry& e) {
			return std::ranges::all_of(e.damage, [](float d) { return d <= 0.0f; });
		});
	}

	void InstallStats()
	{
		const Handler held{ HeldStart, HeldResume, HeldFinish, nullptr };
		Register("mw.fortify_attribute", held);
		Register("mw.drain_attribute", held);
		Register("mw.absorb_attribute", held);
		Register("mw.damage_attribute", { DamageStart, nullptr, nullptr, DamageUpdate });
		Register("mw.restore_attribute", { RestoreStart, nullptr, nullptr, RestoreUpdate });

		Register("mwx.damage_skill", { SkillDamageStart, nullptr, nullptr, SkillDamageUpdate });
		Register("mwx.restore_skill", { SkillRestoreStart, nullptr, nullptr, SkillRestoreUpdate });
		Register("mwx.absorb_skill", { SkillAbsorbStart, SkillAbsorbResume, HeldFinish, nullptr });

		for (auto id : { "mw.fortify_skill", "mw.drain_skill", "mwx.damage_skill", "mwx.restore_skill", "mwx.absorb_skill" }) {
			RegisterSkillFamily(id);
		}
	}
}
