/**
 * @file poe2/vfx.hpp
 *
 * POE2 Mashup effects engine: skill effects drawn by code instead of art. Glowing particles, ground sigils, lightning
 * bolts, shockwave rings, cracks and trails go in a full-colour layer over the 8-bit world, with bloom, under the UI.
 * Everything here is cosmetic: it never touches the game state, its randomness or saves.
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/displacement.hpp"
#include "engine/point.hpp"
#include "engine/size.hpp"
#include "tables/misdat.h"

#ifndef USE_SDL1
#ifdef USE_SDL3
#include <SDL3/SDL_render.h>
#else
#include <SDL.h>
#endif
#endif

namespace devilution::vfx {

/** @brief The look of an effect: its colours and the particles it throws. */
enum class Element : uint8_t {
	Fire,
	Cold,
	Lightning,
	Physical,
	Chaos,
	Holy,
	Arcane,
	Blood,
};

/** @brief The element a skill's damage looks like; spells without an element look arcane. */
Element ElementOf(DamageType type, bool spell);

/** @brief Whether effects are drawn at all: POE2 Mashup, the Modern Effects option, and a screen to draw on. */
bool Enabled();

/** @brief Removes every effect, e.g. on a new level. */
void Clear();

/** @brief A rune circle that draws itself on the ground and charges up for the given time, then flashes. */
void Sigil(Point tile, int radiusTiles, int durationMs, Element element);
/** @brief A heavy landing: flash, shockwave, dust, debris and glowing cracks. */
void Slam(Point tile, int radiusTiles, Element element);
/** @brief The ground splitting open from one tile to another. */
void Fissure(Point from, Point to, Element element);
/** @brief An explosion of the element's particles; size 1 is a normal hit. */
void Impact(Point tile, Element element, float size);
/** @brief A small shower of bright sparks and a flash, like steel striking: a weapon hit or an arrow landing. */
void Sparks(Point tile, Element element, float size);
/** @brief Lightning arcing between two tiles. */
void Arc(Point from, Point to, Element element);
/** @brief Lightning striking down from the sky onto a tile. */
void SkyStrike(Point tile, Element element);
/** @brief A glowing crescent swept through the air by a weapon, from a tile towards another. */
void Swing(Point from, Point toward, Element element, bool wide);
/** @brief One of each effect around a tile, to see them all (the console's vfx()). */
void Showcase(Point tile);

/** @brief Called while the world is drawn: where the view is, so effects line up with the tiles. */
void SetCamera(Point viewTile, Displacement offset, bool zoom);
/** @brief Follows the frame's missiles: trails and glows on projectiles, bursts where explosions appear. */
void ScanMissiles();
/** @brief Moves every effect on by the given time. */
void Advance(float ms);

/** @brief How many particles are alive (tests). */
size_t ParticleCount();
/** @brief How many rings, sigils, bolts, cracks and glows are alive (tests). */
size_t ShapeCount();

#if !defined(USE_SDL1) && !defined(USE_SDL3)
/**
 * @brief Draws the effects over the world, which was just drawn to the whole render target.
 * @param worldSize The world's size in pixels; the renderer's logical size can be smaller (the UI's).
 */
void Render(SDL_Renderer *renderer, Size worldSize, Size logicalSize);
#endif

} // namespace devilution::vfx
