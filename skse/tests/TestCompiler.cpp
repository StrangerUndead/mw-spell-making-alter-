#include "Fixtures.h"

#include "lostart/Compiler.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

using namespace LA;
using namespace LA::Test;

namespace
{
	std::vector<RiderInfo> Riders()
	{
		std::vector<RiderInfo> riders;
		riders.push_back({ "IntenseFlames", ParseFormRef("Skyrim.esm|0x0F2F2F"), {}, false, 1.0 });
		riders.push_back({ "Impact", ParseFormRef("Skyrim.esm|0x0F2F30"), {}, false, 1.0 });
		riders.push_back({ "FrostStaminaSlow", ParseFormRef("Skyrim.esm|0x0F2F31"), {}, true, 1.0 });
		riders.push_back({ "DeepFreeze", ParseFormRef("Skyrim.esm|0x0F2F32"), {}, false, 1.0 });
		return riders;
	}

	RiderLookup Lookup(const std::vector<RiderInfo>& a_riders)
	{
		return [&a_riders](std::string_view a_id) -> const RiderInfo* {
			for (const auto& r : a_riders) {
				if (r.id == a_id) {
					return &r;
				}
			}
			return nullptr;
		};
	}
}

TEST_CASE("Variant EditorIDs follow CONTRACTS naming", "[compiler]")
{
	const auto catalog = MakeCatalog();
	CHECK(Compiler::VariantEditorId(*catalog.Find("mw.fire_damage"), "", Range::kTarget) == "LA_FireDamage_Target");
	CHECK(Compiler::VariantEditorId(*catalog.Find("mw.fortify_attribute"), "Strength", Range::kSelf) == "LA_FortifyAttribute_Strength_Self");
}

TEST_CASE("Single-range spell compiles to one spell", "[compiler]")
{
	const auto catalog = MakeCatalog();
	const auto plan = Compiler::Plan(catalog, { E("mw.levitate", Range::kSelf, 10, 10, 30) }, Settings{});
	REQUIRE(plan.spells.size() == 1);
	CHECK(plan.SubSlotsNeeded() == 0);
	CHECK(plan.Primary().range == Range::kSelf);
	CHECK(plan.Primary().entries.at(0).variantEditorId == "LA_Levitate_Self");
	CHECK(plan.Primary().entries.at(0).duration == 30);
	CHECK(plan.school == School::kAlteration);
}

TEST_CASE("Mixed ranges: farthest range is equipped, others become linked sub-spells", "[compiler]")
{
	const auto catalog = MakeCatalog();
	const auto plan = Compiler::Plan(catalog,
		{ E("mw.fortify_health", Range::kSelf, 20, 20, 30), E("mw.frost_damage", Range::kTarget, 10, 10), E("mw.absorb_health", Range::kTouch, 5, 5) },
		Settings{});
	REQUIRE(plan.spells.size() == 3);
	CHECK(plan.Primary().range == Range::kTarget);
	CHECK(plan.Primary().primary);
	CHECK(plan.spells[1].range == Range::kTouch);
	CHECK(plan.spells[2].range == Range::kSelf);
	CHECK(plan.SubSlotsNeeded() == 2);
	CHECK(plan.Primary().hostile);
	CHECK_FALSE(plan.spells[2].hostile);
}

TEST_CASE("The costliest Target effect leads (its projectile is drawn)", "[compiler]")
{
	const auto catalog = MakeCatalog();
	const auto plan = Compiler::Plan(catalog, { E("mw.frost_damage", Range::kTarget, 5, 5), E("mw.fire_damage", Range::kTarget, 50, 50) }, Settings{});
	CHECK(plan.Primary().entries.front().effectId == "mw.fire_damage");
}

TEST_CASE("Riders are appended hidden; elemental riders follow the setting", "[compiler]")
{
	const auto catalog = MakeCatalog();
	const auto riders = Riders();
	Settings   s;
	auto plan = Compiler::Plan(catalog, { E("mw.frost_damage", Range::kTarget, 10, 10) }, s, Lookup(riders));
	REQUIRE(plan.Primary().entries.size() == 3);
	CHECK(plan.Primary().entries[1].riderId == "FrostStaminaSlow");
	CHECK(plan.Primary().entries[1].vanillaEffect.localId == 0x0F2F31);
	s.elementalRiders = false;
	plan = Compiler::Plan(catalog, { E("mw.frost_damage", Range::kTarget, 10, 10) }, s, Lookup(riders));
	REQUIRE(plan.Primary().entries.size() == 2);
	CHECK(plan.Primary().entries[1].riderId == "DeepFreeze");
}

TEST_CASE("Over the engine ceiling, whole effects move to an overflow sub-spell", "[compiler]")
{
	Catalog c;
	for (int i = 0; i < 8; ++i) {
		auto def = Make("mw.e" + std::to_string(i), 1, School::kDestruction, kAll);
		def.riders = { "a", "b" };  // 3 entries per effect -> 24 total
		c.Add(def);
	}
	std::vector<SpellEffect> effects;
	for (int i = 0; i < 8; ++i) {
		effects.push_back(E("mw.e" + std::to_string(i), Range::kTarget, 1, 1));
	}
	const auto plan = Compiler::Plan(c, effects, Settings{});
	CHECK_FALSE(plan.tooComplex);
	REQUIRE(plan.spells.size() == 2);
	CHECK(plan.spells[0].entries.size() == 15);
	CHECK(plan.spells[1].entries.size() == 9);
	CHECK(plan.spells[1].range == Range::kTarget);
	CHECK_FALSE(plan.spells[1].primary);
}

TEST_CASE("An effect whose bundle alone exceeds the ceiling is too complex", "[compiler]")
{
	Catalog c;
	auto    def = Make("mw.big", 1, School::kDestruction, kAll);
	for (int i = 0; i < 15; ++i) {
		def.riders.push_back("r" + std::to_string(i));
	}
	c.Add(def);
	CHECK(Compiler::Plan(c, { E("mw.big", Range::kSelf, 1, 1) }, Settings{}).tooComplex);
}

TEST_CASE("Durations: a 1-second ticking effect is one instant hit", "[compiler]")
{
	const auto catalog = MakeCatalog();
	auto       plan = Compiler::Plan(catalog, { E("mw.fire_damage", Range::kTarget, 10, 20, 1) }, Settings{});
	CHECK(plan.Primary().entries[0].duration == 0);
	CHECK(plan.Primary().entries[0].maxMag == 20);
	CHECK(plan.Primary().entries[0].minMag == 10);
	plan = Compiler::Plan(catalog, { E("mw.fire_damage", Range::kTarget, 10, 20, 5) }, Settings{});
	CHECK(plan.Primary().entries[0].duration == 5);
	plan = Compiler::Plan(catalog, { E("mw.mark", Range::kSelf, 1, 1) }, Settings{});
	CHECK(plan.Primary().entries[0].duration == 0);
	CHECK(plan.Primary().entries[0].maxMag == 0);
}

TEST_CASE("Area is kept for the plugin's resolver and dropped on Self", "[compiler]")
{
	const auto catalog = MakeCatalog();
	const auto plan = Compiler::Plan(catalog, { E("mw.fire_damage", Range::kTarget, 10, 10, 1, 15), E("mw.fortify_health", Range::kSelf, 5, 5, 10, 20) }, Settings{});
	CHECK(plan.spells[0].entries[0].area == 15);
	CHECK(plan.spells[1].entries[0].area == 0);
}

TEST_CASE("Morrowind skills split across Skyrim skills by weight", "[compiler]")
{
	auto c = MakeCatalog();
	std::vector<std::string> errors;
	const char* skills = R"({"morrowindSkills":[
		{"index":6,"name":"Axe","targets":[{"skill":"OneHanded","weight":0.5},{"skill":"TwoHanded","weight":0.5}]},
		{"index":20,"name":"Acrobatics","targets":[{"skill":"Jump","weight":1.0}]}]})";
	const auto path = std::filesystem::temp_directory_path() / "la_skills_test.json";
	{
		std::ofstream(path) << skills;
	}
	REQUIRE(c.LoadSkills(path, errors));
	const auto plan = Compiler::Plan(c, { E("mw.fortify_skill", Range::kSelf, 10, 10, 30, 0, kMwSkillBase + 6), E("mw.fortify_skill", Range::kSelf, 10, 10, 30, 0, kMwSkillBase + 20) }, Settings{});
	REQUIRE(plan.Primary().entries.size() == 3);
	CHECK(plan.Primary().entries[0].variantEditorId == "LA_FortifySkill_OneHanded_Self");
	CHECK(plan.Primary().entries[0].maxMag == 5);
	CHECK(plan.Primary().entries[1].variantEditorId == "LA_FortifySkill_TwoHanded_Self");
	CHECK(plan.Primary().entries[2].variantEditorId == "LA_FortifySkill_Acrobatics_Self");
	CHECK(plan.Primary().entries[2].maxMag == 10);
}
