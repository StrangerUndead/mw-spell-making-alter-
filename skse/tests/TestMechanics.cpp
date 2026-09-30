#include "Fixtures.h"

#include "lostart/Mechanics.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace LA;
using namespace LA::Test;
using Catch::Matchers::WithinAbs;

TEST_CASE("Magnitude rolls (parity 21)", "[mech][parity]")
{
	const auto catalog = MakeCatalog();
	const auto* fire = catalog.Find("mw.fire_damage");
	const auto* shield = catalog.Find("mw.shield");
	auto fixed = [](int, int a_hi) { return a_hi; };
	// One-time effects roll once.
	CHECK(Mech::LandedMagnitude(*shield, E("mw.shield", Range::kSelf, 10, 20, 30), true, fixed) == 20);
	// A 1-second damage effect is a single hit and rolls.
	CHECK(Mech::LandedMagnitude(*fire, E("mw.fire_damage", Range::kTarget, 10, 20, 1), true, fixed) == 20);
	// Ticking for 2 s or more applies the average.
	CHECK(Mech::LandedMagnitude(*fire, E("mw.fire_damage", Range::kTarget, 10, 20, 5), true, fixed) == 15);
	// Rolls off: always the average.
	CHECK(Mech::LandedMagnitude(*shield, E("mw.shield", Range::kSelf, 10, 20, 30), false, fixed) == 15);
	CHECK(Mech::MagnitudeScale(15, 20) == 0.75);
	// Default RNG stays in range.
	auto rng = Mech::DefaultRng();
	for (int i = 0; i < 200; ++i) {
		const auto v = Mech::LandedMagnitude(*shield, E("mw.shield", Range::kSelf, 3, 7, 30), true, rng);
		REQUIRE(v >= 3);
		REQUIRE(v <= 7);
	}
}

TEST_CASE("Area radius uses 22 units per foot", "[mech]")
{
	CHECK(Mech::AreaRadius(10) == 220);
	CHECK(Mech::AreaRadius(0) == 0);
}

TEST_CASE("Alteration helpers", "[mech]")
{
	CHECK(Mech::BurdenSpeedPenalty(40) == 20);
	CHECK(Mech::BurdenSpeedPenalty(400) == 75);
	CHECK(Mech::SlowfallFactor(100) == 0.5);
	CHECK(Mech::SlowfallFactor(300) == 0.0);
	CHECK(Mech::JumpBonusPercent(10, 3.0) == 30);
	CHECK_THAT(Mech::JumpFallReduction(10), WithinAbs(213.3, 1e-9));
}

TEST_CASE("Lock and Open against Skyrim lock tiers", "[mech]")
{
	CHECK(Mech::CanOpen(25, 25));
	CHECK_FALSE(Mech::CanOpen(50, 49));
	CHECK(Mech::CanOpen(100, 100));
	CHECK_FALSE(Mech::CanOpen(Mech::kRequiresKey, 100));
	CHECK(Mech::LockTarget(0, 60) == 50);
	CHECK(Mech::LockTarget(0, 100) == 100);
	CHECK_FALSE(Mech::LockTarget(75, 60));  // never lowers
	CHECK_FALSE(Mech::LockTarget(Mech::kRequiresKey, 100));
	CHECK(Mech::LockTarget(0, 1) == 1);
	CHECK_FALSE(Mech::LockTarget(0, 0.5));
}

TEST_CASE("Elemental shield retaliation follows Morrowind's formula", "[mech]")
{
	// No save and no resistance: 0.1 * M.
	CHECK_THAT(Mech::ShieldRetaliation(50, 0, 0, 1.0, 0, 99), WithinAbs(5.0, 1e-9));
	// Save 1.25*(40+15) = 68.75 vs roll 18 -> x = 50.75; resist 0 -> 0.1*50*(1-0.5075).
	CHECK_THAT(Mech::ShieldRetaliation(50, 40, 15, 1.0, 0, 18), WithinAbs(0.1 * 50 * (1 - 0.5075), 1e-9));
	// 100 % resistance cancels it.
	CHECK(Mech::ShieldRetaliation(50, 0, 0, 1.0, 100, 50) == 0);
}

TEST_CASE("Miss system", "[mech]")
{
	Mech::MissInputs in;
	in.attackerBlind = 30;
	CHECK(Mech::MissChance(in) == 30);
	in.defenderSanctuary = 200;  // capped at 75
	CHECK(Mech::MissChance(in) == 100);
	in = {};
	in.defenderChameleon = 50;
	in.attackerFortifyAttack = 5;
	CHECK(Mech::MissChance(in) == 5);
}

TEST_CASE("Charm, tiers and chance rolls", "[mech]")
{
	CHECK(Mech::CharmSteps(24) == 0);
	CHECK(Mech::CharmSteps(50) == 2);
	CHECK(Mech::CharmPriceBonus(30) == 15);
	CHECK(Mech::StrengthTier(10) == 0);
	CHECK(Mech::StrengthTier(30) == 1);
	CHECK(Mech::StrengthTier(80) == 2);
	CHECK(Mech::DispelRoll(40, 40));
	CHECK_FALSE(Mech::DispelRoll(40, 41));
	CHECK_FALSE(Mech::ReflectRoll(100, false, 1));
	CHECK(Mech::ChanceRoll(25, 25));
}

TEST_CASE("Divine Intervention picks the nearest temple in the same worldspace", "[mech]")
{
	std::vector<Mech::Destination> temples{ { "whiterun", 0x3C, { 0, 0, 0 } }, { "solitude", 0x3C, { 1000, 0, 0 } },
		{ "frostmoth", 0x0400D, { 5, 5, 0 } } };
	CHECK(Mech::Nearest(temples, 0x3C, { 900, 0, 0 }) == 1u);
	CHECK(Mech::Nearest(temples, 0x0400D, { 900, 0, 0 }) == 2u);
	CHECK_FALSE(Mech::Nearest(temples, 0x1234, { 0, 0, 0 }));
}

TEST_CASE("Disintegrate condition pools", "[mech]")
{
	CHECK(Mech::ConditionAfter(100, 5, 4) == 80);
	CHECK(Mech::ConditionAfter(10, 5, 4) == 0);
	CHECK(Mech::ConditionScale(80) == 0.8);
}

TEST_CASE("Attribute translation profiles", "[mech]")
{
	AttributeDef strength;
	strength.name = "Strength";
	strength.stats = { { "CarryWeight", 3 }, { "MeleeDamagePercent", 0.5 } };
	auto stats = Mech::AttributeStats(strength, 10, AttributeProfile::kDefault);
	REQUIRE(stats.size() == 2);
	CHECK(stats[0].value == 30);
	CHECK(stats[1].value == 5);
	CHECK(Mech::AttributeStats(strength, 10, AttributeProfile::kLight)[0].value == 15);
	CHECK(Mech::AttributeStats(strength, 10, AttributeProfile::kOff).empty());
}

TEST_CASE("Spellmaker refusal reasons (parity 2)", "[mech][parity]")
{
	Settings          s;
	Mech::RefusalFacts facts;
	CHECK(Mech::RefusalReason(Mech::RefusalRule::kCollege, facts, s) == 1);
	facts.collegeMember = true;
	CHECK(Mech::RefusalReason(Mech::RefusalRule::kCollege, facts, s) == 0);
	s.collegeMembership = false;
	facts.collegeMember = false;
	CHECK(Mech::RefusalReason(Mech::RefusalRule::kCollege, facts, s) == 0);

	facts = {};
	facts.wantedInHold = true;
	CHECK(Mech::RefusalReason(Mech::RefusalRule::kWanted, facts, s) == 2);
	s.refuseWanted = false;
	CHECK(Mech::RefusalReason(Mech::RefusalRule::kWanted, facts, s) == 0);

	facts = {};
	facts.questDone = false;
	CHECK(Mech::RefusalReason(Mech::RefusalRule::kQuest, facts, s) == 3);
	facts = {};
	facts.vampireStage4 = true;
	CHECK(Mech::RefusalReason(Mech::RefusalRule::kNone, facts, s) == 4);
	facts = {};
	facts.relationshipRank = -1;
	CHECK(Mech::RefusalReason(Mech::RefusalRule::kNone, facts, s) == 5);
	facts = {};
	facts.servicesEnabled = false;
	CHECK(Mech::RefusalReason(Mech::RefusalRule::kNone, facts, s) == 6);
	CHECK(Mech::ParseRefusalRule("wanted:Whiterun") == Mech::RefusalRule::kWanted);
}

TEST_CASE("Morrowind difficulty stays on the Classic scale", "[mech]")
{
	const auto catalog = MakeCatalog();
	Settings   s;  // Skyrim-balanced magicka model
	s.globalCostMult = 3.0;
	CHECK(Mech::MorrowindDifficulty(catalog, { E("mw.fire_damage", Range::kTarget, 5, 10, 3, 10) }, s) == 13);
}
