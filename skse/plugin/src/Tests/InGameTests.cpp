#include "Tests/InGameTests.h"

#include "Core/Persistence.h"
#include "Core/SpellCompiler.h"
#include "Core/Spellbook.h"

#include <fstream>

// In-game scripted checks (CONTRACTS section 9): the registry and runner, plus the foundation's
// suites "compiler" and "save". Other components register "parity" and "effects" cases through
// Tests::Register at install time. Everything runs on the main thread (Papyrus natives without
// NoWait, the console command via RunOnMainThread).
namespace LA::Tests
{
	namespace
	{
		struct Entry
		{
			std::string name;
			Case        run;
		};

		std::mutex&                                  RegistryLock()
		{
			static std::mutex lock;
			return lock;
		}
		std::map<std::string, std::vector<Entry>>& Registry()
		{
			static std::map<std::string, std::vector<Entry>> suites;
			return suites;
		}

		constexpr std::array kOrder{ "parity"sv, "effects"sv, "save"sv, "compiler"sv };

		// --- helpers ----------------------------------------------------------------------------

		// Record content as values (Effect objects may be recycled; compare what they hold).
		struct Signature
		{
			std::vector<std::tuple<RE::FormID, float, std::uint32_t, std::uint32_t, float>> effects;
			std::int32_t  costOverride{ 0 };
			bool          costFlag{ false };
			int           castingType{ 0 };
			int           delivery{ 0 };
			RE::FormID    perk{ 0 };
			std::string   name;
			std::int32_t  hostile{ 0 };
			bool operator==(const Signature&) const = default;
		};

		Signature Sign(const RE::SpellItem* a_record)
		{
			Signature out;
			if (!a_record) {
				return out;
			}
			for (const auto* effect : a_record->effects) {
				if (effect) {
					out.effects.emplace_back(effect->baseEffect ? effect->baseEffect->GetFormID() : 0, effect->effectItem.magnitude,
						effect->effectItem.duration, effect->effectItem.area, effect->cost);
				}
			}
			out.costOverride = a_record->data.costOverride;
			out.costFlag = a_record->data.flags.any(RE::SpellItem::SpellFlag::kCostOverride);
			out.castingType = static_cast<int>(a_record->data.castingType);
			out.delivery = static_cast<int>(a_record->data.delivery);
			out.perk = a_record->data.castingPerk ? a_record->data.castingPerk->GetFormID() : 0;
			out.name = a_record->GetName() ? a_record->GetName() : "";
			out.hostile = a_record->hostileCount;
			return out;
		}

		// Temporarily claims free sub slots (highest first) as scratch records for a test.
		class Scratch
		{
		public:
			explicit Scratch(std::size_t a_count)
			{
				auto&            state = State::Get();
				std::unique_lock guard(state.lock);
				for (auto i = static_cast<int>(state.slots.SubCapacity()) - 1; i >= 0 && _slots.size() < a_count; --i) {
					const auto slot = static_cast<std::uint16_t>(i);
					if (slot < state.subSlots.size() && state.subSlots[slot] && state.slots.ClaimSub(slot)) {
						_slots.push_back(slot);
					}
				}
			}
			~Scratch()
			{
				auto& state = State::Get();
				for (const auto slot : _slots) {
					Spellbook::Blank(state.subSlots[slot]);
				}
				std::unique_lock guard(state.lock);
				for (const auto slot : _slots) {
					state.slots.FreeSub(slot);
				}
			}
			Scratch(const Scratch&) = delete;
			Scratch& operator=(const Scratch&) = delete;

			bool           Ok(std::size_t a_count) const { return _slots.size() >= a_count; }
			RE::SpellItem* Record(std::size_t a_index) const { return State::Get().subSlots[_slots[a_index]]; }

		private:
			std::vector<std::uint16_t> _slots;
		};

		std::string Join(const std::vector<std::string>& a_parts)
		{
			std::string out;
			for (const auto& part : a_parts) {
				if (!out.empty()) {
					out += "; ";
				}
				out += part;
			}
			return out;
		}

		// --- suite "compiler" -------------------------------------------------------------------

		Result CompileEffect(const std::string& a_id)
		{
			auto&       state = State::Get();
			const auto* def = state.catalog.Find(a_id);
			if (!def) {
				return Fail("effect not in catalog");
			}
			if (state.hiddenEffects.contains(a_id)) {
				return Fail("expected every variant in LostArt.esp, got missing variants (effect hidden; see LostArt.log)");
			}
			std::vector<std::string> problems;
			std::size_t              compiled = 0;
			for (const auto range : def->AllowedRanges()) {
				SpellEffect effect;
				effect.effectId = a_id;
				effect.sub = def->target == TargetKind::kNone ? kNoSub : std::int16_t{ 0 };  // first target of a family
				effect.range = range;
				effect.minMag = def->hasMagnitude ? 5 : 1;
				effect.maxMag = def->hasMagnitude ? 10 : 1;
				effect.duration = def->hasDuration ? 10 : 1;
				effect.area = def->ShowsArea(range) ? 5 : 0;

				SpellDef spell;
				spell.name = "LostArt test";
				spell.effects = { effect };
				spell.costModel = state.settings.costModel;
				spell.cost = std::max<std::uint32_t>(1, Cost::Compute(state.catalog, spell.effects, state.settings, spell.costModel,
															  state.BaseCostFn())
															.cost);
				const auto plan = Compiler::Plan(state.catalog, spell.effects, state.settings, state.content.Riders());
				const auto where = std::string(ToString(range));
				if (plan.spells.empty() || plan.tooComplex) {
					problems.push_back(where + ": expected a plan within 15 effects, got " + (plan.tooComplex ? "too complex" : "empty"));
					continue;
				}

				Scratch scratch(plan.spells.size());
				if (!scratch.Ok(plan.spells.size())) {
					return Fail("expected free scratch sub slots, got a full sub-slot pool");
				}
				for (std::size_t i = 0; i < plan.spells.size(); ++i) {
					auto*       record = scratch.Record(i);
					const auto& planned = plan.spells[i];
					std::string error;
					if (!Spellbook::CompileInto(record, spell, plan, i, &error)) {
						problems.push_back(where + ": compile failed: " + error);
						continue;
					}
					++compiled;
					const auto expected = Compiler::Expected(planned.range);
					const auto count = record->effects.size();
					if (count == 0 || count > kEngineEffectCeiling || count > planned.entries.size()) {
						problems.push_back(fmt::format("{}#{}: expected 1..{} effects, got {}", where, i,
							std::min(planned.entries.size(), kEngineEffectCeiling), count));
					}
					if (record->data.castingType != expected.castingType || record->data.delivery != expected.delivery) {
						problems.push_back(fmt::format("{}#{}: expected FireAndForget/{}, got {}/{}", where, i, static_cast<int>(expected.delivery),
							static_cast<int>(record->data.castingType), static_cast<int>(record->data.delivery)));
					}
					for (const auto* item : record->effects) {
						const auto* mgef = item ? item->baseEffect : nullptr;
						if (!mgef) {
							problems.push_back(where + ": expected every effect bound, got a null MGEF");
							continue;
						}
						if (mgef == state.blankEffect) {
							problems.push_back(where + ": expected the variant, got the placeholder");
							continue;
						}
						if (mgef->data.castingType != RE::MagicSystem::CastingType::kFireAndForget) {
							problems.push_back(fmt::format("{}: expected {:08X} FireAndForget, got casting type {}", where, mgef->GetFormID(),
								static_cast<int>(mgef->data.castingType)));
						}
						// Our variants must match the spell's delivery; vanilla riders are exempt.
						if (!state.forms.EditorIdOf(mgef->GetFormID()).empty() && mgef->data.delivery != expected.delivery) {
							problems.push_back(fmt::format("{}: expected {} delivery {}, got {}", where,
								state.forms.EditorIdOf(mgef->GetFormID()), static_cast<int>(expected.delivery),
								static_cast<int>(mgef->data.delivery)));
						}
					}
					const bool          flag = record->data.flags.any(RE::SpellItem::SpellFlag::kCostOverride);
					const std::int32_t  wantCost = i == 0 ? static_cast<std::int32_t>(spell.cost) : 0;
					if (!flag || record->data.costOverride != wantCost) {
						problems.push_back(fmt::format("{}#{}: expected cost override {}, got {}{}", where, i, wantCost,
							record->data.costOverride, flag ? "" : " (flag off)"));
					}
					if (record->IsHostile() != planned.hostile) {
						problems.push_back(fmt::format("{}#{}: expected hostile {}, got {}", where, i, planned.hostile, record->IsHostile()));
					}
					Spellbook::Blank(record);
					if (!Spellbook::IsBlank(record)) {
						problems.push_back(where + ": expected a blank slot after Blank, got leftovers");
					}
				}
			}
			if (!problems.empty()) {
				return Fail(Join(problems));
			}
			return Pass(fmt::format("{} spell record(s) over {} range(s)", compiled, def->AllowedRanges().size()));
		}

		// --- suite "save" -----------------------------------------------------------------------

		Result SlotIntegrity()
		{
			auto&                    state = State::Get();
			auto*                    player = RE::PlayerCharacter::GetSingleton();
			std::vector<std::string> problems;
			for (const auto& [slot, custom] : state.spells) {
				const auto* primary = slot < state.primarySlots.size() ? state.primarySlots[slot] : nullptr;
				if (!primary || custom.primary != primary) {
					problems.push_back(fmt::format("slot {}: expected its LA_Slot record, got another", slot));
					continue;
				}
				if (primary->data.costOverride != static_cast<std::int32_t>(custom.def.cost) ||
					!primary->data.flags.any(RE::SpellItem::SpellFlag::kCostOverride)) {
					problems.push_back(fmt::format("'{}': expected cost override {}, got {}", custom.def.name, custom.def.cost,
						primary->data.costOverride));
				}
				const std::string name = primary->GetName() ? primary->GetName() : "";
				if (name != custom.def.name) {
					problems.push_back(fmt::format("slot {}: expected name '{}', got '{}'", slot, custom.def.name, name));
				}
				if (player && !player->HasSpell(const_cast<RE::SpellItem*>(primary))) {
					problems.push_back(fmt::format("'{}': expected in the player's spell list, got missing", custom.def.name));
				}
				if (custom.subs.size() != custom.plan.SubSlotsNeeded()) {
					problems.push_back(fmt::format("'{}': expected {} linked sub-spells, got {}", custom.def.name, custom.plan.SubSlotsNeeded(),
						custom.subs.size()));
				}
				for (const auto* sub : custom.subs) {
					if (!sub || Spellbook::IsBlank(sub) || sub->data.costOverride != 0) {
						problems.push_back(fmt::format("'{}': expected a compiled zero-cost sub-spell, got blank or costed", custom.def.name));
					}
				}
			}
			if (!problems.empty()) {
				return Fail(Join(problems));
			}
			return Pass(fmt::format("{} custom spells consistent", state.spells.size()));
		}

		Result PoolCounts()
		{
			auto&       state = State::Get();
			std::size_t subs = 0;
			std::vector<std::string> problems;
			for (const auto& [slot, custom] : state.spells) {
				subs += custom.def.subSlots.size();
				if (!state.slots.PrimaryUsed(slot)) {
					problems.push_back(fmt::format("slot {}: expected claimed, got free", slot));
				}
				for (const auto sub : custom.def.subSlots) {
					if (!state.slots.SubUsed(sub) || sub >= state.subOwner.size() || state.subOwner[sub] != slot) {
						problems.push_back(fmt::format("sub slot {}: expected owned by {}, got otherwise", sub, slot));
					}
				}
			}
			if (state.slots.UsedPrimaryCount() != state.spells.size()) {
				problems.push_back(fmt::format("primary pool: expected {} used, got {}", state.spells.size(), state.slots.UsedPrimaryCount()));
			}
			if (state.slots.UsedSubCount() != subs) {
				problems.push_back(fmt::format("sub pool: expected {} used, got {}", subs, state.slots.UsedSubCount()));
			}
			// Every free slot record is blank (nothing leaked from a previous save or deletion).
			for (std::uint16_t i = 0; i < state.primarySlots.size(); ++i) {
				if (!state.slots.PrimaryUsed(i) && !Spellbook::IsBlank(state.primarySlots[i])) {
					problems.push_back(fmt::format("LA_Slot_{:03}: expected blank (free), got content", i));
				}
			}
			for (std::uint16_t i = 0; i < state.subSlots.size(); ++i) {
				if (!state.slots.SubUsed(i) && !Spellbook::IsBlank(state.subSlots[i])) {
					problems.push_back(fmt::format("LA_Sub_{:03}: expected blank (free), got content", i));
				}
			}
			if (!problems.empty()) {
				return Fail(Join(problems));
			}
			return Pass(fmt::format("{} primary + {} sub slots in use", state.spells.size(), subs));
		}

		std::vector<SpellDef> CurrentDefs()
		{
			std::vector<SpellDef> defs;
			for (const auto& [slot, custom] : State::Get().spells) {
				defs.push_back(custom.def);
			}
			return defs;
		}

		Result CosaveRoundTrip()
		{
			auto&       state = State::Get();
			const auto  identity = [](std::uint32_t a_id) -> std::optional<std::uint32_t> { return a_id; };
			const auto  defs = CurrentDefs();
			std::vector<std::string> problems;

			std::vector<SpellDef> defsOut;
			if (!Serial::DecodeDefinitions(Serial::EncodeDefinitions(defs), kSchemaVersion, defsOut, identity) || defsOut != defs) {
				problems.push_back("LADF: expected identical definitions, got a difference");
			}
			std::vector<Mark> marks;
			if (!Serial::DecodeMarks(Serial::EncodeMarks(state.marks), kSchemaVersion, marks, identity) || marks != state.marks) {
				problems.push_back("LAMK: expected identical marks, got a difference");
			}
			std::vector<LedgerEntry> ledger;
			if (!Serial::DecodeLedger(Serial::EncodeLedger(state.ledger), kSchemaVersion, ledger, identity) || ledger != state.ledger) {
				problems.push_back("LALG: expected identical ledger, got a difference");
			}
			std::vector<ConditionEntry> conditions;
			if (!Serial::DecodeConditions(Serial::EncodeConditions(state.conditions), kSchemaVersion, conditions, identity) ||
				conditions != state.conditions) {
				problems.push_back("LACP: expected identical condition pools, got a difference");
			}
			VersionInfo version;
			if (!Serial::DecodeVersion(Serial::EncodeVersion(state.version), kSchemaVersion, version) || !(version == state.version)) {
				problems.push_back("LAVR: expected identical version info, got a difference");
			}
			const auto size = Serial::EncodeDefinitions(defs).size();
			if (!problems.empty()) {
				return Fail(Join(problems));
			}
			return Pass(fmt::format("{} definitions, LADF {} bytes", defs.size(), size));
		}

		void PutU32(std::vector<std::uint8_t>& a_out, std::uint32_t a_value)
		{
			for (int i = 0; i < 4; ++i) {
				a_out.push_back(static_cast<std::uint8_t>(a_value >> (8 * i)));
			}
		}

		Result CosaveParser()
		{
			// A synthetic .skse image in SKSE64's layout with a foreign plugin block before ours.
			const auto defs = CurrentDefs();
			const auto ladf = Serial::EncodeDefinitions(defs);
			const auto lavr = Serial::EncodeVersion(State::Get().version);

			std::vector<std::uint8_t> file{ 'S', 'K', 'S', 'E' };
			PutU32(file, 1);           // format
			PutU32(file, 0x02000000);  // skse version
			PutU32(file, 0x01060480);  // runtime
			PutU32(file, 2);           // plugins
			PutU32(file, FourCC("TEST"));
			PutU32(file, 1);
			PutU32(file, 12 + 3);
			PutU32(file, FourCC("XXXX"));
			PutU32(file, 1);
			PutU32(file, 3);
			file.insert(file.end(), { 1, 2, 3 });
			PutU32(file, kCosaveId);
			PutU32(file, 2);
			PutU32(file, static_cast<std::uint32_t>(24 + lavr.size() + ladf.size()));
			PutU32(file, kRecordVersion);
			PutU32(file, kSchemaVersion);
			PutU32(file, static_cast<std::uint32_t>(lavr.size()));
			file.insert(file.end(), lavr.begin(), lavr.end());
			PutU32(file, kRecordDefinitions);
			PutU32(file, kSchemaVersion);
			PutU32(file, static_cast<std::uint32_t>(ladf.size()));
			file.insert(file.end(), ladf.begin(), ladf.end());

			std::string error;
			const auto  parsed = Persistence::ParseCosave(file, &error);
			if (!parsed) {
				return Fail("expected the synthetic co-save to parse, got: " + error);
			}
			if (*parsed != defs) {
				return Fail(fmt::format("expected {} definitions back, got {} (or different content)", defs.size(), parsed->size()));
			}
			std::vector<std::uint8_t> truncated(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(file.size() / 2));
			if (!defs.empty() && Persistence::ParseCosave(truncated)) {
				return Fail("expected a truncated co-save to be rejected, got accepted");
			}
			return Pass(fmt::format("{} definitions through the early-restore parser", defs.size()));
		}

		Result CosaveFile()
		{
			const auto path = Persistence::LastCosavePath();
			if (!path) {
				return Pass("no save loaded this session (nothing to read)");
			}
			std::ifstream file(*path, std::ios::binary);
			if (!file) {
				return Pass("the loaded save has no co-save file: " + path->filename().string());
			}
			std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			std::string               error;
			const auto                parsed = Persistence::ParseCosave(bytes, &error);
			if (!parsed) {
				return Fail(fmt::format("expected {} to parse, got: {}", path->filename().string(), error));
			}
			return Pass(fmt::format("{}: {} definitions on disk, {} restored at load", path->filename().string(), parsed->size(),
				Persistence::LastLoadedCount()));
		}

		Result RebuildIdempotent()
		{
			auto&                  state = State::Get();
			std::vector<Signature> before;
			for (const auto& [slot, custom] : state.spells) {
				before.push_back(Sign(custom.primary));
				for (const auto* sub : custom.subs) {
					before.push_back(Sign(sub));
				}
			}
			const auto built = Spellbook::RebuildAll();
			std::vector<Signature> after;
			for (const auto& [slot, custom] : state.spells) {
				after.push_back(Sign(custom.primary));
				for (const auto* sub : custom.subs) {
					after.push_back(Sign(sub));
				}
			}
			if (built != static_cast<int>(state.spells.size())) {
				return Fail(fmt::format("expected {} spells rebuilt, got {}", state.spells.size(), built));
			}
			if (before != after) {
				return Fail("expected identical slot records after a rebuild, got differences");
			}
			return Pass(fmt::format("{} records unchanged", after.size()));
		}

		Result RevertRebuildCopy()
		{
			// What a revert + load would compile, written into scratch records and compared with the
			// live ones (the live slots are not touched).
			auto&                    state = State::Get();
			std::vector<std::string> problems;
			std::size_t              compared = 0;
			for (const auto& [slot, custom] : state.spells) {
				const auto plan = Spellbook::PlanFor(custom.def);
				Scratch    scratch(plan.spells.size());
				if (!scratch.Ok(plan.spells.size())) {
					return Fail("expected free scratch sub slots, got a full sub-slot pool");
				}
				for (std::size_t i = 0; i < plan.spells.size(); ++i) {
					auto* copy = scratch.Record(i);
					if (!Spellbook::CompileInto(copy, custom.def, plan, i)) {
						problems.push_back(fmt::format("'{}'#{}: compile failed", custom.def.name, i));
						continue;
					}
					const auto* live = i == 0 ? custom.primary : (i - 1 < custom.subs.size() ? custom.subs[i - 1] : nullptr);
					if (!(Sign(copy) == Sign(live))) {
						problems.push_back(fmt::format("'{}'#{}: expected the rebuilt copy to equal the live record, got a difference",
							custom.def.name, i));
					}
					++compared;
				}
			}
			if (!problems.empty()) {
				return Fail(Join(problems));
			}
			return Pass(fmt::format("{} records rebuilt on a copy and identical", compared));
		}

		void RegisterCoreSuites()
		{
			auto& state = State::Get();
			for (const auto& def : state.catalog.All()) {
				if (def.set == EffectSet::kPassThrough) {
					continue;
				}
				Register("compiler", def.id, [id = def.id]() { return CompileEffect(id); });
			}
			Register("save", "slot_integrity", SlotIntegrity);
			Register("save", "pool_counts", PoolCounts);
			Register("save", "cosave_roundtrip", CosaveRoundTrip);
			Register("save", "cosave_parser", CosaveParser);
			Register("save", "cosave_file", CosaveFile);
			Register("save", "rebuild_idempotent", RebuildIdempotent);
			Register("save", "revert_rebuild_copy", RevertRebuildCopy);
		}
	}

	void Register(std::string a_suite, std::string a_name, Case a_case)
	{
		std::scoped_lock guard(RegistryLock());
		Registry()[std::move(a_suite)].push_back({ std::move(a_name), std::move(a_case) });
	}

	int Run(std::string_view a_suite)
	{
		static std::once_flag core;
		std::call_once(core, [] {
			if (State::Get().dataReady) {
				RegisterCoreSuites();
			}
		});

		const std::string requested = ToLower(a_suite.empty() ? "all"sv : a_suite);
		std::vector<std::pair<std::string, std::vector<Entry>>> suites;
		{
			std::scoped_lock guard(RegistryLock());
			auto&            registry = Registry();
			if (requested == "all") {
				for (const auto name : kOrder) {
					if (auto it = registry.find(std::string(name)); it != registry.end()) {
						suites.emplace_back(it->first, it->second);
					}
				}
				for (const auto& [name, entries] : registry) {
					if (std::ranges::find(kOrder, std::string_view(name)) == kOrder.end()) {
						suites.emplace_back(name, entries);
					}
				}
			} else if (auto it = registry.find(requested); it != registry.end()) {
				suites.emplace_back(it->first, it->second);
			}
		}

		auto dir = SKSE::log::log_directory();
		std::ofstream log;
		if (dir) {
			log.open(*dir / "LostArt_Tests.log", std::ios::trunc);
		}
		auto write = [&](const std::string& a_line) {
			if (log) {
				log << a_line << '\n';
			}
			logger::info("test: {}", a_line);
		};

		if (suites.empty()) {
			write(fmt::format("[FAIL] {}: expected a registered suite, got none (known: parity, effects, save, compiler, all)", requested));
			return -1;
		}

		int failures = 0;
		for (const auto& [suite, entries] : suites) {
			int passed = 0;
			for (const auto& entry : entries) {
				Result result;
				try {
					result = entry.run ? entry.run() : Fail("expected a test body, got none");
				} catch (const std::exception& e) {
					result = Fail(std::string("expected no exception, got ") + e.what());
				}
				if (result.pass) {
					++passed;
				} else {
					++failures;
				}
				write(fmt::format("[{}] {}.{}: {}", result.pass ? "PASS" : "FAIL", suite, entry.name, result.detail));
			}
			write(fmt::format("[SUMMARY] {}: {}/{}", suite, passed, entries.size()));
		}
		log.flush();
		return failures;
	}
}
