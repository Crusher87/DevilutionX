#include "poe2/aim.hpp"

#include <algorithm>
#include <cmath>

#include "cursor.h"
#include "levels/gendung.h"
#include "objects.h"
#include "player.h"
#include "poe2/telemetry.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

float CursorX = -1;
float CursorY = -1;

Point ClampToDungeon(Point tile)
{
	return { std::clamp(tile.x, 0, MAXDUNX - 1), std::clamp(tile.y, 0, MAXDUNY - 1) };
}

/** @brief Rounds to the nearest whole number (the Amiga toolchain has no std::lround). */
int RoundToInt(float value)
{
	return static_cast<int>(value < 0 ? value - 0.5F : value + 0.5F);
}

/** @brief 32 pixels straight down the screen is one tile further in both x and y. */
constexpr float ProjectileHeightTiles = 1.F;

Point Extend(Point from, float dx, float dy, int tiles)
{
	const float length = std::sqrt(dx * dx + dy * dy);
	if (length < 0.01F)
		return from;
	const float scale = static_cast<float>(tiles) / length;
	return ClampToDungeon({ from.x + RoundToInt(dx * scale), from.y + RoundToInt(dy * scale) });
}

} // namespace

void SetCursorTileExact(float x, float y)
{
	CursorX = x;
	CursorY = y;
}

void GetCursorTileExact(float &x, float &y)
{
	x = CursorX;
	y = CursorY;
}

Point AimAlongCursor(const Player &player, int tiles)
{
	const Point from = player.position.tile;
	if (CursorX < 0)
		return from + Displacement { player._pdir } * tiles;
	// A barrel or other object under the cursor is aimed at itself, not at the floor behind its picture.
	if (ObjectUnderCursor != nullptr && ObjectUnderCursor->position != from)
		return FarAlongLine(from, ObjectUnderCursor->position, tiles);
	// Projectiles fly half a tile above the floor (their pictures are drawn that high), so they aim at the floor half a
	// tile below the cursor: then the flying picture passes right under the cursor.
	const float dx = CursorX + ProjectileHeightTiles - static_cast<float>(from.x);
	const float dy = CursorY + ProjectileHeightTiles - static_cast<float>(from.y);
	if (dx * dx + dy * dy < 0.04F)
		return from + Displacement { player._pdir } * tiles;
	const Point aimed = Extend(from, dx, dy, tiles);
	TelemetryLog("aim", StrCat("hero ", from.x, ",", from.y, " exact ", static_cast<int>(CursorX * 100), ",", static_cast<int>(CursorY * 100), " aim ", aimed.x, ",", aimed.y));
	return aimed;
}

Point FarAlongLine(Point from, Point target, int tiles)
{
	return Extend(from, static_cast<float>(target.x - from.x), static_cast<float>(target.y - from.y), tiles);
}

} // namespace devilution
