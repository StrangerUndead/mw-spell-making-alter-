// Cast router (OUTLINE "Casting behavior", component 7): linked sub-spells for mixed ranges,
// Morrowind area on impact, min-max magnitude rolls, Lock/Open on struck doors and containers,
// the optional casting-failure module (with Sound and Silence), the touch reach and the Morrowind
// summon limit.
//
// Hooks (all virtual-function hooks on CommonLib VTABLE constants, CommonLib 10.1.0 headers):
//  * ActorMagicCaster::SpellCast, vfunc 0x09 of VTABLE_ActorMagicCaster[0]
//    (RE/A/ActorMagicCaster.h "SpellCast(bool a_doCast, std::uint32_t, MagicItem*) // 09").
//    Runs once per release of a hand caster; a dual cast is released by one caster whose
//    GetIsDualCasting() is true. Failure checks run before the original (not calling it = the
//    spell never fires), sub-spells are cast after it.
//    VERIFY(in-game): called once per release with a_doCast true, including dual casts.
//  * ActorMagicCaster::CheckCast, vfunc 0x0A (Silence: the engine refuses to start the cast).
//  * ActiveEffect::AdjustForPerks, vfunc 0x00 of every ActiveEffect subclass vtable
//    (RE/A/ActiveEffect.h "AdjustForPerks(Actor*, MagicTarget*) // 00"): the rolled magnitude
//    is written before Skyrim's perk multipliers, so Augmented Flames and dual casting stack on
//    it (OUTLINE "Magnitude rolls").
//  * Projectile::AddImpact, vfunc 0xBD on MissileProjectile and BeamProjectile
//    (RE/P/Projectile.h "AddImpact(TESObjectREFR*, const NiPoint3& a_targetLoc, ...) //
//    SE/AE 0xBD"): the impact point and the struck reference for area and Lock/Open.
//
// Area application: each extra target gets the spell's area entries through
// MagicTarget::AddTarget with AddTargetData.areaTarget set, the same entry point explosions
// use (RE/M/MagicTarget.h), so resistances, hostility and hit art stay the engine's.

#include "Casting/CastRouter.h"

#include "Effects/EffectsInternal.h"
#include "Tests/InGameTests.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace LA::Casting
{
	namespace
	{
		using namespace LA::Effects::Internal;

		// ---- spell / plan lookup ----------------------------------------------------------
		// Shared lock for readers off the main thread (State.h threading contract).
		struct ReadGuard
		{
			ReadGuard() :
				lock(State::Get().lock, std::defer_lock)
			{
				if (!State::IsMainThread()) {
					lock.lock();
				}
			}
			std::shared_lock<std::shared_mutex> lock;
		};

		struct PlanRef
		{
			const CustomSpell*  spell{ nullptr };
			std::size_t         planIndex{ 0 };  // 0 = equipped spell, n = subs[n-1]
			const RE::SpellItem* record{ nullptr };
		};

		// FindBySpell takes its own shared lock; callers must not hold State::lock.
		std::optional<PlanRef> Resolve(const RE::MagicItem* a_item)
		{
			const auto* record = a_item ? a_item->As<RE::SpellItem>() : nullptr;
			if (!record || !IsCustomSlotSpell(record)) {
				return std::nullopt;
			}
			const auto* custom = State::Get().FindBySpell(record);
			if (!custom) {
				return std::nullopt;
			}
			PlanRef ref{ custom, 0, record };
			if (custom->primary != record) {
				const auto it = std::ranges::find(custom->subs, record);
				if (it == custom->subs.end()) {
					return std::nullopt;
				}
				ref.planIndex = static_cast<std::size_t>(std::distance(custom->subs.begin(), it)) + 1;
			}
			if (ref.planIndex >= custom->plan.spells.size()) {
				return std::nullopt;
			}
			return ref;
		}

		// Plan entry of the record's a_effect. The compiler writes one record effect per plan entry,
		// in order (an unresolved rider or effect becomes an inert placeholder), so the record index
		// is the entry index.
		const PlannedEntry* EntryOf(const PlanRef& a_ref, const RE::Effect* a_effect)
		{
			const auto& entries = a_ref.spell->plan.spells[a_ref.planIndex].entries;
			const auto& effects = a_ref.record->effects;
			for (std::uint32_t i = 0; i < effects.size() && i < entries.size(); ++i) {
				if (effects[i] == a_effect) {
					return &entries[i];
				}
			}
			return nullptr;
		}

		const SpellEffect* SourceOf(const PlanRef& a_ref, const PlannedEntry& a_entry)
		{
			const auto& effects = a_ref.spell->def.effects;
			return a_entry.sourceIndex < effects.size() ? &effects[a_entry.sourceIndex] : nullptr;
		}

		// ---- magnitude rolls ------------------------------------------------------------------
		// One roll per (target, spell record, definition effect) so an effect, its riders and its
		// attribute stats all land with the same number (Morrowind rolls once per target).
		struct RollKey
		{
			const void*          target;
			const RE::MagicItem* spell;
			std::size_t          source;
			bool operator==(const RollKey&) const = default;
		};
		struct Roll
		{
			RollKey                               key;
			double                                landed;
			std::chrono::steady_clock::time_point when;
		};
		std::mutex        g_rollLock;
		std::vector<Roll> g_rolls;

		double LandedFor(const void* a_target, const PlanRef& a_ref, const PlannedEntry& a_entry, const SpellEffect& a_source,
			const EffectDef& a_def)
		{
			const auto now = std::chrono::steady_clock::now();
			const RollKey key{ a_target, a_ref.record, a_entry.sourceIndex };
			std::scoped_lock lock(g_rollLock);
			std::erase_if(g_rolls, [&](const Roll& r) { return now - r.when > std::chrono::seconds(2); });
			for (const auto& r : g_rolls) {
				if (r.key == key) {
					return r.landed;
				}
			}
			static const Mech::Rng rng = Mech::DefaultRng();
			const double landed = Mech::LandedMagnitude(a_def, a_source, State::Get().settings.minMaxRolls, rng);
			g_rolls.push_back({ key, landed, now });
			return landed;
		}

		// VERIFY(in-game): ActiveEffect::magnitude holds the item magnitude (x power) when
		// AdjustForPerks runs, and the value-modifier archetypes apply it only after (Start).
		void PreAdjust(RE::ActiveEffect* a_effect, RE::MagicTarget* a_target)
		{
			if (!a_effect || !a_effect->effect || !IsCustomSlotSpell(a_effect->spell) || !State::Get().dataReady) {
				return;
			}
			const auto ref = Resolve(a_effect->spell);
			if (!ref) {
				return;
			}
			ReadGuard guard;
			const auto* entry = EntryOf(*ref, a_effect->effect);
			const auto* source = entry ? SourceOf(*ref, *entry) : nullptr;
			const auto* def = source ? State::Get().catalog.Find(source->effectId) : nullptr;
			if (!def || !def->hasMagnitude || source->minMag >= source->maxMag) {
				return;  // nothing to roll: the item already carries the magnitude
			}
			const double landed = LandedFor(a_target, *ref, *entry, *source, *def);
			const double scale = std::max(0.0, landed) / std::max<std::uint16_t>(1, source->maxMag);
			a_effect->magnitude = static_cast<float>(a_effect->magnitude * scale);
		}

		template <std::size_t I>
		struct AdjustForPerksHook
		{
			static void thunk(RE::ActiveEffect* a_this, RE::Actor* a_caster, RE::MagicTarget* a_target)
			{
				PreAdjust(a_this, a_target);
				func(a_this, a_caster, a_target);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// Every ActiveEffect class a crafted spell can create: the archetypes of data/effects/*.json
		// (Script, ValueModifier, SummonCreature, BoundWeapon, Absorb, DetectLife, Cloak, Calm,
		// Demoralize, Frenzy, Light, Rally, CureDisease, Invisibility, Paralysis, TurnUndead, SoulTrap,
		// Reanimate, Banish, CommandSummoned) plus the rider classes (Stagger, the value-modifier
		// family). A class that does not
		// override AdjustForPerks still has its own vtable slot, so each is patched separately.
		constexpr std::array kEffectVtables{
			RE::VTABLE_ScriptEffect[0], RE::VTABLE_ValueModifierEffect[0], RE::VTABLE_PeakValueModifierEffect[0],
			RE::VTABLE_DualValueModifierEffect[0], RE::VTABLE_AccumulatingValueModifierEffect[0], RE::VTABLE_AbsorbEffect[0],
			RE::VTABLE_SummonCreatureEffect[0], RE::VTABLE_BoundItemEffect[0], RE::VTABLE_DetectLifeEffect[0], RE::VTABLE_CloakEffect[0],
			RE::VTABLE_CalmEffect[0], RE::VTABLE_DemoralizeEffect[0], RE::VTABLE_FrenzyEffect[0], RE::VTABLE_RallyEffect[0],
			RE::VTABLE_LightEffect[0], RE::VTABLE_CureEffect[0], RE::VTABLE_InvisibilityEffect[0], RE::VTABLE_ParalysisEffect[0],
			RE::VTABLE_TurnUndeadEffect[0], RE::VTABLE_SoulTrapEffect[0], RE::VTABLE_ReanimateEffect[0], RE::VTABLE_BanishEffect[0],
			RE::VTABLE_CommandSummonedEffect[0], RE::VTABLE_StaggerEffect[0]
		};

		template <std::size_t... Is>
		void InstallAdjustHooks(std::index_sequence<Is...>)
		{
			(stl::write_vfunc<AdjustForPerksHook<Is>>(kEffectVtables[Is], 0x0), ...);
		}

		// ---- per-cast context and linked spells ------------------------------------------------
		struct LastCast
		{
			RE::FormID                            caster{ 0 };
			const RE::MagicItem*                  spell{ nullptr };
			std::chrono::steady_clock::time_point when;
			bool                                  failed{ false };
		};
		std::mutex            g_castLock;
		std::vector<LastCast> g_lastCasts;

		struct Release
		{
			bool first{ true };
			bool failed{ false };
		};

		// A release seen twice within 150 ms is the same release (both hands of a dual cast): the
		// failure roll is made once and both hands follow it.
		Release Decide(RE::Actor* a_caster, const RE::MagicItem* a_spell, const std::function<bool()>& a_roll)
		{
			const auto now = std::chrono::steady_clock::now();
			{
				std::scoped_lock lock(g_castLock);
				std::erase_if(g_lastCasts, [&](const LastCast& c) { return now - c.when > std::chrono::milliseconds(150); });
				for (const auto& c : g_lastCasts) {
					if (c.caster == a_caster->GetFormID() && c.spell == a_spell) {
						return { false, c.failed };
					}
				}
			}
			const bool failed = a_roll();
			std::scoped_lock lock(g_castLock);
			g_lastCasts.push_back({ a_caster->GetFormID(), a_spell, now, failed });
			return { true, failed };
		}

		// data/generated/variants.json "linkedSpells": tome spells split by range (primary ->
		// linked sub-spells cast on release).
		std::unordered_map<const RE::MagicItem*, std::vector<RE::SpellItem*>> g_linked;

		void LoadLinkedSpells()
		{
			const auto path = State::Get().dataDir / "variants.json";
			std::ifstream file(path);
			if (!file) {
				logger::info("casting: {} not found; no linked tome spells", path.string());
				return;
			}
			try {
				const auto root = nlohmann::json::parse(file);
				const auto it = root.find("linkedSpells");
				if (it == root.end() || !it->is_object()) {
					return;
				}
				for (const auto& [primaryId, linked] : it->items()) {
					auto* primary = State::Get().forms.Get<RE::SpellItem>(primaryId);
					if (!primary || !linked.is_array()) {
						continue;
					}
					for (const auto& id : linked) {
						if (auto* sub = id.is_string() ? State::Get().forms.Get<RE::SpellItem>(id.get<std::string>()) : nullptr) {
							g_linked[primary].push_back(sub);
						}
					}
				}
				logger::info("casting: {} linked tome spells", g_linked.size());
			} catch (const std::exception& e) {
				logger::warn("casting: variants.json: {}", e.what());
			}
		}

		float DualEffectiveness()
		{
			auto* settings = RE::GameSettingCollection::GetSingleton();
			auto* setting = settings ? settings->GetSetting("fMagicDualCastingEffectivenessBase") : nullptr;
			return setting ? setting->GetFloat() : 2.2f;
		}

		// Casts a linked spell instantly: Self on the caster, aimed ones along the caster's aim
		// (no explicit target: the instant caster fires from the caster in its facing direction).
		void CastLinked(RE::Actor* a_caster, RE::SpellItem* a_spell, float a_effectiveness)
		{
			auto* caster = a_caster ? a_caster->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant) : nullptr;
			if (!caster || !a_spell) {
				return;
			}
			const bool self = a_spell->data.delivery == RE::MagicSystem::Delivery::kSelf;
			caster->CastSpellImmediate(a_spell, false, self ? a_caster : nullptr, a_effectiveness, false, 0.0f, a_caster);
		}

		// ---- casting failure ---------------------------------------------------------------------
		void Fizzle(RE::ActorMagicCaster* a_caster, RE::Actor* a_actor, RE::MagicItem* a_spell)
		{
			// Morrowind: the magicka is spent, the spell fizzles, no experience.
			// VERIFY(in-game): currentSpellCost holds the release cost (dual cast included).
			float cost = a_caster->currentSpellCost;
			if (cost <= 0.0f) {
				cost = a_spell->CalculateMagickaCost(a_actor);
			}
			a_actor->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kMagicka, cost);
			a_caster->InterruptCast(false);
			RE::PlaySound("MAGFailSD");  // Skyrim.esm|0x03D0D3
			if (a_actor->IsPlayerRef()) {
				Notify(LocalText("$LA_Msg_SpellFizzled", "You failed casting the spell."));
			}
		}

		bool FailureApplies(RE::Actor* a_actor)
		{
			const auto& settings = State::Get().settings;
			return settings.castingFailure && (a_actor->IsPlayerRef() || settings.npcFailure);
		}

		bool RollFails(RE::Actor* a_actor, const std::vector<SpellEffect>* a_effects, const RE::MagicItem* a_spell)
		{
			if (a_effects && FailureApplies(a_actor)) {
				const int chance = CastingChance(a_actor, *a_effects, false);
				return chance >= 0 && Roll100() > chance;
			}
			// Without the module, Sound makes spells fizzle M % of the time.
			if (a_spell->GetSpellType() == RE::MagicSystem::SpellType::kSpell) {
				const double sound = Effects::SoundMagnitude(a_actor);
				return sound > 0.0 && Mech::ChanceRoll(sound, Roll100());
			}
			return false;
		}

		struct SpellCastHook
		{
			static void thunk(RE::ActorMagicCaster* a_this, bool a_doCast, std::uint32_t a_arg2, RE::MagicItem* a_spell)
			{
				auto* actor = a_this ? a_this->actor : nullptr;
				if (!a_doCast || !a_spell || !actor || !State::Get().dataReady) {
					return func(a_this, a_doCast, a_arg2, a_spell);
				}
				const auto ref = Resolve(a_spell);
				const bool primary = ref && ref->planIndex == 0;
				const CustomSpell* custom = primary ? ref->spell : nullptr;
				std::vector<RE::SpellItem*> subs;
				std::vector<SpellEffect>    effects;
				if (custom) {
					ReadGuard guard;
					subs = custom->subs;
					effects = custom->def.effects;
				}

				const auto release = Decide(actor, a_spell, [&]() { return RollFails(actor, custom ? &effects : nullptr, a_spell); });
				if (release.failed) {
					if (release.first) {
						Fizzle(a_this, actor, a_spell);
					} else {
						a_this->InterruptCast(false);  // the other hand of a failed dual cast
					}
					return;
				}
				func(a_this, a_doCast, a_arg2, a_spell);

				const auto linked = g_linked.find(a_spell);
				if ((!custom && linked == g_linked.end()) || !release.first) {
					return;
				}
				// Sub-spells inherit the release: dual casting scales them like the primary (the
				// perk multipliers apply to them anyway through AdjustForPerks on the same caster).
				const float effectiveness = a_this->GetIsDualCasting() ? DualEffectiveness() : 1.0f;
				if (linked != g_linked.end()) {
					subs.insert(subs.end(), linked->second.begin(), linked->second.end());
				}
				if (subs.empty()) {
					return;
				}
				// Cast from the task queue (next frame), outside the hand caster's own SpellCast:
				// the instant caster is re-entered from a clean stack.
				std::vector<RE::FormID> ids;
				for (auto* sub : subs) {
					ids.push_back(sub->GetFormID());
				}
				const auto handle = actor->GetHandle();
				SKSE::GetTaskInterface()->AddTask([handle, ids, effectiveness]() {
					auto casterPtr = handle.get();
					if (!casterPtr || casterPtr->IsDead()) {
						return;
					}
					for (auto id : ids) {
						CastLinked(casterPtr.get(), RE::TESForm::LookupByID<RE::SpellItem>(id), effectiveness);
					}
				});
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct CheckCastHook
		{
			static bool thunk(RE::ActorMagicCaster* a_this, RE::MagicItem* a_spell, bool a_dualCast, float* a_effectStrength,
				RE::MagicSystem::CannotCastReason* a_reason, bool a_useBaseValueForCost)
			{
				auto* actor = a_this ? a_this->actor : nullptr;
				if (actor && a_spell && a_spell->GetSpellType() == RE::MagicSystem::SpellType::kSpell && Effects::IsSilenced(actor)) {
					// Silence: spells can't be cast; powers, shouts, scrolls and staves still work.
					// NPC combat AI picks another action when CheckCast fails.
					if (a_reason) {
						*a_reason = RE::MagicSystem::CannotCastReason::kCustomReasonNoStart;
					}
					if (actor->IsPlayerRef()) {
						static std::chrono::steady_clock::time_point last{};
						const auto now = std::chrono::steady_clock::now();
						if (now - last > std::chrono::seconds(2)) {
							last = now;
							Notify(LocalText("$LA_Msg_Silenced", "You are silenced."));
						}
					}
					return false;
				}
				return func(a_this, a_spell, a_dualCast, a_effectStrength, a_reason, a_useBaseValueForCost);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// ---- impact: area and Lock/Open -------------------------------------------------------------
		struct Impact
		{
			RE::ActorHandle          caster;
			RE::FormID               spell{ 0 };
			RE::NiPoint3             point;
			RE::ObjectRefHandle      direct;
			float                    power{ 1.0f };
			bool                     dual{ false };
		};

		std::mutex                  g_impactLock;
		std::vector<std::pair<RE::FormID, std::chrono::steady_clock::time_point>> g_impacted;

		bool FirstImpact(RE::Projectile* a_projectile)
		{
			const auto now = std::chrono::steady_clock::now();
			std::scoped_lock lock(g_impactLock);
			std::erase_if(g_impacted, [&](const auto& p) { return now - p.second > std::chrono::seconds(5); });
			for (const auto& [id, when] : g_impacted) {
				if (id == a_projectile->GetFormID()) {
					return false;
				}
			}
			g_impacted.emplace_back(a_projectile->GetFormID(), now);
			return true;
		}

		void ResolveImpact(const Impact& a_impact)
		{
			auto  casterPtr = a_impact.caster.get();
			auto* caster = casterPtr.get();
			auto* record = RE::TESForm::LookupByID<RE::SpellItem>(a_impact.spell);
			const auto ref = Resolve(record);
			if (!caster || !ref) {
				return;
			}
			auto directPtr = a_impact.direct.get();
			auto* direct = directPtr.get();

			// Open / Lock on a struck door or container.
			if (direct && !direct->As<RE::Actor>()) {
				for (const auto& entry : ref->spell->plan.spells[ref->planIndex].entries) {
					if (!entry.riderId.empty() || (entry.effectId != "mw.open" && entry.effectId != "mw.lock")) {
						continue;
					}
					const auto* source = SourceOf(*ref, entry);
					const auto* def = State::Get().catalog.Find(entry.effectId);
					if (source && def) {
						static const Mech::Rng rng = Mech::DefaultRng();
						const double magnitude = Mech::LandedMagnitude(*def, *source, State::Get().settings.minMaxRolls, rng);
						ApplyLockOpen(caster, direct, entry.effectId, magnitude);
					}
				}
			}
			ResolveArea(caster, *ref->spell, ref->planIndex, a_impact.point, direct);
		}

		template <std::size_t I>
		struct AddImpactHook
		{
			static void thunk(RE::Projectile* a_this, RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_targetLoc, const RE::NiPoint3& a_velocity,
				RE::hkpCollidable* a_collidable, std::int32_t a_arg6, std::uint32_t a_arg7)
			{
				func(a_this, a_ref, a_targetLoc, a_velocity, a_collidable, a_arg6, a_arg7);
				if (!a_this || !State::Get().dataReady) {
					return;
				}
				// VERIFY(in-game): AddImpact runs for spell missiles and beams on actors, statics, doors
				// and containers; the first impact per projectile is used.
				auto& data = a_this->GetProjectileRuntimeData();
				if (!IsCustomSlotSpell(data.spell) || !FirstImpact(a_this)) {
					return;
				}
				auto shooter = data.shooter.get();
				auto* actor = shooter ? shooter->As<RE::Actor>() : nullptr;
				if (!actor) {
					return;
				}
				Impact impact;
				impact.caster = actor->GetHandle();
				impact.spell = data.spell->GetFormID();
				impact.point = a_targetLoc;
				impact.direct = a_ref ? a_ref->GetHandle() : RE::ObjectRefHandle{};
				impact.power = data.power > 0.0f ? data.power : 1.0f;
				impact.dual = data.flags.any(RE::Projectile::Flags::kIsDual);
				SKSE::GetTaskInterface()->AddTask([impact]() { ResolveImpact(impact); });
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// ---- area helpers ----------------------------------------------------------------------------
		bool LineOfSight(const RE::NiPoint3& a_from, RE::Actor* a_actor)
		{
			auto* cell = a_actor->GetParentCell();
			auto* world = cell ? cell->GetbhkWorld() : nullptr;
			if (!world) {
				return true;
			}
			const float scale = RE::bhkWorld::GetWorldScale();
			RE::NiPoint3 to = a_actor->GetPosition();
			to.z += 70.0f;  // chest height
			RE::bhkPickData pick;
			pick.rayInput.from = RE::hkVector4(a_from.x * scale, a_from.y * scale, (a_from.z + 16.0f) * scale, 0.0f);
			pick.rayInput.to = RE::hkVector4(to.x * scale, to.y * scale, to.z * scale, 0.0f);
			pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kLOS);
			// VERIFY(in-game): walls block, the target's own body does not.
			if (!world->PickObject(pick) || !pick.rayOutput.HasHit()) {
				return true;
			}
			auto* hit = RE::TESHavokUtilities::FindCollidableRef(*pick.rayOutput.rootCollidable);
			return hit == a_actor || pick.rayOutput.hitFraction > 0.97f;
		}

		void SpawnAreaVisual(RE::Actor* a_caster, const CustomSpell& a_spell, std::size_t a_planIndex, const RE::NiPoint3& a_point,
			std::uint16_t a_feet)
		{
			School school = a_spell.plan.school;
			for (const auto& entry : a_spell.plan.spells[a_planIndex].entries) {
				if (entry.area > 0 && entry.riderId.empty()) {
					if (const auto* def = State::Get().catalog.Find(entry.effectId)) {
						school = def->school;
					}
					break;
				}
			}
			static constexpr std::array kSchoolNames{ "Alteration", "Conjuration", "Destruction", "Illusion", "Restoration" };
			const char* size = a_feet <= 10 ? "Small" : (a_feet <= 25 ? "Medium" : "Large");
			const auto  editorId = fmt::format("LA_AreaFX_{}_{}", kSchoolNames[static_cast<std::size_t>(school)], size);
			auto*       explosion = State::Get().forms.Get<RE::BGSExplosion>(editorId);
			auto*       handler = RE::TESDataHandler::GetSingleton();
			if (!explosion || !handler || !a_caster) {
				return;
			}
			// A damage-free explosion placed at the impact point (the Papyrus PlaceAtMe path).
			// VERIFY(in-game): creating a reference of a BGSExplosion detonates it (as PlaceAtMe does).
			handler->CreateReferenceAtLocation(explosion, a_point, RE::NiPoint3{}, a_caster->GetParentCell(), a_caster->GetWorldspace(),
				nullptr, nullptr, RE::ObjectRefHandle(), false, true);
		}

		// ---- touch reach and summon limit -------------------------------------------------------------
		void ApplyTouchReach()
		{
			auto* projectile = State::Get().forms.Get<RE::BGSProjectile>("LA_TouchProjectile");
			const auto reach = static_cast<float>(State::Get().settings.touchReach);
			if (projectile && reach > 0.0f && projectile->data.range != reach) {
				projectile->data.range = reach;
				logger::info("casting: touch reach {} units", reach);
			}
		}

		void ApplySummonPerk()
		{
			// Morrowind summon limit: the per-type rule is enforced when a summon starts
			// (EffectRegistry.cpp); lifting Skyrim's total needs a perk with Mod Commanded Actor
			// Limit, which LostArt.esp may provide as LA_Perk_SummonLimit.
			auto* perk = State::Get().forms.Get<RE::BGSPerk>("LA_Perk_SummonLimit");
			auto* player = Player();
			if (!perk || !player) {
				return;
			}
			const bool want = State::Get().settings.summonLimit == 1;
			if (want != player->HasPerk(perk)) {
				want ? player->AddPerk(perk) : player->RemovePerk(perk);
			}
		}

		float g_settingsTimer{ 0.0f };
		void CastingFrame(float a_delta)
		{
			g_settingsTimer -= a_delta;
			if (g_settingsTimer > 0.0f) {
				return;
			}
			g_settingsTimer = 5.0f;  // settings can change from the MCM at any time
			ApplyTouchReach();
			ApplySummonPerk();
		}

		void RegisterCastingTests();
	}

	// =========================================================================================
	void Install()
	{
		static bool installed = false;
		if (installed) {
			return;
		}
		installed = true;

		REL::Relocation<std::uintptr_t> casterVtbl{ RE::VTABLE_ActorMagicCaster[0] };
		SpellCastHook::func = casterVtbl.write_vfunc(0x09, SpellCastHook::thunk);
		CheckCastHook::func = casterVtbl.write_vfunc(0x0A, CheckCastHook::thunk);
		InstallAdjustHooks(std::make_index_sequence<kEffectVtables.size()>{});
		stl::write_vfunc<AddImpactHook<0>>(RE::VTABLE_MissileProjectile[0], 0xBD);
		stl::write_vfunc<AddImpactHook<1>>(RE::VTABLE_BeamProjectile[0], 0xBD);
		logger::info("casting: hooks installed (SpellCast, CheckCast, {} AdjustForPerks, AddImpact x2)", kEffectVtables.size());

		LoadLinkedSpells();
		ApplyTouchReach();
		AddFrameCallback(CastingFrame);
		RegisterCastingTests();
	}

	int CastingChance(RE::Actor* a_caster, const std::vector<SpellEffect>& a_effects, bool a_preview)
	{
		const auto& state = State::Get();
		if (!state.settings.castingFailure || !a_caster || a_effects.empty()) {
			return -1;
		}
		auto skillOf = [&](const EffectDef& a_def) {
			return static_cast<double>(a_caster->AsActorValueOwner()->GetActorValue(SchoolSkillAV(a_def.school)));
		};
		const int hardest = Cost::HardestEffect(state.catalog, a_effects, skillOf);
		double    skill = 0.0;
		if (hardest >= 0 && static_cast<std::size_t>(hardest) < a_effects.size()) {
			if (const auto* def = state.catalog.Find(a_effects[static_cast<std::size_t>(hardest)].effectId)) {
				skill = skillOf(*def);
			}
		}
		const double difficulty = Mech::MorrowindDifficulty(state.catalog, a_effects, state.settings);
		const double sound = Effects::SoundMagnitude(a_caster);
		const double stamina = a_preview ? 0.5 : StaminaRatio(a_caster);
		return Cost::SkyrimChance(skill, state.settings.castingBonus, difficulty, sound, stamina);
	}

	void ResolveArea(RE::Actor* a_caster, const CustomSpell& a_spell, std::size_t a_planIndex, const RE::NiPoint3& a_point,
		RE::TESObjectREFR* a_directTarget)
	{
		if (!a_caster || a_planIndex >= a_spell.plan.spells.size()) {
			return;
		}
		auto* record = a_planIndex == 0 ? a_spell.primary : (a_planIndex - 1 < a_spell.subs.size() ? a_spell.subs[a_planIndex - 1] : nullptr);
		if (!record) {
			return;
		}
		const PlanRef ref{ &a_spell, a_planIndex, record };
		// Area entries and their radius (A x 22 units, OUTLINE "Area").
		std::vector<std::pair<RE::Effect*, float>> areaEffects;
		std::uint16_t                              maxFeet = 0;
		for (auto* effect : record->effects) {
			const auto* entry = EntryOf(ref, effect);
			if (entry && entry->area > 0) {
				areaEffects.emplace_back(effect, static_cast<float>(Mech::AreaRadius(entry->area)));
				maxFeet = std::max(maxFeet, entry->area);
			}
		}
		if (areaEffects.empty()) {
			return;
		}
		SpawnAreaVisual(a_caster, a_spell, a_planIndex, a_point, maxFeet);

		const float maxRadius = static_cast<float>(Mech::AreaRadius(maxFeet));
		const bool  los = State::Get().settings.areaLOS;
		std::vector<RE::Actor*> victims;
		auto consider = [&](RE::Actor* a_actor) {
			if (!a_actor || a_actor == a_caster || a_actor == a_directTarget || a_actor->IsDead() || !a_actor->Is3DLoaded()) {
				return;
			}
			if (a_actor->GetPosition().GetDistance(a_point) > maxRadius) {
				return;
			}
			if (los && !LineOfSight(a_point, a_actor)) {
				return;
			}
			victims.push_back(a_actor);
		};
		consider(Player());
		if (auto* lists = RE::ProcessLists::GetSingleton()) {
			for (auto& handle : lists->highActorHandles) {
				if (auto actor = handle.get()) {
					consider(actor.get());
				}
			}
		}
		for (auto* victim : victims) {
			auto* magicTarget = victim->GetMagicTarget();
			if (!magicTarget) {
				continue;
			}
			const float distance = victim->GetPosition().GetDistance(a_point);
			for (auto& [effect, radius] : areaEffects) {
				if (distance > radius) {
					continue;
				}
				RE::MagicTarget::ResultsCollector collector{};
				collector.target = magicTarget;
				collector.caster = a_caster;
				collector.magicItem = record;
				RE::MagicTarget::AddTargetData data{};
				data.caster = a_caster;
				data.magicItem = record;
				data.effect = effect;
				data.source = nullptr;
				data.postCreationCallback = nullptr;
				data.resultsCollector = &collector;
				data.explosionPoint = a_point;
				data.magnitude = effect->effectItem.magnitude;  // VERIFY(in-game): engine scales by power itself
				data.power = 1.0f;
				data.castingSource = RE::MagicSystem::CastingSource::kInstant;
				data.areaTarget = true;
				data.dualCasted = false;
				magicTarget->AddTarget(data);
			}
		}
		logger::debug("casting: area {} ft hit {} actors", maxFeet, victims.size());
	}

	namespace
	{
		// ---- in-game checks (suite "effects", casting part) ---------------------------------------
		void RegisterCastingTests()
		{
			Tests::Register("effects", "casting.touch_reach", []() {
				auto* projectile = State::Get().forms.Get<RE::BGSProjectile>("LA_TouchProjectile");
				if (!projectile) {
					return Tests::Fail("LA_TouchProjectile not loaded");
				}
				return Tests::Expect(static_cast<float>(State::Get().settings.touchReach), projectile->data.range, "touch range");
			});
			Tests::Register("effects", "casting.chance_formula", []() {
				// The module formula on the player at preview stamina: (2S + bonus - D - Sound) x 1.0.
				auto* player = Player();
				if (!player) {
					return Tests::Fail("no player");
				}
				std::vector<SpellEffect> effects{ SpellEffect{ "mw.fire_damage", kNoSub, Range::kTarget, 5, 10, 3, 10 } };
				auto&      settings = State::Get().settings;
				const bool was = settings.castingFailure;
				settings.castingFailure = true;
				const int chance = CastingChance(player, effects, true);
				settings.castingFailure = was;
				const double skill = player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kDestruction);
				const int expected = Cost::SkyrimChance(skill, settings.castingBonus,
					Mech::MorrowindDifficulty(State::Get().catalog, effects, settings), Effects::SoundMagnitude(player), 0.5);
				return Tests::Expect(expected, chance, "chance at half stamina");
			});
			Tests::Register("effects", "casting.chance_off", []() {
				auto& settings = State::Get().settings;
				const bool was = settings.castingFailure;
				settings.castingFailure = false;
				const int chance = CastingChance(Player(), { SpellEffect{ "mw.fire_damage" } }, true);
				settings.castingFailure = was;
				return Tests::Expect(-1, chance, "module off");
			});
			Tests::Register("effects", "casting.area_radius", []() {
				// Morrowind: A x 22 units.
				return Tests::Expect(220.0, Mech::AreaRadius(10), "10 ft");
			});
		}
	}
}
