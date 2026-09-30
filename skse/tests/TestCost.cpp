#include "Fixtures.h"

#include "lostart/CostEngine.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <random>

using namespace LA;
using namespace LA::Test;

namespace
{
	std::pair<std::uint32_t, std::uint32_t> Classic(const std::vector<SpellEffect>& a_effects)
	{
		static const auto catalog = MakeCatalog();
		const auto        r = Cost::Compute(catalog, a_effects, Test::Classic());
		return { r.cost, r.price };
	}
}

TEST_CASE("Classic worked examples (OUTLINE golden vectors)", "[cost][classic][golden]")
{
	using P = std::pair<std::uint32_t, std::uint32_t>;
	CHECK(Classic({ E("mw.fire_damage", Range::kTarget, 5, 10, 3, 10) }) == P{ 13, 91 });
	CHECK(Classic({ E("mw.fire_damage", Range::kTouch, 10, 10), E("mw.frost_damage", Range::kTarget, 10, 10) }) == P{ 15, 107 });
	CHECK(Classic({ E("mw.frost_damage", Range::kTarget, 10, 10), E("mw.fire_damage", Range::kTouch, 10, 10) }) == P{ 12, 89 });
	CHECK(Classic({ E("mw.fire_damage", Range::kTarget, 10, 20), E("mw.fortify_health", Range::kSelf, 20, 20, 30) }) == P{ 42, 297 });
	CHECK(Classic({ E("mw.fortify_health", Range::kSelf, 20, 20, 30), E("mw.fire_damage", Range::kTarget, 10, 20) }) == P{ 57, 405 });
	CHECK(Classic({ E("mw.levitate", Range::kSelf, 10, 10, 30) }) == P{ 46, 326 });
	CHECK(Classic({ E("mw.absorb_health", Range::kTouch, 10, 20, 5) }) == P{ 36, 253 });
	CHECK(Classic({ E("mw.mark", Range::kSelf, 1, 1) }) == P{ 43, 306 });
	CHECK(Classic({ E("mw.recall", Range::kSelf, 1, 1) }) == P{ 43, 306 });
	CHECK(Classic({ E("mw.divine_intervention", Range::kSelf, 1, 1) }) == P{ 18, 131 });
}

TEST_CASE("Classic cost of vanilla spells rebuilt in the spellmaker", "[cost][classic][golden]")
{
	CHECK(Classic({ E("mw.paralyze", Range::kTarget, 1, 1, 10) }).first == 34);
	CHECK(Classic({ E("mw.soultrap", Range::kTarget, 1, 1, 60) }).first == 9);
	CHECK(Classic({ E("mw.invisibility", Range::kSelf, 1, 1, 30) }).first == 31);
	CHECK(Classic({ E("mw.bound_longsword", Range::kSelf, 1, 1, 120) }).first == 12);
	CHECK(Classic({ E("mw.fire_damage", Range::kTarget, 60, 60) }).first == 45);            // Incinerate
	CHECK(Classic({ E("mw.summon_dremora", Range::kSelf, 1, 1, 60) }).first == 86);         // Dremora Lord
	CHECK(Classic({ E("mw.fire_damage", Range::kTarget, 40, 40, 1, 15) }).first == 32);     // Fireball
	CHECK(Classic({ E("mw.restore_health", Range::kSelf, 50, 50) }).first == 25);           // Fast Healing
	CHECK(Classic({ E("mw.restore_health", Range::kSelf, 100, 100) }).first == 50);         // Close Wounds
	CHECK(Classic({ E("mw.fire_damage", Range::kTarget, 25, 25) }).first == 18);            // Firebolt
	CHECK(Classic({ E("mw.summon_flame_atronach", Range::kSelf, 1, 1, 60) }).first == 70);
	CHECK(Classic({ E("mw.shock_damage", Range::kTarget, 25, 25) }).first == 26);           // Lightning Bolt
	CHECK(Classic({ E("mw.restore_health", Range::kTarget, 75, 75) }).first == 56);         // Heal Other
	CHECK(Classic({ E("mw.shield", Range::kSelf, 100, 100, 60) }).first == 610);            // Ebonyflesh
	CHECK(Classic({ E("mw.shield", Range::kSelf, 60, 60, 60) }).first == 366);              // Stoneflesh
	CHECK(Classic({ E("mw.shield", Range::kSelf, 40, 40, 60) }).first == 244);              // Oakflesh
}

TEST_CASE("Target x1.5 per effect when the running-total rule is off", "[cost][classic]")
{
	const auto catalog = MakeCatalog();
	auto       s = Test::Classic();
	s.targetRunningTotal = false;
	const auto a = Cost::Compute(catalog, { E("mw.fire_damage", Range::kTouch, 10, 10), E("mw.frost_damage", Range::kTarget, 10, 10) }, s);
	const auto b = Cost::Compute(catalog, { E("mw.frost_damage", Range::kTarget, 10, 10), E("mw.fire_damage", Range::kTouch, 10, 10) }, s);
	CHECK(a.cost == b.cost);
	CHECK(a.cost == 12);  // 5.125 + 5.125*1.5 = 12.8125
}

TEST_CASE("Classic global cost multiplier scales magicka, not price", "[cost][classic]")
{
	const auto catalog = MakeCatalog();
	auto       s = Test::Classic();
	s.globalCostMult = 2.0;
	const auto r = Cost::Compute(catalog, { E("mw.fire_damage", Range::kTarget, 5, 10, 3, 10) }, s);
	CHECK(r.cost == 26);
	CHECK(r.price == 91);
}

TEST_CASE("Skyrim-balanced reproduces vanilla fire costs", "[cost][balanced][golden]")
{
	const auto catalog = MakeCatalog();
	const Settings s;  // Skyrim-balanced is the default
	REQUIRE(s.costModel == CostModel::kSkyrimBalanced);
	CHECK(Cost::Compute(catalog, { E("mw.fire_damage", Range::kTarget, 25, 25) }, s).cost == 41);          // Firebolt
	CHECK(Cost::Compute(catalog, { E("mw.fire_damage", Range::kTarget, 40, 40, 1, 15) }, s).cost == 133);  // Fireball
	CHECK(Cost::Compute(catalog, { E("mw.fire_damage", Range::kTarget, 5, 10, 3, 10) }, s).cost == 59);
	CHECK(Cost::Compute(catalog, { E("mw.fire_damage", Range::kTarget, 10, 10, 10) }, s).cost == 188);
}

TEST_CASE("Skyrim-balanced price uses k = 3", "[cost][balanced]")
{
	const auto catalog = MakeCatalog();
	const auto r = Cost::Compute(catalog, { E("mw.fire_damage", Range::kTarget, 25, 25) }, Settings{});
	CHECK(r.price == static_cast<std::uint32_t>(std::floor(3 * r.y + 1e-9)));
}

TEST_CASE("Fitting a base cost from a vanilla spell", "[cost][balanced]")
{
	const auto catalog = MakeCatalog();
	const auto* fire = catalog.Find("mw.fire_damage");
	REQUIRE(fire);
	FitFrom firebolt{ {}, "Firebolt", 25, 0, 0, 41 };
	CHECK_THAT(Cost::FitBaseCost(*fire, firebolt), Catch::Matchers::WithinRel(FireBase(), 1e-9));
}

TEST_CASE("Target costs 1.5x only where vanilla has no priced Target version", "[cost][balanced]")
{
	const auto catalog = MakeCatalog();
	const auto* levitate = catalog.Find("mw.levitate");
	REQUIRE(levitate);
	REQUIRE_FALSE(levitate->vanillaTargetPriced);
	const auto self = Cost::BalancedEffect(*levitate, E("mw.levitate", Range::kSelf, 10, 10, 30));
	const auto target = Cost::BalancedEffect(*levitate, E("mw.levitate", Range::kTarget, 10, 10, 30));
	CHECK_THAT(target, Catch::Matchers::WithinRel(self * 1.5, 1e-12));
}

TEST_CASE("Property: effect order never changes a Skyrim-balanced cost", "[cost][property]")
{
	const auto catalog = MakeCatalog();
	std::mt19937 rng(1234);
	const Settings s;
	std::vector<std::string> ids;
	for (const auto& def : catalog.All()) {
		if (def.target == TargetKind::kNone) {
			ids.push_back(def.id);
		}
	}
	for (int trial = 0; trial < 500; ++trial) {
		std::vector<SpellEffect> effects;
		std::shuffle(ids.begin(), ids.end(), rng);
		const int n = 1 + static_cast<int>(rng() % 8);
		for (int i = 0; i < n; ++i) {
			const auto* def = catalog.Find(ids[static_cast<std::size_t>(i)]);
			const auto  ranges = def->AllowedRanges();
			const auto  lo = static_cast<std::uint16_t>(1 + rng() % 100);
			effects.push_back(E(def->id, ranges[rng() % ranges.size()], lo, static_cast<std::uint16_t>(lo + rng() % 50),
				static_cast<std::uint16_t>(1 + rng() % 120), static_cast<std::uint16_t>(rng() % 51)));
		}
		auto shuffled = effects;
		std::shuffle(shuffled.begin(), shuffled.end(), rng);
		const auto a = Cost::Compute(catalog, effects, s);
		const auto b = Cost::Compute(catalog, shuffled, s);
		// Summation order may differ in the last ulp; the floored cost must not.
		REQUIRE(a.cost == b.cost);
		REQUIRE(a.price >= 1);
		auto c = Test::Classic();
		REQUIRE(Cost::Compute(catalog, effects, c).price >= 1);
	}
}

TEST_CASE("Morrowind casting chance worked example", "[cost][chance][golden]")
{
	const auto catalog = MakeCatalog();
	const std::vector effects{ E("mw.fire_damage", Range::kTarget, 5, 10, 3, 10) };
	const auto cost = Cost::Compute(catalog, effects, Test::Classic()).cost;
	REQUIRE(cost == 13);
	// Destruction 30, Willpower 40, Luck 40. The menu previews at half fatigue (multiplier 1.0).
	CHECK(Cost::MorrowindChance(30, 40, 40, cost, 0, 0.5) == 59);
	CHECK(Cost::MorrowindChance(30, 40, 40, cost, 0, 1.0) == 73);
	CHECK(Cost::MorrowindChance(0, 0, 0, 500, 0, 1.0) == 0);
	CHECK(Cost::MorrowindChance(100, 100, 100, 0, 0, 1.0) == 100);
}

TEST_CASE("Skyrim casting chance module", "[cost][chance]")
{
	// Same numbers with the bonus standing in for W/5 + L/10 = 12.
	CHECK(Cost::SkyrimChance(30, 12, 13, 0, 0.5) == 59);
	CHECK(Cost::SkyrimChance(30, 12, 13, 0, 1.0) == 73);
	CHECK(Cost::SkyrimChance(30, 12, 13, 20, 1.0) == 48);  // Sound 20 comes off the chance
}

TEST_CASE("Hardest effect picks the lowest 2*skill - x", "[cost][chance]")
{
	const auto catalog = MakeCatalog();
	const std::vector effects{ E("mw.fortify_health", Range::kSelf, 5, 5, 5), E("mw.fire_damage", Range::kTarget, 50, 50, 10) };
	auto skill = [](const EffectDef& a_def) { return a_def.school == School::kDestruction ? 60.0 : 20.0; };
	// Fire x = 5*(100*10)/40*1.5 = 187.5 -> 2*60-187.5 = -67.5; Fortify x = 1*(10*5)/40 = 1.25 -> 40-1.25.
	CHECK(Cost::HardestEffect(catalog, effects, skill) == 1);
}

TEST_CASE("Rank ladders and cost thresholds", "[cost][rank]")
{
	const auto catalog = MakeCatalog();
	CHECK(Cost::SpellRank(catalog, { E("mw.fire_damage", Range::kTarget, 10, 10) }) == Rank::kNovice);
	CHECK(Cost::SpellRank(catalog, { E("mw.fire_damage", Range::kTarget, 25, 25) }) == Rank::kApprentice);
	CHECK(Cost::SpellRank(catalog, { E("mw.fire_damage", Range::kTarget, 40, 40) }) == Rank::kAdept);
	CHECK(Cost::SpellRank(catalog, { E("mw.fire_damage", Range::kTarget, 60, 60) }) == Rank::kExpert);
	CHECK(Cost::SpellRank(catalog, { E("mw.shield", Range::kSelf, 40, 40, 60) }) == Rank::kNovice);    // Oakflesh
	CHECK(Cost::SpellRank(catalog, { E("mw.shield", Range::kSelf, 80, 80, 60) }) == Rank::kAdept);
	CHECK(Cost::SpellRank(catalog, { E("mw.shield", Range::kSelf, 100, 100, 60) }) == Rank::kExpert);
	// The spell's rank is the highest among its effects.
	CHECK(Cost::SpellRank(catalog, { E("mw.fire_damage", Range::kTarget, 10, 10), E("mw.shield", Range::kSelf, 100, 100, 60) }) == Rank::kExpert);
}

TEST_CASE("Costliest school files the spell", "[cost]")
{
	const auto catalog = MakeCatalog();
	const Settings s;
	CHECK(Cost::CostliestSchool(catalog, { E("mw.fortify_health", Range::kSelf, 1, 1), E("mw.fire_damage", Range::kTarget, 30, 30) }, s) == School::kDestruction);
	CHECK(Cost::CostliestSchool(catalog, { E("mw.fortify_health", Range::kSelf, 100, 100, 600), E("mw.fire_damage", Range::kTarget, 1, 1) }, s) == School::kRestoration);
}
