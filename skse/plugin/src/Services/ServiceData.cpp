#include "Services/ServiceData.h"

#include <fstream>

#include <nlohmann/json.hpp>

namespace LA::Services
{
	namespace
	{
		using json = nlohmann::json;

		std::optional<json> ReadJson(const std::filesystem::path& a_file)
		{
			std::ifstream in(a_file, std::ios::binary);
			if (!in) {
				logger::warn("services: cannot open {}"sv, a_file.string());
				return std::nullopt;
			}
			try {
				return json::parse(in, nullptr, true, true);
			} catch (const std::exception& e) {
				logger::error("services: {}: {}"sv, a_file.string(), e.what());
				return std::nullopt;
			}
		}

		std::string Str(const json& a_obj, const char* a_key)
		{
			const auto it = a_obj.find(a_key);
			return it != a_obj.end() && it->is_string() ? it->get<std::string>() : std::string{};
		}

		ServiceData Load()
		{
			ServiceData data;
			const auto  dir = State::Get().dataDir / "content";

			if (auto root = ReadJson(dir / "spellmakers.json")) {
				data.collegeFaction = ParseFormRef(Str(*root, "collegeFaction"));
				if (auto holds = root->find("holds"); holds != root->end() && holds->is_object()) {
					for (const auto& [hold, value] : holds->items()) {
						if (value.is_object()) {
							data.holdCrimeFactions[hold] = ParseFormRef(Str(value, "crimeFaction"));
						}
					}
				}
				if (auto list = root->find("spellmakers"); list != root->end() && list->is_array()) {
					for (const auto& s : *list) {
						SpellmakerExtra extra;
						extra.refusalLine = Str(s, "refusalLine");
						const auto rule = Str(s, "refusal");
						extra.anyone = rule == "anyone" || rule.empty();
						if (rule.starts_with("wanted:")) {
							extra.hold = rule.substr(7);
						}
						data.spellmakers[Str(s, "id")] = std::move(extra);
					}
				}
			}

			if (auto root = ReadJson(dir / "altars.json")) {
				if (auto list = root->find("altars"); list != root->end() && list->is_array()) {
					for (const auto& a : *list) {
						AltarExtra extra;
						extra.nameKey = Str(a, "nameKey");
						extra.freeForFaction = ParseFormRef(Str(a, "freeForFaction"));
						data.altars[Str(a, "id")] = std::move(extra);
					}
				}
			}

			if (auto root = ReadJson(dir / "tomes.json")) {
				if (auto list = root->find("tomes"); list != root->end() && list->is_array()) {
					for (const auto& t : *list) {
						TomeExtra tome;
						// Generator (ItemsBuilder.TomePascal): "$LA_Tome_<Pascal>" -> BOOK "LA_Tome_<Pascal>".
						if (const auto title = Str(t, "title"); title.starts_with("$LA_Tome_")) {
							tome.bookEditorId = title.substr(1);
						} else if (const auto edid = Str(t, "editorId"); edid.starts_with("LA_Tome_")) {
							tome.bookEditorId = edid;
						} else {
							tome.bookEditorId = "LA_Tome_" + Catalog::PascalFromId(Str(t, "id"));
						}
						for (const char* key : { "level", "lootLevel" }) {
							if (auto it = t.find(key); it != t.end() && it->is_number()) {
								tome.level = static_cast<std::uint16_t>(std::clamp(it->get<int>(), 1, 255));
								break;
							}
						}
						if (auto loot = t.find("loot"); loot != t.end() && loot->is_boolean()) {
							tome.loot = loot->get<bool>();
						}
						if (auto lists = t.find("lootLists"); lists != t.end() && lists->is_array()) {
							for (const auto& l : *lists) {
								if (l.is_string()) {
									tome.lootLists.push_back(ParseFormRef(l.get<std::string>()));
								}
							}
						}
						if (auto vendors = t.find("vendors"); vendors != t.end() && vendors->is_array()) {
							for (const auto& v : *vendors) {
								if (v.is_string()) {
									tome.vendors.push_back(v.get<std::string>());
								}
							}
						}
						data.tomes.push_back(std::move(tome));
					}
				}
			}

			if (!data.collegeFaction.Valid()) {
				data.collegeFaction = FormRef{ "Skyrim.esm", Vanilla::kCollegeOfWinterholdFaction };
			}
			logger::info("services: {} spellmakers, {} altars, {} tomes (extras)"sv, data.spellmakers.size(), data.altars.size(), data.tomes.size());
			return data;
		}
	}

	const ServiceData& ServiceData::Get()
	{
		static const ServiceData data = Load();
		return data;
	}
}
