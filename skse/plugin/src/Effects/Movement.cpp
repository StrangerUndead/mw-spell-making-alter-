// Flight, falling, jumping and swimming: Levitate, Slowfall, Jump, Swift Swim and the Morrowind
// Acrobatics special target, plus the fall-damage hook.
//
// Sources for engine behaviour:
//  * bhkCharacterController::gravity / fallStartHeight / fallTime / context.currentState and the
//    SetLinearVelocityImpl virtual (CommonLib RE/B/bhkCharacterController.h). Velocities are in
//    Havok units: game units x bhkWorld::GetWorldScale() (KB "Havok units").
//  * Fall damage: the two calls to the fall-damage calculation that powerof3's Papyrus Extender
//    hooks (powerof3/PapyrusExtenderSSE src/Game/HookedEventHandler.cpp, namespace
//    FallLongDistance: RELOCATION_ID(36346, 37336) + 0x35 and RELOCATION_ID(36973, 37998) +
//    OFFSET(0xAE, 0xAB); thunk float(Actor*, float fallDistance, float defaultMult)). write_call
//    chains, so Papyrus Extender's own hook keeps working.
//  * Jump height: the GMST fJumpHeightMin (vanilla 76 units, the value "setgs fJumpHeightMin"
//    changes). The JumpingBonus actor value is ignored by vanilla; po3's Tweaks makes the engine
//    read it (OUTLINE "Compatibility"), so with po3_Tweaks.dll loaded we only set the actor value.
//
// Spike S4 (Levitation feel): this is the velocity-based controller. The documented fallback, an
// invisible platform under the actor (as older levitation mods do), is not implemented; if S4
// fails, replace DriveLevitation() with a platform that follows the actor's XY and is raised or
// lowered by the same inputs.

#include "Effects/EffectsInternal.h"

namespace LA::Effects::Internal
{
	namespace
	{
		struct MoveState
		{
			float originalGravity{ 1.0f };
			float lastWritten{ -1.0f };  // gravity we wrote last frame (-1 = we don't own it)
			bool  yielded{ false };
			bool  levitating{ false };
			bool  slowfalling{ false };
			float grace{ 0.0f };       // seconds of grace Slowfall left (after Levitate)
			float hoverZ{ 0.0f };      // NPC hover height
			bool  hoverInit{ false };
			bool  fallAdjusted{ false };  // Jump: fall height shortened for the current fall
		};

		// Written on the main thread; read by the fall-damage hook, which can run on an AI thread.
		std::recursive_mutex                      g_moveLock;
		std::unordered_map<RE::FormID, MoveState> g_move;
		std::atomic<bool> g_jumpHeld{ false };
		std::atomic<bool> g_sneakHeld{ false };
		bool              g_fallHookInstalled{ false };
		bool              g_po3Tweaks{ false };
		float             g_jumpBase{ 0.0f };  // fJumpHeightMin at install
		RE::Setting*      g_jumpSetting{ nullptr };

		// ---- input ------------------------------------------------------------------------
		class InputSink final : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static InputSink* Get()
			{
				static InputSink sink;
				return &sink;
			}
			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				if (!a_event) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const auto* events = RE::UserEvents::GetSingleton();
				for (auto* e = *a_event; e; e = e->next) {
					const auto* button = e->AsButtonEvent();
					if (!button || !events) {
						continue;
					}
					const auto& name = button->QUserEvent();
					if (name == events->jump) {
						g_jumpHeld = button->IsPressed();
					} else if (name == events->sneak) {
						g_sneakHeld = button->IsPressed();
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// ---- helpers ------------------------------------------------------------------------
		bool InAir(RE::bhkCharacterController* a_controller)
		{
			const auto state = a_controller->context.currentState;
			return state == RE::hkpCharacterStateType::kInAir || state == RE::hkpCharacterStateType::kJumping;
		}

		// Another flight or glide controller changed gravity since our last write: yield to it.
		bool OwnsGravity(MoveState& a_state, RE::bhkCharacterController* a_controller)
		{
			if (a_state.lastWritten >= 0.0f && std::abs(a_controller->gravity - a_state.lastWritten) > 1e-3f &&
				std::abs(a_controller->gravity - a_state.originalGravity) > 1e-3f) {
				if (!a_state.yielded) {
					logger::info("effects: another controller owns gravity ({:.2f}); Levitate/Slowfall yield", a_controller->gravity);
				}
				a_state.yielded = true;
				a_state.lastWritten = -1.0f;
				return false;
			}
			a_state.yielded = false;
			return true;
		}

		void WriteGravity(MoveState& a_state, RE::bhkCharacterController* a_controller, float a_value)
		{
			if (a_state.lastWritten < 0.0f) {
				a_state.originalGravity = a_controller->gravity > 0.0f ? a_controller->gravity : 1.0f;
			}
			a_controller->gravity = a_value;
			a_state.lastWritten = a_value;
		}

		void ReleaseGravity(MoveState& a_state, RE::Actor* a_actor)
		{
			if (a_state.lastWritten < 0.0f) {
				return;
			}
			if (auto* controller = a_actor ? a_actor->GetCharController() : nullptr) {
				if (std::abs(controller->gravity - a_state.lastWritten) < 1e-3f) {
					controller->gravity = a_state.originalGravity;
				}
				controller->fallStartHeight = a_actor->GetPositionZ();
				controller->fallTime = 0.0f;
			}
			a_state.lastWritten = -1.0f;
		}

		void NoFallDamage(RE::Actor* a_actor, RE::bhkCharacterController* a_controller)
		{
			// Fallback when the fall-damage hook is unavailable: the fall starts here every frame.
			a_controller->fallStartHeight = a_actor->GetPositionZ();
			a_controller->fallTime = 0.0f;
		}

		void DriveLevitation(RE::Actor* a_actor, MoveState& a_state, double a_magnitude)
		{
			auto* controller = a_actor->GetCharController();
			if (!controller || !OwnsGravity(a_state, controller)) {
				return;
			}
			const float speed = static_cast<float>(Mech::LevitateSpeed(a_magnitude));
			const float scale = RE::bhkWorld::GetWorldScale();
			NoFallDamage(a_actor, controller);

			if (a_actor->IsPlayerRef()) {
				const bool up = g_jumpHeld.load();
				const bool down = g_sneakHeld.load() || a_actor->IsSneaking();
				if (!InAir(controller) && !up) {
					// Walking on the ground: leave the normal controller alone.
					WriteGravity(a_state, controller, a_state.lastWritten < 0.0f ? controller->gravity : a_state.originalGravity);
					return;
				}
				WriteGravity(a_state, controller, 0.0f);
				auto*  controls = RE::PlayerControls::GetSingleton();
				float  mx = controls ? controls->data.moveInputVec.x : 0.0f;
				float  my = controls ? controls->data.moveInputVec.y : 0.0f;
				const float heading = a_actor->GetAngleZ();
				const float fx = std::sin(heading), fy = std::cos(heading);   // forward (heading 0 = +Y)
				const float rx = std::cos(heading), ry = -std::sin(heading);  // right
				const float vx = speed * (my * fx + mx * rx);
				const float vy = speed * (my * fy + mx * ry);
				const float vz = up ? speed * 0.6f : (down ? -speed * 0.6f : 0.0f);
				if (up && !InAir(controller)) {
					controller->context.currentState = RE::hkpCharacterStateType::kInAir;  // VERIFY(in-game): lift-off from standing
				}
				controller->SetLinearVelocityImpl(RE::hkVector4(vx * scale, vy * scale, vz * scale, 0.0f));
				return;
			}

			// NPCs float up about 1.5 m and hover (OUTLINE: "Levitated NPCs float up and hover").
			const float z = a_actor->GetPositionZ();
			if (!a_state.hoverInit) {
				a_state.hoverInit = true;
				a_state.hoverZ = z + 100.0f;
			}
			WriteGravity(a_state, controller, 0.0f);
			const float vz = std::clamp((a_state.hoverZ - z) * 2.0f, -speed * 0.5f, speed * 0.5f);
			if (std::abs(a_state.hoverZ - z) > 2.0f && !InAir(controller)) {
				controller->context.currentState = RE::hkpCharacterStateType::kInAir;  // VERIFY(in-game)
			}
			controller->SetLinearVelocityImpl(RE::hkVector4(0.0f, 0.0f, vz * scale, 0.0f));
		}

		void DriveSlowfall(RE::Actor* a_actor, MoveState& a_state, double a_magnitude)
		{
			auto* controller = a_actor->GetCharController();
			if (!controller || !OwnsGravity(a_state, controller)) {
				return;
			}
			if (!InAir(controller)) {
				if (a_state.lastWritten >= 0.0f) {
					ReleaseGravity(a_state, a_actor);
				}
				return;
			}
			// Fall speed x (1 - M/200): with constant acceleration, speed at every instant scales
			// with the gravity factor.
			WriteGravity(a_state, controller, static_cast<float>(Mech::SlowfallFactor(a_magnitude)));
			if (!g_fallHookInstalled) {
				NoFallDamage(a_actor, controller);
			}
		}

		double JumpPercent(const RE::Actor* a_actor)
		{
			const double perPoint = State::Get().settings.jumpPerPoint;
			double       pct = 0.0;
			const auto   id = a_actor ? a_actor->GetFormID() : 0;
			ForEachInstance([&](const Instance& a_inst) {
				double sign = 0.0;
				if (a_inst.key->id == "mw.jump") {
					sign = a_inst.targetId == id ? 1.0 : 0.0;
				} else if (a_inst.key->special == "Jump") {
					if (a_inst.targetId == id) {
						sign = a_inst.key->id == "mw.fortify_skill" ? 1.0 : (a_inst.key->id == "mwx.restore_skill" ? 0.0 : -1.0);
					} else if (a_inst.key->id == "mwx.absorb_skill" && a_inst.casterId == id) {
						sign = 1.0;
					}
				}
				pct += sign * Mech::JumpBonusPercent(a_inst.magnitude, perPoint);
			});
			return pct;
		}

		void ApplyJumpHeight()
		{
			// Without po3's Tweaks the engine never reads JumpingBonus, so the player's jump height
			// is raised through fJumpHeightMin (only the player jumps under player control).
			if (g_po3Tweaks || !g_jumpSetting || g_jumpBase <= 0.0f) {
				return;
			}
			const double pct = JumpPercent(Player());
			g_jumpSetting->data.f = static_cast<float>(g_jumpBase * std::max(0.1, 1.0 + pct / 100.0));
		}

		// ---- handlers -----------------------------------------------------------------------
		void Track(Instance& a_inst)
		{
			std::scoped_lock lock(g_moveLock);
			g_move.try_emplace(a_inst.targetId);
			WakeFrame();
		}

		void LevitateStart(Instance& a_inst) { Track(a_inst); }
		void LevitateFinish(Instance& a_inst)
		{
			if (HasInstanceOther(a_inst, "mw.levitate")) {
				return;
			}
			std::scoped_lock lock(g_moveLock);
			auto it = g_move.find(a_inst.targetId);
			if (it == g_move.end()) {
				return;
			}
			auto& state = it->second;
			state.levitating = false;
			state.hoverInit = false;
			ReleaseGravity(state, a_inst.Target());
			if (State::Get().settings.graceSlowfall) {
				state.grace = 15.0f;  // until landing, at most 15 s
				WakeFrame();
			}
		}

		void SlowfallStart(Instance& a_inst) { Track(a_inst); }
		void SlowfallFinish(Instance& a_inst)
		{
			if (HasInstanceOther(a_inst, "mw.slowfall")) {
				return;
			}
			std::scoped_lock lock(g_moveLock);
			if (auto it = g_move.find(a_inst.targetId); it != g_move.end()) {
				it->second.slowfalling = false;
				ReleaseGravity(it->second, a_inst.Target());
			}
		}

		// Jump: JumpingBonus actor value (read by po3's Tweaks) plus the player's jump height.
		// VERIFY(in-game): po3's Tweaks reads JumpingBonus as a percentage of the jump height
		// (the value written here); fJumpHeightMin is global, so NPC jumps grow too while the
		// player's Jump is active (NPCs rarely jump).
		void JumpStart(Instance& a_inst)
		{
			const float pct = static_cast<float>(Mech::JumpBonusPercent(a_inst.magnitude, State::Get().settings.jumpPerPoint));
			ApplyAVDeltas(a_inst.Target(), a_inst.avTarget, { { RE::ActorValue::kJumpingBonus, pct } });
			Track(a_inst);
			ApplyJumpHeight();
		}
		void JumpResume(Instance& a_inst)
		{
			const float pct = static_cast<float>(Mech::JumpBonusPercent(a_inst.magnitude, State::Get().settings.jumpPerPoint));
			a_inst.avTarget = { { RE::ActorValue::kJumpingBonus, pct } };
			Track(a_inst);
			ApplyJumpHeight();
		}
		void JumpFinish(Instance& a_inst)
		{
			RevertAVDeltas(a_inst.Target(), a_inst.avTarget);
			// JumpPercent still counts this instance until it is erased: recompute next frame.
			SKSE::GetTaskInterface()->AddTask([]() { ApplyJumpHeight(); });
		}

		float AcrobaticsSign(const Instance& a_inst)
		{
			if (a_inst.key->id == "mw.fortify_skill") {
				return 1.0f;
			}
			if (a_inst.key->id == "mwx.restore_skill") {
				return 0.0f;
			}
			return -1.0f;
		}

		void AcrobaticsStart(Instance& a_inst)
		{
			const float pct = AcrobaticsSign(a_inst) *
			                  static_cast<float>(Mech::JumpBonusPercent(a_inst.magnitude, State::Get().settings.jumpPerPoint));
			if (pct != 0.0f) {
				ApplyAVDeltas(a_inst.Target(), a_inst.avTarget, { { RE::ActorValue::kJumpingBonus, pct } });
				if (a_inst.key->id == "mwx.absorb_skill") {
					if (auto* caster = a_inst.Caster(); caster && caster != a_inst.Target()) {
						ApplyAVDeltas(caster, a_inst.avCaster, { { RE::ActorValue::kJumpingBonus, -pct } });
					}
				}
			}
			Track(a_inst);
			ApplyJumpHeight();
		}
		void AcrobaticsResume(Instance& a_inst)
		{
			const float pct = AcrobaticsSign(a_inst) *
			                  static_cast<float>(Mech::JumpBonusPercent(a_inst.magnitude, State::Get().settings.jumpPerPoint));
			if (pct != 0.0f) {
				a_inst.avTarget = { { RE::ActorValue::kJumpingBonus, pct } };
				if (a_inst.key->id == "mwx.absorb_skill" && a_inst.casterId && a_inst.casterId != a_inst.targetId) {
					a_inst.avCaster = { { RE::ActorValue::kJumpingBonus, -pct } };
				}
			}
			ApplyJumpHeight();
		}
		void AcrobaticsFinish(Instance& a_inst)
		{
			RevertAVDeltas(a_inst.Target(), a_inst.avTarget);
			if (!a_inst.avCaster.empty()) {
				RevertAVDeltas(a_inst.Caster(), a_inst.avCaster);
			}
			SKSE::GetTaskInterface()->AddTask([]() { ApplyJumpHeight(); });
		}

		// Swift Swim: +M % speed only while swimming.
		void SwiftSwimUpdate(Instance& a_inst, float a_delta)
		{
			a_inst.tick += a_delta;
			if (a_inst.tick < 0.25f) {
				return;
			}
			a_inst.tick = 0.0f;
			auto* target = a_inst.Target();
			const bool swimming = target && target->AsActorState()->IsSwimming();
			if (swimming && !a_inst.applied) {
				ApplyAVDeltas(target, a_inst.avTarget, { { RE::ActorValue::kSpeedMult, a_inst.magnitude } });
				a_inst.applied = true;
			} else if (!swimming && a_inst.applied) {
				RevertAVDeltas(target, a_inst.avTarget);
				a_inst.applied = false;
			}
		}
		void SwiftSwimResume(Instance& a_inst)
		{
			// The speed modifier is saved with the actor when the game was saved mid-swim.
			auto* target = a_inst.Target();
			if (target && target->AsActorState()->IsSwimming()) {
				a_inst.avTarget = { { RE::ActorValue::kSpeedMult, a_inst.magnitude } };
				a_inst.applied = true;
			}
		}
		void SwiftSwimFinish(Instance& a_inst)
		{
			RevertAVDeltas(a_inst.Target(), a_inst.avTarget);
			a_inst.applied = false;
		}

		// ---- frame ----------------------------------------------------------------------------
		void MovementFrame(float a_delta)
		{
			std::scoped_lock lock(g_moveLock);
			if (g_move.empty()) {
				return;
			}
			std::vector<RE::FormID> done;
			for (auto& [id, state] : g_move) {
				auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
				if (!actor) {
					done.push_back(id);
					continue;
				}
				const double lev = MaxMagnitude(actor, "mw.levitate");
				const double slow = MaxMagnitude(actor, "mw.slowfall");
				const bool   jumping = HasInstance(actor, "mw.jump") || JumpPercent(actor) != 0.0;
				state.levitating = lev > 0.0 || HasInstance(actor, "mw.levitate");
				state.slowfalling = slow > 0.0 || HasInstance(actor, "mw.slowfall");
				if (!actor->Is3DLoaded()) {
					continue;
				}
				if (state.levitating) {
					DriveLevitation(actor, state, std::max(1.0, lev));
				} else if (state.slowfalling) {
					DriveSlowfall(actor, state, slow);
				} else if (state.grace > 0.0f) {
					state.grace -= a_delta;
					auto* controller = actor->GetCharController();
					if (!controller || !InAir(controller)) {
						state.grace = 0.0f;
						ReleaseGravity(state, actor);
					} else {
						DriveSlowfall(actor, state, 100.0);  // grace: half speed, no fall damage
					}
				}
				if (!state.levitating && !state.slowfalling && state.grace <= 0.0f && !jumping) {
					ReleaseGravity(state, actor);
					done.push_back(id);
				}
			}
			for (auto id : done) {
				g_move.erase(id);
			}
		}

		// ---- fall damage hook -------------------------------------------------------------------
		template <int N>
		struct CalcDoDamage
		{
			static float thunk(RE::Actor* a_this, float a_fallDistance, float a_defaultMult)
			{
				if (a_this && State::Get().dataReady) {
					if (FallDamageCancelled(a_this)) {
						return 0.0f;
					}
					const double reduction = FallReduction(a_this);
					if (reduction > 0.0) {
						a_fallDistance = std::max(0.0f, a_fallDistance - static_cast<float>(reduction));
					}
				}
				return func(a_this, a_fallDistance, a_defaultMult);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		SKSE::Trampoline& Trampoline()
		{
			// Never destroyed: the patched call sites keep jumping through it until the process exits.
			static auto* trampoline = new SKSE::Trampoline(std::string_view{ "LostArt.Movement" });
			return *trampoline;
		}

		template <class T>
		bool WriteCall(REL::Relocation<std::uintptr_t>& a_target, std::string_view a_what)
		{
			const auto address = a_target.address();
			if (*reinterpret_cast<const std::uint8_t*>(address) != 0xE8) {
				logger::warn("effects: {} hook site is not a call (0x{:X}); skipped", a_what, address);
				return false;
			}
			T::func = Trampoline().write_call<5>(address, T::thunk);
			return true;
		}
	}

	bool HasInstanceOther(const Instance& a_inst, std::string_view a_id)
	{
		bool other = false;
		ForEachOn(a_inst.Target(), [&](const Instance& a_other) {
			other = other || (&a_other != &a_inst && a_other.key->id == a_id);
		});
		return other;
	}

	bool IsLevitating(const RE::Actor* a_actor) { return HasInstance(a_actor, "mw.levitate"); }

	bool FallDamageCancelled(const RE::Actor* a_actor)
	{
		if (!a_actor) {
			return false;
		}
		if (IsLevitating(a_actor) || HasInstance(a_actor, "mw.slowfall")) {
			return true;
		}
		std::scoped_lock lock(g_moveLock);
		auto it = g_move.find(a_actor->GetFormID());
		return it != g_move.end() && it->second.grace > 0.0f;
	}

	double FallReduction(const RE::Actor* a_actor)
	{
		double     units = 0.0;
		const auto id = a_actor ? a_actor->GetFormID() : 0;
		ForEachInstance([&](const Instance& a_inst) {
			if (a_inst.targetId != id) {
				return;
			}
			if (a_inst.key->id == "mw.jump" || (a_inst.key->special == "Jump" && a_inst.key->id == "mw.fortify_skill")) {
				units += Mech::JumpFallReduction(a_inst.magnitude);
			}
		});
		return units;
	}

	void RevertMovement()
	{
		std::scoped_lock lock(g_moveLock);
		for (auto& [id, state] : g_move) {
			ReleaseGravity(state, RE::TESForm::LookupByID<RE::Actor>(id));
		}
		g_move.clear();
		if (g_jumpSetting && g_jumpBase > 0.0f) {
			g_jumpSetting->data.f = g_jumpBase;
		}
	}

	void InstallMovement()
	{
		g_po3Tweaks = REX::W32::GetModuleHandleW(L"po3_Tweaks.dll") != nullptr;
		if (auto* settings = RE::GameSettingCollection::GetSingleton()) {
			g_jumpSetting = settings->GetSetting("fJumpHeightMin");
			g_jumpBase = g_jumpSetting ? g_jumpSetting->GetFloat() : 0.0f;
		}
		logger::info("effects: jump height via {} (fJumpHeightMin {:.0f})", g_po3Tweaks ? "po3's Tweaks (JumpingBonus)" : "fJumpHeightMin",
			g_jumpBase);

		Register("mw.levitate", { LevitateStart, LevitateStart, LevitateFinish, nullptr });
		Register("mw.slowfall", { SlowfallStart, SlowfallStart, SlowfallFinish, nullptr });
		Register("mw.jump", { JumpStart, JumpResume, JumpFinish, nullptr });
		Register("mw.swift_swim", { nullptr, SwiftSwimResume, SwiftSwimFinish, SwiftSwimUpdate });
		for (auto id : { "mw.fortify_skill", "mw.drain_skill", "mwx.damage_skill", "mwx.restore_skill", "mwx.absorb_skill" }) {
			Register(std::string(id) + "#Jump", { AcrobaticsStart, AcrobaticsResume, AcrobaticsFinish, nullptr });
		}
		AddFrameCallback(MovementFrame);

		if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
			input->AddEventSink(InputSink::Get());
		}

		Trampoline().create(64);
		REL::Relocation<std::uintptr_t> ragdoll{ RELOCATION_ID(36346, 37336), 0x35 };
		REL::Relocation<std::uintptr_t> moveFinish{ RELOCATION_ID(36973, 37998), REL::VariantOffset(0xAE, 0xAB, 0) };
		const bool a = WriteCall<CalcDoDamage<0>>(ragdoll, "fall damage (ragdoll)");
		const bool b = WriteCall<CalcDoDamage<1>>(moveFinish, "fall damage (landing)");
		g_fallHookInstalled = a && b;
		logger::info("effects: fall-damage hook {}", g_fallHookInstalled ? "installed" : "partial - per-frame fallback in use");
	}
}

namespace LA::Effects
{
	bool IsSlowfalling(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return false;
		}
		if (Internal::HasInstance(a_actor, "mw.slowfall")) {
			return true;
		}
		std::scoped_lock lock(Internal::g_moveLock);
		auto it = Internal::g_move.find(a_actor->GetFormID());
		return it != Internal::g_move.end() && it->second.grace > 0.0f;
	}
}
