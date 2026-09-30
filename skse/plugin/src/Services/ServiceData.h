#pragma once

#include "Core/State.h"

// Fields of data/content/{spellmakers,altars,tomes}.json that core Content does not carry yet
// (docs/data-notes.md 1.5, 1.6, 1.8), plus variants.json "lootInjection" (docs/dev/GENERATOR.md).
// Read once from State::dataDir with nlohmann::json.
// Owner: Services/*.cpp. Core change requested: move these into lostart::Content.
namespace LA::Services
{
	struct SpellmakerExtra
	{
		std::string refusalLine;  // "$LA_Refuse_<NpcLabel>"
		std::string hold;         // for "wanted:<Hold>"
		bool        anyone{ false };
	};

	struct AltarExtra
	{
		std::string nameKey;         // "$LA_Altar_College"
		FormRef     freeForFaction;  // Arch-Mage faction for "archmage"
	};

	struct TomeExtra
	{
		std::string          bookEditorId;  // "LA_Tome_<Pascal>"
		std::uint16_t        level{ 1 };
		std::vector<FormRef> lootLists;
		std::vector<std::string> vendors;   // Morrowind school names
		bool                 loot{ true };
	};

	struct ServiceData
	{
		FormRef                                          collegeFaction;
		std::map<std::string, FormRef>                   holdCrimeFactions;  // hold -> crime faction
		std::unordered_map<std::string, SpellmakerExtra> spellmakers;        // by spellmaker id
		std::unordered_map<std::string, AltarExtra>      altars;             // by altar id
		std::vector<TomeExtra>                           tomes;
		// variants.json "lootInjection": generated LVLI EditorID -> vanilla leveled list.
		std::vector<std::pair<std::string, FormRef>>     lootInjection;

		static const ServiceData& Get();  // loads on first use
	};

	// Vanilla forms the services use (Mutagen.Bethesda.FormKeys.SkyrimSE, checked 2026-09).
	namespace Vanilla
	{
		inline constexpr std::uint32_t kGold001 = 0x00000F;
		inline constexpr std::uint32_t kCollegeOfWinterholdFaction = 0x01F259;
		inline constexpr std::uint32_t kCollegeArchMageFaction = 0x103372;  // CollegeofWinterholdArchMageFaction
		inline constexpr std::uint32_t kAbVampire04 = 0x0ED09E;             // stage-4 vampire ability
		inline constexpr std::uint32_t kAbVampire04b = 0x10F1E8;            // post-Dawnguard variant
	}
}
