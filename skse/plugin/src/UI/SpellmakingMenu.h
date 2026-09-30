#pragma once

#include "Core/State.h"

// The Scaleform spellmaking menu host (docs/dev/CONTRACTS.md section 7). The SWF renders; the
// MenuSession in lostart_core decides. Owner: UI/SpellmakingMenu.cpp.
namespace LA::UI
{
	inline constexpr std::string_view kMenuName = "LostArt_SpellmakingMenu"sv;
	inline constexpr std::string_view kMoviePath = "LostArt_Spellmaking"sv;  // Interface/<name>.swf

	void RegisterSpellmakingMenu();  // at kDataLoaded (UI::Register)

	// Opens the menu; a_provider is the altar or the spellmaker (may be null from the console).
	void OpenSpellmakingMenu(const Provider& a_provider, RE::TESObjectREFR* a_providerRef);
	bool IsSpellmakingMenuOpen();
	void CloseSpellmakingMenu();

	// Magic-menu extensions: Morrowind-style item cards for custom spells and the delete hotkey
	// (Shift+click / Delete / left-stick click) with sQuestionDeleteSpell confirmation.
	void InstallMagicMenuExtensions();

	// Shows a message box with a single OK button (Morrowind message style).
	void ShowMessage(const std::string& a_text);
	void PlayUISound(const char* a_editorId);  // "UIMenuOK", "UIMenuCancel", ...
}
