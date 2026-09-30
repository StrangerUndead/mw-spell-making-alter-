// Effect systems: registry, instance bookkeeping, event dispatch and the frame hook.
//
// Engine behaviour this file relies on (sources):
//  * TESActiveEffectApplyRemoveEvent carries the target and the ActiveEffect's usUniqueID
//    (CommonLib RE/T/TESActiveEffectApplyRemoveEvent.h); the ActiveEffect is found in
//    MagicTarget::GetActiveEffectList() (RE/M/MagicTarget.h, vfunc 07 on SE/AE).
//  * The frame hook is PlayerCharacter::Update, vfunc 0xAD of the PlayerCharacter primary
//    vtable (CommonLib RE/A/Actor.h: "Update(float a_delta) // 0AD"; the same slot is hooked by
//    ersh1/TrueDirectionalMovement src/Hooks.cpp, PlayerCharacter vtbl write_vfunc(0xAD)).
//  * Temporary actor-value modifiers (ActorValueOwner::ModActorValue(kTemporary, ...)) are what
//    vanilla value-modifier effects use and are saved with the actor (the KB's "Actor Value
//    Functions": ModActorValue changes current and base - here we only touch the magic modifier).

#include "Effects/EffectsInternal.h"

#include "Tests/InGameTests.h"

namespace LA::Effects::Internal
{
	namespace
	{
		// --- key index ----------------------------------------------------------------------
		std::unordered_map<std::string, const EffectDef*> g_byPascal;
		std::mutex                                         g_keyLock;
		std::unordered_map<RE::FormID, std::unique_ptr<EffectKey>> g_keys;
		std::unordered_set<RE::FormID>                     g_notOurs;

		std::unordered_map<std::string, Handler> g_handlers;

		// --- instances ------------------------------------------------------------------------
		std::shared_mutex                      g_instLock;
		std::vector<std::unique_ptr<Instance>> g_instances;
		std::vector<FrameFn>                   g_frameFns;
		std::atomic<bool>                      g_wake{ false };
		std::atomic<bool>                      g_pendingResume{ false };
		std::atomic<bool>                      g_installed{ false };

		std::mt19937& Engine()
		{
			static thread_local std::mt19937 engine{ std::random_device{}() };
			return engine;
		}

		std::optional<Range> RangeFromSuffix(std::string_view a_text)
		{
			if (a_text == "Self") {
				return Range::kSelf;
			}
			if (a_text == "Touch") {
				return Range::kTouch;
			}
			if (a_text == "Target") {
				return Range::kTarget;
			}
			return std::nullopt;
		}

		std::unique_ptr<EffectKey> ParseKey(std::string_view a_editorId)
		{
			// "LA_<Pascal>[_<Target>]_<Range>"; riders ("LA_Rider_*") and non-variants are skipped.
			if (!a_editorId.starts_with("LA_") || a_editorId.starts_with("LA_Rider_")) {
				return nullptr;
			}
			auto parts = Split(a_editorId.substr(3), '_');
			if (parts.size() < 2) {
				return nullptr;
			}
			const auto range = RangeFromSuffix(parts.back());
			if (!range) {
				return nullptr;
			}
			const auto it = g_byPascal.find(parts.front());
			if (it == g_byPascal.end()) {
				return nullptr;
			}
			auto key = std::make_unique<EffectKey>();
			key->def = it->second;
			key->id = it->second->id;
			key->range = *range;
			for (std::size_t i = 1; i + 1 < parts.size(); ++i) {
				if (!key->target.empty()) {
					key->target += "_";
				}
				key->target += parts[i];
			}
			const auto& catalog = State::Get().catalog;
			if (key->def->target == TargetKind::kAttribute) {
				for (int a = 0; a < static_cast<int>(kAttributeCount); ++a) {
					if (Catalog::AttributeName(a) == key->target) {
						key->attribute = a;
					}
				}
			} else if (key->def->target == TargetKind::kSkill) {
				for (int s = 0; s < static_cast<int>(kSkyrimSkillCount); ++s) {
					if (Catalog::SkyrimSkillName(s) == key->target) {
						key->skill = s;
					}
				}
				if (key->skill < 0) {
					for (const auto& mw : catalog.MwSkills()) {
						if (mw.name != key->target) {
							continue;
						}
						for (const auto& t : mw.targets) {
							if (t.skyrimSkill < 0 && !t.special.empty()) {
								key->special = t.special;
							}
						}
					}
				}
			}
			key->custom = key->def->tier == Tier::kCustom || key->def->target == TargetKind::kAttribute || !key->special.empty();
			return key;
		}

		// Captured on the event's thread while the ActiveEffect is guaranteed alive.
		struct Captured
		{
			RE::ActorHandle    target;
			RE::ActorHandle    caster;
			RE::FormID         targetId{ 0 };
			RE::FormID         casterId{ 0 };
			std::uint16_t      uid{ 0 };
			bool               applied{ false };
			RE::EffectSetting* mgef{ nullptr };
			RE::MagicItem*     spell{ nullptr };
			float              magnitude{ 0 };
			float              duration{ 0 };
		};

		RE::ActiveEffect* FindActiveEffect(RE::Actor* a_actor, std::uint16_t a_uid)
		{
			auto* magicTarget = a_actor ? a_actor->GetMagicTarget() : nullptr;
			auto* list = magicTarget ? magicTarget->GetActiveEffectList() : nullptr;
			if (!list) {
				return nullptr;
			}
			for (auto* effect : *list) {
				if (effect && effect->usUniqueID == a_uid) {
					return effect;
				}
			}
			return nullptr;
		}

		Instance* FindInstance(RE::FormID a_target, std::uint16_t a_uid)
		{
			for (auto& inst : g_instances) {
				if (inst->targetId == a_target && inst->uid == a_uid) {
					return inst.get();
				}
			}
			return nullptr;
		}

		std::unique_ptr<Instance> MakeInstance(const Captured& a_c, const EffectKey* a_key)
		{
			auto inst = std::make_unique<Instance>();
			inst->target = a_c.target;
			inst->caster = a_c.caster;
			inst->targetId = a_c.targetId;
			inst->casterId = a_c.casterId;
			inst->uid = a_c.uid;
			inst->key = a_key;
			inst->spell = a_c.spell;
			inst->mgef = a_c.mgef;
			inst->magnitude = a_c.magnitude;
			inst->duration = a_c.duration;
			return inst;
		}

		void StartInstance(std::unique_ptr<Instance> a_inst, const Handler& a_handler, bool a_resume)
		{
			Instance* raw = a_inst.get();
			raw->resumed = a_resume;
			{
				std::unique_lock lock(g_instLock);
				g_instances.push_back(std::move(a_inst));
			}
			if (a_resume) {
				if (a_handler.resume) {
					a_handler.resume(*raw);
				}
			} else if (a_handler.start) {
				a_handler.start(*raw);
			}
			logger::debug("effects: {} {} on {:08X} (uid {}, M {:.1f}, D {:.0f})", a_resume ? "resumed" : "started",
				raw->key->id, raw->targetId, raw->uid, raw->magnitude, raw->duration);
		}

		void FinishInstance(Instance* a_inst)
		{
			if (!a_inst) {
				return;
			}
			if (const auto* handler = FindHandler(*a_inst->key); handler && handler->finish) {
				handler->finish(*a_inst);
			}
			logger::debug("effects: finished {} on {:08X} (uid {})", a_inst->key->id, a_inst->targetId, a_inst->uid);
			std::unique_lock lock(g_instLock);
			std::erase_if(g_instances, [&](const auto& p) { return p.get() == a_inst; });
		}

		void HandleCaptured(const Captured& a_c);

		// Summon limit, Morrowind rule (one per type): when a summon from one of our spells
		// starts, dismiss the caster's older commanded actors of the same base creature.
		void DismissSameType(RE::ActorHandle a_caster, RE::ActiveEffect* a_newEffect)
		{
			SKSE::GetTaskInterface()->AddTask([a_caster, a_newEffect]() {
				auto caster = a_caster.get();
				auto* process = caster ? caster->GetActorRuntimeData().currentProcess : nullptr;
				auto* middle = process ? process->middleHigh : nullptr;
				if (!middle) {
					return;
				}
				RE::TESNPC* newBase = nullptr;
				for (auto& data : middle->commandedActors) {
					if (data.activeEffect == a_newEffect) {
						if (auto actor = data.commandedActor.get()) {
							newBase = actor->GetActorBase();
						}
					}
				}
				if (!newBase) {
					return;
				}
				std::vector<RE::ActiveEffect*> dismiss;
				for (auto& data : middle->commandedActors) {
					auto actor = data.commandedActor.get();
					if (data.activeEffect && data.activeEffect != a_newEffect && actor && actor->GetActorBase() == newBase) {
						dismiss.push_back(data.activeEffect);
					}
				}
				for (auto* effect : dismiss) {
					effect->Dispel(true);
				}
			});
		}

		bool IsSanguinare(const RE::MagicItem* a_spell)
		{
			static RE::FormID skyrim = 0;
			static RE::FormID dawnguard = 0;
			static std::once_flag once;
			std::call_once(once, [] {
				if (auto* f = Vanilla("Skyrim.esm|0x0B8780")) {  // DiseaseSanguinareVampiris
					skyrim = f->GetFormID();
				}
				if (auto* f = Vanilla("Dawnguard.esm|0x0037E9")) {  // TrapDiseaseSanguinareVampiris
					dawnguard = f->GetFormID();
				}
			});
			return a_spell && (a_spell->GetFormID() == skyrim || (dawnguard && a_spell->GetFormID() == dawnguard));
		}

		RE::BGSKeyword* BlessingKeyword()
		{
			static RE::BGSKeyword* keyword = Vanilla<RE::BGSKeyword>("Skyrim.esm|0x0FB98C");  // MagicBlessing
			return keyword;
		}

		class ApplyRemoveSink final : public RE::BSTEventSink<RE::TESActiveEffectApplyRemoveEvent>
		{
		public:
			static ApplyRemoveSink* Get()
			{
				static ApplyRemoveSink sink;
				return &sink;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESActiveEffectApplyRemoveEvent* a_event,
				RE::BSTEventSource<RE::TESActiveEffectApplyRemoveEvent>*) override
			{
				if (!a_event || !a_event->target || !State::Get().dataReady) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* target = a_event->target->As<RE::Actor>();
				if (!target) {
					return RE::BSEventNotifyControl::kContinue;
				}
				Captured c;
				c.target = target->GetHandle();
				c.targetId = target->GetFormID();
				c.uid = a_event->activeEffectUniqueID;
				c.applied = a_event->isApplied;
				if (a_event->caster) {
					if (auto* caster = a_event->caster->As<RE::Actor>()) {
						c.caster = caster->GetHandle();
						c.casterId = caster->GetFormID();
					}
				}
				if (c.applied) {
					auto* effect = FindActiveEffect(target, c.uid);
					if (!effect) {
						return RE::BSEventNotifyControl::kContinue;
					}
					c.mgef = effect->GetBaseObject();
					c.spell = effect->spell;
					c.magnitude = effect->magnitude;
					c.duration = effect->duration;
					if (!c.mgef) {
						return RE::BSEventNotifyControl::kContinue;
					}
					// Morrowind summon limit: one per type.
					if (State::Get().settings.summonLimit == 1 &&
						c.mgef->GetArchetype() == RE::EffectArchetypes::ArchetypeID::kSummonCreature &&
						IsCustomSlotSpell(c.spell) && c.caster) {
						DismissSameType(c.caster, effect);
					}
				}
				if (State::IsMainThread()) {
					HandleCaptured(c);
				} else {
					SKSE::GetTaskInterface()->AddTask([c]() { HandleCaptured(c); });
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		void HandleCaptured(const Captured& a_c)
		{
			if (!a_c.applied) {
				Instance* inst = nullptr;
				{
					std::shared_lock lock(g_instLock);
					inst = FindInstance(a_c.targetId, a_c.uid);
				}
				FinishInstance(inst);
				return;
			}
			auto target = a_c.target.get();
			if (!target) {
				return;
			}
			// Shrine prayer (blessing effects carry MagicBlessing): restores damaged attributes, as
			// praying did in Morrowind. VERIFY(in-game): every shrine blessing MGEF has the keyword.
			if (target->IsPlayerRef() && BlessingKeyword() && a_c.mgef->HasKeyword(BlessingKeyword())) {
				ClearLedger(target.get());
			}
			// Resist Corprus: M% chance to shrug off Sanguinare Vampiris when it is contracted.
			if (IsSanguinare(a_c.spell)) {
				const double chance = SumMagnitude(target.get(), "mw.resist_corprus_disease");
				if (chance > 0 && Mech::ChanceRoll(chance, Roll100())) {
					if (auto* disease = a_c.spell->As<RE::SpellItem>()) {
						target->RemoveSpell(disease);
						logger::info("effects: Resist Corprus shrugged off {} on {:08X}", disease->GetName(), a_c.targetId);
					}
				}
			}
			const auto* key = KeyOf(a_c.mgef);
			if (!key) {
				return;
			}
			const auto* handler = FindHandler(*key);
			if (!handler) {
				return;
			}
			if (g_pendingResume.load()) {
				return;  // the post-load scan picks this effect up from the active-effect list
			}
			{
				std::shared_lock lock(g_instLock);
				if (FindInstance(a_c.targetId, a_c.uid)) {
					return;
				}
			}
			StartInstance(MakeInstance(a_c, key), *handler, false);
		}

		void ResumeActor(RE::Actor* a_actor)
		{
			auto* magicTarget = a_actor ? a_actor->GetMagicTarget() : nullptr;
			auto* list = magicTarget ? magicTarget->GetActiveEffectList() : nullptr;
			if (!list) {
				return;
			}
			std::vector<Captured> found;
			for (auto* effect : *list) {
				if (!effect || effect->flags.any(RE::ActiveEffect::Flag::kDispelled)) {
					continue;
				}
				auto* mgef = effect->GetBaseObject();
				const auto* key = KeyOf(mgef);
				if (!key || !FindHandler(*key)) {
					continue;
				}
				Captured c;
				c.target = a_actor->GetHandle();
				c.targetId = a_actor->GetFormID();
				c.uid = effect->usUniqueID;
				c.applied = true;
				c.mgef = mgef;
				c.spell = effect->spell;
				c.magnitude = effect->magnitude;
				c.duration = effect->duration;
				if (auto caster = effect->caster.get()) {
					c.caster = effect->caster;
					c.casterId = caster->GetFormID();
				}
				found.push_back(c);
				if (!found.empty()) {
					found.back().duration = effect->duration;
				}
			}
			for (const auto& c : found) {
				const auto* key = KeyOf(c.mgef);
				auto        inst = MakeInstance(c, key);
				// elapsed is restored so per-second ticking resumes where it stopped
				if (auto* effect = FindActiveEffect(a_actor, c.uid)) {
					inst->elapsed = effect->elapsedSeconds;
				}
				StartInstance(std::move(inst), *FindHandler(*key), true);
			}
		}

		void ResumeAll()
		{
			{
				std::unique_lock lock(g_instLock);
				g_instances.clear();
			}
			if (auto* player = Player()) {
				ResumeActor(player);
			}
			if (auto* lists = RE::ProcessLists::GetSingleton()) {
				for (auto& handle : lists->highActorHandles) {
					if (auto actor = handle.get(); actor && !actor->IsPlayerRef()) {
						ResumeActor(actor.get());
					}
				}
			}
			OnLoadedStats();
			OnLoadedWorld();
			logger::info("effects: resumed {} active custom effects after load", InstanceCount());
		}

		void OnFrame(float a_delta)
		{
			if (g_pendingResume.exchange(false)) {
				ResumeAll();
			}
			if (g_instances.empty() && !g_wake.load()) {
				return;
			}
			// Snapshot: handlers may finish instances (dispel) while we iterate.
			std::vector<Instance*> live;
			{
				std::shared_lock lock(g_instLock);
				live.reserve(g_instances.size());
				for (auto& inst : g_instances) {
					live.push_back(inst.get());
				}
			}
			std::vector<Instance*> stale;
			for (auto* inst : live) {
				auto target = inst->target.get();
				inst->elapsed += a_delta;
				inst->validate += a_delta;
				if (!target) {
					if (inst->validate > 5.0f) {
						stale.push_back(inst);  // actor deleted or unloaded for good
					}
					continue;
				}
				// Stale check every 2 s: the effect must still be on the actor (an effect can end
				// without an event when its actor unloads or the effect list is cleared).
				if (inst->uid != 0 && inst->validate > 2.0f) {
					inst->validate = 0.0f;
					if (!FindActiveEffect(target.get(), inst->uid)) {
						stale.push_back(inst);
						continue;
					}
				}
				if (!target->Is3DLoaded()) {
					continue;
				}
				if (const auto* handler = FindHandler(*inst->key); handler && handler->update) {
					handler->update(*inst, a_delta);
				}
			}
			for (auto* inst : stale) {
				bool present = false;
				{
					std::shared_lock lock(g_instLock);
					for (auto& p : g_instances) {
						present = present || p.get() == inst;
					}
				}
				if (present) {
					FinishInstance(inst);
				}
			}
			for (auto fn : g_frameFns) {
				fn(a_delta);
			}
		}

		struct PlayerUpdateHook
		{
			static void thunk(RE::PlayerCharacter* a_this, float a_delta)
			{
				func(a_this, a_delta);
				if (g_installed.load()) {
					OnFrame(a_delta);
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		class StaticBoolCallback final : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			explicit StaticBoolCallback(std::function<void(std::optional<bool>)> a_fn) : _fn(std::move(a_fn)) {}
			void operator()(RE::BSScript::Variable a_result) override
			{
				std::optional<bool> value;
				if (a_result.IsBool()) {
					value = a_result.GetBool();
				}
				auto fn = std::move(_fn);
				SKSE::GetTaskInterface()->AddTask([fn, value]() {
					if (fn) {
						fn(value);
					}
				});
			}
			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

		private:
			std::function<void(std::optional<bool>)> _fn;
		};
	}

	// ---------------------------------------------------------------------------------------
	void BuildKeyIndex()
	{
		std::scoped_lock lock(g_keyLock);
		g_byPascal.clear();
		for (const auto& def : State::Get().catalog.All()) {
			g_byPascal[def.pascalName] = &def;
		}
		g_keys.clear();
		g_notOurs.clear();
	}

	const EffectKey* KeyOf(const RE::EffectSetting* a_mgef)
	{
		if (!a_mgef) {
			return nullptr;
		}
		const auto formId = a_mgef->GetFormID();
		std::scoped_lock lock(g_keyLock);
		if (g_notOurs.contains(formId)) {
			return nullptr;
		}
		if (auto it = g_keys.find(formId); it != g_keys.end()) {
			return it->second.get();
		}
		const auto editorId = State::Get().forms.EditorIdOf(formId);
		auto       key = editorId.empty() ? nullptr : ParseKey(editorId);
		if (!key) {
			g_notOurs.insert(formId);
			return nullptr;
		}
		auto* raw = key.get();
		g_keys.emplace(formId, std::move(key));
		return raw;
	}

	void Register(std::string_view a_key, Handler a_handler) { g_handlers[std::string(a_key)] = a_handler; }

	const Handler* FindHandler(const EffectKey& a_key)
	{
		if (!a_key.special.empty()) {
			if (auto it = g_handlers.find(a_key.id + "#" + a_key.special); it != g_handlers.end()) {
				return &it->second;
			}
			return nullptr;  // special targets never fall back to the plain skill handler
		}
		if (auto it = g_handlers.find(a_key.id); it != g_handlers.end()) {
			return &it->second;
		}
		return nullptr;
	}

	double SumMagnitude(const RE::Actor* a_actor, std::string_view a_id)
	{
		if (!a_actor) {
			return 0.0;
		}
		const auto id = a_actor->GetFormID();
		double     sum = 0.0;
		std::shared_lock lock(g_instLock);
		for (auto& inst : g_instances) {
			if (inst->targetId == id && inst->key->id == a_id) {
				sum += inst->magnitude;
			}
		}
		return sum;
	}

	double MaxMagnitude(const RE::Actor* a_actor, std::string_view a_id)
	{
		if (!a_actor) {
			return 0.0;
		}
		const auto id = a_actor->GetFormID();
		double     best = 0.0;
		std::shared_lock lock(g_instLock);
		for (auto& inst : g_instances) {
			if (inst->targetId == id && inst->key->id == a_id) {
				best = std::max(best, static_cast<double>(inst->magnitude));
			}
		}
		return best;
	}

	bool HasInstance(const RE::Actor* a_actor, std::string_view a_id)
	{
		if (!a_actor) {
			return false;
		}
		const auto id = a_actor->GetFormID();
		std::shared_lock lock(g_instLock);
		return std::ranges::any_of(g_instances, [&](const auto& inst) { return inst->targetId == id && inst->key->id == a_id; });
	}

	void ForEachOn(const RE::Actor* a_actor, const std::function<void(const Instance&)>& a_fn)
	{
		if (!a_actor) {
			return;
		}
		const auto id = a_actor->GetFormID();
		std::shared_lock lock(g_instLock);
		for (auto& inst : g_instances) {
			if (inst->targetId == id) {
				a_fn(*inst);
			}
		}
	}

	void ForEachInstance(const std::function<void(const Instance&)>& a_fn)
	{
		std::shared_lock lock(g_instLock);
		for (auto& inst : g_instances) {
			a_fn(*inst);
		}
	}

	std::size_t InstanceCount()
	{
		std::shared_lock lock(g_instLock);
		return g_instances.size();
	}

	void AddFrameCallback(FrameFn a_fn) { g_frameFns.push_back(a_fn); }
	void WakeFrame() { g_wake = true; }

	// --- synthetic instances (tests) ---------------------------------------------------------
	namespace
	{
		std::vector<std::unique_ptr<EffectKey>> g_syntheticKeys;
	}

	Instance* StartSynthetic(RE::Actor* a_target, RE::Actor* a_caster, std::string_view a_id, int a_sub, std::string_view a_special,
		float a_magnitude, float a_duration)
	{
		const auto* def = State::Get().catalog.Find(a_id);
		if (!def || !a_target) {
			return nullptr;
		}
		auto key = std::make_unique<EffectKey>();
		key->def = def;
		key->id = def->id;
		key->special = std::string(a_special);
		key->custom = true;
		if (def->target == TargetKind::kAttribute) {
			key->attribute = a_sub;
			key->target = std::string(Catalog::AttributeName(a_sub));
		} else if (def->target == TargetKind::kSkill && a_special.empty()) {
			key->skill = a_sub;
			key->target = std::string(Catalog::SkyrimSkillName(a_sub));
		}
		const auto* handler = FindHandler(*key);
		if (!handler) {
			return nullptr;
		}
		Captured c;
		c.target = a_target->GetHandle();
		c.targetId = a_target->GetFormID();
		c.caster = a_caster ? a_caster->GetHandle() : RE::ActorHandle{};
		c.casterId = a_caster ? a_caster->GetFormID() : 0;
		c.uid = 0;  // synthetic: never validated against the active-effect list
		c.magnitude = a_magnitude;
		c.duration = a_duration;
		auto inst = MakeInstance(c, key.get());
		auto* raw = inst.get();
		g_syntheticKeys.push_back(std::move(key));
		StartInstance(std::move(inst), *handler, false);
		return raw;
	}

	void FinishSynthetic(Instance* a_instance) { FinishInstance(a_instance); }

	void TickSynthetic(Instance* a_instance, float a_seconds)
	{
		if (!a_instance) {
			return;
		}
		if (const auto* handler = FindHandler(*a_instance->key); handler && handler->update) {
			// in 0.25 s steps, as frames would
			for (float t = 0.0f; t < a_seconds - 1e-4f; t += 0.25f) {
				a_instance->elapsed += 0.25f;
				handler->update(*a_instance, 0.25f);
			}
		}
	}

	// --- helpers ----------------------------------------------------------------------------
	RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

	int Roll100() { return std::uniform_int_distribution<int>(1, 100)(Engine()); }
	int Roll0to99() { return std::uniform_int_distribution<int>(0, 99)(Engine()); }
	float RandomFloat() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(Engine()); }

	void ModAV(RE::Actor* a_actor, RE::ActorValue a_av, float a_delta)
	{
		if (!a_actor || a_av == RE::ActorValue::kNone || a_delta == 0.0f) {
			return;
		}
		a_actor->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, a_av, a_delta);
	}

	void NudgeSpeed(RE::Actor* a_actor)
	{
		// Movement speed is recalculated when encumbrance changes; the well-known nudge is a
		// +1/-1 carry-weight change (the outline's "carry-weight nudge Skyrim needs").
		ModAV(a_actor, RE::ActorValue::kCarryWeight, 1.0f);
		ModAV(a_actor, RE::ActorValue::kCarryWeight, -1.0f);
	}

	RE::ActorValue SkillAV(int a_skill)
	{
		if (a_skill < 0 || a_skill >= static_cast<int>(kSkyrimSkillCount)) {
			return RE::ActorValue::kNone;
		}
		return static_cast<RE::ActorValue>(6 + a_skill);
	}

	RE::ActorValue SchoolSkillAV(School a_school)
	{
		switch (a_school) {
		case School::kAlteration:
			return RE::ActorValue::kAlteration;
		case School::kConjuration:
			return RE::ActorValue::kConjuration;
		case School::kDestruction:
			return RE::ActorValue::kDestruction;
		case School::kIllusion:
			return RE::ActorValue::kIllusion;
		case School::kRestoration:
			return RE::ActorValue::kRestoration;
		}
		return RE::ActorValue::kNone;
	}

	RE::TESForm* Vanilla(std::string_view a_ref) { return FormMap::Resolve(ParseFormRef(a_ref)); }

	float Distance(const RE::NiPoint3& a_lhs, const RE::NiPoint3& a_rhs) { return a_lhs.GetDistance(a_rhs); }

	bool IsCustomSlotSpell(const RE::MagicItem* a_spell)
	{
		const auto* spell = a_spell ? a_spell->As<RE::SpellItem>() : nullptr;
		if (!spell) {
			return false;
		}
		const auto& index = State::Get().slotIndex;
		return index.contains(spell);
	}

	void Notify(const std::string& a_text)
	{
		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm || a_text.empty()) {
			return;
		}
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
		auto* args = RE::MakeFunctionArguments(RE::BSFixedString(a_text));
		vm->DispatchStaticCall("Debug", "Notification", args, callback);
	}

	float StaminaRatio(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return 1.0f;
		}
		const float max = a_actor->GetActorValueMax(RE::ActorValue::kStamina);
		const float cur = a_actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kStamina);
		return max > 0.0f ? std::clamp(cur / max, 0.0f, 1.0f) : 1.0f;
	}

	void ApplyAVDeltas(RE::Actor* a_actor, std::vector<std::pair<RE::ActorValue, float>>& a_list,
		const std::vector<std::pair<RE::ActorValue, float>>& a_deltas)
	{
		bool speed = false;
		for (const auto& [av, delta] : a_deltas) {
			ModAV(a_actor, av, delta);
			a_list.emplace_back(av, delta);
			speed = speed || av == RE::ActorValue::kSpeedMult;
		}
		if (speed) {
			NudgeSpeed(a_actor);
		}
	}

	void RevertAVDeltas(RE::Actor* a_actor, std::vector<std::pair<RE::ActorValue, float>>& a_list)
	{
		bool speed = false;
		if (a_actor) {
			for (const auto& [av, delta] : a_list) {
				ModAV(a_actor, av, -delta);
				speed = speed || av == RE::ActorValue::kSpeedMult;
			}
			if (speed) {
				NudgeSpeed(a_actor);
			}
		}
		a_list.clear();
	}

	void CallStaticBool(const char* a_class, const char* a_function, std::function<void(std::optional<bool>)> a_fn)
	{
		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm) {
			a_fn(std::nullopt);
			return;
		}
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{ new StaticBoolCallback(a_fn) };
		auto* args = RE::MakeFunctionArguments();
		if (!vm->DispatchStaticCall(a_class, a_function, args, callback)) {
			a_fn(std::nullopt);
		}
	}
}

// =============================================================================================
// Public interface (Effects/EffectSystems.h)
// =============================================================================================
namespace LA::Effects
{
	using namespace Internal;

	void Install()
	{
		if (g_installed.load()) {
			return;
		}
		BuildKeyIndex();
		InstallStats();
		InstallMovement();
		InstallCombat();
		InstallMagic();
		InstallWorld();

		if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
			holder->AddEventSink<RE::TESActiveEffectApplyRemoveEvent>(ApplyRemoveSink::Get());
		}
		stl::write_vfunc<PlayerUpdateHook>(RE::VTABLE_PlayerCharacter[0], 0xAD);
		logger::info("effects: frame hook on PlayerCharacter::Update (vtbl 0xAD)");

		RegisterEffectTests();
		g_installed = true;
		g_pendingResume = true;  // an already running game (reload of the DLL is impossible, but a
		                         // save may already be loaded when data loads on a fast boot)
		logger::info("effects: installed ({} handlers)", g_handlers.size());
	}

	void OnGameLoaded()
	{
		// The actual scan runs on the next PlayerCharacter::Update so the loaded cell's actors
		// are in the high process list.
		g_pendingResume = true;
	}

	void Revert()
	{
		g_pendingResume = true;
		RevertMovement();
		RevertWorld();
		std::unique_lock lock(g_instLock);
		g_instances.clear();
		g_wake = false;
	}

	void PrepareForUninstall()
	{
		// Finish every instance now: this removes our actor-value modifiers, bound armor, GMST
		// changes and controllers. Persistent attribute damage is repaired first.
		if (auto* player = Player()) {
			ClearLedger(player);
		}
		std::vector<Instance*> all;
		{
			std::shared_lock lock(g_instLock);
			for (auto& inst : g_instances) {
				all.push_back(inst.get());
			}
		}
		for (auto* inst : all) {
			FinishInstance(inst);
		}
		RevertMovement();
		RevertWorld();
	}
}
