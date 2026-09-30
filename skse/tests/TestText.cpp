#include "Fixtures.h"

#include "lostart/Text.h"
#include "lostart/Util.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

using namespace LA;
using namespace LA::Test;

TEST_CASE("Effect lines use Morrowind's format (parity 14)", "[text][parity]")
{
	const auto      catalog = MakeCatalog();
	const auto      strings = EnglishStrings();
	EffectFormatter fmt(catalog, strings);
	CHECK(fmt.Line(E("mw.fire_damage", Range::kTarget, 5, 10, 3, 10)) == "Fire Damage 5 to 10 pts for 3 secs in 10 ft on Target");
	CHECK(fmt.Line(E("mw.fire_damage", Range::kTouch, 10, 10)) == "Fire Damage 10 pts on Touch");
	CHECK(fmt.Line(E("mw.fire_damage", Range::kTouch, 1, 1)) == "Fire Damage 1 pt on Touch");
	CHECK(fmt.Line(E("mw.fortify_health", Range::kSelf, 20, 20, 30)) == "Fortify Health 20 pts for 30 secs on Self");
	CHECK(fmt.Line(E("mw.levitate", Range::kSelf, 10, 10, 30)) == "Levitate 10 pts for 30 secs on Self");
	CHECK(fmt.Line(E("mw.mark", Range::kSelf, 1, 1)) == "Mark on Self");
	CHECK(fmt.Line(E("mw.blind", Range::kTarget, 5, 10, 5)) == "Blind 5 to 10% for 5 secs on Target");
	CHECK(fmt.Line(E("mw.open", Range::kTouch, 25, 25)) == "Open 25 levels on Touch");
	// Area is never printed on Self.
	CHECK(fmt.Line(E("mw.fortify_health", Range::kSelf, 5, 5, 10, 20)) == "Fortify Health 5 pts for 10 secs on Self");
}

TEST_CASE("Attribute and skill effects name their target", "[text]")
{
	const auto      catalog = MakeCatalog();
	const auto      strings = EnglishStrings();
	EffectFormatter fmt(catalog, strings);
	CHECK(fmt.Line(E("mw.fortify_attribute", Range::kSelf, 10, 10, 60, 0, 0)) == "Fortify Strength 10 pts for 60 secs on Self");
	// Without a $LA_EffectFmt_ key the family word is replaced.
	CHECK(fmt.EffectName(*catalog.Find("mw.fortify_skill"), 0) == "Fortify One-Handed");
}

TEST_CASE("String tables load UTF-16 LE with BOM", "[text]")
{
	const auto path = std::filesystem::temp_directory_path() / "la_strings_test.txt";
	const auto bytes = Utf8ToUtf16Le("$LA_A\tAlpha\r\n$LA_B\tB\xC3\xA9ta \xF0\x9F\x94\xA5\r\nnot a key\n");
	{
		std::ofstream out(path, std::ios::binary);
		out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}
	StringTable t;
	REQUIRE(t.LoadFile(path));
	CHECK(t.Size() == 2);
	CHECK(t.Get("$LA_A") == "Alpha");
	CHECK(t.Get("$LA_B") == "B\xC3\xA9ta \xF0\x9F\x94\xA5");
	CHECK(t.Get("$LA_Missing") == "$LA_Missing");
}

TEST_CASE("UTF-8 helpers", "[text]")
{
	CHECK(Utf8Length("a\xC3\xA9\xF0\x9F\x94\xA5") == 3);
	CHECK(Utf8Truncate("a\xC3\xA9\xF0\x9F\x94\xA5", 2) == "a\xC3\xA9");
	CHECK(Utf16LeToUtf8(Utf8ToUtf16Le("x\xE2\x82\xAC")) == "x\xE2\x82\xAC");
	CHECK(FormatPattern("Fortify {0} by {1}", { "Luck", "5" }) == "Fortify Luck by 5");
}

TEST_CASE("Form references parse", "[text]")
{
	const auto ref = ParseFormRef("Skyrim.esm|0x012FD0");
	CHECK(ref.plugin == "Skyrim.esm");
	CHECK(ref.localId == 0x012FD0);
	CHECK(ref.ToString() == "Skyrim.esm|0x012FD0");
	CHECK(ParseFormRef("Dragonborn.esm|FE1234").localId == 0xFE1234);
	CHECK_FALSE(ParseFormRef("nonsense").Valid());
	CHECK_FALSE(ParseFormRef("Skyrim.esm|0xZZ").Valid());
}
