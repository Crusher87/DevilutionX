#include "poe2/hd.hpp"

#include <algorithm>
#include <array>
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
#include "levels/gendung.h"
#include "levels/reencode_dun_cels.hpp"
#include "utils/file_util.h"
#include "options.h"
#include "utils/is_of.hpp"
#include "utils/png.h"
#include "utils/sdl_wrap.h"

namespace devilution {

namespace hd {

bool Recording = false;
float SubFrame = 0;

#if !defined(USE_SDL1) && !defined(USE_SDL3)

namespace {

/** @brief The HD art is twice the size of the game's own pixels. */
constexpr int Scale = 2;

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
};

struct HdArt {
	SDL_Texture *texture = nullptr;
	bool missing = false;
};

std::vector<Draw> Draws;
/** @brief The per-pixel light levels of this frame's draws, copied as they are drawn. */
std::vector<uint8_t> LightArena;
/** @brief The light level of each pixel of the 8-bit world, from the draw that covers it. */
std::vector<uint8_t> LightBuffer;
std::vector<uint32_t> LightPixels;
SDL_Texture *LightTexture = nullptr;
Size LightTextureSize;
int ViewportOffsetX = 0;
Rectangle ClipRect;

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

std::array<float, NumLightingLevels> LightLevel;
uint32_t PaletteVersion = 1;
std::array<SDL_Color, 256> LastPalette;

int PackState = -1;

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
		for (auto &[key, raster] : Rasters) {
			if (raster.texture != nullptr)
				SDL_DestroyTexture(raster.texture);
		}
	}
	Art.clear();
	Rasters.clear();
	if (LightTexture != nullptr && CacheRenderer != nullptr)
		SDL_DestroyTexture(LightTexture);
	LightTexture = nullptr;
}

std::string_view LevelTypeName()
{
	switch (leveltype) {
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

/** @brief Names every dungeon frame of the current level, so a drawn frame can find its HD art. */
void IndexTiles()
{
	TileNames.clear();
	CacheCels = pDungeonCels.get();
	CacheLevelType = leveltype;
	if (CacheCels == nullptr)
		return;
	const auto *table = reinterpret_cast<const uint32_t *>(CacheCels);
	const uint32_t count = Swap32LE(table[0]);
	const std::string prefix = std::string("tiles/") + std::string(LevelTypeName()) + "/";
	for (uint32_t frame = 1; frame <= count; frame++) {
		const uint8_t *data = GetDunFrame(CacheCels, frame);
		TileNames[data] = prefix + std::to_string(frame);
		TileNames[data + ReencodedTriangleFrameSize] = prefix + std::to_string(frame) + "f";
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

SDL_Texture *RasterTexture(SDL_Renderer *renderer, Raster &raster)
{
	if (raster.width <= 0 || raster.height <= 0)
		return nullptr;
	if (raster.texture != nullptr && (!raster.cycles || raster.paletteVersion == PaletteVersion))
		return raster.texture;
	if (raster.texture == nullptr) {
		raster.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, raster.width, raster.height);
		if (raster.texture == nullptr)
			return nullptr;
		SDL_SetTextureBlendMode(raster.texture, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
		SDL_SetTextureScaleMode(raster.texture, SDL_ScaleModeNearest);
#endif
	}
	std::vector<uint8_t> rgba(static_cast<size_t>(raster.width) * raster.height * 4);
	for (size_t i = 0; i < raster.index.size(); i++) {
		const SDL_Color &color = logical_palette[raster.index[i]];
		rgba[i * 4 + 0] = color.r;
		rgba[i * 4 + 1] = color.g;
		rgba[i * 4 + 2] = color.b;
		rgba[i * 4 + 3] = raster.alpha[i];
	}
	SDL_UpdateTexture(raster.texture, nullptr, rgba.data(), raster.width * 4);
	raster.paletteVersion = PaletteVersion;
	return raster.texture;
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
		const int pad = draw.style == SpriteStyle::Outline ? 1 : 0;
		const Point position { pad, draw.height - 1 - pad };
		const auto drawSprite = [&](const Surface &out, bool blended) {
			if (draw.style == SpriteStyle::Outline)
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
	}
	return it->second;
}

/** @brief A wall tile's HD art cut the way the game cuts it near the hero (see-through or half cut away). */
SDL_Texture *MaskedArt(SDL_Renderer *renderer, const std::string &name, const Draw &draw)
{
	const std::string key = name + "#" + std::to_string(static_cast<int>(draw.mask));
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
			SDL_SetTextureScaleMode(art.texture, SDL_ScaleModeLinear);
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
	if (draw.style == SpriteStyle::Outline)
		return nullptr;
	const auto it = SpriteNames.find(draw.sprite->pixelData());
	return it != SpriteNames.end() ? &it->second : nullptr;
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

void CheckPalette()
{
	if (std::memcmp(LastPalette.data(), logical_palette.data(), sizeof(LastPalette)) != 0) {
		std::memcpy(LastPalette.data(), logical_palette.data(), sizeof(LastPalette));
		PaletteVersion++;
		UpdateLightLevels();
	}
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
		const int uniform = std::max<int>(draw.light, 0);
		for (int row = 0; row < raster.height; row++) {
			const int y = draw.y + row;
			if (y < 0 || y >= size.height)
				continue;
			for (int col = 0; col < raster.width; col++) {
				const int x = draw.x + col;
				if (x < 0 || x >= size.width)
					continue;
				const size_t i = static_cast<size_t>(row) * raster.width + col;
				if (raster.alpha[i] != 255)
					continue;
				LightBuffer[static_cast<size_t>(y) * size.width + x] = draw.perPixel ? LightArena[draw.lightOffset + i] : static_cast<uint8_t>(uniform);
			}
		}
	}
}

} // namespace

bool Enabled()
{
	return gbIsPoe2 && !HeadlessMode && *GetOptions().Graphics.hdGraphics && HavePack();
}

bool Active()
{
	return Enabled() && *GetOptions().Graphics.zoom;
}

void BeginWorld(const Surface &out, Rectangle viewport)
{
	Draws.clear();
	LightArena.clear();
	ViewportOffsetX = viewport.position.x;
	ClipRect = viewport;
	(void)out;
	if (CacheCels != pDungeonCels.get() || CacheLevelType != leveltype)
		IndexTiles();
	Recording = true;
}

void EndWorld()
{
	Recording = false;
}

void RecordTile(const Surface &out, const Lightmap &lightmap, Point position, TileType tile, const uint8_t *src, int height, MaskType mask, const uint8_t *tbl)
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

void RecordSprite(const Surface &out, Point position, ClxSprite clx, const uint8_t *trn, SpriteStyle style, uint8_t outlineColor, const Lightmap *lightmap)
{
	const bool perPixel = lightmap != nullptr && *GetOptions().Graphics.perPixelLighting;
	const int pad = style == SpriteStyle::Outline ? 1 : 0;
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
		CacheRenderer = renderer;
	}
	CheckPalette();
	if (Rasters.size() > 20000) {
		for (auto &[key, raster] : Rasters) {
			if (raster.texture != nullptr)
				SDL_DestroyTexture(raster.texture);
		}
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
		SDL_Texture *texture = nullptr;
		bool hdArt = false;
		// Wall tiles cut away near the hero keep the game's own cut-out shape.
		const bool masked = draw.kind == DrawKind::Tile && draw.mask != MaskType::Solid;
		if (draw.light >= 0) {
			if (const std::string *name = ArtName(draw); name != nullptr) {
				if (draw.inBetween != 0 && !masked)
					texture = LoadArt(renderer, *name + "_m" + std::to_string(draw.inBetween));
				if (texture == nullptr)
					texture = masked ? MaskedArt(renderer, *name, draw) : LoadArt(renderer, *name);
				hdArt = texture != nullptr && !masked;
			}
		}
		// Recolours other than light (stone curse, infravision, unique monsters) are drawn by the game itself.
		if (texture == nullptr)
			texture = RasterTexture(renderer, FallbackRaster(draw, draw.light < 0));
		if (texture == nullptr)
			return;
		SDL_SetTextureColorMod(texture,
		    static_cast<Uint8>(std::clamp(255.F * colour[0], 0.F, 255.F)),
		    static_cast<Uint8>(std::clamp(255.F * colour[1], 0.F, 255.F)),
		    static_cast<Uint8>(std::clamp(255.F * colour[2], 0.F, 255.F)));
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
		if (draw.style != SpriteStyle::Outline)
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
		std::array<uint32_t, NumLightingLevels> shades;
		for (size_t level = 0; level < NumLightingLevels; level++) {
			uint32_t pixel = 0xFF000000U;
			for (int c = 0; c < 3; c++)
				pixel |= static_cast<uint32_t>(std::clamp(255.F * LightLevel[level] * tint[c], 0.F, 255.F)) << (16 - c * 8);
			shades[level] = pixel;
		}
		LightPixels.resize(LightBuffer.size());
		for (size_t i = 0; i < LightBuffer.size(); i++)
			LightPixels[i] = shades[std::min<size_t>(LightBuffer[i], NumLightingLevels - 1)];
		SDL_UpdateTexture(LightTexture, nullptr, LightPixels.data(), lightSize.width * 4);
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
		if (draw.style == SpriteStyle::Outline)
			drawOne(draw, tint);
	}
	SDL_RenderSetClipRect(renderer, nullptr);
}

#else

bool Enabled() { return false; }
bool Active() { return false; }
void BeginWorld(const Surface &, Rectangle) { }
void EndWorld() { }
void RecordTile(const Surface &, const Lightmap &, Point, TileType, const uint8_t *, int, MaskType, const uint8_t *) { }
void RecordSprite(const Surface &, Point, ClxSprite, const uint8_t *, SpriteStyle, uint8_t, const Lightmap *) { }
void RegisterSprites(std::string_view, ClxSpriteListOrSheet) { }
void ClearCaches() { }
void Render(SDL_Renderer *, Size, Size) { }
bool ExportLevelTiles(const std::string &) { return false; }

#endif

} // namespace hd

} // namespace devilution
