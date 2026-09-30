#pragma once

#include "Core/State.h"

// Co-save persistence (CONTRACTS section 4, OUTLINE "Load and save sequence"). Owner: Core.
namespace LA::Persistence
{
	// Registers the 'LART' co-save id and the revert/save/load callbacks (SKSEPluginLoad).
	bool Install();

	// SKSE messages (main.cpp).
	void OnPreLoadGame(const char* a_saveName, std::uint32_t a_length);
	void OnPostLoadGame(bool a_success);
	void OnNewGame();

	// Reads the LART definitions out of an SKSE co-save image (the .skse file). nullopt when the
	// bytes are not a co-save or LADF does not decode; an empty vector when LART is absent.
	std::optional<std::vector<SpellDef>> ParseCosave(const std::vector<std::uint8_t>& a_bytes, std::string* a_error = nullptr);

	// The co-save of the last save loaded (for the in-game "save" suite), and how many
	// definitions its load restored.
	std::optional<std::filesystem::path> LastCosavePath();
	std::size_t                          LastLoadedCount();

	// Re-equips custom spells held in either hand so the hand art is rebuilt.
	void ReequipHands();
}
