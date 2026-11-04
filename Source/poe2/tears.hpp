/**
 * @file poe2/tears.hpp
 *
 * POE2 Mashup: the Fountain of Tears lets the hero move one attribute point
 * from a stat they pick to another stat they pick, instead of Diablo's random
 * swap. The fountain is used up only once a trade is made.
 */
#pragma once

#include "player.h"

namespace devilution {

struct Object;

/** The four attributes, in the order the window lists them. */
constexpr CharacterAttribute TearsAttributes[] = {
	CharacterAttribute::Strength,
	CharacterAttribute::Magic,
	CharacterAttribute::Dexterity,
	CharacterAttribute::Vitality,
};

/** A stat can give a point while it is above the hero's starting value. */
bool TearsCanGive(const Player &player, CharacterAttribute attribute);

/** A stat can take a point while it is below the hero's maximum. */
bool TearsCanTake(const Player &player, CharacterAttribute attribute);

/** Moves one point from @p from to @p to; false (and no change) when either side can't. */
bool TearsTrade(Player &player, CharacterAttribute from, CharacterAttribute to);

/** Opens the trade window for this fountain. */
void OpenTearsFountain(const Object &fountain);

/** Makes the trade picked in the window and uses up the fountain. */
bool FinishTearsFountain(CharacterAttribute from, CharacterAttribute to);

} // namespace devilution
