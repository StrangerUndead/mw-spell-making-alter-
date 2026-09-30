#pragma once

#include "Core/State.h"

// The 44 custom effects (OUTLINE "Effect systems"): flight, falling, jumping, swimming,
// elemental retaliation, the miss system, Silence and Sound, Reflect, Dispel, detection scans,
// telekinetic reach, Open and Lock, teleports, Command, bound armor, condition pools and the
// attribute ledger. Custom MGEFs are Script-archetype records carrying LA_KW_Custom; the
// systems react to TESActiveEffectApplyRemoveEvent and a frame hook that only runs while an
// effect is active. Owner: Effects/*.cpp.
namespace LA::Effects
{
	void Install();          // at kDataLoaded
	void OnGameLoaded();     // after the co-save loaded (re-apply ledger stats, marks)
	void Revert();           // before another save loads
	void PrepareForUninstall();

	// Queries used elsewhere (Casting, UI).
	double MissChance(RE::Actor* a_attacker, RE::Actor* a_defender);
	bool   IsSilenced(RE::Actor* a_actor);
	double SoundMagnitude(RE::Actor* a_actor);
	bool   IsSlowfalling(RE::Actor* a_actor);
}
