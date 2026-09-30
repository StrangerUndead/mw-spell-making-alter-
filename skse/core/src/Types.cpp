#include "lostart/Types.h"

#include "lostart/Util.h"

namespace LA
{
	std::string_view ToString(Range a_range)
	{
		switch (a_range) {
		case Range::kSelf:
			return "Self";
		case Range::kTouch:
			return "Touch";
		case Range::kTarget:
			return "Target";
		}
		return "Self";
	}

	std::string_view ToString(School a_school)
	{
		switch (a_school) {
		case School::kAlteration:
			return "Alteration";
		case School::kConjuration:
			return "Conjuration";
		case School::kDestruction:
			return "Destruction";
		case School::kIllusion:
			return "Illusion";
		case School::kRestoration:
			return "Restoration";
		}
		return "Alteration";
	}

	std::string_view ToString(Rank a_rank)
	{
		switch (a_rank) {
		case Rank::kNovice:
			return "Novice";
		case Rank::kApprentice:
			return "Apprentice";
		case Rank::kAdept:
			return "Adept";
		case Rank::kExpert:
			return "Expert";
		case Rank::kMaster:
			return "Master";
		}
		return "Novice";
	}

	std::optional<Range> RangeFromString(std::string_view a_text)
	{
		for (auto range : kAllRanges) {
			if (IEquals(a_text, ToString(range))) {
				return range;
			}
		}
		return std::nullopt;
	}

	std::optional<School> SchoolFromString(std::string_view a_text)
	{
		for (std::size_t i = 0; i < kSchoolCount; ++i) {
			const auto school = static_cast<School>(i);
			if (IEquals(a_text, ToString(school))) {
				return school;
			}
		}
		return std::nullopt;
	}

	std::optional<MwSchool> MwSchoolFromString(std::string_view a_text)
	{
		if (IEquals(a_text, "Mysticism")) {
			return MwSchool::kMysticism;
		}
		if (auto school = SchoolFromString(a_text)) {
			switch (*school) {
			case School::kAlteration:
				return MwSchool::kAlteration;
			case School::kConjuration:
				return MwSchool::kConjuration;
			case School::kDestruction:
				return MwSchool::kDestruction;
			case School::kIllusion:
				return MwSchool::kIllusion;
			case School::kRestoration:
				return MwSchool::kRestoration;
			}
		}
		return std::nullopt;
	}

	std::optional<Rank> RankFromString(std::string_view a_text)
	{
		for (int i = 0; i <= static_cast<int>(Rank::kMaster); ++i) {
			const auto rank = static_cast<Rank>(i);
			if (IEquals(a_text, ToString(rank))) {
				return rank;
			}
		}
		return std::nullopt;
	}
}
