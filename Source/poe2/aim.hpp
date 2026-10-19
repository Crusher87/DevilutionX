#pragma once

#include "engine/point.hpp"

namespace devilution {

struct Player;

/** @brief How far ahead an aimed projectile's target point is put: far enough for a precise angle. */
constexpr int AimTiles = 14;

/** @brief Records exactly where the mouse points in the dungeon, in tiles with fractions (from CheckCursMove). */
void SetCursorTileExact(float x, float y);
/** @brief Where the mouse points, in tiles with fractions; negative before the first mouse move. */
void GetCursorTileExact(float &x, float &y);

/**
 * @brief A tile far along the exact line from the hero to the mouse, so a projectile aimed at it flies
 * where the cursor points rather than along the nearest of eight directions.
 */
Point AimAlongCursor(const Player &player, int tiles);

/** @brief A tile @p tiles away along the line from @p from through @p target (any angle, not just eight). */
Point FarAlongLine(Point from, Point target, int tiles);

} // namespace devilution
