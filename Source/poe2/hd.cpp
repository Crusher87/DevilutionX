#include "poe2/hd.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
#include "utils/is_of.hpp"
#include "utils/png.h"
#include "utils/sdl_wrap.h"

namespace devilution {

namespace hd {

float SubFrame = 0;

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
};

void FreeRasterTextures(Raster &raster)
{
	for (SDL_Texture *texture : { raster.texture, raster.shadowTexture, raster.packTexture }) {
		if (texture != nullptr)
			SDL_DestroyTexture(texture);
	}
	raster.texture = nullptr;
	raster.shadowTexture = nullptr;
	raster.packTexture = nullptr;
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

// The HD look. Light falls off faster than the game's own (an exponent on its light levels), and bright light
// leans warm like torchlight while shadow leans cool.
constexpr float LightFalloff = 1.15F;
constexpr std::array<float, 3> WarmLight { 1.05F, 0.97F, 0.86F };
constexpr std::array<float, 3> CoolShadow { 0.86F, 0.91F, 1.05F };
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

SDL_Texture *LoadArt(SDL_Renderer *renderer, const std::string &name)
{
	auto [it, inserted] = Art.try_emplace(name);
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
	// A Gaussian blur, along the rows and then down the columns.
	constexpr int Radius = static_cast<int>(ShadowSoft * 3 + 0.5F);
	std::array<float, Radius * 2 + 1> kernel;
	float total = 0;
	for (int k = -Radius; k <= Radius; k++) {
		kernel[k + Radius] = std::exp(-static_cast<float>(k * k) / (2 * ShadowSoft * ShadowSoft));
		total += kernel[k + Radius];
	}
	for (float &weight : kernel)
		weight /= total;
	std::vector<float> rows(mask.size(), 0.F);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			float sum = 0;
			for (int k = std::max(-Radius, -x); k <= std::min(Radius, width - 1 - x); k++)
				sum += mask[static_cast<size_t>(y) * width + x + k] * kernel[k + Radius];
			rows[static_cast<size_t>(y) * width + x] = sum;
		}
	}
	std::vector<uint8_t> rgba(mask.size() * 4, 0);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			float sum = 0;
			for (int k = std::max(-Radius, -y); k <= std::min(Radius, height - 1 - y); k++)
				sum += rows[static_cast<size_t>(y + k) * width + x] * kernel[k + Radius];
			rgba[(static_cast<size_t>(y) * width + x) * 4 + 3] = static_cast<uint8_t>(std::clamp(sum * 1.15F, 0.F, 1.F) * ShadowDark * 255.F + 0.5F);
		}
	}
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
	if (draw.inBetween != 0 && !masked)
		texture = LoadArt(renderer, *name + "_m" + std::to_string(draw.inBetween));
	// Tiles recoloured for this level's palette, e.g. tiles/l1_p3/120, beat evening out the base ones.
	if (texture == nullptr && draw.kind == DrawKind::Tile && PaletteVariant > 1) {
		const size_t slash = name->find('/', 6);
		if (slash != std::string::npos) {
			texture = MaskedArt(renderer, name->substr(0, slash) + "_p" + std::to_string(PaletteVariant) + name->substr(slash), draw);
			recoloured = texture != nullptr;
		}
	}
	if (texture == nullptr)
		texture = draw.kind == DrawKind::Tile ? MaskedArt(renderer, *name, draw) : LoadArt(renderer, *name);
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
	    | (static_cast<uint64_t>(draw.height & 0xFFFF) << 48);
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
	(void)out;
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

void RecordTileImpl(const Surface &out, const Lightmap &lightmap, Point position, TileType tile, const uint8_t *src, int height, MaskType mask, const uint8_t *tbl)
{
	const bool perPixel = *GetOptions().Graphics.perPixelLighting;
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
	});
}

void RecordSpriteImpl(const Surface &out, Point position, ClxSprite clx, const uint8_t *trn, SpriteStyle style, uint8_t outlineColor, const Lightmap *lightmap)
{
	const bool perPixel = lightmap != nullptr && *GetOptions().Graphics.perPixelLighting;
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
		Art.clear();
		Rasters.clear();
		ArtChoices.clear();
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

	const auto drawOne = [&](const Draw &draw, const std::array<float, 3> &colour) {
		ArtChoice art;
		if (draw.light >= 0)
			art = CachedArt(renderer, draw);
		SDL_Texture *texture = art.texture;
		const bool hdArt = art.hdArt;
		bool tileColours = art.tileArt;
		SDL_Texture *shadow = nullptr;
		// Recolours other than light (stone curse, infravision, unique monsters) are drawn by the game itself.
		if (texture == nullptr) {
			Raster &raster = FallbackRaster(draw, draw.light < 0);
			shadow = ShadowTexture(renderer, raster);
			if (ArchesTakeTileColours() && IsArchPiece(draw)) {
				texture = PackColourTexture(renderer, raster);
				tileColours = texture != nullptr;
			}
			if (texture == nullptr)
				texture = RasterTexture(renderer, raster);
		}
		if (texture == nullptr)
			return;
		if (shadow != nullptr) {
			const SDL_FRect around {
				(ViewportOffsetX + static_cast<float>(draw.x - ShadowPad) * Scale) * toLogical,
				static_cast<float>(draw.y - ShadowPad) * Scale * toLogical,
				static_cast<float>(draw.width + ShadowPad * 2) * Scale * toLogical,
				static_cast<float>(draw.height + ShadowPad * 2) * Scale * toLogical,
			};
			SDL_RenderCopyF(renderer, shadow, nullptr, &around);
		}
		std::array<float, 3> mod = colour;
		if (tileColours) {
			for (int c = 0; c < 3; c++)
				mod[c] *= TileGain[c];
		}
		SDL_SetTextureColorMod(texture,
		    static_cast<Uint8>(std::clamp(255.F * mod[0], 0.F, 255.F)),
		    static_cast<Uint8>(std::clamp(255.F * mod[1], 0.F, 255.F)),
		    static_cast<Uint8>(std::clamp(255.F * mod[2], 0.F, 255.F)));
		// The game's own see-through pixels are already in the fallback's coverage.
		SDL_SetTextureAlphaMod(texture, hdArt && draw.style == SpriteStyle::Blended ? 128 : 255);
		const SDL_FRect dest {
			(ViewportOffsetX + static_cast<float>(draw.x) * Scale) * toLogical,
			static_cast<float>(draw.y) * Scale * toLogical,
			static_cast<float>(draw.width) * Scale * toLogical,
			static_cast<float>(draw.height) * Scale * toLogical,
		};
		SDL_RenderCopyF(renderer, texture, nullptr, &dest);
	};

	// Everything is drawn fully lit, then darkened by the light of each pixel, so lighting stays as smooth as the game's.
	const std::array<float, 3> unlit { 1.F, 1.F, 1.F };
	for (const Draw &draw : Draws) {
		if (!IsOutline(draw.style))
			drawOne(draw, unlit);
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
			const float warmth = std::clamp(1.F - static_cast<float>(level) / 10.F, 0.F, 1.F);
			uint32_t pixel = 0xFF000000U;
			for (int c = 0; c < 3; c++) {
				const float grade = CoolShadow[c] + (WarmLight[c] - CoolShadow[c]) * warmth;
				const float shade = 255.F * std::pow(LightLevel[level], LightFalloff) * grade * tint[c];
				pixel |= static_cast<uint32_t>(std::clamp(shade, 0.F, 255.F)) << (16 - c * 8);
			}
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
	for (const Draw &draw : Draws) {
		if (IsOutline(draw.style))
			drawOne(draw, tint);
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
void RegisterSprites(std::string_view, ClxSpriteListOrSheet) { }
void ClearCaches() { }
void Render(SDL_Renderer *, Size, Size) { }
bool CoversView(Size) { return false; }
bool ExportLevelTiles(const std::string &) { return false; }
const char *PackPalette(uint8_t) { return nullptr; }

#endif

} // namespace hd

} // namespace devilution
