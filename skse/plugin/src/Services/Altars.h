#pragma once

#include "Core/State.h"

// Altar internals shared between Services and the menu host. Owner: Services/*.cpp.
namespace LA::Services
{
	// Makes the player leave the altar furniture if they occupy it (the menu closed).
	void StandUpFromAltar(RE::TESObjectREFR* a_altar);

	// Whether a reference is one of our altars (base form LA_AltarCollege / LA_AltarTelvanni).
	bool IsAltar(const RE::TESObjectREFR* a_ref);

	// The content entry for an altar reference (matched by furniture base).
	const Altar* AltarFor(const RE::TESObjectREFR* a_ref);

	// Places missing altars in a_cell (called from the cell-load sink and PlaceAltars).
	void PlaceAltarsIn(RE::TESObjectCELL* a_cell);

	// Enables/disables every placed altar according to settings.altars and SetAltarActive.
	void ApplyAltarState();
}
