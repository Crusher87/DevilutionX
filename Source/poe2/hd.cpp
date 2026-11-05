#include "poe2/hd.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_render.h>
#else
#include <SDL.h>
#endif

#include "engine/assets.hpp"
#include "engine/light_tables.hpp"
#include "engine/load_file.hpp"
#include "engine/palette.h"
#include "engine/render/clx_render.hpp"
#include "engine/render/dun_render.hpp"
#include "engine/render/light_render.hpp"
#include "engine/surface.hpp"
#include "engine/trn.hpp"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "levels/dun_tile.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/tileset_mix.hpp"
#include "levels/gendung.h"
#include "levels/reencode_dun_cels.hpp"
#include "utils/file_util.h"
#include "options.h"
#include "poe2/hd_light.hpp"
#include "utils/is_of.hpp"
#include "utils/png.h"
#include "utils/sdl_wrap.h"

namespace devilution {

namespace hd {

float SubFrame = 0;
Caster CurrentCaster = Caster::None;
Point CurrentTile;
Point CurrentTileOrigin;
bool HaveTile = false;

#if !defined(USE_SDL1) && !defined(USE_SDL3)

namespace {

/** @brief The HD art is twice the size of the game's own pixels. */
constexpr int Scale = 2;

/** @brief Painted shadows are drawn soft and see-through, the way the HD maker draws the hero's. */
constexpr float ShadowDark = 0.55F;
/** @brief How soft a shadow's edge is, in game pixels. */
constexpr float ShadowSoft = 1.5F;
/** @brief Room around a sprite for its shadow's soft edge, in game pixels. */
constexpr int ShadowPad = 5;

constexpr bool IsOutline(SpriteStyle style)
{
	return style == SpriteStyle::Outline || style == SpriteStyle::OutlineSkipZero;
}

enum class DrawKind : uint8_t {
	Sprite,
	Tile,
};

struct Draw {
	DrawKind kind;
	SpriteStyle style;
	TileType tile;
	MaskType mask;
	uint8_t outlineColor;
	/** @brief Top-left corner in the 8-bit world buffer. */
	int x;
	int y;
	int width;
	int height;
	std::optional<ClxSprite> sprite;
	const uint8_t *src;
	const uint8_t *tbl;
	/** @brief The light level the whole draw has, or -1 when its table is a recolour rather than light. */
	int8_t light;
	/** @brief Lit pixel by pixel: its light levels are in LightArena from lightOffset, width x height. */
	bool perPixel;
	uint32_t lightOffset;
	/** @brief Which AI in-between drawing to show after this frame: 0 for none, 1 or 2. */
	uint8_t inBetween;
	/** @brief A hero or monster (casts a shadow with HD Lighting), and how its colours take the light. */
	Caster caster;
	/** @brief Drawn as part of the floor (before hd::EndFloor). */
	bool floor;
	/** @brief The tile it belongs to and where that tile is drawn in the world buffer, when it's known. */
	bool hasTile;
	Point cell;
	Point tileOrigin;
	/** @brief Drawn in the light of the game's per-pixel light map (with HD Lighting the map itself isn't copied). */
	bool lightmapped;
};

/** @brief A drawing turned into colour: the palette index of each pixel, and whether the pixel is drawn (0, 128 blended or 255). */
struct Raster {
	int width = 0;
	int height = 0;
	std::vector<uint8_t> index;
	std::vector<uint8_t> alpha;
	bool cycles = false;
	SDL_Texture *texture = nullptr;
	uint32_t paletteVersion = 0;
	/** @brief The sprite's painted shadow, taken out of its pixels (1 where it was), or empty when it has none. */
	std::vector<uint8_t> shadow;
	SDL_Texture *shadowTexture = nullptr;
	/** @brief The same pixels in the colours the HD tiles were made in, for arches among HD walls. */
	SDL_Texture *packTexture = nullptr;
	uint32_t packVersion = 0;
	/** @brief The sprite's body as a soft black shape, for the shadows it casts with HD Lighting (CastPad pixels around it). */
	SDL_Texture *castTexture = nullptr;
	bool castTried = false;
	/** @brief The first row of the sprite's body, for how far its shadow reaches. */
	int bodyTop = 0;
};

void FreeRasterTextures(Raster &raster)
{
	for (SDL_Texture *texture : { raster.texture, raster.shadowTexture, raster.packTexture, raster.castTexture }) {
		if (texture != nullptr)
			SDL_DestroyTexture(texture);
	}
	raster.texture = nullptr;
	raster.shadowTexture = nullptr;
	raster.packTexture = nullptr;
	raster.castTexture = nullptr;
	raster.castTried = false;
}

struct HdArt {
	SDL_Texture *texture = nullptr;
	bool missing = false;
};

std::vector<Draw> Draws;
/** @brief The per-pixel light levels of this frame's draws, copied as they are drawn. */
std::vector<uint8_t> LightArena;
/** @brief The light level of each pixel of the 8-bit world, from the draw that covers it. */
std::vector<uint8_t> LightBuffer;
SDL_Texture *LightTexture = nullptr;
Size LightTextureSize;
int ViewportOffsetX = 0;
Rectangle ClipRect;
/** @brief The world was recorded for the frame about to be drawn (BeginWorld since the last Render). */
bool WorldRecorded = false;

// Where the tiles sit this frame (hd::SetViewOrigin): the view's first tile and its centre in the world buffer.
bool HaveViewOrigin = false;
Point ViewTile;
float ViewCentreX = 0;
float ViewCentreY = 0;
Point RecordRegion;
/** @brief The draws before this one are the floor (hd::EndFloor). */
bool FloorDone = false;
size_t FloorEnd = 0;
/** @brief This frame is lit with HD Lighting: sprite art leaves out the shadow the maker painted in. */
bool FrameLit = false;
/** @brief The world being recorded will be lit with HD Lighting (so the game's per-pixel light isn't copied). */
bool RecordingLit = false;
SDL_Texture *FieldTexture = nullptr;
Size FieldTextureSize;
/** @brief A soft round dark spot, for the shadow round a hero's feet from the light they carry. */
SDL_Texture *BlobTexture = nullptr;

SDL_Renderer *CacheRenderer = nullptr;
const std::byte *CacheCels = nullptr;
dungeon_type CacheLevelType = DTYPE_NONE;

/** @brief Each frame of a loaded sprite, by the address of its pixels: the name of its HD art. */
std::unordered_map<const uint8_t *, std::string> SpriteNames;
/** @brief Each dungeon frame, by the address of its pixels: its HD art name. */
std::unordered_map<const uint8_t *, std::string> TileNames;
std::unordered_map<std::string, HdArt> Art;

struct RasterKey {
	const uint8_t *data;
	const uint8_t *tbl;
	uint32_t extra;
	bool operator==(const RasterKey &other) const = default;
};
struct RasterKeyHash {
	size_t operator()(const RasterKey &key) const noexcept
	{
		return std::hash<const void *> {}(key.data) ^ (std::hash<const void *> {}(key.tbl) * 31) ^ (key.extra * 1000003U);
	}
};
std::unordered_map<RasterKey, Raster, RasterKeyHash> Rasters;

/** @brief The HD art a draw shows: none, a sprite's or a tile's. */
struct ArtChoice {
	SDL_Texture *texture = nullptr;
	bool hdArt = false;
	bool tileArt = false;
};
struct ArtChoiceKey {
	const uint8_t *data;
	uint64_t shape;
	bool operator==(const ArtChoiceKey &other) const = default;
};
struct ArtChoiceKeyHash {
	size_t operator()(const ArtChoiceKey &key) const noexcept
	{
		return std::hash<const void *> {}(key.data) ^ std::hash<uint64_t> {}(key.shape * 0x9E3779B97F4A7C15ULL);
	}
};
/**
 * @brief Which art each kind of draw ended up with, so the names aren't built and looked up again for every draw
 * of every frame. Emptied whenever the art, the tile names or the sprite names change.
 */
std::unordered_map<ArtChoiceKey, ArtChoice, ArtChoiceKeyHash> ArtChoices;

std::array<float, NumLightingLevels> LightLevel;

/**
 * @brief The HD look's light. It falls off faster than the game's own (an exponent on its light levels), and bright
 * light leans warm like torchlight while shadow leans cool.
 */
struct Mood {
	float falloff;
	std::array<float, 3> warm;
	std::array<float, 3> cool;
};

/** @brief The gentle look the HD world has had since October, everywhere. */
constexpr Mood GentleMood { 1.15F, { 1.05F, 0.97F, 0.86F }, { 0.86F, 0.91F, 1.05F } };

/** @brief With HD Stronger Mood, firelight is warmer and dark corners darker, in a mood of its own for each part of the dungeon. */
const Mood &CurrentMood()
{
	static constexpr Mood Cathedral { 1.35F, { 1.12F, 0.95F, 0.76F }, { 0.74F, 0.84F, 1.08F } };
	static constexpr Mood Catacombs { 1.45F, { 1.00F, 0.97F, 0.93F }, { 0.68F, 0.82F, 1.14F } };
	static constexpr Mood Caves { 1.50F, { 1.16F, 0.90F, 0.66F }, { 0.84F, 0.76F, 0.74F } };
	static constexpr Mood Hell { 1.40F, { 1.18F, 0.86F, 0.64F }, { 1.00F, 0.60F, 0.56F } };
	static constexpr Mood Hive { 1.35F, { 1.08F, 1.02F, 0.70F }, { 0.80F, 0.94F, 0.62F } };
	static constexpr Mood Crypt { 1.35F, { 0.98F, 1.04F, 0.90F }, { 0.72F, 0.98F, 0.80F } };
	if (!*GetOptions().Graphics.hdStrongMood)
		return GentleMood;
	switch (leveltype) {
	case DTYPE_CATHEDRAL: return Cathedral;
	case DTYPE_CATACOMBS: return Catacombs;
	case DTYPE_CAVES: return Caves;
	case DTYPE_HELL: return Hell;
	case DTYPE_NEST: return Hive;
	case DTYPE_CRYPT: return Crypt;
	default: return GentleMood;
	}
}

/** @brief The level's palette against the one the HD tiles were made in: Cathedral levels pick one of four. */
std::array<float, 3> TileGain { 1.F, 1.F, 1.F };
/** @brief Which of its palettes the level uses (1 is the one the pack was made in); the pack can hold tiles recoloured for each. */
int PaletteVariant = 1;
uint32_t PaletteVersion = 1;
std::array<SDL_Color, 256> LastPalette;

int PackState = -1;

/** @brief The palette this level type's HD tiles were made in, and a count of its loads. */
std::array<SDL_Color, 256> PackColours;
bool HavePackColours = false;
uint32_t PackColoursVersion = 0;
/** @brief This level's dungeon pieces show HD art: as the pack made it, or recoloured for the level's palette. */
bool TileArtShown = false;
bool RecolouredTileArtShown = false;

bool HavePack()
{
	if (PackState < 0) {
		AssetRef ref = FindAsset("hd\\pack.txt");
		PackState = ref.ok() ? 1 : 0;
	}
	return PackState == 1;
}

/** @brief This world is lit with HD Lighting: the option is on, it isn't town, and SDL can draw the light's shapes. */
bool LightingWanted()
{
#if SDL_VERSION_ATLEAST(2, 0, 18)
	return *GetOptions().Graphics.hdLighting && leveltype != DTYPE_TOWN;
#else
	return false;
#endif
}

float Luminance(const SDL_Color &color)
{
	return 0.2126F * color.r + 0.7152F * color.g + 0.0722F * color.b;
}

/** @brief How bright each of the 16 light levels leaves the palette, as a fraction. */
void UpdateLightLevels()
{
	for (size_t level = 0; level < NumLightingLevels; level++) {
		float lit = 0;
		float base = 0;
		for (int color = 32; color < 256; color++) {
			const float original = Luminance(logical_palette[color]);
			if (original < 24)
				continue;
			base += original;
			lit += Luminance(logical_palette[LightTables[level][color]]);
		}
		LightLevel[level] = base > 0 ? std::clamp(lit / base, 0.F, 1.F) : 1.F;
	}
}

int LightIndexOf(const uint8_t *tbl)
{
	if (tbl == nullptr || tbl == FullyLitLightTable)
		return 0;
	for (size_t level = 0; level < NumLightingLevels; level++) {
		if (tbl == LightTables[level].data())
			return static_cast<int>(level);
	}
	if (tbl == FullyDarkLightTable)
		return static_cast<int>(NumLightingLevels) - 1;
	return -1;
}

void DestroyTextures()
{
	if (CacheRenderer != nullptr) {
		for (auto &[name, art] : Art) {
			if (art.texture != nullptr)
				SDL_DestroyTexture(art.texture);
		}
		for (auto &[key, raster] : Rasters)
			FreeRasterTextures(raster);
	}
	Art.clear();
	Rasters.clear();
	ArtChoices.clear();
	WorldRecorded = false;
	if (LightTexture != nullptr && CacheRenderer != nullptr)
		SDL_DestroyTexture(LightTexture);
	LightTexture = nullptr;
	if (FieldTexture != nullptr && CacheRenderer != nullptr)
		SDL_DestroyTexture(FieldTexture);
	FieldTexture = nullptr;
	if (BlobTexture != nullptr && CacheRenderer != nullptr)
		SDL_DestroyTexture(BlobTexture);
	BlobTexture = nullptr;
	light::Reset();
}

std::string_view LevelTypeName(dungeon_type type)
{
	switch (type) {
	case DTYPE_TOWN: return "town";
	case DTYPE_CATHEDRAL: return "l1";
	case DTYPE_CATACOMBS: return "l2";
	case DTYPE_CAVES: return "l3";
	case DTYPE_HELL: return "l4";
	case DTYPE_NEST: return "l6";
	case DTYPE_CRYPT: return "l5";
	default: return "none";
	}
}

std::string_view LevelTypeName()
{
	return LevelTypeName(leveltype);
}

/** @brief Names every dungeon frame of the current level, so a drawn frame can find its HD art. */
void IndexTiles()
{
	TileNames.clear();
	ArtChoices.clear();
	TileArtShown = false;
	RecolouredTileArtShown = false;
	CacheCels = pDungeonCels.get();
	CacheLevelType = leveltype;
	if (CacheCels == nullptr)
		return;
	const auto *table = reinterpret_cast<const uint32_t *>(CacheCels);
	const uint32_t count = Swap32LE(table[0]);
	const std::string prefix = std::string("tiles/") + std::string(LevelTypeName()) + "/";
	for (uint32_t frame = 1; frame <= count; frame++) {
		const uint8_t *data = GetDunFrame(CacheCels, frame);
		std::string name = prefix + std::to_string(frame);
		// A map drawn from more than one tile set names each picture by the set it came from.
		dungeon_type sourceType;
		uint32_t sourceFrame;
		if (tilesetmix::Active() && tilesetmix::FrameSource(frame, sourceType, sourceFrame))
			name = std::string("tiles/") + std::string(LevelTypeName(sourceType)) + "/" + std::to_string(sourceFrame);
		TileNames[data] = name;
		TileNames[data + ReencodedTriangleFrameSize] = name + "f";
	}
}

/**
 * @brief Leaves out the soft shadow the HD maker paints under a hero (near-black and see-through), for HD Lighting's
 * own shadows that move with the light.
 */
SDL_Surface *WithoutPaintedShadow(SDL_Surface *surface)
{
	SDL_Surface *rgba = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
	if (rgba == nullptr)
		return surface;
	SDL_FreeSurface(surface);
	if (SDL_LockSurface(rgba) != 0)
		return rgba;
	for (int y = 0; y < rgba->h; y++) {
		auto *row = static_cast<uint8_t *>(rgba->pixels) + static_cast<ptrdiff_t>(y) * rgba->pitch;
		for (int x = 0; x < rgba->w; x++) {
			uint8_t *pixel = row + static_cast<ptrdiff_t>(x) * 4;
			if (pixel[3] != 0 && pixel[3] < 160 && std::max({ pixel[0], pixel[1], pixel[2] }) < 24)
				pixel[3] = 0;
		}
	}
	SDL_UnlockSurface(rgba);
	return rgba;
}

SDL_Texture *LoadArt(SDL_Renderer *renderer, const std::string &name, bool withoutShadow = false)
{
	auto [it, inserted] = Art.try_emplace(withoutShadow ? name + "#lit" : name);
	HdArt &art = it->second;
	if (!inserted)
		return art.texture;
	std::string path = "hd\\" + name + ".png";
	std::replace(path.begin(), path.end(), '/', '\\');
	if (!FindAsset(path).ok()) {
		art.missing = true;
		// For the pack maker: POE2_HD_MISSING=<file> lists the art the game looked for and did not find.
		if (const char *log = std::getenv("POE2_HD_MISSING"); log != nullptr) {
			if (FILE *f = std::fopen(log, "a"); f != nullptr) {
				std::fprintf(f, "%s\n", name.c_str());
				std::fclose(f);
			}
		}
		return nullptr;
	}
	SDL_Surface *surface = LoadPNG(path.c_str());
	if (surface == nullptr) {
		art.missing = true;
		return nullptr;
	}
	if (withoutShadow)
		surface = WithoutPaintedShadow(surface);
	art.texture = SDL_CreateTextureFromSurface(renderer, surface);
	SDL_FreeSurface(surface);
	if (art.texture != nullptr) {
		SDL_SetTextureBlendMode(art.texture, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
		SDL_SetTextureScaleMode(art.texture, SDL_ScaleModeLinear);
#endif
	}
	return art.texture;
}

/**
 * @brief Turns a draw into colour and coverage.
 *
 * The colours come from drawing it solid. The coverage comes from drawing it as the game does, once on
 * black and once on white: pixels that come out the same are opaque, others that the solid draw covers
 * are see-through (blended), and the rest were not drawn.
 */
template <typename DrawFn, typename SolidFn>
void Rasterize(Raster &raster, int width, int height, DrawFn &&draw, SolidFn &&solid)
{
	raster.width = width;
	raster.height = height;
	const size_t size = static_cast<size_t>(width) * height;
	raster.index.assign(size, 0);
	raster.alpha.assign(size, 0);
	std::array<std::vector<uint8_t>, 4> passes;
	for (int pass = 0; pass < 4; pass++) {
		SDLSurfaceUniquePtr surface = SDLWrap::CreateRGBSurfaceWithFormat(0, width, height, 8, SDL_PIXELFORMAT_INDEX8);
		const Surface out(surface.get());
		std::memset(surface->pixels, pass % 2 == 0 ? 0 : 255, static_cast<size_t>(surface->pitch) * height);
		if (pass < 2)
			draw(out);
		else
			solid(out);
		auto &pixels = passes[pass];
		pixels.resize(size);
		for (int y = 0; y < height; y++)
			std::memcpy(&pixels[static_cast<size_t>(y) * width], static_cast<uint8_t *>(surface->pixels) + static_cast<ptrdiff_t>(y) * surface->pitch, width);
	}
	for (size_t i = 0; i < size; i++) {
		if (passes[2][i] != passes[3][i])
			continue;
		raster.index[i] = passes[2][i];
		if (passes[0][i] == passes[1][i])
			raster.alpha[i] = 255;
		else if (passes[0][i] != 0 || passes[1][i] != 255)
			raster.alpha[i] = 128;
		else
			continue;
		if (raster.index[i] >= 1 && raster.index[i] <= 31)
			raster.cycles = true;
	}
	if (!*GetOptions().Graphics.colorCycling || IsNoneOf(leveltype, DTYPE_CAVES, DTYPE_HELL, DTYPE_NEST, DTYPE_CRYPT))
		raster.cycles = false;
}

/** @brief Fills a texture (made if needed) with a raster's pixels in the given palette's colours. */
SDL_Texture *PaintRaster(SDL_Renderer *renderer, SDL_Texture *&texture, const Raster &raster, const SDL_Color *palette)
{
	if (texture == nullptr) {
		texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, raster.width, raster.height);
		if (texture == nullptr)
			return nullptr;
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
		SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
#endif
	}
	std::vector<uint8_t> rgba(static_cast<size_t>(raster.width) * raster.height * 4);
	for (size_t i = 0; i < raster.index.size(); i++) {
		const SDL_Color &color = palette[raster.index[i]];
		rgba[i * 4 + 0] = color.r;
		rgba[i * 4 + 1] = color.g;
		rgba[i * 4 + 2] = color.b;
		rgba[i * 4 + 3] = raster.alpha[i];
	}
	SDL_UpdateTexture(texture, nullptr, rgba.data(), raster.width * 4);
	return texture;
}

SDL_Texture *RasterTexture(SDL_Renderer *renderer, Raster &raster)
{
	if (raster.width <= 0 || raster.height <= 0)
		return nullptr;
	if (raster.texture != nullptr && (!raster.cycles || raster.paletteVersion == PaletteVersion))
		return raster.texture;
	if (PaintRaster(renderer, raster.texture, raster, logical_palette.data()) == nullptr)
		return nullptr;
	raster.paletteVersion = PaletteVersion;
	return raster.texture;
}

/** @brief The raster in the colours the HD tiles were made in, for arches among HD walls. */
SDL_Texture *PackColourTexture(SDL_Renderer *renderer, Raster &raster)
{
	if (raster.width <= 0 || raster.height <= 0)
		return nullptr;
	if (raster.packTexture != nullptr && raster.packVersion == PackColoursVersion)
		return raster.packTexture;
	if (PaintRaster(renderer, raster.packTexture, raster, PackColours.data()) == nullptr)
		return nullptr;
	raster.packVersion = PackColoursVersion;
	return raster.packTexture;
}

/** @brief A Gaussian blur of a coverage mask, along the rows and then down the columns. */
std::vector<float> Blur(const std::vector<float> &mask, int width, int height, float soft)
{
	const int radius = static_cast<int>(soft * 3 + 0.5F);
	std::vector<float> kernel(static_cast<size_t>(radius) * 2 + 1);
	float total = 0;
	for (int k = -radius; k <= radius; k++) {
		kernel[k + radius] = std::exp(-static_cast<float>(k * k) / (2 * soft * soft));
		total += kernel[k + radius];
	}
	for (float &weight : kernel)
		weight /= total;
	std::vector<float> rows(mask.size(), 0.F);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			float sum = 0;
			for (int k = std::max(-radius, -x); k <= std::min(radius, width - 1 - x); k++)
				sum += mask[static_cast<size_t>(y) * width + x + k] * kernel[k + radius];
			rows[static_cast<size_t>(y) * width + x] = sum;
		}
	}
	std::vector<float> out(mask.size(), 0.F);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			float sum = 0;
			for (int k = std::max(-radius, -y); k <= std::min(radius, height - 1 - y); k++)
				sum += rows[static_cast<size_t>(y + k) * width + x] * kernel[k + radius];
			out[static_cast<size_t>(y) * width + x] = sum;
		}
	}
	return out;
}

/** @brief A sprite's painted shadow, soft and see-through, with ShadowPad game pixels of room around it. */
SDL_Texture *ShadowTexture(SDL_Renderer *renderer, Raster &raster)
{
	if (raster.shadow.empty())
		return nullptr;
	if (raster.shadowTexture != nullptr)
		return raster.shadowTexture;
	const int w = raster.width;
	const int h = raster.height;
	// Tucked 3 pixels under the sprite, so no light shows between the two once the edge is softened.
	std::vector<uint8_t> tucked = raster.shadow;
	for (int step = 0; step < 3; step++) {
		std::vector<uint8_t> grown = tucked;
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				const size_t i = static_cast<size_t>(y) * w + x;
				if (tucked[i] != 0 || raster.alpha[i] == 0)
					continue;
				if ((x > 0 && tucked[i - 1] != 0) || (x + 1 < w && tucked[i + 1] != 0) || (y > 0 && tucked[i - w] != 0) || (y + 1 < h && tucked[i + w] != 0))
					grown[i] = 1;
			}
		}
		tucked = std::move(grown);
	}
	const int width = w + ShadowPad * 2;
	const int height = h + ShadowPad * 2;
	std::vector<float> mask(static_cast<size_t>(width) * height, 0.F);
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++)
			mask[static_cast<size_t>(y + ShadowPad) * width + x + ShadowPad] = tucked[static_cast<size_t>(y) * w + x] != 0 ? 1.F : 0.F;
	}
	const std::vector<float> soft = Blur(mask, width, height, ShadowSoft);
	std::vector<uint8_t> rgba(mask.size() * 4, 0);
	for (size_t i = 0; i < soft.size(); i++)
		rgba[i * 4 + 3] = static_cast<uint8_t>(std::clamp(soft[i] * 1.15F, 0.F, 1.F) * ShadowDark * 255.F + 0.5F);
	raster.shadowTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height);
	if (raster.shadowTexture == nullptr) {
		raster.shadow.clear();
		return nullptr;
	}
	SDL_SetTextureBlendMode(raster.shadowTexture, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
	SDL_SetTextureScaleMode(raster.shadowTexture, SDL_ScaleModeLinear);
#endif
	SDL_UpdateTexture(raster.shadowTexture, nullptr, rgba.data(), width * 4);
	return raster.shadowTexture;
}

/** @brief A light map that leaves every pixel fully lit, for drawing art in its own colours. */
Lightmap FullyLitLightmap(const Surface &out)
{
	static std::vector<uint8_t> unlit;
	const size_t size = static_cast<size_t>(out.pitch()) * (out.h() + 1);
	if (unlit.size() < size)
		unlit.assign(size, 0);
	return Lightmap(out.at(0, 0), std::span<const uint8_t>(unlit.data(), size), static_cast<uint16_t>(out.pitch()), LightTables, FullyLitLightTable, FullyDarkLightTable);
}

/** @brief A sprite drawn as it is, with no table: its own palette colours, and the pixels it covers. */
Raster PlainRaster(const Draw &draw, Point position)
{
	Raster plain;
	const auto drawPlain = [&](const Surface &out) { ClxDraw(out, position, *draw.sprite); };
	Rasterize(plain, draw.width, draw.height, drawPlain, drawPlain);
	return plain;
}

/**
 * @brief The game draws a sprite over its own outline, so only the part outside the sprite (its painted shadow
 * included) shows. HD outlines are drawn last, on top, so the part under the sprite is taken out.
 */
void HideOutlineUnderSprite(Raster &raster, const Draw &draw, Point position)
{
	const Raster sprite = PlainRaster(draw, position);
	for (size_t i = 0; i < raster.alpha.size(); i++) {
		if (sprite.alpha[i] != 0)
			raster.alpha[i] = 0;
	}
}

/**
 * @brief Takes a sprite's painted shadow out of its pixels, to be drawn soft and see-through under it instead.
 *
 * Diablo paints shadows solid black (palette colour 0). Only black that reaches the sprite's edge counts:
 * black the sprite closes around (an open coffin, an eye) is part of it.
 */
void SplitShadow(Raster &raster, const Draw &draw, Point position, bool plainColours)
{
	const int width = raster.width;
	const int height = raster.height;
	std::optional<Raster> plain;
	if (!plainColours)
		plain = PlainRaster(draw, position);
	const std::vector<uint8_t> &index = plain ? plain->index : raster.index;
	const auto at = [&](int x, int y) { return static_cast<size_t>(y) * width + x; };
	const auto isBlack = [&](int x, int y) { return raster.alpha[at(x, y)] == 255 && index[at(x, y)] == 0; };
	const auto isOutside = [&](int x, int y) { return x < 0 || y < 0 || x >= width || y >= height || raster.alpha[at(x, y)] == 0; };
	std::vector<uint8_t> shadow(raster.alpha.size(), 0);
	std::vector<Point> open;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			if (isBlack(x, y) && (isOutside(x - 1, y) || isOutside(x + 1, y) || isOutside(x, y - 1) || isOutside(x, y + 1))) {
				shadow[at(x, y)] = 1;
				open.push_back({ x, y });
			}
		}
	}
	if (open.empty())
		return;
	while (!open.empty()) {
		const Point p = open.back();
		open.pop_back();
		for (const Displacement step : { Displacement { -1, 0 }, Displacement { 1, 0 }, Displacement { 0, -1 }, Displacement { 0, 1 } }) {
			const Point next = p + step;
			if (next.x < 0 || next.y < 0 || next.x >= width || next.y >= height || shadow[at(next.x, next.y)] != 0 || !isBlack(next.x, next.y))
				continue;
			shadow[at(next.x, next.y)] = 1;
			open.push_back(next);
		}
	}
	for (size_t i = 0; i < shadow.size(); i++) {
		if (shadow[i] != 0)
			raster.alpha[i] = 0;
	}
	raster.shadow = std::move(shadow);
}

/** @brief The game's own pixels for a draw, coloured by its own table when HD art can't stand in. */
Raster &FallbackRaster(const Draw &draw, bool keepTable)
{
	const uint8_t *tbl = keepTable ? draw.tbl : nullptr;
	if (draw.kind == DrawKind::Tile) {
		const RasterKey key { draw.src, tbl, static_cast<uint32_t>(draw.mask) | (static_cast<uint32_t>(draw.tile) << 8) | (static_cast<uint32_t>(draw.height) << 16) };
		auto [it, inserted] = Rasters.try_emplace(key);
		if (inserted) {
			const auto drawTile = [&](const Surface &out, MaskType mask) {
				RenderTileFrame(out, FullyLitLightmap(out), Point { 0, draw.height - 1 }, draw.tile, draw.src, draw.height, mask, tbl != nullptr ? tbl : FullyLitLightTable);
			};
			Rasterize(it->second, DunFrameWidth, draw.height, [&](const Surface &out) { drawTile(out, draw.mask); }, [&](const Surface &out) { drawTile(out, MaskType::Solid); });
		}
		return it->second;
	}
	const uint32_t extra = static_cast<uint32_t>(draw.style) | (static_cast<uint32_t>(draw.outlineColor) << 8) | (draw.sprite->pixelDataSize() << 16);
	const RasterKey key { draw.sprite->pixelData(), tbl, extra };
	auto [it, inserted] = Rasters.try_emplace(key);
	if (inserted) {
		const int pad = IsOutline(draw.style) ? 1 : 0;
		const Point position { pad, draw.height - 1 - pad };
		const auto drawSprite = [&](const Surface &out, bool blended) {
			if (draw.style == SpriteStyle::OutlineSkipZero)
				ClxDrawOutlineSkipColorZero(out, draw.outlineColor, position, *draw.sprite);
			else if (draw.style == SpriteStyle::Outline)
				ClxDrawOutline(out, draw.outlineColor, position, *draw.sprite);
			else if (blended && tbl != nullptr)
				ClxDrawBlendedTRN(out, position, *draw.sprite, tbl);
			else if (blended)
				ClxDrawBlended(out, position, *draw.sprite);
			else if (tbl != nullptr)
				ClxDrawTRN(out, position, *draw.sprite, tbl);
			else
				ClxDraw(out, position, *draw.sprite);
		};
		Rasterize(it->second, draw.width, draw.height, [&](const Surface &out) { drawSprite(out, draw.style == SpriteStyle::Blended); }, [&](const Surface &out) { drawSprite(out, false); });
		if (draw.style == SpriteStyle::OutlineSkipZero)
			HideOutlineUnderSprite(it->second, draw, position);
		else if (draw.style == SpriteStyle::Normal)
			SplitShadow(it->second, draw, position, tbl == nullptr);
	}
	return it->second;
}

/**
 * @brief A tile's HD art cut to exactly the pixels the game draws for it: the same frame can be drawn as
 * a floor triangle or a full square, and walls near the hero are see-through or half cut away.
 */
SDL_Texture *MaskedArt(SDL_Renderer *renderer, const std::string &name, const Draw &draw)
{
	const std::string key = name + "#" + std::to_string(static_cast<int>(draw.mask)) + "/" + std::to_string(static_cast<int>(draw.tile)) + "/" + std::to_string(draw.height);
	auto [it, inserted] = Art.try_emplace(key);
	HdArt &art = it->second;
	if (!inserted)
		return art.texture;
	art.missing = true;
	std::string path = "hd\\" + name + ".png";
	std::replace(path.begin(), path.end(), '/', '\\');
	if (!FindAsset(path).ok())
		return nullptr;
	SDL_Surface *loaded = LoadPNG(path.c_str());
	if (loaded == nullptr)
		return nullptr;
	SDL_Surface *surface = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
	SDL_FreeSurface(loaded);
	if (surface == nullptr)
		return nullptr;
	const Raster &raster = FallbackRaster(draw, false);
	if (surface->w == raster.width * Scale && surface->h == raster.height * Scale) {
		for (int y = 0; y < surface->h; y++) {
			uint8_t *row = static_cast<uint8_t *>(surface->pixels) + static_cast<ptrdiff_t>(y) * surface->pitch;
			for (int x = 0; x < surface->w; x++) {
				const uint8_t cover = raster.alpha[static_cast<size_t>(y / Scale) * raster.width + x / Scale];
				row[x * 4 + 3] = static_cast<uint8_t>(row[x * 4 + 3] * cover / 255);
			}
		}
		art.texture = SDL_CreateTextureFromSurface(renderer, surface);
		if (art.texture != nullptr) {
			art.missing = false;
			SDL_SetTextureBlendMode(art.texture, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
			// Nearest, like the game's own tiles: smoothing the cut edges of two tiles that meet lets the black
			// under them show through as thin lines wherever the screen isn't drawn at exactly twice the world.
			SDL_SetTextureScaleMode(art.texture, SDL_ScaleModeNearest);
#endif
		}
	}
	SDL_FreeSurface(surface);
	return art.texture;
}

const std::string *ArtName(const Draw &draw)
{
	if (draw.kind == DrawKind::Tile) {
		const auto it = TileNames.find(draw.src);
		return it != TileNames.end() ? &it->second : nullptr;
	}
	if (IsOutline(draw.style))
		return nullptr;
	const auto it = SpriteNames.find(draw.sprite->pixelData());
	return it != SpriteNames.end() ? &it->second : nullptr;
}

/** @brief Works out the HD art a draw shows, if there is any for it. */
ArtChoice ChooseArt(SDL_Renderer *renderer, const Draw &draw)
{
	ArtChoice choice;
	const std::string *name = ArtName(draw);
	if (name == nullptr)
		return choice;
	// Wall tiles cut away near the hero keep the game's own cut-out shape.
	const bool masked = draw.kind == DrawKind::Tile && draw.mask != MaskType::Solid;
	SDL_Texture *texture = nullptr;
	bool recoloured = false;
	// With HD Lighting, heroes and monsters cast shadows of their own in place of the one the maker painted.
	const bool withoutShadow = FrameLit && draw.caster != Caster::None;
	if (draw.inBetween != 0 && !masked)
		texture = LoadArt(renderer, *name + "_m" + std::to_string(draw.inBetween), withoutShadow);
	// Tiles recoloured for this level's palette, e.g. tiles/l1_p3/120, beat evening out the base ones.
	if (texture == nullptr && draw.kind == DrawKind::Tile && PaletteVariant > 1) {
		const size_t slash = name->find('/', 6);
		if (slash != std::string::npos) {
			texture = MaskedArt(renderer, name->substr(0, slash) + "_p" + std::to_string(PaletteVariant) + name->substr(slash), draw);
			recoloured = texture != nullptr;
		}
	}
	if (texture == nullptr)
		texture = draw.kind == DrawKind::Tile ? MaskedArt(renderer, *name, draw) : LoadArt(renderer, *name, withoutShadow);
	choice.texture = texture;
	choice.hdArt = texture != nullptr && draw.kind != DrawKind::Tile;
	choice.tileArt = texture != nullptr && draw.kind == DrawKind::Tile && !recoloured;
	if (texture != nullptr && draw.kind == DrawKind::Tile) {
		if (recoloured)
			RecolouredTileArtShown = true;
		else
			TileArtShown = true;
	}
	return choice;
}

/** @brief The HD art a draw shows, worked out the first time a draw like it comes up. */
ArtChoice CachedArt(SDL_Renderer *renderer, const Draw &draw)
{
	const uint8_t *data = draw.kind == DrawKind::Tile ? draw.src : draw.sprite->pixelData();
	const uint64_t shape = static_cast<uint64_t>(draw.kind) | (static_cast<uint64_t>(draw.style) << 8) | (static_cast<uint64_t>(draw.mask) << 16)
	    | (static_cast<uint64_t>(draw.tile) << 24) | (static_cast<uint64_t>(draw.inBetween) << 32) | (static_cast<uint64_t>(PaletteVariant & 0xFF) << 40)
	    | (static_cast<uint64_t>(draw.height & 0x7FFF) << 48) | (static_cast<uint64_t>(FrameLit && draw.caster != Caster::None ? 1 : 0) << 63);
	auto [it, inserted] = ArtChoices.try_emplace(ArtChoiceKey { data, shape });
	if (inserted)
		it->second = ChooseArt(renderer, draw);
	return it->second;
}

/** @brief The global fade and brightness, as how much of each channel the screen palette keeps. */
std::array<float, 3> ScreenTint()
{
	std::array<float, 3> logical {};
	std::array<float, 3> screen {};
	for (int color = 0; color < 256; color++) {
		logical[0] += logical_palette[color].r;
		logical[1] += logical_palette[color].g;
		logical[2] += logical_palette[color].b;
		screen[0] += system_palette[color].r;
		screen[1] += system_palette[color].g;
		screen[2] += system_palette[color].b;
	}
	std::array<float, 3> tint;
	for (int c = 0; c < 3; c++)
		tint[c] = logical[c] > 0 ? std::clamp(screen[c] / logical[c], 0.F, 1.5F) : 1.F;
	return tint;
}

/** @brief Finds which of the level type's numbered palettes is loaded, by comparing the stone colours. */
int FindPaletteVariant(const char *path)
{
	std::string variant(path);
	const size_t dot = variant.rfind("1.pal");
	if (dot == std::string::npos)
		return 1;
	for (int v = 2; v <= 5; v++) {
		variant[dot] = static_cast<char>('0' + v);
		std::array<uint8_t, 256 * 3> colours;
		if (!LoadFileInMemWithStatus(variant.c_str(), colours).has_value())
			continue;
		bool same = true;
		for (int color = 32; color < 256 && same; color++) {
			same = colours[color * 3] == logical_palette[color].r && colours[color * 3 + 1] == logical_palette[color].g
			    && colours[color * 3 + 2] == logical_palette[color].b;
		}
		if (same)
			return v;
	}
	return 1;
}

void UpdateTileGain()
{
	TileGain = { 1.F, 1.F, 1.F };
	PaletteVariant = 1;
	HavePackColours = false;
	const char *path = PackPalette(static_cast<uint8_t>(leveltype));
	if (path == nullptr)
		return;
	PaletteVariant = FindPaletteVariant(path);
	std::array<uint8_t, 256 * 3> pack;
	if (!LoadFileInMemWithStatus(path, pack).has_value())
		return;
	for (int color = 0; color < 256; color++)
		PackColours[color] = SDL_Color { pack[color * 3], pack[color * 3 + 1], pack[color * 3 + 2], 255 };
	HavePackColours = true;
	PackColoursVersion++;
	std::array<float, 3> current {};
	std::array<float, 3> made {};
	// Skip the colour-cycling entries (water, lava), which move every frame.
	for (int color = 32; color < 256; color++) {
		current[0] += logical_palette[color].r;
		current[1] += logical_palette[color].g;
		current[2] += logical_palette[color].b;
		for (int c = 0; c < 3; c++)
			made[c] += pack[color * 3 + c];
	}
	for (int c = 0; c < 3; c++)
		TileGain[c] = made[c] > 0 ? std::clamp(current[c] / made[c], 0.5F, 2.F) : 1.F;
}

void CheckPalette()
{
	if (std::memcmp(LastPalette.data(), logical_palette.data(), sizeof(LastPalette)) != 0) {
		std::memcpy(LastPalette.data(), logical_palette.data(), sizeof(LastPalette));
		PaletteVersion++;
		UpdateLightLevels();
		UpdateTileGain();
	}
}

/** @brief The draw is one of the level's arches (drawn over the walls, from the level's own extra art). */
bool IsArchPiece(const Draw &draw)
{
	if (draw.kind != DrawKind::Sprite || !pSpecialCels)
		return false;
	const ClxSpriteList arches { *pSpecialCels };
	const uint8_t *data = draw.sprite->pixelData();
	return data >= arches.data() && data < arches.data() + arches.dataSize();
}

/**
 * @brief The walls show HD art made in the level type's first palette, evened out to this level's palette by
 * TileGain. Arches have no HD art, so they take the same colours, or they stand out in the walls' old colours.
 */
bool ArchesTakeTileColours()
{
	return TileArtShown && !RecolouredTileArtShown && HavePackColours && PaletteVariant > 1;
}

/** @brief Copies the light levels a per-pixel lit draw is about to use. */
uint32_t CaptureLight(const Surface &out, const Lightmap &lightmap, Point topLeft, int width, int height, const uint8_t *tbl)
{
	// Pixels outside the light map keep the light of the draw as a whole.
	const uint8_t fill = static_cast<uint8_t>(std::max(LightIndexOf(tbl), 0));
	const auto offset = static_cast<uint32_t>(LightArena.size());
	LightArena.resize(LightArena.size() + static_cast<size_t>(width) * height, fill);
	const std::span<const uint8_t> buffer = lightmap.buffer();
	const int x0 = std::max(0, topLeft.x);
	const int x1 = std::min(out.w(), topLeft.x + width);
	if (x0 >= x1)
		return offset;
	for (int row = 0; row < height; row++) {
		const int y = topLeft.y + row;
		if (y < 0 || y >= out.h())
			continue;
		const uint8_t *src = lightmap.getLightingAt(out.at(x0, y));
		const ptrdiff_t start = src - buffer.data();
		if (start < 0 || static_cast<size_t>(start) >= buffer.size())
			continue;
		const size_t count = std::min<size_t>(x1 - x0, buffer.size() - start);
		std::memcpy(&LightArena[offset + static_cast<size_t>(row) * width + (x0 - topLeft.x)], src, count);
	}
	return offset;
}

/** @brief Builds the light of every pixel from the draws, the way the game's own lighting left it. */
void BuildLightBuffer(Size size)
{
	LightBuffer.assign(static_cast<size_t>(size.width) * size.height, 0);
	for (const Draw &draw : Draws) {
		if (draw.style != SpriteStyle::Normal)
			continue;
		const Raster &raster = FallbackRaster(draw, draw.light < 0);
		// Only the part of the draw inside the buffer, worked out once for the whole draw.
		const int col0 = std::max(0, -draw.x);
		const int col1 = std::min(raster.width, size.width - draw.x);
		const int row0 = std::max(0, -draw.y);
		const int row1 = std::min(raster.height, size.height - draw.y);
		if (col0 >= col1 || row0 >= row1)
			continue;
		const auto uniform = static_cast<uint8_t>(std::max<int>(draw.light, 0));
		const int count = col1 - col0;
		for (int row = row0; row < row1; row++) {
			const size_t from = static_cast<size_t>(row) * raster.width + col0;
			const uint8_t *alpha = raster.alpha.data() + from;
			uint8_t *out = LightBuffer.data() + static_cast<size_t>(draw.y + row) * size.width + (draw.x + col0);
			if (draw.perPixel) {
				const uint8_t *light = LightArena.data() + draw.lightOffset + from;
				for (int col = 0; col < count; col++)
					out[col] = alpha[col] == 255 ? light[col] : out[col];
			} else {
				for (int col = 0; col < count; col++)
					out[col] = alpha[col] == 255 ? uniform : out[col];
			}
		}
	}
}

/** @brief The shade the old light layer gives one of the game's light levels: its brightness, warm or cool, and the screen's fade. */
std::array<float, 3> LevelShade(int level, const std::array<float, 3> &tint)
{
	const Mood &mood = CurrentMood();
	level = std::clamp(level, 0, static_cast<int>(NumLightingLevels) - 1);
	const float warmth = std::clamp(1.F - static_cast<float>(level) / 10.F, 0.F, 1.F);
	std::array<float, 3> shade;
	for (int c = 0; c < 3; c++) {
		const float grade = mood.cool[c] + (mood.warm[c] - mood.cool[c]) * warmth;
		shade[c] = std::pow(LightLevel[level], mood.falloff) * grade * tint[c];
	}
	return shade;
}

using Colour3 = std::array<float, 3>;

/** @brief How a draw is lit: one colour, or colours that change from its left edge to its right, at up to three stops. */
struct Shading {
	std::array<Colour3, 3> colours {};
	/** @brief Where each colour is, as a fraction of the draw's width. */
	std::array<float, 3> stops { 0.F, 0.5F, 1.F };
	int count = 1;
};

Shading Uniform(const Colour3 &colour)
{
	Shading shading;
	shading.colours[0] = colour;
	return shading;
}

Shading Gradient(const Colour3 &left, const Colour3 &right)
{
	Shading shading;
	shading.colours = { left, right, right };
	shading.stops = { 0.F, 1.F, 1.F };
	shading.count = 2;
	return shading;
}

Colour3 ShadeAt(const Shading &shading, float x)
{
	if (shading.count == 1)
		return shading.colours[0];
	for (int s = 1; s < shading.count; s++) {
		if (x > shading.stops[s] && s < shading.count - 1)
			continue;
		const float span = shading.stops[s] - shading.stops[s - 1];
		const float t = span > 1e-6F ? std::clamp((x - shading.stops[s - 1]) / span, 0.F, 1.F) : 1.F;
		Colour3 out;
		for (int c = 0; c < 3; c++)
			out[c] = shading.colours[s - 1][c] + (shading.colours[s][c] - shading.colours[s - 1][c]) * t;
		return out;
	}
	return shading.colours[shading.count - 1];
}

Uint8 ColourByte(float value)
{
	return static_cast<Uint8>(std::clamp(255.F * value, 0.F, 255.F));
}

/** @brief What a draw shows: its HD art or the game's own pixels, and the painted shadow that goes under the game's pixels. */
struct DrawTexture {
	SDL_Texture *texture = nullptr;
	SDL_Texture *shadow = nullptr;
	bool hdArt = false;
	/** @brief In the colours the HD tiles were made in, so it's evened out to the level's palette by TileGain. */
	bool tileColours = false;
};

DrawTexture TextureFor(SDL_Renderer *renderer, const Draw &draw, bool paintedShadow)
{
	DrawTexture out;
	ArtChoice art;
	if (draw.light >= 0)
		art = CachedArt(renderer, draw);
	out.texture = art.texture;
	out.hdArt = art.hdArt;
	out.tileColours = art.tileArt;
	// Recolours other than light (stone curse, infravision, unique monsters) are drawn by the game itself.
	if (out.texture == nullptr) {
		Raster &raster = FallbackRaster(draw, draw.light < 0);
		if (paintedShadow)
			out.shadow = ShadowTexture(renderer, raster);
		if (ArchesTakeTileColours() && IsArchPiece(draw)) {
			out.texture = PackColourTexture(renderer, raster);
			out.tileColours = out.texture != nullptr;
		}
		if (out.texture == nullptr)
			out.texture = RasterTexture(renderer, raster);
	}
	return out;
}

SDL_FRect DestOf(const Draw &draw, float toLogical)
{
	return SDL_FRect {
		(ViewportOffsetX + static_cast<float>(draw.x) * Scale) * toLogical,
		static_cast<float>(draw.y) * Scale * toLogical,
		static_cast<float>(draw.width) * Scale * toLogical,
		static_cast<float>(draw.height) * Scale * toLogical,
	};
}

/** @brief The view on screen, in the renderer's logical pixels. */
SDL_FRect ViewOnScreen;

bool OnScreen(const SDL_FRect &rect, float margin)
{
	return rect.x - margin < ViewOnScreen.x + ViewOnScreen.w && rect.x + rect.w + margin > ViewOnScreen.x
	    && rect.y - margin < ViewOnScreen.y + ViewOnScreen.h && rect.y + rect.h + margin > ViewOnScreen.y;
}

/** @brief The colours a texture is multiplied by: the light, and for art in the HD tiles' colours, the level's palette. */
Colour3 ArtColour(const Colour3 &light, const DrawTexture &art)
{
	Colour3 colour = light;
	if (art.tileColours) {
		for (int c = 0; c < 3; c++)
			colour[c] *= std::min(TileGain[c], 1.F);
	}
	return colour;
}

/** @brief Draws one recorded draw, lit by one colour or a left-to-right gradient. */
void DrawShaded(SDL_Renderer *renderer, const Draw &draw, const Shading &shading, float toLogical, bool paintedShadow)
{
	const SDL_FRect dest = DestOf(draw, toLogical);
	if (!OnScreen(dest, static_cast<float>(ShadowPad * Scale) * toLogical))
		return;
	const DrawTexture art = TextureFor(renderer, draw, paintedShadow);
	if (art.texture == nullptr)
		return;
	if (art.shadow != nullptr) {
		const SDL_FRect around {
			(ViewportOffsetX + static_cast<float>(draw.x - ShadowPad) * Scale) * toLogical,
			static_cast<float>(draw.y - ShadowPad) * Scale * toLogical,
			static_cast<float>(draw.width + ShadowPad * 2) * Scale * toLogical,
			static_cast<float>(draw.height + ShadowPad * 2) * Scale * toLogical,
		};
		SDL_RenderCopyF(renderer, art.shadow, nullptr, &around);
	}
	// The game's own see-through pixels are already in the fallback's coverage.
	const Uint8 alpha = art.hdArt && draw.style == SpriteStyle::Blended ? 128 : 255;
	if (shading.count == 1) {
		const Colour3 colour = ArtColour(shading.colours[0], art);
		SDL_SetTextureColorMod(art.texture, ColourByte(colour[0]), ColourByte(colour[1]), ColourByte(colour[2]));
		SDL_SetTextureAlphaMod(art.texture, alpha);
		SDL_RenderCopyF(renderer, art.texture, nullptr, &dest);
		return;
	}
#if SDL_VERSION_ATLEAST(2, 0, 18)
	// Drawn as shapes with a colour at each corner (which leave the texture's own colour and alpha out).
	SDL_SetTextureColorMod(art.texture, 255, 255, 255);
	SDL_SetTextureAlphaMod(art.texture, 255);
	std::array<SDL_Vertex, 6> vertices;
	for (int s = 0; s < shading.count; s++) {
		const float fx = shading.stops[s];
		const Colour3 colour = ArtColour(shading.colours[s], art);
		const SDL_Color vertexColour { ColourByte(colour[0]), ColourByte(colour[1]), ColourByte(colour[2]), alpha };
		vertices[s * 2] = SDL_Vertex { { dest.x + fx * dest.w, dest.y }, vertexColour, { fx, 0.F } };
		vertices[s * 2 + 1] = SDL_Vertex { { dest.x + fx * dest.w, dest.y + dest.h }, vertexColour, { fx, 1.F } };
	}
	std::array<int, 12> indices;
	int count = 0;
	for (int s = 0; s + 1 < shading.count; s++) {
		for (const int corner : { 0, 2, 3, 0, 3, 1 })
			indices[count++] = s * 2 + corner;
	}
	SDL_RenderGeometry(renderer, art.texture, vertices.data(), shading.count * 2, indices.data(), count);
#endif
}

#if SDL_VERSION_ATLEAST(2, 0, 18)

constexpr float HalfTileWidth = TILE_WIDTH / 2.F;
constexpr float HalfTileHeight = TILE_HEIGHT / 2.F;
/** @brief A tile's length across the screen (its floor diamond's width over the square root of 2), in game pixels. */
constexpr float TileAcross = 45.254834F;
constexpr std::array<int, 6> QuadIndices { 0, 1, 2, 0, 2, 3 };

// HD Lighting's shadows: how dark one light's shadow gets at most, how soft its edge is and the room around it (game
// pixels), and the short shadow round the feet that a light overhead (the hero's own) leaves.
constexpr float CastDark = 0.5F;
constexpr float CastSoft = 2.0F;
constexpr int CastPad = 6;
constexpr float ContactDark = 0.42F;
/** @brief How much of the light a hero carries fills the shadows other lights cast of them (it hangs close over them). */
constexpr float CarriedFill = 0.35F;
/** @brief How wide the shadow round the feet is, in game pixels (it is half as deep, lying on the floor), and how far
 * below the tile's centre its middle is (the art stands its feet a little below it). */
constexpr float ContactWidth = 52.F;
constexpr float ContactDrop = 5.F;
/** @brief The hero is never drawn darker than this much of full light (the game always draws them fully lit). */
constexpr float HeroLightFloor = 0.85F;

/** @brief Where a world point (in tiles) is drawn in the world buffer. */
void BufferOf(float wx, float wy, float &bx, float &by)
{
	const float u = wx - static_cast<float>(ViewTile.x);
	const float v = wy - static_cast<float>(ViewTile.y);
	bx = ViewCentreX + (u - v) * HalfTileWidth;
	by = ViewCentreY + (u + v) * HalfTileHeight;
}

/** @brief The world point (in tiles) on the floor drawn at a point of the world buffer. */
void WorldOf(float bx, float by, float &wx, float &wy)
{
	const float a = (bx - ViewCentreX) / HalfTileWidth;
	const float b = (by - ViewCentreY) / HalfTileHeight;
	wx = static_cast<float>(ViewTile.x) + (a + b) / 2;
	wy = static_cast<float>(ViewTile.y) + (b - a) / 2;
}

/** @brief Where a sprite's hero, monster or object stands in the world buffer: the game draws them half a tile above the sprite's bottom. */
void GroundOf(const Draw &draw, float &bx, float &by)
{
	bx = static_cast<float>(draw.x) + static_cast<float>(draw.width) / 2;
	by = static_cast<float>(draw.y + draw.height - 1) - HalfTileHeight;
}

/** @brief Where a sprite stands in the world, in tiles. */
void StandsAt(const Draw &draw, float &wx, float &wy)
{
	float bx;
	float by;
	GroundOf(draw, bx, by);
	WorldOf(bx, by, wx, wy);
}

SDL_FPoint OnScreenAt(float bx, float by, float toLogical)
{
	return SDL_FPoint { (ViewportOffsetX + bx * Scale) * toLogical, by * Scale * toLogical };
}

Colour3 ToColour(const light::Rgb &rgb)
{
	return { rgb.r, rgb.g, rgb.b };
}

Colour3 LightAt(float x, float y)
{
	return ToColour(light::At(x, y));
}

/**
 * @brief The walls a tile has standing on its back edges, which is where Diablo stands its walls: the left one (on the
 * tile's x-min edge) faces +x, the right one (y-min edge) faces +y, and the tile's own floor lies in front of them.
 */
struct BackWalls {
	bool left = false;
	bool right = false;
};

BackWalls BackWallsOf(Point tile)
{
	if (!InDungeonBounds(tile) || !light::Blocks(tile.x, tile.y))
		return {};
	const MICROS &micros = DPieceMicros[dPiece[tile.x][tile.y]];
	const auto isWall = [](LevelCelBlock block, TileType floorOnly) { return !block.hasValue() || block.type() != floorOnly; };
	return { isWall(micros.mt[0], TileType::LeftTriangle), isWall(micros.mt[1], TileType::RightTriangle) };
}

/** @brief Going from one tile to the next (a step of one tile, or a corner) crosses a wall. */
bool CrossesWall(Point from, Point to)
{
	const auto crossX = [](Point a, Point b) { return b.x > a.x ? BackWallsOf(b).left : BackWallsOf(a).left; };
	const auto crossY = [](Point a, Point b) { return b.y > a.y ? BackWallsOf(b).right : BackWallsOf(a).right; };
	if (from.x != to.x && from.y != to.y) {
		// Through a corner: it gets past if either way round is open.
		const Point viaX { to.x, from.y };
		const Point viaY { from.x, to.y };
		return (crossX(from, viaX) || crossY(viaX, to)) && (crossY(from, viaY) || crossX(viaY, to));
	}
	if (from.x != to.x)
		return crossX(from, to);
	if (from.y != to.y)
		return crossY(from, to);
	return false;
}

/** @brief How far (in tiles) a shadow can lie on the floor from a point in a direction before a wall stops it. */
float RoomFor(float x, float y, float dx, float dy, float limit)
{
	constexpr float Step = 1.F / 16;
	Point previous { static_cast<int>(std::floor(x + 0.5F)), static_cast<int>(std::floor(y + 0.5F)) };
	for (float t = Step; t < limit; t += Step) {
		const Point current { static_cast<int>(std::floor(x + dx * t + 0.5F)), static_cast<int>(std::floor(y + dy * t + 0.5F)) };
		if (current == previous)
			continue;
		if (CrossesWall(previous, current))
			return t;
		previous = current;
	}
	return limit;
}

/** @brief A dungeon tile's bottom piece drawn with the walls (DrawCell): the floor in front of the tile's walls, and the walls' foot. */
bool IsTileBase(const Draw &draw)
{
	return draw.kind == DrawKind::Tile && !draw.floor && draw.hasTile && InDungeonBounds(draw.cell)
	    && draw.y + draw.height - 1 == draw.tileOrigin.y;
}

bool IsFloorOnly(const Draw &draw)
{
	return draw.tile == TileType::LeftTriangle || draw.tile == TileType::RightTriangle;
}

bool IsRightColumn(const Draw &draw)
{
	return draw.x - draw.tileOrigin.x >= DunFrameWidth;
}

/** @brief The light on the floor at a point of a tile, never taken from behind the walls on the tile's back edges. */
Colour3 FloorLightAt(float x, float y, Point tile, BackWalls walls)
{
	if (walls.left)
		x = std::max(x, static_cast<float>(tile.x) - 0.25F);
	if (walls.right)
		y = std::max(y, static_cast<float>(tile.y) - 0.25F);
	return LightAt(x, y);
}

/**
 * @brief A wall tile's faces take the light of the room behind them as well as their own: where the game draws the
 * walls see-through (DrawCell's test), as they stand between the view and the room they belong to, and where the faces
 * look out onto solid rock, as the outer side of the dungeon's walls does, which no light reaches.
 */
bool LitFromBehind(Point tile)
{
	if (TileHasAny(tile, TileProperties::Transparent) && TransList[static_cast<uint8_t>(dTransVal[tile.x][tile.y])])
		return true;
	for (const Displacement step : { Displacement { 1, 0 }, Displacement { 0, 1 }, Displacement { 1, 1 } }) {
		const Point front = tile + step;
		if (InDungeonBounds(front) && !light::Blocks(front.x, front.y))
			return false;
	}
	return true;
}

/** @brief How far (in tiles) the room's light reaches past the foot of a see-through wall, fading out. */
constexpr float SpillDepth = 0.5F;

/**
 * @brief The light on the floor in front of a tile's walls. In front of walls lit from behind it is outside the room
 * the walls belong to, so the room's light spills a little way past the walls' foot and lights the bottoms of their
 * columns.
 */
Colour3 BaseFloorLight(float x, float y, Point tile, BackWalls walls, bool fromBehind)
{
	Colour3 light = FloorLightAt(x, y, tile, walls);
	if (!fromBehind)
		return light;
	const float tx = static_cast<float>(tile.x);
	const float ty = static_cast<float>(tile.y);
	const auto spill = [&](float distance, const Colour3 &room) {
		const float weight = std::clamp(1.F - distance / SpillDepth, 0.F, 1.F);
		for (int c = 0; c < 3; c++)
			light[c] = std::max(light[c], room[c] * weight);
	};
	if (walls.left)
		spill(x - (tx - 0.5F), LightAt(tx - 0.75F, std::clamp(y, ty - 0.5F, ty + 0.5F)));
	if (walls.right)
		spill(y - (ty - 0.5F), LightAt(std::clamp(x, tx - 0.5F, tx + 0.5F), ty - 0.75F));
	return light;
}

/** @brief The world point (in tiles) drawn at a point (fx, fy, 0 to 1) of a tile's bottom piece, on the floor's plane. */
void BaseWorldOf(bool right, float fx, float fy, Point tile, float &x, float &y)
{
	const float tx = static_cast<float>(tile.x);
	const float ty = static_cast<float>(tile.y);
	if (right) {
		x = tx + 0.5F - (1.F - fy - fx / 2);
		y = ty - 0.5F + (fy - fx / 2);
	} else {
		x = tx - 0.5F + (fx + 2 * fy - 1) / 2;
		y = ty + 0.5F - (fx - 2 * fy + 1) / 2;
	}
}

/**
 * @brief Draws the floor half of a tile's bottom piece in the light over the floor.
 *
 * The half floor diamond is cut into triangles at the light samples' rows and columns, so its light matches the
 * floor next to it exactly along every edge.
 */
void DrawTileFloor(SDL_Renderer *renderer, const Draw &draw, float toLogical)
{
	const SDL_FRect dest = DestOf(draw, toLogical);
	if (!OnScreen(dest, 0))
		return;
	const DrawTexture art = TextureFor(renderer, draw, false);
	if (art.texture == nullptr)
		return;
	SDL_SetTextureColorMod(art.texture, 255, 255, 255);
	SDL_SetTextureAlphaMod(art.texture, 255);
	const bool right = IsRightColumn(draw);
	const BackWalls walls = BackWallsOf(draw.cell);
	const bool fromBehind = LitFromBehind(draw.cell);
	const float tx = static_cast<float>(draw.cell.x);
	const float ty = static_cast<float>(draw.cell.y);
	// The half diamond's corner where its two outer edges meet, and points along those edges at fractions a and b:
	// left half, from (0, 1/2) towards the top (1, 0) and the bottom (1, 1); right half, from (1, 1/2) towards (0, 0) and (0, 1).
	static constexpr std::array<float, 4> Steps { 0.F, 0.25F, 0.75F, 1.F };
	std::array<SDL_Vertex, 10> vertices;
	std::array<std::array<int, 4>, 4> at;
	int count = 0;
	for (int ia = 0; ia < 4; ia++) {
		for (int ib = 0; ib < 4; ib++) {
			at[ia][ib] = -1;
			const float a = Steps[ia];
			const float b = Steps[ib];
			if (a + b > 1.001F)
				continue;
			const float fx = right ? 1.F - a - b : a + b;
			const float fy = 0.5F - 0.5F * a + 0.5F * b;
			const float wx = right ? tx + 0.5F - a : tx - 0.5F + b;
			const float wy = right ? ty - 0.5F + b : ty + 0.5F - a;
			const Colour3 colour = ArtColour(BaseFloorLight(wx, wy, draw.cell, walls, fromBehind), art);
			vertices[count] = SDL_Vertex {
				{ dest.x + fx * dest.w, dest.y + fy * dest.h },
				{ ColourByte(colour[0]), ColourByte(colour[1]), ColourByte(colour[2]), 255 },
				{ fx, fy },
			};
			at[ia][ib] = count++;
		}
	}
	std::array<int, 27> indices;
	int used = 0;
	for (int ia = 0; ia < 3; ia++) {
		for (int ib = 0; ib < 3; ib++) {
			const int v00 = at[ia][ib];
			const int v10 = at[ia + 1][ib];
			const int v01 = at[ia][ib + 1];
			const int v11 = at[ia + 1][ib + 1];
			if (v00 < 0 || v10 < 0 || v01 < 0)
				continue;
			for (const int v : { v00, v10, v01 })
				indices[used++] = v;
			if (v11 >= 0) {
				for (const int v : { v10, v11, v01 })
					indices[used++] = v;
			}
		}
	}
	SDL_RenderGeometry(renderer, art.texture, vertices.data(), count, indices.data(), used);
}

/**
 * @brief Draws the rest of a tile's bottom piece: the foot of its wall, above the floor's back edge, lit like the wall's
 * face, and below the floor's front edge lit like the floor. Where the piece is only floor, both are lit like the floor:
 * they hold the floor's own edge, which juts a little past the floor's shape.
 */
void DrawTileWall(SDL_Renderer *renderer, const Draw &draw, const Shading &shading, float toLogical)
{
	const SDL_FRect dest = DestOf(draw, toLogical);
	if (!OnScreen(dest, 0))
		return;
	const DrawTexture art = TextureFor(renderer, draw, false);
	if (art.texture == nullptr)
		return;
	SDL_SetTextureColorMod(art.texture, 255, 255, 255);
	SDL_SetTextureAlphaMod(art.texture, 255);
	static constexpr std::array<std::array<float, 2>, 6> LeftHalf { { { 0, 0 }, { 1, 0 }, { 0, 0.5F }, { 0, 0.5F }, { 1, 1 }, { 0, 1 } } };
	static constexpr std::array<std::array<float, 2>, 6> RightHalf { { { 0, 0 }, { 1, 0 }, { 1, 0.5F }, { 1, 0.5F }, { 1, 1 }, { 0, 1 } } };
	const bool right = IsRightColumn(draw);
	const auto &corners = right ? RightHalf : LeftHalf;
	const BackWalls walls = BackWallsOf(draw.cell);
	const bool fromBehind = LitFromBehind(draw.cell);
	const bool floorOnly = IsFloorOnly(draw);
	std::array<SDL_Vertex, 6> vertices;
	for (size_t i = 0; i < corners.size(); i++) {
		const float fx = corners[i][0];
		const float fy = corners[i][1];
		Colour3 light;
		if (i < 3 && !floorOnly) {
			light = ShadeAt(shading, fx);
		} else {
			float wx;
			float wy;
			BaseWorldOf(right, fx, fy, draw.cell, wx, wy);
			light = BaseFloorLight(wx, wy, draw.cell, walls, fromBehind);
		}
		const Colour3 colour = ArtColour(light, art);
		vertices[i] = SDL_Vertex {
			{ dest.x + fx * dest.w, dest.y + fy * dest.h },
			{ ColourByte(colour[0]), ColourByte(colour[1]), ColourByte(colour[2]), 255 },
			{ fx, fy },
		};
	}
	SDL_RenderGeometry(renderer, art.texture, vertices.data(), static_cast<int>(vertices.size()), nullptr, 0);
}

/** @brief Darkens the floor drawn so far by the light over it: one shape covering the view, the light's samples smoothed between. */
void DrawFloorLight(SDL_Renderer *renderer, float toLogical)
{
	const int width = light::FieldWidth();
	const int height = light::FieldHeight();
	if (width <= 0 || height <= 0)
		return;
	// A little bigger than needed, so it isn't made again whenever the view moves by a sample.
	if (FieldTexture == nullptr || FieldTextureSize.width < width || FieldTextureSize.height < height) {
		if (FieldTexture != nullptr)
			SDL_DestroyTexture(FieldTexture);
		const Size size { (width + 31) / 32 * 32, (height + 31) / 32 * 32 };
		FieldTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, size.width, size.height);
		FieldTextureSize = size;
		if (FieldTexture == nullptr)
			return;
		SDL_SetTextureBlendMode(FieldTexture, SDL_BLENDMODE_MOD);
		SDL_SetTextureScaleMode(FieldTexture, SDL_ScaleModeLinear);
	}
	const SDL_Rect part { 0, 0, width, height };
	SDL_UpdateTexture(FieldTexture, &part, light::FieldPixels(), width * 4);
	float x0;
	float y0;
	float x1;
	float y1;
	light::FieldRect(x0, y0, x1, y1);
	const float u1 = static_cast<float>(width) / static_cast<float>(FieldTextureSize.width);
	const float v1 = static_cast<float>(height) / static_cast<float>(FieldTextureSize.height);
	const std::array<std::array<float, 4>, 4> corners { { { x0, y0, 0, 0 }, { x1, y0, u1, 0 }, { x1, y1, u1, v1 }, { x0, y1, 0, v1 } } };
	std::array<SDL_Vertex, 4> vertices;
	for (size_t i = 0; i < corners.size(); i++) {
		float bx;
		float by;
		BufferOf(corners[i][0], corners[i][1], bx, by);
		vertices[i] = SDL_Vertex { OnScreenAt(bx, by, toLogical), { 255, 255, 255, 255 }, { corners[i][2], corners[i][3] } };
	}
	SDL_RenderGeometry(renderer, FieldTexture, vertices.data(), 4, QuadIndices.data(), static_cast<int>(QuadIndices.size()));
}

/** @brief The sprite's body as a soft black shape at half size, with CastPad game pixels of room around it, for the shadows it casts. */
SDL_Texture *CastTexture(SDL_Renderer *renderer, Raster &raster)
{
	if (raster.castTried)
		return raster.castTexture;
	raster.castTried = true;
	const int w = raster.width;
	const int h = raster.height;
	if (w <= 0 || h <= 0)
		return nullptr;
	const int width = w + CastPad * 2;
	const int height = h + CastPad * 2;
	std::vector<float> mask(static_cast<size_t>(width) * height, 0.F);
	int top = -1;
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			if (raster.alpha[static_cast<size_t>(y) * w + x] < 128)
				continue;
			mask[static_cast<size_t>(y + CastPad) * width + x + CastPad] = 1.F;
			if (top < 0)
				top = y;
		}
	}
	if (top < 0)
		return nullptr;
	raster.bodyTop = top;
	const std::vector<float> soft = Blur(mask, width, height, CastSoft);
	const int halfWidth = (width + 1) / 2;
	const int halfHeight = (height + 1) / 2;
	std::vector<uint8_t> rgba(static_cast<size_t>(halfWidth) * halfHeight * 4, 0);
	for (int y = 0; y < halfHeight; y++) {
		for (int x = 0; x < halfWidth; x++) {
			float sum = 0;
			int n = 0;
			for (int sy = y * 2; sy < std::min(height, y * 2 + 2); sy++) {
				for (int sx = x * 2; sx < std::min(width, x * 2 + 2); sx++) {
					sum += soft[static_cast<size_t>(sy) * width + sx];
					n++;
				}
			}
			rgba[(static_cast<size_t>(y) * halfWidth + x) * 4 + 3] = static_cast<uint8_t>(std::clamp(sum / std::max(n, 1) * 1.1F, 0.F, 1.F) * 255.F + 0.5F);
		}
	}
	raster.castTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, halfWidth, halfHeight);
	if (raster.castTexture == nullptr)
		return nullptr;
	SDL_SetTextureBlendMode(raster.castTexture, SDL_BLENDMODE_BLEND);
	SDL_SetTextureScaleMode(raster.castTexture, SDL_ScaleModeLinear);
	SDL_UpdateTexture(raster.castTexture, nullptr, rgba.data(), halfWidth * 4);
	return raster.castTexture;
}

/** @brief Makes the soft round dark spot (BlobTexture) if it isn't made yet. */
void MakeBlobTexture(SDL_Renderer *renderer)
{
	if (BlobTexture != nullptr)
		return;
	constexpr int Size = 64;
	std::vector<uint8_t> rgba(static_cast<size_t>(Size) * Size * 4, 0);
	for (int y = 0; y < Size; y++) {
		for (int x = 0; x < Size; x++) {
			const float dx = (static_cast<float>(x) + 0.5F) / (Size / 2.F) - 1.F;
			const float dy = (static_cast<float>(y) + 0.5F) / (Size / 2.F) - 1.F;
			const float fade = std::clamp(1.F - (dx * dx + dy * dy), 0.F, 1.F);
			rgba[(static_cast<size_t>(y) * Size + x) * 4 + 3] = static_cast<uint8_t>(fade * fade * 255.F + 0.5F);
		}
	}
	BlobTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, Size, Size);
	if (BlobTexture == nullptr)
		return;
	SDL_SetTextureBlendMode(BlobTexture, SDL_BLENDMODE_BLEND);
	SDL_SetTextureScaleMode(BlobTexture, SDL_ScaleModeLinear);
	SDL_UpdateTexture(BlobTexture, nullptr, rgba.data(), Size * 4);
}

/**
 * @brief The soft shadows a hero or monster casts on the floor: away from the strongest lights shining on it, each the
 * body's outline laid on the floor, turned to point away from its light, longer the lower the light, and stopped by the
 * first wall; and a soft dark spot round the feet from a light overhead.
 */
void DrawCasterShadows(SDL_Renderer *renderer, const Draw &draw, float toLogical)
{
	float gx;
	float gy;
	GroundOf(draw, gx, gy);
	float wx;
	float wy;
	WorldOf(gx, gy, wx, wy);
	// As the game hides a light inside a wall, a monster inside one doesn't give itself away by its shadow.
	const Point tile { static_cast<int>(std::floor(wx + 0.5F)), static_cast<int>(std::floor(wy + 0.5F)) };
	if (!InDungeonBounds(tile) || TileHasAny(tile, TileProperties::Solid))
		return;
	std::array<light::ShadowLight, 2> lights;
	float total = 0;
	float overhead = 0;
	const int count = light::ShadowLights(wx, wy, lights.data(), static_cast<int>(lights.size()), total, overhead);
	if (count == 0 && overhead <= 0)
		return;
	const float margin = static_cast<float>(TILE_WIDTH * 3 * Scale) * toLogical;
	if (!OnScreen(DestOf(draw, toLogical), margin))
		return;
	Raster &raster = FallbackRaster(draw, false);
	SDL_Texture *texture = CastTexture(renderer, raster);
	if (texture == nullptr)
		return;
	const float w = static_cast<float>(raster.width);
	const float h = static_cast<float>(raster.height);
	const float pad = static_cast<float>(CastPad);
	const float feet = h - 1 - HalfTileHeight;
	const float body = std::max(feet - static_cast<float>(raster.bodyTop), 1.F);

	const auto cast = [&](float dx, float dy, float stretch, float strength) {
		// The shadow ends at the first wall it meets.
		const float length = body * stretch / TileAcross;
		const float room = RoomFor(wx, wy, dx, dy, length);
		if (room < length)
			stretch *= room / length;
		if (stretch < 0.03F || strength < 0.02F)
			return;
		// Across the body: square to the shadow on the floor; along it: away from the light, per pixel of height.
		const float acrossX = (-dy - dx) * HalfTileWidth / TileAcross;
		const float acrossY = (dx - dy) * HalfTileHeight / TileAcross;
		const float alongX = (dx - dy) * HalfTileWidth * stretch / TileAcross;
		const float alongY = (dx + dy) * HalfTileHeight * stretch / TileAcross;
		static constexpr std::array<std::array<float, 2>, 4> Corners { { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } } };
		std::array<SDL_Vertex, 4> vertices;
		for (size_t i = 0; i < Corners.size(); i++) {
			const float px = Corners[i][0] * (w + pad * 2);
			const float py = Corners[i][1] * (h + pad * 2);
			const float side = px - pad - w / 2;
			const float up = feet - (py - pad);
			// Darkest at the feet, fading towards the head's end.
			const float fade = 0.35F + 0.65F * Corners[i][1];
			vertices[i] = SDL_Vertex {
				OnScreenAt(gx + side * acrossX + up * alongX, gy + side * acrossY + up * alongY, toLogical),
				{ 0, 0, 0, ColourByte(strength * fade) },
				{ Corners[i][0], Corners[i][1] },
			};
		}
		SDL_RenderGeometry(renderer, texture, vertices.data(), 4, QuadIndices.data(), static_cast<int>(QuadIndices.size()));
	};

	if (overhead > 0 && BlobTexture != nullptr) {
		// A light overhead leaves a soft dark spot on the floor round the feet.
		const Uint8 alpha = ColourByte(std::clamp(ContactDark * overhead / (total + 0.08F), 0.F, ContactDark));
		const SDL_FPoint centre = OnScreenAt(gx, gy + ContactDrop, toLogical);
		const float width = ContactWidth * Scale * toLogical;
		const SDL_FRect spot { centre.x - width / 2, centre.y - width / 4, width, width / 2 };
		SDL_SetTextureAlphaMod(BlobTexture, alpha);
		SDL_RenderCopyF(renderer, BlobTexture, nullptr, &spot);
	}
	for (int n = 0; n < count; n++) {
		const light::ShadowLight &source = lights[n];
		float dx = wx - source.x;
		float dy = wy - source.y;
		const float distance = std::hypot(dx, dy);
		if (distance < 1e-3F)
			continue;
		dx /= distance;
		dy /= distance;
		const float stretch = std::clamp(distance / std::max(source.height, 0.1F), 0.35F, 2.2F);
		// The shadow lacks this light and keeps the rest: it darkens the floor by this light's share of the light there.
		cast(dx, dy, stretch, std::clamp(source.strength / (total - overhead * (1.F - CarriedFill) + 0.08F), 0.F, CastDark));
	}
}

/** @brief The light on a wall's face, from the floor in front of it, taken along the face so walls in a row meet without a seam. */
Shading FaceLight(Point tile, bool right)
{
	const float tx = static_cast<float>(tile.x);
	const float ty = static_cast<float>(tile.y);
	if (right)
		return Gradient(LightAt(tx - 0.5F, ty - 0.25F), LightAt(tx + 0.5F, ty - 0.25F));
	return Gradient(LightAt(tx - 0.25F, ty + 0.5F), LightAt(tx - 0.25F, ty - 0.5F));
}

/**
 * @brief The light on a wall's face, and for walls lit from behind, the room's light behind it too.
 *
 * The face the view sees of a wall in front of a room is its outer side, which the room's light doesn't reach. The
 * game shows such walls as see-through ghosts of the room's own, or as the dungeon's outer side over solid rock, so
 * they take the room's light as well.
 */
Shading WallLight(Point tile, bool right)
{
	Shading shading = FaceLight(tile, right);
	if (!LitFromBehind(tile))
		return shading;
	const float tx = static_cast<float>(tile.x);
	const float ty = static_cast<float>(tile.y);
	const Shading behind = right ? Gradient(LightAt(tx - 0.5F, ty - 0.75F), LightAt(tx + 0.5F, ty - 0.75F))
	                             : Gradient(LightAt(tx - 0.75F, ty + 0.5F), LightAt(tx - 0.75F, ty - 0.5F));
	for (int s = 0; s < shading.count; s++) {
		for (int c = 0; c < 3; c++)
			shading.colours[s][c] = std::max(shading.colours[s][c], behind.colours[s][c]);
	}
	return shading;
}

/** @brief The light on something standing on an open tile: the tile's own light, across it from left to right. */
Shading OpenTileLight(Point tile, bool right)
{
	const float tx = static_cast<float>(tile.x);
	const float ty = static_cast<float>(tile.y);
	if (right)
		return Gradient(LightAt(tx, ty), LightAt(tx + 0.25F, ty - 0.25F));
	return Gradient(LightAt(tx - 0.25F, ty + 0.25F), LightAt(tx, ty));
}

/** @brief How a draw that stands on the floor is lit. */
Shading LightFor(const Draw &draw, const std::array<float, 3> &tint)
{
	if (draw.kind == DrawKind::Tile) {
		if (draw.light < 0)
			return Uniform(ToColour(light::Full()));
		if (!draw.hasTile) {
			float wx;
			float wy;
			WorldOf(static_cast<float>(draw.x) + static_cast<float>(draw.width) / 2, static_cast<float>(draw.y + draw.height - 1), wx, wy);
			return Uniform(LightAt(wx, wy));
		}
		if (light::Blocks(draw.cell.x, draw.cell.y))
			return WallLight(draw.cell, IsRightColumn(draw));
		return OpenTileLight(draw.cell, IsRightColumn(draw));
	}
	float wx;
	float wy;
	StandsAt(draw, wx, wy);
	switch (draw.caster) {
	case Caster::MyHero: {
		// The game always draws the hero being played fully lit; here they take the light's colour but stay bright.
		const light::Rgb at = light::At(wx, wy);
		const light::Rgb full = light::Full();
		return Uniform({ std::max(at.r, full.r * HeroLightFloor), std::max(at.g, full.g * HeroLightFloor), std::max(at.b, full.b * HeroLightFloor) });
	}
	case Caster::Hero:
		// Other heroes are drawn 5 light levels brighter than where they stand.
		return Uniform(draw.light >= 0 ? ToColour(light::Boost(light::At(wx, wy), 5)) : ToColour(light::Full()));
	case Caster::Monster:
		return Uniform(draw.light >= 0 ? LightAt(wx, wy) : ToColour(light::Full()));
	case Caster::MonsterRecoloured:
		return Uniform(LightAt(wx, wy));
	case Caster::MonsterUnlit:
		return Uniform(ToColour(light::Full()));
	case Caster::None:
		break;
	}
	if (draw.light < 0)
		return Uniform(ToColour(light::Full()));
	// Drawn without light (missiles, torch flames, strongboxes): it shines by itself.
	if (!draw.lightmapped && draw.tbl == nullptr)
		return Uniform(ToColour(light::Full()));
	// A see-through sprite drawn at a light level of its own (the dodge's fading after-images) keeps that fade.
	if (draw.style == SpriteStyle::Blended && draw.tbl != nullptr)
		return Uniform(LevelShade(draw.light, tint));
	if (draw.hasTile && IsArchPiece(draw)) {
		// An arch belongs to the walls: its left half to the wall facing +x, its right half to the one facing +y.
		const Shading left = WallLight(draw.cell, false);
		const Shading right = WallLight(draw.cell, true);
		Shading shading;
		shading.count = 3;
		shading.stops = { 0.F, 0.5F, 1.F };
		shading.colours[0] = left.colours[0];
		for (int c = 0; c < 3; c++)
			shading.colours[1][c] = (left.colours[1][c] + right.colours[0][c]) / 2;
		shading.colours[2] = right.colours[1];
		return shading;
	}
	if (draw.hasTile)
		return Uniform(LightAt(static_cast<float>(draw.cell.x), static_cast<float>(draw.cell.y)));
	return Uniform(LightAt(wx, wy));
}

/**
 * @brief Draws the recorded world with HD Lighting.
 *
 * The floor is drawn fully lit and darkened by the light over it. The floor in front of each wall follows, then
 * everything standing on the floor, back to front, each hero and monster just after the shadows it casts.
 * @return False where HD Lighting isn't used, and nothing was drawn.
 */
bool RenderLit(SDL_Renderer *renderer, float toLogical, const std::array<float, 3> &tint)
{
	// The world on screen, in tiles: the view's corners, with room for the walls that stand up into it from below.
	const float left = -static_cast<float>(TILE_WIDTH);
	const float right = static_cast<float>(ClipRect.size.width) / Scale + TILE_WIDTH;
	const float top = static_cast<float>(ClipRect.position.y) / Scale - TILE_HEIGHT * 2;
	const float bottom = static_cast<float>(ClipRect.position.y + ClipRect.size.height) / Scale + static_cast<float>((MicroTileLen / 2 + 1) * TILE_HEIGHT);
	float x0 = std::numeric_limits<float>::max();
	float y0 = std::numeric_limits<float>::max();
	float x1 = std::numeric_limits<float>::lowest();
	float y1 = std::numeric_limits<float>::lowest();
	for (const auto &[bx, by] : { std::pair { left, top }, std::pair { right, top }, std::pair { right, bottom }, std::pair { left, bottom } }) {
		float wx;
		float wy;
		WorldOf(bx, by, wx, wy);
		x0 = std::min(x0, wx);
		y0 = std::min(y0, wy);
		x1 = std::max(x1, wx);
		y1 = std::max(y1, wy);
	}
	// The hero's light follows where they're drawn, smoothly between the game's steps.
	float heroX = std::numeric_limits<float>::quiet_NaN();
	float heroY = std::numeric_limits<float>::quiet_NaN();
	for (size_t i = FloorEnd; i < Draws.size(); i++) {
		if (Draws[i].caster == Caster::MyHero && Draws[i].style == SpriteStyle::Normal) {
			StandsAt(Draws[i], heroX, heroY);
			break;
		}
	}
	const Mood &mood = CurrentMood();
	light::Look look;
	look.levelBrightness = LightLevel;
	look.falloff = mood.falloff;
	look.warm = mood.warm;
	look.cool = mood.cool;
	look.tint = tint;
	if (!light::Update(look, x0, y0, x1, y1, heroX, heroY))
		return false;
	FrameLit = true;
	MakeBlobTexture(renderer);

	const Shading unlit = Uniform({ 1.F, 1.F, 1.F });
	for (size_t i = 0; i < FloorEnd && i < Draws.size(); i++) {
		if (!IsOutline(Draws[i].style))
			DrawShaded(renderer, Draws[i], unlit, toLogical, true);
	}
	DrawFloorLight(renderer, toLogical);
	for (size_t i = FloorEnd; i < Draws.size(); i++) {
		if (IsTileBase(Draws[i]))
			DrawTileFloor(renderer, Draws[i], toLogical);
	}
	for (size_t i = FloorEnd; i < Draws.size(); i++) {
		const Draw &draw = Draws[i];
		if (IsOutline(draw.style))
			continue;
		if (draw.style == SpriteStyle::Normal && draw.caster != Caster::None)
			DrawCasterShadows(renderer, draw, toLogical);
		const Shading shading = LightFor(draw, tint);
		if (IsTileBase(draw)) {
			DrawTileWall(renderer, draw, shading, toLogical);
			continue;
		}
		// Heroes and monsters cast shadows of their own, in place of the one painted under them.
		DrawShaded(renderer, draw, shading, toLogical, draw.caster == Caster::None);
	}
	// Item and monster outlines glow on top of the light.
	const Shading glow = Uniform(tint);
	for (const Draw &draw : Draws) {
		if (IsOutline(draw.style))
			DrawShaded(renderer, draw, glow, toLogical, true);
	}
	return true;
}

#endif

} // namespace

const char *PackPalette(uint8_t levelType)
{
	switch (static_cast<dungeon_type>(levelType)) {
	case DTYPE_TOWN: return "levels\\towndata\\town.pal";
	case DTYPE_CATHEDRAL: return "levels\\l1data\\l1_1.pal";
	case DTYPE_CATACOMBS: return "levels\\l2data\\l2_1.pal";
	case DTYPE_CAVES: return "levels\\l3data\\l3_1.pal";
	case DTYPE_HELL: return "levels\\l4data\\l4_1.pal";
	case DTYPE_NEST: return "nlevels\\l6data\\l6base1.pal";
	case DTYPE_CRYPT: return "nlevels\\l5data\\l5base.pal";
	default: return nullptr;
	}
}

bool Enabled()
{
	return gbIsPoe2 && !HeadlessMode && *GetOptions().Graphics.hdGraphics && HavePack();
}

bool Active()
{
	return Enabled() && *GetOptions().Graphics.zoom;
}

void RecordTileImpl(const Surface &out, const Lightmap &lightmap, Point position, TileType tile, const uint8_t *src, int height, MaskType mask, const uint8_t *tbl);
void RecordSpriteImpl(const Surface &out, Point position, ClxSprite clx, const uint8_t *trn, SpriteStyle style, uint8_t outlineColor, const Lightmap *lightmap);

void BeginWorld(const Surface &out, Rectangle viewport)
{
	Draws.clear();
	LightArena.clear();
	ViewportOffsetX = viewport.position.x;
	ClipRect = viewport;
	RecordRegion = Point { out.region.x, out.region.y };
	HaveViewOrigin = false;
	FloorDone = false;
	FloorEnd = 0;
	RecordingLit = LightingWanted();
	if (CacheCels != pDungeonCels.get() || CacheLevelType != leveltype)
		IndexTiles();
	RecordTileHook = RecordTileImpl;
	RecordSpriteHook = RecordSpriteImpl;
	Recording = true;
	WorldRecorded = true;
}

void EndWorld()
{
	Recording = false;
}

void SetViewOrigin(Point tile, Point bufferPosition)
{
	if (!Recording)
		return;
	ViewTile = tile;
	// A tile's centre is half a tile right of and up from where it's drawn (light_render.cpp's centre0).
	ViewCentreX = static_cast<float>(RecordRegion.x + bufferPosition.x + TILE_WIDTH / 2);
	ViewCentreY = static_cast<float>(RecordRegion.y + bufferPosition.y - TILE_HEIGHT / 2);
	HaveViewOrigin = true;
}

void EndFloor()
{
	if (!Recording)
		return;
	FloorEnd = Draws.size();
	FloorDone = true;
}

void RecordTileImpl(const Surface &out, const Lightmap &lightmap, Point position, TileType tile, const uint8_t *src, int height, MaskType mask, const uint8_t *tbl)
{
	// HD Lighting works out its own light, so the game's per-pixel light needn't be copied.
	const bool perPixel = *GetOptions().Graphics.perPixelLighting && !RecordingLit;
	Draws.push_back(Draw {
	    .kind = DrawKind::Tile,
	    .style = SpriteStyle::Normal,
	    .tile = tile,
	    .mask = mask,
	    .outlineColor = 0,
	    .x = out.region.x + position.x,
	    .y = out.region.y + position.y - height + 1,
	    .width = DunFrameWidth,
	    .height = height,
	    .sprite = std::nullopt,
	    .src = src,
	    .tbl = tbl,
	    .light = static_cast<int8_t>(perPixel ? 0 : LightIndexOf(tbl)),
	    .perPixel = perPixel,
	    .lightOffset = perPixel ? CaptureLight(out, lightmap, { position.x, position.y - height + 1 }, DunFrameWidth, height, tbl) : 0,
	    .inBetween = 0,
	    .caster = Caster::None,
	    .floor = !FloorDone,
	    .hasTile = HaveTile,
	    .cell = CurrentTile,
	    .tileOrigin = CurrentTileOrigin + Displacement { RecordRegion.x, RecordRegion.y },
	    .lightmapped = false,
	});
}

void RecordSpriteImpl(const Surface &out, Point position, ClxSprite clx, const uint8_t *trn, SpriteStyle style, uint8_t outlineColor, const Lightmap *lightmap)
{
	const bool perPixel = lightmap != nullptr && *GetOptions().Graphics.perPixelLighting && !RecordingLit;
	const int pad = IsOutline(style) ? 1 : 0;
	Draws.push_back(Draw {
	    .kind = DrawKind::Sprite,
	    .style = style,
	    .tile = TileType::Square,
	    .mask = MaskType::Solid,
	    .outlineColor = outlineColor,
	    .x = out.region.x + position.x - pad,
	    .y = out.region.y + position.y - clx.height() + 1 - pad,
	    .width = clx.width() + pad * 2,
	    .height = clx.height() + pad * 2,
	    .sprite = clx,
	    .src = nullptr,
	    .tbl = trn,
	    .light = static_cast<int8_t>(perPixel ? 0 : LightIndexOf(trn)),
	    .perPixel = perPixel,
	    .lightOffset = perPixel ? CaptureLight(out, *lightmap, { position.x, position.y - clx.height() + 1 }, clx.width(), clx.height(), trn) : 0,
	    // Two in-betweens per frame split it in thirds.
	    .inBetween = static_cast<uint8_t>(std::clamp(static_cast<int>(SubFrame * 3), 0, 2)),
	    .caster = IsOutline(style) ? Caster::None : CurrentCaster,
	    .floor = !FloorDone,
	    .hasTile = HaveTile,
	    .cell = CurrentTile,
	    .tileOrigin = CurrentTileOrigin + Displacement { RecordRegion.x, RecordRegion.y },
	    .lightmapped = lightmap != nullptr,
	});
}

void RegisterSprites(std::string_view path, ClxSpriteListOrSheet sprites)
{
	// A newly loaded sprite can sit where one with another name was.
	ArtChoices.clear();
	std::string base(path);
	std::replace(base.begin(), base.end(), '\\', '/');
	std::transform(base.begin(), base.end(), base.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	for (const std::string_view ext : { ".cl2", ".clx", ".cel" }) {
		if (base.size() > ext.size() && base.ends_with(ext))
			base.resize(base.size() - ext.size());
	}
	const auto registerList = [&](ClxSpriteList list, size_t direction) {
		for (size_t frame = 0; frame < list.numSprites(); frame++) {
			char name[32];
			std::snprintf(name, sizeof(name), "/d%zu_f%02zu", direction, frame);
			SpriteNames[list[frame].pixelData()] = base + name;
		}
	};
	if (sprites.isSheet()) {
		const ClxSpriteSheet sheet = sprites.sheet();
		for (size_t direction = 0; direction < sheet.numLists(); direction++)
			registerList(sheet[direction], direction);
	} else {
		registerList(sprites.list(), 0);
	}
}

bool ExportLevelTiles(const std::string &dir)
{
	if (pDungeonCels == nullptr)
		return false;
	IndexTiles();
	const std::string levelDir = dir + "/tiles/" + std::string(LevelTypeName());
	RecursivelyCreateDir(levelDir.c_str());

	struct Part {
		std::string name;
		int x;
		int y;
	};
	struct FrameDraw {
		TileType tile;
		const uint8_t *src;
		int height;
	};
	std::unordered_map<std::string, FrameDraw> frames;
	std::unordered_map<uint16_t, DunFrameInfo> firstUse;
	std::string json = "{\"pieces\":[";
	bool firstPiece = true;
	const int blocks = MicroTileLen;
	const int columnHeight = (blocks / 2) * TILE_HEIGHT;
	for (size_t piece = 0; piece < MAXTILES; piece++) {
		std::vector<Part> parts;
		for (int block = 0; block < blocks; block++) {
			const LevelCelBlock cel = DPieceMicros[piece].mt[block];
			if (!cel.hasValue())
				continue;
			const TileType type = cel.type();
			const DunFrameInfo info = firstUse.try_emplace(cel.frame(), DunFrameInfo { static_cast<uint8_t>(block), type, SOLData[piece] }).first->second;
			const uint8_t *data = GetDunFrame(pDungeonCels.get(), cel.frame());
			const int x = (block & 1) * DunFrameWidth;
			const int bottom = columnHeight - 1 - (block / 2) * TILE_HEIGHT;
			const auto add = [&](const uint8_t *src, TileType tile, int height, int frameBottom) {
				const auto it = TileNames.find(src);
				if (it == TileNames.end())
					return;
				frames.try_emplace(it->second, FrameDraw { tile, src, height });
				parts.push_back(Part { it->second, x, frameBottom - height + 1 });
			};
			if (type == TileType::TransparentSquare && info.isFloor()) {
				add(data, info.isFloorLeft() ? TileType::LeftTriangle : TileType::RightTriangle, DunFrameTriangleHeight, bottom);
				add(data + ReencodedTriangleFrameSize, TileType::TransparentSquare, 16, bottom - 16);
			} else if (IsAnyOf(type, TileType::LeftTriangle, TileType::RightTriangle)) {
				add(data, type, DunFrameTriangleHeight, bottom);
			} else {
				add(data, type, DunFrameHeight, bottom);
			}
		}
		if (parts.empty())
			continue;
		json += firstPiece ? "\n" : ",\n";
		firstPiece = false;
		json += "{\"piece\":" + std::to_string(piece) + ",\"width\":" + std::to_string(TILE_WIDTH) + ",\"height\":" + std::to_string(columnHeight) + ",\"parts\":[";
		for (size_t i = 0; i < parts.size(); i++) {
			if (i != 0)
				json += ",";
			json += "[\"" + parts[i].name + "\"," + std::to_string(parts[i].x) + "," + std::to_string(parts[i].y) + "]";
		}
		json += "]}";
	}
	json += "\n]}\n";

	for (const auto &[name, frame] : frames) {
		Raster raster;
		Rasterize(raster, DunFrameWidth, frame.height, [&](const Surface &out) {
			RenderTileFrame(out, FullyLitLightmap(out), Point { 0, frame.height - 1 }, frame.tile, frame.src, frame.height, MaskType::Solid, FullyLitLightTable);
		}, [&](const Surface &out) {
			RenderTileFrame(out, FullyLitLightmap(out), Point { 0, frame.height - 1 }, frame.tile, frame.src, frame.height, MaskType::Solid, FullyLitLightTable);
		});
		SDLSurfaceUniquePtr surface = SDLWrap::CreateRGBSurfaceWithFormat(0, raster.width, raster.height, 32, SDL_PIXELFORMAT_RGBA32);
		auto *pixels = static_cast<uint8_t *>(surface->pixels);
		for (int y = 0; y < raster.height; y++) {
			for (int x = 0; x < raster.width; x++) {
				const size_t i = static_cast<size_t>(y) * raster.width + x;
				const SDL_Color &color = logical_palette[raster.index[i]];
				uint8_t *dst = pixels + static_cast<ptrdiff_t>(y) * surface->pitch + x * 4;
				dst[0] = color.r;
				dst[1] = color.g;
				dst[2] = color.b;
				dst[3] = raster.alpha[i];
			}
		}
		const std::string file = levelDir + "/" + name.substr(name.rfind('/') + 1) + ".png";
		IMG_SavePNG(surface.get(), file.c_str());
	}
	if (FILE *f = std::fopen((levelDir + "/pieces.json").c_str(), "wb"); f != nullptr) {
		std::fwrite(json.data(), 1, json.size(), f);
		std::fclose(f);
	}
	return true;
}

void ClearCaches()
{
	DestroyTextures();
	CacheRenderer = nullptr;
	PackState = -1;
}

void Render(SDL_Renderer *renderer, Size worldSize, Size logicalSize)
{
	if (renderer == nullptr || worldSize.width <= 0 || logicalSize.width <= 0)
		return;
	if (CacheRenderer != renderer) {
		// The old renderer took its textures with it.
		Art.clear();
		Rasters.clear();
		ArtChoices.clear();
		LightTexture = nullptr;
		FieldTexture = nullptr;
		BlobTexture = nullptr;
		CacheRenderer = renderer;
	}
	CheckPalette();
	if (Rasters.size() > 20000) {
		for (auto &[key, raster] : Rasters)
			FreeRasterTextures(raster);
		Rasters.clear();
	}

	const float toLogical = static_cast<float>(logicalSize.width) / static_cast<float>(worldSize.width);
	const std::array<float, 3> tint = ScreenTint();
	const SDL_Rect clip {
		static_cast<int>(ClipRect.position.x * toLogical),
		static_cast<int>(ClipRect.position.y * toLogical),
		static_cast<int>(ClipRect.size.width * toLogical) + 1,
		static_cast<int>(ClipRect.size.height * toLogical) + 1,
	};
	SDL_RenderSetClipRect(renderer, &clip);
	// The game's own world is underneath; the HD one replaces it entirely.
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderFillRect(renderer, &clip);
	ViewOnScreen = SDL_FRect { static_cast<float>(clip.x), static_cast<float>(clip.y), static_cast<float>(clip.w), static_cast<float>(clip.h) };

	FrameLit = false;
#if SDL_VERSION_ATLEAST(2, 0, 18)
	// HD Lighting works out its own light, in colour and stopped by walls.
	if (RecordingLit && HaveViewOrigin && FloorDone && RenderLit(renderer, toLogical, tint)) {
		SDL_RenderSetClipRect(renderer, nullptr);
		WorldRecorded = false;
		return;
	}
#endif

	// Everything is drawn fully lit, then darkened by the light of each pixel, so lighting stays as smooth as the game's.
	const Shading unlit = Uniform({ 1.F, 1.F, 1.F });
	for (const Draw &draw : Draws) {
		if (!IsOutline(draw.style))
			DrawShaded(renderer, draw, unlit, toLogical, true);
	}

	const Size lightSize { std::min(worldSize.width, worldSize.width / Scale + 2), std::min(worldSize.height, worldSize.height / Scale + 2) };
	BuildLightBuffer(lightSize);
	if (LightTexture == nullptr || LightTextureSize != lightSize) {
		if (LightTexture != nullptr)
			SDL_DestroyTexture(LightTexture);
		LightTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, lightSize.width, lightSize.height);
		LightTextureSize = lightSize;
		if (LightTexture != nullptr) {
			SDL_SetTextureBlendMode(LightTexture, SDL_BLENDMODE_MOD);
#if SDL_VERSION_ATLEAST(2, 0, 12)
			SDL_SetTextureScaleMode(LightTexture, SDL_ScaleModeLinear);
#endif
		}
	}
	if (LightTexture != nullptr) {
		// Each light level's colour is worked out once; every pixel then just looks its level up, straight into the
		// texture (most of an HD frame's time went into working it out again for every pixel).
		std::array<uint32_t, 256> shades;
		for (size_t level = 0; level < NumLightingLevels; level++) {
			const std::array<float, 3> shade = LevelShade(static_cast<int>(level), tint);
			uint32_t pixel = 0xFF000000U;
			for (int c = 0; c < 3; c++)
				pixel |= static_cast<uint32_t>(std::clamp(255.F * shade[c], 0.F, 255.F)) << (16 - c * 8);
			shades[level] = pixel;
		}
		std::fill(shades.begin() + NumLightingLevels, shades.end(), shades[NumLightingLevels - 1]);
		void *pixels;
		int pitch;
		if (SDL_LockTexture(LightTexture, nullptr, &pixels, &pitch) == 0) {
			for (int y = 0; y < lightSize.height; y++) {
				const uint8_t *levels = LightBuffer.data() + static_cast<size_t>(y) * lightSize.width;
				auto *row = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(pixels) + static_cast<ptrdiff_t>(y) * pitch);
				for (int x = 0; x < lightSize.width; x++)
					row[x] = shades[levels[x]];
			}
			SDL_UnlockTexture(LightTexture);
		}
		const SDL_FRect dest {
			ViewportOffsetX * toLogical,
			0,
			static_cast<float>(lightSize.width) * Scale * toLogical,
			static_cast<float>(lightSize.height) * Scale * toLogical,
		};
		SDL_RenderCopyF(renderer, LightTexture, nullptr, &dest);
	}

	// Item and monster outlines glow on top of the light.
	const Shading glow = Uniform(tint);
	for (const Draw &draw : Draws) {
		if (IsOutline(draw.style))
			DrawShaded(renderer, draw, glow, toLogical, true);
	}
	SDL_RenderSetClipRect(renderer, nullptr);
	WorldRecorded = false;
}

bool CoversView(Size worldSize)
{
	if (!WorldRecorded || worldSize.width <= 0 || worldSize.height <= 0)
		return false;
	return ClipRect.position.x <= 0 && ClipRect.position.y <= 0 && ClipRect.position.x + ClipRect.size.width >= worldSize.width
	    && ClipRect.position.y + ClipRect.size.height >= worldSize.height;
}

#else

bool Enabled() { return false; }
bool Active() { return false; }
void BeginWorld(const Surface &, Rectangle) { }
void EndWorld() { }
void SetViewOrigin(Point, Point) { }
void EndFloor() { }
void RegisterSprites(std::string_view, ClxSpriteListOrSheet) { }
void ClearCaches() { }
void Render(SDL_Renderer *, Size, Size) { }
bool CoversView(Size) { return false; }
bool ExportLevelTiles(const std::string &) { return false; }
const char *PackPalette(uint8_t) { return nullptr; }

#endif

} // namespace hd

} // namespace devilution
