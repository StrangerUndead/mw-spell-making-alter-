#include "lostart/Discovery.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace LA
{
	using json = nlohmann::json;

	namespace
	{
		std::string Key(const FormRef& a_ref) { return ToLower(a_ref.ToString()); }

		std::optional<std::string> ReadFile(const std::filesystem::path& a_file)
		{
			std::ifstream file(a_file, std::ios::binary);
			if (!file) {
				return std::nullopt;
			}
			std::ostringstream ss;
			ss << file.rdbuf();
			return ss.str();
		}

		bool HasKeyword(const EffectDescriptor& a_effect, std::string_view a_keyword)
		{
			return std::any_of(a_effect.keywords.begin(), a_effect.keywords.end(), [&](const auto& k) { return IEquals(k, a_keyword); });
		}

		bool HasFlag(const EffectDescriptor& a_effect, std::string_view a_flag)
		{
			if (IEquals(a_flag, "Hostile") && a_effect.hostile) {
				return true;
			}
			if (IEquals(a_flag, "Detrimental") && a_effect.detrimental) {
				return true;
			}
			if (IEquals(a_flag, "HideInUI") && a_effect.hideInUI) {
				return true;
			}
			return std::any_of(a_effect.flags.begin(), a_effect.flags.end(), [&](const auto& f) { return IEquals(f, a_flag); });
		}

		bool IsSkillActorValue(std::string_view a_av)
		{
			for (std::size_t i = 0; i < kSkyrimSkillCount; ++i) {
				if (IEquals(a_av, Catalog::SkyrimSkillName(static_cast<int>(i)))) {
					return true;
				}
			}
			return false;
		}

		std::optional<std::string> OptString(const json& a_obj, const char* a_key)
		{
			if (auto it = a_obj.find(a_key); it != a_obj.end() && it->is_string()) {
				return it->get<std::string>();
			}
			return std::nullopt;
		}

		std::vector<std::string> Strings(const json& a_obj, const char* a_key)
		{
			std::vector<std::string> out;
			if (auto it = a_obj.find(a_key); it != a_obj.end()) {
				if (it->is_array()) {
					for (const auto& v : *it) {
						if (v.is_string()) {
							out.push_back(v.get<std::string>());
						}
					}
				} else if (it->is_string()) {
					out.push_back(it->get<std::string>());
				}
			}
			return out;
		}
	}

	bool DiscoveryRule::Matches(const EffectDescriptor& a_effect) const
	{
		if (archetype && !IEquals(*archetype, a_effect.archetype)) {
			return false;
		}
		if (actorValue && !IEquals(*actorValue, a_effect.actorValue)) {
			return false;
		}
		if (actorValueGroup && IEquals(*actorValueGroup, "Skill") && !IsSkillActorValue(a_effect.actorValue)) {
			return false;
		}
		if (resist && !IEquals(*resist, a_effect.resist)) {
			return false;
		}
		if (hostile && *hostile != a_effect.hostile) {
			return false;
		}
		if (castingType && !IEquals(*castingType, a_effect.castingType)) {
			return false;
		}
		if (!delivery.empty() && std::none_of(delivery.begin(), delivery.end(), [&](const auto& d) { return IEquals(d, a_effect.delivery); })) {
			return false;
		}
		if (!keywordsAny.empty() && std::none_of(keywordsAny.begin(), keywordsAny.end(), [&](const auto& k) { return HasKeyword(a_effect, k); })) {
			return false;
		}
		for (const auto& k : keywordsAll) {
			if (!HasKeyword(a_effect, k)) {
				return false;
			}
		}
		for (const auto& k : keywordsNone) {
			if (HasKeyword(a_effect, k)) {
				return false;
			}
		}
		for (const auto& f : flagsAll) {
			if (!HasFlag(a_effect, f)) {
				return false;
			}
		}
		for (const auto& f : flagsNone) {
			if (HasFlag(a_effect, f)) {
				return false;
			}
		}
		return true;
	}

	void Discovery::AddLookup(const FormRef& a_form, std::string a_effectId)
	{
		auto& ids = _lookup[Key(a_form)];
		if (std::find(ids.begin(), ids.end(), a_effectId) == ids.end()) {
			ids.push_back(std::move(a_effectId));
		}
	}

	bool Discovery::LoadVanillaFromString(std::string_view a_json, std::vector<std::string>& a_errors)
	{
		json root;
		try {
			root = json::parse(a_json);
		} catch (const json::exception& e) {
			a_errors.push_back(std::string("vanilla.json: ") + e.what());
			return false;
		}
		const json* list = &root;
		if (root.is_object()) {
			if (auto it = root.find("map"); it != root.end()) {
				list = &*it;
			}
		}
		if (list->is_object()) {
			// Also accept {"Skyrim.esm|0x..": "mw.id"}.
			for (const auto& [ref, id] : list->items()) {
				if (auto form = ParseFormRef(ref); form.Valid() && id.is_string()) {
					AddLookup(form, id.get<std::string>());
				}
			}
			return true;
		}
		if (!list->is_array()) {
			a_errors.push_back("vanilla.json: expected {\"map\": [...]}");
			return false;
		}
		for (const auto& entry : *list) {
			const auto form = ParseFormRef(OptString(entry, "effect").value_or(""));
			const auto id = OptString(entry, "id");
			if (!form.Valid() || !id) {
				a_errors.push_back("vanilla.json: bad entry " + entry.dump());
				continue;
			}
			AddLookup(form, *id);
		}
		return true;
	}

	bool Discovery::LoadRulesFromString(std::string_view a_json, std::vector<std::string>& a_errors)
	{
		json root;
		try {
			root = json::parse(a_json);
		} catch (const json::exception& e) {
			a_errors.push_back(std::string("rules.json: ") + e.what());
			return false;
		}
		const json* list = &root;
		if (root.is_object()) {
			if (auto it = root.find("rules"); it != root.end()) {
				list = &*it;
			}
		}
		if (!list->is_array()) {
			a_errors.push_back("rules.json: expected {\"rules\": [...]}");
			return false;
		}
		for (const auto& entry : *list) {
			DiscoveryRule rule;
			rule.id = OptString(entry, "id").value_or("");
			const json& match = entry.contains("match") ? entry["match"] : entry;
			rule.alsoIds = Strings(entry, "alsoIds");
			rule.archetype = OptString(match, "archetype");
			rule.actorValueGroup = OptString(match, "actorValueGroup");
			rule.actorValue = OptString(match, "actorValue");
			rule.resist = OptString(match, "resist");
			if (auto it = match.find("hostile"); it != match.end() && it->is_boolean()) {
				rule.hostile = it->get<bool>();
			}
			rule.keywordsAny = Strings(match, "keywordsAny");
			rule.keywordsAll = Strings(match, "keywordsAll");
			rule.keywordsNone = Strings(match, "keywordsNone");
			rule.flagsAll = Strings(match, "flagsAll");
			rule.flagsNone = Strings(match, "flagsNone");
			rule.delivery = Strings(match, "delivery");
			rule.castingType = OptString(match, "castingType");
			if (rule.id.empty()) {
				a_errors.push_back("rules.json: rule without id");
				continue;
			}
			AddRule(std::move(rule));
		}
		return true;
	}

	bool Discovery::LoadVanilla(const std::filesystem::path& a_file, std::vector<std::string>& a_errors)
	{
		auto text = ReadFile(a_file);
		if (!text) {
			a_errors.push_back("cannot read " + a_file.string());
			return false;
		}
		return LoadVanillaFromString(*text, a_errors);
	}

	bool Discovery::LoadRules(const std::filesystem::path& a_file, std::vector<std::string>& a_errors)
	{
		auto text = ReadFile(a_file);
		if (!text) {
			a_errors.push_back("cannot read " + a_file.string());
			return false;
		}
		return LoadRulesFromString(*text, a_errors);
	}

	std::vector<std::string> Discovery::ClassifyAll(const EffectDescriptor& a_effect) const
	{
		if (a_effect.form.Valid()) {
			if (auto it = _lookup.find(Key(a_effect.form)); it != _lookup.end() && !it->second.empty()) {
				return it->second;
			}
		}
		for (const auto& rule : _rules) {
			if (rule.Matches(a_effect)) {
				std::vector<std::string> ids{ rule.id };
				ids.insert(ids.end(), rule.alsoIds.begin(), rule.alsoIds.end());
				return ids;
			}
		}
		return {};
	}

	std::optional<std::string> Discovery::Classify(const EffectDescriptor& a_effect) const
	{
		auto ids = ClassifyAll(a_effect);
		if (ids.empty()) {
			return std::nullopt;
		}
		return ids.front();
	}

	bool Discovery::Allowed(const EffectDef& a_def, const Settings& a_settings)
	{
		switch (a_def.set) {
		case EffectSet::kMorrowind:
			break;
		case EffectSet::kExtended:
			if (!a_settings.extended) {
				return false;
			}
			break;
		case EffectSet::kSkyrim:
			if (!a_settings.skyrimOnly) {
				return false;
			}
			break;
		case EffectSet::kPassThrough:
			if (!a_settings.passThrough) {
				return false;
			}
			break;
		}
		if (a_def.target == TargetKind::kAttribute && a_settings.attributeProfile == AttributeProfile::kOff) {
			return false;
		}
		return true;
	}

	std::set<std::string> Discovery::Everything(const Catalog& a_catalog, const Settings& a_settings)
	{
		std::set<std::string> out;
		for (const auto& def : a_catalog.All()) {
			if (def.set != EffectSet::kPassThrough && Allowed(def, a_settings)) {
				out.insert(def.id);
			}
		}
		return out;
	}

	EffectDef MakePassThrough(const EffectDescriptor& a_effect)
	{
		EffectDef def;
		def.id = "pt." + ToLower(a_effect.form.ToString());
		def.pascal = "PassThrough";
		def.nameKey = a_effect.name;  // not a key: the StringTable returns it unchanged
		def.set = EffectSet::kPassThrough;
		def.tier = Tier::kNative;
		def.school = SchoolFromString(a_effect.school).value_or(School::kAlteration);
		def.skBaseCost = a_effect.baseCost > 0 ? a_effect.baseCost : 1.0;
		// Morrowind-scale difficulty for the failure module: roughly 4x the Skyrim base cost.
		def.mwBaseCost = def.skBaseCost * 4.0;
		def.vanillaTargetPriced = true;
		def.archetype = a_effect.archetype;
		def.vanillaEffect = a_effect.form;
		// "Pass through at their own delivery": only the delivery the modded effect was built for.
		if (IEquals(a_effect.delivery, "Self")) {
			def.ranges = Bit(Range::kSelf);
		} else if (IEquals(a_effect.delivery, "Touch")) {
			def.ranges = Bit(Range::kTouch);
		} else {
			def.ranges = Bit(Range::kTarget);
		}
		def.hasMagnitude = !a_effect.noMagnitude && !a_effect.scripted;
		def.unit = def.hasMagnitude ? Unit::kPoints : Unit::kNone;
		def.hasDuration = !a_effect.noDuration;
		def.hasArea = !a_effect.noArea && !IEquals(a_effect.delivery, "Self");
		def.hostile = a_effect.hostile;
		def.passThroughForm = a_effect.fullFormId;
		def.sources.push_back(a_effect.form);
		return def;
	}

	DiscoveryResult Discovery::Discover(const Catalog& a_catalog, const std::vector<KnownSpell>& a_spells, const Settings& a_settings) const
	{
		DiscoveryResult result;
		std::set<std::string> seenPassThrough;
		for (const auto& spell : a_spells) {
			// Morrowind's rule: only normal spells (not powers, abilities, diseases or curses).
			if (!IEquals(spell.type, "Spell")) {
				continue;
			}
			for (const auto& effect : spell.effects) {
				if (effect.hideInUI) {
					continue;  // riders and other hidden helper effects teach nothing
				}
				if (auto ids = ClassifyAll(effect); !ids.empty()) {
					for (const auto& id : ids) {
						if (const auto* def = a_catalog.Find(id); def && Allowed(*def, a_settings)) {
							result.effects.insert(id);
						}
					}
					continue;
				}
				result.unmatched.push_back(effect.form.ToString());
				// Unrecognized fire-and-forget effects may pass through (concentration and
				// target-location effects stay hidden until v1.x).
				if (!a_settings.passThrough || !IEquals(effect.castingType, "FireAndForget") || IEquals(effect.delivery, "TargetLocation")) {
					continue;
				}
				auto def = MakePassThrough(effect);
				if (seenPassThrough.insert(def.id).second) {
					result.effects.insert(def.id);
					result.passThrough.push_back(std::move(def));
				}
			}
		}
		return result;
	}
}
