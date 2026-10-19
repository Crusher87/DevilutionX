/**
 * @file poe2/hud_globes.hpp
 *
 * POE2 Mashup HUD: animated life and mana globes, the energy shield ring around the life globe,
 * the Spirit arc along the mana globe and liquid levels on belt flasks, all drawn in code on top of
 * Diablo's own panel art.
 */
#pragma once

#include <cstdint>

#include "engine/clx_sprite.hpp"
#include "engine/point.hpp"
#include "engine/surface.hpp"

namespace devilution {

/** @brief Size of the bulb art; rows above GlobePanelTopRow stick out above the main panel. */
constexpr int GlobeArtSize = 88;
constexpr int GlobeTopRow = 3;
constexpr int GlobePanelTopRow = 16;
constexpr int GlobeBottomRow = 85;
/** @brief Columns of the dome above the panel. */
constexpr int GlobeDomeLeft = 11;
constexpr int GlobeDomeRight = 73;
/** @brief How full a globe is when full, like Player::_pHPPer. */
constexpr int GlobeFull = 81;

constexpr int EnergyShieldSegments = 12;
constexpr int SpiritSegments = 10;

enum class GlobeLiquid : uint8_t {
	Life,
	Mana,
};

/** @brief Where the liquid sits in the bulb art, measured from the empty and full art. */
struct GlobeShape {
	float centerX = 0;
	float centerY = 0;
	float radiusX = 0;
	float radiusY = 0;
	int top = 0;
	int bottom = 0;
	bool valid = false;
};

/** @brief A ring of segments around a globe. Angles are in degrees on screen: 0 is right, -90 is up. */
struct GlobeArc {
	float startDegrees;
	float endDegrees;
	int segments;
	/** @brief First color of the 16 shade palette ramp lit segments use. */
	uint8_t rampBase;
	/** @brief Light segments from the end of the arc instead of the start. */
	bool fillFromEnd;
};

/** @brief The life globe's energy shield ring runs down its right side and fills from the bottom. */
constexpr GlobeArc EnergyShieldArc { -80.F, 80.F, EnergyShieldSegments, 176, true };
/** @brief The mana globe's Spirit arc runs from its left side over the top. */
constexpr GlobeArc SpiritArc { -205.F, -70.F, SpiritSegments, 192, false };

/** @brief Pixels that differ between the empty and the full art are liquid. */
GlobeShape MeasureGlobe(const Surface &emptyArt, const Surface &fullArt);

/** @brief Segments to light for current out of maximum; at least one while current is above zero. */
int LitArcSegments(int current, int maximum, int segments);

/** @brief The art row the liquid surface rests on at the given fill, before waves. */
float GlobeLiquidLevel(const GlobeShape &shape, int fill);

/**
 * @brief Draws part of a globe: liquid below the waving surface, the empty art above it.
 * @param origin Screen position of the art's top left corner.
 * @param fill How full the globe is, from 0 to GlobeFull.
 * @param dome Draws the rows above the panel (and skips their transparent pixels) instead of the rows inside it.
 * @param pulse Makes the liquid throb, for low life.
 */
void DrawGlobeLiquid(const Surface &out, Point origin, const Surface &emptyArt, const Surface &fullArt, const GlobeShape &shape,
    GlobeLiquid liquid, int fill, bool dome, uint32_t timeMs, bool pulse);

/**
 * @brief Draws the segments of an arc around a globe, only on screen rows from clipTop up to clipBottom.
 */
void DrawGlobeArc(const Surface &out, Point center, int radius, int thickness, const GlobeArc &arc, int litSegments,
    int clipTop, int clipBottom, uint32_t timeMs);

/** @brief Draws the life globe and its energy shield ring; dome selects the part above the main panel. */
void DrawPoe2LifeGlobe(const Surface &out, bool dome);
/** @brief Draws the mana globe and its Spirit arc; dome selects the part above the main panel. */
void DrawPoe2ManaGlobe(const Surface &out, bool dome);
/** @brief Energy shield and Spirit numbers, shown with the life/mana values or while hovering a globe. */
void DrawPoe2GlobeValues(const Surface &out);
/** @brief Whether the mouse is over the life or mana globe, which then shows its current / max value. */
bool IsHoveringLifeGlobe();
bool IsHoveringManaGlobe();

/**
 * @brief Darkens the empty top part of a flask in the belt and lights its liquid surface.
 * @param position Bottom left corner the sprite was drawn at.
 */
void DrawFlaskLiquidLevel(const Surface &out, Point position, ClxSprite sprite, int charges, int maxCharges);

} // namespace devilution
