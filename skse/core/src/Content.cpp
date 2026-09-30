#include "lostart/Content.h"

#include "lostart/CostEngine.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace LA
{
	using json = nlohmann::json;

	namespace
	{
		std::optional<json> ReadJson(const std::filesystem::path& a_file, std::vector<std::string>& a_errors)
		{
			std::ifstream file(a_file, std::ios::binary);
			if (!file) {
				a_errors.push_back("cannot read " + a_file.string());
				return std::nullopt;
			}
			std::ostringstream ss;
			ss << file.rdbuf();
			try {
				return json::parse(ss.str());
			} catch (const json::exception& e) {
				a_errors.push_back(a_file.filename().string() + ": " + e.what());
				return std::nullopt;
			}
		}

		template <class T>
		T Get(const json& a_obj, const char* a_key, T a_default)
		{
			if (a_obj.is_object()) {
				if (auto it = a_obj.find(a_key); it != a_obj.end() && !it->is_null()) {
					try {
						return it->get<T>();
					} catch (const json::exception&) {
					}
				}
			}
			return a_default;
		}

		FormRef Ref(const json& a_obj, const char* a_key) { return ParseFormRef(Get<std::string>(a_obj, a_key, "")); }

		const json& List(const json& a_root, const char* a_key)
		{
			static const json empty = json::array();
			if (a_root.is_array()) {
				return a_root;
			}
			if (auto it = a_root.find(a_key); it != a_root.end() && it->is_array()) {
				return *it;
			}
			return empty;
		}

		std::array<float, 3> Vec3(const json& a_obj, const char* a_key)
		{
			std::array<float, 3> out{};
			const auto           values = Get<std::vector<float>>(a_obj, a_key, {});
			for (std::size_t i = 0; i < 3 && i < values.size(); ++i) {
				out[i] = values[i];
			}
			return out;
		}
	}

	bool Content::LoadAll(const std::filesystem::path& a_dataDir, std::vector<std::string>& a_errors)
	{
		bool ok = true;
		const auto content = a_dataDir / "content";
		ok = LoadRiders(content / "riders.json", a_errors) && ok;
		ok = LoadSpellmakers(content / "spellmakers.json", a_errors) && ok;
		ok = LoadAltars(content / "altars.json", a_errors) && ok;
		ok = LoadTomes(content / "tomes.json", a_errors) && ok;
		ok = LoadRanks(content / "ranks.json", a_errors) && ok;
		ok = LoadStandIns(a_dataDir / "standins" / "stand-ins.json", a_errors) && ok;
		return ok;
	}

	bool Content::LoadRiders(const std::filesystem::path& a_file, std::vector<std::string>& a_errors)
	{
		auto root = ReadJson(a_file, a_errors);
		if (!root) {
			return false;
		}
		_riders.clear();
		for (const auto& r : List(*root, "riders")) {
			RiderInfo info;
			info.id = Get<std::string>(r, "id", "");
			info.effect = Ref(r, "effect");
			info.perk = Ref(r, "perk");
			info.elemental = Get<bool>(r, "elemental", false);
			info.magnitudeScale = Get<double>(r, "magnitudeScale", 1.0);
			info.replacesPrimary = Get<bool>(r, "replacesPrimary", false);
			if (auto variants = r.find("variants"); variants != r.end() && variants->is_object()) {
				for (const auto& [key, value] : variants->items()) {
					if (value.is_string()) {
						info.variants[key] = ParseFormRef(value.get<std::string>());
					}
				}
			}
			if (!info.id.empty()) {
				_riders.push_back(std::move(info));
			}
		}
		return true;
	}

	const RiderInfo* Content::FindRider(std::string_view a_id) const
	{
		for (const auto& rider : _riders) {
			if (rider.id == a_id) {
				return &rider;
			}
		}
		return nullptr;
	}

	bool Content::LoadStandIns(const std::filesystem::path& a_file, std::vector<std::string>& a_errors)
	{
		auto root = ReadJson(a_file, a_errors);
		if (!root) {
			return false;
		}
		_standIns.clear();
		for (const auto& s : List(*root, "standins")) {
			StandIn standIn;
			standIn.effect = Get<std::string>(s, "effect", "");
			standIn.creature = Ref(s, "creature");
			standIn.nameKey = Get<std::string>(s, "nameKey", "");
			standIn.levelScale = Get<double>(s, "levelScale", 1.0);
			standIn.credit = Get<std::string>(s, "credit", "");
			_standIns.push_back(std::move(standIn));
		}
		return true;
	}

	bool Content::LoadSpellmakers(const std::filesystem::path& a_file, std::vector<std::string>& a_errors)
	{
		auto root = ReadJson(a_file, a_errors);
		if (!root) {
			return false;
		}
		_spellmakers.clear();
		for (const auto& s : List(*root, "spellmakers")) {
			Spellmaker sm;
			sm.id = Get<std::string>(s, "id", "");
			sm.npc = Ref(s, "npc");
			sm.name = Get<std::string>(s, "name", "");
			sm.location = Get<std::string>(s, "location", "");
			const auto refusal = Get<std::string>(s, "refusal", "");
			sm.rule = Mech::ParseRefusalRule(refusal);
			if (const auto colon = refusal.find(':'); colon != std::string::npos) {
				sm.ruleArg = refusal.substr(colon + 1);
			}
			if (sm.rule == Mech::RefusalRule::kQuest) {
				// "quest:Dragonborn.esm|0x0179D1:200"
				const auto lastColon = sm.ruleArg.rfind(':');
				sm.quest = ParseFormRef(sm.ruleArg.substr(0, lastColon));
				if (lastColon != std::string::npos) {
					try {
						sm.questStage = std::stoi(sm.ruleArg.substr(lastColon + 1));
					} catch (...) {
					}
				}
			}
			sm.crimeFaction = Ref(s, "crimeFaction");
			sm.merchantChest = Ref(s, "merchantChest");
			sm.priceMult = Get<double>(s, "priceMult", 1.0);
			sm.specialties = Get<std::vector<std::string>>(s, "specialties", {});
			_spellmakers.push_back(std::move(sm));
		}
		return true;
	}

	bool Content::LoadAltars(const std::filesystem::path& a_file, std::vector<std::string>& a_errors)
	{
		auto root = ReadJson(a_file, a_errors);
		if (!root) {
			return false;
		}
		_altars.clear();
		for (const auto& a : List(*root, "altars")) {
			Altar altar;
			altar.id = Get<std::string>(a, "id", "");
			altar.furniture = Get<std::string>(a, "furniture", "");
			altar.cell = Ref(a, "cell");
			altar.position = Vec3(a, "position");
			altar.rotation = Vec3(a, "rotation");
			if (auto alt = a.find("alternate"); alt != a.end() && alt->is_object()) {
				altar.alternate = std::make_pair(Vec3(*alt, "position"), Vec3(*alt, "rotation"));
			}
			altar.access = Get<std::string>(a, "access", "anyone");
			altar.owner = Ref(a, "owner");
			altar.freeFor = Get<std::string>(a, "freeFor", "");
			_altars.push_back(std::move(altar));
		}
		return true;
	}

	bool Content::LoadTomes(const std::filesystem::path& a_file, std::vector<std::string>& a_errors)
	{
		auto root = ReadJson(a_file, a_errors);
		if (!root) {
			return false;
		}
		_tomes.clear();
		for (const auto& t : List(*root, "tomes")) {
			Tome tome;
			tome.id = Get<std::string>(t, "id", "");
			tome.titleKey = Get<std::string>(t, "title", "");
			tome.spellNameKey = Get<std::string>(t, "spellName", "");
			tome.school = Get<std::string>(t, "school", "");
			tome.value = Get<int>(t, "value", Get<int>(t, "price", 0));
			tome.vendors = Get<std::vector<std::string>>(t, "vendors", {});
			tome.loot = Get<bool>(t, "loot", true);
			if (auto effects = t.find("effects"); effects != t.end() && effects->is_array()) {
				for (const auto& e : *effects) {
					SpellEffect effect;
					effect.effectId = Get<std::string>(e, "id", "");
					effect.sub = static_cast<std::int16_t>(Get<int>(e, "sub", kNoSub));
					effect.range = RangeFromString(Get<std::string>(e, "range", "self")).value_or(Range::kSelf);
					effect.minMag = static_cast<std::uint16_t>(Get<int>(e, "min", 1));
					effect.maxMag = static_cast<std::uint16_t>(Get<int>(e, "max", effect.minMag));
					effect.duration = static_cast<std::uint16_t>(Get<int>(e, "duration", 1));
					effect.area = static_cast<std::uint16_t>(Get<int>(e, "area", 0));
					tome.effects.push_back(std::move(effect));
				}
			}
			_tomes.push_back(std::move(tome));
		}
		return true;
	}

	bool Content::LoadRanks(const std::filesystem::path& a_file, std::vector<std::string>& a_errors)
	{
		auto root = ReadJson(a_file, a_errors);
		if (!root) {
			return false;
		}
		const auto thresholds = Get<std::vector<double>>(*root, "costThresholds", {});
		for (std::size_t i = 0; i < 4 && i < thresholds.size(); ++i) {
			_ranks.costThresholds[i] = thresholds[i];
			Cost::kRankCostThresholds[i] = thresholds[i];
		}
		if (auto perks = root->find("perks"); perks != root->end() && perks->is_object()) {
			for (const auto& [school, ranks] : perks->items()) {
				if (!ranks.is_object()) {
					continue;
				}
				for (const auto& [rank, ref] : ranks.items()) {
					if (ref.is_string()) {
						_ranks.perks[school][rank] = ParseFormRef(ref.get<std::string>());
					}
				}
			}
		}
		return true;
	}
}
