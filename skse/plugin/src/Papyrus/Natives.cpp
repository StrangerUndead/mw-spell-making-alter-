#include "Papyrus/Natives.h"

#include "Core/Spellbook.h"
#include "Services/Services.h"
#include "Tests/InGameTests.h"

// Natives of `Scriptname LostArt Hidden` (CONTRACTS section 8, scripts/Source/LostArt.psc).
//
// They are registered without "callable from tasklets", so the VM runs them synchronously on the
// main thread (the same guarantee SKSE gives natives without the NoWait flag); record edits are
// therefore safe here. OpenSpellmaking still goes through the task queue so the menu opens after
// the calling script's frame.
namespace LA::Papyrus
{
	namespace
	{
		using VM = RE::BSScript::IVirtualMachine;
		using Tag = RE::StaticFunctionTag;

		constexpr std::string_view kScript = "LostArt"sv;

		void OpenSpellmaking(Tag*, RE::TESObjectREFR* a_provider)
		{
			RE::TESObjectREFRPtr keep{ a_provider };
			SKSE::GetTaskInterface()->AddTask([keep]() { Services::OpenSpellmaking(keep.get()); });
		}

		bool IsCustomSpell(Tag*, RE::SpellItem* a_spell)
		{
			return a_spell && State::Get().IsCustomSpell(a_spell);
		}

		std::int32_t GetCustomSpellCount(Tag*)
		{
			auto&            state = State::Get();
			std::shared_lock guard(state.lock);
			return static_cast<std::int32_t>(state.spells.size());
		}

		std::int32_t GetFreeSlotCount(Tag*)
		{
			auto&            state = State::Get();
			std::shared_lock guard(state.lock);
			return static_cast<std::int32_t>(state.slots.FreePrimaryCount());
		}

		std::int32_t GetFreeSubSlotCount(Tag*)
		{
			auto&            state = State::Get();
			std::shared_lock guard(state.lock);
			return static_cast<std::int32_t>(state.slots.FreeSubCount());
		}

		bool DeleteCustomSpell(Tag*, RE::SpellItem* a_spell)
		{
			const auto* custom = a_spell ? State::Get().FindBySpell(a_spell) : nullptr;
			if (!custom || custom->primary != a_spell) {
				return false;  // only the equipped spell of a custom spell
			}
			return Spellbook::Delete(custom->def.slot);
		}

		void SetAltarActive(Tag*, RE::TESObjectREFR* a_altar, bool a_active)
		{
			if (a_altar) {
				Services::SetAltarActive(a_altar, a_active);
			}
		}

		std::int32_t GetRefusalReason(Tag*, RE::Actor* a_spellmaker)
		{
			return a_spellmaker ? Services::RefusalReason(a_spellmaker) : 6;
		}

		void ReloadSettings(Tag*)
		{
			State::Get().ReloadSettings();
		}

		std::int32_t RebalanceAll(Tag*)
		{
			return State::Get().dataReady ? Spellbook::RebalanceAll() : -1;
		}

		std::int32_t RebuildAllSlots(Tag*)
		{
			return State::Get().dataReady ? Spellbook::RebuildAll() : -1;
		}

		bool PrepareForUninstall(Tag*)
		{
			return Spellbook::PrepareForUninstall();
		}

		RE::BSFixedString GetVersion(Tag*)
		{
			return fmt::format("{}.{}.{}", LOSTART_VERSION_MAJOR, LOSTART_VERSION_MINOR, LOSTART_VERSION_PATCH);
		}

		std::int32_t RunTests(Tag*, RE::BSFixedString a_suite)
		{
			const std::string suite = a_suite.empty() ? std::string("all") : std::string(a_suite.c_str());
			return Tests::Run(suite);
		}
	}

	bool Register(VM* a_vm)
	{
		if (!a_vm) {
			return false;
		}
		a_vm->RegisterFunction("OpenSpellmaking"sv, kScript, OpenSpellmaking);
		a_vm->RegisterFunction("IsCustomSpell"sv, kScript, IsCustomSpell);
		a_vm->RegisterFunction("GetCustomSpellCount"sv, kScript, GetCustomSpellCount);
		a_vm->RegisterFunction("GetFreeSlotCount"sv, kScript, GetFreeSlotCount);
		a_vm->RegisterFunction("GetFreeSubSlotCount"sv, kScript, GetFreeSubSlotCount);
		a_vm->RegisterFunction("DeleteCustomSpell"sv, kScript, DeleteCustomSpell);
		a_vm->RegisterFunction("SetAltarActive"sv, kScript, SetAltarActive);
		a_vm->RegisterFunction("GetRefusalReason"sv, kScript, GetRefusalReason);
		a_vm->RegisterFunction("ReloadSettings"sv, kScript, ReloadSettings);
		a_vm->RegisterFunction("RebalanceAll"sv, kScript, RebalanceAll);
		a_vm->RegisterFunction("RebuildAllSlots"sv, kScript, RebuildAllSlots);
		a_vm->RegisterFunction("PrepareForUninstall"sv, kScript, PrepareForUninstall);
		a_vm->RegisterFunction("GetVersion"sv, kScript, GetVersion);
		a_vm->RegisterFunction("RunTests"sv, kScript, RunTests);
		logger::info("Papyrus: {} natives registered on script {}", 14, kScript);
		return true;
	}

	void SendSpellEvent(std::string_view a_event, RE::SpellItem* a_spell)
	{
		// ModEvent through SKSE's mod callback source: scripts receive
		// Event OnX(string eventName, string strArg, float numArg, Form sender) with sender = spell.
		auto* source = SKSE::GetModCallbackEventSource();
		if (!source) {
			return;
		}
		SKSE::ModCallbackEvent event{ RE::BSFixedString(a_event), RE::BSFixedString(), 0.0f, a_spell };
		source->SendEvent(&event);
	}
}
