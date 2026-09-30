#pragma once

#include "Core/State.h"

// The spellbook: creating, compiling, deleting and rebuilding custom spells. Implemented in
// Core/Spellbook.cpp and Core/SpellCompiler.cpp. Every function here must be called on the main
// thread.
namespace LA::Spellbook
{
	// Writes a plan into slot records (effects, cost override, delivery, perk, flags, name) and
	// returns the primary spell. Sub slots in a_def.subSlots must already be claimed.
	RE::SpellItem* Compile(CustomSpell& a_spell);

	// Resets a slot record to its blank state (LA_BlankEffect, no name, cost 0).
	void Blank(RE::SpellItem* a_slot);

	// Allocates slots, compiles and teaches the spell to the player. Returns nullptr and logs on
	// failure (no free slot, compile error). Sends the LostArt_SpellCreated mod event.
	RE::SpellItem* Create(SpellDef a_def, const CompilePlan& a_plan);

	// Replaces an existing custom spell in place (keeps its slot, hotkeys and favorites).
	RE::SpellItem* Replace(std::uint16_t a_slot, SpellDef a_def, const CompilePlan& a_plan);

	// Removes the spell from the player and followers, dispels its effects, blanks and frees its
	// slots. Sends LostArt_SpellDeleted.
	bool Delete(std::uint16_t a_slot);

	// Deletes any non-inherent spell the settings allow (Morrowind's delete-on-demand).
	enum class DeleteCheck
	{
		kOk,
		kInherent,     // powers, racial, standing-stone, abilities: sDeleteSpellError
		kQuestSpell,   // quest-protected
		kNotCustom     // custom-only setting
	};
	DeleteCheck CanDelete(RE::SpellItem* a_spell);
	bool        DeleteAny(RE::SpellItem* a_spell);

	// Recomputes costs under the current model (MCM "Rebalance all"). Returns spells changed.
	int RebalanceAll();
	// Re-applies every definition to its slots (MCM "Rebuild all slots", and on every load).
	int RebuildAll();
	// Clears everything before another save loads (revert): blanks every used slot.
	void Revert();
	// MCM "Prepare for uninstall".
	bool PrepareForUninstall();

	// Player's known spells as the discovery engine sees them.
	std::vector<KnownSpell> KnownSpells(RE::Actor* a_actor);
	std::set<std::string>   KnownEffects(RE::Actor* a_actor);

	// Menu-facing list of custom spells for Load.
	std::vector<const CustomSpell*> List();
}
