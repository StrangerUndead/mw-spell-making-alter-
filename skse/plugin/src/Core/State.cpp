#include "Core/State.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <thread>

namespace LA
{
	namespace
	{
		std::thread::id g_mainThread{};

		// ESL-flagged plugins address 0x000-0xFFF; full plugins 0x000000-0xFFFFFF.
		RE::TESForm* LookupLocal(std::string_view a_plugin, std::uint32_t a_localId)
		{
			auto* handler = RE::TESDataHandler::GetSingleton();
			if (!handler || a_plugin.empty()) {
				return nullptr;
			}
			const auto* file = handler->LookupModByName(a_plugin);
			if (!file || file->compileIndex == 0xFF) {
				return nullptr;  // not in the load order
			}
			const std::uint32_t mask = file->IsLight() ? 0xFFFu : 0xFFFFFFu;
			return handler->LookupForm(a_localId & mask, a_plugin);
		}
	}

	// --- FormMap ------------------------------------------------------------------------------

	bool FormMap::Load(const std::filesystem::path& a_file, std::vector<std::string>& a_errors)
	{
		_ids.clear();
		_forms.clear();
		_reverse.clear();

		std::ifstream in(a_file, std::ios::binary);
		if (!in) {
			a_errors.push_back("cannot read " + a_file.string());
			return false;
		}
		std::ostringstream ss;
		ss << in.rdbuf();

		nlohmann::json root;
		try {
			root = nlohmann::json::parse(ss.str());
		} catch (const nlohmann::json::exception& e) {
			a_errors.push_back(a_file.filename().string() + ": " + e.what());
			return false;
		}
		if (!root.is_object()) {
			a_errors.push_back(a_file.filename().string() + ": expected an object of plugins");
			return false;
		}

		std::size_t unresolved = 0;
		for (const auto& [plugin, entries] : root.items()) {
			if (!entries.is_object()) {
				continue;
			}
			for (const auto& [editorId, value] : entries.items()) {
				if (!value.is_string()) {
					a_errors.push_back(fmt::format("formmap: {} has a non-string id", editorId));
					continue;
				}
				std::uint32_t localId = 0;
				try {
					localId = static_cast<std::uint32_t>(std::stoul(value.get<std::string>(), nullptr, 16));
				} catch (const std::exception&) {
					a_errors.push_back(fmt::format("formmap: {} has a bad id {}", editorId, value.get<std::string>()));
					continue;
				}
				_ids[editorId] = Entry{ plugin, localId };
				if (auto* form = LookupLocal(plugin, localId)) {
					_forms[editorId] = form;
					_reverse[form->GetFormID()] = editorId;
				} else {
					++unresolved;
				}
			}
		}
		// The reverse map is complete once Load returns.
		std::call_once(_reverseBuilt, [] {});
		if (unresolved > 0) {
			a_errors.push_back(fmt::format("formmap: {} of {} records are not in the load order (is LostArt.esp / LostArt_Slots.esp enabled?)",
				unresolved, _ids.size()));
		}
		return true;
	}

	RE::TESForm* FormMap::Get(std::string_view a_editorId) const
	{
		if (const auto it = _forms.find(std::string(a_editorId)); it != _forms.end()) {
			return it->second;
		}
		return nullptr;
	}

	RE::TESForm* FormMap::Resolve(const FormRef& a_ref)
	{
		if (!a_ref.Valid()) {
			return nullptr;
		}
		return LookupLocal(a_ref.plugin, a_ref.localId);
	}

	std::string_view FormMap::EditorIdOf(RE::FormID a_formId) const
	{
		if (const auto it = _reverse.find(a_formId); it != _reverse.end()) {
			return it->second;
		}
		return {};
	}

	// --- State --------------------------------------------------------------------------------

	State& State::Get()
	{
		static State instance;
		return instance;
	}

	Cost::BaseCostOverride State::BaseCostFn() const
	{
		return [this](const EffectDef& a_def) -> std::optional<double> {
			if (const auto it = liveBaseCost.find(a_def.id); it != liveBaseCost.end()) {
				return it->second;
			}
			return std::nullopt;
		};
	}

	const CustomSpell* State::FindBySpell(const RE::SpellItem* a_spell) const
	{
		if (!a_spell || !slotIndex.contains(a_spell)) {
			return nullptr;
		}
		std::shared_lock guard(lock);
		return FindBySpellLocked(a_spell);
	}

	const CustomSpell* State::FindBySpellLocked(const RE::SpellItem* a_spell) const
	{
		if (!a_spell) {
			return nullptr;
		}
		const auto it = slotIndex.find(a_spell);
		if (it == slotIndex.end()) {
			return nullptr;
		}
		std::uint16_t primary = it->second;
		if ((primary & kSubFlag) != 0) {
			const std::uint16_t sub = primary & static_cast<std::uint16_t>(~kSubFlag);
			if (sub >= subOwner.size() || subOwner[sub] == kNoSlot) {
				return nullptr;
			}
			primary = subOwner[sub];
		}
		const auto spell = spells.find(primary);
		return spell != spells.end() ? &spell->second : nullptr;
	}

	std::optional<std::uint16_t> State::SlotOf(const RE::SpellItem* a_spell) const
	{
		if (const auto* custom = FindBySpell(a_spell)) {
			return custom->def.slot;
		}
		return std::nullopt;
	}

	bool State::EffectUsable(std::string_view a_effectId) const
	{
		if (hiddenEffects.contains(std::string(a_effectId))) {
			return false;
		}
		const auto* def = catalog.Find(a_effectId);
		if (!def) {
			return false;
		}
		if (def->set == EffectSet::kPassThrough) {
			return def->passThroughForm != 0 && RE::TESForm::LookupByID<RE::EffectSetting>(def->passThroughForm) != nullptr;
		}
		return true;
	}

	RE::BGSPerk* State::CastingPerk(School a_school, Rank a_rank) const
	{
		const auto school = static_cast<std::size_t>(a_school);
		const auto rank = static_cast<std::size_t>(a_rank);
		if (school >= castingPerks.size() || rank >= castingPerks[school].size()) {
			return nullptr;
		}
		return castingPerks[school][rank];
	}

	void State::AddPassThrough(EffectDef a_def)
	{
		if (a_def.id.empty()) {
			return;
		}
		std::unique_lock guard(lock);
		if (catalog.Find(a_def.id)) {
			return;  // appended once per id, never replaced (Catalog::Add would overwrite in place)
		}
		logger::info("pass-through effect registered: {} ({})", a_def.id, a_def.nameKey);
		catalog.Add(std::move(a_def));
	}

	void State::ApplySettings()
	{
		// iLogLevel: 0 Off (warnings and errors only), 1 Info, 2 Verbose.
		if (auto log = spdlog::default_logger()) {
			const auto level = settings.logLevel >= 2 ? spdlog::level::debug :
			                   settings.logLevel == 1 ? spdlog::level::info :
			                                            spdlog::level::warn;
			log->set_level(level);
			log->flush_on(level);
		}
		if (altarsEnabled) {
			altarsEnabled->value = settings.altars ? 1.0f : 0.0f;
		}
		if (spellmakersEnabled) {
			spellmakersEnabled->value = settings.spellmakers ? 1.0f : 0.0f;
		}
	}

	void State::ReloadSettings()
	{
		// Cheap and idempotent: the MCM script calls ReloadSettings and also sends
		// LostArt_SettingsChanged, so this usually runs twice per change.
		settings = LoadSettings(SettingsDefaultsPath(), SettingsUserPath());
		ApplySettings();
		logger::info("settings reloaded (cost model {}, availability {}, log level {})",
			static_cast<int>(settings.costModel), settings.availability, settings.logLevel);
	}

	bool State::IsMainThread()
	{
		return std::this_thread::get_id() == g_mainThread;
	}

	void State::MarkMainThread()
	{
		g_mainThread = std::this_thread::get_id();
	}

	void RunOnMainThread(std::function<void()> a_task)
	{
		if (!a_task) {
			return;
		}
		if (State::IsMainThread()) {
			a_task();
			return;
		}
		if (const auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask(std::move(a_task));
		} else {
			logger::error("RunOnMainThread: no SKSE task interface; task dropped");
		}
	}

	std::filesystem::path SettingsDefaultsPath()
	{
		return std::filesystem::path("Data") / "MCM" / "Config" / "LostArt" / "settings.ini";
	}

	std::filesystem::path SettingsUserPath()
	{
		return std::filesystem::path("Data") / "MCM" / "Settings" / "LostArt.ini";
	}
}
