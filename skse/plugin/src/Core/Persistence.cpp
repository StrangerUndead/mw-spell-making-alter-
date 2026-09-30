#include "Core/Persistence.h"

#include "Core/SpellCompiler.h"
#include "Core/Spellbook.h"
#include "Effects/EffectSystems.h"

#include <fstream>

#ifdef GetObject
#	undef GetObject  // wingdi.h; BGSDefaultObjectManager::GetObject below
#endif

// Co-save persistence.
//
// Timing (SKSE64 source, Hooks_SaveLoad.cpp / Serialization.cpp): kPreLoadGame is dispatched
// before BGSSaveLoadManager loads the save; the revert callback runs when the game clears its
// global data at the start of that load; the load callback runs after the Papyrus VM's save data
// (global data table 3), i.e. after every change form, including the player's active effects,
// has been read; kPostLoadGame follows. Custom spells live in fixed slot records, so the player's
// spell list survives any order. Active effects are different: when the player's change form is
// read, each saved ActiveEffect is re-attached to its spell's Effect entries, so a slot that is
// still blank at that moment loses its buffs (the reload bug of other slot-based spellcrafting
// mods; OUTLINE "Why fixed slots"). Hence the early restore: at kPreLoadGame the plugin reads the
// LART definitions straight from the save's .skse file and compiles the slots before any change
// form loads; the revert callback re-applies them after blanking; the load callback then keeps
// that compilation when the co-save agrees with it (recompiling would detach the effects the
// engine just re-attached) and rebuilds from scratch otherwise.
namespace LA::Persistence
{
	namespace
	{
		struct EarlyRestore
		{
			bool                  pending{ false };   // parsed at kPreLoadGame, not yet confirmed
			bool                  applied{ false };   // slots currently hold these definitions
			std::vector<SpellDef> defs;
		};
		EarlyRestore g_early;

		std::optional<std::filesystem::path> g_lastCosave;
		std::size_t                          g_lastLoaded{ 0 };

		float DaysPassed()
		{
			const auto* calendar = RE::Calendar::GetSingleton();
			return calendar ? calendar->GetDaysPassed() : 0.0f;
		}

		// Equality that ignores the provider FormID (raw in the early parse, resolved in the load).
		bool SameDefinitions(const std::vector<SpellDef>& a_lhs, const std::vector<SpellDef>& a_rhs)
		{
			if (a_lhs.size() != a_rhs.size()) {
				return false;
			}
			for (std::size_t i = 0; i < a_lhs.size(); ++i) {
				auto left = a_lhs[i];
				auto right = a_rhs[i];
				left.provider = right.provider = 0;
				if (!(left == right)) {
					return false;
				}
			}
			return true;
		}

		void Apply(const std::vector<SpellDef>& a_defs, std::string_view a_why)
		{
			Spellbook::Revert();
			const auto built = Spellbook::Restore(a_defs);
			logger::info("{}: {} of {} custom spells compiled", a_why, built, a_defs.size());
		}

		bool Write(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type, const std::vector<std::uint8_t>& a_bytes)
		{
			static const std::uint8_t empty = 0;
			const void* data = a_bytes.empty() ? static_cast<const void*>(&empty) : a_bytes.data();
			if (!a_intfc->WriteRecord(a_type, kSchemaVersion, data, static_cast<std::uint32_t>(a_bytes.size()))) {
				logger::error("co-save: failed to write record {:08X} ({} bytes)", a_type, a_bytes.size());
				return false;
			}
			return true;
		}

		void OnSave(SKSE::SerializationInterface* a_intfc)
		{
			auto&                       state = State::Get();
			std::vector<SpellDef>       defs;
			std::vector<Mark>           marks;
			std::vector<LedgerEntry>    ledger;
			std::vector<ConditionEntry> conditions;
			VersionInfo                 version;
			{
				std::shared_lock guard(state.lock);
				defs.reserve(state.spells.size());
				for (const auto& [slot, custom] : state.spells) {
					defs.push_back(custom.def);
				}
				marks = state.marks;
				ledger = state.ledger;
				conditions = state.conditions;
				version = state.version;
			}
			version.schema = kSchemaVersion;

			std::size_t bytes = 0;
			auto        put = [&](std::uint32_t a_type, std::vector<std::uint8_t> a_data) {
				bytes += a_data.size();
				Write(a_intfc, a_type, a_data);
			};
			put(kRecordVersion, Serial::EncodeVersion(version));  // first: load reads it before the rest
			put(kRecordDefinitions, Serial::EncodeDefinitions(defs));
			put(kRecordMarks, Serial::EncodeMarks(marks));
			put(kRecordLedger, Serial::EncodeLedger(ledger));
			put(kRecordConditions, Serial::EncodeConditions(conditions));
			logger::info("co-save written: {} spells, {} marks, {} ledger, {} condition entries, {} bytes", defs.size(), marks.size(),
				ledger.size(), conditions.size(), bytes);
		}

		void OnLoad(SKSE::SerializationInterface* a_intfc)
		{
			auto&      state = State::Get();
			const auto resolve = [a_intfc](std::uint32_t a_old) -> std::optional<std::uint32_t> {
				if (a_old == 0) {
					return 0u;
				}
				RE::FormID fresh = 0;
				// ResolveFormID maps a FormID saved under the old load order to the current one and
				// fails when its plugin is gone (SKSE SerializationInterface).
				if (a_intfc->ResolveFormID(a_old, fresh)) {
					return fresh;
				}
				return std::nullopt;
			};

			std::vector<SpellDef>       defs;
			std::vector<Mark>           marks;
			std::vector<LedgerEntry>    ledger;
			std::vector<ConditionEntry> conditions;
			VersionInfo                 version;
			bool                        sawVersion = false;

			std::uint32_t type = 0;
			std::uint32_t recordVersion = 0;
			std::uint32_t length = 0;
			while (a_intfc->GetNextRecordInfo(type, recordVersion, length)) {
				std::vector<std::uint8_t> bytes(length);
				if (length > 0 && a_intfc->ReadRecordData(bytes.data(), length) != length) {
					logger::error("co-save: record {:08X} is truncated", type);
					continue;
				}
				if (recordVersion > kSchemaVersion) {
					logger::warn("co-save: record {:08X} has schema {} (this plugin knows {}); reading what it can", type, recordVersion,
						kSchemaVersion);
				}
				bool ok = true;
				switch (type) {
				case kRecordVersion:
					ok = Serial::DecodeVersion(bytes, recordVersion, version);
					sawVersion = ok;
					break;
				case kRecordDefinitions:
					ok = Serial::DecodeDefinitions(bytes, recordVersion, defs, resolve);
					break;
				case kRecordMarks:
					ok = Serial::DecodeMarks(bytes, recordVersion, marks, resolve);
					break;
				case kRecordLedger:
					ok = Serial::DecodeLedger(bytes, recordVersion, ledger, resolve);
					break;
				case kRecordConditions:
					ok = Serial::DecodeConditions(bytes, recordVersion, conditions, resolve);
					break;
				default:
					logger::warn("co-save: unknown record {:08X} skipped", type);
					break;
				}
				if (!ok) {
					logger::error("co-save: record {:08X} (schema {}) failed to decode", type, recordVersion);
				}
			}

			// Migrations: records decode by their own version; the history notes each upgrade.
			if (!sawVersion) {
				version = VersionInfo{};
				version.schema = 0;
			}
			if (version.schema < kSchemaVersion) {
				logger::info("co-save: migrating schema {} -> {}", version.schema, kSchemaVersion);
				version.history.emplace_back(kSchemaVersion, DaysPassed());
				version.schema = kSchemaVersion;
			}

			if (g_early.applied && SameDefinitions(g_early.defs, defs)) {
				// Slots already hold exactly these spells: keep them (and the active effects the
				// engine re-attached); only take the resolved provider ids.
				std::unique_lock guard(state.lock);
				for (const auto& def : defs) {
					if (auto it = state.spells.find(def.slot); it != state.spells.end()) {
						it->second.def.provider = def.provider;
					}
				}
				logger::info("co-save loaded: {} custom spells (already compiled before the save loaded)", defs.size());
			} else {
				if (g_early.applied) {
					logger::warn("co-save: the early restore disagreed with the co-save; rebuilding (active custom buffs may be lost)");
				}
				Apply(defs, "co-save loaded");
			}
			g_early = EarlyRestore{};
			g_lastLoaded = defs.size();

			{
				std::unique_lock guard(state.lock);
				state.marks = std::move(marks);
				state.ledger = std::move(ledger);
				state.conditions = std::move(conditions);
				state.version = std::move(version);
			}
		}

		void OnRevert(SKSE::SerializationInterface*)
		{
			// Once per game load/new game, and before anything is blanked: Effect objects retired
			// two sessions ago become reusable (Core/SpellCompiler.cpp, EffectPool).
			Compiler::OnRevert();
			Spellbook::Revert();
			Effects::Revert();
			if (g_early.pending && !g_early.defs.empty()) {
				const auto built = Spellbook::Restore(g_early.defs);
				g_early.applied = true;
				logger::info("revert: {} custom spells re-applied for the save being loaded", built);
			}
		}

		std::optional<std::filesystem::path> CosavePath(std::string_view a_saveName)
		{
			std::filesystem::path save{ std::string(a_saveName) };
			if (!save.is_absolute()) {
				// Documents/My Games/<Skyrim Special Edition[ GOG]>/<sLocalSavePath>/<name>
				auto logs = SKSE::log::log_directory();
				if (!logs) {
					return std::nullopt;
				}
				std::string local = "Saves\\";
				if (auto* setting = RE::GetINISetting("sLocalSavePath:General")) {
					if (const char* value = setting->GetString(); value && *value) {
						local = value;
					}
				}
				save = logs->parent_path() / local / save;
			}
			if (save.extension() == ".ess") {
				save.replace_extension(".skse");
			} else {
				save += ".skse";
			}
			return save;
		}

		std::uint32_t ReadU32(const std::vector<std::uint8_t>& a_bytes, std::size_t& a_pos, bool& a_ok)
		{
			if (!a_ok || a_pos + 4 > a_bytes.size()) {
				a_ok = false;
				return 0;
			}
			const std::uint32_t value = static_cast<std::uint32_t>(a_bytes[a_pos]) | (static_cast<std::uint32_t>(a_bytes[a_pos + 1]) << 8) |
			                            (static_cast<std::uint32_t>(a_bytes[a_pos + 2]) << 16) |
			                            (static_cast<std::uint32_t>(a_bytes[a_pos + 3]) << 24);
			a_pos += 4;
			return value;
		}
	}

	std::optional<std::vector<SpellDef>> ParseCosave(const std::vector<std::uint8_t>& a_bytes, std::string* a_error)
	{
		// SKSE64 co-save layout (skse64/Serialization.cpp): Header { signature 'SKSE' (the file
		// starts with the ASCII bytes "SKSE"), formatVersion 1, skseVersion, runtimeVersion,
		// numPlugins }, then per plugin PluginHeader { uid, numChunks, length of its chunks
		// including their headers } and ChunkHeader { type, version, length } + data per chunk.
		auto fail = [&](std::string a_text) -> std::optional<std::vector<SpellDef>> {
			if (a_error) {
				*a_error = std::move(a_text);
			}
			return std::nullopt;
		};
		std::size_t pos = 0;
		bool        ok = true;
		const bool  signature = a_bytes.size() >= 4 && a_bytes[0] == 'S' && a_bytes[1] == 'K' && a_bytes[2] == 'S' && a_bytes[3] == 'E';
		pos = 4;
		const auto format = ReadU32(a_bytes, pos, ok);
		ReadU32(a_bytes, pos, ok);  // skseVersion
		ReadU32(a_bytes, pos, ok);  // runtimeVersion
		const auto plugins = ReadU32(a_bytes, pos, ok);
		if (!ok || !signature) {
			return fail("not an SKSE co-save");
		}
		if (format != 1) {
			return fail(fmt::format("unknown co-save format {}", format));
		}
		if (plugins > 4096) {
			return fail("implausible plugin count");
		}
		for (std::uint32_t p = 0; p < plugins; ++p) {
			const auto uid = ReadU32(a_bytes, pos, ok);
			const auto chunks = ReadU32(a_bytes, pos, ok);
			const auto length = ReadU32(a_bytes, pos, ok);
			if (!ok || pos + length > a_bytes.size()) {
				return fail("truncated plugin block");
			}
			const auto end = pos + length;
			if (uid != kCosaveId) {
				pos = end;
				continue;
			}
			for (std::uint32_t c = 0; c < chunks && pos < end; ++c) {
				const auto type = ReadU32(a_bytes, pos, ok);
				const auto version = ReadU32(a_bytes, pos, ok);
				const auto size = ReadU32(a_bytes, pos, ok);
				if (!ok || pos + size > end) {
					return fail("truncated LART chunk");
				}
				if (type == kRecordDefinitions) {
					std::vector<std::uint8_t> data(a_bytes.begin() + static_cast<std::ptrdiff_t>(pos),
						a_bytes.begin() + static_cast<std::ptrdiff_t>(pos + size));
					std::vector<SpellDef> defs;
					if (!Serial::DecodeDefinitions(data, version, defs)) {
						return fail("LADF does not decode");
					}
					return defs;
				}
				pos += size;
			}
			return std::vector<SpellDef>{};  // LART present without definitions
		}
		return std::vector<SpellDef>{};  // no LART block: a save made without the mod
	}

	bool Install()
	{
		const auto* serialization = SKSE::GetSerializationInterface();
		if (!serialization) {
			logger::critical("no SKSE serialization interface");
			return false;
		}
		serialization->SetUniqueID(kCosaveId);
		serialization->SetRevertCallback(OnRevert);
		serialization->SetSaveCallback(OnSave);
		serialization->SetLoadCallback(OnLoad);
		return true;
	}

	void OnPreLoadGame(const char* a_saveName, std::uint32_t a_length)
	{
		g_early = EarlyRestore{};
		if (!a_saveName || a_length == 0 || !State::Get().dataReady) {
			return;
		}
		const std::string_view name(a_saveName, std::strlen(a_saveName) < a_length ? std::strlen(a_saveName) : a_length);
		const auto             path = CosavePath(name);
		g_lastCosave = path;
		if (!path) {
			return;
		}
		std::ifstream file(*path, std::ios::binary);
		if (!file) {
			logger::debug("early restore: no co-save at {}", path->string());
			return;
		}
		std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		std::string               error;
		auto                      defs = ParseCosave(bytes, &error);
		if (!defs) {
			logger::warn("early restore skipped ({}): {}", path->filename().string(), error);
			return;
		}
		g_early.pending = true;
		g_early.defs = std::move(*defs);
		// Applied now and again after the revert callback blanks the slots (whichever comes last
		// wins; both compile the same definitions).
		Apply(g_early.defs, "early restore");
		g_early.applied = true;
	}

	void OnPostLoadGame(bool a_success)
	{
		auto& state = State::Get();
		if (g_early.pending && !g_early.defs.empty()) {
			// No LART data came through the load callback although the file had some: keep the
			// early compilation, it came from this very save.
			logger::warn("post-load: the co-save load callback did not run; keeping the early restore ({} spells)", g_early.defs.size());
		}
		g_early = EarlyRestore{};
		if (!a_success) {
			return;
		}

		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			std::size_t missing = 0;
			for (const auto& [slot, custom] : state.spells) {
				if (custom.primary && !player->HasSpell(custom.primary)) {
					++missing;
					logger::warn("post-load: '{}' (slot {}) is defined but not in the player's spell list", custom.def.name, slot);
				}
			}
			logger::info("post-load: {} custom spells, {} free slots, {} free sub slots, {} missing from the spell list", state.spells.size(),
				state.slots.FreePrimaryCount(), state.slots.FreeSubCount(), missing);
		}
		Effects::OnGameLoaded();
		// Next frame: the player's 3D and equip state are settled by then.
		if (const auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask([] { ReequipHands(); });
		}
	}

	void OnNewGame()
	{
		g_early = EarlyRestore{};
		g_lastLoaded = 0;
		if (!State::Get().spells.empty()) {
			Spellbook::Revert();  // the revert callback normally did this already
		}
	}

	std::optional<std::filesystem::path> LastCosavePath() { return g_lastCosave; }
	std::size_t                          LastLoadedCount() { return g_lastLoaded; }

	void ReequipHands()
	{
		auto& state = State::Get();
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* defaults = RE::BGSDefaultObjectManager::GetSingleton();
		auto* equip = RE::ActorEquipManager::GetSingleton();
		if (!player || !defaults || !equip) {
			return;
		}
		const std::array<std::pair<bool, RE::DEFAULT_OBJECT>, 2> hands{ { { true, RE::DEFAULT_OBJECT::kLeftHandEquip },
			{ false, RE::DEFAULT_OBJECT::kRightHandEquip } } };
		for (const auto& [left, slotId] : hands) {
			auto* form = player->GetEquippedObject(left);
			auto* spell = form ? form->As<RE::SpellItem>() : nullptr;
			if (!spell || !state.IsCustomSpell(spell)) {
				continue;
			}
			// VERIFY(in-game): equipping the already-selected spell again rebuilds its hand art.
			equip->EquipSpell(player, spell, defaults->GetObject<RE::BGSEquipSlot>(slotId));
			logger::debug("re-equipped '{}' in the {} hand", spell->GetName(), left ? "left" : "right");
		}
	}
}
