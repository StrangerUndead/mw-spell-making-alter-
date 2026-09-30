// Runs the core against the shipped data/ files: counts, golden vectors with the real catalog,
// and that every loader accepts what the data pipeline produces.
#include "Fixtures.h"

#include "lostart/Compiler.h"
#include "lostart/Content.h"
#include "lostart/CostEngine.h"
#include "lostart/Discovery.h"
#include "lostart/MenuSession.h"
#include "lostart/Text.h"

#include <catch2/catch_test_macros.hpp>

#include <map>

using namespace LA;
using namespace LA::Test;

namespace
{
	std::filesystem::path Data() { return SourceDir() / "data"; }

	bool HaveCatalog()
	{
		std::error_code ec;
		const auto      dir = Data() / "effects";
		return std::filesystem::is_directory(dir, ec) && !std::filesystem::is_empty(dir, ec);
	}

	const Catalog& RealCatalog()
	{
		static Catalog catalog = [] {
			Catalog                  c;
			std::vector<std::string> errors;
			c.LoadEffects(Data() / "effects", errors);
			c.LoadAttributes(Data() / "content" / "attributes.json", errors);
			c.LoadSkills(Data() / "content" / "skills.json", errors);
			return c;
		}();
		return catalog;
	}
}

TEST_CASE("Shipped catalog loads cleanly with the outline's counts", "[data]")
{
	if (!HaveCatalog()) {
		SKIP("data/effects not present");
	}
	Catalog                  c;
	std::vector<std::string> errors;
	const bool               ok = c.LoadEffects(Data() / "effects", errors);
	for (const auto& e : errors) {
		UNSCOPED_INFO(e);
	}
	REQUIRE(ok);

	std::map<MwSchool, int> perSchool;
	std::map<Tier, int>     perTier;
	int                     morrowind = 0;
	for (const auto& def : c.All()) {
		if (def.set != EffectSet::kMorrowind) {
			continue;
		}
		++morrowind;
		++perSchool[def.mwSchool];
		++perTier[def.tier];
		INFO(def.id);
		CHECK(def.ranges != 0);
		CHECK(def.mwBaseCost > 0);
		CHECK(def.skBaseCost > 0);
		CHECK_FALSE(def.nameKey.empty());
		if (def.AllowedRanges() == std::vector<Range>{ Range::kSelf }) {
			CHECK_FALSE(def.ShowsArea(Range::kSelf));
		}
	}
	CHECK(morrowind == 119);
	CHECK(perTier[Tier::kNative] == 65);
	CHECK(perTier[Tier::kCustom] == 44);
	CHECK(perTier[Tier::kStandIn] == 10);
	CHECK(perSchool[MwSchool::kAlteration] == 14);
	CHECK(perSchool[MwSchool::kConjuration] == 29);
	CHECK(perSchool[MwSchool::kDestruction] == 21);
	CHECK(perSchool[MwSchool::kIllusion] == 18);
	CHECK(perSchool[MwSchool::kMysticism] == 15);
	CHECK(perSchool[MwSchool::kRestoration] == 22);
}

TEST_CASE("Golden vectors hold against the shipped catalog", "[data][golden]")
{
	if (!HaveCatalog()) {
		SKIP("data/effects not present");
	}
	const auto& c = RealCatalog();
	for (auto id : { "mw.fire_damage", "mw.frost_damage", "mw.fortify_health", "mw.levitate", "mw.absorb_health", "mw.mark",
			 "mw.recall", "mw.divine_intervention" }) {
		INFO(id);
		REQUIRE(c.Find(id));
	}
	const auto classic = Test::Classic();
	auto       cp = [&](std::vector<SpellEffect> a_effects) {
        const auto r = Cost::Compute(c, a_effects, classic);
        return std::pair{ r.cost, r.price };
	};
	using P = std::pair<std::uint32_t, std::uint32_t>;
	CHECK(cp({ E("mw.fire_damage", Range::kTarget, 5, 10, 3, 10) }) == P{ 13, 91 });
	CHECK(cp({ E("mw.fire_damage", Range::kTouch, 10, 10), E("mw.frost_damage", Range::kTarget, 10, 10) }) == P{ 15, 107 });
	CHECK(cp({ E("mw.frost_damage", Range::kTarget, 10, 10), E("mw.fire_damage", Range::kTouch, 10, 10) }) == P{ 12, 89 });
	CHECK(cp({ E("mw.fire_damage", Range::kTarget, 10, 20), E("mw.fortify_health", Range::kSelf, 20, 20, 30) }) == P{ 42, 297 });
	CHECK(cp({ E("mw.fortify_health", Range::kSelf, 20, 20, 30), E("mw.fire_damage", Range::kTarget, 10, 20) }) == P{ 57, 405 });
	CHECK(cp({ E("mw.levitate", Range::kSelf, 10, 10, 30) }) == P{ 46, 326 });
	CHECK(cp({ E("mw.absorb_health", Range::kTouch, 10, 20, 5) }) == P{ 36, 253 });
	CHECK(cp({ E("mw.mark", Range::kSelf, 1, 1) }) == P{ 43, 306 });
	CHECK(cp({ E("mw.recall", Range::kSelf, 1, 1) }) == P{ 43, 306 });
	CHECK(cp({ E("mw.divine_intervention", Range::kSelf, 1, 1) }) == P{ 18, 131 });

	const Settings balanced;
	auto           b = [&](std::vector<SpellEffect> a_effects) { return Cost::Compute(c, a_effects, balanced).cost; };
	CHECK(b({ E("mw.fire_damage", Range::kTarget, 25, 25) }) == 41);
	CHECK(b({ E("mw.fire_damage", Range::kTarget, 40, 40, 1, 15) }) == 133);
	CHECK(b({ E("mw.fire_damage", Range::kTarget, 5, 10, 3, 10) }) == 59);
	CHECK(b({ E("mw.fire_damage", Range::kTarget, 10, 10, 10) }) == 188);
}

TEST_CASE("Every catalog effect compiles on each allowed range", "[data][compiler]")
{
	if (!HaveCatalog()) {
		SKIP("data/effects not present");
	}
	const auto& c = RealCatalog();
	Content     content;
	std::vector<std::string> errors;
	content.LoadRiders(Data() / "content" / "riders.json", errors);
	const Settings s;
	std::size_t    variants = 0;
	for (const auto& def : c.All()) {
		for (auto range : def.AllowedRanges()) {
			std::int16_t sub = def.target == TargetKind::kNone ? kNoSub : 0;
			const auto   plan = Compiler::Plan(c, { E(def.id, range, 10, 20, 10, def.ShowsArea(range) ? 5 : 0, sub) }, s, content.Riders());
			INFO(def.id << " " << ToString(range));
			REQUIRE(plan.spells.size() == 1);
			CHECK_FALSE(plan.tooComplex);
			CHECK(plan.Primary().entries.size() <= kEngineEffectCeiling);
			CHECK(plan.Primary().hostile == def.hostile);
			CHECK_FALSE(plan.Primary().entries[0].variantEditorId.empty());
			++variants;
		}
	}
	CHECK(variants > 200);
}

TEST_CASE("Every catalog name and card key is translated", "[data][text]")
{
	const auto file = Data() / "translations" / "LostArt_ENGLISH.txt";
	if (!HaveCatalog() || !std::filesystem::exists(file)) {
		SKIP("data not present");
	}
	StringTable strings;
	REQUIRE(strings.LoadFile(file));
	for (const auto& def : RealCatalog().All()) {
		INFO(def.id);
		CHECK(strings.Has(def.nameKey));
		if (!def.itemCardKey.empty()) {
			CHECK(strings.Has(def.itemCardKey));
		}
	}
	for (int m = 0; m <= static_cast<int>(Msg::kNeedSoulGem); ++m) {
		const auto key = "$LA_Msg_" + std::string(MessageKey(static_cast<Msg>(m)));
		INFO(key);
		CHECK(strings.Has(key));
	}
	CHECK(strings.Get("$LA_Msg_sNotifyMessage28") == "You can only add eight effects to a spell.");
	CHECK(strings.Get("$LA_Msg_sOnetypeEffectMessage") == "This effect has already been added.");
	CHECK(strings.Get("$LA_Msg_sNotifyMessage30") == "You have to add at least one effect to a spell.");
	CHECK(strings.Get("$LA_Msg_sNotifyMessage10") == "You have to name the spell before buying it.");
	CHECK(strings.Get("$LA_Msg_sEnchantmentMenu8") == "You cannot buy a spell that has a zero point cost.");
	CHECK(strings.Get("$LA_Msg_sNotifyMessage18") == "You don't have enough gold to buy this spell.");

	// Real formatter over the real strings.
	EffectFormatter fmt(RealCatalog(), strings);
	CHECK(fmt.Line(E("mw.fire_damage", Range::kTarget, 5, 10, 3, 10)) == "Fire Damage 5 to 10 pts for 3 secs in 10 ft on Target");
}

TEST_CASE("Content and discovery files load", "[data]")
{
	if (!std::filesystem::exists(Data() / "content" / "spellmakers.json")) {
		SKIP("content not present");
	}
	Content                  content;
	std::vector<std::string> errors;
	const bool               ok = content.LoadAll(Data(), errors);
	for (const auto& e : errors) {
		UNSCOPED_INFO(e);
	}
	CHECK(ok);
	CHECK(content.Spellmakers().size() == 15);
	CHECK(content.Altars().size() == 2);
	CHECK(content.StandIns().size() >= 10);
	CHECK(content.Tomes().size() >= 40);
	for (const auto& tome : content.Tomes()) {
		for (const auto& e : tome.effects) {
			INFO(tome.id << " " << e.effectId);
			CHECK(RealCatalog().Find(e.effectId));
		}
	}
	for (const auto& def : RealCatalog().All()) {
		for (const auto& rider : def.riders) {
			INFO(def.id << " rider " << rider);
			CHECK(content.FindRider(rider));
		}
	}

	Discovery d;
	CHECK(d.LoadVanilla(Data() / "discovery" / "vanilla.json", errors));
	CHECK(d.LoadRules(Data() / "discovery" / "rules.json", errors));
	CHECK(d.LookupSize() > 50);
	CHECK(d.RuleCount() > 10);

	Catalog                  attrs;
	CHECK(attrs.LoadAttributes(Data() / "content" / "attributes.json", errors));
	CHECK(attrs.Attributes().size() == 8);
	CHECK(attrs.LoadSkills(Data() / "content" / "skills.json", errors));
	CHECK(attrs.MwSkills().size() == 27);
}
