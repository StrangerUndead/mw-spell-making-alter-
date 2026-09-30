#include "Fixtures.h"

#include "lostart/Discovery.h"

#include <catch2/catch_test_macros.hpp>

using namespace LA;
using namespace LA::Test;

namespace
{
	EffectDescriptor Fx(std::string a_form, std::string a_archetype, std::string a_av, std::string a_resist, bool a_hostile,
		std::vector<std::string> a_keywords = {}, std::string a_delivery = "Aimed")
	{
		EffectDescriptor d;
		d.form = ParseFormRef(a_form);
		d.archetype = std::move(a_archetype);
		d.actorValue = std::move(a_av);
		d.resist = std::move(a_resist);
		d.hostile = a_hostile;
		d.keywords = std::move(a_keywords);
		d.castingType = "FireAndForget";
		d.delivery = std::move(a_delivery);
		d.name = "Something";
		return d;
	}

	Discovery MakeDiscovery()
	{
		Discovery                d;
		std::vector<std::string> errors;
		REQUIRE(d.LoadVanillaFromString(R"({"map":[{"effect":"Skyrim.esm|0x05AD5D","id":"mw.shield","label":"AlterationArmorFFSelf"}]})", errors));
		REQUIRE(d.LoadRulesFromString(R"({"rules":[
			{"id":"mw.fire_damage","match":{"archetype":"ValueModifier","actorValue":"Health","resist":"FireResist","hostile":true}},
			{"id":"mw.frost_damage","match":{"archetype":"ValueModifier","actorValue":"Health","keywordsAny":["MagicDamageFrost"],"hostile":true}},
			{"id":"mw.restore_health","match":{"archetype":"ValueModifier","actorValue":"Health","hostile":false}}]})", errors));
		REQUIRE(errors.empty());
		return d;
	}
}

TEST_CASE("Lookup table first, then rules in order", "[discovery]")
{
	const auto d = MakeDiscovery();
	CHECK(d.Classify(Fx("Skyrim.esm|0x05AD5D", "ValueModifier", "DamageResist", "", false)) == "mw.shield");
	CHECK(d.Classify(Fx("Mod.esp|0x000801", "ValueModifier", "Health", "FireResist", true)) == "mw.fire_damage");
	CHECK(d.Classify(Fx("Mod.esp|0x000802", "valuemodifier", "health", "", true, { "MagicDamageFrost" })) == "mw.frost_damage");
	CHECK(d.Classify(Fx("Mod.esp|0x000803", "ValueModifier", "Health", "", false)) == "mw.restore_health");
	CHECK_FALSE(d.Classify(Fx("Mod.esp|0x000804", "Script", "", "", false)));
}

TEST_CASE("Only spells of type Spell teach effects (parity 3)", "[discovery][parity]")
{
	const auto     d = MakeDiscovery();
	const auto     catalog = MakeCatalog();
	const Settings s;
	std::vector<KnownSpell> spells{
		{ "Power", { Fx("Mod.esp|0x000801", "ValueModifier", "Health", "FireResist", true) } },
		{ "Ability", { Fx("Mod.esp|0x000803", "ValueModifier", "Health", "", false) } },
		{ "Disease", { Fx("Skyrim.esm|0x05AD5D", "ValueModifier", "DamageResist", "", false) } },
	};
	CHECK(d.Discover(catalog, spells, s).effects.empty());
	spells.push_back({ "Spell", { Fx("Mod.esp|0x000801", "ValueModifier", "Health", "FireResist", true) } });
	const auto result = d.Discover(catalog, spells, s);
	CHECK(result.effects == std::set<std::string>{ "mw.fire_damage" });
}

TEST_CASE("Hidden helper effects teach nothing; unknown effects may pass through", "[discovery]")
{
	const auto catalog = MakeCatalog();
	const auto d = MakeDiscovery();
	Settings   s;
	auto       hidden = Fx("Mod.esp|0x000801", "ValueModifier", "Health", "FireResist", true);
	hidden.hideInUI = true;
	auto modded = Fx("Mod.esp|0x000900", "Script", "", "", true, {}, "Aimed");
	modded.fullFormId = 0xFE000900;
	auto concentration = Fx("Mod.esp|0x000901", "Script", "", "", true);
	concentration.castingType = "Concentration";
	auto rune = Fx("Mod.esp|0x000902", "Script", "", "", true, {}, "TargetLocation");
	const std::vector<KnownSpell> spells{ { "Spell", { hidden, modded, concentration, rune } } };

	auto result = d.Discover(catalog, spells, s);
	REQUIRE(result.passThrough.size() == 1);
	CHECK(result.passThrough[0].set == EffectSet::kPassThrough);
	CHECK(result.passThrough[0].passThroughForm == 0xFE000900);
	CHECK(result.passThrough[0].AllowedRanges() == std::vector<Range>{ Range::kTarget });
	CHECK(result.effects.size() == 1);
	CHECK(result.unmatched.size() == 3);

	s.passThrough = false;
	result = d.Discover(catalog, spells, s);
	CHECK(result.effects.empty());
}

TEST_CASE("Settings filter effect sets; sandbox unlocks everything allowed", "[discovery]")
{
	Catalog c = MakeCatalog();
	auto    ext = Make("mwx.restore_magicka", 1, School::kRestoration, kAll);
	ext.set = EffectSet::kExtended;
	c.Add(ext);
	auto sk = Make("sk.muffle", 1, School::kIllusion, kSelfOnly);
	sk.set = EffectSet::kSkyrim;
	c.Add(sk);
	Settings s;
	auto     all = Discovery::Everything(c, s);
	CHECK(all.contains("sk.muffle"));
	CHECK_FALSE(all.contains("mwx.restore_magicka"));
	s.extended = true;
	s.skyrimOnly = false;
	all = Discovery::Everything(c, s);
	CHECK(all.contains("mwx.restore_magicka"));
	CHECK_FALSE(all.contains("sk.muffle"));
	s.attributeProfile = AttributeProfile::kOff;
	CHECK_FALSE(Discovery::Everything(c, s).contains("mw.fortify_attribute"));
}
