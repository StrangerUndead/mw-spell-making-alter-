#include "Fixtures.h"

#include "lostart/Settings.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

using namespace LA;

TEST_CASE("Defaults match the outline's MCM tables", "[settings]")
{
	const Settings s;
	CHECK(s.costModel == CostModel::kSkyrimBalanced);
	CHECK(s.PriceMultiplier() == 3.0);
	CHECK(s.maxEffects == 8);
	CHECK(s.magnitudeCap == 100);
	CHECK(s.durationCap == 1440);
	CHECK(s.areaCap == 50);
	CHECK(s.touchReach == 192);
	CHECK(s.castingBonus == 15);
	CHECK_FALSE(s.castingFailure);
	CHECK(s.minMaxRolls);
	CHECK(s.marks == 1);
	CHECK(s.deletable == 1);
	Settings classic;
	classic.costModel = CostModel::kClassic;
	CHECK(classic.PriceMultiplier() == 7.0);
}

TEST_CASE("INI parsing: sections, comments, case", "[settings]")
{
	const auto ini = ParseIni("; comment\n[Costs]\niCostModel = 1 ; classic\nfGlobalCostMult=2.5\n[MENU]\nIMAXEFFECTS=4\nbCloseAfterCreate=0\n");
	Settings   s;
	s.Apply(ini);
	CHECK(s.costModel == CostModel::kClassic);
	CHECK(s.globalCostMult == 2.5);
	CHECK(s.maxEffects == 4);
	CHECK_FALSE(s.closeAfterCreate);
}

TEST_CASE("Values are clamped to the MCM ranges", "[settings]")
{
	Settings s;
	s.Apply(ParseIni("[Menu]\niMaxEffects=12\niMagnitudeCap=50\n[Costs]\nfGlobalCostMult=10\niCostModel=9\n[Casting]\niMarks=0\n"));
	CHECK(s.maxEffects == 8);
	CHECK(s.magnitudeCap == 100);
	CHECK(s.globalCostMult == 4.0);
	CHECK(s.costModel == CostModel::kEngineAutocalc);
	CHECK(s.marks == 1);
}

TEST_CASE("User settings override MCM defaults; float sliders accepted for ints", "[settings]")
{
	const auto dir = std::filesystem::temp_directory_path();
	const auto defaults = dir / "la_defaults.ini";
	const auto user = dir / "la_user.ini";
	std::ofstream(defaults) << "[Costs]\niCostModel=0\nfPriceMult=3.0\n[Menu]\niAreaCap=50\n";
	std::ofstream(user) << "[Costs]\niCostModel=1.000000\n";
	const auto s = LoadSettings(defaults, user);
	CHECK(s.costModel == CostModel::kClassic);
	CHECK(s.areaCap == 50);
	const auto missing = LoadSettings(dir / "nope1.ini", dir / "nope2.ini");
	CHECK(missing.costModel == CostModel::kSkyrimBalanced);
}

TEST_CASE("The shipped MCM settings.ini loads to the documented defaults", "[settings][data]")
{
	const auto path = LA::Test::SourceDir() / "mcm" / "settings.ini";
	if (!std::filesystem::exists(path)) {
		SKIP("mcm/settings.ini not present");
	}
	const auto s = LoadSettings(path, {});
	const Settings d;
	CHECK(s.costModel == d.costModel);
	CHECK(s.maxEffects == d.maxEffects);
	CHECK(s.priceMult == d.priceMult);
	CHECK(s.priceMultClassic == d.priceMultClassic);
	CHECK(s.touchReach == d.touchReach);
	CHECK(s.castingBonus == d.castingBonus);
	CHECK(s.deletable == d.deletable);
	CHECK(s.skyrimOnly == d.skyrimOnly);
	CHECK(s.extended == d.extended);
}
