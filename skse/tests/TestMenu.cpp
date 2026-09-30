#include "Fixtures.h"

#include "lostart/MenuSession.h"

#include <catch2/catch_test_macros.hpp>

using namespace LA;
using namespace LA::Test;

namespace
{
	struct Harness
	{
		Catalog     catalog = MakeCatalog();
		Settings    settings;
		MenuSession session;

		explicit Harness(Settings a_settings = {}, Provider a_provider = {}) :
			settings(a_settings),
			session(catalog, settings, a_provider, Known(catalog))
		{}

		static std::set<std::string> Known(const Catalog& a_catalog)
		{
			std::set<std::string> ids;
			for (const auto& def : a_catalog.All()) {
				ids.insert(def.id);
			}
			return ids;
		}

		void Add(std::string_view a_id)
		{
			REQUIRE(session.AddEffect(a_id).ok());
			REQUIRE(session.Editor());
			REQUIRE(session.EditorOk().ok());
		}

		PurchaseContext Rich() const
		{
			PurchaseContext ctx;
			ctx.gold = 1'000'000;
			ctx.freePrimarySlots = 500;
			ctx.freeSubSlots = 500;
			return ctx;
		}
	};

	const char* kEight[] = { "mw.fire_damage", "mw.frost_damage", "mw.shock_damage", "mw.fortify_health",
		"mw.restore_health", "mw.levitate", "mw.shield", "mw.invisibility" };
}

TEST_CASE("At most 8 effects (parity 5)", "[menu][parity]")
{
	Harness h;
	for (auto id : kEight) {
		h.Add(id);
	}
	CHECK(h.session.Effects().size() == 8);
	const auto outcome = h.session.AddEffect("mw.paralyze");
	REQUIRE(outcome.messages.size() == 1);
	CHECK(outcome.messages[0] == Msg::kMaxEffects);
	CHECK(MessageKey(Msg::kMaxEffects) == "sNotifyMessage28");
	CHECK_FALSE(h.session.CanAdd("mw.paralyze"));
}

TEST_CASE("The effect limit is a setting", "[menu]")
{
	Settings s;
	s.maxEffects = 2;
	Harness h(s);
	h.Add("mw.fire_damage");
	h.Add("mw.frost_damage");
	CHECK(h.session.AddEffect("mw.shock_damage").messages.at(0) == Msg::kMaxEffects);
}

TEST_CASE("Limit is checked before duplicates", "[menu][parity]")
{
	Harness h;
	for (auto id : kEight) {
		h.Add(id);
	}
	CHECK(h.session.AddEffect("mw.fire_damage").messages.at(0) == Msg::kMaxEffects);
}

TEST_CASE("No effect twice (parity 6)", "[menu][parity]")
{
	Harness h;
	h.Add("mw.fire_damage");
	const auto outcome = h.session.AddEffect("mw.fire_damage");
	REQUIRE(outcome.messages.size() == 1);
	CHECK(outcome.messages[0] == Msg::kDuplicate);
	CHECK(MessageKey(outcome.messages[0]) == "sOnetypeEffectMessage");
	CHECK_FALSE(h.session.CanAdd("mw.fire_damage"));
	CHECK_FALSE(h.session.Editor());
}

TEST_CASE("Attribute effects open a picker and may repeat on another attribute (parity 6-7)", "[menu][parity]")
{
	Harness h;
	REQUIRE(h.session.AddEffect("mw.fortify_attribute").ok());
	REQUIRE(h.session.Picker());
	CHECK(h.session.Picker()->options.size() == 8);
	CHECK_FALSE(h.session.Editor());
	REQUIRE(h.session.PickTarget(0).ok());  // Strength
	REQUIRE(h.session.Editor());
	CHECK(h.session.Editor()->effect.sub == 0);
	h.session.EditorOk();

	// Another attribute is fine...
	h.session.AddEffect("mw.fortify_attribute");
	REQUIRE(h.session.PickTarget(4).ok());  // Speed
	h.session.EditorOk();
	CHECK(h.session.Effects().size() == 2);

	// ...the same one is a duplicate.
	h.session.AddEffect("mw.fortify_attribute");
	const auto dup = h.session.PickTarget(0);
	CHECK(dup.messages.at(0) == Msg::kDuplicate);
	CHECK(h.session.Effects().size() == 2);
	CHECK(h.session.CanAdd("mw.fortify_attribute"));
}

TEST_CASE("Skill picker lists Skyrim's 18 skills by default", "[menu]")
{
	Harness h;
	h.session.AddEffect("mw.fortify_skill");
	REQUIRE(h.session.Picker());
	CHECK(h.session.Picker()->options.size() == 18);
	CHECK(h.session.Picker()->options.front().second == "OneHanded");
}

TEST_CASE("A new effect starts at 1 to 1 pts, 1 sec, 0 ft with OpenMW's starting range (parity 13)", "[menu][parity]")
{
	Harness h;
	h.session.AddEffect("mw.fire_damage");
	REQUIRE(h.session.Editor());
	const auto& e = h.session.Editor()->effect;
	CHECK(e.minMag == 1);
	CHECK(e.maxMag == 1);
	CHECK(e.duration == 1);
	CHECK(e.area == 0);
	CHECK(e.range == Range::kTouch);  // three-range effect
	h.session.EditorCancel();

	h.session.AddEffect("mw.absorb_health");
	CHECK(h.session.Editor()->effect.range == Range::kTarget);  // Touch/Target effect
	h.session.EditorCancel();

	h.session.AddEffect("mw.mark");
	CHECK(h.session.Editor()->effect.range == Range::kSelf);
}

TEST_CASE("Starting range 'first allowed' option", "[menu]")
{
	Settings s;
	s.startingRange = 1;
	Harness h(s);
	h.session.AddEffect("mw.fire_damage");
	CHECK(h.session.Editor()->effect.range == Range::kSelf);
	h.session.EditorCancel();
	h.session.AddEffect("mw.absorb_health");
	CHECK(h.session.Editor()->effect.range == Range::kTouch);
}

TEST_CASE("Range button cycles Self -> Touch -> Target skipping disallowed ranges (parity 8)", "[menu][parity]")
{
	Harness h;
	h.session.AddEffect("mw.fire_damage");
	REQUIRE(h.session.Editor()->effect.range == Range::kTouch);
	h.session.EditorCycleRange();
	CHECK(h.session.Editor()->effect.range == Range::kTarget);
	h.session.EditorCycleRange();
	CHECK(h.session.Editor()->effect.range == Range::kSelf);
	h.session.EditorCycleRange();
	CHECK(h.session.Editor()->effect.range == Range::kTouch);
	h.session.EditorCancel();

	h.session.AddEffect("mw.absorb_health");
	h.session.EditorCycleRange();
	CHECK(h.session.Editor()->effect.range == Range::kTouch);
	h.session.EditorCycleRange();
	CHECK(h.session.Editor()->effect.range == Range::kTarget);
	h.session.EditorCancel();

	h.session.AddEffect("mw.mark");
	CHECK_FALSE(h.session.EditorCycleRange().changed);
}

TEST_CASE("Magnitude rules (parity 9)", "[menu][parity]")
{
	Harness h;
	h.session.AddEffect("mw.fire_damage");
	h.session.EditorSet(EditorField::kMax, 20);
	h.session.EditorSet(EditorField::kMin, 10);
	CHECK(h.session.Editor()->effect.minMag == 10);
	CHECK(h.session.Editor()->effect.maxMag == 20);
	// Raising min pulls max up.
	h.session.EditorSet(EditorField::kMin, 30);
	CHECK(h.session.Editor()->effect.maxMag == 30);
	// Max can't drop below min.
	h.session.EditorSet(EditorField::kMax, 5);
	CHECK(h.session.Editor()->effect.maxMag == 30);
	// 1..100 by default.
	h.session.EditorSet(EditorField::kMin, 0);
	CHECK(h.session.Editor()->effect.minMag == 1);
	h.session.EditorSet(EditorField::kMax, 500);
	CHECK(h.session.Editor()->effect.maxMag == 100);
}

TEST_CASE("Magnitude cap is a setting", "[menu]")
{
	Settings s;
	s.magnitudeCap = 300;
	Harness h(s);
	h.session.AddEffect("mw.fire_damage");
	h.session.EditorSet(EditorField::kMax, 500);
	CHECK(h.session.Editor()->effect.maxMag == 300);
}

TEST_CASE("Duration 1-1440, hidden without duration (parity 10)", "[menu][parity]")
{
	Harness h;
	h.session.AddEffect("mw.levitate");
	h.session.EditorSet(EditorField::kDuration, 5000);
	CHECK(h.session.Editor()->effect.duration == 1440);
	h.session.EditorSet(EditorField::kDuration, 0);
	CHECK(h.session.Editor()->effect.duration == 1);
	h.session.EditorCancel();
	h.session.AddEffect("mw.mark");
	CHECK_FALSE(h.session.EditorSet(EditorField::kDuration, 30).changed);
}

TEST_CASE("Area 0-50, hidden on Self, reset when switching to Self (parity 11)", "[menu][parity]")
{
	Harness h;
	h.session.AddEffect("mw.fire_damage");  // starts on Touch
	h.session.EditorSet(EditorField::kArea, 80);
	CHECK(h.session.Editor()->effect.area == 50);
	h.session.EditorCycleRange();  // Target keeps area
	CHECK(h.session.Editor()->effect.area == 50);
	h.session.EditorCycleRange();  // Self zeroes it
	CHECK(h.session.Editor()->effect.range == Range::kSelf);
	CHECK(h.session.Editor()->effect.area == 0);
	CHECK_FALSE(h.session.EditorSet(EditorField::kArea, 10).changed);
}

TEST_CASE("Slider steps: magnitude 10, duration 20, area 5; arrows 1 (parity 12)", "[menu][parity]")
{
	Harness h;
	h.session.AddEffect("mw.fire_damage");
	h.session.EditorStep(EditorField::kMax, 1, true);
	CHECK(h.session.Editor()->effect.maxMag == 11);
	h.session.EditorStep(EditorField::kMax, 1, false);
	CHECK(h.session.Editor()->effect.maxMag == 12);
	h.session.EditorStep(EditorField::kDuration, 1, true);
	CHECK(h.session.Editor()->effect.duration == 21);
	h.session.EditorStep(EditorField::kArea, 1, true);
	CHECK(h.session.Editor()->effect.area == 5);
	h.session.EditorStep(EditorField::kArea, -1, false);
	CHECK(h.session.Editor()->effect.area == 4);
}

TEST_CASE("Editor cancel drops a new effect and reverts an edited one", "[menu]")
{
	Harness h;
	h.session.AddEffect("mw.fire_damage");
	h.session.EditorSet(EditorField::kMax, 40);
	h.session.EditorCancel();
	CHECK(h.session.Effects().empty());

	h.Add("mw.fire_damage");
	h.session.EditEffect(0);
	h.session.EditorSet(EditorField::kMax, 40);
	h.session.EditorCancel();
	CHECK(h.session.Effects()[0].maxMag == 1);

	h.session.EditEffect(0);
	h.session.EditorSet(EditorField::kMax, 40);
	h.session.EditorOk();
	CHECK(h.session.Effects()[0].maxMag == 40);

	h.session.EditEffect(0);
	h.session.EditorDelete();
	CHECK(h.session.Effects().empty());
}

TEST_CASE("Reordering changes the Classic cost (order quirk)", "[menu]")
{
	Harness h(Test::Classic());
	h.session.AddEffect("mw.fire_damage");
	h.session.EditorSet(EditorField::kMax, 10);
	h.session.EditorSet(EditorField::kMin, 10);
	h.session.EditorOk();  // Touch
	h.session.AddEffect("mw.frost_damage");
	h.session.EditorCycleRange();
	h.session.EditorSet(EditorField::kMin, 10);
	h.session.EditorOk();  // Target
	CHECK(h.session.PreviewCost().cost == 15);
	h.session.MoveEffect(1, -1);
	CHECK(h.session.PreviewCost().cost == 12);
	CHECK_FALSE(h.session.MoveEffect(0, -1).changed);
}

TEST_CASE("Buy checks run in Morrowind's order (parity 18)", "[menu][parity]")
{
	Harness h;
	auto ctx = h.Rich();
	Outcome o1;
	CHECK_FALSE(h.session.TryCreate(ctx, o1));
	CHECK(o1.messages.at(0) == Msg::kNoEffects);

	h.Add("mw.fire_damage");
	Outcome o2;
	CHECK_FALSE(h.session.TryCreate(ctx, o2));
	CHECK(o2.messages.at(0) == Msg::kNoName);

	h.session.SetName("   ");
	Outcome o3;
	CHECK_FALSE(h.session.TryCreate(ctx, o3));
	CHECK(o3.messages.at(0) == Msg::kNoName);

	h.session.SetName("Spark");
	ctx.gold = 0;
	Outcome o4;
	CHECK_FALSE(h.session.TryCreate(ctx, o4));
	CHECK(o4.messages.at(0) == Msg::kNoGold);

	ctx.gold = 1000;
	Outcome o5;
	auto purchase = h.session.TryCreate(ctx, o5);
	REQUIRE(purchase);
	CHECK(o5.ok());
	CHECK(purchase->def.name == "Spark");
	CHECK(purchase->def.cost == h.session.PreviewCost().cost);
	CHECK(purchase->price == h.session.PreviewPrice());
}

TEST_CASE("Zero-cost spells can't be bought", "[menu][parity]")
{
	Catalog c;
	auto    free = Make("mw.free", 0.0, School::kAlteration, kSelfOnly);
	free.skBaseCost = 0.0;
	c.Add(free);
	Settings    s;
	MenuSession session(c, s, {}, { "mw.free" });
	session.AddEffect("mw.free");
	session.EditorOk();
	session.SetName("Nothing");
	PurchaseContext ctx;
	ctx.gold = 100;
	ctx.freePrimarySlots = 1;
	Outcome o;
	CHECK_FALSE(session.TryCreate(ctx, o));
	CHECK(o.messages.at(0) == Msg::kZeroCost);
}

TEST_CASE("Spellbook full and too-complex checks", "[menu]")
{
	Harness h;
	h.Add("mw.fire_damage");
	h.session.SetName("X");
	auto ctx = h.Rich();
	ctx.freePrimarySlots = 0;
	Outcome o;
	CHECK_FALSE(h.session.TryCreate(ctx, o));
	CHECK(o.messages.at(0) == Msg::kSpellbookFull);

	// A mixed-range spell needs a sub-spell slot.
	h.session.AddEffect("mw.fortify_health");
	h.session.EditorCycleRange();  // Touch -> Target
	h.session.EditorCycleRange();  // -> Self
	h.session.EditorOk();
	h.session.EditEffect(0);
	h.session.EditorCycleRange();  // fire: Touch -> Target
	h.session.EditorOk();
	ctx = h.Rich();
	ctx.freeSubSlots = 0;
	Outcome o2;
	CHECK_FALSE(h.session.TryCreate(ctx, o2));
	CHECK(o2.messages.at(0) == Msg::kSpellbookFull);
}

TEST_CASE("A name matching a known spell is a notice, not a block", "[menu]")
{
	Harness h;
	h.Add("mw.fire_damage");
	h.session.SetName("Flames");
	auto ctx = h.Rich();
	ctx.nameExists = [](std::string_view a_name) { return a_name == "Flames"; };
	Outcome o;
	auto    purchase = h.session.TryCreate(ctx, o);
	REQUIRE(purchase);
	CHECK(o.ok());
	CHECK(o.messages.at(0) == Msg::kNameExists);
}

TEST_CASE("Names are capped at 40 characters", "[menu]")
{
	Harness h;
	h.session.SetName(std::string(60, 'a'));
	CHECK(h.session.Name().size() == 40);
	h.session.SetName("\xC3\xA9\xC3\xA9");  // two code points, four bytes
	CHECK(h.session.Name() == "\xC3\xA9\xC3\xA9");
}

TEST_CASE("Altar fees and free service", "[menu][price]")
{
	Settings s;
	Provider altar;
	altar.kind = Provider::Kind::kAltar;
	Harness full(s, altar);
	full.Add("mw.fire_damage");
	const auto base = full.session.PreviewCost().price;
	CHECK(full.session.PreviewPrice() == base);

	s.altarFee = AltarFee::kHalf;
	Harness half(s, altar);
	half.Add("mw.fire_damage");
	CHECK(half.session.PreviewPrice() == std::max<std::uint32_t>(1, base / 2));

	s.altarFee = AltarFee::kFilledSoulGem;
	Harness gem(s, altar);
	gem.Add("mw.fire_damage");
	gem.session.SetName("Gem");
	CHECK(gem.session.PreviewPrice() == 0);
	auto ctx = gem.Rich();
	Outcome o;
	CHECK_FALSE(gem.session.TryCreate(ctx, o));
	CHECK(o.messages.at(0) == Msg::kNeedSoulGem);
	ctx.hasFilledSoulGem = true;
	Outcome o2;
	auto p = gem.session.TryCreate(ctx, o2);
	REQUIRE(p);
	CHECK(p->consumeSoulGem);

	altar.freeService = true;  // the Arch-Mage at the College altar
	Harness arch(Settings{}, altar);
	arch.Add("mw.fire_damage");
	CHECK(arch.session.PreviewPrice() == 0);
}

TEST_CASE("Haggling applies only when enabled", "[menu][price]")
{
	Settings s;
	Provider npc;
	npc.kind = Provider::Kind::kSpellmaker;
	Harness h(s, npc);
	h.Add("mw.fire_damage");
	auto ctx = h.Rich();
	ctx.haggle = [](std::uint32_t a_price) { return a_price / 2; };
	const auto base = h.session.PreviewPrice(&ctx);
	CHECK(base == h.session.PreviewCost().price);

	s.haggling = true;
	Harness hag(s, npc);
	hag.Add("mw.fire_damage");
	CHECK(hag.session.PreviewPrice(&ctx) == base / 2);
}

TEST_CASE("Load copies a spell; difference pricing is a setting", "[menu][spellbook]")
{
	Harness h;
	SpellDef def;
	def.slot = 7;
	def.name = "Old Spell";
	def.effects = { E("mw.fire_damage", Range::kTarget, 10, 20) };
	def.pricePaid = 50;
	h.session.Load(def);
	CHECK(h.session.Effects().size() == 1);
	CHECK(h.session.Name() == "Old Spell");
	CHECK(h.session.LoadedFromSlot() == 7);
	CHECK(h.session.PreviewPrice() == h.session.PreviewCost().price);  // full price by default

	Settings s;
	s.loadPricing = 1;
	Harness diff(s);
	diff.session.Load(def);
	const auto full = diff.session.PreviewCost().price;
	CHECK(diff.session.PreviewPrice() == (full > 50 ? full - 50 : 0));
}

TEST_CASE("Replacing the original keeps its slot", "[menu][spellbook]")
{
	Harness h;
	SpellDef def;
	def.slot = 3;
	def.name = "Mine";
	def.effects = { E("mw.fire_damage", Range::kTarget, 10, 20) };
	h.session.Load(def);
	auto ctx = h.Rich();
	ctx.freePrimarySlots = 0;  // replacing needs no new slot
	ctx.replacing = &def;
	Outcome o;
	auto    purchase = h.session.TryCreate(ctx, o);
	REQUIRE(purchase);
	CHECK(purchase->def.slot == 3);
	CHECK((purchase->def.flags & SpellFlags::kReplaced) != 0);
}

TEST_CASE("Only known effects can be added", "[menu][parity]")
{
	Catalog     c = MakeCatalog();
	Settings    s;
	MenuSession session(c, s, {}, { "mw.fire_damage" });
	CHECK_FALSE(session.AddEffect("mw.frost_damage").changed);
	CHECK(session.AddEffect("mw.fire_damage").changed);
}

TEST_CASE("The live base-cost override feeds the readout and the purchase", "[menu][price]")
{
	Harness h;
	h.Add("mw.fire_damage");
	h.session.SetName("Hot");
	const auto before = h.session.PreviewCost().cost;
	h.session.SetBaseCostOverride([](const EffectDef& a_def) -> std::optional<double> {
		return a_def.id == "mw.fire_damage" ? std::optional<double>(a_def.skBaseCost * 2) : std::nullopt;
	});
	const auto after = h.session.PreviewCost().cost;
	CHECK(after > before);
	Outcome o;
	auto    p = h.session.TryCreate(h.Rich(), o);
	REQUIRE(p);
	CHECK(p->def.cost == after);
}

TEST_CASE("Editor cancel also closes the attribute picker", "[menu]")
{
	Harness h;
	h.session.AddEffect("mw.fortify_attribute");
	REQUIRE(h.session.Picker());
	CHECK(h.session.EditorCancel().changed);
	CHECK_FALSE(h.session.Picker());
}
