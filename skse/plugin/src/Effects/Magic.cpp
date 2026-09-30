// Magic-control effects: Silence, Sound, Reflect, Dispel, Cure Blight Disease, Cure
// Paralyzation, Cure Poison and Resist Paralysis (Resist Corprus is handled where diseases are
// contracted, in EffectRegistry.cpp).
//
// Incoming effects are filtered in MagicTarget::AddTarget, vfunc 01 of the MagicTarget
// sub-object of Character and PlayerCharacter (CommonLib RE/M/MagicTarget.h "virtual bool
// AddTarget(AddTargetData&) // 01"; the MagicTarget base is the second base of Actor, so its
// vtable is VTABLE_Character[1] / VTABLE_PlayerCharacter[1], RE/A/Actor.h class declaration).
// Returning false means the effect was not added; this is the call that also produces the
// "magic effect apply" events Papyrus Extender reports (powerof3/PapyrusExtenderSSE
// src/Game/HookedEventHandler.cpp, MagicEffectApply::MagicTargetApply wraps a call into the same
// function and reads its bool result).

#include "Effects/EffectsInternal.h"

namespace LA::Effects::Internal
{
	namespace
	{
		using Archetype = RE::EffectArchetypes::ArchetypeID;

		void Nothing(Instance&) {}

		// ---- Reflect ------------------------------------------------------------------------
		// Never reflected, as in Morrowind: Lock, Open, Calm, Frenzy, Demoralize, Rally, Soultrap,
		// Turn Undead. Our own variants carry the catalog's reflectable flag.
		bool Reflectable(const RE::EffectSetting* a_base)
		{
			if (!a_base) {
				return false;
			}
			if (const auto* key = KeyOf(a_base)) {
				return key->def->reflectable;
			}
			switch (a_base->GetArchetype()) {
			case Archetype::kLock:
			case Archetype::kOpen:
			case Archetype::kCalm:
			case Archetype::kFrenzy:
			case Archetype::kDemoralize:
			case Archetype::kRally:
			case Archetype::kSoulTrap:
			case Archetype::kTurnUndead:
				return false;
			default:
				return true;
			}
		}

		bool IsSpellLike(const RE::MagicItem* a_item)
		{
			if (!a_item) {
				return false;
			}
			switch (a_item->GetSpellType()) {
			case RE::MagicSystem::SpellType::kSpell:
			case RE::MagicSystem::SpellType::kScroll:
			case RE::MagicSystem::SpellType::kStaffEnchantment:
				return true;
			default:
				return false;
			}
		}

		// One reflect decision per incoming spell, so all its effects bounce together.
		struct ReflectDecision
		{
			const RE::MagicItem* spell{ nullptr };
			RE::FormID           target{ 0 };
			RE::FormID           caster{ 0 };
			std::chrono::steady_clock::time_point when;
			bool                 reflect{ false };
		};
		std::mutex                   g_reflectLock;
		std::vector<ReflectDecision> g_reflect;
		thread_local int             g_reflectDepth = 0;

		bool DecideReflect(const RE::MagicItem* a_spell, RE::Actor* a_target, RE::Actor* a_caster, double a_magnitude)
		{
			const auto now = std::chrono::steady_clock::now();
			std::scoped_lock lock(g_reflectLock);
			std::erase_if(g_reflect, [&](const ReflectDecision& d) { return now - d.when > std::chrono::milliseconds(500); });
			for (const auto& d : g_reflect) {
				if (d.spell == a_spell && d.target == a_target->GetFormID() && d.caster == a_caster->GetFormID()) {
					return d.reflect;
				}
			}
			const bool reflect = Mech::ReflectRoll(a_magnitude, true, Roll100());
			g_reflect.push_back({ a_spell, a_target->GetFormID(), a_caster->GetFormID(), now, reflect });
			return reflect;
		}

		template <std::size_t I>
		struct AddTargetHook
		{
			static bool thunk(RE::MagicTarget* a_this, RE::MagicTarget::AddTargetData& a_data)
			{
				if (!State::Get().dataReady || InstanceCount() == 0 || !a_data.effect || !a_data.effect->baseEffect) {
					return func(a_this, a_data);
				}
				auto* ref = a_this->GetTargetStatsObject();
				auto* target = ref ? ref->As<RE::Actor>() : nullptr;
				if (!target) {
					return func(a_this, a_data);
				}
				auto* base = a_data.effect->baseEffect;

				// Resist Paralysis: M % chance to ignore paralysis.
				if (base->GetArchetype() == Archetype::kParalysis) {
					const double chance = SumMagnitude(target, "mw.resist_paralysis");
					if (chance > 0.0 && Mech::ChanceRoll(chance, Roll100())) {
						logger::debug("effects: {:08X} resisted paralysis", target->GetFormID());
						return false;
					}
				}

				// Reflect: M % chance to send an incoming spell back at its caster.
				auto* caster = a_data.caster ? a_data.caster->As<RE::Actor>() : nullptr;
				if (g_reflectDepth == 0 && caster && caster != target && IsSpellLike(a_data.magicItem) &&
					base->data.delivery != RE::MagicSystem::Delivery::kSelf && Reflectable(base)) {
					const double reflect = SumMagnitude(target, "mw.reflect");
					if (reflect > 0.0 && DecideReflect(a_data.magicItem, target, caster, reflect)) {
						auto* casterTarget = caster->GetMagicTarget();
						if (casterTarget) {
							RE::MagicTarget::AddTargetData bounced = a_data;
							bounced.caster = target;
							++g_reflectDepth;
							casterTarget->AddTarget(bounced);  // VERIFY(in-game): the bounced effect shows on the caster
							--g_reflectDepth;
						}
						return false;
					}
				}
				return func(a_this, a_data);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// ---- Dispel ---------------------------------------------------------------------------
		bool Dispellable(const RE::ActiveEffect* a_effect, const RE::MagicItem* a_self)
		{
			const auto* spell = a_effect->spell;
			if (!spell || spell == a_self || a_effect->duration <= 0.0f) {
				return false;
			}
			switch (spell->GetSpellType()) {
			case RE::MagicSystem::SpellType::kSpell:
			case RE::MagicSystem::SpellType::kScroll:
			case RE::MagicSystem::SpellType::kPower:
			case RE::MagicSystem::SpellType::kLesserPower:
			case RE::MagicSystem::SpellType::kVoicePower:
				return true;
			default:
				return false;  // abilities, diseases, potions, poisons, enchantments, addictions
			}
		}

		void DispelStart(Instance& a_inst)
		{
			auto* target = a_inst.Target();
			auto* list = target ? target->GetMagicTarget()->GetActiveEffectList() : nullptr;
			if (!list) {
				return;
			}
			// Group by (spell, caster): each active spell gets one roll.
			std::map<std::pair<const RE::MagicItem*, RE::FormID>, std::vector<RE::ActiveEffect*>> spells;
			for (auto* effect : *list) {
				if (effect && !effect->flags.any(RE::ActiveEffect::Flag::kDispelled) && Dispellable(effect, a_inst.spell)) {
					const auto caster = effect->caster.get();
					spells[{ effect->spell, caster ? caster->GetFormID() : 0 }].push_back(effect);
				}
			}
			std::vector<std::pair<RE::ActorHandle, std::uint16_t>> toDispel;
			for (auto& [key, effects] : spells) {
				if (Mech::DispelRoll(a_inst.magnitude, Roll100())) {
					for (auto* effect : effects) {
						toDispel.emplace_back(target->GetHandle(), effect->usUniqueID);
					}
				}
			}
			if (toDispel.empty()) {
				return;
			}
			SKSE::GetTaskInterface()->AddTask([toDispel]() {
				for (const auto& [handle, uid] : toDispel) {
					auto  actor = handle.get();
					auto* list = actor ? actor->GetMagicTarget()->GetActiveEffectList() : nullptr;
					if (!list) {
						continue;
					}
					for (auto* effect : *list) {
						if (effect && effect->usUniqueID == uid) {
							effect->Dispel(true);
							break;
						}
					}
				}
			});
		}

		// ---- Cures -----------------------------------------------------------------------------
		template <class Pred>
		void DispelMatching(RE::Actor* a_target, Pred a_pred, bool a_removeSpell)
		{
			auto* list = a_target ? a_target->GetMagicTarget()->GetActiveEffectList() : nullptr;
			if (!list) {
				return;
			}
			std::vector<std::uint16_t> uids;
			std::set<RE::FormID>       spells;
			for (auto* effect : *list) {
				if (effect && !effect->flags.any(RE::ActiveEffect::Flag::kDispelled) && a_pred(effect)) {
					uids.push_back(effect->usUniqueID);
					if (a_removeSpell && effect->spell) {
						spells.insert(effect->spell->GetFormID());
					}
				}
			}
			if (uids.empty()) {
				return;
			}
			const auto handle = a_target->GetHandle();
			SKSE::GetTaskInterface()->AddTask([handle, uids, spells]() {
				auto actor = handle.get();
				if (!actor) {
					return;
				}
				for (auto id : spells) {
					if (auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(id)) {
						actor->RemoveSpell(spell);  // diseases live in the spell list
					}
				}
				if (auto* list = actor->GetMagicTarget()->GetActiveEffectList()) {
					for (auto* effect : *list) {
						if (effect && std::ranges::find(uids, effect->usUniqueID) != uids.end()) {
							effect->Dispel(true);
						}
					}
				}
			});
		}

		void CureBlightStart(Instance& a_inst)
		{
			static RE::SpellItem* blackHeart = Vanilla<RE::SpellItem>("Dragonborn.esm|0x01FF2E");  // DLC2DiseaseBlackHeartBlight
			static RE::SpellItem* droops = Vanilla<RE::SpellItem>("Dragonborn.esm|0x0285C1");      // DLC2DiseaseDroops
			auto* blightKeyword = State::Get().forms.Get<RE::BGSKeyword>("LA_KW_Blight");
			DispelMatching(a_inst.Target(), [&](RE::ActiveEffect* a_effect) {
				const auto* spell = a_effect->spell;
				if (!spell || spell->GetSpellType() != RE::MagicSystem::SpellType::kDisease) {
					return false;
				}
				if (spell == blackHeart || spell == droops) {
					return true;
				}
				auto* base = a_effect->GetBaseObject();
				return blightKeyword && ((base && base->HasKeyword(blightKeyword)) || spell->HasKeyword(blightKeyword));
			}, true);
		}

		void CureParalysisStart(Instance& a_inst)
		{
			static RE::BGSKeyword* magicParalysis = Vanilla<RE::BGSKeyword>("Skyrim.esm|0x01EA70");  // MagicParalysis
			DispelMatching(a_inst.Target(), [&](RE::ActiveEffect* a_effect) {
				auto* base = a_effect->GetBaseObject();
				return base && (base->GetArchetype() == Archetype::kParalysis || (magicParalysis && base->HasKeyword(magicParalysis)));
			}, false);
		}

		void CurePoisonStart(Instance& a_inst)
		{
			DispelMatching(a_inst.Target(), [&](RE::ActiveEffect* a_effect) {
				const auto* spell = a_effect->spell;
				if (spell && (spell->IsPoison() || spell->GetSpellType() == RE::MagicSystem::SpellType::kPoison)) {
					return true;
				}
				auto* base = a_effect->GetBaseObject();
				// Poison damage from other sources (trap spells, creature attacks): resisted by
				// PoisonResist and detrimental.
				return base && base->data.resistVariable == RE::ActorValue::kPoisonResist && base->IsDetrimental();
			}, false);
		}

		// ---- Silence --------------------------------------------------------------------------
		void SilenceStart(Instance& a_inst)
		{
			if (auto* target = a_inst.Target()) {
				target->InterruptCast(false);  // a spell being charged fizzles
			}
		}
	}

	void InstallMagic()
	{
		Register("mw.silence", { SilenceStart, nullptr, nullptr, nullptr });  // CheckCast hook (Casting) reads it
		Register("mw.sound", { Nothing, nullptr, nullptr, nullptr });         // SpellCast hook (Casting) reads it
		Register("mw.reflect", { Nothing, nullptr, nullptr, nullptr });
		Register("mw.resist_paralysis", { Nothing, nullptr, nullptr, nullptr });
		Register("mw.resist_corprus_disease", { Nothing, nullptr, nullptr, nullptr });
		Register("mw.dispel", { DispelStart, nullptr, nullptr, nullptr });
		Register("mw.cure_blight_disease", { CureBlightStart, nullptr, nullptr, nullptr });
		Register("mw.cure_paralyzation", { CureParalysisStart, nullptr, nullptr, nullptr });
		Register("mw.cure_poison", { CurePoisonStart, nullptr, nullptr, nullptr });

		stl::write_vfunc<AddTargetHook<0>>(RE::VTABLE_Character[1], 0x1);
		stl::write_vfunc<AddTargetHook<1>>(RE::VTABLE_PlayerCharacter[1], 0x1);
		logger::info("effects: MagicTarget::AddTarget hooks (Reflect, Resist Paralysis)");
	}
}

namespace LA::Effects
{
	bool IsSilenced(RE::Actor* a_actor) { return Internal::HasInstance(a_actor, "mw.silence"); }
	double SoundMagnitude(RE::Actor* a_actor) { return Internal::SumMagnitude(a_actor, "mw.sound"); }
}
