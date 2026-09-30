#include "Console/Commands.h"

#include "Core/Spellbook.h"
#include "Tests/InGameTests.h"
#include "UI/SpellmakingMenu.h"

#include <fmt/ranges.h>

// Console command `la` / `LostArt`.
//
// Skyrim has no API for new console commands; plugins repurpose an unused entry of the engine's
// console command table (SCRIPT_FUNCTION) by renaming it and replacing its execute callback, as
// powerof3's plugins do (PhotoMode src/Console.h, SeasonsOfSkyrim "SetStackDepth",
// CameraPersistenceFixes "TestDegrade") and others ("GetLegalDocs"/"AcceptLegalDoc" in
// NPCsUsePotions, "RecvAnimEvent" in anim-event-logger, "DumpNiUpdates" in skse-qui). We take the
// first entry of that list of retail no-ops that is still unmodified: LocateConsoleCommand matches
// the original long name, so an entry another plugin already renamed is skipped automatically.
namespace LA::Console
{
	namespace
	{
		constexpr std::array kCandidates{ "GetLegalDocs"sv, "AcceptLegalDoc"sv, "RecvAnimEvent"sv, "TestDegrade"sv, "DumpNiUpdates"sv };

		void Print(const std::string& a_text)
		{
			if (auto* console = RE::ConsoleLog::GetSingleton()) {
				console->Print("%s", a_text.c_str());
			}
			logger::info("console: {}", a_text);
		}

		void Help()
		{
			Print("LostArt: la menu | la test <parity|effects|save|compiler|all> | la discover | la slots | la rebuild");
		}

		void Discover()
		{
			auto& state = State::Get();
			auto* player = RE::PlayerCharacter::GetSingleton();
			const auto spells = Spellbook::KnownSpells(player);
			const auto result = state.discovery.Discover(state.catalog, spells, state.settings);
			logger::info("la discover: {} known spells, {} effects discovered, {} pass-through, {} unmatched", spells.size(),
				result.effects.size(), result.passThrough.size(), result.unmatched.size());
			for (const auto& id : result.effects) {
				logger::info("  known: {}{}", id, state.EffectUsable(id) ? "" : " (hidden: variants missing)");
			}
			for (const auto& spell : spells) {
				for (const auto& effect : spell.effects) {
					if (state.discovery.Classify(effect)) {
						continue;
					}
					logger::info("  unmatched: {} '{}' [{}] archetype={} av={} resist={} hostile={} {} {} keywords=[{}]", effect.form.ToString(),
						effect.name, spell.type, effect.archetype, effect.actorValue, effect.resist, effect.hostile, effect.castingType,
						effect.delivery, fmt::join(effect.keywords, ","));
				}
			}
			Print(fmt::format("LostArt: {} effects known ({} pass-through), {} unmatched effects; details in LostArt.log", result.effects.size(),
				result.passThrough.size(), result.unmatched.size()));
		}

		void Slots()
		{
			auto&            state = State::Get();
			std::shared_lock guard(state.lock);
			Print(fmt::format("LostArt: {} custom spells; slots {}/{} used, sub slots {}/{} used", state.spells.size(),
				state.slots.UsedPrimaryCount(), state.slots.PrimaryCapacity(), state.slots.UsedSubCount(), state.slots.SubCapacity()));
			for (const auto& [slot, custom] : state.spells) {
				Print(fmt::format("  {:03} '{}' cost {} subs [{}]", slot, custom.def.name, custom.def.cost, fmt::join(custom.def.subSlots, ",")));
			}
		}

		void Run(std::string a_line)
		{
			auto tokens = Split(Trim(a_line), ' ');
			std::erase_if(tokens, [](const std::string& a_token) { return a_token.empty(); });
			// The console line starts with the command itself ("la ..." / "LostArt ...").
			if (!tokens.empty() && (IEquals(tokens.front(), "la") || IEquals(tokens.front(), "LostArt"))) {
				tokens.erase(tokens.begin());
			}
			const std::string sub = tokens.empty() ? std::string() : ToLower(tokens[0]);
			const std::string arg = tokens.size() > 1 ? ToLower(tokens[1]) : std::string();

			if (!State::Get().dataReady && sub != "slots") {
				Print("LostArt: data not loaded (see LostArt.log)");
				return;
			}
			if (sub == "menu") {
				Provider provider;
				provider.kind = Provider::Kind::kAltar;
				provider.name = "Console";
				provider.freeService = true;
				UI::OpenSpellmakingMenu(provider, nullptr);
			} else if (sub == "test") {
				const auto suite = arg.empty() ? std::string("all") : arg;
				const int  failures = Tests::Run(suite);
				if (failures < 0) {
					Print(fmt::format("LostArt: unknown test suite '{}'", suite));
				} else {
					Print(fmt::format("LostArt: tests '{}' done, {} failed; see SKSE/LostArt_Tests.log", suite, failures));
				}
			} else if (sub == "discover") {
				Discover();
			} else if (sub == "slots") {
				Slots();
			} else if (sub == "rebuild") {
				Print(fmt::format("LostArt: {} spells rebuilt", Spellbook::RebuildAll()));
			} else {
				Help();
			}
		}

		bool Execute(const RE::SCRIPT_PARAMETER*, RE::SCRIPT_FUNCTION::ScriptData*, RE::TESObjectREFR*, RE::TESObjectREFR*,
			RE::Script* a_script, RE::ScriptLocals*, double&, std::uint32_t&)
		{
			// The whole console line; the optional string parameters only let the console's
			// compiler accept the extra words.
			std::string line = a_script ? a_script->GetCommand() : std::string();
			RunOnMainThread([line = std::move(line)]() { Run(line); });
			return true;
		}

		RE::SCRIPT_PARAMETER g_params[] = {
			{ "Subcommand", RE::SCRIPT_PARAM_TYPE::kChar, true },
			{ "Argument", RE::SCRIPT_PARAM_TYPE::kChar, true },
		};
	}

	void Install()
	{
		for (const auto name : kCandidates) {
			auto* command = RE::SCRIPT_FUNCTION::LocateConsoleCommand(name);
			if (!command) {
				continue;
			}
			command->functionName = "LostArt";
			command->shortName = "la";
			command->helpString = "Lost Art of Spellmaking: la menu | test <suite> | discover | slots | rebuild";
			command->referenceFunction = false;
			command->SetParameters(g_params);
			command->executeFunction = &Execute;
			command->conditionFunction = nullptr;
			command->editorFilter = false;
			command->invalidatesCellList = false;
			logger::info("console command 'la' installed (replaces unused '{}')", name);
			return;
		}
		logger::warn("console command 'la' not installed: no free console command slot");
	}
}
