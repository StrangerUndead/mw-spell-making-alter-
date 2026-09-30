#pragma once

#include "lostart/Compiler.h"
#include "lostart/Mechanics.h"
#include "lostart/Types.h"
#include "lostart/Util.h"

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace LA
{
	struct StandIn
	{
		std::string effect;
		FormRef     creature;
		std::string nameKey;
		double      levelScale{ 1.0 };
		std::string credit;
	};

	struct Spellmaker
	{
		std::string              id;
		FormRef                  npc;
		std::string              name;
		std::string              location;
		Mech::RefusalRule        rule{ Mech::RefusalRule::kNone };
		std::string              ruleArg;       // hold name, or quest ref
		FormRef                  quest;         // for quest rules
		int                      questStage{ 0 };
		FormRef                  crimeFaction;  // for wanted rules
		FormRef                  merchantChest;
		double                   priceMult{ 1.0 };
		std::vector<std::string> specialties;
	};

	struct Altar
	{
		std::string              id;
		std::string              furniture;  // EditorID in LostArt.esp
		FormRef                  cell;
		std::array<float, 3>     position{};
		std::array<float, 3>     rotation{};
		std::optional<std::pair<std::array<float, 3>, std::array<float, 3>>> alternate;
		std::string              access;   // "college" | "anyone"
		FormRef                  owner;
		std::string              freeFor;  // "archmage" or empty
	};

	struct Tome
	{
		std::string              id;
		std::string              titleKey;
		std::string              spellNameKey;
		std::string              school;
		std::vector<SpellEffect> effects;
		int                      value{ 0 };
		std::vector<std::string> vendors;
		bool                     loot{ true };
	};

	struct RankPerks
	{
		std::array<double, 4>                            costThresholds{ 60, 130, 250, 500 };
		std::map<std::string, std::map<std::string, FormRef>> perks;  // school -> rank -> perk
	};

	// Everything under data/ besides the effect catalog and discovery rules.
	class Content
	{
	public:
		bool LoadAll(const std::filesystem::path& a_dataDir, std::vector<std::string>& a_errors);

		bool LoadRiders(const std::filesystem::path& a_file, std::vector<std::string>& a_errors);
		bool LoadStandIns(const std::filesystem::path& a_file, std::vector<std::string>& a_errors);
		bool LoadSpellmakers(const std::filesystem::path& a_file, std::vector<std::string>& a_errors);
		bool LoadAltars(const std::filesystem::path& a_file, std::vector<std::string>& a_errors);
		bool LoadTomes(const std::filesystem::path& a_file, std::vector<std::string>& a_errors);
		bool LoadRanks(const std::filesystem::path& a_file, std::vector<std::string>& a_errors);

		const RiderInfo* FindRider(std::string_view a_id) const;
		RiderLookup      Riders() const
		{
			return [this](std::string_view a_id) { return FindRider(a_id); };
		}

		const std::vector<RiderInfo>&  AllRiders() const { return _riders; }
		const std::vector<StandIn>&    StandIns() const { return _standIns; }
		const std::vector<Spellmaker>& Spellmakers() const { return _spellmakers; }
		const std::vector<Altar>&      Altars() const { return _altars; }
		const std::vector<Tome>&       Tomes() const { return _tomes; }
		const RankPerks&               Ranks() const { return _ranks; }

	private:
		std::vector<RiderInfo>  _riders;
		std::vector<StandIn>    _standIns;
		std::vector<Spellmaker> _spellmakers;
		std::vector<Altar>      _altars;
		std::vector<Tome>       _tomes;
		RankPerks               _ranks;
	};
}
