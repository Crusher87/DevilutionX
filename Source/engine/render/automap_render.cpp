/**
 * @file automap_render.cpp
 *
 * Line drawing routines for the automap.
 */
#include "engine/render/automap_render.hpp"

#include <algorithm>
#include <cstdint>

#include "automap.h"
#include "engine/render/primitive_render.hpp"
#include "utils/palette_blending.hpp"

namespace devilution {
namespace {

int MapLineOpacity = 100;

enum class DirectionX : int8_t {
	EAST = 1,
	WEST = -1,
};

enum class DirectionY : int8_t {
	SOUTH = 1,
	NORTH = -1,
};

template <DirectionX DirX, DirectionY DirY>
void DrawMapLine(const Surface &out, Point from, int height, std::uint8_t colorIndex)
{
	while (height-- > 0) {
		SetMapPixel(out, { from.x, from.y + 1 }, 0);
		SetMapPixel(out, from, colorIndex);
		from.x += static_cast<int>(DirX);
		SetMapPixel(out, { from.x, from.y + 1 }, 0);
		SetMapPixel(out, from, colorIndex);
		from.x += static_cast<int>(DirX);
		from.y += static_cast<int>(DirY);
	}
	SetMapPixel(out, { from.x, from.y + 1 }, 0);
	SetMapPixel(out, from, colorIndex);
}

template <DirectionX DirX, DirectionY DirY>
void DrawMapLineSteep(const Surface &out, Point from, int width, std::uint8_t colorIndex)
{
	while (width-- > 0) {
		SetMapPixel(out, { from.x, from.y + 1 }, 0);
		SetMapPixel(out, from, colorIndex);
		from.y += static_cast<int>(DirY);
		SetMapPixel(out, { from.x, from.y + 1 }, 0);
		SetMapPixel(out, from, colorIndex);
		from.y += static_cast<int>(DirY);
		from.x += static_cast<int>(DirX);
	}
	SetMapPixel(out, { from.x, from.y + 1 }, 0);
	SetMapPixel(out, from, colorIndex);
}

} // namespace

void DrawMapLineNS(const Surface &out, Point from, int height, std::uint8_t colorIndex)
{
	if (from.x < 0 || from.x >= out.w() || from.y >= out.h() || height <= 0 || from.y + height <= 0)
		return;

	if (from.y < 0) {
		height += from.y;
		from.y = 0;
	}

	if (from.y + height > out.h())
		height = out.h() - from.y;

	for (int i = 0; i < height; ++i) {
		SetMapPixel(out, { from.x, from.y + i }, colorIndex);
	}
}

void DrawMapLineWE(const Surface &out, Point from, int width, std::uint8_t colorIndex)
{
	if (from.y < 0 || from.y >= out.h() || from.x >= out.w() || width <= 0 || from.x + width <= 0)
		return;

	if (from.x < 0) {
		width += from.x;
		from.x = 0;
	}

	if (from.x + width > out.w())
		width = out.w() - from.x;

	for (int i = 0; i < width; ++i) {
		SetMapPixel(out, { from.x + i, from.y }, colorIndex);
	}
}

void DrawMapLineNE(const Surface &out, Point from, int height, std::uint8_t colorIndex)
{
	DrawMapLine<DirectionX::EAST, DirectionY::NORTH>(out, from, height, colorIndex);
}

void DrawMapLineSE(const Surface &out, Point from, int height, std::uint8_t colorIndex)
{
	DrawMapLine<DirectionX::EAST, DirectionY::SOUTH>(out, from, height, colorIndex);
}

void DrawMapLineNW(const Surface &out, Point from, int height, std::uint8_t colorIndex)
{
	DrawMapLine<DirectionX::WEST, DirectionY::NORTH>(out, from, height, colorIndex);
}

void DrawMapLineSW(const Surface &out, Point from, int height, std::uint8_t colorIndex)
{
	DrawMapLine<DirectionX::WEST, DirectionY::SOUTH>(out, from, height, colorIndex);
}

void DrawMapLineSteepNE(const Surface &out, Point from, int width, std::uint8_t colorIndex)
{
	DrawMapLineSteep<DirectionX::EAST, DirectionY::NORTH>(out, from, width, colorIndex);
}

void DrawMapLineSteepSE(const Surface &out, Point from, int width, std::uint8_t colorIndex)
{
	DrawMapLineSteep<DirectionX::EAST, DirectionY::SOUTH>(out, from, width, colorIndex);
}

void DrawMapLineSteepNW(const Surface &out, Point from, int width, std::uint8_t colorIndex)
{
	DrawMapLineSteep<DirectionX::WEST, DirectionY::NORTH>(out, from, width, colorIndex);
}

void DrawMapLineSteepSW(const Surface &out, Point from, int width, std::uint8_t colorIndex)
{
	DrawMapLineSteep<DirectionX::WEST, DirectionY::SOUTH>(out, from, width, colorIndex);
}

/**
 * @brief Draws a line from first point to second point, unrestricted to the standard automap angles. Doesn't include shadow.
 */
void DrawMapFreeLine(const Surface &out, Point from, Point to, uint8_t colorIndex)
{
	const int dx = std::abs(to.x - from.x);
	const int dy = std::abs(to.y - from.y);
	const int sx = from.x < to.x ? 1 : -1;
	const int sy = from.y < to.y ? 1 : -1;
	int err = dx - dy;

	while (true) {
		SetMapPixel(out, from, colorIndex);

		if (from.x == to.x && from.y == to.y) {
			break;
		}

		const int e2 = 2 * err;
		if (e2 > -dy) {
			err -= dy;
			from.x += sx;
		}
		if (e2 < dx) {
			err += dx;
			from.y += sy;
		}
	}
}

void SetMapPixel(const Surface &out, Point position, uint8_t color)
{
	if (GetAutomapType() == AutomapType::Minimap && !MinimapRect.contains(position))
		return;

	if (GetAutomapType() == AutomapType::Transparent) {
		SetHalfTransparentPixel(out, position, color);
	} else if (MapLineOpacity < 100) {
		if (out.InBounds(position)) {
			uint8_t &pixel = *out.at(position.x, position.y);
			pixel = BlendMapColor(color, pixel, MapLineOpacity);
		}
	} else {
		out.SetPixel(position, color);
	}
}

void SetMapLineOpacity(int percent)
{
	MapLineOpacity = std::clamp(percent, 0, 100);
}

int GetMapLineOpacity()
{
	return MapLineOpacity;
}

uint8_t BlendMapColor(uint8_t color, uint8_t under, int opacityPercent)
{
	if (opacityPercent >= 88)
		return color;
	const uint8_t half = paletteTransparencyLookup[color][under];
	if (opacityPercent >= 63)
		return paletteTransparencyLookup[color][half];
	if (opacityPercent >= 38)
		return half;
	if (opacityPercent >= 13)
		return paletteTransparencyLookup[under][half];
	return under;
}

} // namespace devilution
