#pragma once

#include "Core/State.h"
#include "Effects/EffectSystems.h"

// Private plumbing shared by Effects/*.cpp and Casting/*.cpp (both owned by the cast-router /
// effect-systems component). Nothing outside Casting/ and Effects/ includes this header.
//
// Model: every custom-tier magic effect (Script archetype, LA_KW_Custom) that becomes active on
// an actor is mirrored by one Instance, created from TESActiveEffectApplyRemoveEvent (or rebuilt
// from the actor's active-effect list after a save loads) and destroyed when the effect ends.
// Handlers apply their changes in start(), undo them in finish(), and rebuild runtime-only state
// (shaders, controllers) in resume(). Queries used by hooks (miss chance, Silence, Sound, ...)
// read the instance list under a shared lock, so no hook ever walks an actor's effect list.
namespace LA::Effects::Internal
{
	// ---------------------------------------------------------------------------------------
	// Catalog identity of one of our MGEF variants ("LA_<Pascal>[_<Target>]_<Range>").
	struct EffectKey
	{
		const EffectDef* def{ nullptr };
		std::string      id;         // catalog id, e.g. "mw.fortify_attribute"
		std::string      target;     // "Strength", "OneHanded", "Acrobatics" (as in the EditorID)
		int              attribute{ -1 };  // 0-7 for attribute families
		int              skill{ -1 };      // Skyrim skill 0-17 for skill families
		std::string      special;    // special skill target: "Jump", "RunSwimSpeed", "UnarmedDamage", "Unarmored"
		Range            range{ Range::kSelf };
		bool             custom{ false };  // carries logic in this plugin (custom tier, attribute, special, mwx skill)
	};

	// nullptr when the MGEF is not one of our generated variants.
	const EffectKey* KeyOf(const RE::EffectSetting* a_mgef);
	void             BuildKeyIndex();  // pascal -> def table (kDataLoaded)

	// ---------------------------------------------------------------------------------------
	struct Instance
	{
		RE::ActorHandle    target;
		RE::ActorHandle    caster;
		RE::FormID         targetId{ 0 };
		RE::FormID         casterId{ 0 };
		std::uint16_t      uid{ 0 };
		const EffectKey*   key{ nullptr };
		RE::MagicItem*     spell{ nullptr };
		RE::EffectSetting* mgef{ nullptr };
		float              magnitude{ 0.0f };  // after the roll and perks
		float              duration{ 0.0f };   // 0 = instant
		float              elapsed{ 0.0f };
		float              tick{ 0.0f };       // accumulator for per-second work
		float              validate{ 0.0f };   // accumulator for the stale-instance check
		bool               resumed{ false };   // rebuilt after a load (persistent changes already in the save)
		bool               applied{ false };   // handler-specific "currently applied" toggle

		// Actor value deltas this instance applied with the Temporary ("magic") modifier. They are
		// saved by the engine with the actor, so finish() can always undo exactly this list; after a
		// load the handler recomputes it from the magnitude (resume()).
		std::vector<std::pair<RE::ActorValue, float>> avTarget;
		std::vector<std::pair<RE::ActorValue, float>> avCaster;

		// Handler scratch.
		std::array<std::int32_t, 4> i{};
		std::array<float, 4>        f{};
		std::array<RE::FormID, 4>   form{};

		RE::Actor* Target() const { return target.get().get(); }
		RE::Actor* Caster() const { return caster.get().get(); }
	};

	using Fn = void (*)(Instance&);
	using UpdateFn = void (*)(Instance&, float);

	struct Handler
	{
		Fn       start{ nullptr };
		Fn       resume{ nullptr };  // nullptr: nothing runtime-only to rebuild
		Fn       finish{ nullptr };
		UpdateFn update{ nullptr };  // called every frame while the instance lives (target loaded)
	};

	// Registration: by catalog id, or by "<id>#<special>" for the Morrowind special skill targets.
	void           Register(std::string_view a_key, Handler a_handler);
	const Handler* FindHandler(const EffectKey& a_key);

	// Instance queries (thread-safe; shared lock).
	double SumMagnitude(const RE::Actor* a_actor, std::string_view a_id);
	double MaxMagnitude(const RE::Actor* a_actor, std::string_view a_id);
	bool   HasInstance(const RE::Actor* a_actor, std::string_view a_id);
	// Calls a_fn for every live instance on a_actor (shared lock held; a_fn must not mutate).
	void ForEachOn(const RE::Actor* a_actor, const std::function<void(const Instance&)>& a_fn);
	void ForEachInstance(const std::function<void(const Instance&)>& a_fn);
	std::size_t InstanceCount();

	// Frame callbacks (main thread, PlayerCharacter::Update). Each module registers one.
	using FrameFn = void (*)(float a_delta);
	void AddFrameCallback(FrameFn a_fn);
	void WakeFrame();  // something needs per-frame work even without instances (grace slowfall, ...)

	// Test support: run a handler synchronously on a synthetic instance (no engine effect).
	Instance* StartSynthetic(RE::Actor* a_target, RE::Actor* a_caster, std::string_view a_id, int a_sub,
		std::string_view a_special, float a_magnitude, float a_duration);
	void      FinishSynthetic(Instance* a_instance);
	void      TickSynthetic(Instance* a_instance, float a_seconds);

	// ---------------------------------------------------------------------------------------
	// Module entry points (Effects/*.cpp).
	void InstallStats();
	void InstallMovement();
	void InstallCombat();
	void InstallMagic();
	void InstallWorld();
	void RegisterEffectTests();

	void RevertMovement();   // restore controller gravity / GMSTs before another save loads
	void RevertWorld();      // GMSTs (activation reach), detection shaders
	void OnLoadedStats();    // re-validate the ledger against loaded actors
	void OnLoadedWorld();

	// Cross-module services.
	bool   ApplyLockOpen(RE::Actor* a_caster, RE::TESObjectREFR* a_ref, std::string_view a_effectId, double a_magnitude);
	double EvasionPercent(const RE::Actor* a_actor);         // attribute translation (Agility, Luck)
	double MeleeDamagePercent(const RE::Actor* a_actor);     // Strength
	bool   IsLevitating(const RE::Actor* a_actor);
	bool   FallDamageCancelled(const RE::Actor* a_actor);    // Slowfall, Levitate, grace
	double FallReduction(const RE::Actor* a_actor);          // Jump / Acrobatics: units off the fall height
	void   ClearLedger(RE::Actor* a_actor);                  // shrine prayer
	void   OnDisintegrateRepair(RE::Actor* a_actor, bool a_armor, bool a_weapons);
	double ConditionOf(RE::FormID a_actor, RE::FormID a_item);  // 100 when untracked

	// ---------------------------------------------------------------------------------------
	// Small helpers.
	RE::PlayerCharacter* Player();
	int                  Roll100();      // 1..100
	int                  Roll0to99();
	float                RandomFloat();  // [0,1)
	void                 ModAV(RE::Actor* a_actor, RE::ActorValue a_av, float a_delta);  // Temporary modifier
	void                 NudgeSpeed(RE::Actor* a_actor);  // carry-weight nudge so SpeedMult changes apply
	RE::ActorValue       SkillAV(int a_skyrimSkill);      // 0-17 -> ActorValue (6 + index)
	RE::ActorValue       SchoolSkillAV(School a_school);
	RE::TESForm*         Vanilla(std::string_view a_ref);  // "Skyrim.esm|0x012FD0"
	template <class T>
	T* Vanilla(std::string_view a_ref)
	{
		auto* form = Vanilla(a_ref);
		return form ? form->As<T>() : nullptr;
	}
	float Distance(const RE::NiPoint3& a_lhs, const RE::NiPoint3& a_rhs);
	bool  IsCustomSlotSpell(const RE::MagicItem* a_spell);  // LA_Slot_* / LA_Sub_* record
	void  Notify(const std::string& a_text);
	float StaminaRatio(RE::Actor* a_actor);
	void  ApplyAVDeltas(RE::Actor* a_actor, std::vector<std::pair<RE::ActorValue, float>>& a_list,
		 const std::vector<std::pair<RE::ActorValue, float>>& a_deltas);
	void  RevertAVDeltas(RE::Actor* a_actor, std::vector<std::pair<RE::ActorValue, float>>& a_list);

	// Papyrus natives called through the VM (asynchronous, main thread safe).
	template <class... Args>
	bool CallMethod(RE::TESForm* a_object, const char* a_class, const char* a_function, Args... a_args)
	{
		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
		if (!a_object || !policy) {
			return false;
		}
		const auto handle = policy->GetHandleForObject(a_object->GetFormType(), a_object);
		if (handle == policy->EmptyHandle()) {
			return false;
		}
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
		auto* args = RE::MakeFunctionArguments(std::move(a_args)...);
		return vm->DispatchMethodCall(handle, a_class, a_function, args, callback);
	}

	// Runs a_fn with the result of a global Papyrus function (bool results only).
	void CallStaticBool(const char* a_class, const char* a_function, std::function<void(std::optional<bool>)> a_fn);

	inline constexpr std::string_view kSkyrim = "Skyrim.esm";
}
