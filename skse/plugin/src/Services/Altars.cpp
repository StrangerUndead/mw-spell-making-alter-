#include "Services/Altars.h"

#include "Services/Services.h"

// The two Altars of Spellmaking (OUTLINE "Altars"): placed at runtime instead of edited into
// the cells, so interior overhauls don't conflict.
//
// Placement: when an altar's cell finishes loading (TESCellFullyLoadedEvent, and after every
// save load for the cell the player is in), the cell is searched for a reference whose base is
// our furniture (LA_AltarCollege / LA_AltarTelvanni). None: one is created with
// TESDataHandler::CreateReferenceAtLocation (the same call TESObjectREFR::PlaceObjectAtMe makes,
// see CommonLib TESObjectREFR.cpp) as a *persistent* reference. Persistence puts the created ref
// in the save, so the next load finds it again by its base in the cell: the save itself is the
// record of what we placed, and no co-save entry is needed (a co-save FormID could go stale if
// the created-form index is compacted). If another object stands on the spot, the alternate
// marker from altars.json is used. Placeholder coordinates (all zero, altars.json verify:true)
// are never placed: a warning is logged instead.

namespace LA::Services
{
	namespace
	{
		constexpr float kOccupiedRadius = 48.0f;  // game units around the altar origin
		constexpr float kDegToRad = 0.017453292519943295f;

		std::mutex                     g_mutex;
		std::unordered_set<RE::FormID> g_disabledBySettings;
		std::unordered_set<RE::FormID> g_placed;  // altar refs seen or created this session
		std::unordered_set<std::string> g_warnedPlaceholder;

		RE::TESBoundObject* FurnitureOf(const Altar& a_altar)
		{
			return State::Get().forms.Get<RE::TESBoundObject>(a_altar.furniture);
		}

		bool IsZero(const std::array<float, 3>& a_v)
		{
			return a_v[0] == 0.0f && a_v[1] == 0.0f && a_v[2] == 0.0f;
		}

		RE::NiPoint3 ToPoint(const std::array<float, 3>& a_v) { return { a_v[0], a_v[1], a_v[2] }; }
		RE::NiPoint3 ToRadians(const std::array<float, 3>& a_deg)
		{
			// altars.json rotations are in degrees, as the Creation Kit shows them.
			return { a_deg[0] * kDegToRad, a_deg[1] * kDegToRad, a_deg[2] * kDegToRad };
		}

		RE::TESObjectREFR* FindPlaced(RE::TESObjectCELL* a_cell, const RE::TESBoundObject* a_base)
		{
			RE::TESObjectREFR* found = nullptr;
			a_cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
				if (a_ref && a_ref->GetBaseObject() == a_base && !a_ref->IsDeleted()) {
					found = a_ref;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			return found;
		}

		// Something solid stands within kOccupiedRadius: furniture, containers, activators, doors,
		// actors, flora, movable statics, or a clutter-sized static. Big architecture statics
		// (floors, walls: bounds wider than 256 units) and markers, lights and sounds don't count.
		bool Occupied(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_pos, const RE::TESBoundObject* a_ours)
		{
			bool occupied = false;
			a_cell->ForEachReferenceInRange(a_pos, kOccupiedRadius, [&](RE::TESObjectREFR* a_ref) {
				auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
				if (!base || base == a_ours || a_ref->IsDisabled() || a_ref->IsDeleted()) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				switch (base->GetFormType()) {
				case RE::FormType::Furniture:
				case RE::FormType::Container:
				case RE::FormType::Activator:
				case RE::FormType::TalkingActivator:
				case RE::FormType::Door:
				case RE::FormType::NPC:
				case RE::FormType::Flora:
				case RE::FormType::Tree:
				case RE::FormType::MovableStatic:
					occupied = true;
					break;
				case RE::FormType::Static:
					{
						auto* stat = base->As<RE::TESObjectSTAT>();
						if (stat && (stat->GetFormFlags() & RE::TESObjectSTAT::RecordFlags::kIsMarker) != 0) {
							break;
						}
						const auto& bounds = base->boundData;
						const int   width = std::max(bounds.boundMax.x - bounds.boundMin.x, bounds.boundMax.y - bounds.boundMin.y);
						occupied = width > 0 && width <= 256;
						break;
					}
				default:
					break;
				}
				return occupied ? RE::BSContainer::ForEachResult::kStop : RE::BSContainer::ForEachResult::kContinue;
			});
			return occupied;
		}

		void ApplyStateTo(RE::TESObjectREFR* a_ref)
		{
			const bool       wanted = State::Get().settings.altars;
			std::scoped_lock lock(g_mutex);
			const auto       formId = a_ref->GetFormID();
			if (!wanted && !a_ref->IsDisabled()) {
				a_ref->Disable();
				g_disabledBySettings.insert(formId);
			} else if (wanted && a_ref->IsDisabled() && g_disabledBySettings.erase(formId) > 0) {
				a_ref->Enable(false);
			}
		}
	}

	bool IsAltar(const RE::TESObjectREFR* a_ref)
	{
		return AltarFor(a_ref) != nullptr;
	}

	const Altar* AltarFor(const RE::TESObjectREFR* a_ref)
	{
		const auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
		if (!base) {
			return nullptr;
		}
		for (const auto& altar : State::Get().content.Altars()) {
			if (FurnitureOf(altar) == base) {
				return &altar;
			}
		}
		return nullptr;
	}

	void PlaceAltarsIn(RE::TESObjectCELL* a_cell)
	{
		const auto& state = State::Get();
		if (!a_cell || !state.dataReady) {
			return;
		}
		for (const auto& altar : state.content.Altars()) {
			auto* cell = FormMap::Resolve<RE::TESObjectCELL>(altar.cell);
			if (cell != a_cell) {
				continue;
			}
			auto* base = FurnitureOf(altar);
			if (!base) {
				logger::warn("altars: {} furniture {} is not loaded"sv, altar.id, altar.furniture);
				continue;
			}
			if (auto* existing = FindPlaced(a_cell, base)) {
				{
					std::scoped_lock lock(g_mutex);
					g_placed.insert(existing->GetFormID());
				}
				ApplyStateTo(existing);
				continue;
			}
			if (IsZero(altar.position)) {
				std::scoped_lock lock(g_mutex);
				if (g_warnedPlaceholder.insert(altar.id).second) {
					logger::warn("altars: {} has placeholder coordinates (altars.json verify:true); not placed"sv, altar.id);
				}
				continue;
			}

			auto position = altar.position;
			auto rotation = altar.rotation;
			if (Occupied(a_cell, ToPoint(position), base)) {
				if (altar.alternate && !IsZero(altar.alternate->first)) {
					logger::info("altars: {} spot is occupied (interior overhaul?); using the alternate marker"sv, altar.id);
					position = altar.alternate->first;
					rotation = altar.alternate->second;
				} else {
					logger::warn("altars: {} spot is occupied and there is no alternate; placing anyway"sv, altar.id);
				}
			}

			auto* dataHandler = RE::TESDataHandler::GetSingleton();
			if (!dataHandler) {
				return;
			}
			auto* worldspace = a_cell->IsInteriorCell() ? nullptr : a_cell->GetRuntimeData().worldSpace;
			// VERIFY(in-game): the created persistent ref survives save/load and is found again in
			// the cell's reference list on the next load (no duplicate altar).
			const auto handle = dataHandler->CreateReferenceAtLocation(base, ToPoint(position), ToRadians(rotation), a_cell, worldspace,
				nullptr, nullptr, RE::ObjectRefHandle(), true, true);
			auto ref = handle.get();
			if (!ref) {
				logger::error("altars: could not create {} in cell {:08X}"sv, altar.id, a_cell->GetFormID());
				continue;
			}
			{
				std::scoped_lock lock(g_mutex);
				g_placed.insert(ref->GetFormID());
			}
			logger::info("altars: placed {} as {:08X} in cell {:08X}"sv, altar.id, ref->GetFormID(), a_cell->GetFormID());
			ApplyStateTo(ref.get());
		}
	}

	void ApplyAltarState()
	{
		std::vector<RE::FormID> placed;
		{
			std::scoped_lock lock(g_mutex);
			placed.assign(g_placed.begin(), g_placed.end());
		}
		for (const auto formId : placed) {
			// g_placed spans every save of the session and created refs (0xFF......) are renumbered
			// per save: after loading another save the id may name some other created reference.
			if (auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(formId); ref && IsAltar(ref)) {
				ApplyStateTo(ref);
			}
		}
	}

	void PlaceAltars()
	{
		// Cells that are loaded right now (the one the player stands in after a load); others are
		// handled by the cell-load sink when they load.
		for (const auto& altar : State::Get().content.Altars()) {
			if (auto* cell = FormMap::Resolve<RE::TESObjectCELL>(altar.cell); cell && cell->IsAttached()) {
				PlaceAltarsIn(cell);
			}
		}
		ApplyAltarState();
	}

	void SetAltarActive(RE::TESObjectREFR* a_altar, bool a_active)
	{
		if (!a_altar || !IsAltar(a_altar)) {
			logger::warn("SetAltarActive: not an altar"sv);
			return;
		}
		RE::ObjectRefHandle handle = a_altar->CreateRefHandle();
		RunOnMainThread([handle, a_active]() {
			auto ref = handle.get();
			if (!ref) {
				return;
			}
			{
				std::scoped_lock lock(g_mutex);
				g_disabledBySettings.erase(ref->GetFormID());  // an explicit call wins over the setting
			}
			if (a_active && ref->IsDisabled()) {
				ref->Enable(false);
			} else if (!a_active && !ref->IsDisabled()) {
				StandUpFromAltar(ref.get());
				ref->Disable();
			}
		});
	}

	void StandUpFromAltar(RE::TESObjectREFR* a_altar)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		const auto occupied = player->GetOccupiedFurniture().get();
		if (!occupied || (a_altar && occupied.get() != a_altar) || !IsAltar(occupied.get())) {
			return;
		}
		// The behaviour-graph event furniture exits run on (SkyClimb and SexLab use the same
		// call to leave furniture); the engine clears the furniture state when the exit
		// animation finishes. VERIFY(in-game): the player stands up and can move afterwards.
		player->NotifyAnimationGraph("IdleFurnitureExit");
	}
}
