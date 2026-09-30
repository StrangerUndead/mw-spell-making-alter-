#include "Fixtures.h"

#include "lostart/Serialization.h"

#include <catch2/catch_test_macros.hpp>

using namespace LA;
using namespace LA::Test;

namespace
{
	SpellDef Sample(std::uint16_t a_slot, std::size_t a_effects, std::size_t a_nameLen)
	{
		static const char* ids[] = { "mw.fire_damage", "mw.frost_damage", "mw.shock_damage", "mw.fortify_health", "mw.levitate",
			"mw.fortify_attribute", "mw.absorb_health", "mw.restore_health" };
		SpellDef def;
		def.slot = a_slot;
		def.name = std::string(a_nameLen, 'x');
		for (std::size_t i = 0; i < a_effects; ++i) {
			def.effects.push_back(E(ids[i % 8], static_cast<Range>(i % 3), static_cast<std::uint16_t>(5 + i), static_cast<std::uint16_t>(20 + i),
				static_cast<std::uint16_t>(30 + i), static_cast<std::uint16_t>(i), i == 5 ? std::int16_t{ 3 } : kNoSub));
		}
		def.costModel = CostModel::kClassic;
		def.cost = 123;
		def.pricePaid = 861;
		def.created = 12.5f;
		def.provider = 0xFE001234;
		def.flags = SpellFlags::kReplaced;
		def.subSlots = { 4, 9 };
		return def;
	}
}

TEST_CASE("Definitions round-trip", "[serial]")
{
	std::vector<SpellDef> defs{ Sample(0, 3, 12), Sample(499, 8, 40), Sample(17, 1, 1) };
	const auto            bytes = Serial::EncodeDefinitions(defs);
	std::vector<SpellDef> out;
	REQUIRE(Serial::DecodeDefinitions(bytes, kSchemaVersion, out));
	CHECK(out == defs);
}

TEST_CASE("Definitions resolve provider form ids", "[serial]")
{
	std::vector<SpellDef> defs{ Sample(0, 1, 5) };
	const auto            bytes = Serial::EncodeDefinitions(defs);
	std::vector<SpellDef> out;
	REQUIRE(Serial::DecodeDefinitions(bytes, kSchemaVersion, out, [](std::uint32_t a_id) -> std::optional<std::uint32_t> {
		return a_id == 0xFE001234 ? std::optional<std::uint32_t>(0xFE101234) : std::nullopt;
	}));
	CHECK(out[0].provider == 0xFE101234);
	REQUIRE(Serial::DecodeDefinitions(bytes, kSchemaVersion, out, [](std::uint32_t) { return std::optional<std::uint32_t>{}; }));
	CHECK(out[0].provider == 0);
}

TEST_CASE("Truncated or future data is rejected without crashing", "[serial]")
{
	std::vector<SpellDef> defs{ Sample(0, 8, 40), Sample(1, 8, 40) };
	auto                  bytes = Serial::EncodeDefinitions(defs);
	std::vector<SpellDef> out{ Sample(9, 1, 1) };
	for (std::size_t cut = 0; cut < bytes.size(); cut += 7) {
		std::vector<std::uint8_t> partial(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut));
		std::vector<SpellDef>     tmp;
		CHECK_FALSE(Serial::DecodeDefinitions(partial, kSchemaVersion, tmp));
	}
	CHECK_FALSE(Serial::DecodeDefinitions(bytes, kSchemaVersion + 1, out));
	CHECK_FALSE(Serial::DecodeDefinitions(bytes, 0, out));
	CHECK(out.size() == 1);  // untouched on failure
}

TEST_CASE("Co-save stays under 64 KB with 500 typical spells (performance budget)", "[serial][budget]")
{
	std::vector<SpellDef> defs;
	for (std::uint16_t i = 0; i < 500; ++i) {
		defs.push_back(Sample(i, 4, 20));
	}
	const auto bytes = Serial::EncodeDefinitions(defs);
	INFO("bytes: " << bytes.size());
	CHECK(bytes.size() < 64 * 1024);
}

TEST_CASE("Marks, ledger, condition pools and version round-trip", "[serial]")
{
	std::vector<Mark> marks{ { 0, 0x0000003C, { 1.f, 2.f, 3.f }, 0.5f }, { 0x000165A7, 0, { -4.f, 5.f, 6.f }, 1.f } };
	std::vector<Mark> marksOut;
	REQUIRE(Serial::DecodeMarks(Serial::EncodeMarks(marks), kSchemaVersion, marksOut));
	CHECK(marksOut == marks);
	// A mark whose cell's plugin is gone is dropped.
	REQUIRE(Serial::DecodeMarks(Serial::EncodeMarks(marks), kSchemaVersion, marksOut, [](std::uint32_t a_id) -> std::optional<std::uint32_t> {
		return a_id == 0x3C ? std::optional<std::uint32_t>(a_id) : std::nullopt;
	}));
	CHECK(marksOut.size() == 1);

	std::vector<LedgerEntry> ledger{ { 0x14, { 1, 0, 0, 5, 0, 0, 0, 2 } }, { 0xFF000801, {} } };
	std::vector<LedgerEntry> ledgerOut;
	REQUIRE(Serial::DecodeLedger(Serial::EncodeLedger(ledger), kSchemaVersion, ledgerOut));
	CHECK(ledgerOut == ledger);

	std::vector<ConditionEntry> pools{ { 0x14, 0x00012EB7, 42.5f } };
	std::vector<ConditionEntry> poolsOut;
	REQUIRE(Serial::DecodeConditions(Serial::EncodeConditions(pools), kSchemaVersion, poolsOut));
	CHECK(poolsOut == pools);

	VersionInfo info;
	info.history = { { 1, 3.25f } };
	VersionInfo infoOut;
	REQUIRE(Serial::DecodeVersion(Serial::EncodeVersion(info), kSchemaVersion, infoOut));
	CHECK(infoOut == info);
}

TEST_CASE("FourCC codes", "[serial]")
{
	CHECK(kCosaveId == 0x4C415254);  // 'LART'
	CHECK(kRecordDefinitions == 0x4C414446);
}
