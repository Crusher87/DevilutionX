/**
 * @file poe2/dodge_roll.hpp
 *
 * POE2 Mashup dodge roll: a quick dash a few tiles away that monsters and missiles can't hit.
 */
#pragma once

namespace devilution {

struct Player;

/** @brief Number of tiles a dodge roll covers when nothing is in the way. */
constexpr int DodgeRollTiles = 3;

/**
 * @brief Starts a dodge roll towards the held WASD direction, or towards the cursor.
 * @return true if the roll started.
 */
bool TryDodgeRoll(Player &player);

/** @brief Animation frames skipped per tile while rolling, so the roll moves quickly. */
constexpr int DodgeRollSkippedWalkFrames = 4;

} // namespace devilution
