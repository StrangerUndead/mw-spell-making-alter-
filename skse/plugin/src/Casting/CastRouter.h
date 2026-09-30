#pragma once

#include "Core/State.h"

// Cast-time behaviour Skyrim lacks (OUTLINE "Casting behavior"): linked sub-spells for mixed
// ranges, touch projectiles, Morrowind-style area, min-max rolls through the active-effect
// perk-adjustment hook, and the optional casting-failure module. Owner: Casting/*.cpp.
namespace LA::Casting
{
	void Install();  // hooks + event sinks, at kPostLoad/kDataLoaded as appropriate

	// Chance (0-100) the player would have right now, for the menu readout (half stamina when
	// a_preview). -1 when the failure module is off.
	int CastingChance(RE::Actor* a_caster, const std::vector<SpellEffect>& a_effects, bool a_preview);

	// Applies a_spell's effects of the given plan entries to every actor within the area radius
	// of a_point (the plugin's own area resolver).
	void ResolveArea(RE::Actor* a_caster, const CustomSpell& a_spell, std::size_t a_planIndex, const RE::NiPoint3& a_point,
		RE::TESObjectREFR* a_directTarget);
}
