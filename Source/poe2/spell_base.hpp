/**
 * @file poe2/spell_base.hpp
 *
 * POE2 Mashup: the base damage of spell-like skills (Ice Bolt, table spells), like Firebolt's: from Magic and the gem level.
 */
#pragma once

#include "engine/random.hpp"
#include "player.h"

namespace devilution {

/** @brief How many values the spell base roll can take. */
constexpr int SpellBaseSpread = 10;

inline int SpellBaseMin(const Player &player, int gemLevel)
{
	return (player._pMagic / 8) + gemLevel + 2;
}

inline int SpellBaseMax(const Player &player, int gemLevel)
{
	return SpellBaseMin(player, gemLevel) + SpellBaseSpread - 1;
}

/** @brief Rolls the spell base (one GenerateRnd call). */
inline int RollSpellBase(const Player &player, int gemLevel)
{
	return GenerateRnd(SpellBaseSpread) + SpellBaseMin(player, gemLevel);
}

} // namespace devilution
