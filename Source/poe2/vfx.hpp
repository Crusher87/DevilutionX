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
#include "engine/surface.hpp"
#include "tables/misdat.h"

#ifndef USE_SDL1
#ifdef USE_SDL3
#include <SDL3/SDL_render.h>
#else
#include <SDL.h>
#endif
#endif

#include "engine/clx_sprite.hpp"

namespace devilution {
struct Object;
} // namespace devilution

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

/** @brief How a weapon moves through the air, to match the hero's attack animation. */
enum class SwingStyle : uint8_t {
	/** @brief Swept level across the front. */
	Slash,
	/** @brief Swept back the other way: a second, backhand cut. */
	Backhand,
	/** @brief Brought down from overhead onto the target, as the hero swings an axe or a mace. */
	Chop,
};

/** @brief The element a skill's damage looks like; spells without an element look arcane. */
Element ElementOf(DamageType type, bool spell);

/** @brief Whether effects are drawn at all: POE2 Mashup, the Modern Effects option, and a screen to draw on. */
bool Enabled();

/** @brief Removes every effect, e.g. on a new level. */
void Clear();

/** @brief A rune circle that draws itself on the ground and charges up for the given time, then flashes. */
void Sigil(Point tile, int radiusTiles, int durationMs, Element element);
/**
 * @brief Frost cracks spread over a tile for the given time, then a cluster of ice spikes bursts up out of the ground,
 * stands a moment and shatters. Size 1 is one tile across.
 */
void IceSpike(Point tile, int delayMs, float size);
/** @brief A heavy landing: flash, shockwave, dust, debris and glowing cracks. */
void Slam(Point tile, int radiusTiles, Element element);
/** @brief The ground splitting open from one tile to another. */
void Fissure(Point from, Point to, Element element);
/** @brief An explosion of the element's particles; size 1 is a normal hit. */
void Impact(Point tile, Element element, float size);
/** @brief A small shower of bright sparks and a flash, like steel striking: a weapon hit or an arrow landing. */
void Sparks(Point tile, Element element, float size);
/** @brief Arrows loosed from a bow: a bowstring snap, pale shafts fanning out toward @p toward, and with @p feathers a few fletching flecks. */
void Volley(Point from, Point toward, int arrows, bool feathers);
/** @brief A weapon biting into a target: blood and a few sparks for plain steel, the element's sparks otherwise. */
void Cut(Point tile, Element element);
/** @brief Lightning arcing between two tiles. */
void Arc(Point from, Point to, Element element);
/** @brief Lightning striking down from the sky onto a tile. */
void SkyStrike(Point tile, Element element);
/** @brief A crescent smear swept through the air by a weapon, from a tile towards another; it glows only for an elemental weapon. Heavy is an axe, mace or staff: a shorter, thicker arc. */
void Swing(Point from, Point toward, Element element, bool wide, bool heavy = false);
/** @brief A weapon's smear in the given style. A chop ends in a thud of dust where it lands. */
void SwingAs(Point from, Point toward, Element element, SwingStyle style, bool heavy);
/**
 * @brief Cleave: a long, wide slice that carries on through the target and both sides of it, throwing sparks along
 * its edge. With @p chop the blade first comes down from overhead, as an axe does, and the slice bursts out sideways
 * from where it lands.
 */
void Cleave(Point from, Point toward, Element element, bool chop);
/** @brief Whirlwind: blades spinning round a tile for a few turns, in a funnel of wind and kicked-up dust. */
void Whirl(Point tile, int radiusTiles, Element element);
/** @brief The hero levelling up: a pillar of hellfire, a pentagram burning into the floor, shockwaves and a fountain of embers. */
void LevelUp(Point tile);
/** @brief One of each effect around a tile, to see them all (the console's vfx()). */
void Showcase(Point tile);

/** @brief Called while the world is drawn: where the view is, so effects line up with the tiles. */
void SetCamera(Point viewTile, Displacement offset, bool zoom);
/** @brief Follows the frame's missiles: trails and glows on projectiles, bursts where explosions appear. */
void ScanMissiles();
/** @brief Whether the effects draw this missile themselves this frame, so its classic sprite is left out. */
bool ReplacesSprite(const Missile &missile);
/**
 * @brief The frame to draw of a light object: candles hold one frame (their glow sways instead) and torches run at
 * their own pace and phase, so no two flames flicker in step. Other objects draw their current frame.
 */
ClxSprite ObjectSprite(const Object &object);
/**
 * @brief Finds what the dungeon itself gives off this frame: the flames of torches, candles, fires and fire traps, and
 * the lava and water on screen (the palette-animated colours of the frame just drawn).
 */
void ScanWorld(const Surface &world);
/**
 * @brief Whether to keep a copy of the bare floor while the world is drawn, so the effects lying on the floor (circles,
 * cracks, shockwaves, light pools) can hide behind the walls, objects, items and characters drawn over it.
 */
bool WantsFloorCopy();
/** @brief Copies the world just after its floor is drawn, before anything stands on it. */
void CopyFloor(const Surface &world);
/** @brief The copy of the floor, to zoom it the same way as the world; empty when there's none. */
Surface FloorCopy();
/**
 * @brief Whether a hero on this tile stands on or in front of a town portal, so they're drawn over it (Curtis
 * 2026-10-21: arriving in town, the hero was drawn under the portal).
 */
bool HeroInFrontOfPortal(Point tile);
/** @brief Starts this frame's record of the heroes drawn over the portals, before the world's sprites are drawn. */
void BeginFront(const Surface &world);
/** @brief Records the shape of a hero's sprite as it's drawn into the world, to draw the hero again over the portal. */
void MarkFront(const Surface &out, Point position, ClxSprite sprite);
/** @brief The record of the heroes over the portals, to zoom it with the world; empty when there's none. */
Surface FrontMask();
/** @brief Moves every effect on by the given time. */
void Advance(float ms);

/** @brief How many particles are alive (tests). */
size_t ParticleCount();
/** @brief How many rings, sigils, bolts, cracks and glows are alive (tests). */
size_t ShapeCount();
/** @brief How many flames, lava and water patches the last ScanWorld found (tests). */
size_t AmbientCount();
/** @brief How many town portals are standing or folding shut (tests). */
size_t PortalCount();
/** @brief How many raindrops and snowflakes are falling (tests). */
size_t DropCount();

#if !defined(USE_SDL1) && !defined(USE_SDL3)
/**
 * @brief Draws the effects over the world, which was just drawn to the whole render target.
 * @param worldSize The world's size in pixels; the renderer's logical size can be smaller (the UI's).
 */
void Render(SDL_Renderer *renderer, Size worldSize, Size logicalSize);
#endif

} // namespace devilution::vfx
