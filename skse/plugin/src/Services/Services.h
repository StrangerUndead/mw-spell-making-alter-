#pragma once

#include "Core/State.h"

// Where spells are made: the two altars and the 15 NPC spellmakers (OUTLINE "Where spells are
// made"), the purchase flow, and tome distribution. Owner: Services/*.cpp.
namespace LA::Services
{
	// Event sinks (furniture enter/exit, topic info, dialogue menu, cell attach for altar
	// placement). Called once at kDataLoaded.
	void Install();

	// Adds tomes to spellmaker vendor lists and dungeon loot lists in memory (no record edits).
	void InjectVendorsAndLoot();

	// Opens the spellmaking menu for an altar reference or a spellmaker actor. Checks access and
	// refusal first and shows the refusal message instead when needed.
	void OpenSpellmaking(RE::TESObjectREFR* a_provider);

	// CONTRACTS.md section 8 reason codes; 0 = serves.
	int RefusalReason(RE::Actor* a_spellmaker);

	// Provider description the menu session uses (altar or spellmaker, fee rules).
	Provider MakeProvider(RE::TESObjectREFR* a_provider);

	// Takes the gold (to the spellmaker's merchant chest / inventory, or the altar's owner), a
	// soul gem when that fee applies, then compiles and teaches the spell, plays the flash and the
	// spell-learned sound. Returns the new spell or nullptr.
	RE::SpellItem* CompletePurchase(const Purchase& a_purchase, RE::TESObjectREFR* a_provider);

	// Skyrim buy-price adjustment (Speech, Fortify Barter, Modify Buy Prices perks) for the
	// haggling setting.
	std::uint32_t Haggle(std::uint32_t a_price, RE::Actor* a_merchant);

	// Altar state (SetAltarActive native) and runtime placement.
	void SetAltarActive(RE::TESObjectREFR* a_altar, bool a_active);
	void PlaceAltars();
}
