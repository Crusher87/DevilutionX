#include "poe2/aim.hpp"

#include <algorithm>
#include <cmath>

#include "levels/gendung.h"
#include "player.h"

namespace devilution {

namespace {

float CursorX = -1;
float CursorY = -1;

Point ClampToDungeon(Point tile)
{
	return { std::clamp(tile.x, 0, MAXDUNX - 1), std::clamp(tile.y, 0, MAXDUNY - 1) };
}

Point Extend(Point from, float dx, float dy, int tiles)
{
	const float length = std::sqrt(dx * dx + dy * dy);
	if (length < 0.01F)
		return from;
	const float scale = static_cast<float>(tiles) / length;
	return ClampToDungeon({ from.x + static_cast<int>(std::lround(dx * scale)), from.y + static_cast<int>(std::lround(dy * scale)) });
}

} // namespace

void SetCursorTileExact(float x, float y)
{
	CursorX = x;
	CursorY = y;
}

Point AimAlongCursor(const Player &player, int tiles)
{
	const Point from = player.position.tile;
	if (CursorX < 0)
		return from + Displacement { player._pdir } * tiles;
	const float dx = CursorX - static_cast<float>(from.x);
	const float dy = CursorY - static_cast<float>(from.y);
	if (dx * dx + dy * dy < 0.04F)
		return from + Displacement { player._pdir } * tiles;
	return Extend(from, dx, dy, tiles);
}

Point FarAlongLine(Point from, Point target, int tiles)
{
	return Extend(from, static_cast<float>(target.x - from.x), static_cast<float>(target.y - from.y), tiles);
}

} // namespace devilution
