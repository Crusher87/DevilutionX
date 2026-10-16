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
#include <optional>
#include <unordered_map>
#include <vector>

#include "diablo.h"
#include "engine/palette.h"
#include "engine/render/clx_render.hpp"
#include "engine/render/scrollrt.h"
#include "engine/size.hpp"
#include "engine/surface.hpp"
#include "headless_mode.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "objects.h"
#include "options.h"
#include "poe2/camera_shake.hpp"
#include "poe2/monster_mods.hpp"
#include "utils/is_of.hpp"

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
};

struct Sweep {
	Vec pos;
	float facing;
	float span;
	float radius;
	float age = 0;
	float life;
	Colours colours;
};

struct Crack {
	Vec pos;
	std::vector<std::array<Vec, 2>> segments;
	float age = 0;
	float life;
	Rgb glow;
};

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
std::vector<Crack> Cracks;
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
/** @brief Each monster's mode last frame, to catch it starting to cast, fade, charge or die. */
std::unordered_map<const Monster *, MonsterMode> LastModes;

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
		if (mode == MonsterMode::Death)
			continue;
		const float height = std::min(static_cast<float>(monster.animInfo.currentSprite().height()), 128.F);
		const Vec body = feet - Vec { 0, height * 0.35F };
		const float width = static_cast<float>(monster.animInfo.currentSprite().width());

		if (mode == MonsterMode::Charge)
			Emitters.push_back({ feet, 1, Ambient::Charge, { 115, 100, 80 } });

		if (rank != Rank::Normal) {
			const MonsterAffixes affixes = GetMonsterAffixes(monster);
			const float radius = rank >= Rank::Unique ? std::max(TileRadius * 0.85F, width * 0.42F) : TileRadius * 0.6F;
			Halos.push_back({ feet, radius, rank, affixes.has(MonsterMod::Resistant), static_cast<uint32_t>(ActiveMonsters[i]) * 2654435761U | 1 });
			switch (rank) {
			case Rank::Magic:
				Emitters.push_back({ feet, 1, Ambient::Motes, { 130, 160, 255 }, radius });
				break;
			case Rank::Rare:
				Emitters.push_back({ feet, 2, Ambient::Motes, { 255, 205, 90 }, radius });
				if (affixes.has(MonsterMod::Regenerating))
					Emitters.push_back({ body, 5, Ambient::Motes, { 90, 235, 110 }, 14 });
				if (affixes.has(MonsterMod::ExtraLife))
					Emitters.push_back({ body, 4, Ambient::Motes, { 235, 40, 50 }, 14 });
				if (affixes.has(MonsterMod::HardHitting))
					Emitters.push_back({ body, 1, Ambient::Embers, { 255, 160, 60 } });
				break;
			default:
				Emitters.push_back({ feet, rank == Rank::Boss ? 16.F : 9.F, Ambient::Motes, { 255, 120, 40 }, radius });
				break;
			}
		}

		// What the hero did to it.
		const Poe2MonsterState &state = monster.poe2;
		Ring glow;
		glow.pos = body;
		glow.life = 1;
		glow.width = 0;
		glow.disc = true;
		glow.ground = false;
		if (state.igniteTicks > 0) {
			Emitters.push_back({ body, height, Ambient::Ignite, { 255, 140, 40 } });
			glow.r0 = glow.r1 = 26 + height * 0.15F;
			glow.colour = { 255, 120, 30 };
			glow.alpha = 0.28F * Flicker(body, 2);
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
	constexpr int Step = 8;
	constexpr int Cell = 32;
	constexpr int SamplesPerCell = (Cell / Step) * (Cell / Step);
	const int width = world.w();
	const int height = world.h();
	const int columns = (width + Cell - 1) / Cell;
	const int rows = (height + Cell - 1) / Cell;
	struct Patch {
		int lava = 0;
		int water = 0;
		float light = 0;
		float r = 0, g = 0, b = 0;
	};
	std::vector<Patch> patches(static_cast<size_t>(columns * rows));
	for (int y = Step / 2; y < height; y += Step) {
		const uint8_t *line = world.at(0, y);
		for (int x = Step / 2; x < width; x += Step) {
			const uint8_t index = line[x];
			if (index == 0 || index > last || kinds[index] == 0)
				continue;
			Patch &patch = patches[static_cast<size_t>((y / Cell) * columns + x / Cell)];
			const SDL_Color &c = logical_palette[index];
			(kinds[index] == 1 ? patch.lava : patch.water)++;
			patch.light += Luminance(c) / 255.F;
			patch.r += c.r;
			patch.g += c.g;
			patch.b += c.b;
		}
	}
	const float spread = Cell / 2.F / CameraScale;
	const float pulseTime = AmbientTime * 0.0016F;
	for (int row = 0; row < rows; row++) {
		for (int column = 0; column < columns; column++) {
			const Patch &patch = patches[static_cast<size_t>(row * columns + column)];
			const int total = patch.lava + patch.water;
			if (total == 0)
				continue;
			const Vec pos = Vec { (column + 0.5F) * Cell, (row + 0.5F) * Cell } * (1 / CameraScale) + CameraOrigin;
			if (!view.contains(pos, 0))
				continue;
			const float share = static_cast<float>(total) / SamplesPerCell;
			const float light = patch.light / total;
			const Rgb average { static_cast<uint8_t>(patch.r / total), static_cast<uint8_t>(patch.g / total), static_cast<uint8_t>(patch.b / total) };
			if (patch.lava >= patch.water) {
				// Molten glow that slowly breathes, spilling a little past the lava's edge.
				const float pulse = 0.85F + 0.15F * std::sin(pulseTime + column * 0.9F + row * 1.3F);
				Ring glow;
				glow.pos = pos;
				glow.life = 1;
				glow.width = 0;
				glow.disc = true;
				glow.ground = false;
				glow.r0 = glow.r1 = spread * 2.1F;
				glow.colour = { 255, 90, 20 };
				glow.alpha = 0.16F * share * (0.35F + light) * pulse;
				FrameGlows.push_back(glow);
				Emitters.push_back({ pos, share, Ambient::Lava, average, spread });
			} else {
				Emitters.push_back({ pos, share, Ambient::Water, Mix(average, { 235, 245, 255 }, 0.55F), spread });
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
			for (int i = Occurrences(22, ms); i > 0; i--) {
				const float h = emitter.size;
				Particle p;
				p.pos = emitter.pos + Vec { Random(-0.18F, 0.18F) * h, Random(-0.25F, 0.3F) * h };
				p.vel = { Random(-8, 8), 0 };
				p.vz = Random(40, 90);
				p.gravity = -40;
				p.drag = 1;
				p.life = Random(250, 500);
				p.size0 = Random(2.5F, 4);
				p.size1 = 0.5F;
				p.c0 = { 255, 210, 110 };
				p.c1 = { 200, 50, 10 };
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

void Clear()
{
	Particles.clear();
	Rings.clear();
	Bolts.clear();
	Sigils.clear();
	Cracks.clear();
	Sweeps.clear();
	FrameGlows.clear();
	FrameComets.clear();
	Trails.clear();
	SeenBursts.clear();
	Emitters.clear();
	HotSpots.clear();
	SteadySpots.clear();
	Halos.clear();
	LastModes.clear();
}

void Sigil(Point tile, int radiusTiles, int durationMs, Element element)
{
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

void Slam(Point tile, int radiusTiles, Element element)
{
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
	if (!Enabled())
		return;
	ImpactAt(TileCentre(tile), element, size);
}

void Sparks(Point tile, Element element, float size)
{
	if (!Enabled())
		return;
	SparksAt(TileCentre(tile), element, size);
}

void Arc(Point from, Point to, Element element)
{
	if (!Enabled())
		return;
	AddBolt(TileCentre(from) + Vec { 0, -30 }, TileCentre(to) + Vec { 0, -30 }, 220, 3, ColoursOf(element));
}

void SkyStrike(Point tile, Element element)
{
	if (!Enabled())
		return;
	const Vec pos = TileCentre(tile);
	const Colours c = ColoursOf(element);
	AddBolt(pos + Vec { Random(-30, 30), -300 }, pos, 260, 4, c);
	AddBolt(pos + Vec { Random(-60, 60), -280 }, pos + Vec { Random(-10, 10), -40 }, 180, 2, c);
	ImpactAt(pos, element, 1.2F);
}

void Swing(Point from, Point toward, Element element, bool wide)
{
	if (!Enabled())
		return;
	const Vec pos = TileCentre(from);
	const Vec d = TileCentre(toward) - pos;
	Sweep sweep;
	sweep.pos = pos + Vec { 0, -26 };
	sweep.facing = std::atan2(d.y * 2, d.x);
	sweep.span = wide ? Pi * 0.75F : Pi * 0.4F;
	sweep.radius = TileRadius * (wide ? 1.15F : 0.9F);
	sweep.life = 220;
	sweep.colours = ColoursOf(element);
	Sweeps.push_back(sweep);
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
	if (!Enabled() || world.w() <= 0 || world.h() <= 0)
		return;
	const View view { CameraOrigin, CameraOrigin + Vec { static_cast<float>(world.w()), static_cast<float>(world.h()) } * (1 / CameraScale) };
	ScanFlames(view);
	ScanFluids(world, view);
	ScanMonsters(view);
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
	Age(Cracks, ms);
	Age(Sweeps, ms);
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
	return Rings.size() + Sigils.size() + Bolts.size() + Cracks.size() + Sweeps.size();
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
		Batch &batch = ring.dark ? Dark : Glowing;
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
		SoftDisc(Glowing, glow.pos, glow.r1, glow.colour, glow.alpha, glow.ground);
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
			SoftLine(Dark, a, b, 4, { 15, 10, 8 }, { 15, 10, 8 }, 0.85F * fade, 0.85F * fade);
			if (heat > 0) {
				SoftLine(Glowing, a, b, 2, crack.glow, crack.glow, heat, heat);
				SoftLine(Glowing, a, b, 8, crack.glow, crack.glow, heat * 0.3F, heat * 0.3F);
			}
		}
	}
}

void DrawSweeps()
{
	for (const Sweep &sweep : Sweeps) {
		const float t = sweep.age / sweep.life;
		// The blade's edge races across the arc; the band behind it fades.
		const float head = EaseOut(std::min(t * 1.6F, 1.F));
		const float tailLag = 0.45F;
		const int steps = 18;
		const float start = sweep.facing - sweep.span / 2;
		for (int i = 0; i < steps; i++) {
			const float u0 = static_cast<float>(i) / steps;
			const float u1 = static_cast<float>(i + 1) / steps;
			if (u1 > head || u0 < head - tailLag)
				continue;
			const float a0 = start + sweep.span * u0;
			const float a1 = start + sweep.span * u1;
			const float bright = (1 - (head - u1) / tailLag) * (1 - t * t);
			const Vec p0 = OnGround(sweep.pos, sweep.radius, a0);
			const Vec p1 = OnGround(sweep.pos, sweep.radius, a1);
			SoftLine(Glowing, p0, p1, 3, sweep.colours.core, sweep.colours.core, bright, bright);
			SoftLine(Glowing, p0, p1, 16, sweep.colours.mid, sweep.colours.mid, bright * 0.35F, bright * 0.35F);
			const Vec q0 = OnGround(sweep.pos, sweep.radius * 0.7F, a0);
			const Vec q1 = OnGround(sweep.pos, sweep.radius * 0.7F, a1);
			SoftLine(Glowing, q0, q1, 8, sweep.colours.edge, sweep.colours.edge, bright * 0.3F, bright * 0.3F);
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
		SoftDisc(Glowing, sigil.pos, r * 1.05F, c.edge, (0.10F + 0.25F * std::min(t, 1.F)) * alpha * draw, true);
		SoftDisc(Dark, sigil.pos, r * 0.98F, { 10, 5, 15 }, 0.3F * alpha * draw, true);

		// Outer double ring, drawn in around the circle.
		const int segments = 64;
		const int drawn = static_cast<int>(segments * draw);
		for (int i = 0; i < drawn; i++) {
			const float a0 = spin + 2 * Pi * i / segments;
			const float a1 = spin + 2 * Pi * (i + 1) / segments;
			SoftLine(Glowing, OnGround(sigil.pos, r, a0), OnGround(sigil.pos, r, a1), 3, c.mid, c.mid, pulse * alpha, pulse * alpha);
			SoftLine(Glowing, OnGround(sigil.pos, r, a0), OnGround(sigil.pos, r, a1), 12, c.mid, c.mid, 0.25F * pulse * alpha, 0.25F * pulse * alpha);
			SoftLine(Glowing, OnGround(sigil.pos, r * 0.86F, a0), OnGround(sigil.pos, r * 0.86F, a1), 1.6F, c.core, c.core, 0.8F * alpha, 0.8F * alpha);
		}
		// Ticks between the rings.
		for (int i = 0; i < 48 && i < drawn * 48 / segments; i += 2) {
			const float a = spin + 2 * Pi * i / 48;
			SoftLine(Glowing, OnGround(sigil.pos, r * 0.88F, a), OnGround(sigil.pos, r * 0.98F, a), 1.4F, c.core, c.core, 0.7F * alpha, 0.2F * alpha);
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
				SoftLine(Glowing, OnGround(sigil.pos, r * 0.58F, a0), OnGround(sigil.pos, r * 0.58F, a1), 2, c.mid, c.mid, 0.85F * pulse * alpha, 0.85F * pulse * alpha);
			}
			SoftRing(Glowing, sigil.pos, r * 0.58F, 1.5F, c.core, 0.6F * alpha, 0, 1);
			SoftRing(Glowing, sigil.pos, r * 0.2F, 2, c.core, pulse * alpha, 0, 1);
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
			SoftRing(Glowing, sigil.pos, r * (1 + exploding * 0.6F), 6, c.core, 1 - exploding, 0, 1);
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

/** @brief Render targets for the glowing layer and its blurred copies; remade when the size or renderer changes. */
struct Targets {
	SDL_Renderer *renderer = nullptr;
	Size size;
	SDL_Texture *full = nullptr;
	SDL_Texture *half = nullptr;
	SDL_Texture *quarter = nullptr;
	SDL_Texture *eighth = nullptr;

	void release()
	{
		for (SDL_Texture *texture : { full, half, quarter, eighth }) {
			if (texture != nullptr)
				SDL_DestroyTexture(texture);
		}
		full = half = quarter = eighth = nullptr;
	}

	bool ensure(SDL_Renderer *wanted, Size wantedSize)
	{
		if (wanted == renderer && wantedSize == size && full != nullptr)
			return true;
		if (wanted == renderer)
			release();
		renderer = wanted;
		size = wantedSize;
		full = half = quarter = eighth = nullptr;
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
	if (Particles.empty() && Rings.empty() && Bolts.empty() && Sigils.empty() && Cracks.empty() && Sweeps.empty() && FrameGlows.empty() && Halos.empty())
		return;

	Glowing.clear();
	Dark.clear();
	DrawHalos();
	DrawCracks();
	DrawSigils();
	DrawRings();
	DrawComets();
	DrawBolts();
	DrawSweeps();
	DrawParticles();

	const float toLogical = static_cast<float>(logicalSize.width) / static_cast<float>(worldSize.width);
	// Smoke, dust, debris and scorch marks blend straight onto the world.
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	Submit(renderer, Dark, toLogical);

	if (!Layer.ensure(renderer, worldSize)) {
		// No render targets: the glow goes straight on, without bloom.
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
		Submit(renderer, Glowing, toLogical);
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
		return;
	}
	// The glowing layer adds up on black (opaque, so copying it with additive blending adds its colours).
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
