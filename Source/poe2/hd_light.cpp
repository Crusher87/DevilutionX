#include "poe2/hd_light.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <queue>
#include <utility>
#include <vector>

#include "engine/light_tables.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "levels/trigs.h"
#include "lighting.h"
#include "missiles.h"
#include "monster.h"
#include "objects.h"
#include "player.h"
#include "poe2/atmosphere.hpp"
#include "poe2/defenses.hpp"
#include "poe2/moments.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/vfx.hpp"
#include "poe2/weather.hpp"
#include "utils/endian_swap.hpp"
#include "utils/is_of.hpp"

namespace devilution {

namespace hd {

namespace light {

namespace {

/** @brief Light samples per tile, each way: a quarter tile either side of each tile's centre. */
constexpr int Per = 2;
constexpr int LatticeW = MAXDUNX * Per;
constexpr int LatticeH = MAXDUNY * Per;

/** @brief Where sample i sits, in tiles (tile centres are whole numbers). */
constexpr float SampleAt(int i)
{
	return static_cast<float>(i) / Per - 0.25F;
}

/** @brief The sample at or just before a world coordinate. */
int SampleBefore(float world)
{
	return static_cast<int>(std::floor((world + 0.25F) * Per));
}

/** @brief How far (dx, dy) reaches. Not std::hypot, which is slower and gives precision not needed here. */
float Length(float dx, float dy)
{
	return std::sqrt(dx * dx + dy * dy);
}

/** @brief Rounds to the nearest whole number (the Amiga toolchain has no std::lround). */
int RoundToInt(float value)
{
	return static_cast<int>(value < 0 ? value - 0.5F : value + 0.5F);
}

using Colour = std::array<float, 3>;

struct Source {
	/** @brief Where the light is: its brightness falls off from here, as the game's does. */
	float x = 0;
	float y = 0;
	/** @brief Where its light starts out: for a torch hanging on a wall, just in front of the wall. */
	float rayX = 0;
	float rayY = 0;
	int radius = 0;
	/** @brief Its colour, evened out to the brightness of the hero's own light. */
	Colour colour {};
	/** @brief How high its flame hangs, in tiles: measured from the game's pictures (a hero stands about 1.5 tall). */
	float height = 1;
	/** @brief How wide its flame or glow is, in tiles: the wider, the softer the edges of its shadows. */
	float size = 0.2F;
	/** @brief How much of its light still reaches the parts of its room that it can't see, bounced off the floor and walls. */
	float fill = 0.15F;
	/** @brief How bright it shines for its light level, against the hero's own glow (1). */
	float power = 1;
	bool castsShadows = true;
	/** @brief Glowing lava (or a glowing pool): its tiles together shine as one wide light lying on the floor (ShadowLights). */
	bool lava = false;
	/** @brief A hero's own glow, the light the game gives every hero to see by (GlowAt). */
	bool glow = false;
	/** @brief Carried by a hero or a monster: right over them, it casts their shadow straight down (ShadowLights). */
	bool carried = false;
	/** @brief For a torch on a wall: the open side it faces (the camera sees walls from +x and +y). */
	int faceX = 0;
	int faceY = 0;
	/** @brief The open side a light shines out of (a doorway), as a direction in tiles; 0, 0 for all round (ConeOf). */
	float coneX = 0;
	float coneY = 0;
	/** @brief How its flame moves its light (the Atmosphere option's flicker, idea 1), and its brightness this frame against steady (1). */
	uint8_t flickerKind = 0;
	uint32_t flickerSeed = 0;
	float flicker = 1;
};

/** @brief Source::flickerKind: a steady light, a torch, a candle, a fire or brazier, stairs down breathing slowly. */
constexpr uint8_t SteadyLight = 0;
constexpr uint8_t TorchFlicker = 1;
constexpr uint8_t CandleFlicker = 2;
constexpr uint8_t FireFlicker = 3;
constexpr uint8_t StairsBreath = 4;
/** @brief The cathedral's doorway in town: its red glow breathes, and is stronger the deeper the hero has been (idea 21). */
constexpr uint8_t DoorBreath = 5;

// The colours of the lights, before they're evened out.
constexpr Colour TorchColour { 1.00F, 0.60F, 0.28F };
constexpr Colour CandelabraColour { 1.00F, 0.72F, 0.42F };
constexpr Colour CandleColour { 1.00F, 0.80F, 0.52F };
constexpr Colour FireColour { 1.00F, 0.52F, 0.24F };
constexpr Colour BurningCrossColour { 1.00F, 0.46F, 0.22F };
constexpr Colour LavaColour { 1.00F, 0.40F, 0.16F };
constexpr Colour BerserkColour { 1.00F, 0.30F, 0.25F };
constexpr Colour UniqueGlowColour { 1.00F, 0.92F, 0.80F };
/** @brief How far a light's colour goes from the hero's warm white towards its own (1: all the way). */
constexpr float ColourStrength = 0.85F;
/** @brief Light adds up as linear light; brightness as drawn is linear light to the power 1/Gamma (as sRGB is). */
constexpr float Gamma = 2.2F;
/**
 * @brief Real flames and spells outshine the glow a hero carries (the game gives every light the same brightness at its
 * heart, so its light levels can't say so). Beside a torch or a firebolt its light is then most of the light there,
 * and the shadows it casts show clearly even next to the hero; the hero's glow alone lights things as before.
 */
constexpr float FlamePower = 2.F;
constexpr float SpellPower = 2.5F;
/** @brief How high lava's glow lies, in tiles: low over the floor, so it lights a body from below and its shadows reach far. */
constexpr float LavaHeight = 0.3F;
/** @brief The most linear light the drawn brightness table goes up to. */
constexpr float LinearTop = 4.F;

/**
 * @brief Tristram's evening, the HD Lighting look in town (Curtis's pick, 2026-11-07): the sky's light everywhere, a
 * little darker and cooler than the game's fully lit town, firelight from the forge and the tavern's open door, and
 * lamplight from the windows and lamps the town's art paints lit.
 */
constexpr float TownSkyBrightness = 0.62F;
constexpr Colour TownSkyColour { 0.82F, 0.90F, 1.10F };
/**
 * @brief The sky's shadows fall the way the town's painted ones did (down and to the left on screen, along +y): soft
 * and faint, from a light this far off and high enough that a body's shadow is about half as long as it is tall, as
 * wide as a patch of bright sky, and half filled in by the light of the rest of the sky.
 */
constexpr float SkyDirX = 0.13F;
constexpr float SkyDirY = 0.99F;
constexpr float SkyDistance = 12.F;
constexpr float SkyShadowLength = 0.55F;
constexpr float SkySize = 4.F;
constexpr float SkyFill = 0.45F;
/** @brief A light shining out of an opening still lights things to its sides this much (0 to 1, see ConeOf). */
constexpr float ConeSpill = 0.25F;

/** @brief A fire lighting Tristram's evening: where it is (in tiles), and the open side it shines out of. */
struct TownFire {
	float x;
	float y;
	Colour colour;
	int radius;
	float height;
	float size;
	float fill;
	float power;
	float coneX;
	float coneY;
};

/** @brief The cathedral's doorway glows red from inside, the red the town's art paints it. */
constexpr Colour CathedralDoorColour { 1.00F, 0.18F, 0.16F };

/** @brief The fires and lamps in town, where the town's art paints their glow. */
constexpr std::array<TownFire, 14> TownFires { {
	// Griswold's forge: its coals glow at waist height at the back of the smithy, out over the square (+y).
	{ 63.F, 60.F, FireColour, 8, 0.9F, 0.6F, 0.2F, FlamePower, 0.F, 1.F },
	// The tavern's open door: the fire inside shines out past Ogden, who stands in the doorway (+y), so his shadow falls
	// out of the doorway in front of him.
	{ 54.85F, 61.55F, TorchColour, 7, 1.5F, 0.5F, 0.25F, 1.6F, 0.F, 1.F },
	// Lit windows and lamps shine a little onto the ground below them, out of the wall they're in. The tavern's two on
	// its side towards the smithy:
	{ 55.7F, 59.6F, CandleColour, 4, 1.4F, 0.4F, 0.2F, 1.F, 1.F, 0.F },
	{ 55.7F, 58.5F, CandleColour, 4, 1.4F, 0.4F, 0.2F, 1.F, 1.F, 0.F },
	// Pepin's three windows and the lamp beside his door:
	{ 50.4F, 80.7F, CandleColour, 4, 1.1F, 0.4F, 0.2F, 1.F, 0.F, 1.F },
	{ 52.6F, 80.7F, CandleColour, 4, 1.4F, 0.4F, 0.2F, 1.F, 0.F, 1.F },
	{ 54.7F, 76.3F, CandleColour, 4, 1.4F, 0.4F, 0.2F, 1.F, 1.F, 0.F },
	{ 54.7F, 79.1F, TorchColour, 4, 1.F, 0.3F, 0.2F, 1.F, 1.F, 0.F },
	// Adria's two windows:
	{ 78.7F, 19.3F, CandleColour, 4, 1.1F, 0.4F, 0.2F, 1.F, 1.F, 0.F },
	{ 78.7F, 17.4F, CandleColour, 4, 1.4F, 0.4F, 0.2F, 1.F, 1.F, 0.F },
	// The lamps by the doors of the three houses built alike:
	{ 42.7F, 66.25F, TorchColour, 4, 1.3F, 0.3F, 0.2F, 1.F, 1.F, 0.F },
	{ 54.7F, 44.25F, TorchColour, 4, 1.3F, 0.3F, 0.2F, 1.F, 1.F, 0.F },
	{ 72.7F, 80.25F, TorchColour, 4, 1.3F, 0.3F, 0.2F, 1.F, 1.F, 0.F },
	// The cathedral's doorway, red down the path in front of it (+y).
	{ 25.1F, 30.F, CathedralDoorColour, 6, 1.2F, 0.8F, 0.25F, 1.5F, 0.F, 1.F },
} };

Look CurrentLook;
bool HaveLook = false;
/** @brief Brightness of a light level (0 bright to 15 dark), in 256 steps. */
std::array<float, 256> BrightnessOfLevel {};
/** @brief The same as linear light, and linear light (0 to LinearTop, in 4096 steps) back to brightness as drawn. */
std::array<float, 256> LinearOfLevel {};
std::array<float, 4097> BrightnessOfLinear {};
/** @brief How warm (1) or cool (0) light of a brightness (0 to 1, in 256 steps) looks, as the old shade table had it. */
std::array<float, 256> WarmthOfBrightness {};
/** @brief The sky's light alone as drawn (see Sky), worked out with the rest of the light in Finish. */
Rgb SkyRgb;
/**
 * @brief The sky's light over Tristram this frame against the evening's own (1): less while it rains, and for a moment
 * much more when lightning strikes (idea 23). Linear light.
 */
float SkyScale = 1;

/** @brief Per tile: 0 where light and walkers pass, 1 where light stops (walls), 2 for ground light passes over that
 * nothing stands on (OpenGround: the Cathedral's dirt outside the rooms). */
std::array<uint8_t, MAXDUNX * MAXDUNY> BlockMap {};
uint32_t BlockVersion = 0;
bool (*OpenGround)(int x, int y) = nullptr;
/** @brief How many floor edges a sample of rock can be lit through (SpillRoots). */
constexpr size_t SpillWays = 3;
/**
 * @brief For each sample of the lattice in solid rock in front of a floor (towards the view) that no room faces, the
 * open samples at the floors' edges it is lit through (-1 for none): the nearest any way, and the nearest straight
 * behind it along y and along x. And the BlockVersion that was worked out for.
 */
std::vector<std::array<int32_t, SpillWays>> SpillRoots;
uint32_t SpillVersion = 0;

std::vector<Source> StaticSources;
std::vector<Source> DynamicSources;
/**
 * @brief What each static light that flickers adds to the level's light, sample by sample (as ShineStatic adds it), so
 * its light can move with its flame every frame without the level's light being worked out again.
 */
struct Patch {
	int i0 = 0;
	int j0 = 0;
	int w = 0;
	int h = 0;
	std::vector<float> linear;
};
std::vector<Patch> StaticPatches;

/** @brief How many times the light has been worked out, for UpdateCount. */
uint32_t Updates = 0;
/** @brief What the static light was worked out for: the level, its walls and the game's own baked light. */
struct StaticKey {
	int levelType = -1;
	int level = -1;
	bool setLevel = false;
	int setLevelNum = -1;
	uint32_t blockVersion = UINT32_MAX;
	/** @brief The lit objects and where they stand (a lever can light braziers without touching the baked light). */
	uint32_t objects = 0;
	/** @brief The Atmosphere option and its switches (the stairs' light is baked in). */
	uint32_t atmosphere = 0;
	bool operator==(const StaticKey &other) const = default;
};
StaticKey BuiltStatics;
std::array<uint8_t, MAXDUNX * MAXDUNY> BuiltPreLight {};
/** @brief The static lights' brightness at every sample, and their colours weighted by brightness squared (and the weight). */
std::vector<float> StaticBright;
std::vector<std::array<float, 4>> StaticHue;
/**
 * @brief The same for the tops of the rock round the rooms (RockTopsLit), at the rock's samples: each static light but
 * the lava by how far it is, with no wall in its way (see RockTopAt). Empty where the rock's tops don't show.
 */
std::vector<float> StaticTopBright;
std::vector<std::array<float, 4>> StaticTopHue;
/**
 * @brief The lava's own light at every sample (it is in StaticBright too), kept by itself so it can swell and fade each
 * frame (the Atmosphere option's idea 30).
 */
std::vector<float> StaticLava;
bool StaticHasLava = false;
/** @brief The daylight down the first floor's stairs (idea 24), where there is one. */
bool HaveDaylight = false;
float DaylightX = 0;
float DaylightY = 0;
float DaylightReach = 0;

/** @brief Which samples of the lattice a moving light reaches, kept while it doesn't move. */
struct Reach {
	int keyX = INT_MIN;
	int keyY = INT_MIN;
	int radius = -1;
	uint32_t blockVersion = UINT32_MAX;
	float size = -1;
	int i0 = 0;
	int j0 = 0;
	int w = 0;
	int h = 0;
	/** @brief How much of the light each sample sees (0 to 255), and how far its light goes round walls to get there. */
	std::vector<uint8_t> seen;
	std::vector<float> paths;
};
std::array<Reach, MAXLIGHTS> Reaches;

// This frame's window of the lattice: the part on screen.
int WinI0 = 0;
int WinJ0 = 0;
int WinW = 0;
int WinH = 0;
std::vector<float> WinBright;
std::vector<std::array<float, 4>> WinHue;
std::vector<uint8_t> WinSet;
std::vector<Rgb> WinColour;
std::vector<uint32_t> WinPixels;
std::vector<uint32_t> WinMulPixels;
/** @brief The rock tops' light in the window (StaticTopBright and the moving lights), and as drawn; empty where they don't show. */
std::vector<float> WinTopBright;
std::vector<std::array<float, 4>> WinTopHue;
std::vector<Rgb> WinTopColour;

float Luminance(const Colour &c)
{
	return 0.2126F * c[0] + 0.7152F * c[1] + 0.0722F * c[2];
}

/** @brief A light's colour, leaning towards the hero's light and as bright as it. */
Colour Even(const Colour &raw)
{
	Colour mixed;
	for (int c = 0; c < 3; c++)
		mixed[c] = CurrentLook.warm[c] + (raw[c] - CurrentLook.warm[c]) * ColourStrength;
	const float lum = Luminance(mixed);
	const float scale = lum > 0.01F ? Luminance(CurrentLook.warm) / lum : 1.F;
	for (float &channel : mixed)
		channel *= scale;
	return mixed;
}

Colour FromBytes(const std::array<uint8_t, 3> &bytes)
{
	return { bytes[0] / 255.F, bytes[1] / 255.F, bytes[2] / 255.F };
}

float LevelLinear(float level)
{
	const float step = std::clamp(level, 0.F, 15.F) * (255.F / 15.F);
	return LinearOfLevel[static_cast<size_t>(step + 0.5F)];
}

/** @brief Linear light as drawn: its brightness, easing off above full towards MaxLight. */
float DrawnBrightness(float linear)
{
	const float step = std::clamp(linear / LinearTop, 0.F, 1.F) * static_cast<float>(BrightnessOfLinear.size() - 1);
	const auto below = static_cast<size_t>(step);
	const size_t above = std::min(below + 1, BrightnessOfLinear.size() - 1);
	const float part = step - static_cast<float>(below);
	return BrightnessOfLinear[below] + (BrightnessOfLinear[above] - BrightnessOfLinear[below]) * part;
}

/** @brief The game's falloff (lighting.cpp DoLighting and light_tables.cpp), without its rounding to whole levels. */
float LevelAt(float distance, int radius)
{
	const float range = static_cast<float>(radius + 1);
	const float part = distance / range;
	if (part > 1.F)
		return 15.F;
	if (IsAnyOf(leveltype, DTYPE_NEST, DTYPE_CRYPT)) {
		const float overExposure = static_cast<float>(radius) * 1.25F;
		return std::max(0.F, part * part * overExposure + (15.F - overExposure));
	}
	return part * 15.F;
}

/**
 * @brief The plain tile under one of the shadows a level type paints into itself (the shadow passes in drlg_l1.cpp,
 * drlg_l2.cpp and drlg_l4.cpp ApplyShadowsPatterns and crypt.cpp ApplyCryptShadowsPatterns), 0 for any other tile.
 * @param mx, my Where the tile is, for picking one of the Cathedral's floor looks.
 */
uint8_t PlainTileOf(uint8_t tile, int mx, int my)
{
	switch (leveltype) {
	case DTYPE_CATHEDRAL:
		// Floor12-Floor21 lie on the floor; VWall17, VArch5, HWallShadow(2) and HArchShadow(2) on a wall or an arch.
		if ((tile >= 139 && tile <= 145) || (tile >= 150 && tile <= 152)) {
			// The plain floor has three looks (drlg_l1.cpp FillFloor), picked here by where the tile is.
			constexpr std::array<uint8_t, 3> Floors { 13, 162, 163 };
			return Floors[((static_cast<uint32_t>(mx) * 73856093U) ^ (static_cast<uint32_t>(my) * 19349663U)) % Floors.size()];
		}
		switch (tile) {
		case 146: return 1;  // VWall
		case 147: return 11; // VArch
		case 148:
		case 154: return 2; // HWall
		case 149:
		case 153: return 12; // HArch
		default: return 0;
		}
	case DTYPE_CATACOMBS:
		return IsAnyOf(tile, 48, 50) ? 3 : 0;
	case DTYPE_HELL:
		return IsAnyOf(tile, 47, 48) ? 6 : 0;
	case DTYPE_CRYPT:
		return tile >= 203 && tile <= 217 ? 13 : 0;
	default:
		return 0;
	}
}

bool BlocksTile(int x, int y)
{
	if (x < 0 || y < 0 || x >= MAXDUNX || y >= MAXDUNY)
		return true;
	return BlockMap[static_cast<size_t>(x) * MAXDUNY + y] != 0;
}

bool SampleOpen(int i, int j)
{
	return !BlocksTile(i / Per, j / Per);
}

/** @brief Light can't pass this tile: a wall, unlike the open ground outside the rooms that light passes over. */
bool BlocksSight(int x, int y)
{
	if (x < 0 || y < 0 || x >= MAXDUNX || y >= MAXDUNY)
		return true;
	return BlockMap[static_cast<size_t>(x) * MAXDUNY + y] == 1;
}

/** @brief Light goes from one world point to another without a wall in between (the tiles they're in don't count). */
bool Clear(float x0, float y0, float x1, float y1)
{
	int tx = static_cast<int>(std::floor(x0 + 0.5F));
	int ty = static_cast<int>(std::floor(y0 + 0.5F));
	const int ex = static_cast<int>(std::floor(x1 + 0.5F));
	const int ey = static_cast<int>(std::floor(y1 + 0.5F));
	const float dx = x1 - x0;
	const float dy = y1 - y0;
	const int sx = dx > 0 ? 1 : -1;
	const int sy = dy > 0 ? 1 : -1;
	constexpr float Never = 1e30F;
	const float adx = std::abs(dx);
	const float ady = std::abs(dy);
	const float stepX = adx > 1e-6F ? 1.F / adx : Never;
	const float stepY = ady > 1e-6F ? 1.F / ady : Never;
	// How far along the ray (0 to 1) the next tile edge each way is.
	float nextX = adx > 1e-6F ? (sx > 0 ? static_cast<float>(tx) + 0.5F - x0 : x0 - (static_cast<float>(tx) - 0.5F)) * stepX : Never;
	float nextY = ady > 1e-6F ? (sy > 0 ? static_cast<float>(ty) + 0.5F - y0 : y0 - (static_cast<float>(ty) - 0.5F)) * stepY : Never;
	int steps = std::abs(ex - tx) + std::abs(ey - ty);
	while (steps > 0) {
		if (nextX < nextY) {
			tx += sx;
			nextX += stepX;
			steps--;
		} else if (nextY < nextX) {
			ty += sy;
			nextY += stepY;
			steps--;
		} else {
			// Exactly through a corner: it squeezes past only if one of the two tiles beside the corner is open.
			if (BlocksSight(tx + sx, ty) && BlocksSight(tx, ty + sy))
				return false;
			tx += sx;
			ty += sy;
			nextX += stepX;
			nextY += stepY;
			steps -= 2;
		}
		if (tx == ex && ty == ey)
			return true;
		if (BlocksSight(tx, ty))
			return false;
	}
	return true;
}

int TileOf(float world)
{
	return static_cast<int>(std::floor(world + 0.5F));
}

/**
 * @brief How much of a light's flame or glow a world point sees past the walls, 0 to 1: rays from across its width,
 * so its shadows are sharp beside what casts them and soften further out, the more the wider it is.
 */
float SeenFraction(const Source &source, float x, float y)
{
	const int rays = source.size < 0.2F ? 1 : (source.size < 0.5F ? 3 : 5);
	const float dx = x - source.rayX;
	const float dy = y - source.rayY;
	const float length = Length(dx, dy);
	if (rays == 1 || length < 1e-3F)
		return Clear(source.rayX, source.rayY, x, y) ? 1.F : 0.F;
	// Across the light, square to the way to the point.
	const float acrossX = -dy / length;
	const float acrossY = dx / length;
	int tried = 0;
	int clear = 0;
	for (int ray = 0; ray < rays; ray++) {
		const float offset = source.size * ((static_cast<float>(ray) + 0.5F) / static_cast<float>(rays) - 0.5F);
		const float fromX = source.rayX + acrossX * offset;
		const float fromY = source.rayY + acrossY * offset;
		// A part of the flame inside a wall doesn't shine past it.
		if (BlocksTile(TileOf(fromX), TileOf(fromY)) && !(TileOf(fromX) == TileOf(source.rayX) && TileOf(fromY) == TileOf(source.rayY)))
			continue;
		tried++;
		if (Clear(fromX, fromY, x, y))
			clear++;
	}
	if (tried == 0)
		return Clear(source.rayX, source.rayY, x, y) ? 1.F : 0.F;
	return static_cast<float>(clear) / static_cast<float>(tried);
}

/**
 * @brief How far (in tiles) a light's light has to go round walls to reach each sample of a box of the lattice, for
 * the light that bounces round pillars and corners; samples it can't reach within its radius are left very far.
 */
void PathsFrom(const Source &source, int i0, int j0, int w, int h, std::vector<float> &paths)
{
	constexpr float Far = 1e30F;
	paths.assign(static_cast<size_t>(std::max(w, 0)) * std::max(h, 0), Far);
	if (w <= 0 || h <= 0)
		return;
	const float reach = static_cast<float>(source.radius + 1);
	using Step = std::pair<float, int>;
	std::priority_queue<Step, std::vector<Step>, std::greater<>> queue;
	// Start from the open samples round where the light starts.
	const int si = SampleBefore(source.rayX);
	const int sj = SampleBefore(source.rayY);
	for (int j = sj; j <= sj + 1; j++) {
		for (int i = si; i <= si + 1; i++) {
			const int a = i - i0;
			const int b = j - j0;
			if (a < 0 || b < 0 || a >= w || b >= h || !SampleOpen(i, j))
				continue;
			const float start = Length(SampleAt(i) - source.rayX, SampleAt(j) - source.rayY);
			const int k = b * w + a;
			if (start < paths[static_cast<size_t>(k)]) {
				paths[static_cast<size_t>(k)] = start;
				queue.emplace(start, k);
			}
		}
	}
	constexpr float Straight = 1.F / Per;
	constexpr float Diagonal = 1.41421356F / Per;
	while (!queue.empty()) {
		const auto [distance, k] = queue.top();
		queue.pop();
		if (distance > paths[static_cast<size_t>(k)] || distance > reach)
			continue;
		const int a = k % w;
		const int b = k / w;
		for (int db = -1; db <= 1; db++) {
			for (int da = -1; da <= 1; da++) {
				if (da == 0 && db == 0)
					continue;
				const int na = a + da;
				const int nb = b + db;
				if (na < 0 || nb < 0 || na >= w || nb >= h || !SampleOpen(i0 + na, j0 + nb))
					continue;
				// Round a corner only where both samples beside it are open.
				if (da != 0 && db != 0 && (!SampleOpen(i0 + na, j0 + b) || !SampleOpen(i0 + a, j0 + nb)))
					continue;
				const float next = distance + (da != 0 && db != 0 ? Diagonal : Straight);
				const int n = nb * w + na;
				if (next < paths[static_cast<size_t>(n)]) {
					paths[static_cast<size_t>(n)] = next;
					queue.emplace(next, n);
				}
			}
		}
	}
}

/** @brief A light's linear light at a sample it sees this much of, with the light bouncing in where it sees less. */
float SampleLinear(const Source &source, float distance, float seen, float path)
{
	float linear = 0;
	if (seen > 0)
		linear = LevelLinear(LevelAt(distance, source.radius)) * seen;
	if (seen < 1 && path <= static_cast<float>(source.radius + 1))
		linear += LevelLinear(LevelAt(path, source.radius)) * source.fill * (1 - seen);
	return linear * source.power;
}

/** @brief How much of a light shining out of an opening reaches a point: all of it straight out, less to its sides, none behind it. */
float ConeOf(const Source &source, float x, float y)
{
	if (source.coneX == 0 && source.coneY == 0)
		return 1.F;
	const float dx = x - source.rayX;
	const float dy = y - source.rayY;
	const float length = Length(dx, dy);
	if (length < 0.35F)
		return 1.F;
	const float facing = (dx * source.coneX + dy * source.coneY) / length;
	return std::clamp((facing + ConeSpill) / (1 + ConeSpill), 0.F, 1.F);
}

/**
 * @brief A light inside a wall tile (a torch hanging on it) shines from just in front of the wall, into the room the
 * camera sees: the side a torch faces, else the tile's +x or +y side (Diablo stands walls on the back edges).
 */
void PlaceRays(Source &source)
{
	source.rayX = source.x;
	source.rayY = source.y;
	const int tx = static_cast<int>(std::floor(source.x + 0.5F));
	const int ty = static_cast<int>(std::floor(source.y + 0.5F));
	if (!BlocksTile(tx, ty))
		return;
	if ((source.faceX != 0 || source.faceY != 0) && !BlocksTile(tx + source.faceX, ty + source.faceY)) {
		source.rayX = static_cast<float>(tx) + 0.55F * static_cast<float>(source.faceX);
		source.rayY = static_cast<float>(ty) + 0.55F * static_cast<float>(source.faceY);
		return;
	}
	// Lava glows all round; a wall light that faces nowhere open shines into the room in front, never the one behind.
	const size_t sides = source.castsShadows ? 3 : 8;
	constexpr std::array<std::array<int, 2>, 8> Sides { { { 1, 1 }, { 1, 0 }, { 0, 1 }, { -1, 1 }, { 1, -1 }, { -1, 0 }, { 0, -1 }, { -1, -1 } } };
	for (size_t s = 0; s < sides; s++) {
		const auto &side = Sides[s];
		if (side[0] != 0 && side[1] != 0 && (BlocksTile(tx + side[0], ty) || BlocksTile(tx, ty + side[1])))
			continue;
		if (BlocksTile(tx + side[0], ty + side[1]))
			continue;
		source.rayX = static_cast<float>(tx) + 0.55F * static_cast<float>(side[0]);
		source.rayY = static_cast<float>(ty) + 0.55F * static_cast<float>(side[1]);
		return;
	}
}

/** @brief The look's light levels are as good as the same (colour cycling nudges them, and rebuilding the light for that would stutter). */
bool SameLevels(const std::array<float, NumLightingLevels> &a, const std::array<float, NumLightingLevels> &b)
{
	for (size_t level = 0; level < NumLightingLevels; level++) {
		if (std::abs(a[level] - b[level]) > 1.F / 512)
			return false;
	}
	return true;
}

void UpdateLook(const Look &look)
{
	// The screen's fade changes from frame to frame and only goes into the finished colours.
	CurrentLook.tint = look.tint;
	if (HaveLook && SameLevels(look.levelBrightness, CurrentLook.levelBrightness) && look.falloff == CurrentLook.falloff && look.warm == CurrentLook.warm && look.cool == CurrentLook.cool)
		return;
	CurrentLook = look;
	HaveLook = true;
	for (size_t step = 0; step < BrightnessOfLevel.size(); step++) {
		const float level = static_cast<float>(step) * 15.F / 255.F;
		const auto below = static_cast<size_t>(level);
		const size_t above = std::min<size_t>(below + 1, NumLightingLevels - 1);
		const float part = level - static_cast<float>(below);
		const float brightness = look.levelBrightness[below] + (look.levelBrightness[above] - look.levelBrightness[below]) * part;
		BrightnessOfLevel[step] = std::pow(std::max(brightness, 0.F), look.falloff);
		LinearOfLevel[step] = ToLinear(BrightnessOfLevel[step]);
	}
	for (size_t step = 0; step < BrightnessOfLinear.size(); step++) {
		const float brightness = ToBrightness(LinearTop * static_cast<float>(step) / static_cast<float>(BrightnessOfLinear.size() - 1));
		// Above full it eases off, the way a camera's highlights do, never past MaxLight.
		constexpr float Headroom = MaxLight - 1;
		BrightnessOfLinear[step] = brightness <= 1 ? brightness : 1 + Headroom * std::tanh((brightness - 1) / Headroom);
	}
	// The old shade table leaned warm by light level (warm at 0, cool from 10 on): find the level of each brightness.
	for (size_t step = 0; step < WarmthOfBrightness.size(); step++) {
		const float brightness = static_cast<float>(step) / 255.F;
		size_t level = 0;
		while (level + 1 < BrightnessOfLevel.size() && BrightnessOfLevel[level] > brightness)
			level++;
		const float levelValue = static_cast<float>(level) * 15.F / 255.F;
		WarmthOfBrightness[step] = std::clamp(1.F - levelValue / 10.F, 0.F, 1.F);
	}
	// Light colours are evened out against the warm colour, so the light worked out with the old one is stale.
	BuiltStatics = {};
	for (Reach &reach : Reaches)
		reach.radius = -1;
}

/**
 * @brief Finds the tiles that stop light, and the ground outside the rooms that light passes over (the Cathedral's dirt,
 * see SetOpenGround). Stopped by the dirt as by a wall, the light of a hero at a room's open corner left the room
 * across the dirt black, and the dirt drawn beside them with it, from a straight line on.
 */
/**
 * @brief Idea 47: a room coming up from the dark behind a door that has just opened. The light as it was the frame before,
 * which what the door lets through is faded in over.
 */
struct Reveal {
	std::vector<float> bright;
	std::vector<std::array<float, 4>> hue;
	int i0;
	int j0;
	int w;
	int h;
	float x;
	float y;
	uint32_t start;
};
std::vector<Reveal> Reveals;
/** @brief The floor's light as the last frame showed it, and its window, while doors are watched. */
std::vector<float> ShownBright;
std::vector<std::array<float, 4>> ShownHue;
int ShownI0 = 0;
int ShownJ0 = 0;
int ShownW = 0;
int ShownH = 0;
/** @brief How far from its door a reveal reaches, in tiles: as far as a hero's light, fading out over the last two. */
constexpr float RevealReach = 11;

void StartReveal(int x, int y)
{
	moments::DoorOpened(x, y);
	if (ShownBright.empty() || ShownW <= 0)
		return;
	if (Reveals.size() >= 3)
		Reveals.erase(Reveals.begin());
	Reveals.push_back({ ShownBright, ShownHue, ShownI0, ShownJ0, ShownW, ShownH, static_cast<float>(x), static_cast<float>(y), SDL_GetTicks() });
}

/** @brief Holds the light a newly opened door lets through back to what was there before it opened, less and less. */
void ApplyReveals()
{
	const float length = moments::DoorRevealMs();
	if (length <= 0) {
		Reveals.clear();
		ShownBright.clear();
		return;
	}
	const uint32_t now = SDL_GetTicks();
	for (size_t r = 0; r < Reveals.size();) {
		const Reveal &reveal = Reveals[r];
		const float t = static_cast<float>(now - reveal.start) / length;
		if (t >= 1) {
			Reveals.erase(Reveals.begin() + static_cast<ptrdiff_t>(r));
			continue;
		}
		r++;
		const float held = 1 - t * t * (3 - 2 * t);
		const int i0 = std::max({ WinI0, reveal.i0, SampleBefore(reveal.x - RevealReach) });
		const int i1 = std::min({ WinI0 + WinW, reveal.i0 + reveal.w, SampleBefore(reveal.x + RevealReach) + 2 });
		const int j0 = std::max({ WinJ0, reveal.j0, SampleBefore(reveal.y - RevealReach) });
		const int j1 = std::min({ WinJ0 + WinH, reveal.j0 + reveal.h, SampleBefore(reveal.y + RevealReach) + 2 });
		for (int j = j0; j < j1; j++) {
			const float dy = SampleAt(j) - reveal.y;
			for (int i = i0; i < i1; i++) {
				const size_t k = static_cast<size_t>(j - WinJ0) * WinW + static_cast<size_t>(i - WinI0);
				const size_t was = static_cast<size_t>(j - reveal.j0) * reveal.w + static_cast<size_t>(i - reveal.i0);
				const float more = WinBright[k] - reveal.bright[was];
				if (more <= 0)
					continue;
				const float distance = Length(SampleAt(i) - reveal.x, dy);
				const float hold = held * std::clamp((RevealReach - distance) / 2, 0.F, 1.F);
				if (hold <= 0)
					continue;
				WinBright[k] -= more * hold;
				for (int c = 0; c < 4; c++)
					WinHue[k][c] -= (WinHue[k][c] - reveal.hue[was][c]) * hold;
			}
		}
	}
	ShownBright = WinBright;
	ShownHue = WinHue;
	ShownI0 = WinI0;
	ShownJ0 = WinJ0;
	ShownW = WinW;
	ShownH = WinH;
}

void UpdateBlocks()
{
	std::array<uint8_t, MAXDUNX * MAXDUNY> blocks;
	for (int x = 0; x < MAXDUNX; x++) {
		for (int y = 0; y < MAXDUNY; y++) {
			uint8_t block = 0;
			if (TileHasAny({ x, y }, TileProperties::BlockLight))
				block = OpenGround != nullptr && OpenGround(x, y) ? 2 : 1;
			blocks[static_cast<size_t>(x) * MAXDUNY + y] = block;
		}
	}
	if (blocks != BlockMap) {
		// Idea 47: a door that stopped the light a frame ago and lets it through now has just been opened. (A new
		// level changes far more of the map than a few doors.)
		if (moments::DoorRevealMs() > 0) {
			std::array<Point, 4> opened;
			size_t count = 0;
			for (int x = 0; x < MAXDUNX && count <= opened.size(); x++) {
				for (int y = 0; y < MAXDUNY; y++) {
					const size_t k = static_cast<size_t>(x) * MAXDUNY + y;
					if (blocks[k] == BlockMap[k])
						continue;
					const Object *object = blocks[k] == 0 ? FindObjectAtPosition({ x, y }) : nullptr;
					if (object == nullptr || !object->isDoor() || count == opened.size()) {
						count = opened.size() + 1;
						break;
					}
					opened[count++] = { x, y };
				}
			}
			if (count <= opened.size()) {
				for (size_t i = 0; i < count; i++)
					StartReveal(opened[i].x, opened[i].y);
			}
		}
		BlockMap = blocks;
		BlockVersion++;
	}
}

/** @brief How far (in samples) in front of a floor's edge its light reaches into the rock: as far as any light shines. */
constexpr int SpillSteps = static_cast<int>(NumLightRadiuses) * Per;

/**
 * @brief Works out which rock the floors' light reaches into (SpillRoots) when the walls change.
 *
 * FillWalls gives rock that faces a room (an open sample at most three samples in front of it) that room's light. Rock
 * further in front of a floor, where the floor ends without a wall (the Cathedral's dirt edges, drawn as a field of dirt
 * below the floor), is lit as the floor would be there: by each light as far from it as it is, seeing the lights the
 * floor's edge behind it sees. Left dark, the dirt showed only its first row (lit from behind, see hd.cpp's
 * FromBehind), as blocks with black lines between them, and a light fading out from the edge left the same lines a
 * row or two further down.
 *
 * Rock in front of two rooms' edges (the Cathedral's new rooms, a dark room beside the lit one) is lit through both,
 * each light through whichever edge sees it. Lit only through the nearest edge, the dirt in front of the hero's room
 * went black from a straight line on, wherever a dark room's edge was nearer.
 */
void UpdateSpill()
{
	if (SpillVersion == BlockVersion && !SpillRoots.empty())
		return;
	SpillVersion = BlockVersion;
	const size_t count = static_cast<size_t>(LatticeW) * LatticeH;
	// As FillWalls marks them: 1 for open samples, 2 to 4 for rock lit by the room in front of it.
	std::vector<uint8_t> set(count, 0);
	for (int j = 0; j < LatticeH; j++) {
		for (int i = 0; i < LatticeW; i++)
			set[static_cast<size_t>(j) * LatticeW + i] = SampleOpen(i, j) ? 1 : 0;
	}
	for (uint8_t pass = 1; pass <= 3; pass++) {
		for (int j = 0; j < LatticeH; j++) {
			for (int i = 0; i < LatticeW; i++) {
				const size_t k = static_cast<size_t>(j) * LatticeW + i;
				if (set[k] != 0)
					continue;
				for (const auto &step : { std::array<int, 2> { 1, 0 }, std::array<int, 2> { 0, 1 }, std::array<int, 2> { 1, 1 } }) {
					const int ni = i + step[0];
					const int nj = j + step[1];
					if (ni >= LatticeW || nj >= LatticeH)
						continue;
					const uint8_t front = set[static_cast<size_t>(nj) * LatticeW + ni];
					if (front != 0 && front <= pass) {
						set[k] = pass + 1;
						break;
					}
				}
			}
		}
	}
	SpillRoots.assign(count, { -1, -1, -1 });
	// Steps from the edge, a step further each pass, and the samples the last pass reached.
	std::vector<uint8_t> depth(count, 0);
	std::vector<size_t> last;
	std::vector<size_t> reached;
	const auto reach = [&](int i, int j, int pass) {
		const size_t k = static_cast<size_t>(j) * LatticeW + i;
		if (set[k] != 0 || depth[k] != 0)
			return;
		// Straight behind first, so rock in front of a straight edge is lit through the edge right behind it.
		for (const auto &step : { std::array<int, 2> { 0, 1 }, std::array<int, 2> { 1, 0 }, std::array<int, 2> { 1, 1 } }) {
			const size_t n = static_cast<size_t>(j - step[1]) * LatticeW + (i - step[0]);
			if (pass == 1 ? set[n] != 1 : depth[n] != pass - 1)
				continue;
			SpillRoots[k][0] = pass == 1 ? static_cast<int32_t>(n) : SpillRoots[n][0];
			depth[k] = static_cast<uint8_t>(pass);
			reached.push_back(k);
			return;
		}
	};
	for (int j = 1; j < LatticeH; j++) {
		for (int i = 1; i < LatticeW; i++)
			reach(i, j, 1);
	}
	for (int pass = 2; pass <= SpillSteps && !reached.empty(); pass++) {
		last.swap(reached);
		reached.clear();
		for (const size_t k : last) {
			const int i = static_cast<int>(k % LatticeW);
			const int j = static_cast<int>(k / LatticeW);
			for (const auto &step : { std::array<int, 2> { 1, 0 }, std::array<int, 2> { 0, 1 }, std::array<int, 2> { 1, 1 } }) {
				if (i + step[0] < LatticeW && j + step[1] < LatticeH)
					reach(i + step[0], j + step[1], pass);
			}
		}
	}
	// And the edge straight behind, each way: the rock right in front of a room's edge belongs to that room's light too,
	// even where another room's edge is nearer.
	const auto straight = [&](int i, int j, size_t way, int32_t &edge, int &run) {
		const size_t k = static_cast<size_t>(j) * LatticeW + i;
		if (set[k] == 1) {
			edge = static_cast<int32_t>(k);
			run = 0;
			return;
		}
		if (set[k] != 0 || edge < 0 || ++run > SpillSteps) {
			edge = -1;
			return;
		}
		std::array<int32_t, SpillWays> &roots = SpillRoots[k];
		if (std::find(roots.begin(), roots.end(), edge) == roots.end())
			roots[way] = edge;
	};
	for (int i = 0; i < LatticeW; i++) {
		int32_t edge = -1;
		int run = 0;
		for (int j = 0; j < LatticeH; j++)
			straight(i, j, 1, edge, run);
	}
	for (int j = 0; j < LatticeH; j++) {
		int32_t edge = -1;
		int run = 0;
		for (int i = 0; i < LatticeW; i++)
			straight(i, j, 2, edge, run);
	}
}

/**
 * @brief The open samples at the floors' edges a sample of rock is lit through (SpillRoots), as indexes into a box of the
 * lattice (w by h samples from i0, j0), and how far the sample is from each. None where the floors' light doesn't reach
 * into the sample, or the edges are outside the box.
 * @return How many edges there are.
 */
int EdgesInBox(int i, int j, int i0, int j0, int w, int h, std::array<int, SpillWays> &edges, std::array<float, SpillWays> &extras)
{
	if (SpillRoots.empty())
		return 0;
	int count = 0;
	for (const int32_t root : SpillRoots[static_cast<size_t>(j) * LatticeW + i]) {
		if (root < 0)
			continue;
		const int ri = root % LatticeW;
		const int rj = root / LatticeW;
		if (ri < i0 || rj < j0 || ri >= i0 + w || rj >= j0 + h)
			continue;
		extras[static_cast<size_t>(count)] = Length(SampleAt(i) - SampleAt(ri), SampleAt(j) - SampleAt(rj));
		edges[static_cast<size_t>(count)] = (rj - j0) * w + (ri - i0);
		count++;
	}
	return count;
}

/**
 * @brief The sample is ground outside the rooms that light passes over (the Cathedral's dirt, see SetOpenGround), where
 * the floors' light reaches it so it is drawn: the lights that see it light it as they light the floor.
 */
bool GroundSample(int i, int j)
{
	if (SpillRoots.empty() || BlockMap[static_cast<size_t>(i / Per) * MAXDUNY + j / Per] != 2)
		return false;
	const std::array<int32_t, SpillWays> &roots = SpillRoots[static_cast<size_t>(j) * LatticeW + i];
	return roots[0] >= 0 || roots[1] >= 0 || roots[2] >= 0;
}

/**
 * @brief The light a dungeon object gives (the ones lighting.cpp's AddObjectLight bakes into the level). Heights and
 * flame widths are measured from the objects' pictures, in tiles (a hero stands about 1.5 tall in the same measure).
 */
bool ObjectLight(const Object &object, Source &source)
{
	switch (object._otype) {
	case OBJ_STORYCANDLE:
	case OBJ_L5CANDLE:
		source.radius = 3;
		source.colour = CandleColour;
		source.height = 1.15F;
		source.size = 0.08F;
		source.flickerKind = CandleFlicker;
		return true;
	case OBJ_L1LIGHT:
		// A brazier on its stand.
		source.radius = 5;
		source.colour = CandelabraColour;
		source.height = 1.6F;
		source.size = 0.3F;
		source.power = FlamePower;
		source.flickerKind = FireFlicker;
		return true;
	case OBJ_SKFIRE:
		// A fire on the floor.
		source.radius = 5;
		source.colour = FireColour;
		source.height = 0.3F;
		source.size = 0.6F;
		source.fill = 0.2F;
		source.power = FlamePower;
		source.flickerKind = FireFlicker;
		return true;
	case OBJ_CANDLE1:
	case OBJ_CANDLE2:
	case OBJ_BOOKCANDLE:
		source.radius = 5;
		source.colour = CandleColour;
		source.height = 1.15F;
		source.size = 0.08F;
		source.flickerKind = CandleFlicker;
		return true;
	case OBJ_BCROSS:
	case OBJ_TBCROSS:
		source.radius = 5;
		source.colour = BurningCrossColour;
		source.height = 1.0F;
		source.size = 0.6F;
		source.fill = 0.2F;
		source.power = FlamePower;
		source.flickerKind = FireFlicker;
		return true;
	case OBJ_TORCHL2:
	case OBJ_TORCHR:
	case OBJ_TORCHR2:
	case OBJ_TORCHL:
		// A torch hangs on its wall a little below a hero's head height.
		source.radius = 8;
		source.colour = TorchColour;
		source.height = 1.45F;
		source.size = 0.2F;
		source.power = FlamePower;
		source.flickerKind = TorchFlicker;
		// Each torch hangs on its own wall tile, facing the way its picture does.
		if (IsAnyOf(object._otype, OBJ_TORCHL2, OBJ_TORCHR))
			source.faceX = 1;
		else
			source.faceY = 1;
		return true;
	default:
		return false;
	}
}

/** @brief The lava that lights itself (drlg_l3.cpp PlaceCaveLights and PlaceHiveLights, crypt.cpp PlaceCryptLights). */
int LavaRadius(int piece)
{
	switch (leveltype) {
	case DTYPE_CAVES:
		if ((piece >= 55 && piece <= 146) || (piece >= 153 && piece <= 160) || piece == 149 || piece == 151)
			return 7;
		return 0;
	case DTYPE_NEST:
		return piece >= 381 && piece <= 456 ? 9 : 0;
	case DTYPE_CRYPT: {
		static constexpr int CryptLava[] = {
			124, 128, 130, 132, 133, 134, 135, 139, 141, 143, 145, 156, 164, 166,
			167, 168, 169, 170, 182, 190, 192, 195, 196, 199, 200, 254, 266, 273,
			276, 281, 282, 283, 284, 285, 286, 287, 288, 290, 302, 316, 434, 435,
			436, 437, 445, 446, 447, 453, 457, 460, 466, 470, 477, 479, 484, 485,
			486, 490, 507, 537, 557, 559, 561, 563, 564, 568, 569, 572, 578, 580,
			584, 585, 589, 592, 593, 594, 595, 596, 597, 598, 599, 600, 601
		};
		return std::find(std::begin(CryptLava), std::end(CryptLava), piece) != std::end(CryptLava) ? 3 : 0;
	}
	default:
		return 0;
	}
}

/**
 * @brief Adds a static light to the level's light. Lava instead goes into `lava`, where each sample keeps the brightest of
 * its tiles, as the game draws it: every lava tile is a light as strong as a torch, so added up a stream of it would
 * shine about ten times as bright as the game shows it.
 */
void ShineStatic(const Source &source, std::vector<float> &lava, Patch *patch)
{
	const float reach = static_cast<float>(source.radius + 1);
	const int i0 = std::max(0, SampleBefore(source.x - reach));
	const int i1 = std::min(LatticeW - 1, SampleBefore(source.x + reach) + 1);
	const int j0 = std::max(0, SampleBefore(source.y - reach));
	const int j1 = std::min(LatticeH - 1, SampleBefore(source.y + reach) + 1);
	const int w = i1 - i0 + 1;
	const int h = j1 - j0 + 1;
	if (patch != nullptr) {
		patch->i0 = i0;
		patch->j0 = j0;
		patch->w = w;
		patch->h = h;
		patch->linear.assign(static_cast<size_t>(std::max(w, 0)) * std::max(h, 0), 0.F);
	}
	static std::vector<float> paths;
	PathsFrom(source, i0, j0, w, h, paths);
	// How much of the light each open sample sees, for the rock lit through the floors' edges (EdgesInBox). An edge comes
	// before the rock in front of it in this order.
	static std::vector<float> seenAt;
	seenAt.assign(paths.size(), 0.F);
	for (int j = j0; j <= j1; j++) {
		const float sy = SampleAt(j);
		for (int i = i0; i <= i1; i++) {
			const float sx = SampleAt(i);
			const float distance = Length(sx - source.x, sy - source.y);
			if (distance >= reach)
				continue;
			const size_t b = static_cast<size_t>(j - j0) * w + (i - i0);
			float linear = 0;
			const bool open = SampleOpen(i, j);
			if (open || GroundSample(i, j)) {
				const float seen = SeenFraction(source, sx, sy);
				seenAt[b] = seen;
				linear = SampleLinear(source, distance, seen, paths[b]);
			}
			if (!open) {
				// Rock in front of the floors' edges: as much light as the edge that lets the most through.
				std::array<int, SpillWays> edges;
				std::array<float, SpillWays> extras;
				const int count = EdgesInBox(i, j, i0, j0, w, h, edges, extras);
				for (int e = 0; e < count; e++) {
					const auto edge = static_cast<size_t>(edges[static_cast<size_t>(e)]);
					linear = std::max(linear, SampleLinear(source, distance, seenAt[edge], paths[edge] + extras[static_cast<size_t>(e)]));
				}
			}
			linear *= ConeOf(source, sx, sy);
			if (linear <= 1e-5F)
				continue;
			const size_t k = static_cast<size_t>(j) * LatticeW + i;
			if (source.lava) {
				lava[k] = std::max(lava[k], linear);
				continue;
			}
			StaticBright[k] += linear;
			for (int c = 0; c < 3; c++)
				StaticHue[k][c] += source.colour[c] * linear;
			StaticHue[k][3] += linear;
			if (patch != nullptr)
				patch->linear[b] = linear;
		}
	}
}

/** @brief A light's linear light on the top of the rock this far from it, as the game lights the rock: nothing in its way. */
float TopLinear(const Source &source, float distance)
{
	return LevelLinear(LevelAt(distance, source.radius)) * source.power;
}

/** @brief Adds a static light to the rock tops' light (StaticTopBright). */
void ShineTop(const Source &source)
{
	const float reach = static_cast<float>(source.radius + 1);
	const int i0 = std::max(0, SampleBefore(source.x - reach));
	const int i1 = std::min(LatticeW - 1, SampleBefore(source.x + reach) + 1);
	const int j0 = std::max(0, SampleBefore(source.y - reach));
	const int j1 = std::min(LatticeH - 1, SampleBefore(source.y + reach) + 1);
	for (int j = j0; j <= j1; j++) {
		const float sy = SampleAt(j);
		for (int i = i0; i <= i1; i++) {
			if (SampleOpen(i, j))
				continue;
			const float sx = SampleAt(i);
			const float distance = Length(sx - source.x, sy - source.y);
			if (distance >= reach)
				continue;
			const float linear = TopLinear(source, distance) * ConeOf(source, sx, sy);
			if (linear <= 1e-5F)
				continue;
			const size_t k = static_cast<size_t>(j) * LatticeW + i;
			StaticTopBright[k] += linear;
			for (int c = 0; c < 3; c++)
				StaticTopHue[k][c] += source.colour[c] * linear;
			StaticTopHue[k][3] += linear;
		}
	}
}

/**
 * @brief The object gives the level a light the game baked in (AddObjectLight). Old saves can hold lights that come
 * and go instead (they're among the moving lights), and braziers a lever adds aren't baked, so the game doesn't light them.
 */
bool BakedObjectLight(const Object &object, Source &source)
{
	if (!ObjectLight(object, source) || object._oVar1 != -1)
		return false;
	const Point tile = object.position;
	if (tile.x < 0 || tile.y < 0 || tile.x >= MAXDUNX || tile.y >= MAXDUNY)
		return false;
	return dPreLight[tile.x][tile.y] <= LightFalloffs[source.radius][0];
}

uint32_t ObjectsSignature()
{
	uint32_t hash = 2166136261U;
	for (int i = 0; i < ActiveObjectCount; i++) {
		const Object &object = Objects[ActiveObjects[i]];
		Source source;
		if (!ObjectLight(object, source))
			continue;
		for (const uint32_t part : { static_cast<uint32_t>(object._otype), static_cast<uint32_t>(object.position.x), static_cast<uint32_t>(object.position.y), static_cast<uint32_t>(object._oVar1) })
			hash = (hash ^ part) * 16777619U;
	}
	return hash;
}

/**
 * @brief The Atmosphere option's stairs (idea 48): stairs down glow dully from below, redder and stronger in each area
 * further down, breathing slowly; stairs up take a faint cold light from above. Their numbers are the area's in
 * atmosphere.tsv (stairs_down, stairs_down_colour, stairs_down_radius, stairs_up, stairs_up_colour).
 */
void AddStairsLights()
{
	HaveDaylight = false;
	const float strength = atmosphere::Strength(atmosphere::Knob::Stairs);
	// Idea 24: on the Cathedral's first floor the stairs up lead out to town, and cold grey daylight falls down them.
	const bool firstFloor = leveltype == DTYPE_CATHEDRAL && currlevel == 1 && !setlevel;
	const float daylight = firstFloor ? atmosphere::Value("daylight", 0) * atmosphere::Strength(atmosphere::Knob::Daylight) : 0.F;
	if ((strength <= 0 && daylight <= 0.01F) || leveltype == DTYPE_TOWN)
		return;
	const float down = atmosphere::Value("stairs_down", 0.35F) * strength;
	const float up = atmosphere::Value("stairs_up", 0.4F) * strength;
	const Colour downColour = Even(atmosphere::Colour("stairs_down_colour", { 1.F, 0.42F, 0.2F }));
	const Colour upColour = Even(atmosphere::Colour("stairs_up_colour", { 0.75F, 0.85F, 1.F }));
	const int downRadius = std::clamp(static_cast<int>(std::lround(atmosphere::Value("stairs_down_radius", 4))), 1, static_cast<int>(NumLightRadiuses) - 1);
	const int upRadius = std::clamp(static_cast<int>(std::lround(atmosphere::Value("stairs_up_radius", 3))), 1, static_cast<int>(NumLightRadiuses) - 1);
	for (int i = 0; i < numtrigs; i++) {
		const TriggerStruct &trigger = trigs[i];
		const bool goesDown = trigger._tmsg == WM_DIABNEXTLVL;
		const bool goesUp = IsAnyOf(trigger._tmsg, WM_DIABPREVLVL, WM_DIABTWARPUP, WM_DIABRTNLVL);
		if (!goesDown && !goesUp)
			continue;
		if (trigger._tmsg == WM_DIABPREVLVL && daylight > 0.01F) {
			// Hung high and wide, so it falls from above and its shadows are soft.
			Source source;
			source.x = static_cast<float>(trigger.position.x);
			source.y = static_cast<float>(trigger.position.y);
			source.power = daylight;
			source.radius = std::clamp(static_cast<int>(std::lround(atmosphere::Value("daylight_radius", 6))), 1, static_cast<int>(NumLightRadiuses) - 1);
			source.colour = Even(atmosphere::Colour("daylight_colour", { 0.8F, 0.88F, 1.F }));
			source.height = 3.F;
			source.size = 1.6F;
			source.fill = 0.45F;
			PlaceRays(source);
			StaticSources.push_back(source);
			HaveDaylight = true;
			DaylightX = source.x;
			DaylightY = source.y;
			DaylightReach = static_cast<float>(source.radius);
			continue;
		}
		const float power = goesDown ? down : up;
		if (power <= 0.01F)
			continue;
		Source source;
		source.x = static_cast<float>(trigger.position.x);
		source.y = static_cast<float>(trigger.position.y);
		source.power = power;
		source.castsShadows = false;
		if (goesDown) {
			// From below the floor, wide and soft.
			source.radius = downRadius;
			source.colour = downColour;
			source.height = 0.1F;
			source.size = 1.F;
			source.fill = 0.4F;
			source.flickerKind = StairsBreath;
			source.flickerSeed = (static_cast<uint32_t>(trigger.position.x) * 73856093U) ^ (static_cast<uint32_t>(trigger.position.y) * 19349663U);
		} else {
			// From the floor above, down the stairwell.
			source.radius = upRadius;
			source.colour = upColour;
			source.height = 2.F;
			source.size = 1.2F;
			source.fill = 0.4F;
		}
		PlaceRays(source);
		StaticSources.push_back(source);
	}
}

/**
 * @brief The famous rooms' own light (idea 46), the Butcher's for now: a dull red light inside his room that the walls
 * and his closed door hold in, so it spills out when the door opens, and a faint red glow just outside each closed door.
 */
void AddRoomLights()
{
	const float strength = atmosphere::Strength(atmosphere::Knob::Rooms);
	const std::optional<WorldTileRectangle> butcher = atmosphere::ButcherRoom();
	if (strength <= 0 || !butcher)
		return;
	const WorldTileRectangle room = *butcher;
	const WorldTilePosition corner = room.position;
	const float width = static_cast<float>(room.size.width);
	const float height = static_cast<float>(room.size.height);
	const float cx = static_cast<float>(corner.x) + width / 2 - 0.5F;
	const float cy = static_cast<float>(corner.y) + height / 2 - 0.5F;
	const Colour colour = Even(atmosphere::RoomColour("butcher", "light_colour", { 1.F, 0.18F, 0.1F }));
	const float power = atmosphere::RoomValue("butcher", "light", 1.4F) * strength;
	if (power > 0.01F) {
		Source source;
		source.x = cx;
		source.y = cy;
		source.power = power;
		source.colour = colour;
		source.radius = std::clamp(static_cast<int>(std::lround(atmosphere::RoomValue("butcher", "light_radius", 6))), 1, static_cast<int>(NumLightRadiuses) - 1);
		source.height = 1.2F;
		source.size = 0.8F;
		source.fill = 0.3F;
		source.flickerKind = FireFlicker;
		source.flickerSeed = (static_cast<uint32_t>(corner.x) * 73856093U) ^ (static_cast<uint32_t>(corner.y) * 19349663U);
		PlaceRays(source);
		StaticSources.push_back(source);
	}
	const float door = atmosphere::RoomValue("butcher", "door_light", 0.45F) * strength;
	if (door <= 0.01F)
		return;
	for (int i = 0; i < ActiveObjectCount; i++) {
		const Object &object = Objects[ActiveObjects[i]];
		if (!object.isDoor() || object._oVar4 != 0 /* closed */ || !room.contains(WorldTilePosition(object.position)))
			continue;
		// Just outside the door: away from the room's middle along the axis the door faces.
		const float dx = static_cast<float>(object.position.x) - cx;
		const float dy = static_cast<float>(object.position.y) - cy;
		Source source;
		source.x = static_cast<float>(object.position.x) + (std::abs(dx) >= std::abs(dy) ? (dx > 0 ? 0.6F : -0.6F) : 0.F);
		source.y = static_cast<float>(object.position.y) + (std::abs(dx) >= std::abs(dy) ? 0.F : (dy > 0 ? 0.6F : -0.6F));
		source.power = door;
		source.colour = colour;
		source.radius = 2;
		source.height = 0.05F;
		source.size = 0.8F;
		source.fill = 0.2F;
		source.castsShadows = false;
		source.flickerKind = FireFlicker;
		source.flickerSeed = (static_cast<uint32_t>(object.position.x) * 73856093U) ^ (static_cast<uint32_t>(object.position.y) * 19349663U);
		PlaceRays(source);
		StaticSources.push_back(source);
	}
}

/**
 * @brief How bright a flickering light is this frame against steady (1).
 * @param flutter How much its flame flutters as someone hurries past (idea 25, 0 to 1 and a little over): its flicker
 * is some three times as strong, and its light dips and shivers.
 */
float FlickerOf(const Source &source, float flicker, float stairs, float flutter, float door)
{
	const float shiver = flutter > 0 ? flutter * (0.12F * std::sin(vfx::Clock() * 0.047F + static_cast<float>(source.flickerSeed % 628) / 100.F) - 0.08F) : 0.F;
	flicker += 2 * flutter;
	switch (source.flickerKind) {
	case TorchFlicker: return 1 + 0.55F * flicker * (vfx::FlameFlicker(source.flickerSeed, false) - 1) + shiver;
	case CandleFlicker: return 1 + 1.F * flicker * (vfx::FlameFlicker(source.flickerSeed, true) - 1) + 1.5F * shiver;
	case FireFlicker: return 1 + 0.45F * flicker * (vfx::FlameFlicker(source.flickerSeed, false) - 1) + shiver;
	case StairsBreath: {
		// A slow swell, about once every four seconds, never in step with other stairs.
		const float phase = static_cast<float>(source.flickerSeed % 628) / 100.F;
		return 1 + 0.18F * std::min(stairs, 1.F) * std::sin(vfx::Clock() * 0.0016F + phase);
	}
	case DoorBreath: {
		// A slow breath, once every five seconds, round a glow that grows with the deepest floor the hero has reached.
		const float deep = MyPlayer != nullptr ? std::clamp(static_cast<float>(DeepestDungeonLevel(*MyPlayer)) / 16.F, 0.F, 1.F) : 0.F;
		const float glow = atmosphere::Value("cathedral_glow", 0.75F) + atmosphere::Value("cathedral_glow_deep", 0.6F) * deep;
		const float breath = 1 + atmosphere::Value("cathedral_breath", 0.15F) * std::sin(vfx::Clock() * 0.00126F);
		return 1 + std::min(door, 1.F) * (glow * breath - 1);
	}
	default: return 1;
	}
}

/**
 * @brief The lava's glow swells and fades slowly (the Atmosphere option's idea 30): its own light in the window, more
 * and less of it in a slow wave that travels across the cavern. Its numbers are the area's in atmosphere.tsv
 * (lava_breath, how much of its light comes and goes, and lava_breath_s, how many seconds a swell takes).
 */
void ApplyLavaBreath()
{
	if (!StaticHasLava)
		return;
	const float depth = std::min(atmosphere::Value("lava_breath", 0) * atmosphere::Strength(atmosphere::Knob::Lava), 0.8F);
	if (depth <= 0)
		return;
	const float time = vfx::Clock() * 0.001F * 6.2831853F / std::max(atmosphere::Value("lava_breath_s", 6), 0.5F);
	const Colour colour = Even(LavaColour);
	for (int j = 0; j < WinH; j++) {
		const size_t from = static_cast<size_t>(WinJ0 + j) * LatticeW + WinI0;
		const size_t out = static_cast<size_t>(j) * WinW;
		for (int i = 0; i < WinW; i++) {
			const float lava = StaticLava[from + static_cast<size_t>(i)];
			if (lava <= 0)
				continue;
			const auto across = static_cast<float>(WinI0 + i);
			const auto down = static_cast<float>(WinJ0 + j);
			const float wave = 0.7F * std::sin(time + (across + down) * 0.09F) + 0.3F * std::sin(time * 1.7F + (across - down) * 0.13F);
			const float add = lava * depth * wave;
			const size_t k = out + static_cast<size_t>(i);
			WinBright[k] = std::max(WinBright[k] + add, 0.F);
			for (int c = 0; c < 3; c++)
				WinHue[k][c] = std::max(WinHue[k][c] + colour[c] * add, 0.F);
			WinHue[k][3] = std::max(WinHue[k][3] + add, 0.F);
		}
	}
}

/**
 * @brief Rain and lightning change the sky's light over Tristram (the Atmosphere option's idea 23): a shower darkens
 * the evening, so the lamps and windows stand out, and lightning lights the town from the sky for a moment (the
 * strike, a dip, then its flicker dying away), with the sky's own shadows. Nothing is drawn over the screen. The
 * numbers are the town's in atmosphere.tsv (rain_sky, the share of the sky's light a downpour takes away, and
 * lightning, how many times the evening's light a strike adds). Each shower multiplies both by its own draw of 1 to 3
 * (weather::State::gloom and charge), so no two storms look alike.
 */
void UpdateSky()
{
	SkyScale = 1;
	if (leveltype != DTYPE_TOWN || !weather::Enabled())
		return;
	const weather::State &sky = weather::Current();
	const float share = sky.kind == weather::Kind::Rain ? 1.F : 0.4F;
	const float dim = std::clamp(atmosphere::Value("rain_sky", 0.4F) * atmosphere::Strength(atmosphere::Knob::RainSky) * sky.gloom * sky.strength * share, 0.F, 0.85F);
	float pulse = 0;
	if (sky.flash > 0) {
		const float t = 1 - sky.flash;
		if (t < 0.12F)
			pulse = 1;
		else if (t < 0.3F)
			pulse = 0.15F;
		else if (t < 0.5F)
			pulse = 0.6F;
		else
			pulse = 0.6F * (1 - t) / 0.5F;
	}
	SkyScale = 1 - dim + atmosphere::Value("lightning", 1.F) * atmosphere::Strength(atmosphere::Knob::Lightning) * sky.charge * pulse;
}

/** @brief The sky's light in the window, changed by UpdateSky's share. */
void ApplySky()
{
	if (SkyScale == 1 || leveltype != DTYPE_TOWN)
		return;
	const float add = ToLinear(TownSkyBrightness) * (SkyScale - 1);
	const Colour sky = Even(TownSkyColour);
	for (size_t k = 0; k < WinBright.size(); k++) {
		WinBright[k] = std::max(WinBright[k] + add, 0.F);
		for (int c = 0; c < 3; c++)
			WinHue[k][c] = std::max(WinHue[k][c] + sky[c] * add, 0.F);
		WinHue[k][3] = std::max(WinHue[k][3] + add, 0.F);
	}
	// The rock tops keep their hue (the sky's share of it was never in it).
	for (float &bright : WinTopBright)
		bright = std::max(bright + add, 0.F);
}

/**
 * @brief A cavern with lava in it is dim orange in the distance, not black (the Atmosphere option's idea 32): the lava's
 * glow fills the air of the whole cavern a little, the more the more lava lies round a spot, walls or no walls. Its
 * numbers are the area's in atmosphere.tsv (lava_far, how bright; lava_far_reach, how many tiles round a spot count;
 * lava_far_full, the share of them that is lava for all of the glow).
 */
void AddFarGlow()
{
	if (!StaticHasLava)
		return;
	const float glow = atmosphere::Value("lava_far", 0) * atmosphere::Strength(atmosphere::Knob::FarGlow);
	if (glow <= 0)
		return;
	const int reach = std::clamp(static_cast<int>(std::lround(atmosphere::Value("lava_far_reach", 10))), 1, 24);
	const float full = std::max(atmosphere::Value("lava_far_full", 0.06F), 0.005F);
	// How many lava tiles lie in the box from the map's corner to each tile.
	constexpr int Wide = MAXDUNY + 1;
	static std::vector<int> sums;
	sums.assign(static_cast<size_t>(MAXDUNX + 1) * Wide, 0);
	for (int x = 0; x < MAXDUNX; x++) {
		for (int y = 0; y < MAXDUNY; y++) {
			sums[static_cast<size_t>(x + 1) * Wide + y + 1] = (LavaRadius(dPiece[x][y]) != 0 ? 1 : 0)
			    + sums[static_cast<size_t>(x) * Wide + y + 1] + sums[static_cast<size_t>(x + 1) * Wide + y] - sums[static_cast<size_t>(x) * Wide + y];
		}
	}
	const Colour colour = Even(LavaColour);
	for (int j = 0; j < LatticeH; j++) {
		const int y = std::clamp(static_cast<int>(std::floor(SampleAt(j) + 0.5F)), 0, MAXDUNY - 1);
		const int y0 = std::max(y - reach, 0);
		const int y1 = std::min(y + reach, MAXDUNY - 1) + 1;
		for (int i = 0; i < LatticeW; i++) {
			const int x = std::clamp(static_cast<int>(std::floor(SampleAt(i) + 0.5F)), 0, MAXDUNX - 1);
			const int x0 = std::max(x - reach, 0);
			const int x1 = std::min(x + reach, MAXDUNX - 1) + 1;
			const int lava = sums[static_cast<size_t>(x1) * Wide + y1] - sums[static_cast<size_t>(x0) * Wide + y1] - sums[static_cast<size_t>(x1) * Wide + y0] + sums[static_cast<size_t>(x0) * Wide + y0];
			if (lava == 0)
				continue;
			const float share = static_cast<float>(lava) / static_cast<float>((2 * reach + 1) * (2 * reach + 1));
			const float add = glow * std::min(share / full, 1.F);
			const size_t k = static_cast<size_t>(j) * LatticeW + i;
			StaticBright[k] += add;
			for (int c = 0; c < 3; c++)
				StaticHue[k][c] += colour[c] * add;
			StaticHue[k][3] += add;
		}
	}
}

/**
 * @brief Moves each flickering static light's light with its flame this frame (the Atmosphere option's flicker, idea 1):
 * what it adds to the window's light, scaled by how bright its flame is now against steady. Its shadows read the same
 * brightness (Source::flicker), so they sway with it.
 */
void ApplyFlicker()
{
	// Idea 50: the flames steady in the quiet after a fight.
	const float flicker = atmosphere::Strength(atmosphere::Knob::Flicker) * atmosphere::Value("flicker", 1) * moments::FlickerKept();
	const float stairs = atmosphere::Strength(atmosphere::Knob::Stairs);
	const bool flutters = atmosphere::Strength(atmosphere::Knob::Flutter) > 0;
	const float door = atmosphere::Strength(atmosphere::Knob::Cathedral);
	// Idea 53: the flames near a dying hero die down.
	const bool dimmed = moments::FlamesDimmed();
	if (flicker <= 0 && stairs <= 0 && !flutters && door <= 0 && !dimmed) {
		for (Source &source : StaticSources)
			source.flicker = 1;
		return;
	}
	for (size_t s = 0; s < StaticSources.size() && s < StaticPatches.size(); s++) {
		Source &source = StaticSources[s];
		const bool flame = IsAnyOf(source.flickerKind, TorchFlicker, CandleFlicker, FireFlicker);
		const float flutter = flutters && flame ? vfx::FlameFlutter(Point { static_cast<int>(std::lround(source.x)), static_cast<int>(std::lround(source.y)) }) : 0.F;
		float factor = FlickerOf(source, flicker, stairs, flutter, door);
		if (dimmed && flame)
			factor *= moments::FlameKept(source.x, source.y);
		source.flicker = factor;
		const Patch &patch = StaticPatches[s];
		if (std::abs(factor - 1) < 1e-3F || patch.linear.empty())
			continue;
		const float change = factor - 1;
		const int a0 = std::max(0, WinI0 - patch.i0);
		const int a1 = std::min(patch.w, WinI0 + WinW - patch.i0);
		const int b0 = std::max(0, WinJ0 - patch.j0);
		const int b1 = std::min(patch.h, WinJ0 + WinH - patch.j0);
		for (int b = b0; b < b1; b++) {
			const size_t row = static_cast<size_t>(b) * patch.w;
			const size_t out = static_cast<size_t>(patch.j0 + b - WinJ0) * WinW;
			for (int a = a0; a < a1; a++) {
				const float linear = patch.linear[row + a];
				if (linear <= 0)
					continue;
				const float add = change * linear;
				const size_t k = out + static_cast<size_t>(patch.i0 + a - WinI0);
				WinBright[k] = std::max(WinBright[k] + add, 0.F);
				for (int c = 0; c < 3; c++)
					WinHue[k][c] = std::max(WinHue[k][c] + source.colour[c] * add, 0.F);
				WinHue[k][3] = std::max(WinHue[k][3] + add, 0.F);
			}
		}
	}
}

void UpdateStatics()
{
	const StaticKey key { static_cast<int>(leveltype), static_cast<int>(currlevel), setlevel, static_cast<int>(setlvlnum), BlockVersion, ObjectsSignature(), atmosphere::Version() };
	if (key == BuiltStatics && std::memcmp(BuiltPreLight.data(), dPreLight, sizeof(dPreLight)) == 0)
		return;
	BuiltStatics = key;
	std::memcpy(BuiltPreLight.data(), dPreLight, sizeof(dPreLight));

	StaticSources.clear();
	const bool town = leveltype == DTYPE_TOWN;
	if (town) {
		for (const TownFire &fire : TownFires) {
			Source source;
			source.x = fire.x;
			source.y = fire.y;
			source.radius = fire.radius;
			source.colour = Even(fire.colour);
			source.height = fire.height;
			source.size = fire.size;
			source.fill = fire.fill;
			source.power = fire.power;
			source.coneX = fire.coneX;
			source.coneY = fire.coneY;
			// The forge's coals glow like a fire, the lamps like torches, a lit window like a candle behind glass.
			if (fire.colour == FireColour)
				source.flickerKind = FireFlicker;
			else if (fire.colour == TorchColour)
				source.flickerKind = TorchFlicker;
			else if (fire.colour == CandleColour)
				source.flickerKind = CandleFlicker;
			else if (fire.colour == CathedralDoorColour)
				source.flickerKind = DoorBreath;
			source.flickerSeed = (static_cast<uint32_t>(fire.x * 8) * 73856093U) ^ (static_cast<uint32_t>(fire.y * 8) * 19349663U);
			PlaceRays(source);
			StaticSources.push_back(source);
		}
	}
	for (int i = 0; i < ActiveObjectCount; i++) {
		const Object &object = Objects[ActiveObjects[i]];
		Source source;
		if (!BakedObjectLight(object, source))
			continue;
		source.x = static_cast<float>(object.position.x);
		source.y = static_cast<float>(object.position.y);
		source.colour = Even(source.colour);
		source.flickerSeed = vfx::FlameSeed(object.position);
		PlaceRays(source);
		StaticSources.push_back(source);
	}
	AddStairsLights();
	AddRoomLights();
	for (int x = 0; x < MAXDUNX; x++) {
		for (int y = 0; y < MAXDUNY; y++) {
			const int radius = LavaRadius(dPiece[x][y]);
			if (radius == 0)
				continue;
			Source source;
			source.x = static_cast<float>(x);
			source.y = static_cast<float>(y);
			source.radius = radius;
			source.colour = Even(LavaColour);
			source.height = LavaHeight;
			source.castsShadows = false;
			source.lava = true;
			PlaceRays(source);
			StaticSources.push_back(source);
		}
	}

	// The dungeon's dark, or the sky's light in town (in its own colour).
	const float ambient = town ? ToLinear(TownSkyBrightness) : LevelLinear(15.F);
	StaticBright.assign(static_cast<size_t>(LatticeW) * LatticeH, ambient);
	const Colour sky = Even(TownSkyColour);
	StaticHue.assign(static_cast<size_t>(LatticeW) * LatticeH, town ? std::array<float, 4> { sky[0] * ambient, sky[1] * ambient, sky[2] * ambient, ambient } : std::array<float, 4> { 0.F, 0.F, 0.F, 0.F });
	// Lava shines as one light: its brightest tile at each sample, added to the other lights.
	std::vector<float> &lava = StaticLava;
	lava.assign(StaticBright.size(), 0.F);
	StaticHasLava = false;
	StaticPatches.assign(StaticSources.size(), {});
	for (size_t s = 0; s < StaticSources.size(); s++) {
		const Source &source = StaticSources[s];
		ShineStatic(source, lava, source.flickerKind != SteadyLight && !source.lava ? &StaticPatches[s] : nullptr);
	}
	const Colour lavaColour = Even(LavaColour);
	for (size_t k = 0; k < lava.size(); k++) {
		if (lava[k] <= 0)
			continue;
		StaticHasLava = true;
		StaticBright[k] += lava[k];
		for (int c = 0; c < 3; c++)
			StaticHue[k][c] += lavaColour[c] * lava[k];
		StaticHue[k][3] += lava[k];
	}
	AddFarGlow();
	// The tops of the rock round the rooms, in the dungeon's dark and lit by the lights round them. The lava lies on the
	// floor, below them.
	StaticTopBright.clear();
	StaticTopHue.clear();
	if (RockTopsLit()) {
		StaticTopBright.assign(StaticBright.size(), ambient);
		StaticTopHue.assign(StaticBright.size(), { 0.F, 0.F, 0.F, 0.F });
		for (const Source &source : StaticSources) {
			if (!source.lava)
				ShineTop(source);
		}
	}

	// The game lights all of the town fully: its evening light is the sky's and the fires'.
	if (town)
		return;
	// Anything the game baked into the level that the lights above don't explain still shines, through walls as before.
	std::array<uint8_t, MAXDUNX * MAXDUNY> explained;
	explained.fill(15);
	for (const Source &source : StaticSources) {
		const int sx = static_cast<int>(source.x);
		const int sy = static_cast<int>(source.y);
		for (int dx = -15; dx <= 15; dx++) {
			for (int dy = -15; dy <= 15; dy++) {
				const int x = sx + dx;
				const int y = sy + dy;
				if (x < 0 || y < 0 || x >= MAXDUNX || y >= MAXDUNY)
					continue;
				const int distance = LightConeInterpolations[0][0][std::abs(dx)][std::abs(dy)];
				if (distance >= 128)
					continue;
				uint8_t &level = explained[static_cast<size_t>(x) * MAXDUNY + y];
				level = std::min(level, LightFalloffs[source.radius][distance]);
			}
		}
	}
	const Colour residualColour = Even(CurrentLook.warm);
	for (int x = 0; x < MAXDUNX; x++) {
		for (int y = 0; y < MAXDUNY; y++) {
			const uint8_t baked = dPreLight[x][y];
			if (baked >= explained[static_cast<size_t>(x) * MAXDUNY + y])
				continue;
			// Brought up to the baked light, not added on top of what the lights already give there.
			const float linear = LevelLinear(baked);
			for (int i = x * Per; i < x * Per + Per; i++) {
				for (int j = y * Per; j < y * Per + Per; j++) {
					const size_t k = static_cast<size_t>(j) * LatticeW + i;
					const float missing = linear - StaticBright[k];
					if (missing <= 0)
						continue;
					StaticBright[k] = linear;
					for (int c = 0; c < 3; c++)
						StaticHue[k][c] += residualColour[c] * missing;
					StaticHue[k][3] += missing;
				}
			}
		}
	}
}

/** @brief Adds a moving light's light to this frame's window, from what it sees (its Reach). */
void ShineMoving(const Source &source, const Reach &reach, bool tops)
{
	const float range = static_cast<float>(source.radius + 1);
	const int a0 = std::max(0, WinI0 - reach.i0);
	const int a1 = std::min(reach.w, WinI0 + WinW - reach.i0);
	const int b0 = std::max(0, WinJ0 - reach.j0);
	const int b1 = std::min(reach.h, WinJ0 + WinH - reach.j0);
	for (int b = b0; b < b1; b++) {
		const int j = reach.j0 + b;
		const float sy = SampleAt(j);
		for (int a = a0; a < a1; a++) {
			const size_t r = static_cast<size_t>(b) * reach.w + a;
			const float seen = static_cast<float>(reach.seen[r]) / 255.F;
			const float path = reach.paths[r];
			const int i = reach.i0 + a;
			const float distance = Length(SampleAt(i) - source.x, sy - source.y);
			if (tops && distance < range && !SampleOpen(i, j)) {
				const float top = TopLinear(source, distance);
				if (top > 1e-5F) {
					const size_t k = static_cast<size_t>(j - WinJ0) * WinW + (i - WinI0);
					WinTopBright[k] += top;
					for (int c = 0; c < 3; c++)
						WinTopHue[k][c] += source.colour[c] * top;
					WinTopHue[k][3] += top;
				}
			}
			float linear = 0;
			if (distance < range || path <= range)
				linear = SampleLinear(source, distance, seen, path);
			if (seen < 1 && path > range) {
				// Rock in front of the floors' edges sees what they see (UpdateSpill): as much light as the edge that
				// lets the most through.
				std::array<int, SpillWays> edges;
				std::array<float, SpillWays> extras;
				const int count = EdgesInBox(i, j, reach.i0, reach.j0, reach.w, reach.h, edges, extras);
				for (int e = 0; e < count; e++) {
					const auto edge = static_cast<size_t>(edges[static_cast<size_t>(e)]);
					const float edgeSeen = static_cast<float>(reach.seen[edge]) / 255.F;
					const float edgePath = reach.paths[edge] + extras[static_cast<size_t>(e)];
					if ((edgeSeen <= 0 || distance >= range) && edgePath > range)
						continue;
					linear = std::max(linear, SampleLinear(source, distance, edgeSeen, edgePath));
				}
			}
			if (linear <= 1e-5F)
				continue;
			const size_t k = static_cast<size_t>(j - WinJ0) * WinW + (i - WinI0);
			WinBright[k] += linear;
			for (int c = 0; c < 3; c++)
				WinHue[k][c] += source.colour[c] * linear;
			WinHue[k][3] += linear;
		}
	}
}

/**
 * @brief A spell's light kept on after the spell is gone, fading out (the Atmosphere option's linger, idea 10, a maybe):
 * a firebolt's glow on the walls fades over a third of a second instead of going at once.
 */
struct Lingering {
	Source source;
	Reach reach;
	float born = 0;
	float power = 0;
};
std::vector<Lingering> Lingerings;
/** @brief Each light id's spell light last frame, to see which went out this frame. */
std::array<bool, MAXLIGHTS> WasSpell {};
std::array<Source, MAXLIGHTS> LastSpell {};
int LingerLevel = -1;

/** @brief Starts the lingering of the spell lights that went out this frame, and adds those still fading to the window. */
void Linger(const std::array<bool, MAXLIGHTS> &isSpell, bool tops)
{
	const float strength = atmosphere::Strength(atmosphere::Knob::Linger);
	const int level = static_cast<int>(currlevel) * 2 + (setlevel ? 1 : 0);
	if (strength <= 0 || level != LingerLevel) {
		Lingerings.clear();
		LingerLevel = level;
		WasSpell = isSpell;
		if (strength <= 0)
			return;
	}
	const FramePartScope part(FramePart::Atmosphere);
	const float now = vfx::Clock();
	const float fadeMs = std::max(atmosphere::Value("linger_ms", 330) * strength, 1.F);
	for (int id = 0; id < MAXLIGHTS; id++) {
		if (!WasSpell[static_cast<size_t>(id)] || isSpell[static_cast<size_t>(id)] || Lingerings.size() >= 24)
			continue;
		const Reach &reach = Reaches[static_cast<size_t>(id)];
		if (reach.radius < 0 || reach.seen.empty())
			continue;
		Lingering lingering;
		lingering.source = LastSpell[static_cast<size_t>(id)];
		lingering.reach = reach;
		lingering.born = now;
		lingering.power = lingering.source.power;
		Lingerings.push_back(std::move(lingering));
	}
	WasSpell = isSpell;
	std::erase_if(Lingerings, [&](const Lingering &lingering) { return now - lingering.born >= fadeMs || now < lingering.born; });
	for (Lingering &lingering : Lingerings) {
		const float left = 1 - (now - lingering.born) / fadeMs;
		// Quick at first, then trailing off, as a glow on stone cools.
		lingering.source.power = lingering.power * left * left;
		DynamicSources.push_back(lingering.source);
		ShineMoving(lingering.source, lingering.reach, tops);
	}
}

void GatherDynamic(float heroX, float heroY)
{
	// What each moving light is: its colour, how high it hangs, how wide it glows, how much of it bounces and how bright it is.
	struct Kind {
		Colour colour;
		float height;
		float size;
		float fill;
		float power;
		bool glow;
		bool carried;
		/** @brief A spell's light (a town portal's): the only moving light Tristram's evening shows. */
		bool spell;
	};
	std::array<Kind, MAXLIGHTS> kinds;
	kinds.fill({ CurrentLook.warm, 1.4F, 0.3F, 0.15F, 1.F, false, false, false });
	const auto owns = [](int id) { return id >= 0 && id < MAXLIGHTS; };
	// Owners from the least to the most sure, so a stale id held by a dying missile can't recolour a hero's light.
	for (const Missile &missile : Missiles) {
		if (!owns(missile._mlid))
			continue;
		// A spell flies at about waist height (a firebolt's ball of fire is about a third of a tile across).
		kinds[missile._mlid] = { FromBytes(vfx::LightColourOf(missile)), 0.85F, 0.35F, 0.15F, SpellPower, false, false, true };
	}
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &monster = Monsters[ActiveMonsters[i]];
		if (!owns(monster.lightId))
			continue;
		// Uniques glow; other monsters only carry a light while berserk (POE2's Confuse sets the flag without one).
		kinds[monster.lightId] = { monster.isUnique() ? UniqueGlowColour : BerserkColour, 0.7F, 0.8F, 0.3F, 1.F, false, true, false };
	}
	int myLight = NO_LIGHT;
	for (const Player &player : Players) {
		if (!player.plractive || !player.isOnActiveLevel() || !owns(player.lightId))
			continue;
		// A hero's own light is a wide, soft glow hanging over them: it lights the way without hard shadows, so the
		// shadows it gives are short, soft and faint, and a torch or a spell nearby casts the clear ones.
		kinds[player.lightId] = { CurrentLook.warm, 2.6F, 1.6F, 0.45F, 1.F, true, true, false };
		if (&player == MyPlayer)
			myLight = player.lightId;
	}

	const bool tops = !WinTopBright.empty();
	DynamicSources.clear();
	std::array<bool, MAXLIGHTS> isSpell {};
	for (int i = 0; i < ActiveLightCount; i++) {
		const int id = ActiveLights[i];
		const Light &light = Lights[id];
		if (light.isInvalid)
			continue;
		// Tristram's evening is lit by its sky and its fires: no glow round the heroes (the game keeps a light on each).
		if (leveltype == DTYPE_TOWN && !kinds[id].spell)
			continue;
		// The hero being played carries their light where they are drawn. (The game's own record of where that light is
		// can be left behind, in the rock, after a level change or a load until the hero moves; the light was lost then.)
		const bool mine = id == myLight && !std::isnan(heroX);
		// As the game does: a monster hidden in a wall doesn't give itself away.
		if (!mine && TileHasAny(light.position.tile, TileProperties::Solid))
			continue;
		Source source;
		source.x = static_cast<float>(light.position.tile.x) + static_cast<float>(light.position.offset.deltaX) / 8.F;
		source.y = static_cast<float>(light.position.tile.y) + static_cast<float>(light.position.offset.deltaY) / 8.F;
		if (mine) {
			source.x = heroX;
			source.y = heroY;
		}
		source.radius = std::min<int>(light.radius, NumLightRadiuses - 1);
		source.colour = Even(kinds[id].colour);
		source.height = kinds[id].height;
		source.size = kinds[id].size;
		source.fill = kinds[id].fill;
		// Idea 53: a dying hero's light shrinks away.
		source.power = kinds[id].power * (id == myLight ? moments::HeroLightKept() : 1.F);
		source.glow = kinds[id].glow;
		source.carried = kinds[id].carried;
		// Moving lights shine from where they are: a tile can stop light and still be walked on (cathedral piece 450).
		source.rayX = source.x;
		source.rayY = source.y;

		Reach &reach = Reaches[id];
		// What a light can see is worked out again only when it moves an eighth of a tile (the hero's light moves every
		// frame), or turns into a different kind of light.
		const int keyX = RoundToInt(source.rayX * 8.F);
		const int keyY = RoundToInt(source.rayY * 8.F);
		if (reach.keyX != keyX || reach.keyY != keyY || reach.radius != source.radius || reach.blockVersion != BlockVersion || reach.size != source.size) {
			reach.keyX = keyX;
			reach.keyY = keyY;
			reach.radius = source.radius;
			reach.blockVersion = BlockVersion;
			reach.size = source.size;
			const float range = static_cast<float>(source.radius + 1);
			reach.i0 = std::max(0, SampleBefore(source.x - range));
			reach.j0 = std::max(0, SampleBefore(source.y - range));
			reach.w = std::min(LatticeW - 1, SampleBefore(source.x + range) + 1) - reach.i0 + 1;
			reach.h = std::min(LatticeH - 1, SampleBefore(source.y + range) + 1) - reach.j0 + 1;
			reach.seen.assign(static_cast<size_t>(std::max(reach.w, 0)) * std::max(reach.h, 0), 0);
			for (int b = 0; b < reach.h; b++) {
				const int j = reach.j0 + b;
				for (int a = 0; a < reach.w; a++) {
					const int i = reach.i0 + a;
					if (SampleOpen(i, j) || GroundSample(i, j))
						reach.seen[static_cast<size_t>(b) * reach.w + a] = static_cast<uint8_t>(SeenFraction(source, SampleAt(i), SampleAt(j)) * 255.F + 0.5F);
				}
			}
			PathsFrom(source, reach.i0, reach.j0, reach.w, reach.h, reach.paths);
		}
		DynamicSources.push_back(source);
		// Light it into this frame's window.
		ShineMoving(source, reach, tops);
		if (kinds[id].spell) {
			isSpell[static_cast<size_t>(id)] = true;
			LastSpell[static_cast<size_t>(id)] = source;
		}
	}
	Linger(isSpell, tops);
}

/**
 * @brief Samples inside wall tiles take the light of the floor in front of them (the side the camera sees), so the
 * floor before a wall and the wall's face are lit by the room they face, never by the room behind the wall.
 */
void FillWalls()
{
	for (int j = 0; j < WinH; j++) {
		for (int i = 0; i < WinW; i++)
			WinSet[static_cast<size_t>(j) * WinW + i] = SampleOpen(WinI0 + i, WinJ0 + j) ? 1 : 0;
	}
	for (uint8_t pass = 1; pass <= 3; pass++) {
		for (int j = 0; j < WinH; j++) {
			for (int i = 0; i < WinW; i++) {
				const size_t k = static_cast<size_t>(j) * WinW + i;
				if (WinSet[k] != 0)
					continue;
				int best = -1;
				for (const auto &step : { std::array<int, 2> { 1, 0 }, std::array<int, 2> { 0, 1 }, std::array<int, 2> { 1, 1 } }) {
					const int ni = i + step[0];
					const int nj = j + step[1];
					if (ni < 0 || nj < 0 || ni >= WinW || nj >= WinH)
						continue;
					const size_t n = static_cast<size_t>(nj) * WinW + ni;
					if (WinSet[n] == 0 || WinSet[n] > pass)
						continue;
					if (best < 0 || WinBright[n] > WinBright[static_cast<size_t>(best)])
						best = static_cast<int>(n);
				}
				if (best < 0)
					continue;
				WinBright[k] = WinBright[static_cast<size_t>(best)];
				WinHue[k] = WinHue[static_cast<size_t>(best)];
				WinSet[k] = pass + 1;
			}
		}
	}
}

/** @brief Linear light with its lights' colours (weighted by their light) as drawn: their colour where it is bright, cool in the dark. */
std::array<float, 3> Graded(float linear, const std::array<float, 4> &hue)
{
	const Look &look = CurrentLook;
	const float brightness = DrawnBrightness(linear);
	const float warmth = WarmthOfBrightness[static_cast<size_t>(std::clamp(brightness, 0.F, 1.F) * 255.F + 0.5F)];
	std::array<float, 3> colour;
	for (int c = 0; c < 3; c++) {
		const float lightColour = hue[3] > 1e-6F ? hue[c] / hue[3] : look.warm[c];
		const float grade = look.cool[c] + (lightColour - look.cool[c]) * warmth;
		colour[c] = std::clamp(brightness * grade * look.tint[c], 0.F, MaxLight);
	}
	return colour;
}

void Finish()
{
	const Look &look = CurrentLook;
	const auto byte = [](float value) { return static_cast<uint32_t>(std::clamp(value, 0.F, 1.F) * 255.F + 0.5F); };
	for (size_t k = 0; k < WinBright.size(); k++) {
		const std::array<float, 3> colour = Graded(WinBright[k], WinHue[k]);
		Rgb &out = WinColour[k];
		out = { colour[0], colour[1], colour[2] };
		WinPixels[k] = 0xFF000000U | (byte(colour[0]) << 16) | (byte(colour[1]) << 8) | byte(colour[2]);
		// Each channel's light is its colour plus one less the alpha (see FieldMulPixels). Up to full that is the colour
		// with full alpha; above it, an alpha halfway between what the brightest and the darkest channel allow.
		const float high = std::max({ colour[0], colour[1], colour[2] });
		const float low = std::min({ colour[0], colour[1], colour[2] });
		const float alpha = high > 1 ? std::clamp(1.5F - (high + low) / 2, 0.F, 1.F) : 1.F;
		WinMulPixels[k] = (byte(alpha) << 24) | (byte(colour[0] - 1 + alpha) << 16) | (byte(colour[1] - 1 + alpha) << 8) | byte(colour[2] - 1 + alpha);
	}
	// The rock tops' light, at the rock's samples (the open ones keep the floor's).
	for (size_t k = 0; k < WinTopBright.size(); k++) {
		if (WinSet[k] == 1) {
			WinTopColour[k] = WinColour[k];
			continue;
		}
		const std::array<float, 3> colour = Graded(WinTopBright[k], WinTopHue[k]);
		WinTopColour[k] = { colour[0], colour[1], colour[2] };
	}
	// The sky's light alone, drawn as a point lit by nothing else is above.
	SkyRgb = Full();
	if (leveltype == DTYPE_TOWN) {
		const float brightness = DrawnBrightness(ToLinear(TownSkyBrightness) * SkyScale);
		const float warmth = WarmthOfBrightness[static_cast<size_t>(std::clamp(brightness, 0.F, 1.F) * 255.F + 0.5F)];
		const Colour sky = Even(TownSkyColour);
		std::array<float, 3> colour;
		for (int c = 0; c < 3; c++) {
			const float grade = look.cool[c] + (sky[c] - look.cool[c]) * warmth;
			colour[c] = std::clamp(brightness * grade * look.tint[c], 0.F, MaxLight);
		}
		SkyRgb = { colour[0], colour[1], colour[2] };
	}
}

} // namespace

bool Update(const Look &look, float x0, float y0, float x1, float y1, float heroX, float heroY)
{
	if (leveltype == DTYPE_NONE)
		return false;
	UpdateLook(look);
	UpdateBlocks();
	UpdateSpill();
	UpdateStatics();

	WinI0 = std::clamp(SampleBefore(std::min(x0, x1)) - 1, 0, LatticeW - 1);
	WinJ0 = std::clamp(SampleBefore(std::min(y0, y1)) - 1, 0, LatticeH - 1);
	const int i1 = std::clamp(SampleBefore(std::max(x0, x1)) + 2, WinI0, LatticeW - 1);
	const int j1 = std::clamp(SampleBefore(std::max(y0, y1)) + 2, WinJ0, LatticeH - 1);
	WinW = i1 - WinI0 + 1;
	WinH = j1 - WinJ0 + 1;
	const size_t count = static_cast<size_t>(WinW) * WinH;
	WinBright.resize(count);
	WinHue.resize(count);
	WinSet.resize(count);
	WinColour.resize(count);
	WinPixels.resize(count);
	WinMulPixels.resize(count);
	const bool tops = !StaticTopBright.empty();
	WinTopBright.resize(tops ? count : 0);
	WinTopHue.resize(tops ? count : 0);
	WinTopColour.resize(tops ? count : 0);
	for (int j = 0; j < WinH; j++) {
		const size_t from = static_cast<size_t>(WinJ0 + j) * LatticeW + WinI0;
		std::copy_n(StaticBright.begin() + static_cast<ptrdiff_t>(from), WinW, WinBright.begin() + static_cast<ptrdiff_t>(j) * WinW);
		std::copy_n(StaticHue.begin() + static_cast<ptrdiff_t>(from), WinW, WinHue.begin() + static_cast<ptrdiff_t>(j) * WinW);
		if (tops) {
			std::copy_n(StaticTopBright.begin() + static_cast<ptrdiff_t>(from), WinW, WinTopBright.begin() + static_cast<ptrdiff_t>(j) * WinW);
			std::copy_n(StaticTopHue.begin() + static_cast<ptrdiff_t>(from), WinW, WinTopHue.begin() + static_cast<ptrdiff_t>(j) * WinW);
		}
	}
	{
		const FramePartScope part(FramePart::Atmosphere);
		ApplyFlicker();
		ApplyLavaBreath();
		UpdateSky();
		ApplySky();
	}
	GatherDynamic(heroX, heroY);
	ApplyReveals();
	FillWalls();
	Finish();
	Updates++;
	return true;
}

uint32_t UpdateCount()
{
	return Updates;
}

int FieldWidth()
{
	return WinW;
}

int FieldHeight()
{
	return WinH;
}

const uint32_t *FieldPixels()
{
	return WinPixels.data();
}

const uint32_t *FieldMulPixels()
{
	return WinMulPixels.data();
}

void FieldRect(float &x0, float &y0, float &x1, float &y1)
{
	x0 = SampleAt(WinI0) - 0.5F / Per;
	y0 = SampleAt(WinJ0) - 0.5F / Per;
	x1 = SampleAt(WinI0 + WinW - 1) + 0.5F / Per;
	y1 = SampleAt(WinJ0 + WinH - 1) + 0.5F / Per;
}

namespace {

/** @brief The light at a world point (in tiles), blended from the four samples around it, each sample's light as `of` gives it (from its place in the window). */
template <typename SampleLight>
Rgb Blend(float x, float y, SampleLight of)
{
	if (WinW <= 0 || WinH <= 0)
		return Full();
	const float fi = std::clamp((x + 0.25F) * Per - static_cast<float>(WinI0), 0.F, static_cast<float>(WinW - 1));
	const float fj = std::clamp((y + 0.25F) * Per - static_cast<float>(WinJ0), 0.F, static_cast<float>(WinH - 1));
	const int i = std::min(static_cast<int>(fi), WinW - 2 >= 0 ? WinW - 2 : 0);
	const int j = std::min(static_cast<int>(fj), WinH - 2 >= 0 ? WinH - 2 : 0);
	const float u = std::clamp(fi - static_cast<float>(i), 0.F, 1.F);
	const float v = std::clamp(fj - static_cast<float>(j), 0.F, 1.F);
	const int i2 = std::min(i + 1, WinW - 1);
	const int j2 = std::min(j + 1, WinH - 1);
	const Rgb a = of(i, j);
	const Rgb b = of(i2, j);
	const Rgb c = of(i, j2);
	const Rgb d = of(i2, j2);
	const auto mix = [&](float pa, float pb, float pc, float pd) {
		return (pa * (1 - u) + pb * u) * (1 - v) + (pc * (1 - u) + pd * u) * v;
	};
	return { mix(a.r, b.r, c.r, d.r), mix(a.g, b.g, c.g, d.g), mix(a.b, b.b, c.b, d.b) };
}

/** @brief FillWalls gave this sample the light of the room in front of it (it is rock inside or behind a wall). */
bool LitForWalls(size_t k)
{
	return WinSet[k] >= 2;
}

} // namespace

Rgb At(float x, float y)
{
	return Blend(x, y, [](int i, int j) { return WinColour[static_cast<size_t>(j) * WinW + i]; });
}

Rgb GroundAt(float x, float y)
{
	return Blend(x, y, [](int i, int j) {
		const size_t k = static_cast<size_t>(j) * WinW + i;
		return LitForWalls(k) ? Rgb {} : WinColour[k];
	});
}

Rgb TileGroundAt(float x, float y, int tileX, int tileY)
{
	if (WinW <= 0 || WinH <= 0)
		return Full();
	const auto add = [](Rgb &sum, const Rgb &colour, float weight) {
		sum.r += colour.r * weight;
		sum.g += colour.g * weight;
		sum.b += colour.b * weight;
	};
	// Blended from the samples round the point, leaving out the rock lit for the walls' faces.
	const float fi = std::clamp((x + 0.25F) * Per - static_cast<float>(WinI0), 0.F, static_cast<float>(WinW - 1));
	const float fj = std::clamp((y + 0.25F) * Per - static_cast<float>(WinJ0), 0.F, static_cast<float>(WinH - 1));
	const int i = std::min(static_cast<int>(fi), std::max(WinW - 2, 0));
	const int j = std::min(static_cast<int>(fj), std::max(WinH - 2, 0));
	const float u = std::clamp(fi - static_cast<float>(i), 0.F, 1.F);
	const float v = std::clamp(fj - static_cast<float>(j), 0.F, 1.F);
	Rgb sum;
	float weights = 0;
	for (int dj = 0; dj <= 1; dj++) {
		for (int di = 0; di <= 1; di++) {
			const size_t k = static_cast<size_t>(std::min(j + dj, WinH - 1)) * WinW + std::min(i + di, WinW - 1);
			const float weight = (di == 0 ? 1 - u : u) * (dj == 0 ? 1 - v : v);
			if (LitForWalls(k) || weight <= 0)
				continue;
			add(sum, WinColour[k], weight);
			weights += weight;
		}
	}
	if (weights > 0.05F)
		return { sum.r / weights, sum.g / weights, sum.b / weights };
	// All rock lit for the walls (the point is on the wall beside it): the tile's own samples.
	sum = {};
	int count = 0;
	for (int dj = 0; dj < Per; dj++) {
		for (int di = 0; di < Per; di++) {
			const int si = tileX * Per + di - WinI0;
			const int sj = tileY * Per + dj - WinJ0;
			if (si < 0 || sj < 0 || si >= WinW || sj >= WinH)
				continue;
			const size_t k = static_cast<size_t>(sj) * WinW + si;
			if (LitForWalls(k))
				continue;
			add(sum, WinColour[k], 1);
			count++;
		}
	}
	if (count == 0)
		return {};
	return { sum.r / static_cast<float>(count), sum.g / static_cast<float>(count), sum.b / static_cast<float>(count) };
}

Rgb FaceAt(float x, float y)
{
	if (WinW <= 0 || WinH <= 0)
		return Full();
	const float fi = std::clamp((x + 0.25F) * Per - static_cast<float>(WinI0), 0.F, static_cast<float>(WinW - 1));
	const float fj = std::clamp((y + 0.25F) * Per - static_cast<float>(WinJ0), 0.F, static_cast<float>(WinH - 1));
	const int i = std::min(static_cast<int>(fi), std::max(WinW - 2, 0));
	const int j = std::min(static_cast<int>(fj), std::max(WinH - 2, 0));
	const float u = std::clamp(fi - static_cast<float>(i), 0.F, 1.F);
	const float v = std::clamp(fj - static_cast<float>(j), 0.F, 1.F);
	std::array<size_t, 4> samples;
	std::array<float, 4> weights;
	bool wallLit = false;
	for (int n = 0; n < 4; n++) {
		const int di = n & 1;
		const int dj = n >> 1;
		samples[n] = static_cast<size_t>(std::min(j + dj, WinH - 1)) * WinW + std::min(i + di, WinW - 1);
		weights[n] = (di == 0 ? 1 - u : u) * (dj == 0 ? 1 - v : v);
		wallLit = wallLit || (weights[n] > 0 && LitForWalls(samples[n]));
	}
	Rgb sum;
	float total = 0;
	for (int n = 0; n < 4; n++) {
		if (weights[n] <= 0 || (wallLit && WinSet[samples[n]] == 1))
			continue;
		const Rgb &colour = WinColour[samples[n]];
		sum.r += colour.r * weights[n];
		sum.g += colour.g * weights[n];
		sum.b += colour.b * weights[n];
		total += weights[n];
	}
	if (total <= 0)
		return At(x, y);
	return { sum.r / total, sum.g / total, sum.b / total };
}

Rgb BehindAt(float x, float y, bool alongY)
{
	return Blend(x, y, [alongY](int i, int j) {
		// Back a sample at a time past the rock lit for the walls, to the first floor or ground outside the rooms (lit
		// past a floor's open edge, see UpdateSpill). Where it is rock lit for the walls all the way back, the walls
		// behind are lit by the room beside them, as the face's own room sees them.
		for (int step = 0; step <= 2; step++) {
			const int bi = alongY ? i : i - step;
			const int bj = alongY ? j - step : j;
			if (bi < 0 || bj < 0)
				break;
			const size_t k = static_cast<size_t>(bj) * WinW + bi;
			if (!LitForWalls(k))
				return WinColour[k];
		}
		return WinColour[static_cast<size_t>(j) * WinW + i];
	});
}

bool RockTopsLit()
{
	return IsAnyOf(leveltype, DTYPE_CATACOMBS, DTYPE_CAVES, DTYPE_NEST);
}

Rgb RockTopAt(float x, float y)
{
	if (WinTopColour.empty())
		return At(x, y);
	return Blend(x, y, [](int i, int j) { return WinTopColour[static_cast<size_t>(j) * WinW + i]; });
}

Rgb Boost(const Rgb &light, float levels)
{
	const float full = BrightnessOfLevel[0];
	const float brightness = std::max({ light.r, light.g, light.b });
	if (brightness <= 1e-4F || full <= 1e-4F)
		return light;
	// The light level this brightness has, and the brightness a few levels brighter.
	size_t step = 0;
	const float relative = brightness / full;
	while (step + 1 < BrightnessOfLevel.size() && BrightnessOfLevel[step] / full > relative)
		step++;
	const float target = BrightnessOfLevel[static_cast<size_t>(std::max(0.F, static_cast<float>(step) - levels * 255.F / 15.F))] / full;
	const float gain = std::clamp(target / std::max(relative, 1e-4F), 1.F, 8.F);
	const Rgb top = Full();
	return { std::min(light.r * gain, std::max(top.r, light.r)), std::min(light.g * gain, std::max(top.g, light.g)), std::min(light.b * gain, std::max(top.b, light.b)) };
}

Rgb Full()
{
	const float brightness = BrightnessOfLevel[0];
	Rgb out;
	out.r = std::clamp(brightness * CurrentLook.warm[0] * CurrentLook.tint[0], 0.F, 1.F);
	out.g = std::clamp(brightness * CurrentLook.warm[1] * CurrentLook.tint[1], 0.F, 1.F);
	out.b = std::clamp(brightness * CurrentLook.warm[2] * CurrentLook.tint[2], 0.F, 1.F);
	return out;
}

Rgb Sky()
{
	return SkyRgb;
}

int Lamps(Lamp *out, int maxCount)
{
	int count = 0;
	for (const Source &source : StaticSources) {
		if (count >= maxCount)
			break;
		if (source.lava || source.glow || source.carried || source.flickerKind == StairsBreath)
			continue;
		Lamp &lamp = out[count++];
		lamp.door = source.flickerKind == DoorBreath;
		lamp.x = source.rayX;
		lamp.y = source.rayY;
		lamp.height = source.height;
		lamp.outX = source.coneX;
		lamp.outY = source.coneY;
		lamp.size = source.size;
		lamp.colour = source.colour;
		lamp.strength = source.power * source.flicker;
	}
	return count;
}

bool Blocks(int x, int y)
{
	return BlocksTile(x, y);
}

bool HasLava()
{
	return StaticHasLava;
}

bool IsLava(int x, int y)
{
	return StaticHasLava && x >= 0 && y >= 0 && x < MAXDUNX && y < MAXDUNY && LavaRadius(dPiece[x][y]) != 0;
}

bool Daylight(float &x, float &y, float &reach)
{
	if (!HaveDaylight)
		return false;
	x = DaylightX;
	y = DaylightY;
	reach = DaylightReach;
	return true;
}

void SetOpenGround(bool (*isOpenGround)(int x, int y))
{
	OpenGround = isOpenGround;
}

namespace {

const Source *SourceOf(int index)
{
	if (index < 0)
		return nullptr;
	if (static_cast<size_t>(index) < StaticSources.size())
		return &StaticSources[static_cast<size_t>(index)];
	const size_t dynamic = static_cast<size_t>(index) - StaticSources.size();
	return dynamic < DynamicSources.size() ? &DynamicSources[dynamic] : nullptr;
}

} // namespace

float ToLinear(float brightness)
{
	return brightness <= 0 ? 0.F : std::pow(brightness, Gamma);
}

float ToBrightness(float linear)
{
	return linear <= 0 ? 0.F : std::pow(linear, 1.F / Gamma);
}

float Drawn(float linear)
{
	return DrawnBrightness(linear);
}

float LinearAt(float x, float y)
{
	if (WinW <= 0 || WinH <= 0)
		return 1.F;
	const float fi = std::clamp((x + 0.25F) * Per - static_cast<float>(WinI0), 0.F, static_cast<float>(WinW - 1));
	const float fj = std::clamp((y + 0.25F) * Per - static_cast<float>(WinJ0), 0.F, static_cast<float>(WinH - 1));
	const int i = std::min(static_cast<int>(fi), std::max(WinW - 2, 0));
	const int j = std::min(static_cast<int>(fj), std::max(WinH - 2, 0));
	const float u = std::clamp(fi - static_cast<float>(i), 0.F, 1.F);
	const float v = std::clamp(fj - static_cast<float>(j), 0.F, 1.F);
	const int i2 = std::min(i + 1, WinW - 1);
	const int j2 = std::min(j + 1, WinH - 1);
	const auto at = [](int si, int sj) { return WinBright[static_cast<size_t>(sj) * WinW + si]; };
	return (at(i, j) * (1 - u) + at(i2, j) * u) * (1 - v) + (at(i, j2) * (1 - u) + at(i2, j2) * u) * v;
}

int ShadowLights(float x, float y, ShadowLight *out, int maxCount, ShadowLight &overhead)
{
	overhead = {};
	float overheadLinear = 0;
	int count = 0;
	const auto offer = [&](const ShadowLight &light, float linear, float distance, bool carried) {
		// A light right where the hero or monster stands (its own) hangs over it and casts its shadow straight down.
		// (A glow a little behind, on a monster being pulled, counts too.) A fire or a spell just beside them still
		// casts their shadow away from it.
		if (distance < (carried ? 0.6F : 0.2F)) {
			if (linear > overheadLinear) {
				overheadLinear = linear;
				overhead = light;
			}
			return;
		}
		int at = count;
		while (at > 0 && out[at - 1].strength < light.strength)
			at--;
		if (at >= maxCount)
			return;
		for (int move = std::min(count, maxCount - 1); move > at; move--)
			out[move] = out[move - 1];
		out[at] = light;
		count = std::min(count + 1, maxCount);
	};
	// Lava lies over whole stretches of floor, so all the lava a point sees shines on it as one wide light lying on the
	// floor: as bright as its brightest tile there (as the level's light has it, see ShineStatic), from the middle of its
	// light (each tile weighted by the light it gives there), as wide as that lava is seen across the way to the point.
	// Its shadows are then long (the light lies lower than any body), sharp only right at the feet and soon very soft
	// and faint, except where the lava is most of the light.
	float lavaBrightest = 0;
	float lavaLinear = 0;
	float lavaX = 0;
	float lavaY = 0;
	float lavaXX = 0;
	float lavaXY = 0;
	float lavaYY = 0;
	float lavaPast = 0;
	float lavaFill = 0;
	int lavaRadius = 0;
	const auto consider = [&](const Source &source, int index) {
		const float distance = Length(x - source.x, y - source.y);
		if (distance >= static_cast<float>(source.radius + 1))
			return;
		if (source.lava) {
			if (!Clear(source.rayX, source.rayY, x, y))
				return;
			// Kept from the point, so the sums stay small numbers.
			const float weight = LevelLinear(LevelAt(distance, source.radius)) * source.power;
			const float offX = source.x - x;
			const float offY = source.y - y;
			lavaBrightest = std::max(lavaBrightest, weight);
			lavaLinear += weight;
			lavaX += weight * offX;
			lavaY += weight * offY;
			lavaXX += weight * offX * offX;
			lavaXY += weight * offX * offY;
			lavaYY += weight * offY * offY;
			lavaFill += weight * source.fill;
			lavaPast = std::max(lavaPast, static_cast<float>(source.radius + 1) - distance);
			lavaRadius = std::max(lavaRadius, source.radius);
			return;
		}
		if (!source.castsShadows)
			return;
		const float linear = LevelLinear(LevelAt(distance, source.radius)) * source.power * source.flicker * SeenFraction(source, x, y) * ConeOf(source, x, y);
		const float strength = ToBrightness(linear);
		if (strength < 0.02F)
			return;
		ShadowLight light { source.rayX, source.rayY, strength, source.height, source.size, source.fill, static_cast<float>(source.radius + 1), index };
		light.glow = source.glow;
		offer(light, linear, distance, source.carried);
	};
	int index = 0;
	for (const Source &source : StaticSources)
		consider(source, index++);
	for (const Source &source : DynamicSources)
		consider(source, index++);
	if (leveltype == DTYPE_TOWN) {
		// The evening sky shines on all of the town from far off, so its shadows all fall the same way.
		const float linear = ToLinear(TownSkyBrightness) * SkyScale;
		const ShadowLight sky { x - SkyDirX * SkyDistance, y - SkyDirY * SkyDistance, ToBrightness(linear), SkyDistance / SkyShadowLength, SkySize, SkyFill, SkyDistance * 2, SkyLight };
		offer(sky, linear, SkyDistance, false);
	}
	const float lavaStrength = ToBrightness(lavaBrightest);
	if (lavaStrength >= 0.02F) {
		const float offX = lavaX / lavaLinear;
		const float offY = lavaY / lavaLinear;
		const float distance = Length(offX, offY);
		if (distance >= 0.6F) {
			// How spread out the lava is across the way from it to the point: a strip of lava W tiles across spreads
			// W / sqrt(12) each way, and each tile is itself a tile wide.
			const float alongX = -offX / distance;
			const float alongY = -offY / distance;
			const float spreadXX = std::max(lavaXX / lavaLinear - offX * offX, 0.F);
			const float spreadXY = lavaXY / lavaLinear - offX * offY;
			const float spreadYY = std::max(lavaYY / lavaLinear - offY * offY, 0.F);
			const float across = std::max(alongY * alongY * spreadXX - 2 * alongX * alongY * spreadXY + alongX * alongX * spreadYY, 0.F);
			const float size = std::clamp(std::sqrt(12 * across) + 1, 1.F, 8.F);
			// It shines on round the point as a light at its middle would, as bright as the lava is at the point.
			const float atPoint = LevelLinear(LevelAt(distance, lavaRadius));
			ShadowLight light { x + offX, y + offY, lavaStrength, LavaHeight, size, lavaFill / lavaLinear, distance + lavaPast, LavaLight };
			light.power = std::min(lavaBrightest / std::max(atPoint, 1e-3F), 64.F);
			light.radius = lavaRadius;
			offer(light, lavaBrightest, distance, false);
		}
	}
	return count;
}

float LinearOf(const ShadowLight &light, float x, float y)
{
	if (light.source == SkyLight)
		return ToLinear(TownSkyBrightness) * SkyScale;
	if (light.source == LavaLight) {
		const float distance = Length(x - light.x, y - light.y);
		if (distance >= static_cast<float>(light.radius + 1))
			return 0;
		return LevelLinear(LevelAt(distance, light.radius)) * light.power;
	}
	const Source *source = SourceOf(light.source);
	if (source == nullptr)
		return 0;
	const float distance = Length(x - source->x, y - source->y);
	if (distance >= static_cast<float>(source->radius + 1) || !Clear(source->rayX, source->rayY, x, y))
		return 0;
	return LevelLinear(LevelAt(distance, source->radius)) * source->power * source->flicker * ConeOf(*source, x, y);
}

float GlowAt(float x, float y)
{
	float linear = 0;
	for (const Source &source : DynamicSources) {
		if (!source.glow)
			continue;
		const float distance = Length(x - source.x, y - source.y);
		if (distance < static_cast<float>(source.radius + 1) && Clear(source.rayX, source.rayY, x, y))
			linear += LevelLinear(LevelAt(distance, source.radius)) * source.power;
	}
	return linear;
}

uint16_t UnpaintedPiece(int x, int y, uint16_t piece)
{
	// Each dungeon tile is four pieces in dPiece, from world tile 16 on (gendung.cpp DRLG_LPass3).
	if (x < 16 || y < 16 || pMegaTiles == nullptr)
		return piece;
	const int mx = (x - 16) / 2;
	const int my = (y - 16) / 2;
	if (mx >= DMAXX || my >= DMAXY)
		return piece;
	const uint8_t tile = dungeon[mx][my];
	const uint8_t plain = PlainTileOf(tile, mx, my);
	if (plain == 0)
		return piece;
	const int quarter = ((x - 16) & 1) + ((y - 16) & 1) * 2;
	const auto pieceOf = [quarter](uint8_t megaTile) {
		const MegaTile &mega = pMegaTiles[megaTile - 1];
		const std::array<uint16_t, 4> micros { mega.micro1, mega.micro2, mega.micro3, mega.micro4 };
		return Swap16LE(micros[static_cast<size_t>(quarter)]);
	};
	// Only while the piece is still the painted one: an open door, a quest or another tile set may have changed it since.
	if (pieceOf(tile) != piece)
		return piece;
	return pieceOf(plain);
}

void Reset()
{
	BuiltStatics = {};
	SpillRoots.clear();
	for (Reach &reach : Reaches)
		reach.radius = -1;
	HaveLook = false;
}

} // namespace light

} // namespace hd

} // namespace devilution
