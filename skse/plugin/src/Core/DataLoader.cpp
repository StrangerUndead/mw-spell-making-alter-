#include "Core/State.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

// kDataLoaded pass (OUTLINE "Load and save sequence" 1 and "Components" 1): reads every JSON pack,
// the translation file and the settings, resolves the generated records through formmap.json,
// verifies that every variant the compiler can emit exists, and refits Skyrim-balanced base costs
// against the live load order.
namespace LA
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		double Ms(Clock::time_point a_since)
		{
			return std::chrono::duration<double, std::milli>(Clock::now() - a_since).count();
		}

		std::string Language()
		{
			// Skyrim's own language setting (Skyrim.ini [General] sLanguage), as the engine uses to
			// pick Interface/Translations/<mod>_<LANGUAGE>.txt.
			if (auto* setting = RE::GetINISetting("sLanguage:General")) {
				if (const char* value = setting->GetString(); value && *value) {
					std::string language(value);
					std::ranges::transform(language, language.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
					return language;
				}
			}
			return "ENGLISH";
		}

		void LoadStrings(State& a_state, std::vector<std::string>& a_errors)
		{
			const auto dir = std::filesystem::path("Data") / "Interface" / "Translations";
			const auto language = Language();
			auto       file = dir / ("LostArt_" + language + ".txt");
			std::error_code ec;
			if (!std::filesystem::exists(file, ec)) {
				if (language != "ENGLISH") {
					a_errors.push_back(fmt::format("no LostArt_{}.txt; using ENGLISH", language));
				}
				file = dir / "LostArt_ENGLISH.txt";
			}
			if (!a_state.strings.LoadFile(file)) {
				a_errors.push_back("cannot read " + file.string() + " (menu text will show $LA_ keys)");
			}
		}

		void ResolveSlots(State& a_state, std::vector<std::string>& a_errors)
		{
			auto resolve = [&](const char* a_prefix, std::vector<RE::SpellItem*>& a_out, std::uint16_t a_count) {
				a_out.assign(a_count, nullptr);
				std::uint16_t usable = 0;
				bool          gap = false;
				for (std::uint16_t i = 0; i < a_count; ++i) {
					a_out[i] = a_state.forms.Get<RE::SpellItem>(fmt::format("{}{:03}", a_prefix, i));
					if (a_out[i] && !gap) {
						usable = static_cast<std::uint16_t>(i + 1);
					} else if (!a_out[i]) {
						gap = true;
					}
				}
				if (usable < a_count) {
					a_errors.push_back(fmt::format("only {} of {} {}* slot records resolved; the pool is limited to them", usable,
						a_count, a_prefix));
				}
				a_out.resize(usable);
				return usable;
			};
			const auto primary = resolve("LA_Slot_", a_state.primarySlots, kPrimarySlots);
			const auto sub = resolve("LA_Sub_", a_state.subSlots, kSubSlots);

			a_state.slots = SlotPool(primary, sub);
			a_state.subOwner.assign(sub, kNoSlot);
			a_state.slotIndex.clear();
			for (std::uint16_t i = 0; i < primary; ++i) {
				a_state.slotIndex[a_state.primarySlots[i]] = i;
			}
			for (std::uint16_t i = 0; i < sub; ++i) {
				a_state.slotIndex[a_state.subSlots[i]] = static_cast<std::uint16_t>(i | State::kSubFlag);
			}

			a_state.blankEffect = a_state.forms.Get<RE::EffectSetting>("LA_BlankEffect");
			if (!a_state.blankEffect) {
				a_errors.push_back("LA_BlankEffect not found: LostArt_Slots.esp is missing or disabled");
			}
			a_state.altarsEnabled = a_state.forms.Get<RE::TESGlobal>("LA_AltarsEnabled");
			a_state.spellmakersEnabled = a_state.forms.Get<RE::TESGlobal>("LA_SpellmakersEnabled");
		}

		void ResolvePerks(State& a_state, std::vector<std::string>& a_errors)
		{
			const auto& perks = a_state.content.Ranks().perks;
			int         missing = 0;
			for (std::size_t s = 0; s < kSchoolCount; ++s) {
				for (std::size_t r = 0; r < 5; ++r) {
					auto& slot = a_state.castingPerks[s][r];
					slot = nullptr;
					const auto school = perks.find(std::string(ToString(static_cast<School>(s))));
					if (school == perks.end()) {
						++missing;
						continue;
					}
					const auto rank = school->second.find(std::string(ToString(static_cast<Rank>(r))));
					if (rank == school->second.end() || !(slot = FormMap::Resolve<RE::BGSPerk>(rank->second))) {
						++missing;
					}
				}
			}
			if (missing > 0) {
				a_errors.push_back(fmt::format("{} of 25 half-cost casting perks are unresolved (ranks.json)", missing));
			}
		}

		void LoadProtectedSpells(State& a_state, std::vector<std::string>& a_errors)
		{
			a_state.protectedSpells.clear();
			// Quest-given Spell-type records whose loss cannot be undone. Most quest rewards in
			// Skyrim are powers, shouts or abilities, which the spell-type check already protects.
			std::vector<std::string> refs{
				"Skyrim.esm|0x0618C6",  // DBSummonAssassin "Summon Spectral Assassin" (Dark Brotherhood)
			};
			const auto file = a_state.dataDir / "content" / "protected-spells.json";
			std::error_code ec;
			if (std::filesystem::exists(file, ec)) {
				// { "spells": ["Plugin.esp|0x000123", ...] } or a bare array.
				std::ifstream     in(file, std::ios::binary);
				std::stringstream ss;
				ss << in.rdbuf();
				try {
					const auto root = nlohmann::json::parse(ss.str());
					const auto& list = root.is_array() ? root : root.value("spells", nlohmann::json::array());
					for (const auto& ref : list) {
						if (ref.is_string()) {
							refs.push_back(ref.get<std::string>());
						}
					}
				} catch (const nlohmann::json::exception& e) {
					a_errors.push_back("protected-spells.json: " + std::string(e.what()));
				}
			}
			for (const auto& text : refs) {
				if (auto* spell = FormMap::Resolve<RE::SpellItem>(ParseFormRef(text))) {
					a_state.protectedSpells.insert(spell->GetFormID());
				}
			}
		}

		// Every variant EditorID the compiler can emit, per effect x allowed range x target.
		void VerifyVariants(State& a_state, std::vector<std::string>& a_errors, std::size_t& a_variants, std::size_t& a_missing)
		{
			a_state.hiddenEffects.clear();
			Settings probeSettings = a_state.settings;
			probeSettings.elementalRiders = true;  // verify every rider, whatever the setting
			const auto riders = a_state.content.Riders();

			std::unordered_set<std::string> checked;
			std::unordered_set<std::string> missingRiders;
			for (const auto& def : a_state.catalog.All()) {
				if (def.set == EffectSet::kPassThrough) {
					continue;
				}
				std::vector<std::int16_t> subs;
				switch (def.target) {
				case TargetKind::kAttribute:
					for (std::int16_t i = 0; i < static_cast<std::int16_t>(kAttributeCount); ++i) {
						subs.push_back(i);
					}
					break;
				case TargetKind::kSkill:
					for (std::int16_t i = 0; i < static_cast<std::int16_t>(kSkyrimSkillCount); ++i) {
						subs.push_back(i);
					}
					for (const auto& skill : a_state.catalog.MwSkills()) {
						subs.push_back(static_cast<std::int16_t>(kMwSkillBase + skill.index));
					}
					break;
				default:
					subs.push_back(kNoSub);
					break;
				}

				std::vector<std::string> missing;
				for (const auto range : def.AllowedRanges()) {
					const auto expected = range == Range::kSelf ? RE::MagicSystem::Delivery::kSelf : RE::MagicSystem::Delivery::kAimed;
					for (const auto sub : subs) {
						SpellEffect probe;
						probe.effectId = def.id;
						probe.sub = sub;
						probe.range = range;
						const auto plan = Compiler::Plan(a_state.catalog, { probe }, probeSettings, riders);
						if (plan.tooComplex) {
							a_errors.push_back(fmt::format("{} on {} (target {}) exceeds {} effects with its riders", def.id, ToString(range), sub,
								kEngineEffectCeiling));
						}
						for (const auto& spell : plan.spells) {
							for (const auto& entry : spell.entries) {
								if (!entry.riderId.empty()) {
									if (!FormMap::Resolve<RE::EffectSetting>(entry.vanillaEffect) && missingRiders.insert(entry.riderId).second) {
										a_errors.push_back(fmt::format("rider {} ({}) is not loaded; it will be skipped", entry.riderId,
											entry.vanillaEffect.ToString()));
									}
									continue;
								}
								if (entry.variantEditorId.empty() || !checked.insert(entry.variantEditorId).second) {
									continue;
								}
								++a_variants;
								const auto* mgef = a_state.forms.Get<RE::EffectSetting>(entry.variantEditorId);
								if (!mgef) {
									missing.push_back(entry.variantEditorId);
									continue;
								}
								if (mgef->data.castingType != RE::MagicSystem::CastingType::kFireAndForget || mgef->data.delivery != expected) {
									a_errors.push_back(fmt::format("{} has casting type {} / delivery {}; expected FireAndForget / {}",
										entry.variantEditorId, static_cast<int>(mgef->data.castingType), static_cast<int>(mgef->data.delivery),
										static_cast<int>(expected)));
								}
							}
						}
					}
				}
				if (!missing.empty()) {
					a_missing += missing.size();
					a_state.hiddenEffects.insert(def.id);
					a_errors.push_back(fmt::format("{} hidden: {} variant(s) missing (first: {})", def.id, missing.size(), missing.front()));
				}
			}
		}

		// Live refit of Skyrim-balanced base costs against the vanilla spell each effect was fitted
		// to, so a load order that rebalances vanilla spells moves our prices with it.
		std::size_t Refit(State& a_state)
		{
			a_state.liveBaseCost.clear();
			std::size_t refitted = 0;
			for (const auto& def : a_state.catalog.All()) {
				if (!def.fitFrom || !def.fitFrom->spell.Valid()) {
					continue;
				}
				auto* spell = FormMap::Resolve<RE::SpellItem>(def.fitFrom->spell);
				if (!spell) {
					logger::debug("refit {}: {} not loaded; keeping the file's base cost", def.id, def.fitFrom->spell.ToString());
					continue;
				}
				// A manual cost is the cost; otherwise ask the engine's autocalc with no caster (no
				// skill or perk discounts).
				// VERIFY(in-game): CalculateMagickaCost(nullptr) returns the unmodified autocalc cost
				// (Firebolt 41 in an unmodded game).
				const float cost = spell->data.flags.any(RE::SpellItem::SpellFlag::kCostOverride) ?
				                       static_cast<float>(spell->data.costOverride) :
				                       spell->CalculateMagickaCost(nullptr);
				if (!(cost > 0.0f) || !std::isfinite(cost)) {
					continue;
				}
				FitFrom fit = *def.fitFrom;
				fit.cost = cost;
				const double base = Cost::FitBaseCost(def, fit);
				if (!(base > 0.0) || !std::isfinite(base)) {
					continue;
				}
				a_state.liveBaseCost[def.id] = base;
				++refitted;
				if (std::abs(cost - def.fitFrom->cost) >= 1.0) {
					logger::info("refit {}: {} costs {:.0f} in this load order (file {:.0f}); base cost {:.4f} -> {:.4f}", def.id,
						def.fitFrom->label, cost, def.fitFrom->cost, def.skBaseCost, base);
				}
			}
			return refitted;
		}
	}

	bool LoadData()
	{
		const auto start = Clock::now();
		auto&      state = State::Get();
		std::vector<std::string> errors;

		state.dataReady = false;
		state.dataDir = std::filesystem::path("Data") / "SKSE" / "Plugins" / "LostArt";
		state.settings = LoadSettings(SettingsDefaultsPath(), SettingsUserPath());

		const auto& dir = state.dataDir;
		state.catalog.LoadEffects(dir / "effects", errors);
		state.catalog.LoadAttributes(dir / "content" / "attributes.json", errors);
		state.catalog.LoadSkills(dir / "content" / "skills.json", errors);
		state.content.LoadAll(dir, errors);
		state.discovery.LoadVanilla(dir / "discovery" / "vanilla.json", errors);
		state.discovery.LoadRules(dir / "discovery" / "rules.json", errors);
		LoadStrings(state, errors);
		const auto jsonMs = Ms(start);

		const auto formsStart = Clock::now();
		state.forms.Load(dir / "formmap.json", errors);
		ResolveSlots(state, errors);
		ResolvePerks(state, errors);
		LoadProtectedSpells(state, errors);

		std::size_t variants = 0;
		std::size_t missing = 0;
		VerifyVariants(state, errors, variants, missing);
		const auto refitted = Refit(state);
		const auto formsMs = Ms(formsStart);

		state.dataReady = !state.primarySlots.empty() && state.blankEffect != nullptr;
		for (const auto& error : errors) {
			logger::warn("data: {}", error);
		}
		state.loadWarnings = std::move(errors);

		logger::info(
			"data loaded in {:.1f} ms (json {:.1f} ms, records {:.1f} ms): {} effects ({} hidden), {} attributes, {} Morrowind skills, "
			"{} riders, {} stand-ins, {} tomes, {} spellmakers, {} altars; discovery {} lookups + {} rules; {} strings; formmap {} "
			"records; slots {}+{}; variants {} checked, {} missing; {} base costs refitted; {} warnings",
			Ms(start), jsonMs, formsMs, state.catalog.Size(), state.hiddenEffects.size(), state.catalog.Attributes().size(),
			state.catalog.MwSkills().size(), state.content.AllRiders().size(), state.content.StandIns().size(), state.content.Tomes().size(),
			state.content.Spellmakers().size(), state.content.Altars().size(), state.discovery.LookupSize(), state.discovery.RuleCount(),
			state.strings.Size(), state.forms.Size(), state.primarySlots.size(), state.subSlots.size(), variants, missing, refitted,
			state.loadWarnings.size());
		if (!state.dataReady) {
			logger::critical("LostArt cannot run: the slot records are missing (enable LostArt.esp and LostArt_Slots.esp)");
		}

		state.ApplySettings();  // log level from iLogLevel, service globals
		return state.dataReady;
	}
}
