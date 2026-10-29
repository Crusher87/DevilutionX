#pragma once

#include <cstdint>
#include <cstdlib>

#include "engine/point.hpp"
#include "engine/size.hpp"
#include "engine/surface.hpp"

namespace devilution {

/**
 * @brief Fill a rectangle with the given color.
 */
void FillRect(const Surface &out, int x, int y, int width, int height, uint8_t colorIndex);

/**
 * @brief Draw a horizontal line segment in the target buffer (left to right)
 * @param out Target buffer
 * @param from Start of the line segment
 * @param width
 * @param colorIndex Color index from current palette
 */
void DrawHorizontalLine(const Surface &out, Point from, int width, std::uint8_t colorIndex);

/** Same as DrawHorizontalLine but without bounds clipping. */
void UnsafeDrawHorizontalLine(const Surface &out, Point from, int width, std::uint8_t colorIndex);

/**
 * @brief Draw a vertical line segment in the target buffer (top to bottom)
 * @param out Target buffer
 * @param from Start of the line segment
 * @param height
 * @param colorIndex Color index from current palette
 */
void DrawVerticalLine(const Surface &out, Point from, int height, std::uint8_t colorIndex);

/** Same as DrawVerticalLine but without bounds clipping. */
void UnsafeDrawVerticalLine(const Surface &out, Point from, int height, std::uint8_t colorIndex);

void DrawHalfTransparentHorizontalLine(const Surface &out, Point from, int width, uint8_t colorIndex);
void DrawHalfTransparentVerticalLine(const Surface &out, Point from, int width, uint8_t colorIndex);

/**
 * Draws a half-transparent rectangle by palette blending with black.
 *
 * @brief Render a transparent black rectangle
 * @param out Target buffer
 * @param sx Screen coordinate
 * @param sy Screen coordinate
 * @param width Rectangle width
 * @param height Rectangle height
 */
void DrawHalfTransparentRectTo(const Surface &out, int sx, int sy, int width, int height);

void DrawHalfTransparentRectTo(const Surface &out, int sx, int sy, int width, int height, uint8_t color);

/**
 * Draws a half-transparent pixel
 *
 * @brief Render a transparent pixel
 * @param out Target buffer
 * @param position Screen coordinates
 * @param col Pixel color
 */
void SetHalfTransparentPixel(const Surface &out, Point position, uint8_t color);

/**
 * Draws a 2px inset border.
 *
 * @param out Target buffer
 * @param rect The rectangle that border pixels are rendered inside of.
 * @param color Border color.
 */
void UnsafeDrawBorder2px(const Surface &out, Rectangle rect, uint8_t color);

/**
 * @brief POE2 Mashup: how a UI shape covers the world under it. The effects go between the world and the UI, and the UI
 * is told apart from the world by its colours, so a UI pixel the same colour as the world under it let them through
 * (Curtis 2026-10-23: campfire embers showed through the inventory).
 */
enum class UiCover : uint8_t {
	None,
	/**
	 * @brief A see-through black box darkening the world: the effects show through it darkened as the world is (Curtis
	 * 2026-10-23: under the quest dialog, a quest marker showed only where the world under it was black).
	 */
	Shaded,
	Solid,
};

/**
 * @brief POE2 Mashup: a UI pixel laid over the world with real alpha rather than mixed into the 8-bit copy of the world
 * under the UI: the colour it was drawn in, to tell whether something has been drawn over it since, and its opacity
 * from 0 (none) to 255 (Curtis 2026-10-29: the Tab map's floor darkened and flickered while walking).
 */
struct UiTint {
	uint8_t color;
	uint8_t alpha;
};

/**
 * @brief POE2 Mashup: records where the UI is drawn on @p surface into @p cells, one per pixel; null stops it. With
 * @p tints, pixels can also be laid over the world with real alpha (TintUiPixel).
 */
void SetUiCoverTarget(SDL_Surface *surface, UiCover *cells, UiTint *tints = nullptr);

/** @brief POE2 Mashup: whether pixels drawn on @p out can be laid over the world with real alpha (TintUiPixel). */
bool CanTintUi(const Surface &out);

/**
 * @brief POE2 Mashup: draws @p color at @p position, to be laid over the world at @p alpha (0 to 255) where CanTintUi
 * says so, and solid elsewhere. Over a tint still showing, its colour takes the tint's place and the two opacities add up.
 */
void TintUiPixel(const Surface &out, Point position, uint8_t color, uint8_t alpha);

/** @brief POE2 Mashup: notes that the UI covers this area of @p out, when it's the surface being recorded. */
void CoverUi(const Surface &out, Rectangle area, UiCover cover = UiCover::Solid);

} // namespace devilution
