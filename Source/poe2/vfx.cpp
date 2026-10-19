/**
 * @file poe2/vfx.cpp
 *
 * POE2 Mashup effects engine. The effects live in the world's isometric pixel space (a tile's centre is at
 * ((x - y) * 32, (x + y) * 16)), move on with real time, and are drawn as triangles: glowing ones added together into
 * their own layer (which is blurred for bloom), dark ones (smoke, dust, scorch marks) blended straight onto the world.
 */
#include "poe2/vfx.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <magic_enum/magic_enum.hpp>

#include "diablo.h"
#include "engine/dx.h"
#include "engine/palette.h"
#include "engine/render/clx_render.hpp"
#include "engine/render/scrollrt.h"
#include "engine/size.hpp"
#include "engine/surface.hpp"
#include "headless_mode.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "missiles.h"
#include "monster.h"
#include "objects.h"
#include "options.h"
#include "poe2/camera_shake.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/telemetry.hpp"
#include "towners.h"
#include "utils/is_of.hpp"
#include "utils/str_cat.hpp"

#ifndef USE_SDL1
#include "utils/display.h"
#endif

namespace devilution::vfx {

namespace {

constexpr float Pi = 3.14159265F;
/** @brief A world circle of one tile's radius, in isometric pixels across. */
constexpr float TileRadius = 45.F;

struct Vec {
	float x = 0;
	float y = 0;
};

Vec operator+(Vec a, Vec b) { return { a.x + b.x, a.y + b.y }; }
Vec operator-(Vec a, Vec b) { return { a.x - b.x, a.y - b.y }; }
Vec operator*(Vec a, float s) { return { a.x * s, a.y * s }; }

float Length(Vec v) { return std::sqrt(v.x * v.x + v.y * v.y); }

struct Rgb {
	uint8_t r, g, b;
};

[[maybe_unused]] Rgb Mix(Rgb a, Rgb b, float t)
{
	t = std::clamp(t, 0.F, 1.F);
	return { static_cast<uint8_t>(a.r + (b.r - a.r) * t), static_cast<uint8_t>(a.g + (b.g - a.g) * t), static_cast<uint8_t>(a.b + (b.b - a.b) * t) };
}

/** @brief An element's colours, hottest first. */
struct Colours {
	Rgb core, mid, edge;
};

Colours ColoursOf(Element element)
{
	switch (element) {
	case Element::Fire: return { { 255, 240, 200 }, { 255, 150, 40 }, { 180, 50, 10 } };
	case Element::Cold: return { { 240, 250, 255 }, { 140, 210, 255 }, { 60, 120, 200 } };
	case Element::Lightning: return { { 255, 255, 255 }, { 170, 200, 255 }, { 110, 90, 255 } };
	case Element::Physical: return { { 255, 240, 220 }, { 200, 170, 130 }, { 110, 90, 70 } };
	case Element::Chaos: return { { 200, 255, 160 }, { 120, 220, 60 }, { 90, 30, 130 } };
	case Element::Holy: return { { 255, 250, 225 }, { 255, 210, 120 }, { 200, 140, 40 } };
	case Element::Arcane: return { { 220, 220, 255 }, { 120, 140, 255 }, { 70, 50, 180 } };
	case Element::Blood: return { { 255, 190, 180 }, { 220, 30, 30 }, { 90, 0, 0 } };
	}
	return { { 255, 255, 255 }, { 200, 200, 200 }, { 100, 100, 100 } };
}

/** @brief Effects' own dice: cosmetic, so they never touch the game's seeded RNG. */
uint32_t Seed = 0x9E3779B9U;

uint32_t NextRandom()
{
	Seed ^= Seed << 13;
	Seed ^= Seed >> 17;
	Seed ^= Seed << 5;
	return Seed;
}

float Random(float lo, float hi)
{
	return lo + (hi - lo) * static_cast<float>(NextRandom() & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

[[maybe_unused]] float SeededRandom(uint32_t &seed)
{
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;
	return static_cast<float>(seed & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

Vec TileCentre(Point tile)
{
	return { static_cast<float>((tile.x - tile.y) * 32), static_cast<float>((tile.x + tile.y) * 16) };
}

enum class Shape : uint8_t {
	/** @brief A soft round glow. */
	Dot,
	/** @brief A short line along its movement, like a spark. */
	Streak,
	/** @brief A sharp splinter pointing along its movement. */
	Shard,
	/** @brief A dark lump of rock or dirt (blended, not glowing). */
	Chunk,
	/** @brief A soft puff of smoke, dust or mist (blended). */
	Puff,
};

struct Particle {
	Vec pos;
	Vec vel;
	/** @brief Height above the ground, drawn as moving up the screen. */
	float z = 0;
	float vz = 0;
	float gravity = 0;
	float drag = 0;
	float age = 0;
	float life = 1;
	float size0 = 2;
	float size1 = 0;
	Rgb c0, c1;
	float alpha = 1;
	Shape shape = Shape::Dot;
	bool bounced = false;
};

/** @brief An expanding ring on the ground (shockwaves, novas) or a glowing disc (flashes, light pools). */
struct Ring {
	Vec pos;
	float r0, r1;
	float age = 0;
	float life;
	float width;
	Rgb colour;
	float alpha;
	/** @brief 0 for a ring; a disc fills it. */
	bool disc = false;
	/** @brief Lying flat on the ground rather than facing the camera. */
	bool ground = true;
	/** @brief Blended (dust, scorch) instead of glowing. */
	bool dark = false;
	float jitter = 0;
	uint32_t seed = 1;
	/** @brief Grows fast then slows (true), or evenly. */
	bool easeOut = true;
};

struct Bolt {
	Vec from, to;
	float age = 0;
	float life;
	float width;
	Colours colours;
	uint32_t seed;
	float reseedTimer = 0;
};

struct SigilFx {
	Vec pos;
	float radius;
	float age = 0;
	float life;
	Colours colours;
	uint32_t seed;
	int glyphs;
	int star;
	/** @brief Lights and darkens the floor inside the circle; off where the hero stands in the middle of it. */
	bool floor = true;
};

struct Sweep {
	Vec pos;
	float facing;
	float span;
	float radius;
	float age = 0;
	float life;
	Colours colours;
	Element element;
	bool heavy;
};

struct Crack {
	Vec pos;
	std::vector<std::array<Vec, 2>> segments;
	float age = 0;
	float life;
	Rgb glow;
};

/** @brief One crystal of an ice spike cluster, relative to the cluster's centre on the ground. */
struct Crystal {
	Vec base;
	/** @brief How far the tip leans sideways for each pixel of height. */
	Vec lean;
	float height;
	float width;
	/** @brief How long after the eruption this crystal pushes up, in ms. */
	float delay;
};

/** @brief A branch of frost growing over the ground from an ice spike, offsets from its foot; starts once growth reaches from. */
struct FrostLine {
	std::vector<Vec> points;
	float from;
};

/** @brief A big piece of a shattered ice crystal, tumbling and falling: a tapered slab wb wide at its foot and wt at its top. */
struct IceChunk {
	Vec pos;
	Vec vel;
	float z;
	float vz;
	float angle;
	float spin;
	float wb;
	float wt;
	float height;
	float life;
	float age = 0;
	bool landed = false;
};

/** @brief Ice spikes bursting out of the ground: frost cracks while it waits, then the crystals, then the shatter. */
struct IceSpikeFx {
	Vec pos;
	float age = 0;
	float life;
	float wait;
	float size;
	std::vector<Crystal> crystals;
	std::vector<FrostLine> frost;
	std::vector<IceChunk> chunks;
	bool erupted = false;
	bool shattered = false;
};

/** @brief How long the crystals stand before they shatter, and how long the shatter takes, in ms. */
constexpr float IceSpikeStand = 650;
constexpr float IceSpikeShatter = 140;
/** @brief How long a crystal takes to punch up to full height, and how long the frost marks outlast the shatter, in ms. */
constexpr float IceSpikeRise = 55;
constexpr float IceSpikeFrost = 1100;

/** @brief How a projectile's trail looks beyond its element, so each monster's spell reads differently. */
enum class TrailStyle : uint8_t {
	Plain,
	/** @brief Two wisps spiralling around its path (Succubi's blood stars). */
	Ribbon,
	/** @brief Drops falling off it (acid spit). */
	Drip,
	/** @brief Heavy molten blobs: drips, embers and smoke (magma balls). */
	Magma,
	/** @brief A throbbing orb shedding motes (the Hellfire liches' and orbs' flares). */
	Orb,
};

struct Trail {
	std::array<Vec, 10> points;
	int count = 0;
	Element element = Element::Fire;
	uint32_t frame = 0;
	float emit = 0;
	/** @brief An arrow or bolt: a pale streak behind it and sparks where it stops. */
	bool arrow = false;
	TrailStyle style = TrailStyle::Plain;
	/** @brief Monsters' spells are a little smaller than the hero's, bosses' bigger. */
	float scale = 1;
};

constexpr size_t MaxParticles = 4000;

std::vector<Particle> Particles;
std::vector<Ring> Rings;
std::vector<Bolt> Bolts;
std::vector<SigilFx> Sigils;

/** @brief A pillar of light standing up from the ground. */
struct Beam {
	Vec pos;
	float age = 0;
	float life;
	float height;
	float width;
	Colours colours;
};

std::vector<Beam> Beams;

constexpr int RitualRunes = 15;
// The level-up circle's timeline, in ms from the start.
constexpr float RitualRingMs = 450;
constexpr float RitualStarStart = 300;
constexpr float RitualStrokeMs = 170;
constexpr float RitualInnerStart = RitualStarStart + 5 * RitualStrokeMs;
constexpr float RitualInnerMs = 160;
constexpr float RitualRuneStart = 550;
constexpr float RitualRuneMs = 90;
constexpr float RitualBurnStart = 3300;
constexpr float RitualBurnMs = 900;
constexpr float RitualScorchMs = 2200;

/**
 * @brief The level-up circle: a ring of thorns that draws itself in from the front, a pentagram drawn one line at a time,
 * Diablo II runes written one by one into the band between the rings, then a burning edge that eats it from the front
 * round to the back and leaves it scorched into the floor.
 */
struct RitualFx {
	Vec pos;
	float radius;
	float age = 0;
	float life;
	Colours colours;
	uint32_t seed;
	std::array<uint8_t, RitualRunes> runes;
};

std::vector<RitualFx> Rituals;

/** @brief The 33 runes of Diablo II, El to Zod. */
constexpr size_t RuneCount = 33;
std::vector<Crack> Cracks;
std::vector<IceSpikeFx> IceSpikes;
std::vector<Sweep> Sweeps;
/** @brief Glows that last one frame (projectile heads). */
std::vector<Ring> FrameGlows;
/** @brief A projectile's body this frame, drawn instead of its classic sprite: a bright head and a tapering tail. */
struct Comet {
	Vec head;
	/** @brief Unit vector of its flight. */
	Vec dir;
	Element element;
	float scale;
	bool small;
	uint32_t seed;
};
std::vector<Comet> FrameComets;
/** @brief The scan the effects were last drawn after; until they are, classic sprites stay. */
uint32_t LastDrawnScan = 0;
std::unordered_map<const Missile *, Trail> Trails;
/** @brief Bursts seen this frame (frame number) and whether their effect has gone off. */
struct SeenBurst {
	uint32_t frame;
	bool fired;
};
std::unordered_map<const Missile *, SeenBurst> SeenBursts;
uint32_t ScanFrame = 0;

/** @brief What a patch of the dungeon gives off while it's on screen. */
enum class Ambient : uint8_t {
	/** @brief A torch, candle or fire's flame: embers and a wisp of smoke. */
	Flame,
	/** @brief Molten rock: embers, bubbles popping and smoke. */
	Lava,
	/** @brief A pool: glints and ripples. */
	Water,
	/** @brief A charging monster's feet: dust and speed streaks. */
	Charge,
	/** @brief A burning monster: flames licking up its body. */
	Ignite,
	/** @brief A shocked monster: sparks crackling over it. */
	Shock,
	/** @brief A chilled monster: frost drifting down off it. */
	Chill,
	/** @brief Motes of the emitter's colour rising off a monster (rarity, regeneration, extra life). */
	Motes,
	/** @brief Sparks flying off a hard-hitting monster. */
	Embers,
};

/** @brief A flame or a patch of lava or water on screen, found anew each frame. */
struct Emitter {
	Vec pos;
	/** @brief Flames: the square root of the flame's pixels. Lava and water: the share of the patch that is fluid. */
	float size;
	Ambient kind;
	Rgb colour;
	/** @brief Lava and water: half the patch's width, in world pixels. */
	float spread = 0;
};

std::vector<Emitter> Emitters;
/** @brief Real time the ambient effects have run, for their flicker and pulse. */
float AmbientTime = 0;

/** @brief A flame in an object's sprite, from the sprite's bottom left corner. */
struct HotSpot {
	Vec offset;
	float size;
};

/** @brief The flames found in each sprite frame, by its pixel data; sprites are reloaded with each level. */
std::unordered_map<const uint8_t *, std::vector<HotSpot>> HotSpots;
/** @brief The flames of a whole animation, where they sit across its frames, by the first frame's pixel data. */
std::unordered_map<const uint8_t *, std::vector<HotSpot>> SteadySpots;

/** @brief How much a monster stands out: magic and rare monsters, uniques, and the quest bosses and Diablo. */
enum class Rank : uint8_t {
	Normal,
	Magic,
	Rare,
	Unique,
	Boss,
};

/** @brief The mark under a monster of rank: a faint glow for magic and rares, a rune circle for uniques. */
struct Halo {
	Vec pos;
	float radius;
	Rank rank;
	/** @brief Resistant rares shimmer through the colours. */
	bool prismatic;
	uint32_t seed;
};

std::vector<Halo> Halos;

/** @brief The marker over a towner's head who has a quest to give (News) or is waiting on the hero (Reward). */
struct QuestMark {
	Vec pos;
	bool reward;
	float phase;
	/** @brief How long it has been showing, so it can draw itself in when it first comes into view. */
	float age;
};

std::vector<QuestMark> QuestMarks;
/** @brief When each towner's marker came into view, by towner type. */
std::unordered_map<int, float> QuestMarkSince;
/** @brief Each monster's mode last frame, to catch it starting to cast, fade, charge or die. */
std::unordered_map<const Monster *, MonsterMode> LastModes;
/** @brief Each monster's burning, shocked, frozen and chilled looks last frame, for the play log. */
std::unordered_map<const Monster *, uint8_t> LastStatus;

Vec CameraOrigin;
float CameraScale = 1;
int LevelKey = -1;

float Intensity()
{
	return static_cast<float>(*GetOptions().Graphics.effectIntensity) / 100.F;
}

int Scaled(float count)
{
	return std::max(0, static_cast<int>(count * Intensity() + 0.5F));
}

void AddParticle(const Particle &particle)
{
	if (Particles.size() < MaxParticles)
		Particles.push_back(particle);
}

/** @brief A spray of particles outward on the ground plane. */
void Spray(Vec pos, int count, float speedLo, float speedHi, float lifeLo, float lifeHi, Shape shape, Rgb c0, Rgb c1, float size0, float size1, float vzLo, float vzHi, float gravity, float drag, float alpha)
{
	for (int i = 0; i < count; i++) {
		const float angle = Random(0, 2 * Pi);
		const float speed = Random(speedLo, speedHi);
		Particle p;
		p.pos = pos;
		p.vel = { std::cos(angle) * speed, std::sin(angle) * speed * 0.5F };
		p.vz = Random(vzLo, vzHi);
		p.gravity = gravity;
		p.drag = drag;
		p.life = Random(lifeLo, lifeHi);
		p.size0 = size0;
		p.size1 = size1;
		p.c0 = c0;
		p.c1 = c1;
		p.alpha = alpha;
		p.shape = shape;
		AddParticle(p);
	}
}

void AddRing(Vec pos, float r0, float r1, float life, float width, Rgb colour, float alpha, bool disc, bool ground, bool dark = false, float jitter = 0)
{
	Ring ring;
	ring.pos = pos;
	ring.r0 = r0;
	ring.r1 = r1;
	ring.life = life;
	ring.width = width;
	ring.colour = colour;
	ring.alpha = alpha;
	ring.disc = disc;
	ring.ground = ground;
	ring.dark = dark;
	ring.jitter = jitter;
	ring.seed = NextRandom() | 1;
	Rings.push_back(ring);
}

void AddBolt(Vec from, Vec to, float life, float width, const Colours &colours)
{
	Bolts.push_back({ from, to, 0, life, width, colours, NextRandom() | 1, 0 });
}

/** @brief Branching cracks spreading from a point, each segment jittered. */
void AddCracks(Vec pos, float length, int branches, Rgb glow, float life)
{
	Crack crack;
	crack.pos = pos;
	crack.life = life;
	crack.glow = glow;
	for (int b = 0; b < branches; b++) {
		float angle = (2 * Pi * b) / branches + Random(-0.3F, 0.3F);
		Vec at {};
		float remaining = length * Random(0.6F, 1.F);
		while (remaining > 0) {
			const float step = Random(8, 16);
			angle += Random(-0.5F, 0.5F);
			const Vec next = at + Vec { std::cos(angle) * step, std::sin(angle) * step * 0.5F };
			crack.segments.push_back({ at, next });
			// A side split now and then.
			if (Random(0, 1) < 0.18F) {
				const float side = angle + (Random(0, 1) < 0.5F ? 0.9F : -0.9F);
				const Vec end = next + Vec { std::cos(side) * step * 1.4F, std::sin(side) * step * 0.7F };
				crack.segments.push_back({ next, end });
			}
			at = next;
			remaining -= step;
		}
	}
	Cracks.push_back(std::move(crack));
}

void FireImpact(Vec pos, float size, const Colours &c)
{
	AddRing(pos + Vec { 0, -16 }, 0, 26 * size, 140, 0, c.core, 1.F, true, false);
	AddRing(pos, TileRadius * 0.4F * size, TileRadius * 1.8F * size, 450, 0, c.mid, 0.55F, true, true);
	AddRing(pos, 0, TileRadius * 1.5F * size, 260, 4, c.mid, 0.9F, false, true);
	Spray(pos, Scaled(40 * size), 60, 220 * size, 400, 1100, Shape::Dot, c.core, c.edge, 3.5F, 0.5F, 80, 260, 260, 1.6F, 1);
	Spray(pos, Scaled(10 * size), 10, 50, 900, 1600, Shape::Puff, { 55, 45, 40 }, { 25, 22, 20 }, 10 * size, 26 * size, 30, 70, -10, 0.6F, 0.45F);
	if (size >= 1)
		AddRing(pos, TileRadius * 1.2F * size, TileRadius * 1.2F * size, 4500, 0, { 0, 0, 0 }, 0.45F, true, true, true, 0.25F);
}

void ColdImpact(Vec pos, float size, const Colours &c)
{
	AddRing(pos + Vec { 0, -16 }, 0, 24 * size, 120, 0, c.core, 0.9F, true, false);
	AddRing(pos, 0, TileRadius * 1.6F * size, 220, 3, c.mid, 1.F, false, true, false, 0.12F);
	AddRing(pos, TileRadius * 0.5F * size, TileRadius * 1.5F * size, 500, 0, c.mid, 0.4F, true, true);
	Spray(pos, Scaled(26 * size), 120, 300 * size, 350, 700, Shape::Shard, c.core, c.mid, 7, 2, 60, 200, 500, 1.F, 1);
	Spray(pos, Scaled(8 * size), 10, 40, 900, 1400, Shape::Puff, { 150, 190, 220 }, { 90, 120, 160 }, 12 * size, 30 * size, 10, 30, 0, 0.5F, 0.25F);
	AddRing(pos, TileRadius * 1.3F * size, TileRadius * 1.3F * size, 4000, 0, { 170, 220, 255 }, 0.12F, true, true, false, 0.2F);
}

void LightningImpact(Vec pos, float size, const Colours &c)
{
	AddRing(pos + Vec { 0, -16 }, 0, 30 * size, 100, 0, c.core, 1.F, true, false);
	AddRing(pos, TileRadius * size, TileRadius * 2.F * size, 300, 0, c.mid, 0.5F, true, true);
	const int bolts = std::max(2, Scaled(4 * size));
	for (int i = 0; i < bolts; i++) {
		const float angle = Random(0, 2 * Pi);
		const float length = Random(40, 90) * size;
		AddBolt(pos + Vec { 0, -16 }, pos + Vec { std::cos(angle) * length, std::sin(angle) * length * 0.5F - 16 }, Random(120, 220), 2.5F, c);
	}
	Spray(pos, Scaled(24 * size), 150, 400 * size, 150, 400, Shape::Streak, c.core, c.edge, 2, 1, 50, 200, 400, 2.F, 1);
}

void PhysicalImpact(Vec pos, float size, const Colours &c)
{
	AddRing(pos + Vec { 0, -14 }, 0, 18 * size, 90, 0, c.core, 0.8F, true, false);
	AddRing(pos, 0, TileRadius * 1.2F * size, 250, 8 * size, { 120, 100, 80 }, 0.5F, false, true, true);
	Spray(pos, Scaled(10 * size), 60, 160 * size, 500, 900, Shape::Chunk, { 70, 58, 45 }, { 40, 34, 28 }, 3, 3, 120, 260, 700, 0.5F, 1);
	Spray(pos, Scaled(10 * size), 20, 70, 600, 1100, Shape::Puff, { 110, 95, 75 }, { 70, 60, 50 }, 8 * size, 22 * size, 10, 40, 0, 0.8F, 0.35F);
	Spray(pos, Scaled(10 * size), 100, 260, 120, 260, Shape::Streak, c.core, c.mid, 1.5F, 0.5F, 40, 160, 300, 1.F, 1);
}

void ChaosImpact(Vec pos, float size, const Colours &c)
{
	AddRing(pos + Vec { 0, -16 }, 0, 26 * size, 160, 0, { 30, 0, 30 }, 0.7F, true, false, true);
	AddRing(pos, 0, TileRadius * 1.4F * size, 300, 3, c.mid, 0.8F, false, true, false, 0.08F);
	AddRing(pos, TileRadius * 0.5F * size, TileRadius * 1.6F * size, 600, 0, c.edge, 0.45F, true, true);
	Spray(pos, Scaled(24 * size), 20, 90, 700, 1400, Shape::Dot, c.mid, c.edge, 4, 1, 40, 120, -40, 1.2F, 0.9F);
	Spray(pos, Scaled(8 * size), 10, 40, 900, 1500, Shape::Puff, { 50, 20, 60 }, { 20, 10, 25 }, 12 * size, 28 * size, 10, 40, 0, 0.6F, 0.4F);
}

void HolyImpact(Vec pos, float size, const Colours &c)
{
	AddRing(pos + Vec { 0, -16 }, 0, 30 * size, 160, 0, c.core, 1.F, true, false);
	AddRing(pos, 0, TileRadius * 1.6F * size, 300, 3, c.mid, 1.F, false, true);
	AddRing(pos, TileRadius * 0.6F * size, TileRadius * 1.8F * size, 500, 0, c.mid, 0.5F, true, true);
	for (int i = 0; i < Scaled(14 * size); i++) {
		Particle p;
		p.pos = pos + Vec { Random(-20, 20) * size, Random(-10, 10) * size };
		p.vz = Random(160, 320);
		p.life = Random(400, 800);
		p.size0 = 2.5F;
		p.size1 = 0.5F;
		p.c0 = c.core;
		p.c1 = c.edge;
		p.shape = Shape::Streak;
		AddParticle(p);
	}
}

void BloodImpact(Vec pos, float size, const Colours &c)
{
	AddRing(pos + Vec { 0, -16 }, 0, 18 * size, 120, 0, c.mid, 0.7F, true, false);
	Spray(pos, Scaled(22 * size), 60, 200 * size, 400, 800, Shape::Chunk, { 150, 10, 10 }, { 70, 0, 0 }, 2.5F, 2, 80, 220, 700, 0.5F, 1);
	AddRing(pos, TileRadius * 0.6F * size, TileRadius * 0.9F * size, 3500, 0, { 70, 0, 0 }, 0.5F, true, true, true, 0.35F);
}

/** @brief The world position of a missile's sprite centre. */
Vec MissileCentre(const Missile &missile)
{
	const Vec offset { static_cast<float>(missile.position.offsetForRendering.deltaX), static_cast<float>(missile.position.offsetForRendering.deltaY) };
	return TileCentre(missile.position.tileForRendering) + offset + Vec { 0, 16 - missile._miAnimWidth / 2.F };
}

struct GraphicLook {
	Element element;
	/** @brief 0: not an effect of ours; otherwise the burst's size when it appears. */
	float burst;
	/** @brief A projectile: a glowing head and a trail. */
	bool projectile;
};

GraphicLook LookOf(MissileGraphicID graphic)
{
	using G = MissileGraphicID;
	switch (graphic) {
	case G::Fireball:
	case G::FireArrow:
	case G::MagmaBall:
	case G::OrangeFlare:
		return { Element::Fire, 0, true };
	case G::MagmaBallExplosion:
	case G::FireArrowExplosion:
	case G::OrangeFlareExplosion:
		return { Element::Fire, 1, false };
	case G::BigExplosion:
	case G::ApocalypseBoom:
	case G::DiabloApocalypseBoom:
		return { Element::Fire, 1.7F, false };
	case G::Inferno:
		return { Element::Fire, 0.6F, false };
	case G::ChargedBolt:
	case G::LightningArrow:
	case G::YellowFlare:
	case G::BloodStarYellow:
		return { Element::Lightning, 0, true };
	case G::Lightning:
	case G::ThinLightning:
		return { Element::Lightning, 0.8F, false };
	case G::YellowFlareExplosion:
	case G::BloodStarYellowExplosion:
		return { Element::Lightning, 1, false };
	case G::BlueFlare:
	case G::BlueFlare2:
	case G::BloodStarBlue:
		return { Element::Cold, 0, true };
	case G::BlueFlareExplosion:
	case G::BlueFlareExplosion2:
	case G::BloodStarBlueExplosion:
		return { Element::Cold, 1, false };
	case G::StoneCurseShatter:
		return { Element::Physical, 0.9F, false };
	case G::HolyBolt:
		return { Element::Holy, 0, true };
	case G::HolyBoltExplosion:
	case G::FlashBottom:
		return { Element::Holy, 1, false };
	case G::Acid:
	case G::BoneSpirit:
	case G::BloodStar:
		return { Element::Chaos, 0, true };
	case G::AcidSplat:
	case G::BloodStarExplosion:
	case G::Etherealize:
		return { Element::Chaos, 0.9F, false };
	case G::RedFlare:
	case G::BloodStarRed:
		return { Element::Blood, 0, true };
	case G::RedFlareExplosion:
	case G::BloodStarRedExplosion:
		return { Element::Blood, 0.8F, false };
	case G::Arrow:
		return { Element::Physical, 0, true };
	default:
		return { Element::Physical, 0, false };
	}
}

void ImpactAt(Vec pos, Element element, float size)
{
	const Colours c = ColoursOf(element);
	switch (element) {
	case Element::Fire: FireImpact(pos, size, c); break;
	case Element::Cold: ColdImpact(pos, size, c); break;
	case Element::Lightning: LightningImpact(pos, size, c); break;
	case Element::Chaos: ChaosImpact(pos, size, c); break;
	case Element::Holy:
	case Element::Arcane: HolyImpact(pos, size, element == Element::Arcane ? ColoursOf(Element::Arcane) : c); break;
	case Element::Blood: BloodImpact(pos, size, c); break;
	case Element::Physical: PhysicalImpact(pos, size, c); break;
	}
}

void SparksAt(Vec pos, Element element, float size)
{
	const Colours c = ColoursOf(element);
	const Rgb core = element == Element::Physical ? Rgb { 255, 240, 200 } : c.core;
	const Rgb mid = element == Element::Physical ? Rgb { 255, 180, 90 } : c.mid;
	AddRing(pos + Vec { 0, -16 }, 0, 14 * size, 80, 0, core, 0.9F, true, false);
	AddRing(pos + Vec { 0, -16 }, 4, 20 * size, 140, 2, mid, 0.6F, false, false);
	Spray(pos + Vec { 0, -16 }, Scaled(14 * size), 120, 320 * size, 100, 260, Shape::Streak, core, mid, 1.5F, 0.5F, 30, 140, 500, 1.5F, 1);
}

/** @brief The trail particles a projectile leaves behind it each frame. */
void EmitTrail(Trail &trail, Vec head, float ms)
{
	const Colours c = ColoursOf(trail.element);
	if (trail.arrow && trail.element == Element::Physical) {
		// A thin pale streak of air torn open behind the arrow.
		trail.emit += 40 * Intensity() * ms / 1000.F;
		while (trail.emit >= 1) {
			trail.emit -= 1;
			Particle p;
			p.pos = head + Vec { Random(-2, 2), Random(-1, 1) };
			p.vel = { Random(-10, 10), Random(-5, 5) };
			p.life = Random(120, 240);
			p.shape = Shape::Streak;
			p.c0 = { 255, 245, 220 };
			p.c1 = { 150, 130, 100 };
			p.size0 = 1.5F;
			p.size1 = 0.5F;
			p.drag = 3.F;
			p.alpha = 0.7F;
			AddParticle(p);
		}
		return;
	}
	if (trail.style != TrailStyle::Plain && trail.count >= 1) {
		const Vec back = trail.count >= 2 ? trail.points[0] - trail.points[1] : Vec { 1, 0 };
		const float length = std::max(Length(back), 0.01F);
		const Vec side { -back.y / length, back.x / length };
		trail.emit += 40 * Intensity() * ms / 1000.F;
		while (trail.emit >= 1) {
			trail.emit -= 1;
			Particle p;
			p.c0 = c.core;
			p.c1 = c.edge;
			p.alpha = 0.9F;
			switch (trail.style) {
			case TrailStyle::Ribbon: {
				// Two wisps winding around the path; left behind, they draw a twisted ribbon.
				const float turn = AmbientTime * 0.025F;
				const float sign = NextRandom() % 2 == 0 ? 1.F : -1.F;
				p.pos = head + side * (sign * 9 * std::sin(turn)) + Vec { 0, sign * 4 * std::cos(turn) };
				p.life = Random(260, 420);
				p.size0 = 2.4F;
				p.size1 = 0.6F;
				p.c0 = c.mid;
				break;
			}
			case TrailStyle::Drip:
				p.pos = head + Vec { Random(-4, 4), Random(-2, 2) };
				p.vel = { Random(-8, 8), Random(70, 130) };
				p.life = Random(220, 380);
				p.size0 = 2.2F;
				p.size1 = 1.2F;
				p.c0 = { 210, 255, 90 };
				p.c1 = { 70, 140, 20 };
				break;
			case TrailStyle::Magma:
				if (NextRandom() % 3 == 0) {
					p.pos = head + Vec { Random(-4, 4), 0 };
					p.vz = Random(8, 20);
					p.life = Random(900, 1500);
					p.size0 = 5;
					p.size1 = 16;
					p.c0 = { 45, 32, 28 };
					p.c1 = { 22, 18, 16 };
					p.alpha = 0.3F;
					p.shape = Shape::Puff;
				} else {
					p.pos = head + Vec { Random(-5, 5), Random(-3, 3) };
					p.vel = { Random(-20, 20), Random(40, 110) };
					p.life = Random(250, 500);
					p.size0 = 3;
					p.size1 = 0.6F;
					p.c0 = { 255, 200, 90 };
					p.c1 = { 180, 40, 10 };
				}
				break;
			case TrailStyle::Orb: {
				const float angle = Random(0, 2 * Pi);
				p.pos = head + Vec { std::cos(angle) * 10, std::sin(angle) * 10 };
				p.vel = Vec { std::cos(angle), std::sin(angle) } * 25;
				p.life = Random(300, 500);
				p.size0 = 1.8F;
				p.size1 = 0.3F;
				p.c0 = c.core;
				p.c1 = c.mid;
				break;
			}
			case TrailStyle::Plain:
				break;
			}
			p.size0 *= trail.scale;
			p.size1 *= trail.scale;
			AddParticle(p);
		}
		if (trail.style != TrailStyle::Magma)
			return;
	}
	float rate = 0;
	switch (trail.element) {
	case Element::Fire: rate = 45; break;
	case Element::Lightning: rate = 30; break;
	case Element::Cold: rate = 25; break;
	case Element::Chaos: rate = 20; break;
	case Element::Physical: rate = 0; break;
	default: rate = 15; break;
	}
	trail.emit += rate * Intensity() * ms / 1000.F;
	while (trail.emit >= 1) {
		trail.emit -= 1;
		Particle p;
		p.pos = head + Vec { Random(-4, 4), Random(-3, 3) };
		p.vel = { Random(-25, 25), Random(-12, 12) };
		p.life = Random(250, 600);
		p.c0 = c.mid;
		p.c1 = c.edge;
		p.drag = 2.F;
		switch (trail.element) {
		case Element::Fire:
			p.vz = Random(10, 50);
			p.gravity = -60;
			p.size0 = 3;
			p.size1 = 0.5F;
			break;
		case Element::Lightning:
			p.shape = Shape::Streak;
			p.vel = { Random(-160, 160), Random(-80, 80) };
			p.life = Random(80, 180);
			p.c0 = c.core;
			p.size0 = 1.5F;
			break;
		case Element::Cold:
			p.shape = Shape::Shard;
			p.vel = { Random(-40, 40), Random(-20, 20) };
			p.gravity = 120;
			p.size0 = 4;
			p.size1 = 1;
			p.c0 = c.core;
			break;
		default:
			p.size0 = 2.5F;
			p.size1 = 0.5F;
			break;
		}
		AddParticle(p);
	}
}

void AdvanceParticles(float ms)
{
	const float s = ms / 1000.F;
	for (Particle &p : Particles) {
		p.age += ms;
		const float damp = std::max(0.F, 1 - p.drag * s);
		p.vel = p.vel * damp;
		p.pos = p.pos + p.vel * s;
		p.vz -= p.gravity * s;
		p.z += p.vz * s;
		if (p.z < 0 && p.gravity > 0) {
			p.z = 0;
			if (!p.bounced && p.vz < -40) {
				p.vz = -p.vz * 0.3F;
				p.bounced = true;
			} else {
				p.vz = 0;
				p.vel = p.vel * 0.5F;
			}
		}
	}
	std::erase_if(Particles, [](const Particle &p) { return p.age >= p.life; });
}

template <typename T>
void Age(std::vector<T> &items, float ms)
{
	for (T &item : items)
		item.age += ms;
	std::erase_if(items, [](const T &item) { return item.age >= item.life; });
}

/** @brief A splinter of ice thrown off a crystal from a height above the ground. */
void IceSplinter(Vec pos, float z, float speed, float vz, Rgb colour, float size, float life)
{
	const float angle = Random(0, 2 * Pi);
	Particle p;
	p.pos = pos;
	p.vel = { std::cos(angle) * speed, std::sin(angle) * speed * 0.5F };
	p.z = z;
	p.vz = vz;
	p.gravity = 1100;
	p.drag = 0.6F;
	p.life = life;
	p.size0 = size;
	p.size1 = size * 0.4F;
	p.c0 = colour;
	p.c1 = { 90, 150, 220 };
	p.shape = Shape::Shard;
	AddParticle(p);
}

/** @brief A flake of frost drifting up off the ice like an ember off a torch. */
void FrostFlake(Vec pos, float z, float spread)
{
	Particle p;
	p.pos = pos + Vec { Random(-spread, spread), Random(-spread, spread) * 0.5F };
	p.z = z;
	p.vel = { Random(-12, 12), Random(-5, 5) };
	p.vz = Random(10, 35);
	p.gravity = -12;
	p.drag = 0.8F;
	p.life = Random(900, 1600);
	p.size0 = Random(1.3F, 2.1F);
	p.size1 = 0.4F;
	p.c0 = { 230, 244, 255 };
	p.c1 = { 120, 170, 230 };
	p.alpha = 0.9F;
	AddParticle(p);
}

/** @brief How wide a crystal is at a height up it: a pillar narrowing a little to its neck, then a point. */
float CrystalWidth(const Crystal &crystal, float y)
{
	const float neck = crystal.height * 0.74F;
	if (y <= neck)
		return crystal.width * (1 - 0.18F * y / neck);
	return crystal.width * 0.82F * std::max(0.F, 1 - (y - neck) / (crystal.height - neck));
}

void AdvanceIceSpikes(float ms)
{
	const Colours cold = ColoursOf(Element::Cold);
	const float dt = ms / 1000;
	for (IceSpikeFx &fx : IceSpikes) {
		fx.age += ms;
		if (!fx.erupted && fx.age >= fx.wait) {
			// The ground bursts: a white flash, a soft wave of cold, earth thrown up, frost flakes and a breath of mist.
			fx.erupted = true;
			AddRing(fx.pos + Vec { 0, -45 * fx.size }, 0, 28 * fx.size, 120, 0, { 235, 248, 255 }, 0.7F, true, false);
			AddRing(fx.pos, 0, TileRadius * 1.8F * fx.size, 300, 3, cold.mid, 0.6F, false, true);
			Spray(fx.pos, Scaled(10 * fx.size), 50, 160, 600, 1000, Shape::Chunk, { 70, 64, 60 }, { 35, 32, 30 }, 3, 2.5F, 160, 340, 900, 0.4F, 1);
			for (int i = 0; i < Scaled(18 * fx.size); i++)
				FrostFlake(fx.pos, Random(5, 60) * fx.size, 18 * fx.size);
			Spray(fx.pos, Scaled(2 * fx.size), 15, 40, 700, 1100, Shape::Puff, { 175, 208, 238 }, { 100, 130, 170 }, 8 * fx.size, 20 * fx.size, 10, 30, -5, 0.5F, 0.12F);
		}
		if (fx.erupted && !fx.shattered) {
			// While it stands, flakes of frost drift up off it like embers off a torch.
			const float rate = 26 * fx.size * Intensity();
			for (int i = static_cast<int>(rate * dt + Random(0, 1)); i > 0; i--) {
				const Crystal &crystal = fx.crystals[NextRandom() % fx.crystals.size()];
				const float up = Random(0.2F, 0.95F) * crystal.height;
				FrostFlake(fx.pos + crystal.base + Vec { crystal.lean.x * up, crystal.lean.y * up }, up, crystal.width * 0.3F);
			}
		}
		if (fx.erupted && !fx.shattered && fx.age >= fx.wait + IceSpikeStand) {
			// Each crystal breaks into a few big pieces that topple and fall, with a crunch of splinters and frost.
			fx.shattered = true;
			for (const Crystal &crystal : fx.crystals) {
				const Vec base = fx.pos + crystal.base;
				const int pieces = crystal.height > 80 * fx.size ? 3 : (crystal.height > 30 * fx.size ? 2 : 1);
				// The big one breaks toward a side and its pieces fall that way like a felled column.
				const float fall = Random(0, 1) < 0.5F ? -1.F : 1.F;
				Vec out = crystal.base;
				const float length = Length(out);
				out = length > 1 ? out * (1 / length) : Vec { Random(-1, 1), Random(-0.5F, 0.5F) };
				for (int j = 0; j < pieces; j++) {
					const float y0 = crystal.height * j / pieces;
					const float y1 = crystal.height * (j + 1) / pieces;
					const float mid = (y0 + y1) / 2;
					IceChunk chunk;
					chunk.pos = base + Vec { crystal.lean.x * mid, crystal.lean.y * mid };
					chunk.z = mid;
					const float high = static_cast<float>(j + 1) / pieces;
					const float speed = Random(40, 90) * (0.5F + high);
					const Vec away = length > 1 ? out : Vec { fall, Random(-0.3F, 0.3F) };
					chunk.vel = away * speed + Vec { Random(-12, 12), Random(-6, 6) };
					chunk.vz = Random(10, 50) * high;
					chunk.angle = std::atan(crystal.lean.x);
					chunk.spin = Random(3.F, 6.F) * (away.x < 0 ? -1.F : 1.F) * (0.5F + high);
					chunk.wb = CrystalWidth(crystal, y0);
					chunk.wt = CrystalWidth(crystal, y1);
					chunk.height = y1 - y0;
					chunk.life = Random(850, 1100);
					fx.chunks.push_back(chunk);
				}
				for (int i = 0; i < Scaled(crystal.height / 30); i++) {
					const float up = Random(0, 1) * crystal.height;
					IceSplinter(base + crystal.lean * up, up, Random(40, 140), Random(40, 200), cold.core, Random(3, 5), Random(300, 550));
				}
				if (crystal.height > 70 * fx.size)
					Spray(base + Vec { 0, -crystal.height * 0.4F }, Scaled(2), 10, 40, 700, 1200, Shape::Puff, { 190, 220, 245 }, { 110, 140, 180 }, 10, 24, 0, 30, -5, 0.5F, 0.2F);
			}
			for (int i = 0; i < Scaled(20 * fx.size); i++)
				FrostFlake(fx.pos, Random(5, 90) * fx.size, 20 * fx.size);
			AddRing(fx.pos + Vec { 0, -40 * fx.size }, 0, 36 * fx.size, 120, 0, cold.mid, 0.55F, true, false);
		}
		for (IceChunk &chunk : fx.chunks) {
			chunk.age += ms;
			chunk.pos = chunk.pos + chunk.vel * dt;
			chunk.vz -= 1300 * dt;
			chunk.z += chunk.vz * dt;
			chunk.angle += chunk.spin * dt;
			const float floor = std::min(chunk.wb, chunk.height) * 0.3F;
			if (chunk.z <= floor && chunk.vz < 0) {
				// It hits the ground: one small bounce, then it lies there and slides to a stop, shedding frost.
				chunk.z = floor;
				chunk.vz = chunk.landed ? 0 : -chunk.vz * 0.22F;
				chunk.vel = chunk.vel * (chunk.landed ? 0.85F : 0.4F);
				chunk.spin *= 0.4F;
				if (!chunk.landed && chunk.height > 12)
					for (int i = 0; i < 3; i++)
						FrostFlake(chunk.pos, 2, chunk.wb * 0.5F);
				chunk.landed = true;
			}
		}
	}
	std::erase_if(IceSpikes, [](const IceSpikeFx &fx) { return fx.age >= fx.life; });
}

float Luminance(const SDL_Color &c)
{
	return 0.3F * c.r + 0.59F * c.g + 0.11F * c.b;
}

/** @brief Flame colours: bright and warm, or white hot. */
bool IsFlameColour(const SDL_Color &c)
{
	const float lum = Luminance(c);
	if (c.r > 225 && c.g > 225 && c.b > 200)
		return true;
	return lum >= 135 && c.r >= 170 && c.r >= c.b + 60;
}

/**
 * @brief Finds the flames in a sprite frame: its bright warm pixels, grouped into blobs.
 * Works on any torch, candle or fire without knowing where its art puts the flame.
 */
const std::vector<HotSpot> &FlameSpots(ClxSprite sprite)
{
	auto [it, added] = HotSpots.try_emplace(sprite.pixelData());
	std::vector<HotSpot> &spots = it->second;
	if (!added)
		return spots;
	const int width = sprite.width();
	const int height = sprite.height();
	if (width <= 0 || height <= 0)
		return spots;
	OwnedSurface canvas(width, height);
	ClxDraw(canvas, { 0, height - 1 }, sprite);

	constexpr int Cell = 6;
	const int columns = (width + Cell - 1) / Cell;
	const int rows = (height + Cell - 1) / Cell;
	struct Bin {
		int count = 0;
		float x = 0;
		float y = 0;
		int group = -1;
	};
	std::vector<Bin> bins(static_cast<size_t>(columns * rows));
	for (int y = 0; y < height; y++) {
		const uint8_t *line = canvas.at(0, y);
		for (int x = 0; x < width; x++) {
			if (line[x] == 0 || !IsFlameColour(logical_palette[line[x]]))
				continue;
			Bin &bin = bins[static_cast<size_t>((y / Cell) * columns + x / Cell)];
			bin.count++;
			bin.x += static_cast<float>(x);
			bin.y += static_cast<float>(y);
		}
	}
	// Neighbouring cells with flame in them make one flame.
	for (int start = 0; start < columns * rows; start++) {
		if (bins[start].count < 2 || bins[start].group != -1)
			continue;
		int count = 0;
		float sumX = 0;
		float sumY = 0;
		std::vector<int> open { start };
		bins[start].group = start;
		while (!open.empty()) {
			const int at = open.back();
			open.pop_back();
			count += bins[at].count;
			sumX += bins[at].x;
			sumY += bins[at].y;
			const int cx = at % columns;
			const int cy = at / columns;
			for (int dy = -1; dy <= 1; dy++) {
				for (int dx = -1; dx <= 1; dx++) {
					const int nx = cx + dx;
					const int ny = cy + dy;
					if (nx < 0 || ny < 0 || nx >= columns || ny >= rows)
						continue;
					Bin &next = bins[static_cast<size_t>(ny * columns + nx)];
					if (next.count < 2 || next.group != -1)
						continue;
					next.group = start;
					open.push_back(ny * columns + nx);
				}
			}
		}
		if (count < 3)
			continue;
		spots.push_back({ { sumX / count, sumY / count - static_cast<float>(height - 1) }, std::sqrt(static_cast<float>(count)) });
	}
	std::sort(spots.begin(), spots.end(), [](const HotSpot &a, const HotSpot &b) { return a.size > b.size; });
	if (spots.size() > 8)
		spots.resize(8);
	return spots;
}

/** @brief A flame's brightness right now: an uneven flicker, different for each flame. */
float Flicker(Vec pos, float speed = 1)
{
	const float phase = std::fmod(pos.x * 0.137F + pos.y * 0.071F, 2 * Pi);
	const float t = AmbientTime * speed;
	return 0.84F + 0.09F * std::sin(t * 0.011F + phase) + 0.07F * std::sin(t * 0.029F + phase * 1.7F);
}

/** @brief Smooth noise in 0..1 along time, from a seed: a value per step, eased between. */
float SmoothNoise(uint32_t seed, float time)
{
	const auto value = [seed](int step) {
		uint32_t h = (static_cast<uint32_t>(step) * 2654435761U) ^ (seed * 2246822519U);
		h ^= h >> 15;
		h *= 2246822519U;
		h ^= h >> 13;
		return static_cast<float>(h & 0xFFFF) / 65535.F;
	};
	const float floor = std::floor(time);
	const float f = time - floor;
	const float ease = f * f * (3 - 2 * f);
	const int step = static_cast<int>(floor);
	return value(step) + (value(step + 1) - value(step)) * ease;
}

/**
 * @brief A real flame's brightness: a slow breathing, a quicker waver and now and then a gust that dips it, never
 * in step with another flame. Candles burn steadier than torches.
 */
float NaturalFlicker(uint32_t seed, bool candle)
{
	const float t = AmbientTime;
	const float phase = static_cast<float>(seed % 628) / 100.F;
	const float breathe = std::sin(t * 0.0013F + phase);
	const float waver = SmoothNoise(seed, t * 0.009F) - 0.5F;
	const float gust = std::max(0.F, SmoothNoise(seed ^ 0x9E3779B9U, t * 0.0016F) - 0.72F) / 0.28F;
	if (candle)
		return 0.93F + 0.03F * breathe + 0.06F * waver - 0.08F * gust;
	return 0.86F + 0.05F * breathe + 0.16F * waver - 0.2F * gust;
}

bool IsCandle(const Object &object)
{
	return IsAnyOf(object._otype, OBJ_CANDLE1, OBJ_CANDLE2, OBJ_BOOKCANDLE, OBJ_STORYCANDLE, OBJ_L5CANDLE);
}

/**
 * @brief The flames of an object's whole animation: the spots found in its frames, grouped where they sit, so the
 * glow stays put while the classic sprite flickers through its frames. A candle has one flame.
 */
const std::vector<HotSpot> &SteadyFlameSpots(const Object &object)
{
	const ClxSpriteList list = *object._oAnimData;
	const ClxSprite first = list[0];
	auto [it, added] = SteadySpots.try_emplace(first.pixelData());
	std::vector<HotSpot> &steady = it->second;
	if (!added)
		return steady;
	struct Group {
		float x = 0, y = 0, size = 0;
		int frames = 0;
	};
	std::vector<Group> groups;
	const uint32_t frames = list.numSprites();
	for (uint32_t frame = 0; frame < frames; frame++) {
		const ClxSprite sprite = list[frame];
		// Frames of other widths sit differently; keep to the first frame's.
		if (sprite.width() != first.width())
			continue;
		for (const HotSpot &spot : FlameSpots(sprite)) {
			Group *match = nullptr;
			for (Group &group : groups) {
				const float gx = group.x / static_cast<float>(group.frames);
				const float gy = group.y / static_cast<float>(group.frames);
				if (std::abs(gx - spot.offset.x) < 9 && std::abs(gy - spot.offset.y) < 9) {
					match = &group;
					break;
				}
			}
			if (match == nullptr)
				match = &groups.emplace_back();
			match->x += spot.offset.x;
			match->y += spot.offset.y;
			match->size += spot.size;
			match->frames++;
		}
	}
	for (const Group &group : groups) {
		// A flame shows in most frames; a stray bright pixel now and then isn't one.
		if (group.frames * 3 < static_cast<int>(frames))
			continue;
		const float n = static_cast<float>(group.frames);
		steady.push_back({ { group.x / n, group.y / n }, group.size / n });
	}
	std::sort(steady.begin(), steady.end(), [](const HotSpot &a, const HotSpot &b) { return a.size > b.size; });
	if (IsCandle(object) && steady.size() > 1)
		steady.resize(1);
	return steady;
}

Rank RankOf(const Monster &monster)
{
	if (monster.type().type == MT_DIABLO || (monster.isUnique() && monster.uniqueType <= UniqueMonsterType::NaKrul))
		return Rank::Boss;
	if (monster.isUnique())
		return Rank::Unique;
	switch (GetMonsterAffixes(monster).rarity) {
	case MonsterRarity::Magic: return Rank::Magic;
	case MonsterRarity::Rare: return Rank::Rare;
	default: return Rank::Normal;
	}
}

/** @brief The monster that cast a missile, if a monster did. */
const Monster *CasterOf(const Missile &missile)
{
	if (missile._micaster != TARGET_PLAYERS || missile._misource < 0 || static_cast<size_t>(missile._misource) >= MaxMonsters)
		return nullptr;
	return &Monsters[missile._misource];
}

/** @brief How big a missile's effects are: the hero's full size, monsters' and traps' a little smaller, bosses' bigger. */
float CasterScale(const Missile &missile)
{
	if (missile._micaster != TARGET_PLAYERS)
		return 1;
	const Monster *caster = CasterOf(missile);
	if (caster == nullptr)
		return 0.8F;
	return RankOf(*caster) >= Rank::Unique ? 1.3F : 0.8F;
}

TrailStyle StyleOf(MissileID id)
{
	switch (id) {
	case MissileID::BloodStar: return TrailStyle::Ribbon;
	case MissileID::Acid: return TrailStyle::Drip;
	case MissileID::MagmaBall: return TrailStyle::Magma;
	case MissileID::OrangeFlare:
	case MissileID::YellowFlare:
	case MissileID::BlueFlare:
	case MissileID::BlueFlare2:
	case MissileID::RedFlare: return TrailStyle::Orb;
	default: return TrailStyle::Plain;
	}
}

bool IsFlameObject(const Object &object)
{
	switch (object._otype) {
	case OBJ_L1LIGHT:
	case OBJ_SKFIRE:
	case OBJ_CANDLE1:
	case OBJ_CANDLE2:
	case OBJ_BOOKCANDLE:
	case OBJ_STORYCANDLE:
	case OBJ_L5CANDLE:
	case OBJ_BCROSS:
	case OBJ_TBCROSS:
	case OBJ_TORCHL:
	case OBJ_TORCHR:
	case OBJ_TORCHL2:
	case OBJ_TORCHR2:
		// Their light is on while they're near enough to see (or always, for lights placed with the map).
		return object._oVar1 != 0;
	case OBJ_FLAMEHOLE:
		// The grate only shows flames while it fires.
		return true;
	default:
		return false;
	}
}

/** @brief The world's corners of the frame just drawn, with a margin. */
struct View {
	Vec min, max;

	[[nodiscard]] bool contains(Vec pos, float margin) const
	{
		return pos.x >= min.x - margin && pos.y >= min.y - margin && pos.x <= max.x + margin && pos.y <= max.y + margin;
	}
};

void ScanFlames(const View &view)
{
	for (int i = 0; i < ActiveObjectCount; i++) {
		const Object &object = Objects[ActiveObjects[i]];
		if (!IsFlameObject(object) || !object._oAnimData || object._oAnimFrame == 0)
			continue;
		const ClxSprite sprite = ObjectSprite(object);
		const Displacement offset = object.getRenderingOffset(sprite, object.position);
		const Vec corner = TileCentre(object.position) + Vec { static_cast<float>(offset.deltaX - 32), static_cast<float>(offset.deltaY + 16) };
		if (!view.contains(corner, 200))
			continue;
		const bool candle = IsCandle(object);
		uint32_t seed = (static_cast<uint32_t>(object.position.x) * 73856093U) ^ (static_cast<uint32_t>(object.position.y) * 19349663U);
		// The grate's fire comes and goes with its frames; the lights keep their flames where the whole animation has them.
		const std::vector<HotSpot> &spots = object._otype == OBJ_FLAMEHOLE ? FlameSpots(sprite) : SteadyFlameSpots(object);
		for (const HotSpot &spot : spots) {
			seed = seed * 1664525U + 1013904223U;
			Vec pos = corner + spot.offset;
			// A candle's flame sways a little.
			if (candle)
				pos.x += 0.6F * std::sin(AmbientTime * 0.0021F + static_cast<float>(seed % 628) / 100.F);
			const float flicker = NaturalFlicker(seed, candle);
			const float s = spot.size;
			Ring glow;
			glow.pos = pos;
			glow.life = 1;
			glow.width = 0;
			glow.disc = true;
			glow.ground = false;
			// A wide warm halo, then a hot core.
			glow.r0 = glow.r1 = (8 + 3.F * s) * (0.95F + 0.05F * flicker);
			glow.colour = { 255, 130, 45 };
			glow.alpha = std::min(0.34F, 0.1F + 0.025F * s) * flicker;
			FrameGlows.push_back(glow);
			glow.r0 = glow.r1 = 2 + 0.9F * s;
			glow.colour = { 255, 210, 140 };
			glow.alpha = 0.4F * flicker;
			FrameGlows.push_back(glow);
			Emitters.push_back({ pos, s, Ambient::Flame, { 255, 150, 50 } });
		}
	}
}

/** @brief The element a monster's spells look like, or nullopt for monsters that don't cast. */
std::optional<Element> CastingElement(const Monster &monster)
{
	switch (monster.ai) {
	case MonsterAIID::Counselor:
	case MonsterAIID::Lich:
	case MonsterAIID::ArchLich:
	case MonsterAIID::Psychorb:
		return Element::Arcane;
	case MonsterAIID::Succubus:
	case MonsterAIID::LazarusSuccubus:
	case MonsterAIID::Necromorb:
		return Element::Blood;
	case MonsterAIID::Storm:
		return Element::Lightning;
	case MonsterAIID::Magma:
	case MonsterAIID::Mega:
	case MonsterAIID::FireBat:
	case MonsterAIID::Torchant:
	case MonsterAIID::Diablo:
		return Element::Fire;
	case MonsterAIID::Acid:
	case MonsterAIID::AcidUnique:
	case MonsterAIID::BoneDemon:
		return Element::Chaos;
	case MonsterAIID::Lazarus:
		return Element::Holy;
	default:
		return std::nullopt;
	}
}

/** @brief The moment a monster changes what it's doing: casting, fading, charging, healing or dying. */
void OnModeChange(const Monster &monster, MonsterMode from, MonsterMode to, Vec feet, Rank rank)
{
	const float big = rank >= Rank::Unique ? 1.5F : 1.F;
	if (IsAnyOf(to, MonsterMode::RangedAttack, MonsterMode::SpecialRangedAttack, MonsterMode::FadeIn, MonsterMode::FadeOut, MonsterMode::Heal) || (to == MonsterMode::Death && rank != Rank::Normal))
		TelemetryEffect("monster", StrCat(monster.name(), " ", magic_enum::enum_name(to)), monster.position.tile.x, monster.position.tile.y);
	switch (to) {
	case MonsterMode::RangedAttack:
	case MonsterMode::SpecialRangedAttack:
		if (const std::optional<Element> element = CastingElement(monster)) {
			SigilFx sigil;
			sigil.pos = feet;
			sigil.radius = TileRadius * 0.7F * big;
			sigil.life = 650;
			sigil.colours = ColoursOf(*element);
			sigil.seed = NextRandom() | 1;
			sigil.glyphs = 6;
			sigil.star = 5;
			Sigils.push_back(sigil);
		}
		break;
	case MonsterMode::FadeIn:
	case MonsterMode::FadeOut: {
		// Shadows boiling off as it slips in or out of sight.
		Spray(feet + Vec { 0, -24 }, Scaled(10 * big), 20, 70, 500, 900, Shape::Puff, { 30, 20, 40 }, { 10, 8, 14 }, 8, 22, 10, 40, 0, 0.8F, 0.45F);
		Spray(feet + Vec { 0, -24 }, Scaled(10 * big), 30, 110, 300, 600, Shape::Dot, { 190, 150, 255 }, { 60, 30, 120 }, 2, 0.4F, 20, 90, 0, 1.5F, 0.8F);
		break;
	}
	case MonsterMode::Heal:
		HolyImpact(feet, 0.6F * big, { { 220, 255, 220 }, { 90, 230, 110 }, { 30, 120, 50 } });
		break;
	case MonsterMode::Death:
		switch (rank) {
		case Rank::Normal:
			break;
		case Rank::Magic:
			HolyImpact(feet, 0.6F, ColoursOf(Element::Arcane));
			break;
		case Rank::Rare:
			HolyImpact(feet, 1.1F, { { 255, 245, 210 }, { 255, 200, 80 }, { 190, 120, 20 } });
			AddRing(feet, 0, TileRadius * 2, 450, 3, { 255, 200, 80 }, 0.8F, false, true);
			break;
		case Rank::Unique:
		case Rank::Boss: {
			// The soul tears loose: a pillar of light, a shockwave and the floor cracking.
			const Colours c { { 255, 240, 220 }, { 255, 120, 40 }, { 150, 20, 10 } };
			AddBolt(feet + Vec { 0, -340 }, feet, 400, 7, c);
			ImpactAt(feet, Element::Fire, rank == Rank::Boss ? 2.2F : 1.5F);
			AddRing(feet, 0, TileRadius * (rank == Rank::Boss ? 5.F : 3.F), 700, 5, c.mid, 1.F, false, true);
			AddCracks(feet, TileRadius * (rank == Rank::Boss ? 2.5F : 1.6F), 7, c.mid, 4000);
			ShakeCamera(rank == Rank::Boss ? ShakeStrength::Heavy : ShakeStrength::Medium, rank == Rank::Boss ? 12 : 6);
			break;
		}
		}
		break;
	default:
		break;
	}
	if (from == MonsterMode::Charge && to != MonsterMode::Death) {
		// Slamming to a stop.
		AddRing(feet, 0, TileRadius * 1.2F * big, 300, 10, { 110, 95, 75 }, 0.45F, false, true, true);
		Spray(feet, Scaled(12 * big), 30, 110, 600, 1100, Shape::Puff, { 115, 100, 80 }, { 70, 60, 50 }, 8, 24, 10, 40, 0, 0.8F, 0.35F);
		AddCracks(feet, TileRadius * 0.8F * big, 4, { 255, 140, 50 }, 2000);
	}
}

/**
 * @brief Whether a wall stands between the tile and the camera. The effects layer is drawn over the world, so effects
 * on a monster behind a wall would shine through it (Curtis 2026-10-16); they are left out instead.
 */
bool BehindWall(Point tile)
{
	const auto solid = [](Point p) { return InDungeonBounds(p) && IsTileSolid(p); };
	return solid(tile + Displacement { 1, 1 }) || (solid(tile + Displacement { 1, 0 }) && solid(tile + Displacement { 0, 1 }));
}

/** @brief Puts a marker over each towner in town who has quest news, with a few motes rising off it. */
void ScanTowners(const View &view)
{
	if (leveltype != DTYPE_TOWN || MyPlayer == nullptr) {
		QuestMarkSince.clear();
		return;
	}
	std::unordered_map<int, float> since;
	for (const Towner &towner : Towners) {
		const QuestMarker marker = TownerQuestMarker(*MyPlayer, towner);
		if (marker == QuestMarker::None || !towner.anim)
			continue;
		const Vec feet = TileCentre(towner.position);
		if (!view.contains(feet, 150))
			continue;
		const float height = std::min(static_cast<float>(towner.currentSprite().height()), 110.F);
		const float phase = static_cast<float>(static_cast<int>(towner._ttype) * 37 % 97);
		const Vec pos = feet - Vec { 0, height * 0.8F + 24 + 3 * std::sin(AmbientTime * 0.003F + phase) };
		const bool reward = marker == QuestMarker::Reward;
		const int key = static_cast<int>(towner._ttype) * 2 + (reward ? 1 : 0);
		const auto seen = QuestMarkSince.find(key);
		const float first = seen != QuestMarkSince.end() ? seen->second : AmbientTime;
		since[key] = first;
		const float age = AmbientTime - first;
		QuestMarks.push_back({ pos, reward, phase, age });
		if (age > 500)
			Emitters.push_back({ pos - Vec { 0, 40 }, 8, Ambient::Flame, {}, 0 });
	}
	QuestMarkSince = std::move(since);
}

void ScanMonsters(const View &view)
{
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &monster = Monsters[ActiveMonsters[i]];
		const MonsterMode mode = monster.mode;
		auto [last, added] = LastModes.try_emplace(&monster, mode);
		const MonsterMode was = last->second;
		last->second = mode;
		if (monster.isPlayerMinion() || (monster.flags & MFLAG_HIDDEN) != 0 || !monster.animInfo.sprites)
			continue;
		const Point tile = monster.position.tile;
		if (!InDungeonBounds(tile) || !IsTileLit(tile))
			continue;
		Vec feet = TileCentre(tile);
		if (monster.isWalking()) {
			const Displacement walk = GetOffsetForWalking(monster.animInfo, monster.direction);
			feet = feet + Vec { static_cast<float>(walk.deltaX), static_cast<float>(walk.deltaY) };
		}
		if (!view.contains(feet, 150))
			continue;
		const Rank rank = RankOf(monster);
		if (!added && was != mode)
			OnModeChange(monster, was, mode, feet, rank);
		if (mode == MonsterMode::Death || BehindWall(tile))
			continue;
		const float height = std::min(static_cast<float>(monster.animInfo.currentSprite().height()), 128.F);
		const Vec body = feet - Vec { 0, height * 0.35F };

		if (mode == MonsterMode::Charge)
			Emitters.push_back({ feet, 1, Ambient::Charge, { 115, 100, 80 } });

		// Rank shows as a thin outline in its colour, drawn with the monster (see DrawMonsterRankOutline), not here:
		// rings and glows under them were too much (Curtis 2026-10-16). Only a few of a rare's modifiers show.
		if (rank == Rank::Rare) {
			const MonsterAffixes affixes = GetMonsterAffixes(monster);
			{
				if (affixes.has(MonsterMod::Regenerating))
					Emitters.push_back({ body, 5, Ambient::Motes, { 90, 235, 110 }, 14 });
				if (affixes.has(MonsterMod::ExtraLife))
					Emitters.push_back({ body, 4, Ambient::Motes, { 235, 40, 50 }, 14 });
				if (affixes.has(MonsterMod::HardHitting))
					Emitters.push_back({ body, 1, Ambient::Embers, { 255, 160, 60 } });
			}
		}

		// What the hero did to it.
		const Poe2MonsterState &state = monster.poe2;
		{
			const uint8_t status = (state.igniteTicks > 0 ? 1 : 0) | (state.shockTicks > 0 || state.electrocuteTicks > 0 ? 2 : 0) | (state.freezeTicks > 0 ? 4 : 0) | (state.chillTicks > 0 ? 8 : 0);
			uint8_t &was = LastStatus[&monster];
			if (status != was) {
				static constexpr std::array<std::string_view, 4> Names { "burning", "shocked", "frozen", "chilled" };
				for (size_t bit = 0; bit < Names.size(); bit++) {
					const bool now = (status & (1 << bit)) != 0;
					if (now != ((was & (1 << bit)) != 0))
						TelemetryEffect("status", StrCat(monster.name(), " ", Names[bit], now ? " starts" : " ends"), tile.x, tile.y);
				}
				was = status;
			}
		}
		Ring glow;
		glow.pos = body;
		glow.life = 1;
		glow.width = 0;
		glow.disc = true;
		glow.ground = false;
		if (state.igniteTicks > 0) {
			Emitters.push_back({ body, height, Ambient::Ignite, { 255, 140, 40 } });
			glow.r0 = glow.r1 = 20 + height * 0.1F;
			glow.colour = { 255, 110, 30 };
			glow.alpha = 0.15F * Flicker(body, 2);
			FrameGlows.push_back(glow);
		}
		if (state.shockTicks > 0 || state.electrocuteTicks > 0) {
			Emitters.push_back({ body, height, Ambient::Shock, { 170, 200, 255 } });
			glow.r0 = glow.r1 = 24;
			glow.colour = { 140, 170, 255 };
			glow.alpha = 0.18F + 0.1F * std::sin(AmbientTime * 0.05F);
			FrameGlows.push_back(glow);
		}
		if (state.freezeTicks > 0) {
			// Locked in ice.
			glow.r0 = glow.r1 = 18 + height * 0.25F;
			glow.colour = { 160, 225, 255 };
			glow.alpha = 0.4F;
			FrameGlows.push_back(glow);
			glow.r0 = glow.r1 = 8 + height * 0.1F;
			glow.colour = { 235, 250, 255 };
			glow.alpha = 0.35F;
			FrameGlows.push_back(glow);
		} else if (state.chillTicks > 0) {
			Emitters.push_back({ body, height, Ambient::Chill, { 180, 225, 255 } });
		}
		if (state.heavyStunTicks > 0) {
			// Stars circling its head.
			for (int star = 0; star < 3; star++) {
				const float angle = AmbientTime * 0.006F + star * 2 * Pi / 3;
				glow.pos = feet - Vec { 0, height * 0.85F } + Vec { std::cos(angle) * 16, std::sin(angle) * 8 };
				glow.r0 = glow.r1 = 4;
				glow.colour = { 255, 235, 140 };
				glow.alpha = 0.9F;
				FrameGlows.push_back(glow);
			}
		}
	}
}

/** @brief Lava (1) or water (2) for each palette-animated colour; 0 for neither. */
std::array<uint8_t, 32> FluidColours(int last)
{
	std::array<uint8_t, 32> kinds {};
	for (int i = 1; i <= last; i++) {
		const SDL_Color &c = logical_palette[i];
		if (c.r >= c.b + 40 && c.r >= c.g)
			kinds[i] = 1;
		else if (c.b > c.r || c.g > c.r)
			kinds[i] = 2;
	}
	return kinds;
}

void ScanFluids(const Surface &world, const View &view)
{
	int last;
	switch (leveltype) {
	case DTYPE_CAVES:
	case DTYPE_CRYPT:
		last = 31;
		break;
	case DTYPE_NEST:
		last = 15;
		break;
	default:
		// Elsewhere the low colours don't move: they're walls and floors, not lava or water.
		return;
	}
	const std::array<uint8_t, 32> kinds = FluidColours(last);
	// The samples and patches are fixed to the dungeon, not the screen, so a patch's glow doesn't flicker as the camera
	// moves over it (Curtis 2026-10-18: "it flashes weird on cells").
	constexpr int Step = 8;
	constexpr int Cell = 48;
	const auto floorDiv = [](int value, int by) { return value >= 0 ? value / by : -((-value + by - 1) / by); };
	const int scale = CameraScale > 1.5F ? 2 : 1;
	const int width = world.w();
	const int height = world.h();
	const int originX = static_cast<int>(std::floor(CameraOrigin.x));
	const int originY = static_cast<int>(std::floor(CameraOrigin.y));
	const int worldWidth = width / scale;
	const int worldHeight = height / scale;
	const int firstColumn = floorDiv(originX, Cell);
	const int firstRow = floorDiv(originY, Cell);
	const int columns = floorDiv(originX + worldWidth - 1, Cell) - firstColumn + 1;
	const int rows = floorDiv(originY + worldHeight - 1, Cell) - firstRow + 1;
	struct Patch {
		int samples = 0;
		int lava = 0;
		int water = 0;
		float light = 0;
		float r = 0, g = 0, b = 0;
	};
	std::vector<Patch> patches(static_cast<size_t>(columns * rows));
	const int firstY = (floorDiv(originY, Step) * Step) + (Step / 2);
	const int firstX = (floorDiv(originX, Step) * Step) + (Step / 2);
	for (int wy = firstY; wy < originY + worldHeight; wy += Step) {
		const int y = (wy - originY) * scale;
		if (y < 0 || y >= height)
			continue;
		const uint8_t *line = world.at(0, y);
		Patch *rowPatches = &patches[static_cast<size_t>((floorDiv(wy, Cell) - firstRow) * columns)];
		for (int wx = firstX; wx < originX + worldWidth; wx += Step) {
			const int x = (wx - originX) * scale;
			if (x < 0 || x >= width)
				continue;
			Patch &patch = rowPatches[floorDiv(wx, Cell) - firstColumn];
			patch.samples++;
			const uint8_t index = line[x];
			if (index == 0 || index > last || kinds[index] == 0)
				continue;
			const SDL_Color &c = logical_palette[index];
			(kinds[index] == 1 ? patch.lava : patch.water)++;
			patch.light += Luminance(c) / 255.F;
			patch.r += c.r;
			patch.g += c.g;
			patch.b += c.b;
		}
	}
	const float spread = Cell / 2.F;
	// Fewer, bigger patches than before keep a screen full of lava cheap to draw; their emitters make up the rate.
	constexpr float EmitterRate = 1.5F;
	const float pulseTime = AmbientTime * 0.0016F;
	for (int row = 0; row < rows; row++) {
		for (int column = 0; column < columns; column++) {
			const Patch &patch = patches[static_cast<size_t>(row * columns + column)];
			const int total = patch.lava + patch.water;
			if (total == 0)
				continue;
			const int worldColumn = firstColumn + column;
			const int worldRow = firstRow + row;
			const Vec pos { (static_cast<float>(worldColumn) + 0.5F) * Cell, (static_cast<float>(worldRow) + 0.5F) * Cell };
			if (!view.contains(pos, Cell))
				continue;
			// Of the samples on screen, so a patch half off the edge doesn't dim.
			const float share = static_cast<float>(total) / static_cast<float>(patch.samples);
			const float light = patch.light / total;
			const Rgb average { static_cast<uint8_t>(patch.r / total), static_cast<uint8_t>(patch.g / total), static_cast<uint8_t>(patch.b / total) };
			if (patch.lava >= patch.water) {
				// Molten glow that slowly breathes, spilling a little past the lava's edge. It lies on the floor, so
				// skills, monsters and the hero over the lava are drawn on top of it, not under it.
				const float pulse = 0.85F + 0.15F * std::sin(pulseTime + worldColumn * 0.9F + worldRow * 1.3F);
				Ring glow;
				glow.pos = pos;
				glow.life = 1;
				glow.width = 0;
				glow.disc = true;
				glow.ground = true;
				glow.r0 = glow.r1 = Cell * 1.05F;
				glow.colour = { 255, 90, 20 };
				glow.alpha = 0.3F * share * (0.35F + light) * pulse;
				FrameGlows.push_back(glow);
				Emitters.push_back({ pos, share * EmitterRate, Ambient::Lava, average, spread });
			} else {
				Emitters.push_back({ pos, share * EmitterRate, Ambient::Water, Mix(average, { 235, 245, 255 }, 0.55F), spread });
			}
		}
	}
}

/** @brief Somewhere random in a lava or water patch. */
Vec InPatch(const Emitter &emitter)
{
	return emitter.pos + Vec { Random(-emitter.spread, emitter.spread), Random(-emitter.spread, emitter.spread) };
}

/** @brief Turns a rate per second into how many to make this time (randomly rounded). */
int Occurrences(float perSecond, float ms)
{
	const float expected = perSecond * Intensity() * ms / 1000.F;
	int count = static_cast<int>(expected);
	if (Random(0, 1) < expected - static_cast<float>(count))
		count++;
	return count;
}

void EmitAmbient(float ms)
{
	for (const Emitter &emitter : Emitters) {
		switch (emitter.kind) {
		case Ambient::Flame: {
			const float s = emitter.size;
			for (int i = Occurrences(0.06F * s * s, ms); i > 0; i--) {
				Particle p;
				p.pos = emitter.pos + Vec { Random(-0.4F, 0.4F) * s, Random(-0.2F, 0.2F) * s };
				p.vel = { Random(-14, 14), Random(-4, 4) };
				p.vz = Random(25, 60);
				p.gravity = -18;
				p.drag = 0.6F;
				p.life = Random(700, 1500);
				p.size0 = Random(1.2F, 1.9F);
				p.size1 = 0.3F;
				p.c0 = { 255, 210, 120 };
				p.c1 = { 200, 60, 10 };
				p.alpha = 0.9F;
				AddParticle(p);
			}
			for (int i = Occurrences(0.008F * s * s, ms); i > 0; i--) {
				Particle p;
				p.pos = emitter.pos + Vec { Random(-2, 2), -0.5F * s };
				p.vel = { Random(-6, 6), 0 };
				p.vz = Random(16, 28);
				p.life = Random(1600, 2600);
				p.size0 = 3;
				p.size1 = 3 + 0.9F * s;
				p.c0 = { 38, 32, 30 };
				p.c1 = { 22, 20, 19 };
				p.alpha = 0.13F;
				p.shape = Shape::Puff;
				AddParticle(p);
			}
			break;
		}
		case Ambient::Lava: {
			const float share = emitter.size;
			for (int i = Occurrences(0.45F * share, ms); i > 0; i--) {
				Particle p;
				p.pos = InPatch(emitter);
				p.vel = { Random(-10, 10), Random(-5, 5) };
				p.vz = Random(30, 80);
				p.gravity = -10;
				p.drag = 0.4F;
				p.life = Random(900, 1900);
				p.size0 = Random(1.3F, 2.2F);
				p.size1 = 0.3F;
				p.c0 = { 255, 200, 90 };
				p.c1 = { 190, 40, 10 };
				AddParticle(p);
			}
			for (int i = Occurrences(0.07F * share, ms); i > 0; i--) {
				// A bubble bursting: a bright ring on the surface and a few spatters.
				const Vec at = InPatch(emitter);
				AddRing(at, 1, 10, 420, 1.5F, { 255, 170, 60 }, 0.6F, false, true);
				Spray(at, 3, 15, 50, 250, 500, Shape::Streak, { 255, 225, 150 }, { 220, 60, 10 }, 1.3F, 0.4F, 70, 150, 320, 1.F, 1);
			}
			for (int i = Occurrences(0.05F * share, ms); i > 0; i--) {
				Particle p;
				p.pos = InPatch(emitter);
				p.vz = Random(12, 24);
				p.life = Random(2000, 3200);
				p.size0 = 6;
				p.size1 = 22;
				p.c0 = { 45, 30, 26 };
				p.c1 = { 25, 20, 18 };
				p.alpha = 0.1F;
				p.shape = Shape::Puff;
				AddParticle(p);
			}
			break;
		}
		case Ambient::Water: {
			const float share = emitter.size;
			for (int i = Occurrences(0.6F * share, ms); i > 0; i--) {
				// Light glinting off the surface.
				Particle p;
				p.pos = InPatch(emitter);
				p.life = Random(180, 420);
				p.size0 = Random(1.F, 1.6F);
				p.size1 = 0.3F;
				p.c0 = { 235, 250, 255 };
				p.c1 = emitter.colour;
				p.alpha = 0.75F;
				AddParticle(p);
			}
			for (int i = Occurrences(0.06F * share, ms); i > 0; i--)
				AddRing(InPatch(emitter), 2, 16, 1200, 1.2F, emitter.colour, 0.2F, false, true);
			break;
		}
		case Ambient::Charge:
			for (int i = Occurrences(30, ms); i > 0; i--) {
				Particle p;
				p.pos = emitter.pos + Vec { Random(-14, 14), Random(-6, 6) };
				p.vz = Random(5, 20);
				p.life = Random(500, 900);
				p.size0 = 5;
				p.size1 = 16;
				p.c0 = { 120, 105, 85 };
				p.c1 = { 70, 60, 50 };
				p.alpha = 0.3F;
				p.shape = Shape::Puff;
				AddParticle(p);
			}
			break;
		case Ambient::Ignite:
			// A slow burn, not a blast: small flames licking over the body, a few embers and thin dark smoke.
			for (int i = Occurrences(14, ms); i > 0; i--) {
				const float h = emitter.size;
				Particle p;
				p.pos = emitter.pos + Vec { Random(-0.16F, 0.16F) * h, Random(-0.1F, 0.3F) * h };
				p.vel = { Random(-3, 3), 0 };
				p.vz = Random(12, 30);
				p.drag = 2;
				p.life = Random(160, 300);
				p.size0 = Random(2, 3.2F);
				p.size1 = 0.4F;
				p.c0 = { 255, 190, 80 };
				p.c1 = { 170, 40, 10 };
				p.alpha = 0.65F;
				AddParticle(p);
			}
			for (int i = Occurrences(3, ms); i > 0; i--) {
				const float h = emitter.size;
				Particle p;
				p.pos = emitter.pos + Vec { Random(-0.15F, 0.15F) * h, Random(-0.1F, 0.2F) * h };
				p.vel = { Random(-6, 6), 0 };
				p.vz = Random(30, 55);
				p.gravity = -15;
				p.drag = 1;
				p.life = Random(400, 700);
				p.size0 = 1.4F;
				p.size1 = 0.3F;
				p.c0 = { 255, 170, 70 };
				p.c1 = { 150, 30, 5 };
				p.shape = Shape::Streak;
				AddParticle(p);
			}
			for (int i = Occurrences(2.5F, ms); i > 0; i--) {
				const float h = emitter.size;
				Particle p;
				p.pos = emitter.pos + Vec { Random(-0.12F, 0.12F) * h, Random(-0.2F, 0.05F) * h };
				p.vel = { Random(-3, 3), 0 };
				p.vz = Random(8, 16);
				p.life = Random(700, 1100);
				p.size0 = 4;
				p.size1 = 11;
				p.c0 = { 55, 45, 38 };
				p.c1 = { 30, 28, 26 };
				p.alpha = 0.18F;
				p.shape = Shape::Puff;
				AddParticle(p);
			}
			break;
		case Ambient::Shock:
			for (int i = Occurrences(7, ms); i > 0; i--) {
				const float h = emitter.size;
				const Vec a = emitter.pos + Vec { Random(-0.25F, 0.25F) * h, Random(-0.35F, 0.35F) * h };
				const Vec b = a + Vec { Random(-14, 14), Random(-14, 14) };
				AddBolt(a, b, Random(60, 120), 1.4F, ColoursOf(Element::Lightning));
			}
			break;
		case Ambient::Chill:
			for (int i = Occurrences(10, ms); i > 0; i--) {
				const float h = emitter.size;
				Particle p;
				p.pos = emitter.pos + Vec { Random(-0.25F, 0.25F) * h, Random(-0.4F, 0.1F) * h };
				p.vel = { Random(-6, 6), Random(20, 45) };
				p.life = Random(400, 800);
				p.size0 = 2.2F;
				p.size1 = 0.5F;
				p.c0 = { 235, 248, 255 };
				p.c1 = { 120, 190, 255 };
				p.alpha = 0.8F;
				p.shape = Shape::Shard;
				AddParticle(p);
			}
			break;
		case Ambient::Motes:
			for (int i = Occurrences(emitter.size, ms); i > 0; i--) {
				Particle p;
				const float angle = Random(0, 2 * Pi);
				const float r = Random(0.3F, 1.F) * emitter.spread;
				p.pos = emitter.pos + Vec { std::cos(angle) * r, std::sin(angle) * r * 0.5F };
				p.vz = Random(25, 55);
				p.gravity = -10;
				p.life = Random(700, 1300);
				p.size0 = 1.8F;
				p.size1 = 0.3F;
				p.c0 = Mix(emitter.colour, { 255, 255, 255 }, 0.4F);
				p.c1 = emitter.colour;
				p.alpha = 0.85F;
				AddParticle(p);
			}
			break;
		case Ambient::Embers:
			for (int i = Occurrences(3, ms); i > 0; i--)
				Spray(emitter.pos + Vec { Random(-10, 10), Random(-10, 10) }, 2, 40, 110, 150, 300, Shape::Streak, { 255, 230, 160 }, { 220, 90, 20 }, 1.3F, 0.4F, 20, 90, 300, 1.5F, 1);
			break;
		}
	}
}

} // namespace

Element ElementOf(DamageType type, bool spell)
{
	switch (type) {
	case DamageType::Fire: return Element::Fire;
	case DamageType::Lightning: return Element::Lightning;
	case DamageType::Magic: return Element::Cold;
	case DamageType::Acid: return Element::Chaos;
	case DamageType::Physical: return spell ? Element::Holy : Element::Physical;
	}
	return Element::Physical;
}

bool Enabled()
{
#ifdef USE_SDL1
	return false;
#else
	return gbIsPoe2 && !HeadlessMode && *GetOptions().Graphics.modernEffects;
#endif
}

#if !defined(USE_SDL1) && !defined(USE_SDL3)
namespace {

SDL_Surface *FloorSurface = nullptr;
/** @brief Whether FloorSurface holds the floor of the frame about to be shown. */
bool FloorFresh = false;

} // namespace

bool WantsFloorCopy()
{
	return Enabled() && (!Sigils.empty() || !Rituals.empty() || !IceSpikes.empty() || !Cracks.empty() || !Rings.empty() || !FrameGlows.empty());
}

void CopyFloor(const Surface &world)
{
	if (FloorSurface == nullptr || FloorSurface->w != world.w() || FloorSurface->h != world.h()) {
		if (FloorSurface != nullptr)
			SDL_FreeSurface(FloorSurface);
		FloorSurface = SDL_CreateRGBSurfaceWithFormat(0, world.w(), world.h(), 8, SDL_PIXELFORMAT_INDEX8);
		if (FloorSurface == nullptr)
			return;
	}
	for (int y = 0; y < world.h(); y++)
		std::memcpy(static_cast<uint8_t *>(FloorSurface->pixels) + static_cast<ptrdiff_t>(y) * FloorSurface->pitch, world.at(0, y), world.w());
	FloorFresh = true;
}

Surface FloorCopy()
{
	return FloorSurface != nullptr ? Surface(FloorSurface) : Surface();
}
#else
bool WantsFloorCopy()
{
	return false;
}

void CopyFloor(const Surface &)
{
}

Surface FloorCopy()
{
	return {};
}
#endif

void Clear()
{
	Particles.clear();
	Rings.clear();
	Bolts.clear();
	Sigils.clear();
	Rituals.clear();
	Cracks.clear();
	Beams.clear();
	IceSpikes.clear();
	Sweeps.clear();
	FrameGlows.clear();
	FrameComets.clear();
	Trails.clear();
	SeenBursts.clear();
	Emitters.clear();
	HotSpots.clear();
	SteadySpots.clear();
	Halos.clear();
	QuestMarks.clear();
	QuestMarkSince.clear();
	LastModes.clear();
	LastStatus.clear();
}

void Sigil(Point tile, int radiusTiles, int durationMs, Element element)
{
	TelemetryEffect("vfx", StrCat("Sigil ", magic_enum::enum_name(element)), tile.x, tile.y);
	if (!Enabled())
		return;
	SigilFx sigil;
	sigil.pos = TileCentre(tile);
	sigil.radius = TileRadius * std::max(radiusTiles, 1);
	sigil.life = static_cast<float>(std::max(durationMs, 300)) + 350;
	sigil.colours = ColoursOf(element);
	sigil.seed = NextRandom() | 1;
	sigil.glyphs = 8 + static_cast<int>(NextRandom() % 5);
	sigil.star = 5 + static_cast<int>(NextRandom() % 3);
	Sigils.push_back(sigil);
}

void IceSpike(Point tile, int delayMs, float size)
{
	TelemetryEffect("vfx", "IceSpike", tile.x, tile.y);
	if (!Enabled())
		return;
	IceSpikeFx fx;
	fx.pos = TileCentre(tile);
	fx.wait = static_cast<float>(std::max(delayMs, 0));
	fx.size = size;
	fx.life = fx.wait + IceSpikeStand + IceSpikeShatter + IceSpikeFrost;
	// Like a cluster of grim pillars: one great pillar in the middle, a tight crown of much smaller ones packed round its
	// foot leaning a little out, and a few small shards at the edge.
	fx.crystals.push_back({ { Random(-2, 2), Random(-1, 1) }, { Random(-0.05F, 0.05F), 0 }, Random(120, 136) * size, 24 * size, 0 });
	const int crown = 8 + static_cast<int>(NextRandom() % 3);
	for (int i = 0; i < crown; i++) {
		const float angle = (2 * Pi * i) / crown + Random(-0.2F, 0.2F);
		const float out = Random(10, 17) * size;
		const Vec dir { std::cos(angle), std::sin(angle) * 0.5F };
		const float lean = Random(0.15F, 0.35F);
		fx.crystals.push_back({ dir * out, { dir.x * lean, dir.y * lean * 0.6F }, Random(24, 48) * size, Random(9, 13) * size, Random(5, 30) });
	}
	const int edge = 6 + static_cast<int>(NextRandom() % 3);
	for (int i = 0; i < edge; i++) {
		const float angle = (2 * Pi * i) / edge + Random(-0.35F, 0.35F);
		const float out = Random(20, 29) * size;
		const Vec dir { std::cos(angle), std::sin(angle) * 0.5F };
		const float lean = Random(0.4F, 0.65F);
		fx.crystals.push_back({ dir * out, { dir.x * lean, dir.y * lean * 0.6F }, Random(10, 20) * size, Random(5, 8) * size, Random(20, 45) });
	}
	// Frost grows over the ground from its foot like frost on a window: branches with feathered twigs off them.
	const int branches = 9 + static_cast<int>(NextRandom() % 3);
	for (int i = 0; i < branches; i++) {
		float angle = (2 * Pi * i) / branches + Random(-0.2F, 0.2F);
		const float length = Random(0.9F, 1.5F) * TileRadius * size;
		constexpr int Steps = 5;
		Vec at = Vec { std::cos(angle), std::sin(angle) } * (TileRadius * 0.35F * size);
		FrostLine branch { { Vec { at.x, at.y * 0.5F } }, 0 };
		for (int s = 1; s <= Steps; s++) {
			angle += Random(-0.18F, 0.18F);
			at = at + Vec { std::cos(angle), std::sin(angle) } * (length / Steps);
			branch.points.push_back({ at.x, at.y * 0.5F });
			if (s < Steps - 1) {
				for (const float side : { -1.F, 1.F }) {
					const float twig = angle + side * Random(0.85F, 1.15F);
					const float reach = length * Random(0.22F, 0.36F) * (1 - static_cast<float>(s) / Steps);
					const Vec end = at + Vec { std::cos(twig), std::sin(twig) } * reach;
					fx.frost.push_back({ { { at.x, at.y * 0.5F }, { end.x, end.y * 0.5F } }, static_cast<float>(s) / Steps });
				}
			}
		}
		fx.frost.push_back(std::move(branch));
	}
	// Draw the ones at the back first.
	std::sort(fx.crystals.begin(), fx.crystals.end(), [](const Crystal &a, const Crystal &b) { return a.base.y < b.base.y; });
	IceSpikes.push_back(std::move(fx));
	// While it waits, cold light gathers on the ground and a little mist rises.
	const Vec pos = TileCentre(tile);
	const Colours c = ColoursOf(Element::Cold);
	AddRing(pos, TileRadius * 1.2F * size, TileRadius * 0.2F * size, std::max(delayMs, 100) + 60.F, 0, c.mid, 0.35F, true, true);
	Spray(pos, Scaled(4 * size), 5, 30, 600, 1000, Shape::Puff, { 160, 200, 235 }, { 100, 130, 170 }, 8, 20, 0, 20, -5, 0.5F, 0.2F);
}

void Slam(Point tile, int radiusTiles, Element element)
{
	TelemetryEffect("vfx", StrCat("Slam ", magic_enum::enum_name(element)), tile.x, tile.y);
	if (!Enabled())
		return;
	const Vec pos = TileCentre(tile);
	const float r = TileRadius * std::max(radiusTiles, 1);
	const Colours c = ColoursOf(element);
	AddRing(pos + Vec { 0, -10 }, 0, 34, 110, 0, { 255, 235, 200 }, 0.9F, true, false);
	AddRing(pos, 0, r * 1.1F, 260, 3, c.core, 1.F, false, true, false, 0.06F);
	AddRing(pos, 0, r * 1.25F, 420, 14, { 110, 95, 75 }, 0.45F, false, true, true);
	Spray(pos, Scaled(18), 80, 240, 600, 1000, Shape::Chunk, { 75, 62, 48 }, { 40, 34, 28 }, 3.5F, 3, 150, 320, 800, 0.4F, 1);
	Spray(pos, Scaled(14), 30, 110, 700, 1300, Shape::Puff, { 115, 100, 80 }, { 70, 60, 50 }, 10, 30, 10, 40, 0, 0.8F, 0.35F);
	AddCracks(pos, r * 0.9F, 5 + static_cast<int>(NextRandom() % 4), element == Element::Physical ? Rgb { 255, 140, 50 } : c.mid, 3500);
}

void Fissure(Point from, Point to, Element element)
{
	TelemetryEffect("vfx", StrCat("Fissure ", magic_enum::enum_name(element)), from.x, from.y);
	if (!Enabled())
		return;
	const Vec a = TileCentre(from);
	const Vec b = TileCentre(to);
	const Colours c = ColoursOf(element);
	const Rgb glow = element == Element::Physical ? Rgb { 255, 140, 50 } : c.mid;
	Crack crack;
	crack.pos = a;
	crack.life = 3500;
	crack.glow = glow;
	const Vec span = b - a;
	const float length = std::max(Length(span), 1.F);
	const int steps = std::max(3, static_cast<int>(length / 12));
	Vec at {};
	for (int i = 1; i <= steps; i++) {
		const float t = static_cast<float>(i) / steps;
		const Vec next = span * t + Vec { Random(-6, 6), Random(-3, 3) };
		crack.segments.push_back({ at, next });
		if (Random(0, 1) < 0.3F)
			crack.segments.push_back({ next, next + Vec { Random(-18, 18), Random(-9, 9) } });
		at = next;
	}
	Cracks.push_back(std::move(crack));
	for (int i = 0; i <= steps; i += 2) {
		const Vec p = a + span * (static_cast<float>(i) / steps);
		Spray(p, Scaled(3), 20, 80, 500, 900, Shape::Chunk, { 75, 62, 48 }, { 40, 34, 28 }, 3, 3, 120, 260, 800, 0.4F, 1);
		Spray(p, Scaled(2), 10, 50, 600, 1100, Shape::Puff, { 115, 100, 80 }, { 70, 60, 50 }, 8, 22, 10, 30, 0, 0.8F, 0.3F);
	}
}

void Impact(Point tile, Element element, float size)
{
	TelemetryEffect("vfx", StrCat("Impact ", magic_enum::enum_name(element)), tile.x, tile.y);
	if (!Enabled())
		return;
	ImpactAt(TileCentre(tile), element, size);
}

void Sparks(Point tile, Element element, float size)
{
	TelemetryEffect("vfx", StrCat("Sparks ", magic_enum::enum_name(element)), tile.x, tile.y);
	if (!Enabled())
		return;
	SparksAt(TileCentre(tile), element, size);
}

void Cut(Point tile, Element element)
{
	TelemetryEffect("vfx", StrCat("Cut ", magic_enum::enum_name(element)), tile.x, tile.y);
	if (!Enabled())
		return;
	if (element != Element::Physical) {
		SparksAt(TileCentre(tile), element, 1.F);
		return;
	}
	// Steel biting into a body: a dull flash, a few short sparks off the edge and a spatter of blood.
	const Vec pos = TileCentre(tile) + Vec { 0, -16 };
	AddRing(pos, 0, 10, 70, 0, { 235, 225, 210 }, 0.35F, true, false);
	Spray(pos, Scaled(5), 80, 220, 70, 160, Shape::Streak, { 255, 225, 170 }, { 200, 120, 50 }, 1.2F, 0.4F, 30, 120, 500, 2.F, 0.8F);
	Spray(pos, Scaled(7), 40, 130, 300, 550, Shape::Chunk, { 110, 8, 8 }, { 45, 0, 0 }, 1.2F, 0.8F, 60, 160, 700, 0.6F, 0.9F);
}

void Arc(Point from, Point to, Element element)
{
	TelemetryEffect("vfx", StrCat("Arc ", magic_enum::enum_name(element)), from.x, from.y);
	if (!Enabled())
		return;
	AddBolt(TileCentre(from) + Vec { 0, -30 }, TileCentre(to) + Vec { 0, -30 }, 220, 3, ColoursOf(element));
}

void SkyStrike(Point tile, Element element)
{
	TelemetryEffect("vfx", StrCat("SkyStrike ", magic_enum::enum_name(element)), tile.x, tile.y);
	if (!Enabled())
		return;
	const Vec pos = TileCentre(tile);
	const Colours c = ColoursOf(element);
	AddBolt(pos + Vec { Random(-30, 30), -300 }, pos, 260, 4, c);
	AddBolt(pos + Vec { Random(-60, 60), -280 }, pos + Vec { Random(-10, 10), -40 }, 180, 2, c);
	ImpactAt(pos, element, 1.2F);
}

void Swing(Point from, Point toward, Element element, bool wide, bool heavy)
{
	TelemetryEffect("vfx", StrCat("Swing ", magic_enum::enum_name(element)), from.x, from.y);
	if (!Enabled())
		return;
	const Vec pos = TileCentre(from);
	const Vec d = TileCentre(toward) - pos;
	Sweep sweep;
	sweep.pos = pos + Vec { 0, -40 };
	sweep.facing = std::atan2(d.y * 2, d.x);
	// A heavy weapon's arc is shorter and thicker; a cleave sweeps wide.
	sweep.span = (wide ? Pi * 0.8F : Pi * 0.5F) * (heavy ? 0.8F : 1.F);
	sweep.radius = TileRadius * (wide ? 1.5F : 1.25F);
	sweep.life = heavy ? 240 : 190;
	sweep.colours = ColoursOf(element);
	sweep.element = element;
	sweep.heavy = heavy;
	Sweeps.push_back(sweep);
}

void LevelUp(Point tile)
{
	TelemetryEffect("vfx", "LevelUp", tile.x, tile.y);
	if (!Enabled())
		return;
	const Vec pos = TileCentre(tile);
	const Colours hell { { 255, 246, 220 }, { 255, 140, 35 }, { 175, 18, 8 } };
	const Colours blood { { 255, 205, 185 }, { 225, 30, 20 }, { 90, 0, 0 } };
	// A flash that whites the hero out.
	AddRing(pos + Vec { 0, -40 }, 0, 75, 140, 0, hell.core, 0.7F, true, false);
	// The pillar of hellfire, and a thinner crimson one inside it that outlasts it.
	Beams.push_back({ pos, 0, 1400, 560, 30, hell });
	Beams.push_back({ pos, 0, 1800, 640, 12, blood });
	// A circle of thorns, a pentagram and runes written into the floor under them, which then burn away.
	RitualFx ritual;
	ritual.pos = pos;
	ritual.radius = TileRadius * 3.2F;
	ritual.life = RitualBurnStart + RitualBurnMs + RitualScorchMs;
	ritual.colours = hell;
	ritual.seed = NextRandom() | 1;
	// Fifteen different runes, in a random order.
	std::array<uint8_t, RuneCount> pool;
	for (size_t i = 0; i < pool.size(); i++)
		pool[i] = static_cast<uint8_t>(i);
	for (size_t i = 0; i < ritual.runes.size(); i++) {
		std::swap(pool[i], pool[i + NextRandom() % (pool.size() - i)]);
		ritual.runes[i] = pool[i];
	}
	Rituals.push_back(ritual);
	// Shockwaves rolling out across the floor.
	AddRing(pos, 0, TileRadius * 7, 700, 6, hell.mid, 1, false, true, false, 0.02F);
	AddRing(pos, 0, TileRadius * 5, 950, 18, blood.mid, 0.5F, false, true);
	AddRing(pos, TileRadius, TileRadius * 3.5F, 850, 0, hell.edge, 0.45F, true, true);
	AddCracks(pos, TileRadius * 1.4F, 6, hell.mid, 1600);
	// A fountain of embers raining back down, and sparks blown out along the ground.
	Spray(pos + Vec { 0, -10 }, Scaled(150), 30, 250, 1000, 1900, Shape::Streak, hell.core, hell.edge, 2.6F, 0.6F, 280, 680, 520, 0.8F, 1);
	Spray(pos, Scaled(70), 160, 440, 400, 900, Shape::Streak, hell.core, blood.mid, 2, 0.5F, 30, 140, 300, 1.5F, 1);
	// Souls rising up the pillar.
	for (int i = Scaled(36); i > 0; i--) {
		Particle p;
		p.pos = pos + Vec { Random(-16, 16), Random(-8, 8) };
		p.vel = { Random(-12, 12), 0 };
		p.vz = Random(180, 520);
		p.gravity = -80;
		p.life = Random(900, 1700);
		p.size0 = 3.2F;
		p.size1 = 0.4F;
		p.c0 = hell.core;
		p.c1 = blood.mid;
		p.shape = Shape::Dot;
		AddParticle(p);
	}
	ShakeCamera(ShakeStrength::Heavy, 8);
}

void Showcase(Point tile)
{
	if (!Enabled())
		return;
	Sigil(tile + Displacement { 3, -1 }, 2, 2000, Element::Arcane);
	Slam(tile + Displacement { -2, 2 }, 2, Element::Physical);
	SkyStrike(tile + Displacement { 2, 3 }, Element::Lightning);
	Impact(tile + Displacement { -3, -2 }, Element::Fire, 1.3F);
	Impact(tile + Displacement { 4, 3 }, Element::Cold, 1.1F);
	Impact(tile + Displacement { -4, -4 }, Element::Chaos, 1.F);
	Fissure(tile + Displacement { 0, 1 }, tile + Displacement { 0, 6 }, Element::Physical);
	Swing(tile, tile + Displacement { 1, 1 }, Element::Fire, true);
	Arc(tile + Displacement { -4, 3 }, tile + Displacement { -1, 5 }, Element::Lightning);
}

void SetCamera(Point viewTile, Displacement offset, bool zoom)
{
	CameraOrigin = TileCentre(viewTile) - Vec { static_cast<float>(offset.deltaX + 32), static_cast<float>(offset.deltaY - 16) };
	CameraScale = zoom ? 2.F : 1.F;
}

void ScanMissiles()
{
	if (!Enabled())
		return;
	const int level = (setlevel ? 1000 : 0) + currlevel * 16 + static_cast<int>(leveltype);
	if (level != LevelKey) {
		Clear();
		LevelKey = level;
	}
	ScanFrame++;
	FrameGlows.clear();
	FrameComets.clear();
	for (const Missile &missile : Missiles) {
		const GraphicLook look = LookOf(missile._miAnimType);
		if (missile._mitype == MissileID::VisualEffect && look.projectile) {
			// A projectile's graphic standing still is a hit flash (steel on steel): sparks, not a flying glow.
			// Once per missile, even if it blinks out of sight and back (else the flash repeats endlessly).
			SeenBurst &seen = SeenBursts.try_emplace(&missile, SeenBurst { ScanFrame, false }).first->second;
			seen.frame = ScanFrame;
			if (missile._miDrawFlag && !seen.fired) {
				seen.fired = true;
				TelemetryEffect("hit flash", magic_enum::enum_name(missile._miAnimType), missile.position.tile.x, missile.position.tile.y);
				SparksAt(TileCentre(missile.position.tile), look.element == Element::Lightning && missile._miAnimType == MissileGraphicID::ChargedBolt ? Element::Physical : look.element, 0.8F);
			}
			continue;
		}
		const float scale = CasterScale(missile) * (missile.poe2.look == 1 ? 0.6F : 1.F);
		if (look.burst > 0) {
			SeenBurst &seen = SeenBursts.try_emplace(&missile, SeenBurst { ScanFrame, false }).first->second;
			seen.frame = ScanFrame;
			if (missile._miDrawFlag && !seen.fired) {
				seen.fired = true;
				TelemetryEffect("burst", StrCat(magic_enum::enum_name(missile._mitype), " ", magic_enum::enum_name(missile._miAnimType)), missile.position.tile.x, missile.position.tile.y);
				ImpactAt(TileCentre(missile.position.tile), look.element, look.burst * scale);
			}
			continue;
		}
		if (!look.projectile || !missile._miDrawFlag)
			continue;
		const Vec head = MissileCentre(missile);
		Trail &trail = Trails[&missile];
		if (trail.frame == 0) {
			trail.element = look.element;
			trail.arrow = IsAnyOf(missile._miAnimType, MissileGraphicID::Arrow, MissileGraphicID::FireArrow, MissileGraphicID::LightningArrow);
			trail.style = StyleOf(missile._mitype);
			trail.scale = scale;
		}
		trail.frame = ScanFrame;
		if (trail.count == 0 || Length(head - trail.points[0]) > 1.5F) {
			std::move_backward(trail.points.begin(), trail.points.end() - 1, trail.points.end());
			trail.points[0] = head;
			trail.count = std::min<int>(trail.count + 1, static_cast<int>(trail.points.size()));
		}
		const Colours c = ColoursOf(look.element);
		const bool arrow = missile._miAnimType == MissileGraphicID::Arrow;
		const bool small = missile.poe2.look == 1;
		if (!trail.arrow) {
			const Vec velocity { static_cast<float>(missile.position.velocity.deltaX), static_cast<float>(missile.position.velocity.deltaY) };
			const float speed = Length(velocity);
			const Vec dir = speed > 0.001F ? velocity * (1 / speed) : Vec { 1, 0 };
			FrameComets.push_back({ head, dir, look.element, scale, small, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&missile) >> 4) });
		}
		Ring glow;
		glow.pos = head;
		glow.r0 = glow.r1 = (arrow ? 6.F : 20.F) * scale;
		glow.life = 1;
		glow.width = 0;
		glow.colour = c.mid;
		glow.alpha = (arrow ? 0.45F : 0.7F) * std::min(scale, 1.F);
		glow.disc = true;
		glow.ground = false;
		FrameGlows.push_back(glow);
		if (trail.style == TrailStyle::Orb && !small) {
			// The orb throbs.
			glow.r0 = glow.r1 = 30 * scale * (1 + 0.3F * std::sin(AmbientTime * 0.018F + head.x * 0.05F));
			glow.alpha = 0.3F;
			FrameGlows.push_back(glow);
		}
		if (!arrow) {
			glow.r0 = glow.r1 = 7 * scale;
			glow.colour = c.core;
			glow.alpha = 1;
			FrameGlows.push_back(glow);
			// The light the projectile throws on the floor beneath it.
			glow.pos = head + Vec { 0, missile._miAnimWidth / 2.F - 16 };
			glow.r0 = glow.r1 = TileRadius * 1.6F;
			glow.colour = c.mid;
			glow.alpha = 0.22F;
			glow.ground = true;
			FrameGlows.push_back(glow);
		}
	}
	std::erase_if(SeenBursts, [](const auto &entry) { return entry.second.frame != ScanFrame; });
	// An arrow that vanished hit something (or the wall): sparks where it stopped.
	for (const auto &[missile, trail] : Trails) {
		if (trail.frame != ScanFrame && trail.arrow && trail.count > 0)
			SparksAt(trail.points[0] + Vec { 0, 16 }, trail.element, 0.6F);
	}
	std::erase_if(Trails, [](const auto &entry) { return entry.second.frame != ScanFrame; });
}

bool ReplacesSprite(const Missile &missile)
{
	if (!Enabled() || ScanFrame == 0 || ScanFrame - LastDrawnScan > 2)
		return false;
	if (missile._mitype == MissileID::VisualEffect)
		return false;
	// Arrows keep their sprite: the effects only streak and spark them.
	if (IsAnyOf(missile._miAnimType, MissileGraphicID::Arrow, MissileGraphicID::FireArrow, MissileGraphicID::LightningArrow))
		return false;
	return LookOf(missile._miAnimType).projectile;
}

ClxSprite ObjectSprite(const Object &object)
{
	const ClxSprite current = object.currentSprite();
	if (!Enabled() || object._otype == OBJ_FLAMEHOLE || !IsFlameObject(object))
		return current;
	const ClxSpriteList list = *object._oAnimData;
	const uint32_t frames = object._oAnimLen > 0 ? std::min(object._oAnimLen, list.numSprites()) : list.numSprites();
	if (frames < 2)
		return current;
	const uint32_t seed = (static_cast<uint32_t>(object.position.x) * 73856093U) ^ (static_cast<uint32_t>(object.position.y) * 19349663U);
	// A candle holds one frame: its glow sways instead of the art stepping through a loop.
	if (IsCandle(object))
		return list[seed % frames];
	// A torch runs at 80% to 125% of its art's pace, from its own starting frame.
	const float msPerFrame = 50.F * static_cast<float>(std::max(object._oAnimDelay, 1));
	const float rate = 0.8F + (0.45F * static_cast<float>(seed % 1000) / 1000.F);
	return list[(static_cast<uint32_t>(AmbientTime * rate / msPerFrame) + seed) % frames];
}

void ScanWorld(const Surface &world)
{
	Emitters.clear();
	Halos.clear();
	QuestMarks.clear();
	if (!Enabled() || world.w() <= 0 || world.h() <= 0)
		return;
	const View view { CameraOrigin, CameraOrigin + Vec { static_cast<float>(world.w()), static_cast<float>(world.h()) } * (1 / CameraScale) };
	ScanFlames(view);
	ScanFluids(world, view);
	ScanMonsters(view);
	ScanTowners(view);
}

void Advance(float ms)
{
	if (ms <= 0)
		return;
	AmbientTime += ms;
	EmitAmbient(ms);
	for (auto &[missile, trail] : Trails) {
		if (trail.count > 0)
			EmitTrail(trail, trail.points[0], ms);
	}
	AdvanceParticles(ms);
	Age(Rings, ms);
	Age(Sigils, ms);
	Age(Rituals, ms);
	Age(Beams, ms);
	Age(Cracks, ms);
	Age(Sweeps, ms);
	AdvanceIceSpikes(ms);
	for (Bolt &bolt : Bolts) {
		bolt.reseedTimer += ms;
		if (bolt.reseedTimer >= 50) {
			bolt.reseedTimer = 0;
			bolt.seed = NextRandom() | 1;
		}
	}
	Age(Bolts, ms);
}

size_t ParticleCount()
{
	return Particles.size();
}

size_t ShapeCount()
{
	return Rings.size() + Sigils.size() + Rituals.size() + Bolts.size() + Cracks.size() + Sweeps.size() + IceSpikes.size() + Beams.size();
}

size_t AmbientCount()
{
	return Emitters.size();
}

#if !defined(USE_SDL1) && !defined(USE_SDL3)
#if SDL_VERSION_ATLEAST(2, 0, 18)
namespace {

struct Batch {
	std::vector<SDL_Vertex> vertices;
	std::vector<int> indices;

	void clear()
	{
		vertices.clear();
		indices.clear();
	}

	int add(Vec p, Rgb c, float alpha)
	{
		SDL_Vertex v;
		v.position = { p.x, p.y };
		v.color = { c.r, c.g, c.b, static_cast<uint8_t>(std::clamp(alpha, 0.F, 1.F) * 255) };
		v.tex_coord = { 0, 0 };
		vertices.push_back(v);
		return static_cast<int>(vertices.size()) - 1;
	}

	void tri(int a, int b, int c)
	{
		indices.push_back(a);
		indices.push_back(b);
		indices.push_back(c);
	}
};

Batch Glowing;
Batch Dark;
/** @brief Glowing and dark shapes lying on the floor, which hide behind whatever stands or lies on it. */
Batch Ground;
Batch GroundDark;

Vec ToScreen(Vec world)
{
	return (world - CameraOrigin) * CameraScale;
}

/** @brief A line with soft edges, fading to nothing at its sides. */
void SoftLine(Batch &batch, Vec a, Vec b, float width, Rgb ca, Rgb cb, float alphaA, float alphaB)
{
	a = ToScreen(a);
	b = ToScreen(b);
	width *= CameraScale;
	const Vec d = b - a;
	const float length = Length(d);
	if (length < 0.01F)
		return;
	const Vec n = Vec { -d.y / length, d.x / length } * (width / 2);
	const int a0 = batch.add(a + n, ca, 0);
	const int a1 = batch.add(a, ca, alphaA);
	const int a2 = batch.add(a - n, ca, 0);
	const int b0 = batch.add(b + n, cb, 0);
	const int b1 = batch.add(b, cb, alphaB);
	const int b2 = batch.add(b - n, cb, 0);
	batch.tri(a0, a1, b0);
	batch.tri(a1, b1, b0);
	batch.tri(a1, a2, b1);
	batch.tri(a2, b2, b1);
}

/** @brief A soft disc, bright in the middle; flat on the ground when ground is set. */
void SoftDisc(Batch &batch, Vec centre, float radius, Rgb colour, float alpha, bool ground, float edgeAlpha = 0)
{
	const Vec c = ToScreen(centre);
	radius *= CameraScale;
	if (radius < 0.3F)
		return;
	const int segments = std::clamp(static_cast<int>(radius / 3), 10, 40);
	const int middle = batch.add(c, colour, alpha);
	const int first = static_cast<int>(batch.vertices.size());
	for (int i = 0; i < segments; i++) {
		const float angle = 2 * Pi * i / segments;
		batch.add(c + Vec { std::cos(angle) * radius, std::sin(angle) * radius * (ground ? 0.5F : 1.F) }, colour, edgeAlpha);
	}
	for (int i = 0; i < segments; i++)
		batch.tri(middle, first + i, first + (i + 1) % segments);
}

/** @brief A ring with soft edges, flat on the ground; jitter makes it jagged, dashes break it up. */
void SoftRing(Batch &batch, Vec centre, float radius, float width, Rgb colour, float alpha, float jitter, uint32_t seed, int dashes = 0, float rotation = 0)
{
	const Vec c = ToScreen(centre);
	radius *= CameraScale;
	width *= CameraScale;
	if (radius < 0.5F)
		return;
	const int segments = std::clamp(static_cast<int>(radius / 4), 16, 72);
	const int first = static_cast<int>(batch.vertices.size());
	for (int i = 0; i < segments; i++) {
		const float angle = rotation + 2 * Pi * i / segments;
		const float r = radius * (1 + (jitter > 0 ? (SeededRandom(seed) - 0.5F) * 2 * jitter : 0));
		const Vec dir { std::cos(angle), std::sin(angle) * 0.5F };
		batch.add(c + dir * (r + width / 2), colour, 0);
		batch.add(c + dir * r, colour, alpha);
		batch.add(c + dir * std::max(r - width / 2, 0.F), colour, 0);
	}
	for (int i = 0; i < segments; i++) {
		if (dashes > 0 && (i * dashes * 2 / segments) % 2 == 1)
			continue;
		const int a = first + i * 3;
		const int b = first + ((i + 1) % segments) * 3;
		batch.tri(a, a + 1, b);
		batch.tri(a + 1, b + 1, b);
		batch.tri(a + 1, a + 2, b + 1);
		batch.tri(a + 2, b + 2, b + 1);
	}
}

/** @brief A point on a ground circle around centre. */
Vec OnGround(Vec centre, float radius, float angle)
{
	return centre + Vec { std::cos(angle) * radius, std::sin(angle) * radius * 0.5F };
}

float EaseOut(float t)
{
	t = std::clamp(t, 0.F, 1.F);
	return 1 - (1 - t) * (1 - t) * (1 - t);
}

void DrawRings()
{
	for (const Ring &ring : Rings) {
		const float t = ring.age / ring.life;
		const float grow = ring.easeOut ? EaseOut(t) : t;
		const float radius = ring.r0 + (ring.r1 - ring.r0) * grow;
		// Bright at once, then fading out over its life.
		const float alpha = ring.alpha * (1 - t * t);
		Batch &batch = ring.ground ? (ring.dark ? GroundDark : Ground) : (ring.dark ? Dark : Glowing);
		if (ring.disc) {
			if (ring.dark && ring.jitter > 0) {
				// Scorch marks: an uneven dark patch.
				uint32_t seed = ring.seed;
				for (int i = 0; i < 4; i++) {
					const float angle = SeededRandom(seed) * 2 * Pi;
					SoftDisc(batch, OnGround(ring.pos, radius * 0.35F * SeededRandom(seed), angle), radius * (0.5F + 0.4F * SeededRandom(seed)), ring.colour, alpha, true);
				}
			} else {
				SoftDisc(batch, ring.pos, radius, ring.colour, alpha, ring.ground);
			}
		} else {
			SoftRing(batch, ring.pos, radius, ring.width + (ring.dark ? radius * 0.25F : 0), ring.colour, alpha, ring.jitter, ring.seed);
			if (!ring.dark)
				SoftRing(batch, ring.pos, radius, ring.width * 4, ring.colour, alpha * 0.25F, ring.jitter, ring.seed);
		}
	}
	for (const Ring &glow : FrameGlows)
		SoftDisc(glow.ground ? Ground : Glowing, glow.pos, glow.r1, glow.colour, glow.alpha, glow.ground);
}

/** @brief Projectile bodies: a white-hot head and a tail tapering back along the flight, shaped by the element. */
void DrawComets()
{
	for (const Comet &comet : FrameComets) {
		const Colours c = ColoursOf(comet.element);
		uint32_t seed = comet.seed + static_cast<uint32_t>(AmbientTime * 0.05F);
		const float s = comet.scale;
		const Vec back = comet.dir * -1;
		const Vec side { -comet.dir.y, comet.dir.x };
		switch (comet.element) {
		case Element::Cold: {
			// A long glassy shard: sharp nose, a pale core and a thin frosty tail.
			const Vec nose = comet.head + comet.dir * (10 * s);
			SoftLine(Glowing, nose, comet.head + back * (16 * s), 9 * s, c.core, c.mid, 1.F, 0.8F);
			SoftLine(Glowing, comet.head + back * (10 * s), comet.head + back * (46 * s), 6 * s, c.mid, c.edge, 0.7F, 0);
			SoftLine(Glowing, comet.head + side * (5 * s), comet.head - side * (5 * s), 5 * s, c.core, c.core, 0.5F, 0.5F);
			break;
		}
		case Element::Lightning: {
			// A crackling spark: a jagged tail that changes every frame.
			Vec at = comet.head;
			for (int i = 0; i < 4; i++) {
				const Vec next = at + back * (9 * s) + side * ((SeededRandom(seed) - 0.5F) * 10 * s);
				SoftLine(Glowing, at, next, 4 * s, c.core, c.mid, 1.F - i * 0.2F, 0.8F - i * 0.2F);
				at = next;
			}
			SoftDisc(Glowing, comet.head, 6 * s, c.core, 1, false);
			break;
		}
		default: {
			// Fire and the rest: a flickering flame licking back from a white-hot head.
			const float flicker = 0.85F + 0.3F * SeededRandom(seed);
			const float length = (comet.small ? 18.F : 34.F) * s * flicker;
			SoftLine(Glowing, comet.head, comet.head + back * length + side * ((SeededRandom(seed) - 0.5F) * 6 * s), (comet.small ? 8.F : 14.F) * s, c.mid, c.edge, 0.9F, 0);
			SoftLine(Glowing, comet.head, comet.head + back * (length * 0.55F), (comet.small ? 4.F : 7.F) * s, c.core, c.mid, 1.F, 0.2F);
			SoftDisc(Glowing, comet.head, (comet.small ? 4.F : 7.F) * s, c.core, 1, false);
			break;
		}
		}
	}
}

void DrawParticles()
{
	for (const Particle &p : Particles) {
		const float t = p.age / p.life;
		const float size = p.size0 + (p.size1 - p.size0) * t;
		const Rgb colour = Mix(p.c0, p.c1, t);
		const Vec at = p.pos - Vec { 0, p.z };
		switch (p.shape) {
		case Shape::Dot:
			SoftDisc(Glowing, at, size * 2.2F, colour, p.alpha * (1 - t) * 0.5F, false);
			SoftDisc(Glowing, at, size, colour, p.alpha * (1 - t * t), false);
			break;
		case Shape::Streak: {
			const Vec tail = at - (p.vel - Vec { 0, p.vz }) * 0.035F;
			SoftLine(Glowing, at, tail, size * 2, colour, p.c1, p.alpha * (1 - t), 0);
			break;
		}
		case Shape::Shard: {
			const Vec v = p.vel - Vec { 0, p.vz };
			const float speed = std::max(Length(v), 1.F);
			const Vec dir = v * (1 / speed);
			const Vec side { -dir.y * size * 0.35F, dir.x * size * 0.35F };
			const Vec tip = ToScreen(at + dir * size * 1.5F);
			const Vec back = at - dir * size;
			const int a = Glowing.add(tip, colour, p.alpha * (1 - t));
			const int b = Glowing.add(ToScreen(back + side), p.c1, p.alpha * (1 - t) * 0.6F);
			const int c = Glowing.add(ToScreen(back - side), p.c1, p.alpha * (1 - t) * 0.6F);
			Glowing.tri(a, b, c);
			break;
		}
		case Shape::Chunk: {
			const Vec c = ToScreen(at);
			const float h = size * CameraScale;
			const float alpha = p.alpha * std::min(1.F, (1 - t) * 3);
			const int a = Dark.add(c + Vec { -h, -h }, colour, alpha);
			const int b = Dark.add(c + Vec { h, -h * 0.6F }, colour, alpha);
			const int d = Dark.add(c + Vec { h * 0.7F, h }, colour, alpha);
			const int e = Dark.add(c + Vec { -h * 0.8F, h * 0.8F }, colour, alpha);
			Dark.tri(a, b, d);
			Dark.tri(a, d, e);
			break;
		}
		case Shape::Puff:
			SoftDisc(Dark, at, size, colour, p.alpha * std::sin(Pi * t), false, 0);
			break;
		}
	}
}

void DrawBolts()
{
	for (const Bolt &bolt : Bolts) {
		const float t = bolt.age / bolt.life;
		const float alpha = 1 - t * t;
		// Midpoint displacement: each pass halves every segment and kicks its middle sideways.
		std::vector<Vec> points { bolt.from, bolt.to };
		uint32_t seed = bolt.seed;
		float kick = Length(bolt.to - bolt.from) * 0.25F;
		for (int generation = 0; generation < 5; generation++) {
			std::vector<Vec> next;
			next.reserve(points.size() * 2);
			for (size_t i = 0; i + 1 < points.size(); i++) {
				const Vec a = points[i];
				const Vec b = points[i + 1];
				const Vec d = b - a;
				const float length = std::max(Length(d), 0.01F);
				const Vec n { -d.y / length, d.x / length };
				next.push_back(a);
				next.push_back((a + b) * 0.5F + n * ((SeededRandom(seed) - 0.5F) * 2 * kick));
			}
			next.push_back(points.back());
			points = std::move(next);
			kick *= 0.5F;
		}
		for (size_t i = 0; i + 1 < points.size(); i++) {
			SoftLine(Glowing, points[i], points[i + 1], bolt.width * 5, bolt.colours.edge, bolt.colours.edge, alpha * 0.45F, alpha * 0.45F);
			SoftLine(Glowing, points[i], points[i + 1], bolt.width, bolt.colours.core, bolt.colours.core, alpha, alpha);
		}
		SoftDisc(Glowing, bolt.from, bolt.width * 6, bolt.colours.mid, alpha * 0.8F, false);
		SoftDisc(Glowing, bolt.to, bolt.width * 6, bolt.colours.mid, alpha * 0.8F, false);
	}
}

void DrawCracks()
{
	for (const Crack &crack : Cracks) {
		const float t = crack.age / crack.life;
		// The cracks tear open quickly, glow, cool, then fade away.
		const float open = EaseOut(crack.age / 180.F);
		const float fade = t < 0.75F ? 1.F : 1 - (t - 0.75F) / 0.25F;
		const float heat = std::max(0.F, 1 - crack.age / 1600.F);
		const size_t shown = static_cast<size_t>(crack.segments.size() * open);
		for (size_t i = 0; i < shown; i++) {
			const Vec a = crack.pos + crack.segments[i][0];
			const Vec b = crack.pos + crack.segments[i][1];
			SoftLine(GroundDark, a, b, 4, { 15, 10, 8 }, { 15, 10, 8 }, 0.85F * fade, 0.85F * fade);
			if (heat > 0) {
				SoftLine(Ground, a, b, 2, crack.glow, crack.glow, heat, heat);
				SoftLine(Ground, a, b, 8, crack.glow, crack.glow, heat * 0.3F, heat * 0.3F);
			}
		}
	}
}

float EaseOutBack(float t)
{
	t = std::clamp(t, 0.F, 1.F) - 1;
	constexpr float Overshoot = 2.2F;
	return 1 + (Overshoot + 1) * t * t * t + Overshoot * t * t;
}

/** @brief The big pieces of a shattered spike: faceted slabs of ice, darker on the left face, fading as they lie. */
void DrawIceChunks(const IceSpikeFx &fx)
{
	const Colours cold = ColoursOf(Element::Cold);
	for (const IceChunk &chunk : fx.chunks) {
		const float alpha = std::clamp((chunk.life - chunk.age) / 400, 0.F, 1.F);
		if (alpha <= 0)
			continue;
		const float c = std::cos(chunk.angle);
		const float s = std::sin(chunk.angle);
		const Vec centre = chunk.pos + Vec { 0, -chunk.z };
		// Local x to the right, y up the crystal; turned by its tumble.
		const auto at = [&](float x, float y) { return centre + Vec { x * c + y * s, x * s - y * c }; };
		const float h = chunk.height / 2;
		const Vec bl = at(-chunk.wb / 2, -h);
		const Vec bm = at(chunk.wb * 0.05F, -h - chunk.wb * 0.12F);
		const Vec br = at(chunk.wb / 2, -h);
		const Vec tl = at(-chunk.wt / 2, h);
		const Vec tm = at(chunk.wt * 0.05F, h + (chunk.wt > 0.5F ? -chunk.wt * 0.12F : 0));
		const Vec tr = at(chunk.wt / 2, h);
		const Rgb shadow { 30, 75, 135 };
		const Rgb face { 80, 150, 215 };
		const Rgb lit { 175, 220, 250 };
		const auto quad = [&](Vec a, Vec b, Vec d, Vec e, Rgb ca, Rgb cb) {
			const int va = Dark.add(ToScreen(a), ca, 0.85F * alpha);
			const int vb = Dark.add(ToScreen(b), cb, 0.85F * alpha);
			const int vd = Dark.add(ToScreen(d), cb, 0.85F * alpha);
			const int ve = Dark.add(ToScreen(e), ca, 0.85F * alpha);
			Dark.tri(va, vb, vd);
			Dark.tri(va, vd, ve);
		};
		quad(bl, bm, tm, tl, shadow, face);
		quad(bm, br, tr, tm, face, lit);
		// Light along its edges, and a bright fresh break where it snapped off.
		SoftLine(Glowing, bm, tm, 1.4F, lit, lit, 0.3F * alpha, 0.45F * alpha);
		SoftLine(Glowing, br, tr, 1.6F, lit, { 255, 255, 255 }, 0.35F * alpha, 0.55F * alpha);
		if (chunk.wt > 0.5F)
			SoftLine(Glowing, tl, tr, 1.8F, { 235, 248, 255 }, { 235, 248, 255 }, 0.6F * alpha, 0.6F * alpha);
		SoftLine(Glowing, bl, br, 1.6F, cold.core, cold.core, 0.4F * alpha, 0.4F * alpha);
	}
}

void DrawIceSpikes()
{
	const Colours cold = ColoursOf(Element::Cold);
	for (const IceSpikeFx &fx : IceSpikes) {
		if (!fx.erupted)
			continue;
		const float t = fx.age - fx.wait;

		// Rime whitens the ground and frost grows out over it in feathered branches, glinting, then melts away.
		const float frost = (1 - std::clamp((t - IceSpikeStand) / IceSpikeFrost, 0.F, 1.F));
		const float grown = EaseOut(t / 260.F);
		SoftDisc(GroundDark, fx.pos, TileRadius * 1.15F * fx.size * (0.4F + 0.6F * grown), { 205, 228, 248 }, 0.3F * frost, true);
		for (size_t i = 0; i < fx.frost.size(); i++) {
			const FrostLine &line = fx.frost[i];
			const float shown = std::clamp((grown - line.from) / std::max(0.25F, 1 - line.from), 0.F, 1.F);
			if (shown <= 0)
				continue;
			const float glint = 0.75F + 0.25F * std::sin(AmbientTime * 0.007F + static_cast<float>(i) * 2.3F);
			const float span = shown * static_cast<float>(line.points.size() - 1);
			for (size_t s = 0; s + 1 < line.points.size() && static_cast<float>(s) < span; s++) {
				const float f = std::min(1.F, span - static_cast<float>(s));
				const float taper = 1 - static_cast<float>(s) / static_cast<float>(line.points.size());
				const Vec w0 = fx.pos + line.points[s];
				const Vec w1 = fx.pos + line.points[s] + (line.points[s + 1] - line.points[s]) * f;
				const bool twig = line.points.size() == 2;
				const float width = (twig ? 1.F : 1.9F) * (0.5F + 0.5F * taper);
				SoftLine(Ground, w0, w1, width * 2.6F, cold.mid, cold.mid, 0.16F * frost, 0.12F * frost);
				SoftLine(Ground, w0, w1, width, { 225, 242, 255 }, cold.core, 0.5F * frost * glint, 0.35F * frost * glint);
			}
			if (shown >= 1 && line.points.size() > 2 && std::sin(AmbientTime * 0.004F + static_cast<float>(i) * 3.1F) > 0.92F)
				SoftDisc(Ground, fx.pos + line.points.back(), 3, { 255, 255, 255 }, 0.8F * frost, false);
		}

		DrawIceChunks(fx);
		// Once it shatters the crystals are gone: their pieces carry on as chunks.
		if (fx.shattered)
			continue;
		const float gone = 0;
		const float alpha = 1;
		for (const Crystal &crystal : fx.crystals) {
			const float local = t - crystal.delay;
			if (local <= 0)
				continue;
			// They punch up fast and overshoot a little, like they were forced out of the earth.
			const float h = crystal.height * EaseOutBack(local / IceSpikeRise) * (1 - gone * 0.3F);
			const float w = crystal.width;
			const Vec base = fx.pos + crystal.base;
			const Vec up = crystal.lean + Vec { 0, -1 };
			const Vec tip = base + up * h;
			// A six-sided pillar seen from above the front: three faces show, narrowing a little, then a faceted point.
			const float neck = h * 0.74F;
			const float taper = 0.82F;
			const Vec bl = base + Vec { -w * 0.5F, -w * 0.06F };
			const Vec bm1 = base + Vec { -w * 0.17F, w * 0.17F };
			const Vec bm2 = base + Vec { w * 0.2F, w * 0.15F };
			const Vec br = base + Vec { w * 0.5F, -w * 0.04F };
			const auto raise = [&](Vec p) { return base + (p - base) * taper + up * neck; };
			const Vec tl = raise(bl);
			const Vec tm1 = raise(bm1);
			const Vec tm2 = raise(bm2);
			const Vec tr = raise(br);
			const float hot = std::max(0.F, 1 - local / 220.F);
			const Rgb shadow = Mix({ 22, 60, 120 }, cold.core, hot * 0.4F);
			const Rgb face = Mix({ 70, 145, 215 }, cold.core, hot * 0.4F);
			const Rgb lit = Mix({ 165, 215, 250 }, { 255, 255, 255 }, hot);
			const auto quad = [&](Vec a, Vec b, Vec c, Vec d, Rgb ca, Rgb cb, float al) {
				const int va = Dark.add(ToScreen(a), ca, al);
				const int vb = Dark.add(ToScreen(b), ca, al);
				const int vc = Dark.add(ToScreen(c), cb, al);
				const int vd = Dark.add(ToScreen(d), cb, al);
				Dark.tri(va, vb, vc);
				Dark.tri(va, vc, vd);
			};
			const auto tri = [&](Vec a, Vec b, Vec c, Rgb colour, float al) {
				Dark.tri(Dark.add(ToScreen(a), colour, al), Dark.add(ToScreen(b), colour, al), Dark.add(ToScreen(c), colour, al));
			};
			// The ice is see-through near the ground and clearer toward the top.
			quad(bl, bm1, tm1, tl, shadow, Mix(shadow, face, 0.3F), 0.78F * alpha);
			quad(bm1, bm2, tm2, tm1, face, Mix(face, lit, 0.35F), 0.82F * alpha);
			quad(bm2, br, tr, tm2, Mix(face, lit, 0.4F), lit, 0.8F * alpha);
			tri(tl, tm1, tip, Mix(shadow, face, 0.5F), 0.86F * alpha);
			tri(tm1, tm2, tip, Mix(face, lit, 0.6F), 0.88F * alpha);
			tri(tm2, tr, tip, lit, 0.9F * alpha);

			// Cold light inside it, and white light caught along its edges and point.
			SoftLine(Glowing, base + up * (h * 0.3F), base + up * (h * 0.85F), w * 0.5F, cold.mid, cold.core, (0.08F + hot * 0.25F) * alpha, 0.1F * alpha);
			SoftLine(Glowing, bm1, tm1, 1.6F, lit, lit, 0.15F * alpha, 0.45F * alpha);
			SoftLine(Glowing, bm2, tm2, 1.8F, lit, lit, 0.2F * alpha, 0.6F * alpha);
			SoftLine(Glowing, tm2, tip, 2, lit, { 255, 255, 255 }, 0.6F * alpha, 0.85F * alpha);
			SoftLine(Glowing, tm1, tip, 1.4F, lit, lit, 0.3F * alpha, 0.6F * alpha);
			SoftLine(Glowing, br, tr, 1.4F, cold.core, lit, 0.15F * alpha, 0.4F * alpha);
			SoftDisc(Glowing, tip, 3 + hot * 8, { 230, 248, 255 }, (0.3F + hot * 0.5F) * alpha, false);
			SoftDisc(Glowing, base, w * 0.6F, cold.mid, (0.03F + hot * 0.1F) * alpha, true);
		}
	}
}

void DrawSweeps()
{
	for (const Sweep &sweep : Sweeps) {
		const float t = sweep.age / sweep.life;
		// A crescent smear left in the air by the blade: thickest in the middle of the arc, tapered to points at
		// both ends, bright at its outer edge and fading towards the inside. It blends over the world rather than
		// glowing, so a plain steel blade reads as a cut, not an electric arc.
		const float head = EaseOut(std::min(t * 1.8F, 1.F));
		const float tailLag = sweep.heavy ? 0.85F : 0.7F;
		const float fade = 1 - t * t;
		const float thickness = sweep.radius * (sweep.heavy ? 0.5F : 0.36F);
		const bool elemental = sweep.element != Element::Physical;
		const Rgb rim = elemental ? sweep.colours.core : Rgb { 245, 245, 250 };
		const Rgb body = elemental ? sweep.colours.mid : Rgb { 175, 180, 192 };
		const float start = sweep.facing - sweep.span / 2;
		const int steps = 28;
		int previous[3] = { -1, -1, -1 };
		int previousGlow[2] = { -1, -1 };
		for (int i = 0; i <= steps; i++) {
			const float u = static_cast<float>(i) / steps;
			if (u > head + 0.001F || u < head - tailLag - 0.001F) {
				previous[0] = -1;
				previousGlow[0] = -1;
				continue;
			}
			// How far behind the blade's edge this point is (0 at the edge), and the crescent's taper.
			const float behind = std::clamp((head - u) / tailLag, 0.F, 1.F);
			const float taper = std::pow(std::sin(Pi * u), 0.7F);
			const float alpha = (1 - behind) * (1 - behind) * fade * (0.35F + 0.65F * taper);
			const float angle = start + sweep.span * u;
			const float width = std::max(thickness * taper * (1 - behind * 0.5F), 1.5F);
			const Vec outer = ToScreen(OnGround(sweep.pos, sweep.radius, angle));
			const Vec edge = ToScreen(OnGround(sweep.pos, sweep.radius - std::min(2.F, width * 0.3F), angle));
			const Vec inner = ToScreen(OnGround(sweep.pos, sweep.radius - width, angle));
			const int v[3] = { Dark.add(outer, rim, alpha * 0.85F), Dark.add(edge, rim, std::min(alpha * 1.15F, 1.F)), Dark.add(inner, body, alpha * 0.15F) };
			if (previous[0] >= 0) {
				Dark.tri(previous[0], v[0], previous[1]);
				Dark.tri(previous[1], v[0], v[1]);
				Dark.tri(previous[1], v[1], previous[2]);
				Dark.tri(previous[2], v[1], v[2]);
			}
			std::copy(std::begin(v), std::end(v), std::begin(previous));
			// A faint sheen on the edge; elemental blades glow properly in their element's colour.
			const float glow = alpha * (elemental ? 0.55F : 0.18F);
			const Vec glowInner = ToScreen(OnGround(sweep.pos, sweep.radius - width * 0.6F, angle));
			const int g[2] = { Glowing.add(edge, elemental ? sweep.colours.mid : rim, glow), Glowing.add(glowInner, sweep.colours.edge, 0) };
			if (previousGlow[0] >= 0) {
				Glowing.tri(previousGlow[0], g[0], previousGlow[1]);
				Glowing.tri(previousGlow[1], g[0], g[1]);
			}
			std::copy(std::begin(g), std::end(g), std::begin(previousGlow));
		}
	}
}

/** @brief A small procedural rune: a few strokes inside a box, the same strokes for the same seed. */
void DrawGlyph(Vec centre, float size, float angle, uint32_t seed, Rgb colour, float alpha)
{
	const Vec ax { std::cos(angle), std::sin(angle) * 0.5F };
	const Vec ay { -std::sin(angle) * 0.5F, std::cos(angle) * 0.25F };
	auto at = [&](float u, float v) { return centre + ax * (u * size) + ay * (v * size * 2); };
	const int strokes = 3 + static_cast<int>(SeededRandom(seed) * 3);
	// Points on a 3x3 grid make the strokes look carved rather than scribbled.
	auto grid = [&]() { return static_cast<float>(static_cast<int>(SeededRandom(seed) * 3)) - 1.F; };
	float u = 0;
	float v = -1;
	for (int i = 0; i < strokes; i++) {
		const float nu = grid();
		const float nv = grid();
		if (nu == u && nv == v)
			continue;
		SoftLine(Glowing, at(u, v), at(nu, nv), 2.2F, colour, colour, alpha, alpha);
		u = nu;
		v = nv;
	}
}

void DrawBeams()
{
	for (const Beam &beam : Beams) {
		const float t = beam.age / beam.life;
		// Slams down in an instant, holds, then thins out and fades from the top.
		const float in = EaseOut(beam.age / 110.F);
		const float out = 1 - std::clamp((t - 0.3F) / 0.7F, 0.F, 1.F);
		const float alpha = in * out;
		const float w = beam.width * (0.5F + 0.5F * out) * (1 + 0.08F * std::sin(beam.age * 0.04F));
		const Colours &c = beam.colours;
		// The pillar rises from just over the hero's head, so the hero stays in sight inside a faint sheath of it.
		const Vec base = beam.pos - Vec { 0, 70 };
		const Vec top = beam.pos - Vec { 0, beam.height * (0.6F + 0.4F * in) };
		SoftLine(Glowing, beam.pos, base, w * 2.2F, c.edge, c.mid, 0.12F * alpha, 0.3F * alpha);
		SoftLine(Glowing, base, top, w * 2.6F, c.edge, c.edge, 0.4F * alpha, 0);
		SoftLine(Glowing, base, top, w, c.mid, c.mid, 0.85F * alpha, 0.1F * alpha);
		SoftLine(Glowing, base, top, w * 0.25F, c.core, c.core, 0.85F * alpha, 0.2F * alpha);
		SoftDisc(Glowing, base, w * 0.9F, c.core, 0.5F * alpha, false);
		SoftDisc(Glowing, beam.pos, w * 2.2F, c.mid, 0.5F * alpha, true);
	}
}

void DrawSigils()
{
	for (const SigilFx &sigil : Sigils) {
		const float charge = sigil.life - 350;
		const float t = sigil.age / charge;
		const float draw = EaseOut(sigil.age / 450.F);
		const float exploding = sigil.age > charge ? (sigil.age - charge) / 350.F : 0.F;
		const float alpha = exploding > 0 ? 1 - exploding : 1;
		// Pulses faster as it nears the end, like a fuse.
		const float pulse = 0.75F + 0.25F * std::sin(sigil.age * (0.006F + 0.02F * std::min(t, 1.F)));
		const float r = sigil.radius * (1 + exploding * 0.15F);
		const float spin = sigil.age * 0.00035F;
		const Colours &c = sigil.colours;
		const uint32_t seed = sigil.seed;

		// The glowing floor inside the circle, brighter as it charges.
		if (sigil.floor) {
			SoftDisc(Ground, sigil.pos, r * 1.05F, c.edge, (0.10F + 0.25F * std::min(t, 1.F)) * alpha * draw, true);
			SoftDisc(GroundDark, sigil.pos, r * 0.98F, { 10, 5, 15 }, 0.3F * alpha * draw, true);
		} else {
			// Only the rim glows, so the hero in the middle isn't washed out.
			SoftRing(Ground, sigil.pos, r * 0.93F, r * 0.25F, c.edge, 0.3F * alpha * draw, 0, 1);
		}

		// Outer double ring, drawn in around the circle.
		const int segments = 64;
		const int drawn = static_cast<int>(segments * draw);
		for (int i = 0; i < drawn; i++) {
			const float a0 = spin + 2 * Pi * i / segments;
			const float a1 = spin + 2 * Pi * (i + 1) / segments;
			SoftLine(Ground, OnGround(sigil.pos, r, a0), OnGround(sigil.pos, r, a1), 3, c.mid, c.mid, pulse * alpha, pulse * alpha);
			SoftLine(Ground, OnGround(sigil.pos, r, a0), OnGround(sigil.pos, r, a1), 12, c.mid, c.mid, 0.25F * pulse * alpha, 0.25F * pulse * alpha);
			SoftLine(Ground, OnGround(sigil.pos, r * 0.86F, a0), OnGround(sigil.pos, r * 0.86F, a1), 1.6F, c.core, c.core, 0.8F * alpha, 0.8F * alpha);
		}
		// Ticks between the rings.
		for (int i = 0; i < 48 && i < drawn * 48 / segments; i += 2) {
			const float a = spin + 2 * Pi * i / 48;
			SoftLine(Ground, OnGround(sigil.pos, r * 0.88F, a), OnGround(sigil.pos, r * 0.98F, a), 1.4F, c.core, c.core, 0.7F * alpha, 0.2F * alpha);
		}
		// Runes in a band, turning the other way, each lighting up as the circle charges.
		uint32_t glyphSeed = seed;
		for (int i = 0; i < sigil.glyphs; i++) {
			const float a = -spin * 1.6F + 2 * Pi * i / sigil.glyphs;
			const float lit = std::clamp(t * sigil.glyphs - i + 1, 0.25F, 1.F);
			const uint32_t own = glyphSeed + static_cast<uint32_t>(i) * 2654435761U;
			if (draw * sigil.glyphs < i)
				continue;
			DrawGlyph(OnGround(sigil.pos, r * 0.72F, a), r * 0.09F, a + Pi / 2, own | 1, lit >= 1 ? c.core : c.mid, lit * alpha);
		}
		// A star joining points on the inner circle, drawn last.
		if (draw >= 1) {
			const float starSpin = spin * 0.6F;
			const int points = sigil.star;
			const int skip = 2;
			for (int i = 0; i < points; i++) {
				const float a0 = starSpin + 2 * Pi * i / points;
				const float a1 = starSpin + 2 * Pi * ((i + skip) % points) / points;
				SoftLine(Ground, OnGround(sigil.pos, r * 0.58F, a0), OnGround(sigil.pos, r * 0.58F, a1), 2, c.mid, c.mid, 0.85F * pulse * alpha, 0.85F * pulse * alpha);
			}
			SoftRing(Ground, sigil.pos, r * 0.58F, 1.5F, c.core, 0.6F * alpha, 0, 1);
			SoftRing(Ground, sigil.pos, r * 0.2F, 2, c.core, pulse * alpha, 0, 1);
		}
		// Motes rising off the circle while it charges.
		if (exploding == 0 && NextRandom() % 100 < static_cast<uint32_t>(25 * Intensity())) {
			Particle p;
			p.pos = OnGround(sigil.pos, r * Random(0.2F, 1.F), Random(0, 2 * Pi));
			p.vz = Random(30, 70);
			p.life = Random(600, 1000);
			p.size0 = 2;
			p.size1 = 0.5F;
			p.c0 = c.core;
			p.c1 = c.edge;
			AddParticle(p);
		}
		if (exploding > 0)
			SoftRing(Ground, sigil.pos, r * (1 + exploding * 0.6F), 6, c.core, 1 - exploding, 0, 1);
	}
}

/** @brief A rune's strokes on a 0-8 grid (x to the right, y down from the rune's top), as "x,y x,y|x,y x,y" polylines; "ox,y" is a dot. */
struct RuneShape {
	std::string_view name;
	std::string_view strokes;
};

// clang-format off
// Traced from the Diablo II rune stones: the shapes of their carved glyphs, simplified to a few strokes each.
constexpr std::array<RuneShape, RuneCount> RuneShapes { {
	{ "El", "2,1 1,2 1,5 2,7 5,7 7,6|6,0 5,2 4,4|o5,5|7,1 7,3 6,5" },
	{ "Eld", "1,2 2,1 3,2 3,3 5,5 6,6 6,7 5,7|7,1 6,0 5,1 6,2 5,4 4,5 3,6 3,7 4,8" },
	{ "Tir", "1,3 3,2 2,4 3,6 4,7 5,6|4,6 5,1 6,6|8,2 7,3 7,5 6,6" },
	{ "Nef", "6,2 6,1 2,1 2,3 4,4 4,6 3,7 2,7 2,6|o6,6" },
	{ "Eth", "5,1 3,1 4,3 3,4 4,5 3,6 4,7|6,2 6,6|8,1 6,4 8,7" },
	{ "Ith", "4,0 3,2 2,4 3,5 2,7|5,2 4,4 5,6 4,7|6,0 7,2 7,4 6,5 7,7" },
	{ "Tal", "2,2 5,2 3,7 6,7 2,2|6,2 7,0 8,1|6,8 8,5|7,8 8,7" },
	{ "Ral", "3,2 2,4 3,6 4,7|6,0 7,2 6,4 5,6|3,6 5,6|4,6 4,8" },
	{ "Ort", "4,1 3,3 2,5 3,7 5,7 6,5 5,3 4,1|3,2 2,1 1,1 1,2|5,2 6,1 7,1 7,2" },
	{ "Thul", "2,1 3,1 2,7|2,7 5,4 5,7 7,3|6,3 7,3 7,4|1,6 4,4" },
	{ "Amn", "3,1 4,0 5,1 5,2 4,3 4,4 5,5 6,6|2,3 2,6 4,8 6,8 7,6 7,4" },
	{ "Sol", "2,1 3,3 4,5 5,3 7,1|4,5 4,6|1,6 7,6|2,6 2,7|3,6 3,7|5,6 5,7|6,6 6,7" },
	{ "Shael", "4,0 4,8|3,2 6,2|4,4 3,3 2,4 3,5 4,5|5,3 6,4" },
	{ "Dol", "2,0 6,5|6,0 2,5|3,4 2,6 3,8 5,8 7,6 6,4" },
	{ "Hel", "o4,4|3,1 5,1 6,2|7,3 7,5 6,6|5,7 3,7 2,6|1,5 1,3 2,2" },
	{ "Io", "4,0 4,8|3,0 4,1 5,0|3,6 5,6|2,2 1,4 2,6|6,2 7,4 6,6" },
	{ "Lum", "2,2 3,1 5,1 7,3 7,5 5,7 3,7 2,8|2,2 2,3 3,3" },
	{ "Ko", "2,1 2,6|1,3 3,3|4,0 4,8|7,2 6,1 5,2 6,4 7,6 6,7 5,7" },
	{ "Fal", "2,0 3,1 2,2 1,1 2,0|1,3 6,3|4,1 4,7 5,8|4,6 6,5 6,3" },
	{ "Lem", "1,3 4,3|2,2 1,3 2,4|4,1 4,8|6,0 7,1 6,4 6,8" },
	{ "Pul", "4,1 3,3 4,4 5,3 4,1|2,1 2,4 4,7 6,4 6,1|4,7 4,8" },
	{ "Um", "2,1 3,3 5,2 6,1|5,1 5,5 7,6 6,7|2,4 3,6 5,4" },
	{ "Mal", "2,1 3,3 4,4 6,2 6,0|1,4 3,5 4,4|4,4 4,6|o4,7" },
	{ "Ist", "2,0 1,3 3,6 4,8|6,0 6,2 4,4 4,8|5,3 7,4 8,3" },
	{ "Gul", "2,0 4,3 7,7|4,3 1,7|3,7 4,5 5,7 3,7" },
	{ "Vex", "3,2 4,3 4,5 3,6 2,5 2,3 3,2|4,4 6,2 7,0|7,0 6,4 7,7|5,7 7,6" },
	{ "Ohm", "3,0 4,2 6,2 7,1|3,2 3,3 5,4 7,3|4,4 4,6 6,7 6,6" },
	{ "Lo", "4,0 4,3 2,6|4,3 6,6|2,1 1,3 2,4 3,3|6,1 7,3 6,4 5,3|3,8 4,6 5,8 3,8" },
	{ "Sur", "2,2 3,1 6,1 6,2|4,1 4,8|4,4 6,4 7,6 6,8" },
	{ "Ber", "3,0 3,8|7,0 7,5|3,4 7,4|5,1 4,3 6,3 5,1|7,5 6,6 7,7" },
	{ "Jah", "2,0 2,8|7,0 7,8|2,2 7,5|7,2 2,5|o4,7" },
	{ "Cham", "3,0 3,8|3,0 7,1 3,4 7,6 3,8" },
	{ "Zod", "2,1 5,1 2,5 6,5|6,0 7,1 6,2|o3,7|o5,7" },
} };
// clang-format on

using Polyline = std::vector<Vec>;

/** @brief A rune's strokes in a unit box (0 to 1, y down), read once from RuneShapes. */
const std::vector<Polyline> &RuneStrokes(size_t rune)
{
	static std::array<std::vector<Polyline>, RuneCount> cache;
	static std::array<bool, RuneCount> read {};
	std::vector<Polyline> &strokes = cache[rune];
	if (read[rune])
		return strokes;
	read[rune] = true;
	std::string_view text = RuneShapes[rune].strokes;
	Polyline line;
	while (!text.empty()) {
		const char ch = text.front();
		if (ch == '|') {
			if (line.size() >= 2)
				strokes.push_back(line);
			line.clear();
			text.remove_prefix(1);
			continue;
		}
		if (ch == ' ') {
			text.remove_prefix(1);
			continue;
		}
		const bool dot = ch == 'o';
		if (dot)
			text.remove_prefix(1);
		// "x,y" with single digits.
		if (text.size() < 3)
			break;
		const Vec at { static_cast<float>(text[0] - '0') / 8, static_cast<float>(text[2] - '0') / 8 };
		text.remove_prefix(3);
		if (!dot) {
			line.push_back(at);
			continue;
		}
		// A dot is a tiny ring.
		Polyline ring;
		for (int i = 0; i <= 6; i++)
			ring.push_back(at + Vec { std::cos(2 * Pi * i / 6), std::sin(2 * Pi * i / 6) } * 0.06F);
		strokes.push_back(ring);
	}
	if (line.size() >= 2)
		strokes.push_back(line);
	return strokes;
}

/** @brief Ground space (centred on the circle, y not yet halved for the view) to the world. */
Vec RitualToWorld(const RitualFx &fx, Vec g)
{
	return fx.pos + Vec { g.x, g.y * 0.5F };
}

Vec RitualPoint(float radius, float angle)
{
	return { std::cos(angle) * radius, std::sin(angle) * radius };
}

/** @brief How far round from the front (0) to the back (1) a point of the circle lies; the burn and the ring both run this way. */
float RitualSide(Vec g)
{
	float a = std::atan2(g.y, g.x) - Pi / 2;
	while (a > Pi)
		a -= 2 * Pi;
	while (a < -Pi)
		a += 2 * Pi;
	return std::abs(a) / Pi;
}

/**
 * @brief Whether something stands over the floor at a point of the world this frame (a hero, a towner, a chest, a
 * fountain), so that flames and sparks rising from there belong behind it. Black shadows don't count.
 */
bool FloorHidden(Vec world)
{
	if (!FloorFresh || FloorSurface == nullptr)
		return false;
	const Surface surface = WorldBackBuffer();
	if (surface.w() != FloorSurface->w || surface.h() != FloorSurface->h)
		return false;
	const Vec s = ToScreen(world);
	int hits = 0;
	for (int dy = -2; dy <= 2; dy += 2) {
		for (int dx = -2; dx <= 2; dx += 2) {
			const int x = static_cast<int>(s.x) + dx;
			const int y = static_cast<int>(s.y) + dy;
			if (x < 0 || y < 0 || x >= surface.w() || y >= surface.h())
				continue;
			const uint8_t now = *surface.at(x, y);
			const uint8_t bare = static_cast<const uint8_t *>(FloorSurface->pixels)[static_cast<ptrdiff_t>(y) * FloorSurface->pitch + x];
			if (now != bare && now != 0)
				hits++;
		}
	}
	return hits >= 3;
}

/** @brief A number from 0 to 1 that depends only on a and b, for effects that look the same every frame without state. */
float Hash01(uint32_t a, uint32_t b)
{
	uint32_t h = (a * 2654435761U) ^ ((b + 0x9E3779B9U) * 2246822519U);
	h ^= h >> 15;
	h *= 2246822519U;
	h ^= h >> 13;
	h *= 3266489917U;
	h ^= h >> 16;
	return static_cast<float>(h & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

/** @brief A soft blob, bright in the middle and fading to its edge, rx wide and ry tall. */
void SoftBlob(Batch &batch, Vec centre, float rx, float ry, Rgb colour, float alpha)
{
	const Vec c = ToScreen(centre);
	rx *= CameraScale;
	ry *= CameraScale;
	if (rx < 0.3F || ry < 0.3F || alpha <= 0)
		return;
	constexpr int Segments = 12;
	const int middle = batch.add(c, colour, alpha);
	const int first = static_cast<int>(batch.vertices.size());
	for (int i = 0; i < Segments; i++) {
		const float angle = 2 * Pi * i / Segments;
		batch.add(c + Vec { std::cos(angle) * rx, std::sin(angle) * ry }, colour, 0);
	}
	for (int i = 0; i < Segments; i++)
		batch.tri(middle, first + i, first + (i + 1) % Segments);
}

/** @brief A closed outline going clockwise from its first point, with each point's outward normal and how far round it is. */
struct Outline {
	std::vector<Vec> points;
	std::vector<Vec> normals;
	/** @brief Distance from the first point the shorter way round, as a share of half the length, 0 to 1. */
	std::vector<float> reach;

	void finish()
	{
		const size_t n = points.size();
		normals.resize(n);
		reach.resize(n);
		std::vector<float> along(n + 1, 0);
		for (size_t i = 0; i < n; i++) {
			const Vec tangent = points[(i + 1) % n] - points[(i + n - 1) % n];
			const float length = std::max(Length(tangent), 0.001F);
			normals[i] = Vec { tangent.y, -tangent.x } * (1 / length);
			along[i + 1] = along[i] + Length(points[(i + 1) % n] - points[i]);
		}
		const float total = along[n];
		for (size_t i = 0; i < n; i++)
			reach[i] = std::min(along[i], total - along[i]) / (total / 2);
	}
};

/**
 * @brief The outline as one unbroken soft band, pushed out along its normals by offset, drawn only as far round as shown
 * (0 to 1, both ways from the first point at once). waver makes its brightness crawl along it like burning.
 */
void StrokeOutline(Batch &batch, const Outline &outline, float offset, float width, Rgb colour, float alpha, float shown, uint32_t seed, float waver)
{
	const size_t n = outline.points.size();
	const int first = static_cast<int>(batch.vertices.size());
	for (size_t i = 0; i < n; i++) {
		const Vec p = outline.points[i];
		const Vec normal = outline.normals[i];
		const float a = alpha * (1 - waver + waver * SmoothNoise(seed, static_cast<float>(i) * 0.45F + AmbientTime * 0.006F));
		batch.add(ToScreen(p + normal * (offset + width / 2)), colour, 0);
		batch.add(ToScreen(p + normal * offset), colour, a);
		batch.add(ToScreen(p + normal * (offset - width / 2)), colour, 0);
	}
	for (size_t i = 0; i < n; i++) {
		const size_t j = (i + 1) % n;
		if (outline.reach[i] > shown || outline.reach[j] > shown)
			continue;
		const int a = first + static_cast<int>(i) * 3;
		const int b = first + static_cast<int>(j) * 3;
		batch.tri(a, a + 1, b);
		batch.tri(a + 1, b + 1, b);
		batch.tri(a + 1, a + 2, b + 1);
		batch.tri(a + 2, b + 2, b + 1);
	}
}

/** @brief Where flames rise off an outline: a point, the way out, how likely a flame starts there and how tall it burns. */
struct FlameSite {
	Vec pos;
	Vec out;
	float weight;
	float height;
};

/**
 * @brief Fire licking up off a line of sites: many small flamelets, each born on the line, rising and swaying with its
 * neighbours so they gather into tongues, white hot at the root and reddening as they thin out at the top.
 */
void DrawFire(const std::vector<FlameSite> &sites, int count, float scale, uint32_t seed, Rgb heart, Rgb body, Rgb tip, float strength, Batch &batch = Glowing)
{
	if (sites.empty() || strength <= 0)
		return;
	float total = 0;
	for (const FlameSite &site : sites)
		total += site.weight;
	const float t = AmbientTime;
	for (int i = 0; i < count; i++) {
		const uint32_t id = seed * 977U + static_cast<uint32_t>(i);
		const float life = 420 + 380 * Hash01(id, 1);
		const float clock = (t + Hash01(id, 2) * life) / life;
		const auto cycle = static_cast<uint32_t>(clock);
		const float a = clock - static_cast<float>(cycle);
		// Pick where this flamelet is born this time round, weighted toward the top.
		float pick = Hash01(id, cycle * 4 + 5) * total;
		size_t s = 0;
		while (s + 1 < sites.size() && pick > sites[s].weight) {
			pick -= sites[s].weight;
			s++;
		}
		const FlameSite &site = sites[s];
		const FlameSite &next = sites[(s + 1) % sites.size()];
		const float between = Hash01(id, cycle * 4 + 6);
		const Vec root = site.pos + (next.pos - site.pos) * between;
		Vec dir = site.out * 0.55F + Vec { 0, -1 };
		dir = dir * (1 / Length(dir));
		// Columns of fire swell and die down slowly, so the top breaks into separate tongues of different heights.
		const auto column = static_cast<uint32_t>(static_cast<int>(root.x / (2.5F * scale)) + 4096);
		const float swell = 0.45F + 1.05F * SmoothNoise(seed * 53U + column, t * 0.0028F);
		const float height = (17 + 22 * Hash01(id, cycle * 4 + 7)) * scale * site.height * swell;
		const float rise = std::pow(a, 1.15F);
		// Neighbouring flamelets sway together, so they read as tongues rather than sparks.
		const float sway = (SmoothNoise(seed * 31U + column, t * 0.006F - a * 2.2F) - 0.5F) * 12 * scale * a;
		const Vec pos = root + site.out * (0.6F * scale) + dir * (height * rise) + Vec { sway, 0 };
		const float size = (1.9F + 1.2F * Hash01(id, 3)) * scale * (1 - a * 0.7F) * std::min(1.F, 0.45F + a * 6);
		Rgb colour;
		if (a < 0.22F)
			colour = Mix(heart, body, a / 0.22F);
		else if (a < 0.6F)
			colour = Mix(body, tip, (a - 0.22F) / 0.38F);
		else
			colour = tip;
		const float alpha = 0.36F * strength * std::pow(1 - a, 1.3F) * std::min(1.F, a / 0.06F);
		SoftBlob(batch, pos, size * 0.85F, size * 2.1F, colour, alpha);
	}
}

/** @brief One line of the circle: glowing, white-hot just before the burning edge reaches it, then a fading scorch mark. */
void RitualLine(const RitualFx &fx, Vec a, Vec b, float width, Rgb colour, float alpha)
{
	const Colours &c = fx.colours;
	const int pieces = std::max(1, static_cast<int>(Length(b - a) / 10));
	for (int i = 0; i < pieces; i++) {
		const Vec p0 = a + (b - a) * (static_cast<float>(i) / pieces);
		const Vec p1 = a + (b - a) * (static_cast<float>(i + 1) / pieces);
		const float since = fx.age - (RitualBurnStart + RitualSide((p0 + p1) * 0.5F) * RitualBurnMs);
		const Vec w0 = RitualToWorld(fx, p0);
		const Vec w1 = RitualToWorld(fx, p1);
		if (since > 0) {
			const float scorch = 0.75F * (1 - since / RitualScorchMs);
			if (scorch > 0)
				SoftLine(GroundDark, w0, w1, width * 2.2F, { 22, 12, 8 }, { 22, 12, 8 }, scorch, scorch);
			const float embers = 1 - since / 160;
			if (embers > 0) {
				SoftLine(Ground, w0, w1, width * 1.6F, c.mid, c.edge, embers * alpha, embers * alpha);
				if (NextRandom() % 100 < static_cast<uint32_t>(10 * Intensity()) && !FloorHidden(w0)) {
					Particle p;
					p.pos = w0;
					p.vel = { Random(-14, 14), Random(-6, 6) };
					p.vz = Random(40, 120);
					p.gravity = -30;
					p.life = Random(400, 900);
					p.size0 = 2.2F;
					p.size1 = 0.3F;
					p.c0 = c.core;
					p.c1 = c.edge;
					AddParticle(p);
				}
			}
			continue;
		}
		// White-hot as the burning edge comes near.
		const float heat = std::clamp(1 + since / 110, 0.F, 1.F);
		SoftLine(Ground, w0, w1, width * (1 + heat), heat > 0.5F ? c.core : colour, heat > 0.5F ? c.core : colour, alpha, alpha);
		SoftLine(Ground, w0, w1, width * 4, colour, colour, alpha * (0.22F + 0.3F * heat), alpha * (0.22F + 0.3F * heat));
	}
}

/** @brief The bright tip of whatever is drawing the circle's lines, throwing a spark now and then. */
void RitualPen(const RitualFx &fx, Vec g, float size)
{
	const Vec w = RitualToWorld(fx, g);
	SoftDisc(Ground, w, size * 3, fx.colours.mid, 0.6F, true);
	SoftDisc(Ground, w, size, fx.colours.core, 1, false);
	if (NextRandom() % 100 < static_cast<uint32_t>(30 * Intensity()) && !FloorHidden(w)) {
		Particle p;
		p.pos = w;
		p.vel = { Random(-60, 60), Random(-30, 30) };
		p.vz = Random(40, 140);
		p.gravity = 260;
		p.life = Random(200, 450);
		p.size0 = 1.8F;
		p.size1 = 0.3F;
		p.c0 = fx.colours.core;
		p.c1 = fx.colours.mid;
		p.shape = Shape::Streak;
		AddParticle(p);
	}
}

/** @brief A ring of the circle, drawn in from the front round both sides to the back as shown goes from 0 to 1. */
void RitualRing(const RitualFx &fx, float radius, float shown, float width, Rgb colour, float alpha, bool pen)
{
	if (shown <= 0)
		return;
	const int segments = 72;
	for (int i = 0; i < segments; i++) {
		const float a0 = Pi / 2 + 2 * Pi * i / segments;
		const float a1 = Pi / 2 + 2 * Pi * (i + 1) / segments;
		const Vec p0 = RitualPoint(radius, a0);
		const Vec p1 = RitualPoint(radius, a1);
		if (RitualSide((p0 + p1) * 0.5F) <= shown)
			RitualLine(fx, p0, p1, width, colour, alpha);
	}
	if (pen && shown < 1) {
		RitualPen(fx, RitualPoint(radius, Pi / 2 + shown * Pi), width);
		RitualPen(fx, RitualPoint(radius, Pi / 2 - shown * Pi), width);
	}
}

/** @brief A polyline drawn up to a share of its length (0 to 1), with the pen at its end while it's being drawn. */
void RitualWrite(const RitualFx &fx, const std::vector<Polyline> &lines, float progress, float width, Rgb colour, float alpha)
{
	float total = 0;
	for (const Polyline &line : lines) {
		for (size_t i = 1; i < line.size(); i++)
			total += Length(line[i] - line[i - 1]);
	}
	float left = total * std::clamp(progress, 0.F, 1.F);
	for (const Polyline &line : lines) {
		for (size_t i = 1; i < line.size(); i++) {
			if (left <= 0)
				return;
			const float length = Length(line[i] - line[i - 1]);
			if (left >= length) {
				RitualLine(fx, line[i - 1], line[i], width, colour, alpha);
				left -= length;
				continue;
			}
			const Vec end = line[i - 1] + (line[i] - line[i - 1]) * (left / length);
			RitualLine(fx, line[i - 1], end, width, colour, alpha);
			if (progress < 1)
				RitualPen(fx, end, width * 0.8F);
			return;
		}
	}
}

/** @brief How many cracks hellfire breaks through the Horadric seal, when the first opens and how far apart they come. */
constexpr int SealCracks = 7;
constexpr float SealCrackStart = 1350;
constexpr float SealCrackSpread = 480;
constexpr float SealCrackGrowMs = 220;
/** @brief How long the fire takes to run round the band from a crack to the next one, in ms. */
constexpr float SealFireSpreadMs = 650;

/** @brief Where crack k of a ritual's seal crosses the band, and when it opens. */
float SealCrackAngle(const RitualFx &fx, int k)
{
	uint32_t seed = fx.seed * 2654435761U + static_cast<uint32_t>(k) * 97U + 1;
	return Pi / 2 + 2 * Pi * (static_cast<float>(k) + 0.5F) / SealCracks + (SeededRandom(seed) - 0.5F) * 0.5F;
}

float SealCrackTime(float angle)
{
	return SealCrackStart + RitualSide(RitualPoint(1, angle)) * SealCrackSpread;
}

/** @brief A crack across the band in ground space: a jagged line from the inner edge to the outer one, and a branch along it. */
std::array<Polyline, 2> SealCrack(const RitualFx &fx, int k, float inner, float outer)
{
	uint32_t seed = fx.seed * 40503U + static_cast<uint32_t>(k) * 7919U + 3;
	const float angle = SealCrackAngle(fx, k);
	std::array<Polyline, 2> lines;
	constexpr int Steps = 5;
	for (int i = 0; i <= Steps; i++) {
		const float f = static_cast<float>(i) / Steps;
		const float a = angle + (SeededRandom(seed) - 0.5F) * 0.07F;
		lines[0].push_back(RitualPoint(inner - 3 + (outer - inner + 6) * f, a));
	}
	const Vec middle = lines[0][Steps / 2];
	float a = std::atan2(middle.y, middle.x);
	float rad = Length(middle);
	const float way = SeededRandom(seed) < 0.5F ? -1.F : 1.F;
	lines[1].push_back(middle);
	for (int i = 0; i < 4; i++) {
		a += way * (0.04F + SeededRandom(seed) * 0.04F);
		rad = std::clamp(rad + (SeededRandom(seed) - 0.5F) * (outer - inner) * 0.5F, inner + 2, outer - 2);
		lines[1].push_back(RitualPoint(rad, a));
	}
	return lines;
}

/** @brief A small mark carved into the seal, strokes on a 3x3 grid like the Horadrim's script, in ground space. */
std::vector<Polyline> SealMark(uint32_t seed, Vec centre, Vec along, Vec across, float size)
{
	std::vector<Polyline> lines;
	const int strokes = 3 + static_cast<int>(SeededRandom(seed) * 2.99F);
	auto grid = [&]() { return static_cast<float>(static_cast<int>(SeededRandom(seed) * 2.99F)) - 1.F; };
	float u = grid();
	float v = -1;
	Polyline line { centre + along * (u * size) + across * (v * size) };
	for (int i = 0; i < strokes; i++) {
		const float nu = grid();
		const float nv = grid();
		if (nu == u && nv == v)
			continue;
		u = nu;
		v = nv;
		line.push_back(centre + along * (u * size) + across * (v * size));
	}
	lines.push_back(line);
	// A short bar across most marks, as the Horadric glyphs have.
	if (SeededRandom(seed) < 0.6F)
		lines.push_back({ centre + along * (-0.6F * size) + across * (0.25F * size), centre + along * (0.6F * size) + across * (0.25F * size) });
	return lines;
}

/**
 * @brief The outer band of the level-up circle: a plain stone ring carved with Horadric marks, like the seals the Horadrim
 * set over Diablo. It is laid from the front round both sides, its marks lit pale blue as it holds; then hellfire cracks it
 * open from inside, bursts up through each crack and runs round the band until the whole ring burns, and the burn takes it.
 */
void DrawHoradricSeal(RitualFx &fx, float laid)
{
	const Colours &c = fx.colours;
	const float r = fx.radius;
	const float age = fx.age;
	const float inner = r * 0.94F;
	const float outer = r * 1.1F;
	const float middle = (inner + outer) / 2;
	const Rgb stone { 52, 48, 46 };
	const Rgb rim { 112, 104, 96 };
	const Rgb groove { 12, 10, 10 };
	const Rgb horadric { 150, 185, 255 };

	std::array<float, SealCracks> crackAngle;
	std::array<float, SealCracks> crackTime;
	for (int k = 0; k < SealCracks; k++) {
		crackAngle[k] = SealCrackAngle(fx, k);
		crackTime[k] = SealCrackTime(crackAngle[k]);
	}
	const auto burnSince = [&](Vec g) { return age - (RitualBurnStart + RitualSide(g) * RitualBurnMs); };
	// How long a point of the band has been burning: fire spreads both ways from each crack once it opens.
	const auto burning = [&](float angle) {
		float best = -1;
		for (int k = 0; k < SealCracks; k++) {
			float d = std::abs(angle - crackAngle[k]);
			d = std::min(d, 2 * Pi - std::fmod(d, 2 * Pi));
			const float reached = crackTime[k] + SealCrackGrowMs * 0.6F + d / (2 * Pi / SealCracks) * SealFireSpreadMs;
			best = std::max(best, age - reached);
		}
		return best;
	};

	// The stone band, laid round from the front, crumbling away as the burn goes past.
	constexpr int Segments = 96;
	const int band = static_cast<int>(GroundDark.vertices.size());
	for (int i = 0; i <= Segments; i++) {
		const float a = Pi / 2 + 2 * Pi * i / Segments;
		GroundDark.add(ToScreen(RitualToWorld(fx, RitualPoint(inner, a))), stone, 0);
		GroundDark.add(ToScreen(RitualToWorld(fx, RitualPoint(middle, a))), stone, 0);
		GroundDark.add(ToScreen(RitualToWorld(fx, RitualPoint(outer, a))), stone, 0);
	}
	for (int i = 0; i < Segments; i++) {
		const float a = Pi / 2 + 2 * Pi * (i + 0.5F) / Segments;
		const Vec g = RitualPoint(middle, a);
		if (RitualSide(g) > laid)
			continue;
		const float crumble = std::clamp(burnSince(g) / 500, 0.F, 1.F);
		const float alpha = 0.88F * (1 - crumble);
		for (int v = 0; v < 3; v++) {
			GroundDark.vertices[band + i * 3 + v].color.a = static_cast<uint8_t>(alpha * (v == 1 ? 255 : 200));
			GroundDark.vertices[band + (i + 1) * 3 + v].color.a = static_cast<uint8_t>(alpha * (v == 1 ? 255 : 200));
		}
		const int p = band + i * 3;
		const int q = band + (i + 1) * 3;
		GroundDark.tri(p, p + 1, q);
		GroundDark.tri(p + 1, q + 1, q);
		GroundDark.tri(p + 1, p + 2, q + 1);
		GroundDark.tri(p + 2, q + 2, q + 1);
		// Its carved edges.
		const float a0 = Pi / 2 + 2 * Pi * i / Segments;
		const float a1 = Pi / 2 + 2 * Pi * (i + 1) / Segments;
		for (const float edge : { inner, outer }) {
			const Vec w0 = RitualToWorld(fx, RitualPoint(edge, a0));
			const Vec w1 = RitualToWorld(fx, RitualPoint(edge, a1));
			SoftLine(GroundDark, w0, w1, 2.2F, rim, rim, 0.75F * (1 - crumble), 0.75F * (1 - crumble));
		}
	}

	// Horadric marks cut round it, lit pale blue while the seal holds, turning to embers as the fire takes them.
	constexpr int Marks = 22;
	for (int m = 0; m < Marks; m++) {
		const float a = Pi / 2 + 2 * Pi * (m + 0.5F) / Marks;
		const Vec g = RitualPoint(middle, a);
		if (RitualSide(g) > laid)
			continue;
		const float crumble = std::clamp(burnSince(g) / 400, 0.F, 1.F);
		if (crumble >= 1)
			continue;
		const Vec across = RitualPoint(1, a);
		const Vec along { -across.y, across.x };
		const float lit = burning(a);
		const float hold = std::clamp((age - RitualSide(g) * RitualRingMs) / 250, 0.F, 1.F) * (lit > 0 ? std::max(0.F, 1 - lit / 250) : 1.F);
		const float heat = lit > 0 ? std::min(1.F, lit / 250) : 0.F;
		for (const Polyline &line : SealMark(fx.seed + static_cast<uint32_t>(m) * 131U, g, along, across, (outer - inner) * 0.24F)) {
			for (size_t s = 0; s + 1 < line.size(); s++) {
				const Vec w0 = RitualToWorld(fx, line[s]);
				const Vec w1 = RitualToWorld(fx, line[s + 1]);
				SoftLine(GroundDark, w0, w1, 2.4F, groove, groove, 0.8F * (1 - crumble), 0.8F * (1 - crumble));
				if (hold > 0)
					SoftLine(Ground, w0, w1, 2.2F, horadric, horadric, 0.45F * hold, 0.45F * hold);
				if (heat > 0)
					SoftLine(Ground, w0, w1, 2, c.mid, c.core, 0.6F * heat * (1 - crumble), 0.6F * heat * (1 - crumble));
			}
		}
	}

	// The cracks: black splits in the stone with hellfire glaring up through them, growing as they open.
	std::vector<FlameSite> back;
	std::vector<FlameSite> front;
	for (int k = 0; k < SealCracks; k++) {
		const float open = (age - crackTime[k]) / SealCrackGrowMs;
		if (open <= 0)
			continue;
		const float crumble = std::clamp(burnSince(RitualPoint(middle, crackAngle[k])) / 500, 0.F, 1.F);
		const float fade = 1 - crumble;
		const float flash = std::clamp(1 - (open - 1) / 2.5F, 0.F, 1.F);
		const std::array<Polyline, 2> crack = SealCrack(fx, k, inner, outer);
		for (int l = 0; l < 2; l++) {
			const Polyline &line = crack[l];
			const float shown = std::clamp(l == 0 ? open : open * 1.6F - 0.6F, 0.F, 1.F) * static_cast<float>(line.size() - 1);
			for (size_t s = 0; s + 1 < line.size() && static_cast<float>(s) < shown; s++) {
				const float f = std::min(1.F, shown - static_cast<float>(s));
				const Vec w0 = RitualToWorld(fx, line[s]);
				const Vec w1 = RitualToWorld(fx, line[s] + (line[s + 1] - line[s]) * f);
				const float width = l == 0 ? 3.2F : 2.2F;
				SoftLine(GroundDark, w0, w1, width * 1.8F, groove, groove, 0.9F * fade, 0.9F * fade);
				SoftLine(Ground, w0, w1, width * 4, c.edge, c.edge, (0.3F + 0.4F * flash) * fade, (0.3F + 0.4F * flash) * fade);
				SoftLine(Ground, w0, w1, width, c.mid, c.core, (0.8F + 0.2F * flash) * fade, (0.8F + 0.2F * flash) * fade);
			}
		}
		const Vec at = RitualToWorld(fx, RitualPoint(Random(inner, outer), crackAngle[k] + Random(-0.04F, 0.04F)));
		if (open < 1.3F && NextRandom() % 100 < static_cast<uint32_t>(60 * Intensity()) && !FloorHidden(at)) {
			// Chips of stone and sparks thrown up as it splits.
			Spray(at, 1, 20, 70, 400, 700, Shape::Chunk, { 90, 84, 78 }, { 40, 36, 34 }, 2.2F, 1.6F, 120, 260, 700, 0.4F, 1);
			Spray(at, 2, 10, 50, 300, 600, Shape::Streak, c.core, c.edge, 2.2F, 0.4F, 60, 160, -20, 0.5F, 1);
		}
	}

	// Fire on the band: up through each crack first, then running round until the whole ring burns, dying where the
	// burn has gone past. Flames at the back hide behind whoever stands in front of them.
	constexpr int Sites = 120;
	for (int i = 0; i < Sites; i++) {
		const float a = Pi / 2 + 2 * Pi * (i + 0.5F) / Sites;
		const float lit = burning(a);
		if (lit <= 0)
			continue;
		const Vec g = RitualPoint(middle, a);
		const float since = burnSince(g);
		const float life = std::min(1.F, lit / 200) * (since > 0 ? std::max(0.F, 1 - since / 350) : 1.F);
		if (life <= 0)
			continue;
		// Taller where a crack has just burst.
		float burst = 0;
		for (int k = 0; k < SealCracks; k++) {
			float d = std::abs(a - crackAngle[k]);
			d = std::min(d, 2 * Pi - std::fmod(d, 2 * Pi));
			const float near = std::max(0.F, 1 - d / 0.25F);
			const float fresh = std::clamp(1 - (age - crackTime[k] - SealCrackGrowMs) / 900, 0.F, 1.F);
			burst = std::max(burst, near * fresh);
		}
		// Flames at the back, or rising from behind someone or something standing on the ring, go behind them.
		const FlameSite site { RitualToWorld(fx, g), { 0, 0 }, life, (0.7F + 1.1F * burst) * life };
		(std::sin(a) < 0 || FloorHidden(site.pos) ? back : front).push_back(site);
	}
	DrawFire(back, 300, 1.7F, fx.seed, { 255, 235, 160 }, c.mid, c.edge, 1.2F, Ground);
	DrawFire(front, 300, 1.7F, fx.seed + 1, { 255, 235, 160 }, c.mid, c.edge, 1.2F, Glowing);
}

void DrawRituals()
{
	for (RitualFx &fx : Rituals) {
		const Colours &c = fx.colours;
		const float r = fx.radius;
		const float age = fx.age;
		const float pulse = 0.85F + 0.15F * std::sin(age * 0.009F);
		const float burnt = std::clamp((age - RitualBurnStart) / RitualBurnMs, 0.F, 1.F);

		// The floor inside the circle glows red until the burn has gone round it.
		SoftDisc(Ground, fx.pos, r, c.edge, 0.22F * EaseOut(age / 600) * (1 - burnt), true);

		// The outer band: a Horadric seal laid round from the front, with a thin line of hellfire just inside it.
		const float ring = EaseOut(age / RitualRingMs);
		DrawHoradricSeal(fx, ring);
		RitualRing(fx, r * 0.92F, ring, 1.4F, c.mid, 0.8F * pulse, true);

		// The pentagram, one line after another, its lower point to the front: P0, P2, P4, P1, P3 and back to P0.
		std::array<Vec, 5> star;
		for (int k = 0; k < 5; k++)
			star[k] = RitualPoint(r * 0.66F, Pi / 2 + 2 * Pi * k / 5);
		for (int j = 0; j < 5; j++) {
			const float drawn = (age - RitualStarStart - j * RitualStrokeMs) / RitualStrokeMs;
			if (drawn <= 0)
				break;
			const std::vector<Polyline> stroke { { star[(2 * j) % 5], star[(2 * j + 2) % 5] } };
			RitualWrite(fx, stroke, drawn, 2.4F, c.mid, pulse);
		}
		// Then the inner double ring that holds it.
		const float inner = EaseOut((age - RitualInnerStart) / RitualInnerMs);
		RitualRing(fx, r * 0.69F, inner, 2, c.mid, pulse, true);
		RitualRing(fx, r * 0.66F, inner, 1.2F, c.core, 0.7F * pulse, false);

		// The runes, written one by one round the band from the front, each flaring as it's finished.
		const float height = r * 0.17F;
		const float width = r * 0.13F;
		for (int k = 0; k < RitualRunes; k++) {
			const float written = (age - RitualRuneStart - k * RitualRuneMs) / RitualRuneMs;
			if (written <= 0)
				break;
			const float angle = Pi / 2 + (k + 0.5F) * 2 * Pi / RitualRunes;
			const Vec out = RitualPoint(1, angle);
			// Their tops face the middle, so the ones at the front read upright.
			const Vec right { out.y, -out.x };
			const Vec centre = RitualPoint(r * 0.815F, angle);
			std::vector<Polyline> lines = RuneStrokes(fx.runes[k]);
			for (Polyline &line : lines) {
				for (Vec &v : line)
					v = centre + right * ((v.x - 0.5F) * width) + out * ((v.y - 0.5F) * height);
			}
			const float flare = std::clamp(1 - (age - RitualRuneStart - (k + 1) * RitualRuneMs) / 350, 0.F, 1.F);
			RitualWrite(fx, lines, written, 2 + flare, flare > 0.5F ? c.core : c.mid, std::min(1.F, pulse + flare));
		}

	}
}

/** @brief A fully saturated colour around the colour wheel (0 to 1). */
Rgb Hue(float h)
{
	h = (h - std::floor(h)) * 6;
	const float x = 1 - std::abs(std::fmod(h, 2.F) - 1);
	const auto c = [](float v) { return static_cast<uint8_t>(v * 255); };
	switch (static_cast<int>(h)) {
	case 0: return { 255, c(x), 0 };
	case 1: return { c(x), 255, 0 };
	case 2: return { 0, 255, c(x) };
	case 3: return { 0, c(x), 255 };
	case 4: return { c(x), 0, 255 };
	default: return { 255, 0, c(x) };
	}
}

void DrawHalos()
{
	const float t = AmbientTime;
	for (const Halo &halo : Halos) {
		const Vec pos = halo.pos;
		const float r = halo.radius;
		const float phase = static_cast<float>(halo.seed % 97);
		switch (halo.rank) {
		case Rank::Normal:
			break;
		// Magic and rare monsters get no ring on the ground (Curtis: rings break immersion), only a faint glow at
		// their feet and a few motes; their name colour says the rest, as in POE2.
		case Rank::Magic: {
			const float pulse = 0.75F + 0.25F * std::sin(t * 0.004F + phase);
			SoftDisc(Glowing, pos, r * 1.3F, { 60, 80, 200 }, 0.06F * pulse, true);
			break;
		}
		case Rank::Rare: {
			const Rgb glow = halo.prismatic ? Hue(t * 0.0005F + phase * 0.1F) : Rgb { 200, 130, 30 };
			const float pulse = 0.8F + 0.2F * std::sin(t * 0.003F + phase);
			SoftDisc(Glowing, pos, r * 1.4F, glow, 0.09F * pulse, true);
			break;
		}
		case Rank::Unique:
		case Rank::Boss: {
			const bool boss = halo.rank == Rank::Boss;
			const Colours c = boss ? Colours { { 255, 230, 210 }, { 255, 60, 35 }, { 140, 10, 10 } } : Colours { { 255, 235, 200 }, { 255, 130, 40 }, { 170, 50, 10 } };
			const float spin = t * 0.0008F + phase;
			const float pulse = 0.85F + 0.15F * std::sin(t * 0.005F + phase);
			SoftDisc(Dark, pos, r, { 10, 4, 4 }, 0.3F, true);
			SoftDisc(Glowing, pos, r * 1.25F, c.edge, 0.25F * pulse, true);
			SoftRing(Glowing, pos, r, 3, c.mid, 0.9F * pulse, 0, 1);
			SoftRing(Glowing, pos, r, 12, c.mid, 0.25F * pulse, 0, 1);
			SoftRing(Glowing, pos, r * 0.62F, 1.5F, c.core, 0.6F, 0, 1, 8, -spin * 1.5F);
			const int glyphs = boss ? 10 : 8;
			for (int i = 0; i < glyphs; i++) {
				const float a = spin + 2 * Pi * i / glyphs;
				DrawGlyph(OnGround(pos, r * 0.81F, a), r * 0.085F, a + Pi / 2, (halo.seed + static_cast<uint32_t>(i) * 2654435761U) | 1, c.core, 0.85F * pulse);
			}
			if (boss)
				SoftLine(Glowing, pos, pos - Vec { 0, 170 }, r * 1.1F, c.mid, c.edge, 0.12F * pulse, 0);
			break;
		}
		}
	}
}

/** @brief A cubic Bezier curve. */
Vec Bezier(Vec p0, Vec p1, Vec p2, Vec p3, float t)
{
	const float u = 1 - t;
	return p0 * (u * u * u) + p1 * (3 * u * u * t) + p2 * (3 * u * t * t) + p3 * (t * t * t);
}

/** @brief The quest marker's two outlines in its own units: a "!" with a rounded, pointed top and curved sides, and its dot. */
const std::array<Outline, 2> &QuestGlyph()
{
	static const std::array<Outline, 2> glyph = [] {
		std::array<Outline, 2> result;
		Outline &stem = result[0];
		const Vec top { 0, -24.5F };
		const Vec shoulder { 6.2F, -17.8F };
		const Vec point { 0, 6.8F };
		constexpr int ArchSteps = 8;
		constexpr int SideSteps = 18;
		std::vector<Vec> right;
		for (int i = 0; i < ArchSteps; i++) {
			const float s = static_cast<float>(i) / ArchSteps;
			right.push_back(Bezier(top, { 1.6F, -23.4F }, { 5.2F, -21.2F }, shoulder, s));
		}
		for (int i = 0; i < SideSteps; i++) {
			const float s = static_cast<float>(i) / SideSteps;
			right.push_back(Bezier(shoulder, { 6.9F, -12 }, { 2.4F, -1.5F }, point, s));
		}
		stem.points = right;
		stem.points.push_back(point);
		for (size_t i = right.size() - 1; i >= 1; i--)
			stem.points.push_back({ -right[i].x, right[i].y });
		Outline &dot = result[1];
		constexpr int DotSteps = 28;
		for (int i = 0; i < DotSteps; i++) {
			const float angle = -Pi / 2 + 2 * Pi * i / DotSteps;
			const float c = std::cos(angle);
			const float s = std::sin(angle);
			const auto curve = [](float v) { return (v < 0 ? -1.F : 1.F) * std::pow(std::abs(v), 1.25F); };
			dot.points.push_back({ 4.3F * curve(c), 14.3F + 4.5F * curve(s) });
		}
		stem.finish();
		dot.finish();
		return result;
	}();
	return glyph;
}

/**
 * @brief A quest marker: a black "!" shaped like the letters of the Diablo logo, its outline burning in like a line of a
 * summoning circle when it appears, then catching fire, flames pouring up off its top and licking off its sides. Hellfire
 * for a quest to give, gold fire when the hero has what the towner wants.
 */
void DrawQuestMarks()
{
	const float t = AmbientTime;
	constexpr float DrawMs = 650;
	constexpr float IgniteMs = 450;
	constexpr float K = 1.4F;
	const std::array<Outline, 2> &glyph = QuestGlyph();
	for (const QuestMark &mark : QuestMarks) {
		const Vec c = mark.pos;
		const uint32_t seed = static_cast<uint32_t>(mark.phase) * 7919U + 17;
		const float drawn = EaseOut(mark.age / DrawMs);
		const float lit = std::clamp((mark.age - DrawMs * 0.6F) / IgniteMs, 0.F, 1.F);
		const float flicker = 0.85F + 0.15F * (SmoothNoise(seed, t * 0.012F) * 0.6F + SmoothNoise(seed + 5, t * 0.031F) * 0.4F);
		const Rgb heart = mark.reward ? Rgb { 255, 250, 220 } : Rgb { 255, 240, 170 };
		const Rgb body = mark.reward ? Rgb { 255, 180, 45 } : Rgb { 255, 105, 18 };
		const Rgb tip = mark.reward ? Rgb { 190, 80, 8 } : Rgb { 160, 18, 4 };
		const Rgb rim = mark.reward ? Rgb { 255, 215, 120 } : Rgb { 255, 150, 55 };
		const Rgb face = mark.reward ? Rgb { 34, 22, 8 } : Rgb { 18, 5, 4 };
		const Rgb core = mark.reward ? Rgb { 110, 64, 14 } : Rgb { 80, 12, 5 };

		std::array<Outline, 2> shape = glyph;
		for (Outline &outline : shape) {
			for (Vec &v : outline.points)
				v = c + v * K;
		}
		const Outline &stem = shape[0];
		const Outline &dot = shape[1];

		// A dark halo so the black glyph reads against the town, and the fire's light around it.
		SoftDisc(Dark, c + Vec { 0, -3 } * K, 24 * K, { 4, 1, 1 }, 0.4F * drawn, false);
		SoftDisc(Glowing, c + Vec { 0, -10 } * K, 28 * K, tip, (0.04F + 0.06F * lit) * flicker, false);

		// The black face, faintly molten down its middle.
		const auto fill = [&](const Outline &outline, Vec middle) {
			const int centre = Dark.add(ToScreen(middle), Mix(face, core, 0.3F), 0.96F * drawn);
			const int first = static_cast<int>(Dark.vertices.size());
			for (const Vec &v : outline.points)
				Dark.add(ToScreen(v), face, 0.96F * drawn);
			const int n = static_cast<int>(outline.points.size());
			for (int i = 0; i < n; i++)
				Dark.tri(centre, first + i, first + (i + 1) % n);
		};
		fill(stem, c + Vec { 0, -9 } * K);
		fill(dot, c + Vec { 0, 14.3F } * K);
		SoftLine(Glowing, c + Vec { 0, -19 } * K, c + Vec { 0, 1 } * K, 1.4F * K, core, core, 0.2F * lit * flicker, 0.05F * lit);
		SoftDisc(Glowing, c + Vec { 0, 14.3F } * K, 1.6F * K, core, 0.3F * lit * flicker, false);

		// The outline burns in from the top of each part down both sides at once, then glows on, its light crawling
		// along it: a hot white thread in a burning rim with a red heat haze outside.
		const float stemShown = std::clamp(drawn / 0.75F, 0.F, 1.F);
		const float dotShown = std::clamp((drawn - 0.75F) / 0.25F, 0.F, 1.F);
		for (int part = 0; part < 2; part++) {
			const Outline &outline = part == 0 ? stem : dot;
			const float shown = part == 0 ? stemShown : dotShown;
			if (shown <= 0)
				continue;
			StrokeOutline(Glowing, outline, 2.6F * K, 5.5F * K, tip, 0.4F * flicker, shown, seed + part * 3, 0.5F);
			StrokeOutline(Glowing, outline, 0.5F * K, 2.1F * K, rim, 0.92F, shown, seed + part * 3 + 1, 0.3F);
			StrokeOutline(Glowing, outline, 0.5F * K, 0.8F * K, heart, 0.6F, shown, seed + part * 3 + 2, 0.3F);
			if (shown >= 1)
				continue;
			// A pen of fire at each head as it burns round.
			const size_t n = outline.points.size();
			for (size_t i = 0; i < n; i++) {
				const size_t j = (i + 1) % n;
				const float ra = outline.reach[i];
				const float rb = outline.reach[j];
				if ((ra <= shown) == (rb <= shown))
					continue;
				const float f = std::clamp((shown - ra) / (rb - ra), 0.F, 1.F);
				const Vec head = outline.points[i] + (outline.points[j] - outline.points[i]) * f;
				SoftDisc(Glowing, head, 4 * K, heart, 0.9F, false);
				SoftDisc(Glowing, head, 9 * K, body, 0.35F, false);
			}
		}

		// Then it catches: fire pours up off the top and licks off the upper sides and the dot.
		if (lit <= 0)
			continue;
		std::vector<FlameSite> sites;
		const size_t n = stem.points.size();
		for (size_t i = 0; i < n; i++) {
			const Vec local = (stem.points[i] - c) * (1 / K);
			if (local.y > -2)
				continue;
			const float topness = std::clamp((-local.y - 2) / 21.F, 0.F, 1.F);
			const bool arch = local.y < -17.4F;
			sites.push_back({ stem.points[i], stem.normals[i], arch ? 3.F : 0.25F + 1.1F * topness, arch ? 1.F : 0.3F + 0.35F * topness });
		}
		for (size_t i = 0; i < dot.points.size(); i++) {
			if (dot.normals[i].y < -0.3F)
				sites.push_back({ dot.points[i], dot.normals[i], 0.3F, 0.3F });
		}
		DrawFire(sites, 170, K, seed, heart, body, tip, EaseOut(lit) * flicker);
	}
}

/** @brief Render targets for the glowing layer and its blurred copies; remade when the size or renderer changes. */
struct Targets {
	SDL_Renderer *renderer = nullptr;
	Size size;
	SDL_Texture *full = nullptr;
	SDL_Texture *half = nullptr;
	SDL_Texture *quarter = nullptr;
	SDL_Texture *eighth = nullptr;
	/** @brief The dark shapes lying on the floor, cut out before they're blended onto the world. */
	SDL_Texture *ground = nullptr;
	/** @brief Opaque where something stands on the floor; drawn over the floor shapes, it cuts them away there. */
	SDL_Texture *mask = nullptr;

	void release()
	{
		for (SDL_Texture *texture : { full, half, quarter, eighth, ground, mask }) {
			if (texture != nullptr)
				SDL_DestroyTexture(texture);
		}
		full = half = quarter = eighth = ground = mask = nullptr;
	}

	bool ensure(SDL_Renderer *wanted, Size wantedSize)
	{
		if (wanted == renderer && wantedSize == size && full != nullptr)
			return true;
		if (wanted == renderer)
			release();
		renderer = wanted;
		size = wantedSize;
		full = half = quarter = eighth = ground = mask = nullptr;
		if (!SDL_RenderTargetSupported(renderer))
			return false;
		auto make = [&](int divisor) {
			SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, std::max(size.width / divisor, 1), std::max(size.height / divisor, 1));
			if (texture != nullptr) {
				SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
			}
			return texture;
		};
		full = make(1);
		half = make(2);
		quarter = make(4);
		eighth = make(8);
		if (full == nullptr || half == nullptr || quarter == nullptr || eighth == nullptr) {
			release();
			return false;
		}
		// The cut-out keeps what's under the mask in proportion to how see-through the mask is. Without custom blend
		// modes the floor shapes go over everything, as the rest of the effects do.
		const SDL_BlendMode cutout = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
		    SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
		mask = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, size.width, size.height);
		ground = make(1);
		if (mask == nullptr || ground == nullptr || SDL_SetTextureBlendMode(mask, cutout) != 0 || SDL_SetTextureBlendMode(ground, SDL_BLENDMODE_BLEND) != 0) {
			if (mask != nullptr)
				SDL_DestroyTexture(mask);
			if (ground != nullptr)
				SDL_DestroyTexture(ground);
			mask = ground = nullptr;
		}
		return true;
	}
};

Targets Layer;

void Submit(SDL_Renderer *renderer, Batch &batch, float scale)
{
	if (batch.indices.empty())
		return;
	if (scale != 1) {
		for (SDL_Vertex &v : batch.vertices) {
			v.position.x *= scale;
			v.position.y *= scale;
		}
	}
	SDL_RenderGeometry(renderer, nullptr, batch.vertices.data(), static_cast<int>(batch.vertices.size()), batch.indices.data(), static_cast<int>(batch.indices.size()));
}

uint32_t LastTicks = 0;

/**
 * @brief Fills the mask where the world differs from its bare floor, over the floor shapes' bounds; that's where walls,
 * objects, items and characters stand. Returns the bounds, or nothing when the floor shapes can't be cut out this frame.
 */
std::optional<SDL_Rect> FillFloorMask(Size worldSize)
{
	if (!FloorFresh || FloorSurface == nullptr || Layer.mask == nullptr)
		return std::nullopt;
	const Surface world = WorldBackBuffer();
	if (world.w() != FloorSurface->w || world.h() != FloorSurface->h || world.w() != worldSize.width || world.h() != worldSize.height)
		return std::nullopt;
	float x0 = static_cast<float>(worldSize.width);
	float y0 = static_cast<float>(worldSize.height);
	float x1 = 0;
	float y1 = 0;
	for (const Batch *batch : { &Ground, &GroundDark }) {
		for (const SDL_Vertex &v : batch->vertices) {
			x0 = std::min(x0, v.position.x);
			y0 = std::min(y0, v.position.y);
			x1 = std::max(x1, v.position.x);
			y1 = std::max(y1, v.position.y);
		}
	}
	SDL_Rect rect;
	rect.x = std::clamp(static_cast<int>(x0) - 1, 0, worldSize.width);
	rect.y = std::clamp(static_cast<int>(y0) - 1, 0, worldSize.height);
	rect.w = std::clamp(static_cast<int>(x1) + 2, 0, worldSize.width) - rect.x;
	rect.h = std::clamp(static_cast<int>(y1) + 2, 0, worldSize.height) - rect.y;
	if (rect.w <= 0 || rect.h <= 0)
		return std::nullopt;
	void *pixels;
	int pitch;
	if (SDL_LockTexture(Layer.mask, &rect, &pixels, &pitch) != 0)
		return std::nullopt;
	// Shadows are painted into the sprites in colour 0, so a pixel of colour 0 over the floor is a shadow lying on it,
	// not something standing on it: the floor effects show through those. But shade on a sprite also darkens some of
	// its own pixels to colour 0, and some of a sprite's pixels happen to match the floor under them, so a pixel like
	// that with the sprite close on both sides is filled in as solid.
	// A lava lake fills the whole frame with floor glow, so this runs over millions of pixels: the solid flags sit in
	// rows padded with 3 empty pixels on every side, which keeps every neighbour lookup in bounds and the loops free of
	// branches (Curtis 2026-10-19: lava levels slowed the game).
	constexpr int Reach = 3;
	// A covered pixel is black with full alpha, in the mask's byte order.
	constexpr uint8_t CoveredBytes[4] = { 0, 0, 0, 255 };
	uint32_t covered;
	std::memcpy(&covered, CoveredBytes, sizeof(covered));
	const int stride = rect.w + 2 * Reach;
	static std::vector<uint8_t> padded;
	padded.assign(static_cast<size_t>(stride) * (rect.h + 2 * Reach), 0);
	const auto row = [&](int y) { return &padded[(static_cast<size_t>(y + Reach) * stride) + Reach]; };
	for (int y = 0; y < rect.h; y++) {
		const uint8_t *now = world.at(rect.x, rect.y + y);
		const uint8_t *bare = static_cast<const uint8_t *>(FloorSurface->pixels) + static_cast<ptrdiff_t>(rect.y + y) * FloorSurface->pitch + rect.x;
		uint8_t *solid = row(y);
		for (int x = 0; x < rect.w; x++)
			solid[x] = static_cast<uint8_t>(static_cast<int>(now[x] != bare[x]) & static_cast<int>(now[x] != 0));
	}
	for (int y = 0; y < rect.h; y++) {
		const uint8_t *s = row(y);
		const uint8_t *up1 = row(y - 1);
		const uint8_t *up2 = row(y - 2);
		const uint8_t *up3 = row(y - 3);
		const uint8_t *down1 = row(y + 1);
		const uint8_t *down2 = row(y + 2);
		const uint8_t *down3 = row(y + 3);
		auto *out = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(pixels) + static_cast<ptrdiff_t>(y) * pitch);
		for (int x = 0; x < rect.w; x++) {
			const uint32_t across = static_cast<uint32_t>((s[x - 1] | s[x - 2] | s[x - 3]) & (s[x + 1] | s[x + 2] | s[x + 3]));
			const uint32_t upDown = static_cast<uint32_t>((up1[x] | up2[x] | up3[x]) & (down1[x] | down2[x] | down3[x]));
			out[x] = (s[x] | across | upDown) * covered;
		}
	}
	SDL_UnlockTexture(Layer.mask);
	return rect;
}

} // namespace
#endif

void Render(SDL_Renderer *renderer, Size worldSize, Size logicalSize)
{
#if SDL_VERSION_ATLEAST(2, 0, 18)
	const uint32_t now = SDL_GetTicks();
	const float elapsed = LastTicks == 0 ? 0.F : static_cast<float>(std::min<uint32_t>(now - LastTicks, 50));
	LastTicks = now;
	if (PauseMode == 0)
		Advance(elapsed);
	if (renderer == nullptr || worldSize.width <= 0 || logicalSize.width <= 0)
		return;
	LastDrawnScan = ScanFrame;
	if (Particles.empty() && Rings.empty() && Bolts.empty() && Sigils.empty() && Rituals.empty() && IceSpikes.empty() && Cracks.empty() && Sweeps.empty() && FrameGlows.empty() && Halos.empty() && QuestMarks.empty() && Beams.empty())
		return;

	Glowing.clear();
	Dark.clear();
	Ground.clear();
	GroundDark.clear();
	DrawHalos();
	DrawCracks();
	DrawSigils();
	DrawRituals();
	DrawBeams();
	DrawIceSpikes();
	DrawRings();
	DrawComets();
	DrawBolts();
	DrawSweeps();
	DrawQuestMarks();
	DrawParticles();

	const float toLogical = static_cast<float>(logicalSize.width) / static_cast<float>(worldSize.width);
	const bool targets = Layer.ensure(renderer, worldSize);
	const std::optional<SDL_Rect> cut = targets ? FillFloorMask(worldSize) : std::nullopt;
	FloorFresh = false;

	// Scorch marks on the floor, cut away where something stands on it, then smoke, dust and debris: all blend
	// straight onto the world.
	if (cut && !GroundDark.indices.empty()) {
		SDL_SetRenderTarget(renderer, Layer.ground);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
		SDL_RenderClear(renderer);
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		Submit(renderer, GroundDark, 1);
		SDL_RenderCopy(renderer, Layer.mask, &*cut, &*cut);
		SDL_SetRenderTarget(renderer, nullptr);
		SDL_RenderCopy(renderer, Layer.ground, nullptr, nullptr);
	} else {
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		Submit(renderer, GroundDark, toLogical);
	}
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	Submit(renderer, Dark, toLogical);

	if (!targets) {
		// No render targets: the glow goes straight on, without bloom.
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
		Submit(renderer, Ground, toLogical);
		Submit(renderer, Glowing, toLogical);
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
		return;
	}
	// The glowing layer adds up on black (opaque, so copying it with additive blending adds its colours). The glow on
	// the floor goes on by itself first, cut away where something stands on it, and without bloom: a lava lake's bloom
	// spread over the skills and monsters on it and hid them (Curtis 2026-10-19).
	if (!Ground.indices.empty()) {
		SDL_SetRenderTarget(renderer, Layer.full);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
		Submit(renderer, Ground, 1);
		if (cut)
			SDL_RenderCopy(renderer, Layer.mask, &*cut, &*cut);
		SDL_SetRenderTarget(renderer, nullptr);
		SDL_SetTextureBlendMode(Layer.full, SDL_BLENDMODE_ADD);
		SDL_RenderCopy(renderer, Layer.full, nullptr, nullptr);
	}
	SDL_SetRenderTarget(renderer, Layer.full);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
	Submit(renderer, Glowing, 1);
	// Bloom: ever smaller copies, each a blur of the last.
	SDL_SetTextureBlendMode(Layer.full, SDL_BLENDMODE_NONE);
	SDL_SetTextureBlendMode(Layer.half, SDL_BLENDMODE_NONE);
	SDL_SetTextureBlendMode(Layer.quarter, SDL_BLENDMODE_NONE);
	SDL_SetRenderTarget(renderer, Layer.half);
	SDL_RenderCopy(renderer, Layer.full, nullptr, nullptr);
	SDL_SetRenderTarget(renderer, Layer.quarter);
	SDL_RenderCopy(renderer, Layer.half, nullptr, nullptr);
	SDL_SetRenderTarget(renderer, Layer.eighth);
	SDL_RenderCopy(renderer, Layer.quarter, nullptr, nullptr);
	SDL_SetRenderTarget(renderer, nullptr);

	const float bloom = std::clamp(Intensity(), 0.F, 1.5F);
	SDL_SetTextureBlendMode(Layer.full, SDL_BLENDMODE_ADD);
	SDL_RenderCopy(renderer, Layer.full, nullptr, nullptr);
	SDL_SetTextureBlendMode(Layer.quarter, SDL_BLENDMODE_ADD);
	SDL_SetTextureAlphaMod(Layer.quarter, static_cast<uint8_t>(std::min(255.F, 170 * bloom)));
	SDL_RenderCopy(renderer, Layer.quarter, nullptr, nullptr);
	SDL_SetTextureBlendMode(Layer.eighth, SDL_BLENDMODE_ADD);
	SDL_SetTextureAlphaMod(Layer.eighth, static_cast<uint8_t>(std::min(255.F, 200 * bloom)));
	SDL_RenderCopy(renderer, Layer.eighth, nullptr, nullptr);
	SDL_SetTextureAlphaMod(Layer.quarter, 255);
	SDL_SetTextureAlphaMod(Layer.eighth, 255);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
#else
	(void)renderer;
	(void)worldSize;
	(void)logicalSize;
#endif
}
#endif

} // namespace devilution::vfx
