#pragma once

// Papyrus natives for script LostArt (docs/dev/CONTRACTS.md section 8).
namespace LA::Papyrus
{
	bool Register(RE::BSScript::IVirtualMachine* a_vm);
	void SendSpellEvent(std::string_view a_event, RE::SpellItem* a_spell);  // LostArt_SpellCreated / _Deleted
}
