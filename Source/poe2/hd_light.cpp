#include "poe2/hd_light.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "engine/light_tables.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "lighting.h"
#include "missiles.h"
#include "monster.h"
#include "objects.h"
#include "player.h"
#include "poe2/vfx.hpp"
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
	float height = 1;
	bool castsShadows = true;
	/** @brief For a torch on a wall: the open side it faces (the camera sees walls from +x and +y). */
	int faceX = 0;
	int faceY = 0;
};

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

Look CurrentLook;
bool HaveLook = false;
/** @brief Brightness of a light level (0 bright to 15 dark), in 256 steps. */
std::array<float, 256> BrightnessOfLevel {};
/** @brief How warm (1) or cool (0) light of a brightness (0 to 1, in 256 steps) looks, as the old shade table had it. */
std::array<float, 256> WarmthOfBrightness {};

std::array<uint8_t, MAXDUNX * MAXDUNY> BlockMap {};
uint32_t BlockVersion = 0;

std::vector<Source> StaticSources;
std::vector<Source> DynamicSources;
/** @brief What the static light was worked out for: the level, its walls and the game's own baked light. */
struct StaticKey {
	int levelType = -1;
	int level = -1;
	bool setLevel = false;
	int setLevelNum = -1;
	uint32_t blockVersion = UINT32_MAX;
	/** @brief The lit objects and where they stand (a lever can light braziers without touching the baked light). */
	uint32_t objects = 0;
	bool operator==(const StaticKey &other) const = default;
};
StaticKey BuiltStatics;
std::array<uint8_t, MAXDUNX * MAXDUNY> BuiltPreLight {};
/** @brief The static lights' brightness at every sample, and their colours weighted by brightness squared (and the weight). */
std::vector<float> StaticBright;
std::vector<std::array<float, 4>> StaticHue;

/** @brief Which samples of the lattice a moving light reaches, kept while it doesn't move. */
struct Reach {
	int keyX = INT_MIN;
	int keyY = INT_MIN;
	int radius = -1;
	uint32_t blockVersion = UINT32_MAX;
	int i0 = 0;
	int j0 = 0;
	int w = 0;
	int h = 0;
	std::vector<uint8_t> visible;
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

float LevelBrightness(float level)
{
	const float step = std::clamp(level, 0.F, 15.F) * (255.F / 15.F);
	return BrightnessOfLevel[static_cast<size_t>(step + 0.5F)];
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
			if (BlocksTile(tx + sx, ty) && BlocksTile(tx, ty + sy))
				return false;
			tx += sx;
			ty += sy;
			nextX += stepX;
			nextY += stepY;
			steps -= 2;
		}
		if (tx == ex && ty == ey)
			return true;
		if (BlocksTile(tx, ty))
			return false;
	}
	return true;
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

void UpdateBlocks()
{
	std::array<uint8_t, MAXDUNX * MAXDUNY> blocks;
	for (int x = 0; x < MAXDUNX; x++) {
		for (int y = 0; y < MAXDUNY; y++)
			blocks[static_cast<size_t>(x) * MAXDUNY + y] = TileHasAny({ x, y }, TileProperties::BlockLight) ? 1 : 0;
	}
	if (blocks != BlockMap) {
		BlockMap = blocks;
		BlockVersion++;
	}
}

/** @brief The light a dungeon object gives (the ones lighting.cpp's AddObjectLight bakes into the level). */
bool ObjectLight(const Object &object, Source &source)
{
	switch (object._otype) {
	case OBJ_STORYCANDLE:
	case OBJ_L5CANDLE:
		source.radius = 3;
		source.colour = CandleColour;
		source.height = 1.0F;
		return true;
	case OBJ_L1LIGHT:
		source.radius = 5;
		source.colour = CandelabraColour;
		source.height = 1.6F;
		return true;
	case OBJ_SKFIRE:
		source.radius = 5;
		source.colour = FireColour;
		source.height = 0.8F;
		return true;
	case OBJ_CANDLE1:
	case OBJ_CANDLE2:
	case OBJ_BOOKCANDLE:
		source.radius = 5;
		source.colour = CandleColour;
		source.height = 1.0F;
		return true;
	case OBJ_BCROSS:
	case OBJ_TBCROSS:
		source.radius = 5;
		source.colour = BurningCrossColour;
		source.height = 1.2F;
		return true;
	case OBJ_TORCHL2:
	case OBJ_TORCHR:
	case OBJ_TORCHR2:
	case OBJ_TORCHL:
		source.radius = 8;
		source.colour = TorchColour;
		source.height = 1.8F;
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

/** @brief Adds a light's brightness and colour to the samples it reaches and can see. */
void ShineStatic(const Source &source)
{
	const float reach = static_cast<float>(source.radius + 1);
	const int i0 = std::max(0, SampleBefore(source.x - reach));
	const int i1 = std::min(LatticeW - 1, SampleBefore(source.x + reach) + 1);
	const int j0 = std::max(0, SampleBefore(source.y - reach));
	const int j1 = std::min(LatticeH - 1, SampleBefore(source.y + reach) + 1);
	for (int j = j0; j <= j1; j++) {
		const float sy = SampleAt(j);
		for (int i = i0; i <= i1; i++) {
			if (!SampleOpen(i, j))
				continue;
			const float sx = SampleAt(i);
			const float level = LevelAt(Length(sx - source.x, sy - source.y), source.radius);
			if (level >= 15.F)
				continue;
			const float brightness = LevelBrightness(level);
			if (brightness <= 0.002F || !Clear(source.rayX, source.rayY, sx, sy))
				continue;
			const size_t k = static_cast<size_t>(j) * LatticeW + i;
			StaticBright[k] = std::max(StaticBright[k], brightness);
			const float weight = brightness * brightness;
			for (int c = 0; c < 3; c++)
				StaticHue[k][c] += source.colour[c] * weight;
			StaticHue[k][3] += weight;
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

void UpdateStatics()
{
	const StaticKey key { static_cast<int>(leveltype), static_cast<int>(currlevel), setlevel, static_cast<int>(setlvlnum), BlockVersion, ObjectsSignature() };
	if (key == BuiltStatics && std::memcmp(BuiltPreLight.data(), dPreLight, sizeof(dPreLight)) == 0)
		return;
	BuiltStatics = key;
	std::memcpy(BuiltPreLight.data(), dPreLight, sizeof(dPreLight));

	StaticSources.clear();
	for (int i = 0; i < ActiveObjectCount; i++) {
		const Object &object = Objects[ActiveObjects[i]];
		Source source;
		if (!BakedObjectLight(object, source))
			continue;
		source.x = static_cast<float>(object.position.x);
		source.y = static_cast<float>(object.position.y);
		source.colour = Even(source.colour);
		PlaceRays(source);
		StaticSources.push_back(source);
	}
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
			source.height = 0.3F;
			source.castsShadows = false;
			PlaceRays(source);
			StaticSources.push_back(source);
		}
	}

	const float ambient = LevelBrightness(15.F);
	StaticBright.assign(static_cast<size_t>(LatticeW) * LatticeH, ambient);
	StaticHue.assign(static_cast<size_t>(LatticeW) * LatticeH, { 0.F, 0.F, 0.F, 0.F });
	for (const Source &source : StaticSources)
		ShineStatic(source);

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
			const float brightness = LevelBrightness(baked);
			for (int i = x * Per; i < x * Per + Per; i++) {
				for (int j = y * Per; j < y * Per + Per; j++) {
					const size_t k = static_cast<size_t>(j) * LatticeW + i;
					StaticBright[k] = std::max(StaticBright[k], brightness);
					const float weight = brightness * brightness;
					for (int c = 0; c < 3; c++)
						StaticHue[k][c] += residualColour[c] * weight;
					StaticHue[k][3] += weight;
				}
			}
		}
	}
}

void GatherDynamic(float heroX, float heroY)
{
	std::array<Colour, MAXLIGHTS> colours;
	std::array<float, MAXLIGHTS> heights;
	colours.fill(CurrentLook.warm);
	heights.fill(1.4F);
	const auto owns = [](int id) { return id >= 0 && id < MAXLIGHTS; };
	// Owners from the least to the most sure, so a stale id held by a dying missile can't recolour a hero's light.
	for (const Missile &missile : Missiles) {
		if (!owns(missile._mlid))
			continue;
		colours[missile._mlid] = FromBytes(vfx::LightColourOf(missile));
		heights[missile._mlid] = 0.9F;
	}
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &monster = Monsters[ActiveMonsters[i]];
		if (!owns(monster.lightId))
			continue;
		// Uniques glow; other monsters only carry a light while berserk (POE2's Confuse sets the flag without one).
		colours[monster.lightId] = monster.isUnique() ? UniqueGlowColour : BerserkColour;
		heights[monster.lightId] = 1.2F;
	}
	int myLight = NO_LIGHT;
	for (const Player &player : Players) {
		if (!player.plractive || !player.isOnActiveLevel() || !owns(player.lightId))
			continue;
		colours[player.lightId] = CurrentLook.warm;
		heights[player.lightId] = 1.5F;
		if (&player == MyPlayer)
			myLight = player.lightId;
	}

	DynamicSources.clear();
	for (int i = 0; i < ActiveLightCount; i++) {
		const int id = ActiveLights[i];
		const Light &light = Lights[id];
		if (light.isInvalid)
			continue;
		// As the game does: a monster hidden in a wall doesn't give itself away.
		if (TileHasAny(light.position.tile, TileProperties::Solid))
			continue;
		Source source;
		source.x = static_cast<float>(light.position.tile.x) + static_cast<float>(light.position.offset.deltaX) / 8.F;
		source.y = static_cast<float>(light.position.tile.y) + static_cast<float>(light.position.offset.deltaY) / 8.F;
		if (id == myLight && !std::isnan(heroX)) {
			source.x = heroX;
			source.y = heroY;
		}
		source.radius = std::min<int>(light.radius, NumLightRadiuses - 1);
		source.colour = Even(colours[id]);
		source.height = heights[id];
		// Moving lights shine from where they are: a tile can stop light and still be walked on (cathedral piece 450).
		source.rayX = source.x;
		source.rayY = source.y;

		Reach &reach = Reaches[id];
		// What a light can see is worked out again only when it moves an eighth of a tile (the hero's light moves every frame).
		const int keyX = RoundToInt(source.rayX * 8.F);
		const int keyY = RoundToInt(source.rayY * 8.F);
		if (reach.keyX != keyX || reach.keyY != keyY || reach.radius != source.radius || reach.blockVersion != BlockVersion) {
			reach.keyX = keyX;
			reach.keyY = keyY;
			reach.radius = source.radius;
			reach.blockVersion = BlockVersion;
			const float range = static_cast<float>(source.radius + 1);
			reach.i0 = std::max(0, SampleBefore(source.x - range));
			reach.j0 = std::max(0, SampleBefore(source.y - range));
			reach.w = std::min(LatticeW - 1, SampleBefore(source.x + range) + 1) - reach.i0 + 1;
			reach.h = std::min(LatticeH - 1, SampleBefore(source.y + range) + 1) - reach.j0 + 1;
			reach.visible.assign(static_cast<size_t>(std::max(reach.w, 0)) * std::max(reach.h, 0), 0);
			for (int b = 0; b < reach.h; b++) {
				const int j = reach.j0 + b;
				for (int a = 0; a < reach.w; a++) {
					const int i = reach.i0 + a;
					if (SampleOpen(i, j) && Clear(source.rayX, source.rayY, SampleAt(i), SampleAt(j)))
						reach.visible[static_cast<size_t>(b) * reach.w + a] = 1;
				}
			}
		}
		DynamicSources.push_back(source);
		// Light it into this frame's window.
		const int a0 = std::max(0, WinI0 - reach.i0);
		const int a1 = std::min(reach.w, WinI0 + WinW - reach.i0);
		const int b0 = std::max(0, WinJ0 - reach.j0);
		const int b1 = std::min(reach.h, WinJ0 + WinH - reach.j0);
		for (int b = b0; b < b1; b++) {
			const int j = reach.j0 + b;
			const float sy = SampleAt(j);
			for (int a = a0; a < a1; a++) {
				if (reach.visible[static_cast<size_t>(b) * reach.w + a] == 0)
					continue;
				const int i = reach.i0 + a;
				const float level = LevelAt(Length(SampleAt(i) - source.x, sy - source.y), source.radius);
				if (level >= 15.F)
					continue;
				const float brightness = LevelBrightness(level);
				const size_t k = static_cast<size_t>(j - WinJ0) * WinW + (i - WinI0);
				WinBright[k] = std::max(WinBright[k], brightness);
				const float weight = brightness * brightness;
				for (int c = 0; c < 3; c++)
					WinHue[k][c] += source.colour[c] * weight;
				WinHue[k][3] += weight;
			}
		}
	}
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

void Finish()
{
	const Look &look = CurrentLook;
	for (size_t k = 0; k < WinBright.size(); k++) {
		const float brightness = WinBright[k];
		const std::array<float, 4> &hue = WinHue[k];
		const float warmth = WarmthOfBrightness[static_cast<size_t>(std::clamp(brightness, 0.F, 1.F) * 255.F + 0.5F)];
		Rgb &out = WinColour[k];
		std::array<float, 3> colour;
		for (int c = 0; c < 3; c++) {
			const float lightColour = hue[3] > 1e-6F ? hue[c] / hue[3] : look.warm[c];
			const float grade = look.cool[c] + (lightColour - look.cool[c]) * warmth;
			colour[c] = std::clamp(brightness * grade * look.tint[c], 0.F, 1.F);
		}
		out = { colour[0], colour[1], colour[2] };
		WinPixels[k] = 0xFF000000U | (static_cast<uint32_t>(colour[0] * 255.F + 0.5F) << 16) | (static_cast<uint32_t>(colour[1] * 255.F + 0.5F) << 8) | static_cast<uint32_t>(colour[2] * 255.F + 0.5F);
	}
}

} // namespace

bool Update(const Look &look, float x0, float y0, float x1, float y1, float heroX, float heroY)
{
	if (IsAnyOf(leveltype, DTYPE_TOWN, DTYPE_NONE))
		return false;
	UpdateLook(look);
	UpdateBlocks();
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
	for (int j = 0; j < WinH; j++) {
		const size_t from = static_cast<size_t>(WinJ0 + j) * LatticeW + WinI0;
		std::copy_n(StaticBright.begin() + static_cast<ptrdiff_t>(from), WinW, WinBright.begin() + static_cast<ptrdiff_t>(j) * WinW);
		std::copy_n(StaticHue.begin() + static_cast<ptrdiff_t>(from), WinW, WinHue.begin() + static_cast<ptrdiff_t>(j) * WinW);
	}
	GatherDynamic(heroX, heroY);
	FillWalls();
	Finish();
	return true;
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

void FieldRect(float &x0, float &y0, float &x1, float &y1)
{
	x0 = SampleAt(WinI0) - 0.5F / Per;
	y0 = SampleAt(WinJ0) - 0.5F / Per;
	x1 = SampleAt(WinI0 + WinW - 1) + 0.5F / Per;
	y1 = SampleAt(WinJ0 + WinH - 1) + 0.5F / Per;
}

Rgb At(float x, float y)
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
	const Rgb &a = WinColour[static_cast<size_t>(j) * WinW + i];
	const Rgb &b = WinColour[static_cast<size_t>(j) * WinW + i2];
	const Rgb &c = WinColour[static_cast<size_t>(j2) * WinW + i];
	const Rgb &d = WinColour[static_cast<size_t>(j2) * WinW + i2];
	const auto mix = [&](float pa, float pb, float pc, float pd) {
		return (pa * (1 - u) + pb * u) * (1 - v) + (pc * (1 - u) + pd * u) * v;
	};
	return { mix(a.r, b.r, c.r, d.r), mix(a.g, b.g, c.g, d.g), mix(a.b, b.b, c.b, d.b) };
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

bool Blocks(int x, int y)
{
	return BlocksTile(x, y);
}

int ShadowLights(float x, float y, ShadowLight *out, int maxCount, float &total, float &overhead)
{
	total = 0;
	overhead = 0;
	int count = 0;
	const auto consider = [&](const Source &source) {
		if (!source.castsShadows)
			return;
		const float distance = Length(x - source.x, y - source.y);
		if (distance > static_cast<float>(source.radius + 1))
			return;
		const float brightness = LevelBrightness(LevelAt(distance, source.radius));
		if (brightness < 0.02F || !Clear(source.rayX, source.rayY, x, y))
			return;
		total += brightness;
		// A light right where the hero or monster stands (its own lantern) has no direction to cast a shadow in: it hangs
		// overhead and leaves a short shadow round the feet. (A glow a little behind, on a monster being pulled, counts too.)
		if (distance < 0.6F) {
			overhead += brightness;
			return;
		}
		const ShadowLight light { source.x, source.y, brightness, source.height };
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
	for (const Source &source : StaticSources)
		consider(source);
	for (const Source &source : DynamicSources)
		consider(source);
	return count;
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
	for (Reach &reach : Reaches)
		reach.radius = -1;
	HaveLook = false;
}

} // namespace light

} // namespace hd

} // namespace devilution
