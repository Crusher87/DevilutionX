#pragma once

#include <array>
#include <cstdint>

#include "engine/lighting_defs.hpp"

namespace devilution {

namespace hd {

/**
 * @brief POE2 Mashup: the HD renderer's own light (the HD Lighting option).
 *
 * The game's light stays as it is for play: this only decides how the HD world is lit. Each light shines in its
 * own colour (torches orange, a frost bolt icy blue) and walls stop it, so a torch no longer lights the room
 * behind its wall. It is worked out on a grid of two samples per tile each way, over the part of the world on
 * screen, with the game's own falloff for each light.
 */
namespace light {

struct Rgb {
	float r = 0;
	float g = 0;
	float b = 0;
};

/** @brief The HD look the light is drawn in, from the renderer. */
struct Look {
	/** @brief How bright each of the game's 16 light levels leaves the palette, 0 to 1. */
	std::array<float, NumLightingLevels> levelBrightness {};
	/** @brief Light falls off faster than the game's own: the brightness is raised to this power. */
	float falloff = 1;
	/** @brief Bright light leans warm, shadow leans cool (and the hero's own light is the warm colour). */
	std::array<float, 3> warm {};
	std::array<float, 3> cool {};
	/** @brief The screen's fade and brightness, per channel. */
	std::array<float, 3> tint {};
};

/** @brief A light shining on a hero or monster, for the shadow it casts. */
struct ShadowLight {
	/** @brief Where the light is, in tiles. */
	float x = 0;
	float y = 0;
	/** @brief How bright it is where the hero or monster stands, 0 to 1. */
	float strength = 0;
	/** @brief How high it hangs, in tiles: the lower, the longer the shadows it casts. */
	float height = 1;
};

/**
 * @brief Works out this frame's light over the world rectangle (in tiles) on screen.
 * @param heroX, heroY Where the hero is drawn, in tiles, so their light follows them smoothly between game ticks
 * (NaN when the hero isn't drawn).
 * @return False where the HD light isn't used (town), and the renderer lights the world as before.
 */
bool Update(const Look &look, float x0, float y0, float x1, float y1, float heroX, float heroY);

/** @brief The light over the rectangle as a picture: one ARGB pixel per sample, row by row. */
int FieldWidth();
int FieldHeight();
const uint32_t *FieldPixels();

/** @brief The world rectangle (in tiles) the picture covers, edge to edge. */
void FieldRect(float &x0, float &y0, float &x1, float &y1);

/** @brief The light at a world point (in tiles), 0 to 1 per channel. */
Rgb At(float x, float y);

/** @brief The brightest light there is (today's fully lit shade), for things that shine by themselves. */
Rgb Full();

/** @brief Light made some of the game's light levels brighter, no brighter than Full (other heroes are drawn 5 levels up). */
Rgb Boost(const Rgb &light, float levels);

/** @brief This tile stops light (walls, pillars, closed doors). */
bool Blocks(int x, int y);

/**
 * @brief The strongest lights shining on a world point that can see it, strongest first.
 * @param total Set to all the light shining there, theirs and the rest.
 * @param overhead Set to the light of lamps right at the point (a hero's own light), which cast no shadow sideways.
 * @return How many were written to out.
 */
int ShadowLights(float x, float y, ShadowLight *out, int maxCount, float &total, float &overhead);

/**
 * @brief The level piece to draw at a tile (dPiece coordinates) under this light: the level's own, except where the
 * level painted one of Diablo's fixed shadows into the floor or a wall (beside pillars, arches and wall ends, all
 * falling the same way). There the plain floor or wall is drawn, since this light casts its own shadows, each away
 * from its light.
 */
uint16_t UnpaintedPiece(int x, int y, uint16_t piece);

/** @brief Forgets the level's lights, e.g. when the renderer's caches are cleared. */
void Reset();

} // namespace light

} // namespace hd

} // namespace devilution
