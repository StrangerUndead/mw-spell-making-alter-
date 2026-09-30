#include "lostart/Catalog.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace LA
{
	using json = nlohmann::json;

	namespace
	{
		constexpr std::array<std::string_view, kSkyrimSkillCount> kSkyrimSkills{
			"OneHanded", "TwoHanded", "Archery", "Block", "Smithing", "HeavyArmor",
			"LightArmor", "Pickpocket", "Lockpicking", "Sneak", "Alchemy", "Speech",
			"Alteration", "Conjuration", "Destruction", "Illusion", "Restoration", "Enchanting"
		};

		constexpr std::array<std::string_view, kAttributeCount> kAttributes{
			"Strength", "Intelligence", "Willpower", "Agility", "Speed", "Endurance", "Personality", "Luck"
		};

		std::optional<std::string> ReadFile(const std::filesystem::path& a_path)
		{
			std::ifstream file(a_path, std::ios::binary);
			if (!file) {
				return std::nullopt;
			}
			std::ostringstream ss;
			ss << file.rdbuf();
			return ss.str();
		}

		Unit ParseUnit(std::string_view a_text)
		{
			if (IEquals(a_text, "pts") || IEquals(a_text, "pt") || IEquals(a_text, "points")) {
				return Unit::kPoints;
			}
			if (IEquals(a_text, "percent") || a_text == "%") {
				return Unit::kPercent;
			}
			if (IEquals(a_text, "ft") || IEquals(a_text, "feet")) {
				return Unit::kFeet;
			}
			if (IEquals(a_text, "level") || IEquals(a_text, "levels")) {
				return Unit::kLevel;
			}
			return Unit::kNone;
		}

		EffectSet ParseSet(std::string_view a_text, std::string_view a_id)
		{
			std::string_view set = a_text;
			if (set.empty()) {
				set = a_id.substr(0, a_id.find('.'));
			}
			if (set == "mwx") {
				return EffectSet::kExtended;
			}
			if (set == "sk") {
				return EffectSet::kSkyrim;
			}
			if (set == "pt") {
				return EffectSet::kPassThrough;
			}
			return EffectSet::kMorrowind;
		}

		Tier ParseTier(std::string_view a_text)
		{
			if (IEquals(a_text, "custom")) {
				return Tier::kCustom;
			}
			if (IEquals(a_text, "standin") || IEquals(a_text, "stand-in")) {
				return Tier::kStandIn;
			}
			return Tier::kNative;
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

		bool ParseEffect(const json& a_obj, EffectDef& a_def, std::string& a_error)
		{
			if (!a_obj.is_object()) {
				a_error = "entry is not an object";
				return false;
			}
			a_def.id = Get<std::string>(a_obj, "id", "");
			if (a_def.id.empty()) {
				a_error = "missing id";
				return false;
			}
			a_def.nameKey = Get<std::string>(a_obj, "name", "");
			a_def.pascal = Get<std::string>(a_obj, "pascal", Catalog::PascalFromId(a_def.id));
			a_def.set = ParseSet(Get<std::string>(a_obj, "set", ""), a_def.id);
			a_def.tier = ParseTier(Get<std::string>(a_obj, "tier", "native"));

			if (auto mw = a_obj.find("morrowind"); mw != a_obj.end() && mw->is_object()) {
				a_def.mwIndex = Get<int>(*mw, "index", -1);
				a_def.mwBaseCost = Get<double>(*mw, "baseCost", 1.0);
				if (auto school = MwSchoolFromString(Get<std::string>(*mw, "school", ""))) {
					a_def.mwSchool = *school;
				}
			}

			if (auto sk = a_obj.find("skyrim"); sk != a_obj.end() && sk->is_object()) {
				if (auto school = SchoolFromString(Get<std::string>(*sk, "school", ""))) {
					a_def.school = *school;
				} else {
					a_error = "missing or invalid skyrim.school";
					return false;
				}
				if (auto cost = sk->find("baseCost"); cost != sk->end()) {
					if (cost->is_number()) {
						a_def.skBaseCost = cost->get<double>();
					} else if (cost->is_object()) {
						a_def.skBaseCost = Get<double>(*cost, "value", 1.0);
					}
				}
				if (auto fit = sk->find("fitFrom"); fit != sk->end() && fit->is_object()) {
					FitFrom from;
					from.spell = ParseFormRef(Get<std::string>(*fit, "spell", ""));
					from.label = Get<std::string>(*fit, "label", "");
					from.magnitude = Get<double>(*fit, "magnitude", 0);
					from.duration = Get<double>(*fit, "duration", 0);
					from.area = Get<double>(*fit, "area", 0);
					from.cost = Get<double>(*fit, "cost", 0);
					a_def.fitFrom = from;
				}
				a_def.vanillaTargetPriced = Get<bool>(*sk, "vanillaTargetPriced", false);
				a_def.archetype = Get<std::string>(*sk, "archetype", "");
				a_def.vanillaEffect = ParseFormRef(Get<std::string>(*sk, "vanillaEffect", ""));
			} else {
				a_error = "missing skyrim block";
				return false;
			}

			a_def.ranges = 0;
			if (auto ranges = a_obj.find("ranges"); ranges != a_obj.end() && ranges->is_array()) {
				for (const auto& r : *ranges) {
					if (r.is_string()) {
						if (auto range = RangeFromString(r.get<std::string>())) {
							a_def.ranges |= Bit(*range);
						}
					}
				}
			}
			if (a_def.ranges == 0) {
				a_error = "no valid ranges";
				return false;
			}

			if (auto mag = a_obj.find("magnitude"); mag != a_obj.end()) {
				if (mag->is_object()) {
					a_def.hasMagnitude = Get<bool>(*mag, "has", true);
					a_def.unit = a_def.hasMagnitude ? ParseUnit(Get<std::string>(*mag, "unit", "pts")) : Unit::kNone;
					a_def.ticking = Get<bool>(*mag, "ticking", false);
					if (a_def.unit == Unit::kNone) {
						a_def.hasMagnitude = false;
					}
				} else if (mag->is_boolean()) {
					a_def.hasMagnitude = mag->get<bool>();
					a_def.unit = a_def.hasMagnitude ? Unit::kPoints : Unit::kNone;
				}
			}
			a_def.hasDuration = Get<bool>(a_obj, "duration", true);
			a_def.hasArea = Get<bool>(a_obj, "area", true);

			const auto target = Get<std::string>(a_obj, "target", "none");
			a_def.target = IEquals(target, "attribute") ? TargetKind::kAttribute :
			               IEquals(target, "skill")     ? TargetKind::kSkill :
			                                              TargetKind::kNone;

			a_def.keywords = Get<std::vector<std::string>>(a_obj, "keywords", {});
			a_def.riders = Get<std::vector<std::string>>(a_obj, "riders", {});
			if (auto ladder = a_obj.find("rankLadder"); ladder != a_obj.end() && ladder->is_object()) {
				for (const auto& [key, value] : ladder->items()) {
					if (auto rank = RankFromString(key); rank && value.is_number()) {
						a_def.rankLadder[*rank] = value.get<double>();
					}
				}
			}
			a_def.reflectable = Get<bool>(a_obj, "reflectable", true);
			a_def.hostile = Get<bool>(a_obj, "hostile", false);
			a_def.itemCardKey = Get<std::string>(a_obj, "itemCard", "");
			for (const auto& src : Get<std::vector<std::string>>(a_obj, "sources", {})) {
				if (auto ref = ParseFormRef(src); ref.Valid()) {
					a_def.sources.push_back(ref);
				}
			}
			return true;
		}
	}

	std::vector<Range> EffectDef::AllowedRanges() const
	{
		std::vector<Range> out;
		for (auto range : kAllRanges) {
			if (Allows(range)) {
				out.push_back(range);
			}
		}
		return out;
	}

	std::string Catalog::PascalFromId(std::string_view a_id)
	{
		const auto dot = a_id.find('.');
		const auto body = dot == std::string_view::npos ? a_id : a_id.substr(dot + 1);
		std::string out;
		bool        upper = true;
		for (char c : body) {
			if (c == '_' || c == '-' || c == ' ') {
				upper = true;
				continue;
			}
			out.push_back(upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c);
			upper = false;
		}
		return out;
	}

	std::string_view Catalog::SkyrimSkillName(int a_index)
	{
		return a_index >= 0 && a_index < static_cast<int>(kSkyrimSkills.size()) ? kSkyrimSkills[a_index] : std::string_view{};
	}

	std::string_view Catalog::AttributeName(int a_index)
	{
		return a_index >= 0 && a_index < static_cast<int>(kAttributes.size()) ? kAttributes[a_index] : std::string_view{};
	}

	void Catalog::Add(EffectDef a_def)
	{
		if (auto it = _index.find(a_def.id); it != _index.end()) {
			_effects[it->second] = std::move(a_def);
			return;
		}
		_index.emplace(a_def.id, _effects.size());
		_effects.push_back(std::move(a_def));
	}

	const EffectDef* Catalog::Find(std::string_view a_id) const
	{
		const auto it = _index.find(std::string(a_id));
		return it != _index.end() ? &_effects[it->second] : nullptr;
	}

	bool Catalog::LoadEffectsFromString(std::string_view a_json, std::vector<std::string>& a_errors, std::string_view a_source)
	{
		json root;
		try {
			root = json::parse(a_json);
		} catch (const json::exception& e) {
			a_errors.push_back(std::string(a_source) + ": " + e.what());
			return false;
		}
		const json* entries = &root;
		if (root.is_object() && root.contains("effects")) {
			entries = &root["effects"];
		}
		if (!entries->is_array()) {
			a_errors.push_back(std::string(a_source) + ": expected an array of effects");
			return false;
		}
		bool ok = true;
		for (std::size_t i = 0; i < entries->size(); ++i) {
			EffectDef   def;
			std::string error;
			if (!ParseEffect((*entries)[i], def, error)) {
				a_errors.push_back(std::string(a_source) + "[" + std::to_string(i) + "] " + def.id + ": " + error);
				ok = false;
				continue;
			}
			if (Find(def.id)) {
				a_errors.push_back(std::string(a_source) + ": duplicate effect id " + def.id);
				ok = false;
				continue;
			}
			Add(std::move(def));
		}
		return ok;
	}

	bool Catalog::LoadEffects(const std::filesystem::path& a_effectsDir, std::vector<std::string>& a_errors)
	{
		std::error_code ec;
		if (!std::filesystem::is_directory(a_effectsDir, ec)) {
			a_errors.push_back("effects directory not found: " + a_effectsDir.string());
			return false;
		}
		std::vector<std::filesystem::path> files;
		for (const auto& entry : std::filesystem::directory_iterator(a_effectsDir, ec)) {
			if (entry.is_regular_file() && entry.path().extension() == ".json") {
				files.push_back(entry.path());
			}
		}
		std::sort(files.begin(), files.end());
		bool ok = true;
		for (const auto& file : files) {
			auto text = ReadFile(file);
			if (!text) {
				a_errors.push_back("cannot read " + file.string());
				ok = false;
				continue;
			}
			ok = LoadEffectsFromString(*text, a_errors, file.filename().string()) && ok;
		}
		return ok;
	}

	bool Catalog::LoadAttributes(const std::filesystem::path& a_file, std::vector<std::string>& a_errors)
	{
		auto text = ReadFile(a_file);
		if (!text) {
			a_errors.push_back("cannot read " + a_file.string());
			return false;
		}
		json root;
		try {
			root = json::parse(*text);
		} catch (const json::exception& e) {
			a_errors.push_back(a_file.filename().string() + ": " + e.what());
			return false;
		}
		const json* list = &root;
		if (root.is_object() && root.contains("attributes")) {
			list = &root["attributes"];
		}
		if (!list->is_array()) {
			a_errors.push_back(a_file.filename().string() + ": expected attributes array");
			return false;
		}
		_attributes.clear();
		for (const auto& entry : *list) {
			AttributeDef def;
			def.name = Get<std::string>(entry, "name", "");
			def.index = Get<int>(entry, "index", static_cast<int>(_attributes.size()));
			def.nameKey = Get<std::string>(entry, "nameKey", "$LA_Attr_" + def.name);
			const json* stats = nullptr;
			if (auto profiles = entry.find("profiles"); profiles != entry.end() && profiles->is_object() && profiles->contains("Default")) {
				stats = &(*profiles)["Default"];
			} else if (entry.contains("stats")) {
				stats = &entry["stats"];
			} else if (entry.contains("perPoint")) {
				stats = &entry["perPoint"];
			}
			if (stats && stats->is_array()) {
				for (const auto& s : *stats) {
					def.stats.push_back({ Get<std::string>(s, "stat", ""), Get<double>(s, "perPoint", Get<double>(s, "value", 0.0)) });
				}
			} else if (stats && stats->is_object()) {
				for (const auto& [key, value] : stats->items()) {
					if (value.is_number()) {
						def.stats.push_back({ key, value.get<double>() });
					}
				}
			}
			_attributes.push_back(std::move(def));
		}
		std::sort(_attributes.begin(), _attributes.end(), [](const auto& a, const auto& b) { return a.index < b.index; });
		return true;
	}

	bool Catalog::LoadSkills(const std::filesystem::path& a_file, std::vector<std::string>& a_errors)
	{
		auto text = ReadFile(a_file);
		if (!text) {
			a_errors.push_back("cannot read " + a_file.string());
			return false;
		}
		json root;
		try {
			root = json::parse(*text);
		} catch (const json::exception& e) {
			a_errors.push_back(a_file.filename().string() + ": " + e.what());
			return false;
		}
		const json* list = &root;
		for (const char* key : { "morrowindSkills", "skills", "mwSkills" }) {
			if (root.is_object() && root.contains(key)) {
				list = &root[key];
				break;
			}
		}
		if (!list->is_array()) {
			a_errors.push_back(a_file.filename().string() + ": expected skills array");
			return false;
		}
		_mwSkills.clear();
		for (const auto& entry : *list) {
			MwSkillDef def;
			def.name = Get<std::string>(entry, "name", "");
			def.index = Get<int>(entry, "index", static_cast<int>(_mwSkills.size()));
			def.nameKey = Get<std::string>(entry, "nameKey", "$LA_MwSkill_" + def.name);
			if (auto targets = entry.find("targets"); targets != entry.end() && targets->is_array()) {
				for (const auto& t : *targets) {
					SkillTarget target;
					const auto  skill = Get<std::string>(t, "skill", "");
					target.skyrimSkill = -1;
					for (std::size_t i = 0; i < kSkyrimSkills.size(); ++i) {
						if (IEquals(skill, kSkyrimSkills[i])) {
							target.skyrimSkill = static_cast<int>(i);
						}
					}
					if (target.skyrimSkill < 0) {
						target.special = skill.empty() ? Get<std::string>(t, "special", "") : skill;
					}
					target.weight = Get<double>(t, "weight", 1.0);
					def.targets.push_back(std::move(target));
				}
			}
			_mwSkills.push_back(std::move(def));
		}
		std::sort(_mwSkills.begin(), _mwSkills.end(), [](const auto& a, const auto& b) { return a.index < b.index; });
		return true;
	}
}
