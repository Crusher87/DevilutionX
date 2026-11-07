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
 * screen, with the game's own falloff for each light. Lights add up as real light does, a wide flame or glow gives
 * shadows soft edges, and a little of each light still reaches round a pillar or corner by bouncing off the room.
 * Fires and spells shine brighter than the hero's own glow, so beside them things are lit brighter than full.
 * In town it is Tristram's evening: the sky's light everywhere, and firelight from the forge, the tavern and the lamps and windows of the houses.
 */
namespace light {

/** @brief The most a strong light (a fire or a spell) lights things: this much brighter than full. */
constexpr float MaxLight = 1.6F;

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
	/** @brief Where its light starts (for a torch on a wall, just in front of the wall), in tiles. */
	float x = 0;
	float y = 0;
	/** @brief How bright it is where the hero or monster stands, as linear light to the power 1/2.2 (1 is full). */
	float strength = 0;
	/** @brief How high it hangs, in tiles (a hero is about one and a half tiles tall): the lower, the longer the shadows. */
	float height = 1;
	/** @brief How wide its flame or glow is, in tiles: the wider, the softer the edges of its shadows. */
	float size = 0;
	/** @brief How much of its light still reaches its shadows after bouncing off the floor and walls round them, 0 to 1. */
	float fill = 0;
	/** @brief How far from it its light reaches, in tiles. */
	float reach = 0;
	/** @brief Which light it is, for LinearOf (LavaLight for all the lava the point sees, as one light). */
	int source = -1;
	/** @brief For the lava's light: how bright it is against one lava tile at its middle, and how far its light falls off. */
	float power = 0;
	int radius = 0;
	/** @brief It is a hero's own glow (see GlowAt). */
	bool glow = false;
};

/** @brief ShadowLight::source for the light of all the lava a point sees, shining as one wide light lying on the floor. */
constexpr int LavaLight = -2;

/** @brief ShadowLight::source for the evening sky over Tristram: a soft light from far off, the same everywhere in town. */
constexpr int SkyLight = -3;

/** @brief Light adds up as linear light: brightness as drawn (0 to 1) to linear light and back (a 2.2 gamma, as sRGB). */
float ToLinear(float brightness);
float ToBrightness(float linear);

/** @brief Linear light as drawn: its brightness, easing off above full towards MaxLight. */
float Drawn(float linear);

/**
 * @brief Works out this frame's light over the world rectangle (in tiles) on screen.
 * @param heroX, heroY Where the hero is drawn, in tiles, so their light follows them smoothly between game ticks
 * (NaN when the hero isn't drawn).
 * @return False where there is no level to light, and the renderer lights the world as before.
 */
bool Update(const Look &look, float x0, float y0, float x1, float y1, float heroX, float heroY);

/** @brief The light over the rectangle as a picture: one ARGB pixel per sample, row by row, up to full. */
int FieldWidth();
int FieldHeight();
const uint32_t *FieldPixels();

/**
 * @brief The whole light, above full too, as a picture to multiply the floor by in one go (SDL_BLENDMODE_MUL keeps
 * colour x (rgb + 1 - alpha)): each channel's light is its value plus one less the alpha. A very coloured light above
 * full (one channel more than full above another) comes out a little paler.
 */
const uint32_t *FieldMulPixels();

/** @brief The world rectangle (in tiles) the picture covers, edge to edge. */
void FieldRect(float &x0, float &y0, float &x1, float &y1);

/** @brief The light at a world point (in tiles), per channel: 1 is full, and beside a strong light up to MaxLight. */
Rgb At(float x, float y);

/**
 * @brief The light on the ground outside the rooms at a world point (in tiles): as At, but none of the light the rock
 * inside and behind the walls takes from the room in front of them for the walls' faces. No light reaches the ground
 * behind a wall.
 */
Rgb GroundAt(float x, float y);

/**
 * @brief The light of the room behind a wall at a world point (in tiles) just behind the wall's face: as At, but where
 * the point is rock lit for the walls by the room in front of them, the light of the first floor (or ground outside the
 * rooms) further back, up to a tile behind it. The face looks along the x axis (towards +x) or, if `alongY`, along the
 * y axis.
 */
Rgb BehindAt(float x, float y, bool alongY);

/** @brief All the light at a world point as linear light: lights add up, so it can pass 1 where they overlap. */
float LinearAt(float x, float y);

/** @brief The brightest light there is (today's fully lit shade), for things that shine by themselves. */
Rgb Full();

/** @brief The light of the sky alone as drawn, in town (Tristram's evening); elsewhere Full. */
Rgb Sky();

/** @brief Light made some of the game's light levels brighter, no brighter than Full (other heroes are drawn 5 levels up). */
Rgb Boost(const Rgb &light, float levels);

/** @brief This tile stops light (walls, pillars, closed doors). */
bool Blocks(int x, int y);

/**
 * @brief The strongest lights shining on a world point that can see it, strongest first.
 * @param overhead Set to the strongest light hanging right over the point (a hero's own), which casts its shadow
 * straight down; its strength is 0 when there is none.
 * @return How many were written to out.
 */
int ShadowLights(float x, float y, ShadowLight *out, int maxCount, ShadowLight &overhead);

/** @brief The linear light a shadow light gives at a world point, 0 where a wall stands between them. */
float LinearOf(const ShadowLight &light, float x, float y);

/**
 * @brief The linear light the heroes' own glows give at a world point (the light the game gives every hero to see by),
 * 0 from a glow a wall stands in front of.
 */
float GlowAt(float x, float y);

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
