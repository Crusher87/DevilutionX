/**
 * @file automap_render.hpp
 * Defines 2 sets of rendering primitives for drawing automap lines.
 *
 * 1. DrawMapLine* - used for rendering most map lines - 2 pixels horizontally for each pixel vertically.
 * 2. DrawMapLineSteep* - currently only used for rendering the player arrow - 2 pixels vertically for each pixel horizontally.
 *
 * These functions draw a single extra pixel at the end of the line -- they always draw an odd number of pixels.
 * These functions clip to the output buffer -- they are safe to call with out-of-bounds coordinates.
 */
#pragma once

#include <cstdint>

#include "engine/point.hpp"
#include "engine/surface.hpp"

namespace devilution {

void DrawMapLineNS(const Surface &out, Point from, int height, std::uint8_t colorIndex);
void DrawMapLineWE(const Surface &out, Point from, int height, std::uint8_t colorIndex);
/**
 * @brief Draw a line in the target buffer from the given point towards north east at an `atan(1/2)` angle.
 *
 * Draws 2 horizontal pixels for each vertical step, then an additional one where it draws 1 pixel.
 *
 * The end point is at `{ from.x + 2 * height + 1, from.y - height }`.
 */
void DrawMapLineNE(const Surface &out, Point from, int height, std::uint8_t colorIndex);

/**
 * @brief Draw a line in the target buffer from the given point towards south east at an `atan(1/2)` angle.
 *
 * Draws 2 horizontal pixels for each vertical step, then an additional one where it draws 1 pixel.
 *
 * The end point is at `{ from.x + 2 * height + 1, from.y + height }`.
 */
void DrawMapLineSE(const Surface &out, Point from, int height, std::uint8_t colorIndex);

/**
 * @brief Draw a line in the target buffer from the given point towards north west at an `atan(1/2)` angle.
 *
 * Draws 2 horizontal pixels for each vertical step, then an additional one where it draws 1 pixel.
 *
 * The end point is at `{ from.x - 2 * height + 1, from.y - height }`.
 */
void DrawMapLineNW(const Surface &out, Point from, int height, std::uint8_t colorIndex);

/**
 * @brief Draw a line in the target buffer from the given point towards south west at an `atan(1/2)` angle.
 *
 * Draws 2 horizontal pixels for each vertical step, then an additional one where it draws 1 pixel.
 *
 * The end point is at `{ from.x - 2 * height + 1, from.y + height }`.
 */
void DrawMapLineSW(const Surface &out, Point from, int height, std::uint8_t colorIndex);

/**
 * @brief Draw a line in the target buffer from the given point towards north east at an `atan(1/2)` angle.
 *
 * Draws 2 vertical pixels for each horizontal step, then an additional one where it draws 1 pixel.
 *
 * The end point is at `{ from.x + width + 1, from.y - 2 * width }`.
 */
void DrawMapLineSteepNE(const Surface &out, Point from, int width, std::uint8_t colorIndex);

/**
 * @brief Draw a line in the target buffer from the given point towards south east at an `atan(2)` angle.
 *
 * Draws 2 vertical pixels for each horizontal step, then an additional one where it draws 1 pixel.
 *
 * The end point is at `{ from.x + width + 1, from.y + 2 * width }`.
 */
void DrawMapLineSteepSE(const Surface &out, Point from, int width, std::uint8_t colorIndex);

/**
 * @brief Draw a line in the target buffer from the given point towards north west at an `atan(1/2)` angle.
 *
 * Draws 2 vertical pixels for each horizontal step, then an additional one where it draws 1 pixel.
 *
 * The end point is at `{ from.x - (width + 1), from.y - 2 * width }`.
 */
void DrawMapLineSteepNW(const Surface &out, Point from, int width, std::uint8_t colorIndex);

/**
 * @brief Draw a line in the target buffer from the given point towards south west at an `atan(1/2)` angle.
 *
 * Draws 2 vertical pixels for each horizontal step, then an additional one where it draws 1 pixel.
 *
 * The end point is at `{ from.x - (width + 1), from.y + 2 * width }`.
 */
void DrawMapLineSteepSW(const Surface &out, Point from, int width, std::uint8_t colorIndex);
void DrawMapFreeLine(const Surface &out, Point from, Point to, uint8_t colorIndex);

/**
 * @brief Draw an automap pixel.
 *
 * Draw either an opaque pixel or a transparent pixel, depending on the automap mode.
 */
void SetMapPixel(const Surface &out, Point position, uint8_t color);

/**
 * @brief POE2 Mashup: how opaque the map's lines are drawn, in percent (the Map Transparency option). 100, the
 * default, draws them solid as Diablo does; the Transparent map type keeps its own half transparency.
 */
void SetMapLineOpacity(int percent);
[[nodiscard]] int GetMapLineOpacity();

/**
 * @brief POE2 Mashup: whether the map is laid over the world with real alpha where the UI can be (CanTintUi), so its
 * see-through lines, markers and floor keep one steady colour whatever the world under them (Curtis 2026-10-29: the
 * Tab map darkened and flickered while walking). Off, they are blended into the 8-bit world under them.
 */
void SetMapTintsUi(bool tints);
[[nodiscard]] bool GetMapTintsUi();

/** @brief POE2 Mashup: a map pixel drawn at about the given opacity, laid over the world or blended into it (SetMapTintsUi). */
void PutMapPixel(const Surface &out, Point position, uint8_t color, int opacityPercent);

/**
 * @brief POE2 Mashup: a palette colour laid over another at about the given opacity, in quarter steps through the
 * palette's blending table.
 */
[[nodiscard]] uint8_t BlendMapColor(uint8_t color, uint8_t under, int opacityPercent);

} // namespace devilution
