/**
 * @file poe2/vfx.hpp
 *
 * POE2 Mashup effects engine: skill effects drawn by code instead of art. Glowing particles, ground sigils, lightning
 * bolts, shockwave rings, cracks and trails go in a full-colour layer over the 8-bit world, with bloom, under the UI.
 * Everything here is cosmetic: it never touches the game state, its randomness or saves.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

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
	/** @brief Bone-white with dust and chips: the bone spells (Bone Prison, Bone Spikes, Bone Spear, Bone Bolt). */
	Bone,
	/** @brief Sickly yellow-green venom: droplets, bubbles and gas (the poison skills). */
	Poison,
};

/** @brief What lies on the ground poisoning it: a bubbling acid pool, a billowing gas cloud, or a murky bog. */
enum class MiasmaKind : uint8_t {
	Acid,
	Gas,
	Bog,
};

/**
 * @brief What a lobbed throw looks like in the air: a glass flask, a seed pod, an iron gas canister, a fire bomb with a
 * burning fuse, or a crackling ball of lightning.
 */
enum class LobKind : uint8_t {
	Flask,
	Seed,
	Canister,
	Bomb,
	Spark,
};

/** @brief How a weapon moves through the air, to match the hero's attack animation. Every hero swings right-handed. */
enum class SwingStyle : uint8_t {
	/** @brief Swept across the front from the hero's right to his left; a blade cuts down from over his right shoulder. */
	Slash,
	/** @brief Swept back the other way, from his left to his right: a second, backhand cut. */
	Backhand,
	/** @brief Brought down from overhead onto the target, as the hero swings an axe or a mace. */
	Chop,
	/** @brief Punched straight out at the target, as the hero strikes with a bare fist or a shield. */
	Jab,
};

/** @brief The element a skill's damage looks like: a physical spell looks like bone, a physical attack like dust and steel. */
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
/** @brief A clump of curved bone spikes bursts out of the ground after the delay, stands a moment and crumbles (Bone Spikes). */
void BoneSpikes(Point tile, int delayMs, float size);
/**
 * @brief A cage of ribs bursts up round a tile after the delay and hooks in over it, standing for the given time before it
 * crumbles (Bone Prison). Size 1 is one tile across.
 */
void BoneCage(Point tile, int delayMs, float size, int standMs);
/**
 * @brief Poison lying on the ground for the given time: an acid pool bubbling, a gas cloud rolling and rising, or a bog
 * with mist and bubbles. A key (any number but 0) lets EndKeyed take it away early.
 */
void Miasma(Point tile, int radiusTiles, int durationMs, MiasmaKind kind, uint32_t key = 0);
/** @brief Thorny vines burst up round a tile and stand for the given time before they wither (Thornvine Arrow). */
void Thorns(Point tile, int radiusTiles, int durationMs, uint32_t key = 0);
/** @brief A slab of rock thrust up out of the floor, standing for the given time or until EndKeyed breaks it (Shield Wall). */
void EarthWall(Point tile, int lifeMs, uint32_t key);
/** @brief Wave of Light's great bell of light, standing for the given time or until EndKeyed takes it (Q38). */
void Bell(Point tile, int lifeMs, uint32_t key);
/** @brief The bell tolls: a ring of holy light rolls out over the radius. */
void BellToll(Point tile, int radiusTiles);
/** @brief A swollen seed pod on the ground, throbbing, until its time runs out or EndKeyed bursts it (Bloated Pods). */
void Pod(Point tile, int lifeMs, uint32_t key);
/** @brief A gnarled effigy stake with glowing green eyes, standing for the given time (Plague Effigy). */
void Effigy(Point tile, int lifeMs, uint32_t key);
/** @brief The effigy's eyes flare as it looses an arrow. */
void EffigyShot(Point tile);
/** @brief A small iron gas canister on the ground, blinking green while it waits for an enemy (Venom Sentry). */
void Canister(Point tile, int lifeMs, uint32_t key);
/** @brief Something thrown in an arc from one tile to another, landing after the given time. */
void Lob(Point from, Point to, int flightMs, LobKind kind);
/** @brief Drops of acid fall out of the air over an area, the first landing after the given time (Acid Cloud). */
void AcidRain(Point tile, int radiusTiles, int fallMs);
/** @brief The keyed effect ends now: a pod or canister bursts, a vine withers, an effigy crumbles, a pool or cloud fades. */
void EndKeyed(uint32_t key);

/** @brief A heavy landing: flash, shockwave, dust, debris and glowing cracks. */
void Slam(Point tile, int radiusTiles, Element element);
/**
 * @brief A heavy nova rolling out over the floor to the radius: a dark wave with a bright lip, droplets or chips flung
 * outward, mist, and a stain left behind (Blood Nova).
 */
void Nova(Point tile, int radiusTiles, Element element);
/** @brief The ground splitting open from one tile to another. */
void Fissure(Point from, Point to, Element element);
/** @brief An explosion of the element's particles; size 1 is a normal hit. */
void Impact(Point tile, Element element, float size);
/** @brief A small shower of bright sparks and a flash, like steel striking: a weapon hit or an arrow landing. */
void Sparks(Point tile, Element element, float size);
/** @brief Arrows loosed from a bow: a bowstring snap, pale shafts fanning out toward @p toward, and with @p feathers a few fletching flecks. */
void Volley(Point from, Point toward, int arrows, bool feathers);
/**
 * @brief A weapon biting into a target: a dull flash, a few sparks and blood. A weapon's added element (@p element) is
 * only a faint touch on it: a dim glow and a few small sparks of that element.
 */
void Cut(Point tile, Element element);
/** @brief Lightning arcing between two tiles. */
void Arc(Point from, Point to, Element element);
/**
 * @brief The Butcher's Road hook: a heavy iron chain between two bodies at waist height. Thrown, it hangs slack a
 * moment and the hook bites with blood; drawn again each game tick while it drags, it is pulled taut.
 */
void HookChain(Point from, Point to, bool thrown);
/** @brief Lightning striking down from the sky onto a tile. */
void SkyStrike(Point tile, Element element);
/**
 * @brief A crescent smear swept through the air by a weapon, from a tile towards another; it glows in a skill's
 * @p element. Heavy is an axe, mace or staff: a shorter, thicker arc. A weapon's added element (@p imbued) shows only as
 * a dim sheen and faint flickers along its edge.
 */
void Swing(Point from, Point toward, Element element, bool wide, bool heavy = false, Element imbued = Element::Physical);
/** @brief A weapon's smear in the given style. A chop ends in a thud of dust where it lands. @p imbued as for Swing. */
void SwingAs(Point from, Point toward, Element element, SwingStyle style, bool heavy, Element imbued = Element::Physical);
/**
 * @brief Cleave: a long, wide slice that carries on through the target and both sides of it, throwing sparks along
 * its edge. With @p chop the blade first comes down from overhead, as an axe does, and the slice bursts out sideways
 * from where it lands. @p imbued as for Swing.
 */
void Cleave(Point from, Point toward, Element element, bool chop, Element imbued = Element::Physical);
/**
 * @brief Whirlwind: blades spinning round a tile for a few turns, in a funnel of wind and kicked-up dust. With @p staff
 * (Whirling Staff), both ends of a staff sweep round half a turn apart, in duller, thicker smears of wood. @p imbued as
 * for Swing.
 */
void Whirl(Point tile, int radiusTiles, Element element, bool staff = false, Element imbued = Element::Physical);
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
/** @brief Whether the missile is a blow of steel (MetalHit): a pale flash and sparks, never Charged Bolt's blue crackle. */
bool IsSteelHit(const Missile &missile);
/** @brief Whether the missile is drawn as Arcing Sparks' small sparks rather than Charged Bolt's bolts: both fly the same missile. */
bool IsArcingSpark(const Missile &missile);
/** @brief Poe2MissileData::look for an arcing spark another skill throws (Charged Bolt's, Q34). */
constexpr uint8_t ArcingSparkLook = 3;
/** @brief Poe2MissileData::look for a thrown blade or axe that spins as it flies (Q39). */
constexpr uint8_t SpinningBladeLook = 4;
/** @brief The colour a missile's light shines in with HD Lighting: its effects' own colour (portals blue or red), 0-255. */
std::array<uint8_t, 3> LightColourOf(const Missile &missile);
/**
 * @brief The frame to draw of a light object: candles hold one frame (their glow sways instead) and torches run at
 * their own pace and phase, so no two flames flicker in step. Other objects draw their current frame.
 */
ClxSprite ObjectSprite(const Object &object);
/**
 * @brief Where the small flames hug the burning cross (burncros), in pixels from its sprite's top left: along its upright
 * and its bar.
 */
const std::vector<Point> &BurningCrossFlames();
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
 * @brief Whether a hero or monster on this tile stands on or in front of a town portal, or in front of a torch, candle
 * or brazier, so they're drawn over its effects (Curtis 2026-10-21: arriving in town, the hero was drawn under the
 * portal; 2026-10-23: under a brazier's fire). Notes the flames it stands in front of, which then go under it.
 */
bool InFrontOfEffects(Point tile);
/** @brief Starts this frame's record of the sprites drawn over the effects, before the world's sprites are drawn. */
void BeginFront(const Surface &world);
/** @brief Records the shape of a hero's or monster's sprite as it's drawn into the world, to draw it again over the effects. */
void MarkFront(const Surface &out, Point position, ClxSprite sprite);
/**
 * @brief Whether the walls, pillars and arches standing on this tile are in front of a portal, so they hide it as heroes
 * do (Curtis, Rogue run 2: the town portal was drawn over the walls in front of it).
 */
bool StandsInFrontOfPortal(Point tile);
/** @brief Before a standing piece is drawn into the world over this area (clipped to it): keeps the pixels it will cover. */
void BeginFrontPiece(const Surface &out, Point topLeft, Size size);
/** @brief After it's drawn: records the pixels it changed as in front of the portals (not of the flames). */
void EndFrontPiece(const Surface &out);
/** @brief The record of the sprites over the effects, to zoom it with the world; empty when there's none. */
Surface FrontMask();
/** @brief Moves every effect on by the given time. */
void Advance(float ms);

/** @brief How many particles are alive (tests). */
size_t ParticleCount();
/** @brief How far the farthest particle is from a flying missile's head (an arrow's tip), in pixels (tests). */
float FarthestParticleFrom(const Missile &missile);
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
