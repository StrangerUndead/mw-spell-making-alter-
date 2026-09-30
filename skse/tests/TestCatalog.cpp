#include "Fixtures.h"

#include "lostart/Catalog.h"

#include <catch2/catch_test_macros.hpp>

using namespace LA;

TEST_CASE("Effect JSON parses per CONTRACTS section 3", "[catalog]")
{
	Catalog                  c;
	std::vector<std::string> errors;
	REQUIRE(c.LoadEffectsFromString(R"([{
		"id": "mw.fire_damage", "name": "$LA_Effect_FireDamage", "set": "mw", "tier": "native",
		"morrowind": { "index": 14, "school": "Destruction", "baseCost": 5.0 },
		"skyrim": { "school": "Destruction", "baseCost": 1.18866, "vanillaTargetPriced": true,
		            "fitFrom": { "spell": "Skyrim.esm|0x012FD0", "label": "Firebolt", "magnitude": 25, "duration": 0, "area": 0, "cost": 41 } },
		"ranges": ["self", "touch", "target"],
		"magnitude": { "has": true, "unit": "pts", "ticking": true },
		"duration": true, "area": true, "target": "none",
		"keywords": ["MagicDamageFire"], "riders": ["IntenseFlames", "Impact"],
		"rankLadder": { "Apprentice": 25, "Adept": 40, "Expert": 60, "Master": 100 },
		"hostile": true, "sources": ["Skyrim.esm|0x012FD0"]
	}])", errors));
	REQUIRE(errors.empty());
	const auto* fire = c.Find("mw.fire_damage");
	REQUIRE(fire);
	CHECK(fire->pascalName == "FireDamage");
	CHECK(fire->mwIndex == 14);
	CHECK(fire->mwBaseCost == 5.0);
	CHECK(fire->school == School::kDestruction);
	CHECK(fire->AllowedRanges().size() == 3);
	CHECK(fire->ticking);
	CHECK(fire->unit == Unit::kPoints);
	CHECK(fire->fitFrom);
	CHECK(fire->fitFrom->spell.localId == 0x012FD0);
	CHECK(fire->rankLadder.at(Rank::kAdept) == 40);
	CHECK(fire->riders.size() == 2);
	CHECK(fire->sources.size() == 1);
}

TEST_CASE("Mysticism effects keep their Morrowind school and a Skyrim school", "[catalog]")
{
	Catalog                  c;
	std::vector<std::string> errors;
	REQUIRE(c.LoadEffectsFromString(R"([{"id":"mw.mark","name":"$LA_Effect_Mark","morrowind":{"index":60,"school":"Mysticism","baseCost":350},
		"skyrim":{"school":"Conjuration","baseCost":1},"ranges":["self"],"magnitude":{"has":false,"unit":"none"},"duration":false,"area":false}])", errors));
	const auto* mark = c.Find("mw.mark");
	REQUIRE(mark);
	CHECK(mark->mwSchool == MwSchool::kMysticism);
	CHECK(mark->school == School::kConjuration);
	CHECK_FALSE(mark->hasMagnitude);
	CHECK_FALSE(mark->hasDuration);
	CHECK_FALSE(mark->ShowsArea(Range::kSelf));
}

TEST_CASE("Bad entries are reported, not fatal", "[catalog]")
{
	Catalog                  c;
	std::vector<std::string> errors;
	CHECK_FALSE(c.LoadEffectsFromString(R"([{"name":"no id"},{"id":"mw.x","skyrim":{"school":"Mysticism"},"ranges":["self"]},
		{"id":"mw.y","skyrim":{"school":"Alteration"},"ranges":[]},
		{"id":"mw.ok","skyrim":{"school":"Alteration"},"ranges":["self"]},{"id":"mw.ok","skyrim":{"school":"Alteration"},"ranges":["self"]}])", errors));
	CHECK(errors.size() == 4);
	CHECK(c.Size() == 1);
	CHECK_FALSE(c.LoadEffectsFromString("not json", errors));
}

TEST_CASE("Pascal names from ids", "[catalog]")
{
	CHECK(Catalog::PascalFromId("mw.summon_ancestral_ghost") == "SummonAncestralGhost");
	CHECK(Catalog::PascalFromId("mwx.summon_centurion_sphere") == "SummonCenturionSphere");
	CHECK(Catalog::PascalFromId("sk.muffle") == "Muffle");
	CHECK(Catalog::SkyrimSkillName(0) == "OneHanded");
	CHECK(Catalog::SkyrimSkillName(17) == "Enchanting");
	CHECK(Catalog::AttributeName(7) == "Luck");
}
