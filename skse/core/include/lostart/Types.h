#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LA
{
	// Morrowind order: Self -> Touch -> Target. The numeric values are stored in the co-save.
	enum class Range : std::uint8_t
	{
		kSelf = 0,
		kTouch = 1,
		kTarget = 2
	};
	inline constexpr std::array kAllRanges{ Range::kSelf, Range::kTouch, Range::kTarget };

	using RangeMask = std::uint8_t;
	constexpr RangeMask Bit(Range a_range) { return static_cast<RangeMask>(1u << static_cast<unsigned>(a_range)); }

	// Skyrim schools. Values match the translation / SWF school index.
	enum class School : std::uint8_t
	{
		kAlteration = 0,
		kConjuration = 1,
		kDestruction = 2,
		kIllusion = 3,
		kRestoration = 4
	};
	inline constexpr std::size_t kSchoolCount = 5;

	enum class MwSchool : std::uint8_t
	{
		kAlteration,
		kConjuration,
		kDestruction,
		kIllusion,
		kMysticism,
		kRestoration
	};

	enum class CostModel : std::uint8_t
	{
		kSkyrimBalanced = 0,
		kClassic = 1,
		kEngineAutocalc = 2
	};

	enum class Unit : std::uint8_t
	{
		kNone,
		kPoints,
		kPercent,
		kFeet,
		kLevel
	};

	enum class Tier : std::uint8_t
	{
		kNative,
		kCustom,
		kStandIn
	};

	enum class EffectSet : std::uint8_t
	{
		kMorrowind,  // "mw"
		kExtended,   // "mwx"
		kSkyrim,     // "sk"
		kPassThrough // "pt" - unrecognized modded effects registered at runtime
	};

	enum class TargetKind : std::uint8_t
	{
		kNone,
		kAttribute,
		kSkill
	};

	enum class Rank : std::uint8_t
	{
		kNovice = 0,
		kApprentice = 1,
		kAdept = 2,
		kExpert = 3,
		kMaster = 4
	};

	inline constexpr std::int16_t kNoSub = -1;
	inline constexpr std::int16_t kMwSkillBase = 100;
	inline constexpr std::size_t kAttributeCount = 8;
	inline constexpr std::size_t kSkyrimSkillCount = 18;
	inline constexpr std::size_t kMwSkillCount = 27;

	struct SpellEffect
	{
		std::string   effectId;
		std::int16_t  sub{ kNoSub };  // attribute 0-7, Skyrim skill 0-17, Morrowind skill 100-126
		Range         range{ Range::kSelf };
		std::uint16_t minMag{ 1 };
		std::uint16_t maxMag{ 1 };
		std::uint16_t duration{ 1 };
		std::uint16_t area{ 0 };

		bool SameEffect(const SpellEffect& a_other) const { return effectId == a_other.effectId && sub == a_other.sub; }
		bool operator==(const SpellEffect&) const = default;
	};

	namespace SpellFlags
	{
		inline constexpr std::uint32_t kReplaced = 1u << 0;
		inline constexpr std::uint32_t kImported = 1u << 1;
		inline constexpr std::uint32_t kNeedsRebalance = 1u << 2;
	}

	inline constexpr std::uint16_t kNoSlot = 0xFFFF;
	inline constexpr std::size_t   kMaxNameLength = 40;

	struct SpellDef
	{
		std::uint16_t              slot{ kNoSlot };
		std::string                name;
		std::vector<SpellEffect>   effects;
		CostModel                  costModel{ CostModel::kSkyrimBalanced };
		std::uint32_t              cost{ 0 };
		std::uint32_t              pricePaid{ 0 };
		float                      created{ 0.0f };
		std::uint32_t              provider{ 0 };
		std::uint32_t              flags{ 0 };
		std::vector<std::uint16_t> subSlots;

		bool operator==(const SpellDef&) const = default;
	};

	std::string_view ToString(Range a_range);
	std::string_view ToString(School a_school);
	std::string_view ToString(Rank a_rank);
	std::optional<Range>    RangeFromString(std::string_view a_text);
	std::optional<School>   SchoolFromString(std::string_view a_text);
	std::optional<MwSchool> MwSchoolFromString(std::string_view a_text);
	std::optional<Rank>     RankFromString(std::string_view a_text);

	// Farthest-first order used by the compiler: Target, then Touch, then Self.
	constexpr int Reach(Range a_range) { return static_cast<int>(a_range); }
}
