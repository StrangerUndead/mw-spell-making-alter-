#pragma once

#include "Core/State.h"

// Internal helpers of Core/SpellCompiler.cpp shared with the spellbook, persistence and the
// in-game tests. The public compiler API is Spellbook::Compile / CompileInto / Blank.
namespace LA::Compiler
{
	struct MagicSystemInfo
	{
		RE::MagicSystem::CastingType castingType;
		RE::MagicSystem::Delivery    delivery;
	};

	// Casting type and delivery a planned spell of this range compiles to: Self -> Self; Touch
	// and Target -> Aimed (touch spells fire LA_TouchProjectile through their MGEF variants).
	MagicSystemInfo Expected(Range a_range);

	// Engine cost of each definition effect (Effect::cost), in definition order.
	std::vector<float> EntryCosts(const SpellDef& a_def);

	// Effect recycling: called once per SKSE revert callback (a game load or new game), before any
	// slot is blanked. Never from anywhere else: the two-revert delay is what makes reuse safe.
	void        OnRevert();
	std::size_t RetiredEffectCount();
}
