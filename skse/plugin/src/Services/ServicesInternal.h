#pragma once

#include "Core/State.h"

// Services internals shared by Services/*.cpp and the parity tests. Owner: Services/*.cpp.
namespace LA::Services
{
	// The spellmakers.json entry for an actor (matched by NPC base form), or nullptr.
	const Spellmaker* FindSpellmaker(const RE::Actor* a_actor);
	const Spellmaker* FindSpellmakerById(std::string_view a_id);

	// The facts CONTRACTS section 8 reasons are computed from, read from the game.
	// a_actor may be null (tests): the relationship then uses the spellmaker's NPC base.
	Mech::RefusalFacts GatherFacts(const Spellmaker* a_spellmaker, RE::Actor* a_actor);
	int                RefusalReasonFor(const Spellmaker* a_spellmaker, RE::Actor* a_actor);

	// Test hook: edits the gathered facts before the rule runs ({} clears it).
	void SetFactsOverride(std::function<void(Mech::RefusalFacts&)> a_override);

	// Text shown after a refusal (reason 1: $LA_Msg_SpellmakerMembersOnly; 2-3: the NPC's
	// refusal line; others: empty, the spoken line is enough).
	std::string RefusalMessage(const Spellmaker* a_spellmaker, int a_reason);

	// Sets LA_ServiceRefusal for the actor the player is about to talk to.
	void UpdateRefusalGlobal(RE::Actor* a_actor);

	// Player membership helpers.
	bool PlayerInFaction(const FormRef& a_faction);
	bool PlayerIsArchMage();
}
