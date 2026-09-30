// World-facing effects: Open and Lock, Telekinesis reach, Detect Key / Detect Enchantment scans,
// Mark and Recall, Divine and Almsivi Intervention, bound armor, Command Creature/Humanoid,
// Charm, and the native-effect touch for Burden on NPCs.
//
// Engine paths used (all vanilla, no hooks):
//  * Papyrus natives through the VM (asynchronous, ordered): ObjectReference.Lock /
//    SetLockLevel (handle linked teleport doors and ownership exactly as scripts do),
//    Actor.SetPlayerTeammate, Actor.SetRelationshipRank, Game.IsFastTravelEnabled.
//  * TESObjectREFR::MoveTo / MoveTo_Impl for teleports (CommonLib RE/T/TESObjectREFR.h; MoveTo
//    passes the target's cell, worldspace, position and angle to MoveTo_Impl).
//  * Actor::TrespassAlarm (CommonLib, RELOCATION_ID(36432, 37427)) to report an owned lock
//    opened by magic the way lockpicking reports it.
//  * TESObjectREFR::ApplyEffectShader with a finite duration for detection highlights, so the
//    highlight fades by itself when the effect ends or the object leaves the radius.

#include "Effects/EffectsInternal.h"

namespace LA::Effects::Internal
{
	namespace
	{
		void Nothing(Instance&) {}

		// ---- Open / Lock ----------------------------------------------------------------------
		int MorrowindLockLevel(RE::TESObjectREFR* a_ref)
		{
			const auto* lock = a_ref->GetLock();
			if (!lock || !lock->IsLocked()) {
				return 0;
			}
			switch (a_ref->GetLockLevel()) {
			case RE::LOCK_LEVEL::kVeryEasy:
				return 1;
			case RE::LOCK_LEVEL::kEasy:
				return 25;
			case RE::LOCK_LEVEL::kAverage:
				return 50;
			case RE::LOCK_LEVEL::kHard:
				return 75;
			case RE::LOCK_LEVEL::kVeryHard:
				return 100;
			case RE::LOCK_LEVEL::kRequiresKey:
				return Mech::kRequiresKey;
			default:
				return 0;
			}
		}

		bool IsOwnedByOthers(RE::TESObjectREFR* a_ref, RE::Actor* a_by)
		{
			auto* owner = a_ref->GetOwner();
			if (!owner) {
				if (auto* cell = a_ref->GetParentCell()) {
					owner = cell->GetOwner();
				}
			}
			if (!owner) {
				return false;
			}
			return !a_ref->IsAnOwner(a_by, true, false);
		}

		// ---- Telekinesis ------------------------------------------------------------------------
		RE::Setting* g_pickGmst{ nullptr };  // iActivatePickLength (GMST)
		RE::Setting* g_pickIni{ nullptr };   // fActivatePickLength:Interface (INI)
		std::int32_t g_pickGmstBase{ 0 };
		float        g_pickIniBase{ 0.0f };

		void ApplyReach()
		{
			// VERIFY(in-game): which of the two the engine reads for the activation pick; both are
			// set so either way the reach grows by the largest active M.
			const double feet = MaxMagnitude(Player(), "mw.telekinesis");
			const double extra = Mech::FeetToUnits(feet);
			if (g_pickGmst && g_pickGmstBase > 0) {
				g_pickGmst->data.i = g_pickGmstBase + static_cast<std::int32_t>(extra);
			}
			if (g_pickIni && g_pickIniBase > 0.0f) {
				g_pickIni->data.f = g_pickIniBase + static_cast<float>(extra);
			}
		}

		void TelekinesisStart(Instance& a_inst)
		{
			if (a_inst.targetId == 0x14) {
				ApplyReach();
			}
		}
		void TelekinesisFinish(Instance& a_inst)
		{
			if (a_inst.targetId == 0x14) {
				SKSE::GetTaskInterface()->AddTask([]() { ApplyReach(); });  // after the instance is gone
			}
		}

		// ---- Detection scans ---------------------------------------------------------------------
		RE::TESEffectShader* KeyShader()
		{
			static RE::TESEffectShader* shader = [] {
				auto* own = State::Get().forms.Get<RE::TESEffectShader>("LA_Shader_DetectKey");
				return own ? own : Vanilla<RE::TESEffectShader>("Skyrim.esm|0x012FD9");  // HealFXS (gold)
			}();
			return shader;
		}
		RE::TESEffectShader* EnchantShader()
		{
			static RE::TESEffectShader* shader = [] {
				auto* own = State::Get().forms.Get<RE::TESEffectShader>("LA_Shader_DetectEnchantment");
				return own ? own : Vanilla<RE::TESEffectShader>("Skyrim.esm|0x103129");  // GhostVioletFXShader
			}();
			return shader;
		}

		bool IsEnchantedBase(const RE::TESBoundObject* a_obj)
		{
			if (const auto* weapon = a_obj->As<RE::TESObjectWEAP>()) {
				return weapon->formEnchanting != nullptr;
			}
			if (const auto* armor = a_obj->As<RE::TESObjectARMO>()) {
				return armor->formEnchanting != nullptr;
			}
			return false;
		}

		bool HoldsKey(RE::TESObjectREFR* a_ref)
		{
			return !a_ref->GetInventoryCounts([](RE::TESBoundObject& a_obj) { return a_obj.IsKey(); }).empty();
		}

		bool HoldsEnchanted(RE::TESObjectREFR* a_ref)
		{
			auto inventory = a_ref->GetInventory([](RE::TESBoundObject& a_obj) {
				return a_obj.Is(RE::FormType::Weapon) || a_obj.Is(RE::FormType::Armor);
			});
			for (auto& [obj, data] : inventory) {
				if (data.first <= 0) {
					continue;
				}
				if (IsEnchantedBase(obj) || (data.second && data.second->GetEnchantment())) {
					return true;
				}
			}
			return false;
		}

		float g_scanTimer{ 0.0f };
		constexpr float kScanPeriod = 1.0f;
		constexpr auto  kScanBudget = std::chrono::microseconds(800);  // < 1 ms per second (QA budget)
		constexpr auto  kHighlightGrace = std::chrono::milliseconds(2500);

		// Highlighted references: one endless shader each, stopped when the reference hasn't matched
		// for a few scans (out of range, picked up, effect ended). Re-applying a timed shader every
		// scan would stack overlapping shader instances.
		struct Highlight
		{
			RE::TESEffectShader*                  shader{ nullptr };
			std::chrono::steady_clock::time_point seen;
		};
		std::map<std::pair<RE::FormID, RE::FormID>, Highlight> g_highlights;  // (ref, shader) -> state

		void StopRefShader(RE::FormID a_ref, RE::TESEffectShader* a_shader)
		{
			auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_ref);
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!ref || !a_shader || !lists) {
				return;
			}
			const auto handle = ref->GetHandle();
			lists->ForEachShaderEffect([&](RE::ShaderReferenceEffect* a_effect) {
				if (a_effect && a_effect->effectData == a_shader && a_effect->target == handle) {
					a_effect->finished = true;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}

		void HighlightRef(RE::TESObjectREFR* a_ref, RE::TESEffectShader* a_shader, std::chrono::steady_clock::time_point a_now)
		{
			const auto key = std::make_pair(a_ref->GetFormID(), a_shader->GetFormID());
			auto [it, added] = g_highlights.try_emplace(key, Highlight{ a_shader, a_now });
			it->second.seen = a_now;
			if (added) {
				a_ref->ApplyEffectShader(a_shader, -1.0f);
			}
		}

		void ExpireHighlights(bool a_all)
		{
			const auto now = std::chrono::steady_clock::now();
			for (auto it = g_highlights.begin(); it != g_highlights.end();) {
				if (a_all || now - it->second.seen > kHighlightGrace) {
					StopRefShader(it->first.first, it->second.shader);
					it = g_highlights.erase(it);
				} else {
					++it;
				}
			}
		}

		void DetectionFrame(float a_delta)
		{
			auto* player = Player();
			if (!player) {
				return;
			}
			const double keyFeet = MaxMagnitude(player, "mw.detect_key");
			const double enchFeet = MaxMagnitude(player, "mw.detect_enchantment");
			if (keyFeet <= 0.0 && enchFeet <= 0.0) {
				if (!g_highlights.empty()) {
					ExpireHighlights(true);
				}
				return;
			}
			g_scanTimer -= a_delta;
			if (g_scanTimer > 0.0f) {
				return;
			}
			g_scanTimer = kScanPeriod;
			auto* tes = RE::TES::GetSingleton();
			if (!tes) {
				return;
			}
			const float keyRadius = static_cast<float>(Mech::FeetToUnits(keyFeet));
			const float enchRadius = static_cast<float>(Mech::FeetToUnits(enchFeet));
			const float radius = std::max(keyRadius, enchRadius);
			const auto  origin = player->GetPosition();
			const auto  start = std::chrono::steady_clock::now();
			auto*       keyShader = KeyShader();
			auto*       enchShader = EnchantShader();
			tes->ForEachReferenceInRange(player, radius, [&](RE::TESObjectREFR* a_ref) {
				if (std::chrono::steady_clock::now() - start > kScanBudget) {
					return RE::BSContainer::ForEachResult::kStop;
				}
				if (!a_ref || a_ref == player || a_ref->IsDisabled() || !a_ref->Is3DLoaded()) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				auto* base = a_ref->GetBaseObject();
				if (!base) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const float dist = origin.GetDistance(a_ref->GetPosition());
				const bool  isHolder = base->Is(RE::FormType::Container) || a_ref->Is(RE::FormType::ActorCharacter);
				if (keyShader && keyFeet > 0.0 && dist <= keyRadius) {
					if (base->IsKey() || (isHolder && HoldsKey(a_ref))) {
						HighlightRef(a_ref, keyShader, start);
					}
				}
				if (enchShader && enchFeet > 0.0 && dist <= enchRadius) {
					const bool ground = IsEnchantedBase(base) || a_ref->extraList.HasType(RE::ExtraDataType::kEnchantment);
					if (ground || (isHolder && HoldsEnchanted(a_ref))) {
						HighlightRef(a_ref, enchShader, start);
					}
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			ExpireHighlights(false);
		}

		// ---- Teleports ---------------------------------------------------------------------------
		RE::TESWorldSpace* RootWorld(RE::TESWorldSpace* a_world)
		{
			while (a_world && a_world->parentWorld) {
				a_world = a_world->parentWorld;
			}
			return a_world;
		}

		// Last exterior position of the player (Divine Intervention from an interior).
		RE::FormID           g_lastWorld{ 0 };
		RE::NiPoint3         g_lastExterior{};
		float                g_trackTimer{ 0.0f };

		void TrackExterior(float a_delta)
		{
			g_trackTimer -= a_delta;
			if (g_trackTimer > 0.0f) {
				return;
			}
			g_trackTimer = 2.0f;
			auto* player = Player();
			auto* world = player ? RootWorld(player->GetWorldspace()) : nullptr;
			if (world) {
				g_lastWorld = world->GetFormID();
				g_lastExterior = player->GetPosition();
			}
		}

		std::vector<RE::ActorHandle> NearbyFollowers()
		{
			std::vector<RE::ActorHandle> out;
			auto* player = Player();
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!player || !lists) {
				return out;
			}
			for (auto& handle : lists->highActorHandles) {
				auto actor = handle.get();
				if (actor && actor->IsPlayerTeammate() && !actor->IsDead() &&
					actor->GetPosition().GetDistance(player->GetPosition()) < 4096.0f) {
					out.push_back(handle);
				}
			}
			return out;
		}

		void BringFollowers(const std::vector<RE::ActorHandle>& a_followers)
		{
			// VERIFY(in-game): followers arrive with the player after the cell loads.
			SKSE::GetTaskInterface()->AddTask([a_followers]() {
				auto* player = Player();
				for (const auto& handle : a_followers) {
					if (auto actor = handle.get(); actor && player) {
						actor->MoveTo(player);
					}
				}
			});
		}

		// TESObjectREFR::MoveTo_Impl is private in CommonLib; same address CommonLib uses
		// (src/RE/T/TESObjectREFR.cpp: RELOCATION_ID(56227, 56626)).
		void MoveRefTo(RE::TESObjectREFR* a_ref, RE::TESObjectCELL* a_cell, RE::TESWorldSpace* a_world, const RE::NiPoint3& a_pos,
			const RE::NiPoint3& a_rot)
		{
			using func_t = void(RE::TESObjectREFR*, const RE::ObjectRefHandle&, RE::TESObjectCELL*, RE::TESWorldSpace*, const RE::NiPoint3&,
				const RE::NiPoint3&);
			static REL::Relocation<func_t> func{ RELOCATION_ID(56227, 56626) };
			func(a_ref, RE::ObjectRefHandle(), a_cell, a_world, a_pos, a_rot);
		}

		RE::TESObjectCELL* ExteriorCell(RE::TESWorldSpace* a_world, const RE::NiPoint3& a_pos)
		{
			if (!a_world) {
				return nullptr;
			}
			const auto x = static_cast<std::int16_t>(std::floor(a_pos.x / 4096.0f));
			const auto y = static_cast<std::int16_t>(std::floor(a_pos.y / 4096.0f));
			if (auto it = a_world->cellMap.find(RE::CellID(y, x)); it != a_world->cellMap.end() && it->second) {
				return it->second;
			}
			return a_world->persistentCell;  // VERIFY(in-game): MoveTo_Impl resolves the exterior cell from the position
		}

		void MarkStart(Instance& a_inst)
		{
			auto* actor = a_inst.Target();
			if (!actor || !actor->IsPlayerRef()) {
				return;
			}
			Mark mark;
			auto* cell = actor->GetParentCell();
			if (cell && cell->IsInteriorCell()) {
				mark.cell = cell->GetFormID();
			} else if (auto* world = actor->GetWorldspace()) {
				mark.worldspace = world->GetFormID();
			} else {
				return;
			}
			const auto pos = actor->GetPosition();
			mark.pos = { pos.x, pos.y, pos.z };
			mark.angleZ = actor->GetAngleZ();
			{
				std::unique_lock lock(State::Get().lock);
				auto&            marks = State::Get().marks;
				const auto       limit = static_cast<std::size_t>(std::max(1, State::Get().settings.marks));
				marks.push_back(mark);
				while (marks.size() > limit) {
					marks.erase(marks.begin());  // the oldest mark goes
				}
			}
			logger::info("effects: Mark set ({:08X}/{:08X} at {:.0f},{:.0f},{:.0f})", mark.cell, mark.worldspace, pos.x, pos.y, pos.z);
		}

		void DoRecall(const Mark& a_mark)
		{
			auto* player = Player();
			if (!player) {
				return;
			}
			const auto followers = NearbyFollowers();
			const RE::NiPoint3 pos{ a_mark.pos[0], a_mark.pos[1], a_mark.pos[2] };
			const RE::NiPoint3 rot{ 0.0f, 0.0f, a_mark.angleZ };
			if (a_mark.cell) {
				auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(a_mark.cell);
				if (!cell) {
					return;
				}
				MoveRefTo(player, cell, nullptr, pos, rot);
			} else {
				auto* world = RE::TESForm::LookupByID<RE::TESWorldSpace>(a_mark.worldspace);
				if (!world) {
					return;
				}
				MoveRefTo(player, ExteriorCell(world, pos), world, pos, rot);
			}
			BringFollowers(followers);
		}

		void RecallStart(Instance& a_inst)
		{
			auto* actor = a_inst.Target();
			if (!actor || !actor->IsPlayerRef()) {
				return;
			}
			std::optional<Mark> mark;
			{
				std::shared_lock lock(State::Get().lock);
				if (!State::Get().marks.empty()) {
					mark = State::Get().marks.back();  // the most recent mark
				}
			}
			if (!mark) {
				Notify(LocalText("$LA_Msg_NoMark", "You have no mark to recall to."));
				return;
			}
			if (auto* world = actor->GetWorldspace(); world && world->flags.any(RE::TESWorldSpace::Flag::kCantFastTravel)) {
				Notify(LocalText("$LA_Msg_RecallBlocked", "You cannot recall from here."));
				return;
			}
			// Blocked wherever a quest has disabled fast travel (Game.EnableFastTravel(false)).
			// VERIFY(in-game): Game.IsFastTravelEnabled exists in this game version; when the call
			// fails or returns no bool, Recall is allowed.
			CallStaticBool("Game", "IsFastTravelEnabled", [mark](std::optional<bool> a_enabled) {
				if (a_enabled.has_value() && !*a_enabled) {
					Notify(LocalText("$LA_Msg_RecallBlocked", "You cannot recall from here."));
					return;
				}
				DoRecall(*mark);
			});
		}

		struct Temple
		{
			std::string_view name;
			std::string_view marker;   // where the player arrives
			std::string_view compare;  // exterior reference used for "nearest" (a city map marker)
		};

		// FormKeys from Mutagen.Bethesda.FormKeys.SkyrimSE (Skyrim/PlacedObject.cs,
		// Dragonborn/PlacedObject.cs). VERIFY(in-game): each arrival marker stands inside the
		// temple; Windhelm has no named temple marker, so its city map marker is used.
		constexpr std::array kDivineTemples{
			Temple{ "Solitude", "Skyrim.esm|0x0D6AEF", "Skyrim.esm|0x04D0F4" },  // TempleOfDivinesMarker / SolitudeMapmarkerRef
			Temple{ "Whiterun", "Skyrim.esm|0x01F89C", "Skyrim.esm|0x0162CE" },  // WhiterunTempleofKynarethCenterMarker / WhiterunMapMarkerREF
			Temple{ "Windhelm", "Skyrim.esm|0x038436", "Skyrim.esm|0x038436" },  // WindhelmMapMarkerRef
			Temple{ "Riften", "Skyrim.esm|0x0449B3", "Skyrim.esm|0x01C390" },    // RiftenTempleWorshipMarker / RiftenMapMarkerREF
			Temple{ "Markarth", "Skyrim.esm|0x01F321", "Skyrim.esm|0x01C38A" },  // MarkarthTempleofDibellaLocationCenterMarkerREF / MarkarthMapMarkerREF
			Temple{ "Fort Frostmoth", "Dragonborn.esm|0x0143C4", "Dragonborn.esm|0x0143C4" },  // DLC2FrostmothMapMarkerRef
		};
		constexpr Temple kAlmsivi{ "Raven Rock Temple", "Dragonborn.esm|0x035E49", "Dragonborn.esm|0x0143DC" };  // DLC2RRTemplePorchMarker / DLC2RavenRockMapMarker

		RE::TESObjectREFR* ResolveTemple(const Temple& a_temple) { return Vanilla<RE::TESObjectREFR>(a_temple.marker); }

		void Intervene(RE::Actor* a_actor, RE::TESObjectREFR* a_marker)
		{
			if (!a_actor || !a_marker) {
				Notify(LocalText("$LA_Msg_NoTemple", "The intervention finds no temple."));
				return;
			}
			const auto followers = a_actor->IsPlayerRef() ? NearbyFollowers() : std::vector<RE::ActorHandle>{};
			a_actor->MoveTo(a_marker);
			if (!followers.empty()) {
				BringFollowers(followers);
			}
		}

		void DivineStart(Instance& a_inst)
		{
			auto* actor = a_inst.Target();
			if (!actor) {
				return;
			}
			std::vector<Mech::Destination> candidates;
			std::vector<RE::TESObjectREFR*> markers;
			for (const auto& temple : kDivineTemples) {
				auto* marker = ResolveTemple(temple);
				auto* compare = Vanilla<RE::TESObjectREFR>(temple.compare);
				auto* world = compare ? RootWorld(compare->GetWorldspace()) : nullptr;
				if (!marker || !compare || !world) {
					continue;
				}
				const auto p = compare->GetPosition();
				candidates.push_back({ std::string(temple.name), world->GetFormID(), { p.x, p.y, p.z } });
				markers.push_back(marker);
			}
			if (candidates.empty()) {
				Notify(LocalText("$LA_Msg_NoTemple", "The intervention finds no temple."));
				return;
			}
			RE::FormID   world = 0;
			RE::NiPoint3 from{};
			if (auto* w = RootWorld(actor->GetWorldspace())) {
				world = w->GetFormID();
				from = actor->GetPosition();
			} else if (actor->IsPlayerRef() && g_lastWorld) {
				world = g_lastWorld;
				from = g_lastExterior;
			}
			auto best = Mech::Nearest(candidates, world, { from.x, from.y, from.z });
			if (!best) {
				// Worlds without a temple (Blackreach, Sovngarde, the Soul Cairn, interiors with no
				// known exterior): Skyrim's heartland, as Morrowind always found a temple.
				const auto tamriel = RE::TESForm::LookupByID<RE::TESWorldSpace>(0x3C);
				best = Mech::Nearest(candidates, tamriel ? tamriel->GetFormID() : 0x3C, { from.x, from.y, from.z });
			}
			Intervene(actor, markers[best.value_or(0)]);
		}

		void AlmsiviStart(Instance& a_inst) { Intervene(a_inst.Target(), ResolveTemple(kAlmsivi)); }

		// ---- Bound armor ----------------------------------------------------------------------
		std::string_view BoundItem(std::string_view a_id)
		{
			if (a_id == "mw.bound_boots") {
				return "LA_Bound_Boots";
			}
			if (a_id == "mw.bound_cuirass") {
				return "LA_Bound_Cuirass";
			}
			if (a_id == "mw.bound_gloves") {
				return "LA_Bound_Gloves";
			}
			if (a_id == "mw.bound_helm") {
				return "LA_Bound_Helm";
			}
			if (a_id == "mw.bound_shield") {
				return "LA_Bound_Shield";
			}
			return {};
		}

		void BoundStart(Instance& a_inst)
		{
			auto* actor = a_inst.Target();
			auto* armor = State::Get().forms.Get<RE::TESObjectARMO>(BoundItem(a_inst.key->id));
			if (!actor || !armor) {
				return;
			}
			// Remember what was worn in that slot so it is put back when the binding ends.
			for (auto slot : { RE::BGSBipedObjectForm::BipedObjectSlot::kShield, RE::BGSBipedObjectForm::BipedObjectSlot::kBody,
					 RE::BGSBipedObjectForm::BipedObjectSlot::kHead, RE::BGSBipedObjectForm::BipedObjectSlot::kHands,
					 RE::BGSBipedObjectForm::BipedObjectSlot::kFeet }) {
				if (armor->GetSlotMask().any(slot)) {
					if (auto* worn = actor->GetWornArmor(slot); worn && worn != armor) {
						a_inst.form[0] = worn->GetFormID();
					}
					break;
				}
			}
			const auto handle = actor->GetHandle();
			const auto armorId = armor->GetFormID();
			SKSE::GetTaskInterface()->AddTask([handle, armorId]() {
				auto  actorPtr = handle.get();
				auto* item = RE::TESForm::LookupByID<RE::TESObjectARMO>(armorId);
				if (!actorPtr || !item) {
					return;
				}
				actorPtr->AddObjectToContainer(item, nullptr, 1, nullptr);
				RE::ActorEquipManager::GetSingleton()->EquipObject(actorPtr.get(), item, nullptr, 1, nullptr, false, true, true, true);
			});
		}

		void BoundFinish(Instance& a_inst)
		{
			auto* armor = State::Get().forms.Get<RE::TESObjectARMO>(BoundItem(a_inst.key->id));
			if (!armor) {
				return;
			}
			const auto handle = a_inst.target;
			const auto armorId = armor->GetFormID();
			const auto previous = a_inst.form[0];
			SKSE::GetTaskInterface()->AddTask([handle, armorId, previous]() {
				auto  actorPtr = handle.get();
				auto* item = RE::TESForm::LookupByID<RE::TESObjectARMO>(armorId);
				if (!actorPtr || !item) {
					return;
				}
				auto counts = actorPtr->GetInventoryCounts([&](RE::TESBoundObject& a_obj) { return &a_obj == item; });
				const auto count = counts.empty() ? 0 : counts.begin()->second;
				RE::ActorEquipManager::GetSingleton()->UnequipObject(actorPtr.get(), item, nullptr, 1, nullptr, false, true, false, true);
				if (count > 0) {
					actorPtr->RemoveItem(item, count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				}
				if (auto* prev = previous ? RE::TESForm::LookupByID<RE::TESObjectARMO>(previous) : nullptr) {
					auto has = actorPtr->GetInventoryCounts([&](RE::TESBoundObject& a_obj) { return &a_obj == prev; });
					if (!has.empty() && has.begin()->second > 0) {
						RE::ActorEquipManager::GetSingleton()->EquipObject(actorPtr.get(), prev, nullptr, 1, nullptr, false, false, false, true);
					}
				}
			});
		}

		// ---- Command ------------------------------------------------------------------------------
		bool InActiveQuestAlias(RE::Actor* a_actor)
		{
			auto* aliases = a_actor->extraList.GetByType<RE::ExtraAliasInstanceArray>();
			if (!aliases) {
				return false;
			}
			for (auto* data : aliases->aliases) {
				if (data && data->quest && data->quest->IsRunning()) {
					return true;
				}
			}
			return false;
		}

		RE::TESPackage* FollowPackage()
		{
			static RE::TESPackage* package = [] {
				auto* own = State::Get().forms.Get<RE::TESPackage>("LA_Package_CommandFollow");
				// Fallback: the vanilla "follow the player" package of Howl of the Pack wolves.
				// VERIFY(in-game): it carries no conditions that gate it for other actors (KB:
				// reused vanilla records can carry gating conditions).
				return own ? own : Vanilla<RE::TESPackage>("Skyrim.esm|0x10ACD1");  // HowlSummonedWolfFollowPlayer
			}();
			return package;
		}

		void CommandApply(RE::Actor* a_target)
		{
			CallMethod(a_target, "Actor", "SetPlayerTeammate", true, true);
			if (auto* package = FollowPackage()) {
				// VERIFY(in-game): a temporary created package overrides the AI stack until
				// EvaluatePackage(.., resetAI) clears it (CommandFinish).
				a_target->PutCreatedPackage(package, true, false, true);
			}
			a_target->StopCombat();
			a_target->EvaluatePackage(true, false);
		}

		void CommandStart(Instance& a_inst)
		{
			auto* target = a_inst.Target();
			auto* caster = a_inst.Caster();
			if (!target || !caster || !caster->IsPlayerRef() || target == caster || target->IsDead() || target->IsPlayerTeammate() ||
				target->IsCommandedActor() || InActiveQuestAlias(target)) {
				return;
			}
			static RE::BGSKeyword* npcKeyword = Vanilla<RE::BGSKeyword>("Skyrim.esm|0x013794");  // ActorTypeNPC
			const bool humanoid = npcKeyword && target->HasKeyword(npcKeyword);
			if (humanoid != (a_inst.key->id == "mw.command_humanoid")) {
				return;
			}
			if (target->GetLevel() > a_inst.magnitude) {
				return;  // level cap M
			}
			a_inst.applied = true;
			CommandApply(target);
		}

		void CommandResume(Instance& a_inst)
		{
			// The teammate flag is saved; the temporary follow package is not.
			auto* target = a_inst.Target();
			if (target && target->IsPlayerTeammate()) {
				a_inst.applied = true;
				if (auto* package = FollowPackage()) {
					target->PutCreatedPackage(package, true, false, true);
				}
				target->EvaluatePackage(true, false);
			}
		}

		void CommandFinish(Instance& a_inst)
		{
			auto* target = a_inst.Target();
			if (!a_inst.applied || !target) {
				return;
			}
			CallMethod(target, "Actor", "SetPlayerTeammate", false, true);
			target->EvaluatePackage(true, true);  // drop the temporary package, back to its own AI
		}

		// ---- Charm ----------------------------------------------------------------------------------
		int RankOf(RE::Actor* a_npc)
		{
			auto* player = Player();
			auto* npcBase = a_npc ? a_npc->GetActorBase() : nullptr;
			auto* playerBase = player ? player->GetActorBase() : nullptr;
			auto* rel = npcBase && playerBase ? RE::BGSRelationship::GetRelationship(npcBase, playerBase) : nullptr;
			if (!rel) {
				return 0;  // acquaintance
			}
			using L = RE::BGSRelationship::RELATIONSHIP_LEVEL;
			switch (rel->level.get()) {
			case L::kLover:
				return 4;
			case L::kAlly:
				return 3;
			case L::kConfidant:
				return 2;
			case L::kFriend:
				return 1;
			case L::kAcquaintance:
				return 0;
			case L::kRival:
				return -1;
			case L::kFoe:
				return -2;
			case L::kEnemy:
				return -3;
			case L::kArchnemesis:
				return -4;
			}
			return 0;
		}

		constexpr int kCharmCap = 3;  // Ally; never Lover. VERIFY(in-game): Ally doesn't unlock follower dialogue

		void CharmStart(Instance& a_inst)
		{
			auto* target = a_inst.Target();
			auto* caster = a_inst.Caster();
			if (!target || !caster || !caster->IsPlayerRef() || target->IsPlayerRef()) {
				return;
			}
			const int current = RankOf(target);
			const int steps = Mech::CharmSteps(a_inst.magnitude);
			const int next = std::max(current, std::min(kCharmCap, current + steps));
			a_inst.i[0] = next - current;  // steps actually applied
			if (a_inst.i[0] > 0) {
				CallMethod(target, "Actor", "SetRelationshipRank", static_cast<RE::Actor*>(caster), next);
			}
		}

		void CharmResume(Instance& a_inst)
		{
			// The raised rank is in the save; assume the full step count applied (capped).
			auto* target = a_inst.Target();
			if (!target) {
				return;
			}
			a_inst.i[0] = std::min(Mech::CharmSteps(a_inst.magnitude), std::max(0, RankOf(target) + 4));
		}

		void CharmFinish(Instance& a_inst)
		{
			auto* target = a_inst.Target();
			if (!target || a_inst.i[0] <= 0) {
				return;
			}
			const int restored = std::max(-4, RankOf(target) - a_inst.i[0]);
			CallMethod(target, "Actor", "SetRelationshipRank", static_cast<RE::Actor*>(Player()), restored);
		}

		// Prices improve by M/2 % with a charmed merchant, through the Fortify Barter actor value
		// (SpeechcraftMod) while the barter menu with that merchant is open.
		// VERIFY(in-game): the barter formula reads SpeechcraftMod as a percentage, as Fortify
		// Barter enchantments do, and the menu picks it up when it opens.
		float g_barterBonus{ 0.0f };

		class BarterSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			static BarterSink* Get()
			{
				static BarterSink sink;
				return &sink;
			}
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (!a_event || a_event->menuName != RE::BarterMenu::MENU_NAME) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const bool opening = a_event->opening;
				RunOnMainThread([opening]() {
					auto* player = Player();
					if (!player) {
						return;
					}
					if (g_barterBonus != 0.0f) {
						ModAV(player, RE::ActorValue::kSpeechcraftModifier, -g_barterBonus);
						g_barterBonus = 0.0f;
					}
					if (!opening) {
						return;
					}
					auto* topics = RE::MenuTopicManager::GetSingleton();
					auto  speaker = topics ? topics->speaker.get() : RE::NiPointer<RE::TESObjectREFR>{};
					auto* merchant = speaker ? speaker->As<RE::Actor>() : nullptr;
					double charm = 0.0;
					ForEachOn(merchant, [&](const Instance& a_inst) {
						if (a_inst.key->id == "mw.charm" && a_inst.casterId == 0x14) {
							charm += a_inst.magnitude;
						}
					});
					if (charm > 0.0) {
						g_barterBonus = static_cast<float>(Mech::CharmPriceBonus(charm));
						ModAV(player, RE::ActorValue::kSpeechcraftModifier, g_barterBonus);
					}
				});
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// ---- Burden on NPCs (native effect, plugin touch) ------------------------------------------
		void BurdenStart(Instance& a_inst)
		{
			auto* target = a_inst.Target();
			if (!target || target->IsPlayerRef()) {
				return;  // the player tracks encumbrance: the native carry-weight change is enough
			}
			ApplyAVDeltas(target, a_inst.avTarget, { { RE::ActorValue::kSpeedMult, -static_cast<float>(Mech::BurdenSpeedPenalty(a_inst.magnitude)) } });
		}
		void BurdenResume(Instance& a_inst)
		{
			if (a_inst.targetId != 0x14) {
				a_inst.avTarget = { { RE::ActorValue::kSpeedMult, -static_cast<float>(Mech::BurdenSpeedPenalty(a_inst.magnitude)) } };
			}
		}
		void AVFinish(Instance& a_inst) { RevertAVDeltas(a_inst.Target(), a_inst.avTarget); }

		void WorldFrame(float a_delta)
		{
			TrackExterior(a_delta);
			DetectionFrame(a_delta);
		}
	}

	bool ApplyLockOpen(RE::Actor* a_caster, RE::TESObjectREFR* a_ref, std::string_view a_effectId, double a_magnitude)
	{
		auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
		if (!base || !(base->Is(RE::FormType::Door) || base->Is(RE::FormType::Container))) {
			return false;
		}
		const int current = MorrowindLockLevel(a_ref);
		if (a_effectId == "mw.open") {
			if (current == 0) {
				return true;
			}
			if (!Mech::CanOpen(current, a_magnitude)) {
				RE::PlaySound("UILockpickingUnlockFail");  // VERIFY: SNDR EditorID; silent when absent
				return true;
			}
			CallMethod(a_ref, "ObjectReference", "Lock", false, false);
			if (a_caster && a_caster->IsPlayerRef() && IsOwnedByOthers(a_ref, a_caster)) {
				// Reported like picking the lock (crime "trespass", the lockpicking crime).
				// VERIFY(in-game): witnesses react and a bounty is added as for lockpicking.
				a_caster->TrespassAlarm(a_ref, a_ref->GetOwner(), RE::PackageNS::CRIME_TYPES::kTrespass);
			}
			return true;
		}
		if (a_effectId == "mw.lock") {
			if (auto next = Mech::LockTarget(current, a_magnitude)) {
				CallMethod(a_ref, "ObjectReference", "SetLockLevel", static_cast<std::int32_t>(*next));
				CallMethod(a_ref, "ObjectReference", "Lock", true, false);
			}
			return true;
		}
		return false;
	}

	void RevertWorld()
	{
		if (g_pickGmst && g_pickGmstBase > 0) {
			g_pickGmst->data.i = g_pickGmstBase;
		}
		if (g_pickIni && g_pickIniBase > 0.0f) {
			g_pickIni->data.f = g_pickIniBase;
		}
		if (g_barterBonus != 0.0f) {
			ModAV(Player(), RE::ActorValue::kSpeechcraftModifier, -g_barterBonus);
			g_barterBonus = 0.0f;
		}
		g_lastWorld = 0;
		g_highlights.clear();  // the references are about to go away with the old save
	}

	void OnLoadedWorld() { ApplyReach(); }

	std::size_t ResolvedTempleCount()
	{
		std::size_t count = ResolveTemple(kAlmsivi) ? 1 : 0;
		for (const auto& temple : kDivineTemples) {
			if (ResolveTemple(temple) && Vanilla<RE::TESObjectREFR>(temple.compare)) {
				++count;
			}
		}
		return count;
	}

	void InstallWorld()
	{
		if (auto* settings = RE::GameSettingCollection::GetSingleton()) {
			g_pickGmst = settings->GetSetting("iActivatePickLength");
			g_pickGmstBase = g_pickGmst ? g_pickGmst->GetInteger() : 0;
		}
		g_pickIni = RE::GetINISetting("fActivatePickLength:Interface");
		g_pickIniBase = g_pickIni ? g_pickIni->GetFloat() : 0.0f;

		Register("mw.lock", { Nothing, nullptr, nullptr, nullptr });  // resolved at impact (CastRouter)
		Register("mw.open", { Nothing, nullptr, nullptr, nullptr });
		Register("mw.telekinesis", { TelekinesisStart, TelekinesisStart, TelekinesisFinish, nullptr });
		Register("mw.detect_key", { Nothing, nullptr, nullptr, nullptr });           // WorldFrame scans
		Register("mw.detect_enchantment", { Nothing, nullptr, nullptr, nullptr });
		Register("mw.mark", { MarkStart, nullptr, nullptr, nullptr });
		Register("mw.recall", { RecallStart, nullptr, nullptr, nullptr });
		Register("mw.divine_intervention", { DivineStart, nullptr, nullptr, nullptr });
		Register("mw.almsivi_intervention", { AlmsiviStart, nullptr, nullptr, nullptr });
		const Handler bound{ BoundStart, nullptr, BoundFinish, nullptr };
		for (auto id : { "mw.bound_boots", "mw.bound_cuirass", "mw.bound_gloves", "mw.bound_helm", "mw.bound_shield" }) {
			Register(id, bound);
		}
		const Handler command{ CommandStart, CommandResume, CommandFinish, nullptr };
		Register("mw.command_creature", command);
		Register("mw.command_humanoid", command);
		Register("mw.charm", { CharmStart, CharmResume, CharmFinish, nullptr });
		Register("mw.burden", { BurdenStart, BurdenResume, AVFinish, nullptr });

		AddFrameCallback(WorldFrame);
		WakeFrame();  // exterior tracking for Divine Intervention needs the frame callback
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(BarterSink::Get());
		}
		logger::info("effects: activation reach GMST {} INI {}", g_pickGmst ? "found" : "missing", g_pickIni ? "found" : "missing");
	}
}
