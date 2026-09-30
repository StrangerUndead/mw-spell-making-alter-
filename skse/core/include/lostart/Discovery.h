#pragma once

#include "lostart/Catalog.h"
#include "lostart/Settings.h"
#include "lostart/Util.h"

#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace LA
{
	// What the plugin reads off a magic effect in a known spell (OUTLINE "Effect Discovery").
	struct EffectDescriptor
	{
		FormRef                  form;         // plugin|local id of the MGEF
		std::uint32_t            fullFormId{ 0 };
		std::string              editorId;     // may be empty at runtime
		std::string              name;
		std::string              archetype;    // CK archetype name without spaces
		std::string              actorValue;
		std::string              secondAV;
		std::string              resist;
		std::vector<std::string> keywords;     // EditorIDs
		bool                     hostile{ false };
		bool                     detrimental{ false };
		std::string              castingType;  // ConstantEffect | FireAndForget | Concentration
		std::string              delivery;     // Self | Touch | Aimed | TargetActor | TargetLocation
		bool                     hideInUI{ false };
		bool                     noMagnitude{ false };
		bool                     noDuration{ false };
		bool                     noArea{ false };
		bool                     scripted{ false };  // has a script attached (ignores magnitude)
		double                   baseCost{ 0 };
		std::string              school;       // Alteration..Restoration
	};

	// A spell the player knows, as the plugin sees it.
	struct KnownSpell
	{
		std::string                   type;  // Spell | Power | LesserPower | Ability | Disease | Voice | ...
		std::vector<EffectDescriptor> effects;
	};

	struct DiscoveryRule
	{
		std::string              id;
		std::optional<std::string> archetype;
		std::optional<std::string> actorValue;
		std::optional<std::string> resist;
		std::optional<bool>      hostile;
		std::vector<std::string> keywordsAny;
		std::vector<std::string> keywordsAll;
		std::vector<std::string> delivery;
		std::optional<std::string> castingType;

		bool Matches(const EffectDescriptor& a_effect) const;
	};

	struct DiscoveryResult
	{
		std::set<std::string>        effects;       // catalog ids now known
		std::vector<EffectDef>       passThrough;   // new pass-through catalog entries
		std::vector<std::string>     unmatched;     // form refs that matched nothing (log)
	};

	class Discovery
	{
	public:
		bool LoadVanilla(const std::filesystem::path& a_file, std::vector<std::string>& a_errors);
		bool LoadRules(const std::filesystem::path& a_file, std::vector<std::string>& a_errors);
		bool LoadVanillaFromString(std::string_view a_json, std::vector<std::string>& a_errors);
		bool LoadRulesFromString(std::string_view a_json, std::vector<std::string>& a_errors);

		void AddLookup(const FormRef& a_form, std::string a_effectId);
		void AddRule(DiscoveryRule a_rule) { _rules.push_back(std::move(a_rule)); }

		// Maps one effect: lookup table, then rules (first match wins). nullopt = unrecognized.
		std::optional<std::string> Classify(const EffectDescriptor& a_effect) const;

		// Runs Morrowind's rule over the player's spell list: only spells of type Spell count.
		// Effects are filtered by the settings (Skyrim-only / Extended sets, pass-through).
		DiscoveryResult Discover(const Catalog& a_catalog, const std::vector<KnownSpell>& a_spells, const Settings& a_settings) const;

		// Sandbox mode: every catalog effect allowed by the settings.
		static std::set<std::string> Everything(const Catalog& a_catalog, const Settings& a_settings);
		static bool Allowed(const EffectDef& a_def, const Settings& a_settings);

		std::size_t LookupSize() const { return _lookup.size(); }
		std::size_t RuleCount() const { return _rules.size(); }

	private:
		std::unordered_map<std::string, std::string> _lookup;  // "Skyrim.esm|0x012FD0" lowercased -> id
		std::vector<DiscoveryRule>                   _rules;
	};

	// Builds a pass-through catalog entry for an unrecognized fire-and-forget effect.
	EffectDef MakePassThrough(const EffectDescriptor& a_effect);
}
