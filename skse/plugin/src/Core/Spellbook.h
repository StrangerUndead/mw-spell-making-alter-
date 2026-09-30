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

	// --- Foundation additions -----------------------------------------------------------------
	// Plan for a definition under the current settings (content riders). Re-registers any
	// pass-through effect the definition uses first.
	CompilePlan PlanFor(const SpellDef& a_def);

	// Writes a_plan.spells[a_index] into a_record: index 0 is the equipped spell (cost override
	// a_def.cost, casting perk, name), others are linked sub-spells (cost 0, no perk). Used by
	// Compile and by the in-game compiler tests (scratch sub slots). Returns false on error.
	bool CompileInto(RE::SpellItem* a_record, const SpellDef& a_def, const CompilePlan& a_plan, std::size_t a_index,
		std::string* a_error = nullptr);

	// True when a slot record holds only the placeholder (as LostArt_Slots.esp ships it).
	bool IsBlank(const RE::SpellItem* a_record);

	// Re-registers a pass-through catalog entry ("pt.<plugin>|0x<id>") from its MGEF; false when
	// the MGEF's plugin is gone. Catalog entries are returned as is.
	bool EnsureEffect(std::string_view a_effectId);

	// Rebuilds the spellbook from saved definitions (co-save load, early restore): claims slots,
	// recompiles every definition, drops definitions whose slots are invalid. Returns spells built.
	int Restore(std::vector<SpellDef> a_defs);

	// Removes these spells from the player and every loaded actor and dispels their effects.
	void RemoveEverywhere(const std::vector<RE::SpellItem*>& a_spells);

	// Discovery view of one magic effect / spell type (also used by `la discover`).
	EffectDescriptor Describe(const RE::EffectSetting* a_effect);
	std::string      SpellTypeName(RE::MagicSystem::SpellType a_type);
}
