#include "poe2/hd.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
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
#include "levels/gendung.h"
#include "levels/reencode_dun_cels.hpp"
#include "levels/tileset_mix.hpp"
#include "options.h"
#include "poe2/atmosphere.hpp"
#include "poe2/hd_light.hpp"
#include "poe2/hd_outline.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/vfx.hpp"
#include "utils/file_util.h"
#include "utils/is_of.hpp"
#include "utils/paths.h"
#include "utils/png.h"
#include "utils/sdl_mutex.h"
#include "utils/sdl_thread.h"
#include "utils/sdl_wrap.h"

namespace devilution {

namespace hd {

float SubFrame = 0;
bool SubFrameBackward = false;
Caster CurrentCaster = Caster::None;
OutlineLook CurrentOutlineLook;
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
	/** @brief The game drew an outline round it just before it (its smooth outline is made with its HD art). */
	bool outlined = false;
	/** @brief For an outline: how it shows in HD. */
	OutlineLook outlineLook {};
	/** @brief A unique monster's colour table it is drawn in (RegisterRecolour), whose HD art it shows (its type's when empty); else null. */
	const std::string *recolour = nullptr;
};

/** @brief The part of a shadow picture that isn't empty, in texture coordinates (0 to 1): across from u0 to u1, down from vTop. */
struct CastPart {
	float u0 = 0;
	float u1 = 1;
	float vTop = 0;
};

/** @brief A smooth outline made for a sprite: white, with its shape in the alpha, and where it sits over the sprite. */
struct Outline {
	SDL_Texture *texture = nullptr;
	/** @brief Its place over the sprite's own rectangle, in the sprite's game pixels (it reaches past the rectangle). */
	SDL_FRect place {};
	bool tried = false;
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
	/** @brief The sprite's body as a black shape, sharp and soft, for the shadows it casts with HD Lighting (CastPad pixels around it). */
	SDL_Texture *castTexture = nullptr;
	SDL_Texture *castSoft = nullptr;
	CastPart sharpPart;
	CastPart softPart;
	bool castTried = false;
	/** @brief The smooth outlines round the game's own pixels, scaled up (for sprites without HD art), by OutlineCut. */
	std::array<Outline, NumOutlineCuts> outlines;
	/** @brief The first row of the sprite's body, for how far its shadow reaches. */
	int bodyTop = 0;
	/** @brief The sprite's body within its picture (BodyBounds), for the light on an object's sides; a negative left until worked out. */
	int boundLeft = -1;
	int boundRight = 0;
	int boundTop = 0;
	int boundBottom = 0;
	/** @brief The middle of the body's lowest row: the corner nearest the eye of a thing with flat sides. */
	int boundCorner = 0;
};

void FreeRasterTextures(Raster &raster)
{
	for (SDL_Texture *texture : { raster.texture, raster.shadowTexture, raster.packTexture, raster.castTexture, raster.castSoft }) {
		if (texture != nullptr)
			SDL_DestroyTexture(texture);
	}
	for (Outline &outline : raster.outlines) {
		if (outline.texture != nullptr)
			SDL_DestroyTexture(outline.texture);
	}
	raster.texture = nullptr;
	raster.shadowTexture = nullptr;
	raster.packTexture = nullptr;
	raster.castTexture = nullptr;
	raster.castSoft = nullptr;
	raster.castTried = false;
	raster.outlines = {};
}

/**
 * @brief The part of a tile's HD art where the game's lava or water flows (its palette's moving colours): those
 * pixels show the game's own moving colours, scaled up, and are painted again whenever the palette moves.
 */
struct Flow {
	/** @brief The part of the art that flows, in its pixels (Scale times the game's). */
	SDL_Rect rect {};
	/** @brief The game's palette index of each of its game pixels. */
	std::vector<uint8_t> index;
	/** @brief The art as made over that part (RGBA), and how much of each pixel the flowing colour covers (0-255). */
	std::vector<uint8_t> still;
	std::vector<uint8_t> weight;
	uint32_t paletteVersion = 0;
};

/** @brief The ways a shape map sorts an object's surface by how it faces: +x, -x, +y, -y and up. */
constexpr size_t NumShapeAxes = 5;

struct HdArt {
	SDL_Texture *texture = nullptr;
	bool missing = false;
	/** @brief Its picture is still being read (ReadPictures): the game's own pixels show until it is in. */
	bool waiting = false;
	/**
	 * @brief An object's art split by the way each part of it faces, from the shape map beside it (MakeShape): one
	 * picture per ShapeAxes entry, null where nothing faces that way. All null without a shape map.
	 */
	std::array<SDL_Texture *, NumShapeAxes> shape {};
	bool shaped = false;
	/** @brief The picture's name, for sprite art (its outlines are made from the picture). */
	std::string name;
	/** @brief Its smooth outlines, by OutlineCut. */
	std::array<Outline, NumOutlineCuts> outlines;
	/** @brief For tile art with flowing lava or water in it. */
	std::unique_ptr<Flow> flow;
};

std::vector<Draw> Draws;
/** @brief The last outline recorded, until the sprite it goes round is: its pixels, where it is drawn (unpadded), and how it looks. */
const uint8_t *PendingOutline = nullptr;
Point PendingOutlineAt;
OutlineLook PendingOutlineLook;
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
/** @brief The field multiplies the floor by light above full too (SDL_BLENDMODE_MUL); without it, it lights it up to full. */
bool FieldMultiplies = false;
/** @brief A soft round dark spot, for the shadow round a hero's feet from the light they carry. */
SDL_Texture *BlobTexture = nullptr;
/** @brief Dark towards the screen's edges and corners, clear in the middle (the Atmosphere option's edges, idea 6). */
SDL_Texture *EdgesTexture = nullptr;

SDL_Renderer *CacheRenderer = nullptr;
const std::byte *CacheCels = nullptr;
dungeon_type CacheLevelType = DTYPE_NONE;

/** @brief A sprite frame's HD art name, with the frame's size to tell it from other art loaded where freed art was. */
struct SpriteName {
	std::string name;
	uint16_t width;
	uint16_t height;
};
/** @brief Each frame of a loaded sprite, by the address of its pixels: the name of its HD art. */
std::unordered_map<const uint8_t *, SpriteName> SpriteNames;
/** @brief Each dungeon frame, by the address of its pixels: its HD art name. */
std::unordered_map<const uint8_t *, std::string> TileNames;
std::unordered_map<std::string, HdArt> Art;
/** @brief Whether the pack has art of each name asked about (HasArt). */
std::unordered_map<std::string, bool> ArtExists;

/** @brief A sprite to show in HD over the UI this frame (DrawUiSprite). */
struct PendingUiSprite {
	std::string name;
	Rectangle dest;
	uint8_t inBetween;
};
std::optional<PendingUiSprite> UiSprite;
/** @brief When RenderUi last ran: HD art over the UI is shown only while it does. */
uint32_t UiRenderedTicks = 0;

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
	/** @brief The sprite art shown, when it is a sprite's. */
	HdArt *art = nullptr;
	/** @brief The tile art shown, when lava or water flows in it. */
	HdArt *flowing = nullptr;
};
struct ArtChoiceKey {
	const uint8_t *data;
	uint64_t shape;
	const std::string *recolour;
	bool operator==(const ArtChoiceKey &other) const = default;
};
struct ArtChoiceKeyHash {
	size_t operator()(const ArtChoiceKey &key) const noexcept
	{
		return std::hash<const void *> {}(key.data) ^ std::hash<uint64_t> {}(key.shape * 0x9E3779B97F4A7C15ULL)
		    ^ (std::hash<const void *> {}(key.recolour) << 1);
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

/** @brief Tristram's evening with HD Lighting (hd_light.cpp): firelight warm, the sky's light cool. */
constexpr Mood TownEvening { 1.15F, { 1.06F, 0.95F, 0.82F }, { 0.84F, 0.92F, 1.08F } };

bool LightingWanted();

/** @brief With HD Stronger Mood, firelight is warmer and dark corners darker, in a mood of its own for each part of the dungeon. */
const Mood &AreaMood()
{
	static constexpr Mood Cathedral { 1.35F, { 1.12F, 0.95F, 0.76F }, { 0.74F, 0.84F, 1.08F } };
	static constexpr Mood Catacombs { 1.45F, { 1.00F, 0.97F, 0.93F }, { 0.68F, 0.82F, 1.14F } };
	static constexpr Mood Caves { 1.50F, { 1.16F, 0.90F, 0.66F }, { 0.84F, 0.76F, 0.74F } };
	static constexpr Mood Hell { 1.40F, { 1.18F, 0.86F, 0.64F }, { 1.00F, 0.60F, 0.56F } };
	static constexpr Mood Hive { 1.35F, { 1.08F, 1.02F, 0.70F }, { 0.80F, 0.94F, 0.62F } };
	static constexpr Mood Crypt { 1.35F, { 0.98F, 1.04F, 0.90F }, { 0.72F, 0.98F, 0.80F } };
	if (leveltype == DTYPE_TOWN && LightingWanted())
		return TownEvening;
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

/**
 * @brief The area's mood on this floor. With the Atmosphere option each floor of an area is grimmer than the one above
 * (idea 4): a smaller pool of light round each flame, redder fires and deeper shadows. The first floor keeps the area's
 * mood; the table (atmosphere.tsv: falloff, warm, cool) says how far it has gone by the last.
 */
Mood CurrentMood()
{
	const Mood &area = AreaMood();
	if (leveltype == DTYPE_TOWN || &area == &GentleMood)
		return area;
	const float grim = atmosphere::Strength(atmosphere::Knob::Grimmer);
	if (grim <= 0)
		return area;
	const float depth = atmosphere::Depth() * grim;
	Mood mood = area;
	mood.falloff += atmosphere::Value("falloff", 0, depth) - atmosphere::Value("falloff", 0, 0);
	const std::array<float, 3> warm = atmosphere::Colour("warm", {}, depth);
	const std::array<float, 3> warmFirst = atmosphere::Colour("warm", {}, 0);
	const std::array<float, 3> cool = atmosphere::Colour("cool", {}, depth);
	const std::array<float, 3> coolFirst = atmosphere::Colour("cool", {}, 0);
	for (size_t c = 0; c < 3; c++) {
		mood.warm[c] += warm[c] - warmFirst[c];
		mood.cool[c] += cool[c] - coolFirst[c];
	}
	return mood;
}

/** @brief The level's palette against the one the HD tiles were made in: Cathedral levels pick one of four. */
std::array<float, 3> TileGain { 1.F, 1.F, 1.F };
/** @brief Which of its palettes the level uses (1 is the one the pack was made in); the pack can hold tiles recoloured for each. */
int PaletteVariant = 1;
uint32_t PaletteVersion = 1;
std::array<SDL_Color, 256> LastPalette;

int PackState = -1;
/** @brief The HD pack is loose files on disk, not packed in an archive: its pictures can be read ahead by the reader. */
bool PackLoose = false;

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
#ifndef UNPACKED_MPQS
		PackLoose = ref.directHandle != nullptr;
#endif
	}
	return PackState == 1;
}

/** @brief This world is lit with HD Lighting (in town, Tristram's evening): the option is on and SDL can draw the light's shapes. */
bool LightingWanted()
{
#if SDL_VERSION_ATLEAST(2, 0, 18)
	return *GetOptions().Graphics.hdLighting;
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

void DropReadPictures();

void DestroyTextures()
{
	if (CacheRenderer != nullptr) {
		for (auto &[name, art] : Art) {
			if (art.texture != nullptr)
				SDL_DestroyTexture(art.texture);
			for (SDL_Texture *shape : art.shape) {
				if (shape != nullptr)
					SDL_DestroyTexture(shape);
			}
			for (const Outline &outline : art.outlines) {
				if (outline.texture != nullptr)
					SDL_DestroyTexture(outline.texture);
			}
		}
		for (auto &[key, raster] : Rasters)
			FreeRasterTextures(raster);
	}
	Art.clear();
	Rasters.clear();
	ArtChoices.clear();
	DropReadPictures();
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
	if (EdgesTexture != nullptr && CacheRenderer != nullptr)
		SDL_DestroyTexture(EdgesTexture);
	EdgesTexture = nullptr;
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
	DropReadPictures();
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

/** @brief HD art over the game's painted shadow counts as that shadow up to this bright; brighter there is the sprite's own edge. */
constexpr uint8_t ShadowBrightest = 56;

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

/** @brief Makes a smooth outline (MakeOutlineShape) as a white texture, with its shape in the alpha. */
Outline MakeOutline(SDL_Renderer *renderer, const std::vector<uint8_t> &body, const std::vector<uint8_t> &cover, int width, int height, float scaleX, float scaleY, OutlineCut cut)
{
	Outline outline;
	outline.tried = true;
	const OutlineShape shape = MakeOutlineShape(body, cover, width, height, scaleX, scaleY, cut);
	if (shape.empty())
		return outline;
	std::vector<uint8_t> rgba(shape.alpha.size() * 4, 255);
	for (size_t i = 0; i < shape.alpha.size(); i++)
		rgba[i * 4 + 3] = shape.alpha[i];
	outline.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, shape.width, shape.height);
	if (outline.texture == nullptr)
		return outline;
	// A glow adds light to what is under it; an outline covers it.
	SDL_SetTextureBlendMode(outline.texture, cut == OutlineCut::Glow ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
	SDL_SetTextureScaleMode(outline.texture, SDL_ScaleModeLinear);
#endif
	SDL_UpdateTexture(outline.texture, nullptr, rgba.data(), shape.width * 4);
	outline.place = SDL_FRect { shape.x, shape.y, shape.w, shape.h };
	return outline;
}

Raster PlainRaster(const Draw &draw, Point position);
void SplitShadow(Raster &raster, const Draw &draw, Point position, bool plainColours);

/**
 * @brief Leaves out the floor shadow painted into an object's HD art, for HD Lighting's own shadows that fall away from
 * each light: the art's dark pixels where the game's own sprite has its painted shadow (palette colour 0, SplitShadow)
 * and just outside it (the art's shadow has a soft edge), and the soft see-through kind (WithoutPaintedShadow).
 */
SDL_Surface *WithoutObjectShadow(SDL_Surface *surface, const Draw &sprite)
{
	SDL_Surface *rgba = WithoutPaintedShadow(surface);
	if (rgba->format->format != SDL_PIXELFORMAT_RGBA32 || sprite.width <= 0 || sprite.height <= 0)
		return rgba;
	const Point position { 0, sprite.height - 1 };
	Raster game = PlainRaster(sprite, position);
	SplitShadow(game, sprite, position, true);
	if (game.shadow.empty())
		return rgba;
	const int gw = game.width;
	const int gh = game.height;
	// The shadow and the empty pixels right beside it: not the body's own pixels.
	std::vector<uint8_t> mask(game.shadow.size(), 0);
	for (int y = 0; y < gh; y++) {
		for (int x = 0; x < gw; x++) {
			const size_t i = static_cast<size_t>(y) * gw + x;
			if (game.shadow[i] != 0) {
				mask[i] = 1;
				continue;
			}
			if (game.alpha[i] != 0)
				continue;
			for (int ny = std::max(y - 1, 0); ny <= std::min(y + 1, gh - 1) && mask[i] == 0; ny++) {
				for (int nx = std::max(x - 1, 0); nx <= std::min(x + 1, gw - 1); nx++) {
					if (game.shadow[static_cast<size_t>(ny) * gw + nx] != 0) {
						mask[i] = 1;
						break;
					}
				}
			}
		}
	}
	// How much a point of the art lies in the game's shadow, 0 to 1, smooth between the game's pixels.
	const auto shadowAt = [&](float x, float y) {
		const float fx = std::clamp(x, 0.F, static_cast<float>(gw - 1));
		const float fy = std::clamp(y, 0.F, static_cast<float>(gh - 1));
		const int x0 = std::min(static_cast<int>(fx), gw - 1);
		const int y0 = std::min(static_cast<int>(fy), gh - 1);
		const int x1 = std::min(x0 + 1, gw - 1);
		const int y1 = std::min(y0 + 1, gh - 1);
		const float u = fx - static_cast<float>(x0);
		const float v = fy - static_cast<float>(y0);
		const auto at = [&](int mx, int my) { return static_cast<float>(mask[static_cast<size_t>(my) * gw + mx]); };
		return (at(x0, y0) * (1 - u) + at(x1, y0) * u) * (1 - v) + (at(x0, y1) * (1 - u) + at(x1, y1) * u) * v;
	};
	const auto ramp = [](float from, float to, float value) {
		const float t = std::clamp((value - from) / (to - from), 0.F, 1.F);
		return t * t * (3 - 2 * t);
	};
	if (SDL_LockSurface(rgba) != 0)
		return rgba;
	const float perX = static_cast<float>(gw) / static_cast<float>(rgba->w);
	const float perY = static_cast<float>(gh) / static_cast<float>(rgba->h);
	for (int y = 0; y < rgba->h; y++) {
		auto *row = static_cast<uint8_t *>(rgba->pixels) + static_cast<ptrdiff_t>(y) * rgba->pitch;
		for (int x = 0; x < rgba->w; x++) {
			uint8_t *pixel = row + static_cast<ptrdiff_t>(x) * 4;
			const uint8_t brightest = std::max({ pixel[0], pixel[1], pixel[2] });
			if (pixel[3] == 0 || brightest >= ShadowBrightest + 16)
				continue;
			const float in = ramp(0.25F, 0.75F, shadowAt((static_cast<float>(x) + 0.5F) * perX - 0.5F, (static_cast<float>(y) + 0.5F) * perY - 0.5F));
			if (in <= 0)
				continue;
			const float dark = 1 - ramp(static_cast<float>(ShadowBrightest - 16), static_cast<float>(ShadowBrightest + 16), static_cast<float>(brightest));
			pixel[3] = static_cast<uint8_t>(static_cast<float>(pixel[3]) * (1 - in * dark) + 0.5F);
		}
	}
	SDL_UnlockSurface(rgba);
	return rgba;
}

/**
 * @brief An outline round a sprite's HD art, from its picture's edge, without the shadow painted under it.
 *
 * Round an object it goes round the object's colours only: where the game's own sprite has its painted floor shadow
 * (palette colour 0, SplitShadow), the HD art's dark pixels are shadow too, and they hide the outline there as the
 * game's shadow does (it is drawn over the outline). Items, monsters and heroes keep it all the way round, and so does
 * an object whose painted shadow isn't drawn (`shadowShown` false: HD Lighting casts its own).
 */
Outline OutlineFromPicture(SDL_Renderer *renderer, SDL_Surface *picture, const Draw &sprite, OutlineCut cut, bool shadowShown = true)
{
	Outline none;
	none.tried = true;
	if (sprite.width <= 0 || sprite.height <= 0)
		return none;
	SDL_Surface *rgba = picture->format->format == SDL_PIXELFORMAT_RGBA32 ? picture : SDL_ConvertSurfaceFormat(picture, SDL_PIXELFORMAT_RGBA32, 0);
	if (rgba == nullptr)
		return none;
	const int width = rgba->w;
	const int height = rgba->h;
	// Only objects stand on a painted floor shadow; an item's or a monster's black edge is part of it.
	Raster game;
	if (static_cast<ThingKind>(sprite.outlineLook.thing >> 24) == ThingKind::Object) {
		const Point position { 0, sprite.height - 1 };
		game = PlainRaster(sprite, position);
		SplitShadow(game, sprite, position, true);
	}
	const auto gameShadow = [&](int x, int y) {
		if (game.shadow.empty())
			return false;
		const int gx = std::min(x * sprite.width / width, game.width - 1);
		const int gy = std::min(y * sprite.height / height, game.height - 1);
		return game.shadow[static_cast<size_t>(gy) * game.width + gx] != 0;
	};
	std::vector<uint8_t> body(static_cast<size_t>(width) * height, 0);
	std::vector<uint8_t> cover(body.size(), 0);
	const bool locked = SDL_LockSurface(rgba) == 0;
	if (locked) {
		for (int y = 0; y < height; y++) {
			const uint8_t *row = static_cast<const uint8_t *>(rgba->pixels) + static_cast<ptrdiff_t>(y) * rgba->pitch;
			for (int x = 0; x < width; x++) {
				const uint8_t *pixel = row + static_cast<ptrdiff_t>(x) * 4;
				uint8_t alpha = pixel[3];
				const uint8_t brightest = std::max({ pixel[0], pixel[1], pixel[2] });
				const size_t i = static_cast<size_t>(y) * width + x;
				if (alpha != 0 && brightest < ShadowBrightest && gameShadow(x, y)) {
					// The game's painted shadow, in the HD art: it covers the outline, and the outline doesn't go round it.
					// With HD Lighting the object casts its own shadow and this one isn't drawn: nothing is there.
					cover[i] = shadowShown ? 255 : 0;
					continue;
				}
				// The soft shadow the maker paints: near-black and see-through, as WithoutPaintedShadow finds it.
				if (alpha < 160 && brightest < 24)
					alpha = 0;
				cover[i] = alpha;
				body[i] = alpha >= 128 ? 1 : 0;
			}
		}
		SDL_UnlockSurface(rgba);
	}
	if (rgba != picture)
		SDL_FreeSurface(rgba);
	if (!locked)
		return none;
	return MakeOutline(renderer, body, cover, width, height, static_cast<float>(width) / static_cast<float>(sprite.width), static_cast<float>(height) / static_cast<float>(sprite.height), cut);
}

// HD pictures are read from disk by threads of their own, ahead of the frame that shows them. Opening and unpacking a
// picture in the frame froze it (some 10 ms a picture on a busy disk: a building coming into view cost 27 at once).
#if !defined(UNPACKED_MPQS) && (defined(_WIN32) || defined(__APPLE__) || (defined(__linux__) && !defined(__ANDROID__)))
#define POE2_HD_READER 1
#endif

/** @brief A picture asked of the reader: waiting, read (`done`), or not on disk (`done` with no surface). */
struct ReadPicture {
	SDL_Surface *surface = nullptr;
	bool done = false;
	/** @brief When it was last asked for (SDL_GetTicks): pictures nobody took are let go (TidyReadPictures). */
	uint32_t used = 0;
	/** @brief A draw is waiting on it, so it was moved to the front of the reader's list. */
	bool rushed = false;
	/** @brief A draw has taken it before. */
	bool taken = false;
};

/**
 * @brief How long a sprite's picture read ahead is kept for the draw that may want it, in milliseconds. A tile's
 * picture is kept while its level lasts: the same tile is cut again each time a wall near the hero turns see-through.
 */
constexpr uint32_t ReadPictureKeptMs = 15000;
/** @brief How long a frame may spend turning pictures read ahead into textures; the rest wait a frame. */
constexpr float PictureBudgetMs = 2.5F;

/** @brief The pictures asked of the reader, by asset path (hd\tiles\town\12.png). Only the game's thread touches it. */
std::unordered_map<std::string, ReadPicture> ReadPictures;
/** @brief The folders loose files are looked for in, in order (the game's thread's copy). */
std::vector<std::string> ReadDirs;
/** @brief Frames drawn since the level's art was last named: the first ones wait for their pictures, later ones don't. */
int SettledFrames = 0;
/** @brief A level's first frames come up whole, behind the loading screen's fade, however long their pictures take. */
constexpr int SettlingFrames = 3;
/** @brief A picture was not in yet when it was asked for, since this was last cleared: what was chosen is not kept. */
bool ArtLater = false;
uint64_t PictureBudgetEnd = 0;
bool PictureMadeThisFrame = false;
/** @brief The dungeon pieces whose pictures were asked for ahead (ReadAheadTiles). */
std::vector<bool> PieceReadAhead;
Point ReadAheadView { -1, -1 };
uint32_t ReadTidiedMs = 0;
/** @brief Sprites are being drawn with in-between frames, so those are read ahead too. */
bool InBetweensShown = false;
/** @brief The frame of each animation that was read last, by its name up to the frame's number: tells which way it plays. */
std::unordered_map<std::string, int> SpriteFrameShown;

bool IsTilePicture(std::string_view path)
{
	return path.starts_with("hd\\tiles\\");
}

/** @brief Reads a loose picture from the first folder that has it. Safe on any thread. */
SDL_Surface *ReadLoosePicture(const std::vector<std::string> &dirs, const std::string &path)
{
	std::string relative = path;
#ifndef _WIN32
	std::replace(relative.begin(), relative.end(), '\\', '/');
#endif
	for (const std::string &dir : dirs) {
		SDL_RWops *file = SDL_RWFromFile((dir + relative).c_str(), "rb");
		if (file == nullptr)
			continue;
		SDL_Surface *surface = IMG_LoadPNG_RW(file);
		SDL_RWclose(file);
		// A tile's picture is cut to the tile's shape in this format (MaskedArt): turned into it here, off the frame.
		if (surface != nullptr && surface->format->format != SDL_PIXELFORMAT_RGBA32 && IsTilePicture(path)) {
			SDL_Surface *rgba = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
			if (rgba != nullptr) {
				SDL_FreeSurface(surface);
				surface = rgba;
			}
		}
		return surface;
	}
	return nullptr;
}

#ifdef POE2_HD_READER
/** @brief How many pictures are read at a time: small files read side by side come in about that much faster. */
constexpr size_t ReaderThreads = 2;

/** @brief What the reader's threads and the game's share, under `lock`. Made once and never freed: the threads may outlive the game's own data. */
struct Reader {
	SdlMutex lock;
	/** @brief Signalled when a picture is asked for. */
	SDL_cond *asked = SDL_CreateCond();
	/** @brief Signalled when a picture is read. */
	SDL_cond *read = SDL_CreateCond();
	std::deque<std::string> wanted;
	/** @brief The pictures being read now, one a thread (empty for a thread with nothing to read). */
	std::array<std::string, ReaderThreads> busy;
	std::vector<std::pair<std::string, SDL_Surface *>> done;
	std::vector<std::string> dirs;
	std::array<SdlThread, ReaderThreads> threads;
};
Reader *TheReader = nullptr;

/** @brief A reader thread and its place in Reader::busy. */
struct ReaderSeat {
	Reader *reader;
	size_t seat;
};

int SDLCALL ReaderMain(void *data)
{
	const auto *mine = static_cast<const ReaderSeat *>(data);
	Reader *reader = mine->reader;
	std::string &busy = reader->busy[mine->seat];
	for (;;) {
		std::string path;
		std::vector<std::string> dirs;
		{
			const std::lock_guard<SdlMutex> guard(reader->lock);
			while (reader->wanted.empty())
				SDL_CondWait(reader->asked, reader->lock.get());
			path = std::move(reader->wanted.front());
			reader->wanted.pop_front();
			busy = path;
			dirs = reader->dirs;
		}
		SDL_Surface *surface = ReadLoosePicture(dirs, path);
		{
			const std::lock_guard<SdlMutex> guard(reader->lock);
			reader->done.emplace_back(std::move(path), surface);
			busy.clear();
		}
		SDL_CondBroadcast(reader->read);
	}
	return 0;
}
#endif

/** @brief Whether pictures are read ahead: on a computer with threads, for a pack of loose files (feel("readahead", 0) or POE2_HD_READER=0 turns it off). */
bool ReaderOn()
{
#ifdef POE2_HD_READER
	static const bool allowed = [] {
		const char *env = std::getenv("POE2_HD_READER");
		return env == nullptr || env[0] != '0';
	}();
	return allowed && PackLoose && atmosphere::Setting(atmosphere::Knob::ReadAhead) > 0;
#else
	return false;
#endif
}

#ifdef POE2_HD_READER
/** @brief Keeps the folders to look in the same as the game's own search (FindAsset): mod folders first, then the game's assets. */
void UpdateReadDirs()
{
	const std::string &assets = paths::AssetsPath();
	if (ReadDirs.size() == OverridePaths.size() + 1 && ReadDirs.back() == assets && std::equal(OverridePaths.begin(), OverridePaths.end(), ReadDirs.begin()))
		return;
	ReadDirs = OverridePaths;
	ReadDirs.push_back(assets);
	if (TheReader != nullptr) {
		const std::lock_guard<SdlMutex> guard(TheReader->lock);
		TheReader->dirs = ReadDirs;
	}
}

/** @brief Takes in what the reader has read since the last time. */
void CollectReadPictures()
{
	if (TheReader == nullptr)
		return;
	std::vector<std::pair<std::string, SDL_Surface *>> done;
	{
		const std::lock_guard<SdlMutex> guard(TheReader->lock);
		done.swap(TheReader->done);
	}
	const uint32_t now = SDL_GetTicks();
	for (auto &[path, surface] : done) {
		const auto it = ReadPictures.find(path);
		if (it == ReadPictures.end() || it->second.done) {
			// Nobody is waiting for it any more.
			if (surface != nullptr)
				SDL_FreeSurface(surface);
			continue;
		}
		it->second.surface = surface;
		it->second.done = true;
		it->second.used = now;
	}
}

/** @brief Starts the reader's threads the first time a picture is asked of them. */
void StartReader()
{
	if (TheReader != nullptr)
		return;
	// Made sure of on this thread, before the others unpack pictures too.
	InitPNG();
	UpdateReadDirs();
	TheReader = new Reader();
	TheReader->dirs = ReadDirs;
	for (size_t seat = 0; seat < ReaderThreads; seat++)
		TheReader->threads[seat] = SdlThread { ReaderMain, new ReaderSeat { TheReader, seat } };
}

/** @brief Asks the reader for a picture, if it wasn't asked for yet: `first` for one a draw wants now or within a few frames. */
void WantPicture(const std::string &path, bool first)
{
	const auto [it, inserted] = ReadPictures.try_emplace(path);
	if (!inserted)
		return;
	it->second.used = SDL_GetTicks();
	StartReader();
	{
		const std::lock_guard<SdlMutex> guard(TheReader->lock);
		if (first)
			TheReader->wanted.push_front(path);
		else
			TheReader->wanted.push_back(path);
	}
	SDL_CondSignal(TheReader->asked);
}

/** @brief Asks the reader for those of a list of pictures that weren't asked for yet, after what it has to read already. */
void WantPictures(std::vector<std::string> &paths)
{
	const uint32_t now = SDL_GetTicks();
	std::erase_if(paths, [now](const std::string &path) {
		const auto [it, inserted] = ReadPictures.try_emplace(path);
		if (inserted)
			it->second.used = now;
		return !inserted;
	});
	if (paths.empty())
		return;
	StartReader();
	{
		const std::lock_guard<SdlMutex> guard(TheReader->lock);
		for (std::string &path : paths)
			TheReader->wanted.push_back(std::move(path));
	}
	SDL_CondBroadcast(TheReader->asked);
}

/** @brief Gets a picture the reader was asked for in now: read here if the reader hasn't started on it, waited for if it has. */
void SettlePicture(const std::string &path, ReadPicture &picture)
{
	bool readHere = false;
	{
		const std::lock_guard<SdlMutex> guard(TheReader->lock);
		const auto queued = std::find(TheReader->wanted.begin(), TheReader->wanted.end(), path);
		if (queued != TheReader->wanted.end()) {
			TheReader->wanted.erase(queued);
			readHere = true;
		} else {
			while (std::find(TheReader->busy.begin(), TheReader->busy.end(), path) != TheReader->busy.end())
				SDL_CondWait(TheReader->read, TheReader->lock.get());
		}
	}
	if (readHere)
		picture.surface = ReadLoosePicture(ReadDirs, path);
	else
		CollectReadPictures();
	picture.done = true;
}
#endif

/** @brief Whether a frame may still turn a picture read ahead into a texture: always one, then as many as fit in PictureBudgetMs. */
bool MayMakePicture()
{
	return !PictureMadeThisFrame || SDL_GetPerformanceCounter() < PictureBudgetEnd;
}

/**
 * @brief Whether a picture can be had without holding the frame up: it has been read ahead (or found to be missing)
 * and, for a `timed` one, the frame has time left for it. If not, it is asked for and the draw shows the game's own
 * pixels meanwhile.
 */
bool PictureIsIn(const std::string &path, bool timed)
{
#ifdef POE2_HD_READER
	UpdateReadDirs();
	const auto it = ReadPictures.find(path);
	if (it == ReadPictures.end()) {
		WantPicture(path, true);
		return false;
	}
	if (!it->second.done) {
		if (!it->second.rushed) {
			it->second.rushed = true;
			const std::lock_guard<SdlMutex> guard(TheReader->lock);
			const auto queued = std::find(TheReader->wanted.begin(), TheReader->wanted.end(), path);
			if (queued != TheReader->wanted.end() && queued != TheReader->wanted.begin()) {
				TheReader->wanted.erase(queued);
				TheReader->wanted.push_front(path);
			}
		}
		return false;
	}
	// A tile shown before is cut again at once (a wall turning see-through must not flicker).
	return !timed || it->second.surface == nullptr || it->second.taken || MayMakePicture();
#else
	(void)path;
	(void)timed;
	return true;
#endif
}

/**
 * @brief An HD picture by its asset path, or nullptr if there is none: from the reader if it has it, else read now.
 * A picture that is `shared` stays with the reader for a while, for the other cuts of the same tile; the caller frees
 * what it gets either way.
 */
SDL_Surface *TakePicture(const std::string &path, bool shared)
{
#ifdef POE2_HD_READER
	if (ReaderOn()) {
		UpdateReadDirs();
		auto [it, inserted] = ReadPictures.try_emplace(path);
		ReadPicture &picture = it->second;
		if (inserted) {
			picture.surface = ReadLoosePicture(ReadDirs, path);
			picture.done = true;
		} else if (!picture.done) {
			SettlePicture(path, picture);
		}
		picture.used = SDL_GetTicks();
		SDL_Surface *surface = picture.surface;
		if (surface != nullptr) {
			if (!PictureMadeThisFrame) {
				PictureMadeThisFrame = true;
				PictureBudgetEnd = SDL_GetPerformanceCounter() + static_cast<uint64_t>(static_cast<double>(SDL_GetPerformanceFrequency()) * PictureBudgetMs / 1000);
			}
			if (shared) {
				surface->refcount++;
				picture.taken = true;
			} else {
				ReadPictures.erase(it);
			}
		}
		return surface;
	}
#else
	(void)shared;
#endif
	AssetRef ref = FindAsset(path);
	if (!ref.ok())
		return nullptr;
#ifdef UNPACKED_MPQS
	SDL_RWops *file = SDL_RWFromFile(ref.path, "rb");
#else
	SDL_RWops *file = OpenAsset(std::move(ref)).release();
#endif
	if (file == nullptr)
		return nullptr;
	SDL_Surface *surface = IMG_LoadPNG_RW(file);
	SDL_RWclose(file);
	return surface;
}

/** @brief The asset path of an HD picture by its name (tiles/town/12). */
std::string PicturePath(std::string_view name)
{
	std::string path = "hd\\";
	path += name;
	path += ".png";
	std::replace(path.begin(), path.end(), '/', '\\');
	return path;
}

/** @brief Lets go of sprite pictures read ahead that no draw took. */
void TidyReadPictures()
{
	const uint32_t now = SDL_GetTicks();
	if (now - ReadTidiedMs < 2000)
		return;
	ReadTidiedMs = now;
	for (auto it = ReadPictures.begin(); it != ReadPictures.end();) {
		if (it->second.done && now - it->second.used > ReadPictureKeptMs && !IsTilePicture(it->first)) {
			if (it->second.surface != nullptr)
				SDL_FreeSurface(it->second.surface);
			it = ReadPictures.erase(it);
		} else {
			++it;
		}
	}
}

/** @brief Lets go of everything read ahead or asked for: the level changed, or the art is to be made anew. */
void DropReadPictures()
{
#ifdef POE2_HD_READER
	if (TheReader != nullptr) {
		const std::lock_guard<SdlMutex> guard(TheReader->lock);
		TheReader->wanted.clear();
	}
	CollectReadPictures();
#endif
	for (auto &[path, picture] : ReadPictures) {
		if (picture.surface != nullptr)
			SDL_FreeSurface(picture.surface);
	}
	// A picture the reader is in the middle of comes back to nobody, and is freed (CollectReadPictures).
	ReadPictures.clear();
	PieceReadAhead.clear();
	ReadAheadView = { -1, -1 };
	SettledFrames = 0;
}

/**
 * @brief Asks the reader for the frames that follow a sprite's (and their in-betweens), so an animation's pictures are
 * in before they show. Some animations play backwards (the storyteller's): those have the frames before read instead.
 */
void ReadAheadSprite(const std::string &name, bool withoutShadow)
{
#ifdef POE2_HD_READER
	if (!ReaderOn())
		return;
	// monsters/sking/skinga/d3_f05, or an in-between of it: d3_f05_m1 on the way to f06, d3_f05_b1 on the way back to f04.
	std::string_view frameName = name;
	int way = 0;
	if (frameName.size() > 3 && frameName[frameName.size() - 3] == '_' && (frameName[frameName.size() - 2] == 'm' || frameName[frameName.size() - 2] == 'b')) {
		way = frameName[frameName.size() - 2] == 'm' ? 1 : -1;
		frameName.remove_suffix(3);
	}
	const size_t at = frameName.rfind("_f");
	if (at == std::string_view::npos || at + 2 >= frameName.size())
		return;
	int frame = 0;
	for (const char c : frameName.substr(at + 2)) {
		if (c < '0' || c > '9')
			return;
		frame = frame * 10 + (c - '0');
	}
	const std::string stem(frameName.substr(0, at + 2));
	// Which way it plays: by its in-between, else by the frame of it that showed before.
	int &last = SpriteFrameShown.try_emplace(stem, frame).first->second;
	if (way == 0)
		way = frame < last ? -1 : 1;
	last = frame;
	std::vector<std::string> wanted;
	const auto want = [&](const std::string &picture) {
		if (!Art.contains(withoutShadow ? picture + "#lit" : picture))
			wanted.push_back(PicturePath(picture));
	};
	const char *between = way > 0 ? "_m" : "_b";
	for (int ahead = 0; ahead <= 3; ahead++) {
		if (frame + way * ahead < 0)
			break;
		char number[16];
		std::snprintf(number, sizeof(number), "%02d", frame + way * ahead);
		const std::string next = stem + number;
		if (ahead != 0)
			want(next);
		if (InBetweensShown && ahead < 3) {
			want(next + between + "1");
			want(next + between + "2");
		}
	}
	// An animation that goes round starts again at its first frame.
	if (way > 0 && frame > 0) {
		want(stem + "00");
		if (InBetweensShown) {
			want(stem + "00_m1");
			want(stem + "00_m2");
		}
	}
	// Ahead of the tiles round the view: these show within a few frames. The nearest goes in front.
	for (size_t i = wanted.size(); i-- > 0;)
		WantPicture(wanted[i], true);
#else
	(void)name;
	(void)withoutShadow;
#endif
}

/** @brief A shape map's tilt: the game looks down on the floor at 30 degrees (a tile is twice as wide as it is high). */
constexpr float ViewSin = 0.5F;
constexpr float ViewCos = 0.8660254F;
/** @brief A way of facing gets its own picture only if this much of the object faces it. */
constexpr float ShapeLeastShare = 0.004F;

/**
 * @brief Splits an object's HD art by the way each part of it faces, from its shape map: the picture beside it named
 * with _n (a normal map as the eye sees it: red is right, green is up, blue is towards the eye).
 *
 * The renderer has no shaders, so light by shape is drawn as a few pictures added together, each lit as one way of
 * facing is (see DrawShapedObject). Each pixel's facing is turned from the eye's view to the world's (x right and down
 * the screen, y left and down, z up) and shared out among the axes it leans to by the square of its lean, which adds up
 * to one: its colour goes into each axis's picture at that share of its alpha.
 */
void MakeShape(SDL_Renderer *renderer, HdArt &art, SDL_Surface *picture, const std::string &name)
{
	if (picture->format->format != SDL_PIXELFORMAT_RGBA32)
		return;
	SDL_Surface *loaded = TakePicture(PicturePath(name + "_n"), false);
	if (loaded == nullptr)
		return;
	SDL_Surface *map = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
	SDL_FreeSurface(loaded);
	if (map == nullptr)
		return;
	const int width = picture->w;
	const int height = picture->h;
	if (width <= 0 || height <= 0 || SDL_LockSurface(picture) != 0) {
		SDL_FreeSurface(map);
		return;
	}
	if (SDL_LockSurface(map) != 0) {
		SDL_UnlockSurface(picture);
		SDL_FreeSurface(map);
		return;
	}
	std::array<std::vector<uint8_t>, NumShapeAxes> pixels;
	for (std::vector<uint8_t> &axis : pixels)
		axis.assign(static_cast<size_t>(width) * height * 4, 0);
	std::array<uint64_t, NumShapeAxes> faces {};
	uint64_t body = 0;
	constexpr float Root2 = 1.41421356F;
	for (int y = 0; y < height; y++) {
		const uint8_t *row = static_cast<const uint8_t *>(picture->pixels) + static_cast<ptrdiff_t>(y) * picture->pitch;
		const uint8_t *mapRow = static_cast<const uint8_t *>(map->pixels) + static_cast<ptrdiff_t>(std::min(y * map->h / height, map->h - 1)) * map->pitch;
		for (int x = 0; x < width; x++) {
			const uint8_t *pixel = row + static_cast<ptrdiff_t>(x) * 4;
			if (pixel[3] == 0)
				continue;
			const uint8_t *facing = mapRow + static_cast<ptrdiff_t>(std::min(x * map->w / width, map->w - 1)) * 4;
			float right = static_cast<float>(facing[0]) / 127.5F - 1;
			float up = static_cast<float>(facing[1]) / 127.5F - 1;
			float toEye = static_cast<float>(facing[2]) / 127.5F - 1;
			// Nothing mapped here: taken as facing the eye.
			if (facing[3] == 0 || right * right + up * up + toEye * toEye < 0.04F) {
				right = 0;
				up = 0;
				toEye = 1;
			}
			float nx = (right - up * ViewSin + toEye * ViewCos) / Root2;
			float ny = (-right - up * ViewSin + toEye * ViewCos) / Root2;
			float nz = up * ViewCos + toEye * ViewSin;
			const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
			nx /= length;
			ny /= length;
			nz /= length;
			const std::array<float, NumShapeAxes> share { nx > 0 ? nx * nx : 0, nx < 0 ? nx * nx : 0, ny > 0 ? ny * ny : 0, ny < 0 ? ny * ny : 0, nz > 0 ? nz * nz : 0 };
			body += pixel[3];
			const size_t at = (static_cast<size_t>(y) * width + x) * 4;
			for (size_t k = 0; k < NumShapeAxes; k++) {
				// The colour goes in even where none of it shows, so a smoothed edge between two facings keeps it.
				const auto alpha = static_cast<uint8_t>(static_cast<float>(pixel[3]) * share[k] + 0.5F);
				pixels[k][at] = pixel[0];
				pixels[k][at + 1] = pixel[1];
				pixels[k][at + 2] = pixel[2];
				pixels[k][at + 3] = alpha;
				faces[k] += alpha;
			}
		}
	}
	SDL_UnlockSurface(map);
	SDL_UnlockSurface(picture);
	SDL_FreeSurface(map);
	for (size_t k = 0; k < NumShapeAxes; k++) {
		if (static_cast<float>(faces[k]) <= static_cast<float>(body) * ShapeLeastShare)
			continue;
		SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height);
		if (texture == nullptr)
			continue;
		SDL_UpdateTexture(texture, nullptr, pixels[k].data(), width * 4);
		// Added on top of the art drawn in the least light any of its sides takes.
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_ADD);
#if SDL_VERSION_ATLEAST(2, 0, 12)
		SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
#endif
		art.shape[k] = texture;
		art.shaped = true;
	}
}

/**
 * @brief A sprite's HD art, loaded the first time it is asked for (its texture is null when there is none). A sprite
 * drawn with an outline (`outlined`) gets its smooth outline made at the same time, from the picture just loaded.
 * Without its painted shadow, an `object` loses the floor shadow the game painted into it (WithoutObjectShadow), and
 * takes its shape map if it has one (MakeShape). A picture that `mayComeLater` is not waited for (its texture is null
 * and ArtLater set until the reader has it).
 */
HdArt &LoadArt(SDL_Renderer *renderer, const std::string &name, bool withoutShadow, const Draw *outlined, const Draw *object = nullptr, bool mayComeLater = false)
{
	auto [it, inserted] = Art.try_emplace(withoutShadow ? name + "#lit" : name);
	HdArt &art = it->second;
	if (!inserted && !art.waiting)
		return art;
	// A sprite seen for the first time doesn't hold the frame up for its picture: it shows the game's own pixels for
	// the frame or two until the reader has it, and the frames after it are read meanwhile (ReadAheadSprite).
	art.waiting = mayComeLater && ReaderOn() && SettledFrames >= SettlingFrames && !PictureIsIn(PicturePath(name), false);
	if (art.waiting) {
		ArtLater = true;
		ReadAheadSprite(name, withoutShadow);
		return art;
	}
	// Made the first time it shows, in the frame (and read then too, unless the reader had it): timed for the play log's freezes.
	const SlowWorkScope timed(SlowWork::HdArt);
	art.name = name;
	SDL_Surface *surface = TakePicture(PicturePath(name), false);
	// The frames that follow it in its animation are read meanwhile, off the frame.
	ReadAheadSprite(name, withoutShadow);
	if (surface == nullptr) {
		art.missing = true;
		// For the pack maker: POE2_HD_MISSING=<file> lists the art the game looked for and did not find.
		if (const char *log = std::getenv("POE2_HD_MISSING"); log != nullptr) {
			if (FILE *f = std::fopen(log, "a"); f != nullptr) {
				std::fprintf(f, "%s\n", name.c_str());
				std::fclose(f);
			}
		}
		return art;
	}
	if (withoutShadow)
		surface = object != nullptr ? WithoutObjectShadow(surface, *object) : WithoutPaintedShadow(surface);
	if (outlined != nullptr) {
		const OutlineCut cut = OutlineCutFor(outlined->outlineLook);
		art.outlines[static_cast<size_t>(cut)] = OutlineFromPicture(renderer, surface, *outlined, cut, !withoutShadow);
		if (outlined->outlineLook.glow)
			art.outlines[static_cast<size_t>(OutlineCut::Glow)] = OutlineFromPicture(renderer, surface, *outlined, OutlineCut::Glow, !withoutShadow);
	}
	art.texture = SDL_CreateTextureFromSurface(renderer, surface);
	if (art.texture != nullptr && object != nullptr && withoutShadow)
		MakeShape(renderer, art, surface, name);
	SDL_FreeSurface(surface);
	if (art.texture != nullptr) {
		SDL_SetTextureBlendMode(art.texture, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
		SDL_SetTextureScaleMode(art.texture, SDL_ScaleModeLinear);
#endif
	}
	return art;
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
			const SlowWorkScope timed(SlowWork::HdRaster);
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
		const SlowWorkScope timed(SlowWork::HdRaster);
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
 * @brief The last of the palette's moving colours (they start at 1) on this level: the lava in the Caves and the
 * Crypt, the Hive's ponds. 0 where none move. (Hell's lava moves through its light tables instead, which its HD art
 * doesn't follow yet.)
 */
int LastFlowingColour()
{
	if (!*GetOptions().Graphics.colorCycling)
		return 0;
	switch (leveltype) {
	case DTYPE_CAVES:
	case DTYPE_CRYPT:
		return 31;
	case DTYPE_NEST:
		return 15;
	default:
		return 0;
	}
}

/**
 * @brief Where lava or water flows in a tile's art (the game pixels in the palette's moving colours, 1 to lastFlowing),
 * and what is needed to show the game's own moving colours there. The art's own pixels along the lava's edge stay
 * half showing, so its rock meets the lava softly. nullptr when nothing in the tile flows.
 */
std::unique_ptr<Flow> FindFlow(const SDL_Surface *surface, const Raster &raster, int lastFlowing)
{
	const auto flows = [&](int x, int y) {
		const size_t i = static_cast<size_t>(y) * raster.width + x;
		return raster.alpha[i] != 0 && raster.index[i] >= 1 && raster.index[i] <= lastFlowing;
	};
	int left = raster.width;
	int top = raster.height;
	int right = -1;
	int bottom = -1;
	for (int y = 0; y < raster.height; y++) {
		for (int x = 0; x < raster.width; x++) {
			if (!flows(x, y))
				continue;
			left = std::min(left, x);
			right = std::max(right, x);
			top = std::min(top, y);
			bottom = std::max(bottom, y);
		}
	}
	if (right < 0)
		return nullptr;
	auto flow = std::make_unique<Flow>();
	const int gameWidth = right - left + 1;
	const int gameHeight = bottom - top + 1;
	flow->rect = SDL_Rect { left * Scale, top * Scale, gameWidth * Scale, gameHeight * Scale };
	flow->index.resize(static_cast<size_t>(gameWidth) * gameHeight);
	for (int y = 0; y < gameHeight; y++) {
		for (int x = 0; x < gameWidth; x++)
			flow->index[static_cast<size_t>(y) * gameWidth + x] = raster.index[static_cast<size_t>(top + y) * raster.width + left + x];
	}
	// Rock: a drawn game pixel that doesn't flow. Past the tile's edge, or where it draws nothing, the lava goes on
	// in the tile next to it.
	const auto rock = [&](int artX, int artY) {
		if (artX < 0 || artY < 0 || artX >= raster.width * Scale || artY >= raster.height * Scale)
			return false;
		const int x = artX / Scale;
		const int y = artY / Scale;
		return raster.alpha[static_cast<size_t>(y) * raster.width + x] != 0 && !flows(x, y);
	};
	const int width = flow->rect.w;
	const int height = flow->rect.h;
	flow->still.resize(static_cast<size_t>(width) * height * 4);
	flow->weight.assign(static_cast<size_t>(width) * height, 0);
	for (int y = 0; y < height; y++) {
		const int artY = flow->rect.y + y;
		const uint8_t *row = static_cast<const uint8_t *>(surface->pixels) + static_cast<ptrdiff_t>(artY) * surface->pitch + static_cast<ptrdiff_t>(flow->rect.x) * 4;
		std::memcpy(&flow->still[static_cast<size_t>(y) * width * 4], row, static_cast<size_t>(width) * 4);
		for (int x = 0; x < width; x++) {
			const int artX = flow->rect.x + x;
			if (!flows(artX / Scale, artY / Scale))
				continue;
			const bool edge = rock(artX - 1, artY) || rock(artX + 1, artY) || rock(artX, artY - 1) || rock(artX, artY + 1);
			flow->weight[static_cast<size_t>(y) * width + x] = edge ? 128 : 255;
		}
	}
	return flow;
}

/** @brief Paints the flowing part of a tile's art in the palette's colours of the moment, when the palette has moved. */
void RefreshFlow(HdArt &art)
{
	Flow &flow = *art.flow;
	if (flow.paletteVersion == PaletteVersion || art.texture == nullptr)
		return;
	flow.paletteVersion = PaletteVersion;
	static std::vector<uint8_t> pixels;
	pixels = flow.still;
	const int width = flow.rect.w;
	const int height = flow.rect.h;
	const int gameWidth = width / Scale;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			const size_t k = static_cast<size_t>(y) * width + x;
			const int weight = flow.weight[k];
			if (weight == 0)
				continue;
			const SDL_Color &colour = logical_palette[flow.index[static_cast<size_t>(y / Scale) * gameWidth + x / Scale]];
			uint8_t *pixel = &pixels[k * 4];
			pixel[0] = static_cast<uint8_t>(pixel[0] + (colour.r - pixel[0]) * weight / 255);
			pixel[1] = static_cast<uint8_t>(pixel[1] + (colour.g - pixel[1]) * weight / 255);
			pixel[2] = static_cast<uint8_t>(pixel[2] + (colour.b - pixel[2]) * weight / 255);
		}
	}
	SDL_UpdateTexture(art.texture, &flow.rect, pixels.data(), width * 4);
}

/**
 * @brief A tile's HD art cut to exactly the pixels the game draws for it: the same frame can be drawn as
 * a floor triangle or a full square, and walls near the hero are see-through or half cut away.
 * nullptr when the tile has no HD art.
 */
HdArt *MaskedArt(SDL_Renderer *renderer, const std::string &name, const Draw &draw)
{
	const std::string key = name + "#" + std::to_string(static_cast<int>(draw.mask)) + "/" + std::to_string(static_cast<int>(draw.tile)) + "/" + std::to_string(draw.height);
	auto [it, inserted] = Art.try_emplace(key);
	HdArt &art = it->second;
	if (!inserted && !art.waiting)
		return art.texture != nullptr ? &art : nullptr;
	const std::string path = PicturePath(name);
	// Once a level has settled, a tile that comes into view never holds the frame up for its picture: it shows the
	// game's own pixels for the frame or two until the reader has it (ReadAheadTiles asks before it is in view).
	art.waiting = ReaderOn() && SettledFrames >= SettlingFrames && !PictureIsIn(path, true);
	if (art.waiting) {
		ArtLater = true;
		return nullptr;
	}
	const SlowWorkScope timed(SlowWork::HdArt);
	art.missing = true;
	SDL_Surface *loaded = TakePicture(path, true);
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
		const int lastFlowing = raster.cycles ? LastFlowingColour() : 0;
		if (lastFlowing > 0)
			art.flow = FindFlow(surface, raster, lastFlowing);
		if (art.flow != nullptr) {
			// Painted again as the lava flows, so in a format of our own choosing.
			art.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, surface->w, surface->h);
			if (art.texture != nullptr)
				SDL_UpdateTexture(art.texture, nullptr, surface->pixels, surface->pitch);
		} else {
			art.texture = SDL_CreateTextureFromSurface(renderer, surface);
		}
		if (art.texture != nullptr) {
			art.missing = false;
			SDL_SetTextureBlendMode(art.texture, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
			// Nearest, like the game's own tiles: smoothing the cut edges of two tiles that meet lets the black
			// under them show through as thin lines wherever the screen isn't drawn at exactly twice the world.
			SDL_SetTextureScaleMode(art.texture, SDL_ScaleModeNearest);
#endif
		} else {
			art.flow = nullptr;
		}
	}
	SDL_FreeSurface(surface);
	return art.texture != nullptr ? &art : nullptr;
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
	// Names stay after their art is freed, and art loaded without a name can land in the same place.
	if (it == SpriteNames.end() || it->second.width != draw.sprite->width() || it->second.height != draw.sprite->height())
		return nullptr;
	return &it->second.name;
}

/** @brief Works out the HD art a draw shows, if there is any for it. */
ArtChoice ChooseArt(SDL_Renderer *renderer, const Draw &draw)
{
	ArtChoice choice;
	const std::string *name = ArtName(draw);
	if (name == nullptr)
		return choice;
	// A unique monster's colours: its own art, beside the sprite's (monsters/sking/skinga@skelaxe/white+genrl/d3_f05).
	std::string uniqueName;
	if (draw.recolour != nullptr && !draw.recolour->empty()) {
		const size_t slash = name->rfind('/');
		if (slash == std::string::npos)
			return choice;
		uniqueName = name->substr(0, slash) + "+" + *draw.recolour + name->substr(slash);
		name = &uniqueName;
	}
	// Wall tiles cut away near the hero keep the game's own cut-out shape.
	const bool masked = draw.kind == DrawKind::Tile && draw.mask != MaskType::Solid;
	SDL_Texture *texture = nullptr;
	HdArt *art = nullptr;
	bool recoloured = false;
	// With HD Lighting, heroes, monsters and solid objects cast shadows of their own in place of the one painted in.
	const bool withoutShadow = FrameLit && draw.caster != Caster::None;
	const Draw *object = IsObject(draw.caster) ? &draw : nullptr;
	// In-betweens 1 and 2 lead to the next frame (_m1, _m2); 3 and 4 back to the frame before (_b1, _b2).
	if (draw.inBetween != 0 && !masked) {
		const bool back = draw.inBetween > 2;
		InBetweensShown = true;
		art = &LoadArt(renderer, *name + (back ? "_b" : "_m") + std::to_string(back ? draw.inBetween - 2 : draw.inBetween), withoutShadow, draw.outlined ? &draw : nullptr, object, true);
		texture = art->texture;
	}
	// Tiles recoloured for this level's palette, e.g. tiles/l1_p3/120, beat evening out the base ones.
	HdArt *tileArt = nullptr;
	if (texture == nullptr && draw.kind == DrawKind::Tile && PaletteVariant > 1) {
		const size_t slash = name->find('/', 6);
		if (slash != std::string::npos) {
			tileArt = MaskedArt(renderer, name->substr(0, slash) + "_p" + std::to_string(PaletteVariant) + name->substr(slash), draw);
			texture = tileArt != nullptr ? tileArt->texture : nullptr;
			recoloured = texture != nullptr;
			// Still being read: not the plain tile meanwhile, which would show in other colours for a frame.
			if (ArtLater)
				return choice;
		}
	}
	if (texture == nullptr) {
		if (draw.kind == DrawKind::Tile) {
			tileArt = MaskedArt(renderer, *name, draw);
			texture = tileArt != nullptr ? tileArt->texture : nullptr;
		} else {
			art = &LoadArt(renderer, *name, withoutShadow, draw.outlined ? &draw : nullptr, object, true);
			texture = art->texture;
		}
	}
	choice.texture = texture;
	choice.hdArt = texture != nullptr && draw.kind != DrawKind::Tile;
	choice.tileArt = texture != nullptr && draw.kind == DrawKind::Tile && !recoloured;
	choice.art = choice.hdArt ? art : nullptr;
	choice.flowing = tileArt != nullptr && tileArt->flow != nullptr ? tileArt : nullptr;
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
	auto [it, inserted] = ArtChoices.try_emplace(ArtChoiceKey { data, shape, draw.recolour });
	if (!inserted)
		return it->second;
	ArtLater = false;
	const ArtChoice choice = ChooseArt(renderer, draw);
	if (ArtLater) {
		// Its picture is still being read: chosen again next frame.
		ArtChoices.erase(it);
		return choice;
	}
	it->second = choice;
	return choice;
}

/** @brief How long a frame may spend making outlines for art loaded before; the rest show the game's own for a frame. */
constexpr float OutlineBudgetMs = 2.F;
uint64_t OutlineBudgetEnd = 0;
bool OutlineMadeThisFrame = false;

/** @brief An outline round HD art that was loaded without it: its picture is loaded again for it. */
Outline MakeArtOutline(SDL_Renderer *renderer, const HdArt &art, const Draw &sprite, OutlineCut cut)
{
	Outline none;
	none.tried = true;
	const SlowWorkScope timed(SlowWork::HdArt);
	SDL_Surface *picture = TakePicture(PicturePath(art.name), false);
	if (picture == nullptr)
		return none;
	Outline outline = OutlineFromPicture(renderer, picture, sprite, cut, !(FrameLit && sprite.caster != Caster::None));
	SDL_FreeSurface(picture);
	return outline;
}

/**
 * @brief An outline round a sprite drawn from the game's own pixels, made at the HD art's size so its outer edge is
 * smooth. As the game's, it goes round the sprite's colours, not its painted shadow (palette colour 0).
 */
Outline MakeRasterOutline(SDL_Renderer *renderer, const Draw &sprite, OutlineCut cut)
{
	const Point position { 0, sprite.height - 1 };
	Raster plain = PlainRaster(sprite, position);
	SplitShadow(plain, sprite, position, true);
	const bool shadowShown = !(FrameLit && sprite.caster != Caster::None);
	const int width = plain.width * Scale;
	const int height = plain.height * Scale;
	std::vector<uint8_t> body(static_cast<size_t>(width) * height, 0);
	std::vector<uint8_t> cover(body.size(), 0);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			const size_t from = static_cast<size_t>(y / Scale) * plain.width + x / Scale;
			const size_t i = static_cast<size_t>(y) * width + x;
			body[i] = plain.alpha[from] != 0 && plain.index[from] != 0 ? 1 : 0;
			// The painted shadow covers the outline only where it is drawn (HD Lighting casts its own instead).
			cover[i] = shadowShown || body[i] != 0 ? plain.alpha[from] : 0;
		}
	}
	return MakeOutline(renderer, body, cover, width, height, static_cast<float>(Scale), static_cast<float>(Scale), cut);
}

/** @brief Whether this frame may still make an outline: always one, then as many as fit in OutlineBudgetMs. */
bool MayMakeOutline()
{
	return !OutlineMadeThisFrame || SDL_GetPerformanceCounter() < OutlineBudgetEnd;
}

/** @brief A smooth outline round what a sprite draw shows, or nullptr when this frame has no time left to make it. */
const Outline *SmoothOutline(SDL_Renderer *renderer, const Draw &sprite, OutlineCut cut)
{
	const auto index = static_cast<size_t>(cut);
	if (sprite.light >= 0 || sprite.recolour != nullptr) {
		const ArtChoice choice = CachedArt(renderer, sprite);
		if (choice.art != nullptr) {
			Outline &outline = choice.art->outlines[index];
			if (!outline.tried) {
				if (!MayMakeOutline())
					return nullptr;
				outline = MakeArtOutline(renderer, *choice.art, sprite, cut);
				OutlineMadeThisFrame = true;
			}
			return &outline;
		}
	}
	Raster &raster = FallbackRaster(sprite, false);
	Outline &outline = raster.outlines[index];
	if (!outline.tried) {
		if (!MayMakeOutline())
			return nullptr;
		outline = MakeRasterOutline(renderer, sprite, cut);
		OutlineMadeThisFrame = true;
	}
	return &outline;
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
 * TileGain. Arches without HD art take the same colours, or they stand out in the walls' old colours.
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
	const Mood mood = CurrentMood();
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

/** @brief How a draw is lit: one colour, or colours that change from its left edge to its right, at up to four stops. */
struct Shading {
	std::array<Colour3, 4> colours {};
	/** @brief Where each colour is, as a fraction of the draw's width. */
	std::array<float, 4> stops { 0.F, 0.5F, 1.F, 1.F };
	int count = 1;
	/** @brief The colours along the draw's top edge, where they differ from those along its bottom (see FireLightUp). */
	bool fades = false;
	std::array<Colour3, 4> top {};
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
	shading.colours = { left, right, right, right };
	shading.stops = { 0.F, 1.F, 1.F, 1.F };
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

/** @brief The part of a light above full (fires and spells light things brighter than full), drawn as a second, added pass. */
Colour3 AboveFull(const Colour3 &colour)
{
	return { std::max(colour[0] - 1.F, 0.F), std::max(colour[1] - 1.F, 0.F), std::max(colour[2] - 1.F, 0.F) };
}

bool IsAboveFull(const Colour3 &colour)
{
	return colour[0] > 1.F + 1.F / 255 || colour[1] > 1.F + 1.F / 255 || colour[2] > 1.F + 1.F / 255;
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
	if (draw.light >= 0 || draw.recolour != nullptr)
		art = CachedArt(renderer, draw);
	if (art.flowing != nullptr)
		RefreshFlow(*art.flowing);
	out.texture = art.texture;
	out.hdArt = art.hdArt;
	out.tileColours = art.tileArt;
	// HD arches are made in the HD walls' palette, so they are evened out to this level's palette like the walls.
	if (art.hdArt && leveltype != DTYPE_TOWN && IsArchPiece(draw))
		out.tileColours = true;
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
	if (shading.count == 1 && !shading.fades) {
		const Colour3 colour = ArtColour(shading.colours[0], art);
		SDL_SetTextureColorMod(art.texture, ColourByte(colour[0]), ColourByte(colour[1]), ColourByte(colour[2]));
		SDL_SetTextureAlphaMod(art.texture, alpha);
		SDL_RenderCopyF(renderer, art.texture, nullptr, &dest);
		if (IsAboveFull(colour)) {
			const Colour3 above = AboveFull(colour);
			SDL_SetTextureColorMod(art.texture, ColourByte(above[0]), ColourByte(above[1]), ColourByte(above[2]));
			SDL_SetTextureBlendMode(art.texture, SDL_BLENDMODE_ADD);
			SDL_RenderCopyF(renderer, art.texture, nullptr, &dest);
			SDL_SetTextureBlendMode(art.texture, SDL_BLENDMODE_BLEND);
		}
		return;
	}
#if SDL_VERSION_ATLEAST(2, 0, 18)
	// Drawn as shapes with a colour at each corner (which leave the texture's own colour and alpha out).
	SDL_SetTextureColorMod(art.texture, 255, 255, 255);
	SDL_SetTextureAlphaMod(art.texture, 255);
	// A colour at each stop along the bottom edge, and along the top edge too where the light fades up the draw (one
	// colour fading up it is drawn as two stops, at its left and right edges).
	const int stops = std::max(shading.count, 2);
	std::array<SDL_Vertex, 8> vertices;
	std::array<Colour3, 8> colours;
	bool aboveFull = false;
	for (int s = 0; s < stops; s++) {
		const int from = std::min(s, shading.count - 1);
		const float fx = shading.count == 1 ? static_cast<float>(s) : shading.stops[s];
		colours[s * 2] = ArtColour(shading.fades ? shading.top[from] : shading.colours[from], art);
		colours[s * 2 + 1] = ArtColour(shading.colours[from], art);
		for (int edge = 0; edge < 2; edge++) {
			const Colour3 &colour = colours[s * 2 + edge];
			aboveFull = aboveFull || IsAboveFull(colour);
			const SDL_Color vertexColour { ColourByte(colour[0]), ColourByte(colour[1]), ColourByte(colour[2]), alpha };
			vertices[s * 2 + edge] = SDL_Vertex { { dest.x + fx * dest.w, dest.y + static_cast<float>(edge) * dest.h }, vertexColour, { fx, static_cast<float>(edge) } };
		}
	}
	std::array<int, 18> indices;
	int count = 0;
	for (int s = 0; s + 1 < stops; s++) {
		for (const int corner : { 0, 2, 3, 0, 3, 1 })
			indices[count++] = s * 2 + corner;
	}
	SDL_RenderGeometry(renderer, art.texture, vertices.data(), stops * 2, indices.data(), count);
	if (aboveFull) {
		for (int v = 0; v < stops * 2; v++) {
			const Colour3 above = AboveFull(colours[v]);
			vertices[v].color = SDL_Color { ColourByte(above[0]), ColourByte(above[1]), ColourByte(above[2]), alpha };
		}
		SDL_SetTextureBlendMode(art.texture, SDL_BLENDMODE_ADD);
		SDL_RenderGeometry(renderer, art.texture, vertices.data(), stops * 2, indices.data(), count);
		SDL_SetTextureBlendMode(art.texture, SDL_BLENDMODE_BLEND);
	}
#endif
}

/** @brief The draw of the sprite an outline goes round: the game draws an outline just before its sprite, a pixel up and left of it. */
const Draw *OutlinedSprite(size_t index)
{
	const Draw &outline = Draws[index];
	const size_t end = std::min(Draws.size(), index + 8);
	for (size_t i = index + 1; i < end; i++) {
		const Draw &draw = Draws[i];
		if (draw.kind == DrawKind::Sprite && !IsOutline(draw.style) && draw.sprite->pixelData() == outline.sprite->pixelData() && draw.x == outline.x + 1 && draw.y == outline.y + 1)
			return &draw;
	}
	return nullptr;
}

/** @brief Each thing's outline as last drawn, by OutlineThing, so the cursor's fades in. */
OutlineFader ShownOutlines;

/**
 * @brief Draws an outline of an item, monster or townsperson smooth, round the HD art its sprite shows or round the
 * game's own pixels scaled up, in the outline's colour; the game's own pixels until that outline is made. Its look
 * (OutlineLook) makes it thick, puts a slow glow under it, or fades it in under the cursor.
 */
void DrawOutline(SDL_Renderer *renderer, size_t index, const Shading &glow, float toLogical)
{
	const Draw &draw = Draws[index];
	const OutlineLook &look = draw.outlineLook;
	const Draw *sprite = draw.style == SpriteStyle::OutlineSkipZero ? OutlinedSprite(index) : nullptr;
	const Outline *outline = sprite != nullptr ? SmoothOutline(renderer, *sprite, OutlineCutFor(look)) : nullptr;
	if (outline == nullptr || outline->texture == nullptr) {
		DrawShaded(renderer, draw, glow, toLogical, true);
		return;
	}
	const SDL_Color &gameColour = logical_palette[draw.outlineColor];
	const Colour3 &light = glow.colours[0];
	Colour3 colour { gameColour.r / 255.F * light[0], gameColour.g / 255.F * light[1], gameColour.b / 255.F * light[2] };
	float alpha = 1.F;
	const uint32_t now = SDL_GetTicks();
	if (look.thing != 0)
		ShownOutlines.Fade(look, now, colour, alpha);
	const SDL_FRect at = DestOf(*sprite, toLogical);
	const float unit = static_cast<float>(Scale) * toLogical;
	const auto drawCut = [&](const Outline &cut, float strength) {
		const SDL_FRect dest { at.x + cut.place.x * unit, at.y + cut.place.y * unit, cut.place.w * unit, cut.place.h * unit };
		if (strength <= 0 || !OnScreen(dest, 0))
			return;
		SDL_SetTextureColorMod(cut.texture, ColourByte(colour[0]), ColourByte(colour[1]), ColourByte(colour[2]));
		SDL_SetTextureAlphaMod(cut.texture, ColourByte(strength));
		SDL_RenderCopyF(renderer, cut.texture, nullptr, &dest);
	};
	if (look.glow) {
		// The glow is made when there is time for it, like the outline; until then the outline shows alone.
		const Outline *halo = SmoothOutline(renderer, *sprite, OutlineCut::Glow);
		if (halo != nullptr && halo->texture != nullptr)
			drawCut(*halo, alpha * GlowStrength(now));
	}
	drawCut(*outline, alpha);
}

#if SDL_VERSION_ATLEAST(2, 0, 18)

constexpr float HalfTileWidth = TILE_WIDTH / 2.F;
constexpr float HalfTileHeight = TILE_HEIGHT / 2.F;
/** @brief A tile's length across the screen (its floor diamond's width over the square root of 2), in game pixels. */
constexpr float TileAcross = 45.254834F;
constexpr std::array<int, 6> QuadIndices { 0, 1, 2, 0, 2, 3 };

// HD Lighting's shadows follow the light the way real shadows do. Each light a hero or monster stands in casts its own
// shadow, straight away from the light. It reaches where the line from the light past each part of the body meets the
// floor: from a light hanging H tiles up and d tiles away, a body h tall casts a shadow d * h / (H - h) long, and a light
// at head height or lower casts it on until its light gives out or a wall stops it. The nearer the light, the wider the
// shadow's far end (it is the body as the light sees it). Its edge is sharp at the feet and softens further out, the
// more the wider the light's flame or glow. And it is as dark as that light's share of all the light there, added up as
// linear light, less what bounces in off the floor and walls round it anyway, and lighter where that light is dim. The
// heroes' own glows count for little in that share (GlowFill), so a fire or a spell beside a hero casts a clear shadow
// and a glow's own shadows stay faint.
/** @brief cos 30 degrees: Diablo's view shows a height at this much of its length (its floor is foreshortened by half). */
constexpr float ViewLean = 0.866F;
/** @brief How wide a hero's or monster's body is, in tiles, for how much of a wide light gets round it. */
constexpr float BodyWidth = 0.45F;
/** @brief How much of an object's width in its picture counts as its body across a light (the picture shows its depth too), and the most. */
constexpr float ObjectWidthShare = 0.75F;
constexpr float MaxObjectWidth = 1.6F;
/** @brief How much of an object's height in its picture is height (the rest is its top, seen from above). */
constexpr float ObjectHeightShare = 0.65F;
/**
 * @brief How far into a round object's footprint its shadow starts, as a share of the way from its middle to its side
 * away from the light: a little inside, so the shadow meets the object's base with no gap (the object hides the rest).
 */
constexpr float ObjectTuck = 0.8F;
/** @brief How far out round a block's base the floor is darker where the block keeps the room's bounced light off it (tiles). */
constexpr float BlockSkirt = 0.22F;
/** @brief The soft edge of a block's shadow at its base, and the least and most at its far end (tiles). */
constexpr float BlockEdge = 0.03F;
constexpr float BlockLeastSoft = 0.06F;
constexpr float BlockMostSoft = 0.55F;
/**
 * @brief A light this near a block's base (tiles) starts to count as over it; one over it is this far above its top,
 * and the shadow it leaves round the base has an edge at least this soft.
 */
constexpr float BlockOverEdge = 0.5F;
constexpr float BlockOverRise = 1.2F;
constexpr float BlockOverSoft = 0.4F;
/** @brief The smallest side of an object's footprint, in tiles. */
constexpr float ObjectLeastSide = 0.3F;
/** @brief Pieces a shadow is drawn in along its length, as its light and stretch change along it. */
constexpr int CastBands = 12;
/** @brief The room round the body in its shadow pictures, and how soft the sharp and soft pictures are (game pixels). */
constexpr int CastPad = 14;
constexpr float SharpBlur = 0.8F;
constexpr float SoftBlur = 7.F;
/** @brief The soft edge (tiles from light to dark) the soft picture stands for: 2.56 of its blur. */
constexpr float SoftWidth = 0.4F;
/** @brief The furthest a shadow is drawn, in tiles. */
constexpr float CastReach = 10.F;
/**
 * @brief About this much of the light anywhere has bounced off the room (stone walls round a room give back a fifth to
 * two fifths of what falls on them), and a body keeps this much of it off the floor right under it, fading out about a
 * body's width round it (the dark patch where something stands on the floor).
 */
constexpr float BounceShare = 0.3F;
constexpr float FootShade = 0.8F;
/** @brief How far below the tile's centre the middle of the spot under the feet is (the art stands its feet a little below it). */
constexpr float ContactDrop = 5.F;
/** @brief The hero is never drawn darker than this much of full light (the game always draws them fully lit). */
constexpr float HeroLightFloor = 0.85F;
/**
 * @brief Under Tristram's evening sky about this much of the light anywhere comes from all round the sky, and a body
 * keeps it off the ground right under it, as it does the light bouncing in under it in the dungeon (BounceShare).
 */
constexpr float SkyShade = 0.5F;
/** @brief How high a game pixel up the screen stands, in tiles (see ViewLean). */
constexpr float RisePerPixel = 1.F / (ViewLean * TileAcross);
/**
 * @brief Tristram's fires burn low (the forge's coals, the hearth behind the tavern's door), so their light reaches the
 * walls round them but not the roofs above: it fades out from this high up a wall (in tiles above its floor) over this
 * much more, and higher up there is only the sky's light.
 */
constexpr float FireLightHigh = 1.2F;
constexpr float FireLightFade = 1.6F;
/**
 * @brief The heroes' own glows are the light the game gives every hero to see by, kept as bright as the game has them. A
 * real fire or spell would outshine such a glow many times over, so inside the shadow of a light at least StrongLight
 * bright a glow fills in only this much of its light, and inside a dimmer light's shadow more, up to all of it.
 */
constexpr float GlowFill = 0.06F;
/** @brief A light at least this bright where its shadow falls (linear light, 1 is full) outshines the glows (GlowFill). */
constexpr float StrongLight = 0.2F;
/** @brief The widest a cast shadow fans out past the body's own width (a flame near head height would spread it to nothing). */
constexpr float MaxSpread = 1.6F;
/** @brief The view's across direction on the floor (screen right is +x, -y), per tile of length. */
constexpr float ViewAcross = 0.70710678F;
/** @brief Up to this height (tiles) a body's shadow turns from where the view draws its feet to the light's layout. */
constexpr float FeetBlend = 0.45F;
/** @brief How near a light is to passing right beside a body (cosine) for its shadow's two layouts to cross-fade. */
constexpr float TurnBand = 0.2F;
/** @brief The most of a cast shadow drawn with the soft picture, so some of the body's shape always shows in it. */
constexpr float MaxSoftness = 0.7F;
/** @brief How dark a shadow is where its light is all the light there (less the light's own bounce fill). */
constexpr float CastStrength = 0.85F;

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

/**
 * @brief The wall a shadow runs into (RoomFor), where the wall's face is turned to the view: it stands on the back edge
 * of the tile the shadow comes over, along x = `at` (facing +x) or along y = `at` (facing +y).
 */
struct WallHit {
	bool found = false;
	bool facesX = false;
	float at = 0;
};

/**
 * @brief How far (in tiles) a shadow can lie on the floor from a point in a direction before a wall stops it.
 * @param hit Told which wall that is, where its face is turned to the view (the shadow goes on up it, see WallShade).
 */
float RoomFor(float x, float y, float dx, float dy, float limit, WallHit *hit = nullptr)
{
	constexpr float Step = 1.F / 16;
	Point previous { static_cast<int>(std::floor(x + 0.5F)), static_cast<int>(std::floor(y + 0.5F)) };
	for (float t = Step; t < limit; t += Step) {
		const Point current { static_cast<int>(std::floor(x + dx * t + 0.5F)), static_cast<int>(std::floor(y + dy * t + 0.5F)) };
		if (current == previous)
			continue;
		if (CrossesWall(previous, current)) {
			if (hit != nullptr) {
				const BackWalls walls = BackWallsOf(previous);
				if (current.x < previous.x && walls.left && (current.y == previous.y || !(current.y < previous.y && walls.right && std::abs(dy) > std::abs(dx))))
					*hit = WallHit { true, true, static_cast<float>(previous.x) - 0.5F };
				else if (current.y < previous.y && walls.right)
					*hit = WallHit { true, false, static_cast<float>(previous.y) - 0.5F };
			}
			return t;
		}
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

/** @brief Where the hero being played stands this frame (in tiles), NaN when they aren't drawn. */
float LitHeroX = std::numeric_limits<float>::quiet_NaN();
float LitHeroY = std::numeric_limits<float>::quiet_NaN();

/** @brief Walls with their back to the hero take the light of the hero's side fully up to this far from the hero (in tiles)... */
constexpr float BehindFull = 6.F;
/** @brief ...and none from this far, where the hero's light has faded. */
constexpr float BehindNone = 9.F;

/**
 * @brief How much a wall's face takes the light of the room behind it as well as its own, 0 to 1. The face the view sees
 * of a wall in front of a room is its outer side, which the room's light doesn't reach. It takes the room's light where
 * the game draws the walls see-through (DrawCell's test), as they stand between the view and the room they belong to,
 * and where the faces look out onto solid rock, as the outer side of the dungeon's walls does, which no light reaches.
 * So do the faces near the hero with their back to them: the walls the game draws solid between the view and the hero
 * (the posts where walls meet, the next room's walls), which showed black beside the room's light. Standing in the
 * wall's own row, in a doorway or where two walls meet, counts as behind it: the faces beside the hero look away from
 * them too, and showed black on both sides of the doorway.
 * @param right The face on the tile's y-min edge (looking towards +y), else the one on its x-min edge (towards +x).
 */
float FromBehind(Point tile, bool right)
{
	if (TileHasAny(tile, TileProperties::Transparent) && TransList[static_cast<uint8_t>(dTransVal[tile.x][tile.y])])
		return 1.F;
	bool facesRock = true;
	for (const Displacement step : { Displacement { 1, 0 }, Displacement { 0, 1 }, Displacement { 1, 1 } }) {
		const Point front = tile + step;
		if (InDungeonBounds(front) && !light::Blocks(front.x, front.y)) {
			facesRock = false;
			break;
		}
	}
	if (facesRock)
		return 1.F;
	if (std::isnan(LitHeroX))
		return 0.F;
	// The middle of the face, how far behind the front of the wall's tile the hero stands (fully from half a tile, so all
	// through a doorway in it), and how near they are.
	const float fx = static_cast<float>(tile.x) - (right ? 0.F : 0.5F);
	const float fy = static_cast<float>(tile.y) - (right ? 0.5F : 0.F);
	const float behind = right ? static_cast<float>(tile.y) + 0.5F - LitHeroY : static_cast<float>(tile.x) + 0.5F - LitHeroX;
	const float dx = fx - LitHeroX;
	const float dy = fy - LitHeroY;
	const float closeness = (BehindNone - std::sqrt(dx * dx + dy * dy)) / (BehindNone - BehindFull);
	return std::clamp(behind * 2, 0.F, 1.F) * std::clamp(closeness, 0.F, 1.F);
}

/** @brief How much each of a tile's back walls takes the light of the room behind it (FromBehind). */
struct BehindWalls {
	float left = 0;
	float right = 0;
};

BehindWalls BehindWallsOf(Point tile, BackWalls walls)
{
	return { walls.left ? FromBehind(tile, false) : 0.F, walls.right ? FromBehind(tile, true) : 0.F };
}

/** @brief The light of the room behind a wall at a point just behind its face (light::BehindAt). */
Colour3 BehindLight(float x, float y, bool right)
{
	return ToColour(light::BehindAt(x, y, right));
}

/** @brief How far (in tiles) the room's light reaches past the foot of a see-through wall, fading out. */
constexpr float SpillDepth = 0.5F;

/**
 * @brief The light on the floor in front of a tile's walls. In front of walls lit from behind it is outside the room
 * the walls belong to, so the room's light spills a little way past the walls' foot and lights the bottoms of their
 * columns.
 */
Colour3 BaseFloorLight(float x, float y, Point tile, BackWalls walls, BehindWalls behind)
{
	Colour3 light = FloorLightAt(x, y, tile, walls);
	const float tx = static_cast<float>(tile.x);
	const float ty = static_cast<float>(tile.y);
	const auto spill = [&](float distance, float share, const Colour3 &room) {
		const float weight = share * std::clamp(1.F - distance / SpillDepth, 0.F, 1.F);
		for (int c = 0; c < 3; c++)
			light[c] = std::max(light[c], room[c] * weight);
	};
	if (behind.left > 0)
		spill(x - (tx - 0.5F), behind.left, BehindLight(tx - 0.75F, std::clamp(y, ty - 0.5F, ty + 0.5F), false));
	if (behind.right > 0)
		spill(y - (ty - 0.5F), behind.right, BehindLight(std::clamp(x, tx - 0.5F, tx + 0.5F), ty - 0.75F, true));
	return light;
}

/**
 * @brief Draws shapes again with the part of their light above full added on top, where any corner has some (beside
 * a fire or a spell): `colours` are each corner's light.
 */
template <size_t N>
void DrawAboveFull(SDL_Renderer *renderer, SDL_Texture *texture, std::array<SDL_Vertex, N> &vertices, const std::array<Colour3, N> &colours, int count, const int *indices, int indexCount)
{
	bool any = false;
	for (int i = 0; i < count; i++)
		any = any || IsAboveFull(colours[static_cast<size_t>(i)]);
	if (!any)
		return;
	for (int i = 0; i < count; i++) {
		const Colour3 above = AboveFull(colours[static_cast<size_t>(i)]);
		SDL_Vertex &vertex = vertices[static_cast<size_t>(i)];
		vertex.color = SDL_Color { ColourByte(above[0]), ColourByte(above[1]), ColourByte(above[2]), vertex.color.a };
	}
	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_ADD);
	SDL_RenderGeometry(renderer, texture, vertices.data(), count, indices, indexCount);
	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
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
 * @brief Draws the floor half of a tile's bottom piece in the light over the floor, and the floor's edge below it.
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
	const BehindWalls behind = BehindWallsOf(draw.cell, walls);
	const float tx = static_cast<float>(draw.cell.x);
	const float ty = static_cast<float>(draw.cell.y);
	// The half diamond's corner where its two outer edges meet, and points along those edges at fractions a and b:
	// left half, from (0, 1/2) towards the top (1, 0) and the bottom (1, 1); right half, from (1, 1/2) towards (0, 0) and (0, 1).
	static constexpr std::array<float, 4> Steps { 0.F, 0.25F, 0.75F, 1.F };
	std::array<SDL_Vertex, 11> vertices;
	std::array<Colour3, 11> colours;
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
			const Colour3 colour = ArtColour(BaseFloorLight(wx, wy, draw.cell, walls, behind), art);
			colours[static_cast<size_t>(count)] = colour;
			vertices[count] = SDL_Vertex {
				{ dest.x + fx * dest.w, dest.y + fy * dest.h },
				{ ColourByte(colour[0]), ColourByte(colour[1]), ColourByte(colour[2]), 255 },
				{ fx, fy },
			};
			at[ia][ib] = count++;
		}
	}
	std::array<int, 36> indices;
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
	// Below the floor's front edge (a = 0), the piece holds the floor's own edge, which juts a little past the floor's
	// shape. It goes with the floor, before any wall, so the tiles in front are drawn over it as in the game: drawn with
	// the walls it showed on top of them as a thin dotted line.
	{
		const float cornerX = right ? 1.F : 0.F;
		float wx;
		float wy;
		BaseWorldOf(right, cornerX, 1.F, draw.cell, wx, wy);
		const Colour3 colour = ArtColour(BaseFloorLight(wx, wy, draw.cell, walls, behind), art);
		colours[static_cast<size_t>(count)] = colour;
		vertices[count] = SDL_Vertex {
			{ dest.x + cornerX * dest.w, dest.y + dest.h },
			{ ColourByte(colour[0]), ColourByte(colour[1]), ColourByte(colour[2]), 255 },
			{ cornerX, 1.F },
		};
		const int corner = count++;
		for (int ib = 0; ib < 3; ib++) {
			for (const int v : { corner, at[0][ib], at[0][ib + 1] })
				indices[used++] = v;
		}
	}
	SDL_RenderGeometry(renderer, art.texture, vertices.data(), count, indices.data(), used);
	DrawAboveFull(renderer, art.texture, vertices, colours, count, indices.data(), used);
}

/**
 * @brief Draws the rest of a tile's bottom piece: the foot of its wall, above the floor's back edge, lit like the wall's
 * face. Where the piece is only floor, it is lit like the floor: it holds the floor's own edge, which juts a little past
 * the floor's shape. (Below the front edge goes with the floor: DrawTileFloor.)
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
	static constexpr std::array<std::array<float, 2>, 3> LeftHalf { { { 0, 0 }, { 1, 0 }, { 0, 0.5F } } };
	static constexpr std::array<std::array<float, 2>, 3> RightHalf { { { 0, 0 }, { 1, 0 }, { 1, 0.5F } } };
	const bool right = IsRightColumn(draw);
	const auto &corners = right ? RightHalf : LeftHalf;
	const BackWalls walls = BackWallsOf(draw.cell);
	const BehindWalls behind = BehindWallsOf(draw.cell, walls);
	const bool floorOnly = IsFloorOnly(draw);
	std::array<SDL_Vertex, 3> vertices;
	std::array<Colour3, 3> colours;
	for (size_t i = 0; i < corners.size(); i++) {
		const float fx = corners[i][0];
		const float fy = corners[i][1];
		Colour3 light;
		if (!floorOnly) {
			light = ShadeAt(shading, fx);
		} else {
			float wx;
			float wy;
			BaseWorldOf(right, fx, fy, draw.cell, wx, wy);
			light = BaseFloorLight(wx, wy, draw.cell, walls, behind);
		}
		const Colour3 colour = ArtColour(light, art);
		colours[i] = colour;
		vertices[i] = SDL_Vertex {
			{ dest.x + fx * dest.w, dest.y + fy * dest.h },
			{ ColourByte(colour[0]), ColourByte(colour[1]), ColourByte(colour[2]), 255 },
			{ fx, fy },
		};
	}
	SDL_RenderGeometry(renderer, art.texture, vertices.data(), static_cast<int>(vertices.size()), nullptr, 0);
	DrawAboveFull(renderer, art.texture, vertices, colours, static_cast<int>(vertices.size()), nullptr, 0);
}

/**
 * @brief The Cathedral's dirt (drlg_l1.cpp's Dirt tiles): the ground outside its rooms, its edges along a floor's open
 * sides and corners, and beside the ends of walls. The level is filled with it before the rooms are cut out. 195 and 196
 * are the dirt beside the doorway of King Leoric's tomb (skngdo.dun).
 */
constexpr std::array<uint8_t, 16> CathedralDirt { 18, 19, 20, 21, 22, 23, 24, 195, 196, 199, 200, 201, 202, 203, 204, 205 };

/** @brief What a piece of the dirt is (DirtPieces). */
enum class DirtKind : uint8_t {
	None,
	/** @brief The tomb's dirt (195, 196): it stands in the tomb's wall beside the doorway, so light stops at it. */
	Walled,
	/** @brief Light passes over it (light::SetOpenGround). */
	Open,
};

/** @brief The pieces the dirt is drawn with, by piece number, worked out from the level's own tiles (empty elsewhere). */
std::vector<DirtKind> DirtPieces;
const MegaTile *DirtMegaTiles = nullptr;
dungeon_type DirtLevelType = DTYPE_NONE;

void IndexDirt()
{
	if (DirtMegaTiles == pMegaTiles.get() && DirtLevelType == leveltype)
		return;
	DirtMegaTiles = pMegaTiles.get();
	DirtLevelType = leveltype;
	DirtPieces.clear();
	if (leveltype != DTYPE_CATHEDRAL || DirtMegaTiles == nullptr)
		return;
	DirtPieces.assign(MAXTILES, DirtKind::None);
	for (const uint8_t tile : CathedralDirt) {
		const DirtKind kind = IsAnyOf(tile, 195, 196) ? DirtKind::Walled : DirtKind::Open;
		const MegaTile &mega = DirtMegaTiles[tile - 1];
		for (const uint16_t piece : { Swap16LE(mega.micro1), Swap16LE(mega.micro2), Swap16LE(mega.micro3), Swap16LE(mega.micro4) }) {
			// Only its solid pieces: the tomb's dirt stands on a piece of plain floor that other tiles share.
			if (piece >= MAXTILES || !HasAnyOf(SOLData[piece], TileProperties::Solid))
				continue;
			// A piece the tomb's dirt shares is the tomb's.
			DirtKind &was = DirtPieces[piece];
			if (was == DirtKind::None || kind < was)
				was = kind;
		}
	}
}

DirtKind DirtAt(Point cell)
{
	if (DirtPieces.empty() || !InDungeonBounds(cell))
		return DirtKind::None;
	return DirtPieces[dPiece[cell.x][cell.y]];
}

bool IsDirt(Point cell)
{
	return DirtAt(cell) != DirtKind::None;
}

/** @brief The dirt outside the rooms that light passes over (see light::SetOpenGround). */
bool IsOpenGround(int x, int y)
{
	return DirtAt({ x, y }) == DirtKind::Open;
}

/**
 * @brief Draws a piece of the dirt in the light on the floor where it shows.
 *
 * The game draws its field of dirt two rows above the tiles it belongs to, so on screen it lies beside the floor as one
 * flat ground, with the stone rim of the floor's open edge on it. Each point of it takes the light on the floor's plane
 * at that point of the screen, as the floor does, in small triangles so the light changes smoothly across it and its
 * pieces meet without a seam. Lit by its own tile, each piece showed as a square of one shade. Behind a wall it takes
 * none of the light the wall's face takes from the room in front of it (light::GroundAt): beside a doorway in a wall
 * (the tomb's), that showed as a lit patch of dirt like a way in.
 */
void DrawGroundLit(SDL_Renderer *renderer, const Draw &draw, float toLogical)
{
	const SDL_FRect dest = DestOf(draw, toLogical);
	if (!OnScreen(dest, 0))
		return;
	const DrawTexture art = TextureFor(renderer, draw, false);
	if (art.texture == nullptr)
		return;
	SDL_SetTextureColorMod(art.texture, 255, 255, 255);
	SDL_SetTextureAlphaMod(art.texture, 255);
	// No darker than the dirt is on its own tile, a tile in front of where it shows: beside the end of a wall it stands
	// up over the floor behind the wall, which the wall can leave dark, and it showed black beside a hero at a room's
	// open corner. Not the tomb's dirt, which stands in its wall.
	const bool own = DirtAt(draw.cell) == DirtKind::Open;
	// Half a piece (16 game pixels) apart, as far apart as the light's samples: closer looks the same and costs more.
	constexpr int Steps = 2;
	constexpr int Row = Steps + 1;
	std::array<SDL_Vertex, Row * Row> vertices;
	std::array<Colour3, Row * Row> colours;
	for (int j = 0; j <= Steps; j++) {
		for (int i = 0; i <= Steps; i++) {
			const float fx = static_cast<float>(i) / Steps;
			const float fy = static_cast<float>(j) / Steps;
			float wx;
			float wy;
			WorldOf(static_cast<float>(draw.x) + fx * static_cast<float>(draw.width), static_cast<float>(draw.y) + fy * static_cast<float>(draw.height), wx, wy);
			const size_t k = static_cast<size_t>(j * Row + i);
			light::Rgb ground = light::GroundAt(wx, wy);
			if (own) {
				const light::Rgb there = light::TileGroundAt(wx + 1, wy + 1, draw.cell.x, draw.cell.y);
				ground = { std::max(ground.r, there.r), std::max(ground.g, there.g), std::max(ground.b, there.b) };
			}
			colours[k] = ArtColour(ToColour(ground), art);
			vertices[k] = SDL_Vertex {
				{ dest.x + fx * dest.w, dest.y + fy * dest.h },
				{ ColourByte(colours[k][0]), ColourByte(colours[k][1]), ColourByte(colours[k][2]), 255 },
				{ fx, fy },
			};
		}
	}
	std::array<int, Steps * Steps * 6> indices;
	int used = 0;
	for (int j = 0; j < Steps; j++) {
		for (int i = 0; i < Steps; i++) {
			const int corner = j * Row + i;
			for (const int v : { corner, corner + 1, corner + Row + 1, corner, corner + Row + 1, corner + Row })
				indices[static_cast<size_t>(used++)] = v;
		}
	}
	SDL_RenderGeometry(renderer, art.texture, vertices.data(), Row * Row, indices.data(), used);
	DrawAboveFull(renderer, art.texture, vertices, colours, Row * Row, indices.data(), used);
}

/**
 * @brief Lights the floor drawn so far by the light over it, in one go: darker where it is dim, and brighter than full
 * where fires and spells light it. A renderer that can't blend that way lights it up to full.
 */
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
		FieldMultiplies = SDL_SetTextureBlendMode(FieldTexture, SDL_BLENDMODE_MUL) == 0;
		if (!FieldMultiplies)
			SDL_SetTextureBlendMode(FieldTexture, SDL_BLENDMODE_MOD);
		SDL_SetTextureScaleMode(FieldTexture, SDL_ScaleModeLinear);
	}
	const SDL_Rect part { 0, 0, width, height };
	SDL_UpdateTexture(FieldTexture, &part, FieldMultiplies ? light::FieldMulPixels() : light::FieldPixels(), width * 4);
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

/** @brief A coverage mask (0 to 1 per pixel) at half size, each pixel the average of a 2x2 block. */
std::vector<float> Halved(const std::vector<float> &cover, int width, int height, int &halfWidth, int &halfHeight)
{
	halfWidth = (width + 1) / 2;
	halfHeight = (height + 1) / 2;
	std::vector<float> half(static_cast<size_t>(halfWidth) * halfHeight, 0.F);
	for (int y = 0; y < halfHeight; y++) {
		for (int x = 0; x < halfWidth; x++) {
			float sum = 0;
			int n = 0;
			for (int sy = y * 2; sy < std::min(height, y * 2 + 2); sy++) {
				for (int sx = x * 2; sx < std::min(width, x * 2 + 2); sx++) {
					sum += cover[static_cast<size_t>(sy) * width + sx];
					n++;
				}
			}
			half[static_cast<size_t>(y) * halfWidth + x] = sum / static_cast<float>(std::max(n, 1));
		}
	}
	return half;
}

/**
 * @brief A shape from a coverage mask (0 to 1 per pixel), for drawing shadows with: white, so it draws black as a shadow
 * (in a black colour) or adds up as cover (JoinedShadows).
 */
SDL_Texture *ShapeTexture(SDL_Renderer *renderer, const std::vector<float> &cover, int width, int height)
{
	std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4, 255);
	for (size_t k = 0; k < cover.size(); k++)
		rgba[k * 4 + 3] = static_cast<uint8_t>(std::clamp(cover[k], 0.F, 1.F) * 255.F + 0.5F);
	SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height);
	if (texture == nullptr)
		return nullptr;
	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
	SDL_UpdateTexture(texture, nullptr, rgba.data(), width * 4);
	return texture;
}

/**
 * @brief The part of a coverage mask that shows once it is a texture (see ShapeTexture), with a pixel to spare all round
 * for smoothing: a sprite's frame is often much wider and taller than its body, and its shadow only needs the body.
 */
CastPart PartOf(const std::vector<float> &cover, int width, int height)
{
	int left = width;
	int right = 0;
	int top = height;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			if (cover[static_cast<size_t>(y) * width + x] < 0.5F / 255)
				continue;
			left = std::min(left, x);
			right = std::max(right, x + 1);
			top = std::min(top, y);
		}
	}
	if (right <= left)
		return CastPart { 0, 0, 1 };
	return CastPart {
		static_cast<float>(std::max(left - 1, 0)) / static_cast<float>(width),
		static_cast<float>(std::min(right + 1, width)) / static_cast<float>(width),
		static_cast<float>(std::max(top - 1, 0)) / static_cast<float>(height),
	};
}

/**
 * @brief The sprite's body as a black shape at half size, with CastPad game pixels of room around it, for the shadows it
 * casts: sharp (castTexture) for where a shadow starts at the feet, and soft (castSoft) for where it has spread out.
 */
bool CastTextures(SDL_Renderer *renderer, Raster &raster)
{
	if (raster.castTried)
		return raster.castTexture != nullptr && raster.castSoft != nullptr;
	raster.castTried = true;
	const int w = raster.width;
	const int h = raster.height;
	if (w <= 0 || h <= 0)
		return false;
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
		return false;
	raster.bodyTop = top;
	// The sharp shape keeps limbs a pixel thick (a skeleton's bones) so they survive halving.
	std::vector<float> body(mask.size(), 0.F);
	for (int y = 1; y + 1 < height; y++) {
		for (int x = 1; x + 1 < width; x++) {
			const size_t k = static_cast<size_t>(y) * width + x;
			body[k] = std::max({ mask[k], mask[k - 1], mask[k + 1], mask[k - static_cast<size_t>(width)], mask[k + static_cast<size_t>(width)] });
		}
	}
	int halfWidth;
	int halfHeight;
	const std::vector<float> sharp = Halved(Blur(body, width, height, SharpBlur), width, height, halfWidth, halfHeight);
	// The soft shape is blurred at half size, where the same softness takes a quarter of the work.
	const std::vector<float> soft = Blur(Halved(mask, width, height, halfWidth, halfHeight), halfWidth, halfHeight, SoftBlur / 2);
	raster.castTexture = ShapeTexture(renderer, sharp, halfWidth, halfHeight);
	raster.castSoft = ShapeTexture(renderer, soft, halfWidth, halfHeight);
	raster.sharpPart = PartOf(sharp, halfWidth, halfHeight);
	raster.softPart = PartOf(soft, halfWidth, halfHeight);
	return raster.castTexture != nullptr && raster.castSoft != nullptr;
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

/** @brief How much a shadow darkens the floor (0 to 1) where it keeps `removed` of the `total` linear light there off it. */
float ShadowAlpha(float total, float removed)
{
	if (total <= 1e-6F || removed <= 0)
		return 0;
	const float lit = light::Drawn(total);
	const float shaded = light::Drawn(std::max(total - removed, 0.F));
	return lit > 1e-4F ? std::clamp(1.F - shaded / lit, 0.F, 1.F) : 0.F;
}

/**
 * @brief How much a light's cast shadow darkens the floor (0 to 1): the share of the light there that its light gives,
 * less its bounce fill, with the heroes' glows filling in less the brighter that light is (GlowFill). A glow's own
 * shadows stay as faint as its light makes them.
 * @param total All the light there, as linear light.
 * @param glows The part of it from the heroes' own glows.
 * @param own The part of it from the shadow's light (one of the glows when `glow`).
 * @param kept How much of the light the body keeps off there (a wide light gets round it further out).
 * @param atBody The shadow's light where the body stands.
 */
float CastAlpha(float total, float glows, float own, bool glow, float fill, float kept, float atBody)
{
	if (glow)
		return ShadowAlpha(total, (1 - fill) * own * kept);
	if (own <= 0)
		return 0;
	// A body standing in a flame's strong light casts that flame's shadow all the way out, though the light has
	// thinned where the shadow lands and the hero's own glow is most of the light there.
	const float outshines = std::min(std::max(own, atBody) / StrongLight, 1.F);
	const float glowsLeft = glows * (1 - outshines * (1 - GlowFill));
	const float others = std::max(total - glows - own, 0.F);
	return std::clamp(CastStrength * (1 - fill) * kept * own / (own + glowsLeft + others), 0.F, 1.F);
}

/**
 * @brief How much one light's cast shadow darkens a point of the floor (CastAlpha). A wall's own tile has floor in front
 * of the wall, where the game's light doesn't go (the tile is solid): a point there takes the light of the open floor
 * next to it on the light's side, so a shadow doesn't fade out just before a wall's foot.
 */
float CastAlphaAt(const light::ShadowLight &source, float x, float y, float kept, float atBody)
{
	float dx = source.x - x;
	float dy = source.y - y;
	const float away = std::sqrt(dx * dx + dy * dy);
	if (away > 1e-3F) {
		dx /= away;
		dy /= away;
		for (int step = 0; step < 3 && light::Blocks(static_cast<int>(std::floor(x + 0.5F)), static_cast<int>(std::floor(y + 0.5F))); step++) {
			x += dx * 0.55F;
			y += dy * 0.55F;
		}
	}
	return CastAlpha(light::LinearAt(x, y), light::GlowAt(x, y), light::LinearOf(source, x, y), source.glow, source.fill, kept, atBody);
}

/** @brief The strongest lights on a world point (light::ShadowLights), for a body's shadows and an object's sides. */
struct LightsOn {
	bool known = false;
	float x = 0;
	float y = 0;
	int count = 0;
	std::array<light::ShadowLight, 3> lights;
	light::ShadowLight overhead;
};

/** @brief The last point asked for this frame: an object asks twice, for its shadows and then for its sides. */
LightsOn LastLightsOn;

const LightsOn &LightsOnPoint(float wx, float wy)
{
	LightsOn &on = LastLightsOn;
	if (on.known && on.x == wx && on.y == wy)
		return on;
	on.known = true;
	on.x = wx;
	on.y = wy;
	on.count = light::ShadowLights(wx, wy, on.lights.data(), static_cast<int>(on.lights.size()), on.overhead);
	return on;
}

/** @brief Where a sprite's body lies in its picture, in game pixels, worked out once (a row or column of nothing but its painted shadow is not body). */
void BodyBounds(Raster &raster)
{
	if (raster.boundLeft >= 0)
		return;
	int left = raster.width;
	int right = -1;
	int top = raster.height;
	int bottom = -1;
	for (int y = 0; y < raster.height; y++) {
		for (int x = 0; x < raster.width; x++) {
			if (raster.alpha[static_cast<size_t>(y) * raster.width + x] < 128)
				continue;
			left = std::min(left, x);
			right = std::max(right, x);
			top = std::min(top, y);
			bottom = std::max(bottom, y);
		}
	}
	if (right < 0) {
		left = 0;
		right = std::max(raster.width - 1, 0);
		top = 0;
		bottom = std::max(raster.height - 1, 0);
	}
	int cornerSum = 0;
	int cornerCount = 0;
	for (int x = left; x <= right && bottom >= 0; x++) {
		if (raster.alpha[static_cast<size_t>(bottom) * raster.width + x] >= 128) {
			cornerSum += x;
			cornerCount++;
		}
	}
	raster.boundLeft = left;
	raster.boundRight = right;
	raster.boundTop = top;
	raster.boundBottom = bottom;
	raster.boundCorner = cornerCount > 0 ? cornerSum / cornerCount : (left + right) / 2;
}

/**
 * @brief A solid object's place on the floor, read from its picture: the lowest point of its outline is its front corner,
 * the width to the left of that its length along x and to the right its length along y (a tile's side is half a tile's
 * width in the picture). Its middle can lie off the tile it stands on (a sarcophagus covers the tile behind it too).
 */
struct Footprint {
	/** @brief How wide the body is across a light, in tiles (a barrel's is about a hero's, a sarcophagus's three times it). */
	float bodyWidth = BodyWidth;
	/** @brief The middle of its base from the point it stands on, in tiles. */
	float middleX = 0;
	float middleY = 0;
	/** @brief Half its base's sides, in tiles. */
	float halfX = 0;
	float halfY = 0;
	/** @brief How tall it is as a block, in tiles. */
	float boxTall = 0;
};

Footprint FootprintOf(Raster &raster)
{
	BodyBounds(raster);
	const float w = static_cast<float>(raster.width);
	const float feet = static_cast<float>(raster.height) - 1 - HalfTileHeight;
	Footprint foot;
	const float pictureWidth = static_cast<float>(raster.boundRight - raster.boundLeft + 1) / TileAcross;
	foot.bodyWidth = std::clamp(pictureWidth * ObjectWidthShare, BodyWidth, MaxObjectWidth);
	const float lengthX = std::max(static_cast<float>(raster.boundCorner - raster.boundLeft) / HalfTileWidth, ObjectLeastSide);
	const float lengthY = std::max(static_cast<float>(raster.boundRight - raster.boundCorner) / HalfTileWidth, ObjectLeastSide);
	const float cornerAcross = (static_cast<float>(raster.boundCorner) - w / 2) / HalfTileWidth;
	const float cornerDown = std::max(static_cast<float>(raster.boundBottom) - feet, 0.F) / (HalfTileWidth / 2);
	const float cornerX = (cornerAcross + cornerDown) / 2;
	const float cornerY = (cornerDown - cornerAcross) / 2;
	foot.halfX = lengthX / 2;
	foot.halfY = lengthY / 2;
	foot.middleX = cornerX - foot.halfX;
	foot.middleY = cornerY - foot.halfY;
	// The picture is as high as the footprint's far corner lies up the screen from its near one, and the block's own
	// height on top of that.
	const float footprintRise = (lengthX + lengthY) * HalfTileHeight;
	foot.boxTall = std::max((static_cast<float>(raster.boundBottom - raster.boundTop) - footprintRise) * RisePerPixel, ObjectLeastSide);
	return foot;
}

/** @brief The most corners a shape on the floor has (DrawFloorShade): a block's base and its top as a light throws it. */
constexpr int MaxShadeCorners = 8;

/** @brief The middle of a shape's corners on the floor. */
SDL_FPoint FloorMiddle(const SDL_FPoint *corners, int count)
{
	SDL_FPoint middle { 0, 0 };
	for (int i = 0; i < count; i++) {
		middle.x += corners[i].x / static_cast<float>(count);
		middle.y += corners[i].y / static_cast<float>(count);
	}
	return middle;
}

/**
 * @brief Darkens a shape on the floor: `count` corners in order round it (world tiles), each as dark as its `alphas`
 * and fading to nothing `feathers` tiles outside it, and its middle (FloorMiddle) as dark as `middle`. As `cover` it is
 * added up in white instead, for a light's joined shadows (JoinedShadows).
 */
void DrawFloorShade(SDL_Renderer *renderer, const SDL_FPoint *corners, const float *alphas, const float *feathers, int count, float middle, float toLogical, bool cover = false)
{
	if (count < 3 || count > MaxShadeCorners)
		return;
	float most = middle;
	for (int i = 0; i < count; i++)
		most = std::max(most, alphas[i]);
	if (most <= 0.004F)
		return;
	const SDL_FPoint centre = FloorMiddle(corners, count);
	const float cx = centre.x;
	const float cy = centre.y;
	const auto placed = [&](float x, float y) {
		float bx;
		float by;
		BufferOf(x, y, bx, by);
		return OnScreenAt(bx, by, toLogical);
	};
	// The middle, the corners, then the corners moved out by their soft edge.
	std::array<SDL_Vertex, 1 + MaxShadeCorners * 2> vertices;
	std::array<int, MaxShadeCorners * 9> indices;
	const Uint8 shade = cover ? 255 : 0;
	vertices[0] = SDL_Vertex { placed(cx, cy), { shade, shade, shade, ColourByte(middle) }, { 0, 0 } };
	for (int i = 0; i < count; i++) {
		const float ox = corners[i].x - cx;
		const float oy = corners[i].y - cy;
		const float out = std::sqrt(ox * ox + oy * oy);
		const float grow = out > 1e-4F ? feathers[i] / out : 0.F;
		vertices[static_cast<size_t>(1 + i)] = SDL_Vertex { placed(corners[i].x, corners[i].y), { shade, shade, shade, ColourByte(alphas[i]) }, { 0, 0 } };
		vertices[static_cast<size_t>(1 + count + i)] = SDL_Vertex { placed(corners[i].x + ox * grow, corners[i].y + oy * grow), { shade, shade, shade, 0 }, { 0, 0 } };
	}
	int used = 0;
	for (int i = 0; i < count; i++) {
		const int a = 1 + i;
		const int b = 1 + (i + 1) % count;
		for (const int v : { 0, a, b, a, a + count, b + count, a, b + count, b })
			indices[static_cast<size_t>(used++)] = v;
	}
	SDL_BlendMode before = SDL_BLENDMODE_NONE;
	SDL_GetRenderDrawBlendMode(renderer, &before);
	SDL_SetRenderDrawBlendMode(renderer, cover ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
	SDL_RenderGeometry(renderer, nullptr, vertices.data(), 1 + count * 2, indices.data(), used);
	SDL_SetRenderDrawBlendMode(renderer, before);
}

/** @brief The corners of the smallest shape with no dents round some points on the floor, in order round it; returns how many. */
int FloorHull(const SDL_FPoint *points, int count, int *hull)
{
	std::array<int, MaxShadeCorners> order;
	if (count > MaxShadeCorners)
		return 0;
	for (int i = 0; i < count; i++)
		order[static_cast<size_t>(i)] = i;
	std::sort(order.begin(), order.begin() + count, [&](int a, int b) {
		return points[a].x < points[b].x || (points[a].x == points[b].x && points[a].y < points[b].y);
	});
	const auto turn = [&](int o, int a, int b) {
		return (points[a].x - points[o].x) * (points[b].y - points[o].y) - (points[a].y - points[o].y) * (points[b].x - points[o].x);
	};
	std::array<int, MaxShadeCorners * 2 + 2> chain;
	int n = 0;
	for (int i = 0; i < count; i++) {
		while (n >= 2 && turn(chain[static_cast<size_t>(n - 2)], chain[static_cast<size_t>(n - 1)], order[static_cast<size_t>(i)]) <= 1e-7F)
			n--;
		chain[static_cast<size_t>(n++)] = order[static_cast<size_t>(i)];
	}
	const int lower = n + 1;
	for (int i = count - 2; i >= 0; i--) {
		while (n >= lower && turn(chain[static_cast<size_t>(n - 2)], chain[static_cast<size_t>(n - 1)], order[static_cast<size_t>(i)]) <= 1e-7F)
			n--;
		chain[static_cast<size_t>(n++)] = order[static_cast<size_t>(i)];
	}
	n--;
	for (int i = 0; i < n && i < MaxShadeCorners; i++)
		hull[i] = chain[static_cast<size_t>(i)];
	return std::min(n, MaxShadeCorners);
}

/** @brief The rows a shadow is drawn in up a wall. */
constexpr int WallRows = 4;
/**
 * @brief A shadow goes no higher up a wall than its face stands, fading out over the last part of that. The face is as
 * high as the wall tile's pictures reach up the screen from its foot, less the wall's top, which the pictures show too.
 */
constexpr float WallLedge = 0.8F;
constexpr float WallFade = 0.5F;
constexpr float WallLeastTall = 0.5F;
constexpr float WallMostTall = 3.5F;
/** @brief The most wall tiles side by side one shadow goes up. */
constexpr int WallSpan = 5;
/** @brief A wall's face stands a little in front of its tile's edge: the shadow starts this far (tiles) below the edge. */
constexpr float WallFootDrop = 0.03F;
/** @brief The most (tiles) a shadow's floor part is carried on past where its middle meets a wall, for its far side to reach a slanting wall. */
constexpr float WallMostSlant = 2.F;

/**
 * @brief The part of a body's shadow that falls on a wall: where the floor ends at a wall turned to the view, the rest
 * of the shadow goes on up the wall's face instead of stopping at its foot. It is cut into the part on each wall tile,
 * drawn right after that tile's own pictures (so behind everything standing in front of the wall).
 */
struct WallShade {
	/** @brief The last of the wall tile's draws. */
	size_t after;
	SDL_Texture *texture;
	/** @brief Its rows of corners up the wall, the left and the right of each. */
	std::array<SDL_Vertex, WallRows * 2> at;
};

/** @brief A shadow's part on a wall before it is cut up: it lies over the wall's line from `from` to `to` (tiles along it). */
struct WallStrip {
	float from = 0;
	float to = 0;
	SDL_Texture *texture = nullptr;
	std::array<SDL_Vertex, WallRows * 2> at;
};

std::vector<WallShade> WallShades;
/** @brief The draw the next of them goes after. */
size_t NextWallShade = std::numeric_limits<size_t>::max();
/** @brief The draw the world's drawing has got to (0 before it starts): a wall drawn before that is on screen already. */
size_t DrawingAt = 0;
/** @brief The last draw of each dungeon tile's own pictures this frame, made when the first shadow meets a wall. */
std::vector<int> LastTileDraw;
/** @brief The top of each dungeon tile's own pictures this frame, in the world buffer. */
std::vector<int> TileTop;
bool LastTileDrawKnown = false;

int LastDrawOfTile(Point tile)
{
	if (!LastTileDrawKnown) {
		LastTileDraw.assign(static_cast<size_t>(MAXDUNX) * MAXDUNY, -1);
		TileTop.assign(static_cast<size_t>(MAXDUNX) * MAXDUNY, std::numeric_limits<int>::max());
		for (size_t i = FloorEnd; i < Draws.size(); i++) {
			const Draw &draw = Draws[i];
			if (draw.kind != DrawKind::Tile || !draw.hasTile || !InDungeonBounds(draw.cell))
				continue;
			const size_t cell = static_cast<size_t>(draw.cell.x) * MAXDUNY + draw.cell.y;
			LastTileDraw[cell] = static_cast<int>(i);
			TileTop[cell] = std::min(TileTop[cell], draw.y);
		}
		LastTileDrawKnown = true;
	}
	return InDungeonBounds(tile) ? LastTileDraw[static_cast<size_t>(tile.x) * MAXDUNY + tile.y] : -1;
}

/** @brief How high a wall tile's face stands, in tiles (see WallLedge); 0 where the tile isn't drawn. */
float WallTallOf(Point tile, bool facesX)
{
	if (LastDrawOfTile(tile) < 0)
		return 0;
	// The middle of the wall's foot.
	float bx;
	float by;
	BufferOf(static_cast<float>(tile.x) - (facesX ? 0.5F : 0.F), static_cast<float>(tile.y) - (facesX ? 0.F : 0.5F), bx, by);
	const float top = static_cast<float>(TileTop[static_cast<size_t>(tile.x) * MAXDUNY + tile.y]);
	return std::clamp((by - top) * RisePerPixel - WallLedge, WallLeastTall, WallMostTall);
}

void DrawWallShade(SDL_Renderer *renderer, const WallShade &shade)
{
	std::array<int, (WallRows - 1) * 6> indices;
	int used = 0;
	for (int r = 0; r + 1 < WallRows; r++) {
		const int corner = r * 2;
		for (const int v : { corner, corner + 1, corner + 3, corner, corner + 3, corner + 2 })
			indices[static_cast<size_t>(used++)] = v;
	}
	SDL_RenderGeometry(renderer, shade.texture, shade.at.data(), static_cast<int>(shade.at.size()), indices.data(), used);
}

/** @brief Draws the shadows on walls that go after a draw (WallShade::after). */
void DrawWallShadesAfter(SDL_Renderer *renderer, size_t index)
{
	if (index != NextWallShade)
		return;
	NextWallShade = std::numeric_limits<size_t>::max();
	for (size_t i = 0; i < WallShades.size();) {
		if (WallShades[i].after == index) {
			DrawWallShade(renderer, WallShades[i]);
			WallShades[i] = WallShades.back();
			WallShades.pop_back();
			continue;
		}
		NextWallShade = std::min(NextWallShade, WallShades[i].after);
		i++;
	}
}

/**
 * @brief Hands a shadow's part on a wall to each wall tile it lies on, cut at the tiles' ends, to be drawn with that
 * tile. (Cut as shapes, not with a clip: the parts on tiles side by side then meet without a seam.)
 */
void AddWallShade(SDL_Renderer *renderer, const WallHit &hit, const WallStrip &strip)
{
	const float span = strip.to - strip.from;
	if (strip.texture == nullptr || std::abs(span) < 1e-3F)
		return;
	const int first = static_cast<int>(std::floor(std::min(strip.from, strip.to) + 0.5F));
	const int last = std::min(static_cast<int>(std::floor(std::max(strip.from, strip.to) + 0.5F)), first + WallSpan - 1);
	const int across = static_cast<int>(std::floor(hit.at + 1.F));
	for (int along = first; along <= last; along++) {
		const Point tile = hit.facesX ? Point { across, along } : Point { along, across };
		const BackWalls walls = BackWallsOf(tile);
		if (!(hit.facesX ? walls.left : walls.right))
			continue;
		const int after = LastDrawOfTile(tile);
		if (after < 0)
			continue;
		// The share of the way across the strip at which the tile starts and ends.
		float a = std::clamp((static_cast<float>(along) - 0.5F - strip.from) / span, 0.F, 1.F);
		float b = std::clamp((static_cast<float>(along) + 0.5F - strip.from) / span, 0.F, 1.F);
		if (a > b)
			std::swap(a, b);
		if (b - a < 1e-4F)
			continue;
		WallShade shade;
		shade.after = static_cast<size_t>(after);
		shade.texture = strip.texture;
		for (size_t r = 0; r < WallRows; r++) {
			const SDL_Vertex &left = strip.at[r * 2];
			const SDL_Vertex &right = strip.at[r * 2 + 1];
			const auto between = [&](float t) {
				return SDL_Vertex {
					{ left.position.x + (right.position.x - left.position.x) * t, left.position.y + (right.position.y - left.position.y) * t },
					{ 0, 0, 0, static_cast<Uint8>(static_cast<float>(left.color.a) + (static_cast<float>(right.color.a) - static_cast<float>(left.color.a)) * t + 0.5F) },
					{ left.tex_coord.x + (right.tex_coord.x - left.tex_coord.x) * t, left.tex_coord.y + (right.tex_coord.y - left.tex_coord.y) * t },
				};
			};
			shade.at[r * 2] = between(a);
			shade.at[r * 2 + 1] = between(b);
		}
		if (shade.after < DrawingAt) {
			DrawWallShade(renderer, shade);
			continue;
		}
		WallShades.push_back(shade);
		NextWallShade = std::min(NextWallShade, shade.after);
	}
}

/** @brief Which of a body's shadows a call draws (DrawCasterShadows). */
enum class CastWhat : uint8_t {
	/** All of them, straight onto the floor. */
	All,
	/** The shade at its feet, and the shadows of the lights whose shadows aren't joined (Joins). */
	Feet,
	/** One light's shadow, as cover: white added up on that light's picture of the floor it can't reach (JoinedShadows). */
	Cover,
};

/** @brief What a call of DrawCasterShadows draws, and for cover, where on the floor it drew. */
struct CastPass {
	CastWhat what = CastWhat::All;
	/** The light whose shadow is drawn as cover (light::ShadowLight::source). */
	int source = -1;
	/** The lights on the body, where they are known already. */
	const LightsOn *on = nullptr;
	/** The floor the cover drawn so far lies on, in tiles. */
	float x0 = std::numeric_limits<float>::max();
	float y0 = std::numeric_limits<float>::max();
	float x1 = std::numeric_limits<float>::lowest();
	float y1 = std::numeric_limits<float>::lowest();

	void grow(float x, float y)
	{
		x0 = std::min(x0, x);
		y0 = std::min(y0, y);
		x1 = std::max(x1, x);
		y1 = std::max(y1, y);
	}
};

/**
 * @brief Whether a light's shadows are joined into one: two things in one light's way don't make the floor behind both
 * darker than one does. The lava's and the sky's light stand for many lights, placed for each body by itself.
 */
bool Joins(const light::ShadowLight &source)
{
	return source.source >= 0;
}

/** @brief Whether a body casts shadows where it stands (`wx`, `wy`). */
bool CastsShadows(const Draw &draw, float &wx, float &wy)
{
	float gx;
	float gy;
	GroundOf(draw, gx, gy);
	WorldOf(gx, gy, wx, wy);
	// As the game hides a light inside a wall, a monster inside one doesn't give itself away by its shadow. (In town
	// Griswold stands behind his counter, on a solid tile.)
	const Point tile { static_cast<int>(std::floor(wx + 0.5F)), static_cast<int>(std::floor(wy + 0.5F)) };
	return InDungeonBounds(tile) && (leveltype == DTYPE_TOWN || !TileHasAny(tile, TileProperties::Solid));
}

/**
 * @brief The shadows a hero, monster or solid object casts on the floor: one away from each of the strongest lights on it, its body
 * as that light sees it laid on the floor (see ViewLean and the notes before it), and the spot under its feet from a
 * light hanging right over it (its own) and from the light bouncing in under it.
 */
void DrawCasterShadows(SDL_Renderer *renderer, const Draw &draw, float toLogical, CastPass *pass = nullptr)
{
	const CastWhat what = pass != nullptr ? pass->what : CastWhat::All;
	const bool cover = what == CastWhat::Cover;
	// Cover is drawn on a picture the size of the world's, not on the screen.
	const float placing = cover ? 1.F : toLogical;
	float gx;
	float gy;
	GroundOf(draw, gx, gy);
	float wx;
	float wy;
	if (!CastsShadows(draw, wx, wy))
		return;
	const LightsOn &on = pass != nullptr && pass->on != nullptr ? *pass->on : LightsOnPoint(wx, wy);
	const std::array<light::ShadowLight, 3> &lights = on.lights;
	const light::ShadowLight &overhead = on.overhead;
	const int count = on.count;
	const float here = light::LinearAt(wx, wy);
	if (here <= 1e-4F)
		return;
	const float margin = static_cast<float>(TILE_WIDTH * 3 * Scale) * toLogical;
	if (!OnScreen(DestOf(draw, toLogical), margin))
		return;
	Raster &raster = FallbackRaster(draw, false);
	if (!CastTextures(renderer, raster))
		return;
	const float w = static_cast<float>(raster.width);
	const float h = static_cast<float>(raster.height);
	const float pad = static_cast<float>(CastPad);
	const float feet = h - 1 - HalfTileHeight;
	// Heights in the picture, in tiles per game pixel.
	// A hero or monster is drawn as a flat figure standing at its feet. An object is a block seen from above: its
	// picture shows its top as well as its sides, so it is lower and narrower than its picture, and the side of it away
	// from a light, where its shadow starts, lies behind its middle.
	const bool object = IsObject(draw.caster);
	const float HeightPerPixel = (object ? ObjectHeightShare : 1.F) / (ViewLean * TileAcross);
	const float tall = std::max(feet - static_cast<float>(raster.bodyTop), 1.F) * HeightPerPixel;
	// How wide the body is across the light, and for an object where its base lies (FootprintOf).
	const Footprint foot = object ? FootprintOf(raster) : Footprint {};
	const float bodyWidth = foot.bodyWidth;
	const float middleX = foot.middleX;
	const float middleY = foot.middleY;
	const float halfX = foot.halfX;
	const float halfY = foot.halfY;
	const float boxTall = foot.boxTall;
	const bool block = draw.caster == Caster::ObjectBlock;
	// A block's base on the floor, corner by corner.
	const float baseX = wx + middleX;
	const float baseY = wy + middleY;
	const std::array<SDL_FPoint, 4> base { { { baseX - halfX, baseY - halfY }, { baseX + halfX, baseY - halfY }, { baseX + halfX, baseY + halfY }, { baseX - halfX, baseY + halfY } } };
	const float acrossShare = object ? ObjectWidthShare : 1.F;

	if (cover) {
		// Only the light's own shadow.
	} else if (block) {
		// Round a block's base: the bounced light it keeps off the floor beside it, on every side, and some of a light
		// right overhead.
		float removed = FootShade * (leveltype == DTYPE_TOWN ? SkyShade : BounceShare) * here;
		if (overhead.strength > 0)
			removed += (1 - overhead.fill) * light::ToLinear(overhead.strength) * 0.5F;
		const float alpha = ShadowAlpha(here, removed);
		const std::array<float, 4> alphas { alpha, alpha, alpha, alpha };
		const std::array<float, 4> feathers { BlockSkirt, BlockSkirt, BlockSkirt, BlockSkirt };
		DrawFloorShade(renderer, base.data(), alphas.data(), feathers.data(), 4, alpha, toLogical);
	} else if (BlobTexture != nullptr) {
		// Under the feet: the bounced light the body keeps off the floor, and the shadow of a light right overhead, its
		// body laid straight down (wider the nearer the light) with an edge as soft as the light is wide.
		float removed = FootShade * (leveltype == DTYPE_TOWN ? SkyShade : BounceShare) * here;
		float across = bodyWidth * 2;
		if (overhead.strength > 0) {
			const float lightHeight = std::max(overhead.height, 0.1F);
			const float middle = std::min(tall / 2, lightHeight * 0.8F);
			const float core = bodyWidth * lightHeight / (lightHeight - middle);
			const float edge = overhead.size * middle / (lightHeight - middle);
			across = std::max(core + edge, bodyWidth * 2);
			removed += (1 - overhead.fill) * light::ToLinear(overhead.strength) * core / across;
		}
		const float alpha = ShadowAlpha(here, removed);
		if (alpha > 0.004F) {
			const SDL_FPoint centre = OnScreenAt(gx, gy + ContactDrop, toLogical);
			// A circle on the floor `across` tiles wide.
			const float width = across * TileAcross * Scale * toLogical;
			const SDL_FRect spot { centre.x - width / 2, centre.y - width / 4, width, width / 2 };
			SDL_SetTextureAlphaMod(BlobTexture, ColourByte(alpha));
			SDL_RenderCopyF(renderer, BlobTexture, nullptr, &spot);
		}
	}

	const float texWidth = w + pad * 2;
	const float texHeight = h + pad * 2;
	// Only the part of the shadow pictures that holds the body and its soft edge is drawn: the rest of them is empty.
	const CastPart &sharpPart = raster.sharpPart;
	const CastPart &softPart = raster.softPart;
	const float vTop = std::min(sharpPart.vTop, softPart.vTop);
	constexpr int Rows = CastBands + 2;
	for (int n = 0; n < count; n++) {
		const light::ShadowLight &source = lights[n];
		if ((cover && source.source != pass->source) || (what == CastWhat::Feet && Joins(source)))
			continue;
		const float mx = wx + middleX;
		const float my = wy + middleY;
		float dx = mx - source.x;
		float dy = my - source.y;
		const float toMiddle = std::sqrt(dx * dx + dy * dy);
		if (toMiddle < 1e-3F)
			continue;
		dx /= toMiddle;
		dy /= toMiddle;
		if (block) {
			// A block's shadow is the floor its base and its top hide from the light: the base itself, the top as the
			// light throws it on the floor (each corner of the base moved straight away from the light, the further the
			// lower the light), and all between. So it starts exactly at the base's edges, whichever way the light lies.
			// A light over the block (a fireball flying across a sarcophagus) is above its top, whatever height it is given:
			// it can't be inside it. It rises to there as it comes over the base's edge, so the shadow never jumps.
			const float outsideX = std::max(std::abs(source.x - baseX) - halfX, 0.F);
			const float outsideY = std::max(std::abs(source.y - baseY) - halfY, 0.F);
			const float over = 1 - std::min(std::sqrt(outsideX * outsideX + outsideY * outsideY) / BlockOverEdge, 1.F);
			const float lightHeight = std::max({ source.height, 0.05F, (boxTall + BlockOverRise) * over });
			const float limit = RoomFor(mx, my, dx, dy, std::clamp(source.reach - toMiddle, 0.F, CastReach));
			if (limit < 0.05F)
				continue;
			const float atBody = light::LinearOf(source, wx, wy);
			const float narrow = 2 * std::min(halfX, halfY);
			std::array<SDL_FPoint, MaxShadeCorners> points;
			std::array<float, MaxShadeCorners> alphas;
			std::array<float, MaxShadeCorners> feathers;
			const auto darkAt = [&](const SDL_FPoint &at, float kept) {
				return CastAlphaAt(source, at.x, at.y, kept, atBody);
			};
			for (size_t i = 0; i < 4; i++) {
				const float ex = base[i].x - source.x;
				const float ey = base[i].y - source.y;
				const float away = std::sqrt(ex * ex + ey * ey);
				// A light no higher than the block throws its top as far as the light reaches.
				float thrown = lightHeight > boxTall * 1.02F ? away * boxTall / (lightHeight - boxTall) : limit;
				thrown = std::min(thrown, limit);
				points[i] = base[i];
				alphas[i] = cover ? 1.F : darkAt(base[i], 1.F);
				feathers[i] = BlockEdge;
				points[i + 4] = away > 1e-3F ? SDL_FPoint { base[i].x + ex / away * thrown, base[i].y + ey / away * thrown } : base[i];
				// The light's width seen past the block's top edge: the soft edge, and a light wider than the block
				// gets round it.
				const float toTop = std::sqrt(away * away + (lightHeight - boxTall) * (lightHeight - boxTall));
				const float pastTop = std::sqrt(thrown * thrown + boxTall * boxTall);
				const float penumbra = source.size * pastTop / std::max(toTop, 0.05F);
				const float kept = penumbra > narrow ? narrow / penumbra : 1.F;
				alphas[i + 4] = cover ? kept : darkAt(points[i + 4], kept);
				feathers[i + 4] = std::clamp(std::max(penumbra, BlockOverSoft * over), BlockLeastSoft, BlockMostSoft);
			}
			std::array<int, MaxShadeCorners> hull;
			const int corners = FloorHull(points.data(), MaxShadeCorners, hull.data());
			std::array<SDL_FPoint, MaxShadeCorners> shape;
			std::array<float, MaxShadeCorners> shapeAlphas;
			std::array<float, MaxShadeCorners> shapeFeathers;
			for (int i = 0; i < corners; i++) {
				const auto from = static_cast<size_t>(hull[static_cast<size_t>(i)]);
				shape[static_cast<size_t>(i)] = points[from];
				shapeAlphas[static_cast<size_t>(i)] = alphas[from];
				shapeFeathers[static_cast<size_t>(i)] = feathers[from];
			}
			const float middle = cover ? 1.F : darkAt(FloorMiddle(shape.data(), corners), 1.F);
			DrawFloorShade(renderer, shape.data(), shapeAlphas.data(), shapeFeathers.data(), corners, middle, placing, cover);
			if (cover) {
				for (int i = 0; i < corners; i++)
					pass->grow(shape[static_cast<size_t>(i)].x, shape[static_cast<size_t>(i)].y);
			}
			continue;
		}
		// Where the shadow starts: under the feet, or just inside a round object's edge away from the light.
		const float behind = object ? (halfX + halfY) / 2 * ObjectTuck : 0.F;
		const float distance = toMiddle + behind;
		const float sx = mx + dx * behind;
		const float sy = my + dy * behind;
		const float lightHeight = std::max(source.height, 0.05F);
		const float atBody = light::LinearOf(source, wx, wy);
		// As far as the floor goes before a wall, and the light reaches past the body.
		WallHit wall;
		const float limit = RoomFor(sx, sy, dx, dy, std::clamp(source.reach - distance, 0.F, CastReach), &wall);
		if (limit < 0.05F && !wall.found)
			continue;
		const bool upWall = wall.found && atmosphere::Setting(atmosphere::Knob::Walls) > 0;
		// Each height of the body lands this far out behind the feet. The picture below the floor point the art stands
		// on is what touches the floor in front of the feet: it stays near them (cast like the body, it would reach far
		// towards a low light such as lava).
		const float underSlope = std::min(distance / lightHeight, 0.5F);
		const auto landsAt = [&](float z) { return z <= 0 ? z * underSlope : distance * z / (lightHeight - z); };
		// The top of the body's part of the pictures and their bottom row, and the highest part of the body whose shadow
		// still lands on the floor.
		const float topZ = (feet + pad - vTop * texHeight) * HeightPerPixel;
		const float bottomZ = (feet - h - pad) * HeightPerPixel;
		// The shadow's far end lies square to it, but a wall's foot may cross it at a slant: then its floor part goes on
		// until both its sides reach the wall, and is kept in front of the wall's line, so it neither stops short of the
		// wall on one side nor runs on into the wall on the other.
		float floorEnd = limit;
		const bool atWall = wall.found;
		const float towardWall = wall.facesX ? dx : dy;
		if (atWall && towardWall < -0.05F) {
			const float endZ = lightHeight * limit / (distance + limit);
			const float endSpread = endZ > 0 ? std::min(lightHeight / std::max(lightHeight - endZ, 0.05F), MaxSpread) : 1.F;
			const float half = std::max(std::abs(softPart.u0 * texWidth - pad - w / 2), std::abs(softPart.u1 * texWidth - pad - w / 2)) / TileAcross * endSpread * acrossShare;
			floorEnd = limit + std::min(half * std::abs(wall.facesX ? dy : dx) / -towardWall, WallMostSlant);
		}
		const float highZ = std::min(topZ, lightHeight * floorEnd / (distance + floorEnd));
		const float farthest = landsAt(highZ);
		// Rows of the shadow: the picture's bottom, the feet, then even steps along the floor to its end.
		std::array<float, Rows> rowZ;
		rowZ[0] = bottomZ;
		rowZ[1] = 0;
		for (int r = 2; r < Rows; r++) {
			const float along = farthest * static_cast<float>(r - 1) / static_cast<float>(CastBands);
			rowZ[r] = lightHeight * along / (distance + along);
		}
		// A hero or monster is a flat picture facing the view. Its shadow is laid out across the light (the body as the
		// light sees it), but at the floor it has to lie where the picture's own feet are drawn, or it starts beside
		// them. So the lowest part of the body (up to FeetBlend) is cast from where the view shows it, and turns to the
		// light's layout above that. The layout keeps the picture's left and right as the view has them, so it mirrors as
		// a light passes right beside the body: there the two layouts are drawn together, one fading into the other.
		const float facing = -(dx + dy) * ViewAcross;
		std::array<float, 2> turns { 1.F, -1.F };
		std::array<float, 2> weights { 1.F, 0.F };
		if (!object) {
			weights[0] = std::clamp(0.5F + facing / (2 * TurnBand), 0.F, 1.F);
			weights[1] = 1 - weights[0];
		}
		std::array<SDL_Vertex, Rows * 2> sharp;
		std::array<SDL_Vertex, Rows * 2> soft;
		for (size_t layout = 0; layout < turns.size(); layout++) {
			const float turn = turns[layout];
			const float weight = weights[layout];
			if (weight <= 0.004F)
				continue;
			for (int r = 0; r < Rows; r++) {
				const float z = rowZ[r];
				const float along = landsAt(z);
				const float fx = sx + dx * along;
				const float fy = sy + dy * along;
				const float raised = std::max(z, 0.F);
				// The body as the light sees it: the nearer the light, the wider each height's shadow.
				// (Held to MaxSpread: under a flame hardly taller than the body, its head and shoulders would fan out so wide
				// and thin that no shadow shows at all.)
				const float spread = raised > 0 ? std::min(lightHeight / (lightHeight - raised), MaxSpread) : 1.F;
				// The light's width seen past this height of the body, laid on the floor: the soft edge.
				const float toBody = std::sqrt(distance * distance + (lightHeight - raised) * (lightHeight - raised));
				const float pastBody = std::sqrt(along * along + raised * raised);
				const float penumbra = source.size * pastBody / std::max(toBody, 0.05F);
				// A light wider than the body seen from the floor gets round it, and no spot is wholly out of it.
				const float kept = penumbra > bodyWidth * spread ? bodyWidth * spread / penumbra : 1.F;
				const float alpha = weight * (cover ? kept : CastAlphaAt(source, fx, fy, kept, atBody));
				// The soft picture's edge is stretched with the shadow, so it stands for a wider soft edge the more it spreads.
				const float softness = std::clamp(penumbra / (SoftWidth * spread), 0.F, MaxSoftness);
				const Uint8 sharpAlpha = ColourByte(alpha * (1 - softness));
				const Uint8 softAlpha = ColourByte(alpha * softness);
				const float v = std::clamp((feet + pad - z / HeightPerPixel) / texHeight, 0.F, 1.F);
				// Across the shadow, square to it on the floor, at a point u (0 to 1) across the pictures.
				// How far this height has turned from the view's layout to the light's.
				const float rise = std::clamp(z / FeetBlend, 0.F, 1.F);
				const float turned = object ? 1.F : rise * rise * (3 - 2 * rise);
				// The picture below the feet lies on the floor towards the view, as it is drawn.
				const float below = z < 0 ? -z / HeightPerPixel / static_cast<float>(TILE_HEIGHT) : 0.F;
				const auto placed = [&](float u) {
					const float fromMiddle = (u * texWidth - pad - w / 2) / TileAcross;
					const float across = fromMiddle * spread * acrossShare * turn;
					float x = fx - dy * across;
					float y = fy + dx * across;
					if (turned < 1) {
						const float out = z > 0 ? along : 0.F;
						const float viewX = wx + fromMiddle * ViewAcross + dx * out + below;
						const float viewY = wy - fromMiddle * ViewAcross + dy * out + below;
						x = viewX + (x - viewX) * turned;
						y = viewY + (y - viewY) * turned;
					}
					if (atWall) {
						if (wall.facesX)
							x = std::max(x, wall.at);
						else
							y = std::max(y, wall.at);
					}
					if (cover)
						pass->grow(x, y);
					float bx;
					float by;
					BufferOf(x, y, bx, by);
					return OnScreenAt(bx, by, placing);
				};
				const Uint8 shade = cover ? 255 : 0;
				for (int side = 0; side < 2; side++) {
					const float sharpU = side == 0 ? sharpPart.u0 : sharpPart.u1;
					const float softU = side == 0 ? softPart.u0 : softPart.u1;
					sharp[static_cast<size_t>(r) * 2 + side] = SDL_Vertex { placed(sharpU), { shade, shade, shade, sharpAlpha }, { sharpU, v } };
					soft[static_cast<size_t>(r) * 2 + side] = SDL_Vertex { placed(softU), { shade, shade, shade, softAlpha }, { softU, v } };
				}
			}
			// Only the bands with some shadow at one end or the other: the rest would draw nothing.
			const auto drawBands = [&](SDL_Texture *texture, const std::array<SDL_Vertex, Rows * 2> &vertices) {
				std::array<int, (Rows - 1) * 6> indices;
				int used = 0;
				for (int r = 0; r + 1 < Rows; r++) {
					const int corner = r * 2;
					if (vertices[static_cast<size_t>(corner)].color.a == 0 && vertices[static_cast<size_t>(corner) + 2].color.a == 0)
						continue;
					for (const int v : { corner, corner + 1, corner + 3, corner, corner + 3, corner + 2 })
						indices[static_cast<size_t>(used++)] = v;
				}
				if (used > 0)
					SDL_RenderGeometry(renderer, texture, vertices.data(), static_cast<int>(vertices.size()), indices.data(), used);
			};
			if (cover) {
				SDL_SetTextureBlendMode(raster.castTexture, SDL_BLENDMODE_ADD);
				SDL_SetTextureBlendMode(raster.castSoft, SDL_BLENDMODE_ADD);
			}
			drawBands(raster.castTexture, sharp);
			drawBands(raster.castSoft, soft);
			if (cover) {
				SDL_SetTextureBlendMode(raster.castTexture, SDL_BLENDMODE_BLEND);
				SDL_SetTextureBlendMode(raster.castSoft, SDL_BLENDMODE_BLEND);
			}
			if (!upWall)
				continue;
			// The rest of the shadow goes up the wall. Each side of the body, as the light sees it across the shadow's
			// start, is thrown on to the wall's line: the part of it that would land past the wall's foot stands on the
			// wall instead, as high as the light's ray over that height of the body meets it.
			std::array<WallStrip, 2> strips;
			strips[0].texture = raster.castTexture;
			strips[1].texture = raster.castSoft;
			// The wall's height where the middle of the shadow meets it.
			const float toWall = wall.facesX ? wall.at - source.x : wall.at - source.y;
			const float toStart = wall.facesX ? sx - source.x : sy - source.y;
			float wallTall = 0;
			if (toStart < -0.02F && toWall < -0.02F) {
				const float times = std::max(toWall / toStart, 1.F);
				const int along = static_cast<int>(std::floor((wall.facesX ? source.y + (sy - source.y) * times : source.x + (sx - source.x) * times) + 0.5F));
				const int across = static_cast<int>(std::floor(wall.at + 1.F));
				wallTall = WallTallOf(wall.facesX ? Point { across, along } : Point { along, across }, wall.facesX);
			}
			bool meets = wallTall > 0;
			for (int side = 0; side < 2 && meets; side++) {
				for (int strip = 0; strip < 2; strip++) {
					const float u = strip == 0 ? (side == 0 ? sharpPart.u0 : sharpPart.u1) : (side == 0 ? softPart.u0 : softPart.u1);
					const float across = (u * texWidth - pad - w / 2) / TileAcross * acrossShare * turn;
					const float ex = sx - dy * across - source.x;
					const float ey = sy + dx * across - source.y;
					const float toSide = wall.facesX ? ex : ey;
					if (toSide > -0.02F) {
						meets = false;
						break;
					}
					// How many times as far from the light as the body the wall is, along this ray.
					const float times = std::max(toWall / toSide, 1.F);
					const float qx = source.x + ex * times;
					const float qy = source.y + ey * times;
					(side == 0 ? strips[static_cast<size_t>(strip)].from : strips[static_cast<size_t>(strip)].to) = wall.facesX ? qy : qx;
					float bx;
					float by;
					BufferOf(qx, qy, bx, by);
					const SDL_FPoint foot = OnScreenAt(bx, by, toLogical);
					// The light there, taken just in front of the wall.
					const float lx = qx + (wall.facesX ? 0.15F : 0.F);
					const float ly = qy + (wall.facesX ? 0.F : 0.15F);
					const float penumbra = source.size * (times - 1);
					const float kept = penumbra > bodyWidth * times ? bodyWidth * times / penumbra : 1.F;
					const float dark = weight * CastAlphaAt(source, lx, ly, kept, atBody);
					const float softness = std::clamp(penumbra / (SoftWidth * times), 0.F, MaxSoftness);
					// From the height whose shadow lands at the wall's foot to the body's top (or as high as the wall's shadow goes).
					const float lowZ = lightHeight * (1 - 1 / times);
					const float topOnWall = lightHeight - (lightHeight - topZ) * times;
					const float endZ = topOnWall > wallTall ? lightHeight - (lightHeight - wallTall) / times : topZ;
					for (int r = 0; r < WallRows; r++) {
						const float z = lowZ + std::max(endZ - lowZ, 0.F) * static_cast<float>(r) / static_cast<float>(WallRows - 1);
						const float up = std::clamp(lightHeight - (lightHeight - z) * times, 0.F, wallTall);
						const float fade = std::clamp((wallTall - up) / WallFade, 0.F, 1.F);
						const float v = std::clamp((feet + pad - z / HeightPerPixel) / texHeight, 0.F, 1.F);
						const SDL_FPoint at { foot.x, foot.y - (r == 0 ? -WallFootDrop : up) / RisePerPixel * Scale * toLogical };
						const Uint8 alpha = ColourByte(dark * fade * (strip == 0 ? 1 - softness : softness));
						strips[static_cast<size_t>(strip)].at[static_cast<size_t>(r) * 2 + side] = SDL_Vertex { at, { 0, 0, 0, alpha }, { u, v } };
					}
				}
			}
			if (meets) {
				AddWallShade(renderer, wall, strips[0]);
				AddWallShade(renderer, wall, strips[1]);
			}
		}
	}
}

/** @brief One light's picture of the floor it can't reach (JoinedShadows), as big as the world's picture. */
SDL_Texture *ShadeTexture = nullptr;
Size ShadeTextureSize;
bool ShadeFailed = false;
/**
 * @brief The part of the picture the last light's shadows were drawn on, wiped before the next light's (wiping the whole
 * picture for every light costs more than drawing the shadows); all of it while `ShadeUsedAll`.
 */
SDL_FRect ShadeUsed { 0, 0, 0, 0 };
bool ShadeUsedAll = true;

/** @brief Makes the picture the joined shadows are added up on; false where the renderer can't draw to one or blend it on. */
bool EnsureShade(SDL_Renderer *renderer, Size size)
{
	if (ShadeTextureSize == size && (ShadeTexture != nullptr || ShadeFailed))
		return ShadeTexture != nullptr;
	if (ShadeTexture != nullptr)
		SDL_DestroyTexture(ShadeTexture);
	ShadeTexture = nullptr;
	ShadeTextureSize = size;
	ShadeFailed = true;
	if (size.width <= 0 || size.height <= 0 || !SDL_RenderTargetSupported(renderer))
		return false;
	// The floor keeps what the picture's white leaves of it.
	static const SDL_BlendMode Darken = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR, SDL_BLENDOPERATION_ADD,
	    SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR, SDL_BLENDOPERATION_ADD);
	SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, size.width, size.height);
	if (texture == nullptr)
		return false;
	if (SDL_SetTextureBlendMode(texture, Darken) != 0) {
		SDL_DestroyTexture(texture);
		return false;
	}
	SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
	ShadeTexture = texture;
	ShadeFailed = false;
	ShadeUsedAll = true;
	return true;
}

/** @brief A body that casts shadows this frame, where it stands, and the lights on it. */
struct CastJob {
	size_t draw;
	float x;
	float y;
	LightsOn on;
};

/** @brief A solid object in the lights' way this frame: its base's middle and half sides, and its height (tiles). */
struct Blocker {
	size_t draw;
	float x;
	float y;
	float halfX;
	float halfY;
	float tall;
};

/** @brief How tall a hero or monster is taken to be where it stands in an object's shadow, in tiles. */
constexpr float BodyTall = 1.4F;
/** @brief The least of its light a body keeps in an object's shadow, and the hero being played (who must always be easy to see). */
constexpr float LeastKept = 0.35F;
constexpr float HeroLeastKept = 0.6F;

/**
 * @brief How much of each draw's light is left where it stands in an object's shadow (1 where it stands in none), at its
 * foot and at its top, with this frame's joined shadows: a barrel behind a sarcophagus is as dark as the floor it
 * stands on, and a hero behind one has his legs in its shadow and his head in the light.
 */
struct Kept {
	float foot = 1;
	float top = 1;
};
std::vector<Kept> ShadeKept;

/** @brief How much of a body an object hides from a light: the share of its width, and how far up it the shadow reaches (0 to 1). */
struct Hidden {
	float across = 0;
	float up = 0;
};

/**
 * @brief How much of a body (at `cx`, `cy`, `width` across and `tall` high) an object hides from a light: the share of
 * its width behind the object as the light sees it, and the share of its height below the line from the light over the
 * object's top.
 */
Hidden HiddenBy(const light::ShadowLight &source, const Blocker &blocker, float cx, float cy, float width, float tall)
{
	// A light over the object lights all round it. A big object is drawn from each tile it covers: it doesn't hide itself.
	if (std::abs(source.x - blocker.x) < blocker.halfX && std::abs(source.y - blocker.y) < blocker.halfY)
		return {};
	if (std::abs(cx - blocker.x) < blocker.halfX && std::abs(cy - blocker.y) < blocker.halfY)
		return {};
	float ux = cx - source.x;
	float uy = cy - source.y;
	const float away = std::sqrt(ux * ux + uy * uy);
	if (away < 0.05F)
		return {};
	ux /= away;
	uy /= away;
	float nearest = std::numeric_limits<float>::max();
	float farthest = std::numeric_limits<float>::lowest();
	float low = std::numeric_limits<float>::max();
	float high = std::numeric_limits<float>::lowest();
	for (int corner = 0; corner < 4; corner++) {
		const float ex = blocker.x + (corner == 1 || corner == 2 ? blocker.halfX : -blocker.halfX) - source.x;
		const float ey = blocker.y + (corner >= 2 ? blocker.halfY : -blocker.halfY) - source.y;
		const float along = ex * ux + ey * uy;
		nearest = std::min(nearest, along);
		farthest = std::max(farthest, along);
		// Where the corner's edge of the shadow passes the body.
		const float beside = (ey * ux - ex * uy) * away / std::max(along, 0.05F);
		low = std::min(low, beside);
		high = std::max(high, beside);
	}
	if (nearest >= away || farthest <= 0)
		return {};
	const float covered = std::clamp((std::min(high, width / 2) - std::max(low, -width / 2)) / width, 0.F, 1.F);
	if (covered <= 0)
		return {};
	const float lightHeight = std::max(source.height, 0.05F);
	const float top = lightHeight - (lightHeight - blocker.tall) * away / std::max(std::min(farthest, away), 0.05F);
	return { covered, std::clamp(top / tall, 0.F, 1.F) };
}

/** @brief Dims a draw's light to what is left of it in another body's shadow (ShadeKept), from its foot to its top. */
void Dim(Shading &shading, const Kept &kept)
{
	if (!shading.fades) {
		shading.top = shading.colours;
		shading.fades = true;
	}
	for (int s = 0; s < shading.count; s++) {
		for (int c = 0; c < 3; c++) {
			shading.colours[static_cast<size_t>(s)][static_cast<size_t>(c)] *= kept.foot;
			shading.top[static_cast<size_t>(s)][static_cast<size_t>(c)] *= kept.top;
		}
	}
}

/** @brief A light and the bodies in its way this frame. */
struct ShadeGroup {
	light::ShadowLight light;
	/** The light where the body it is strongest on stands (see CastAlpha). */
	float atBody = 0;
	std::vector<size_t> jobs;
};

/** @brief The most a joined shadow's picture is cut into across, where it takes the light's share of the floor's light. */
constexpr int ShadeSteps = 16;

/** @brief The part of the joined shadows' picture that the floor from `x0`, `y0` to `x1`, `y1` (tiles) lies on, kept inside the picture. */
SDL_FRect ShadeRect(float x0, float y0, float x1, float y1)
{
	float left = std::numeric_limits<float>::max();
	float top = std::numeric_limits<float>::max();
	float right = std::numeric_limits<float>::lowest();
	float bottom = std::numeric_limits<float>::lowest();
	for (const SDL_FPoint corner : { SDL_FPoint { x0, y0 }, SDL_FPoint { x1, y0 }, SDL_FPoint { x1, y1 }, SDL_FPoint { x0, y1 } }) {
		float bx;
		float by;
		BufferOf(corner.x, corner.y, bx, by);
		const SDL_FPoint in = OnScreenAt(bx, by, 1.F);
		left = std::min(left, in.x);
		right = std::max(right, in.x);
		top = std::min(top, in.y);
		bottom = std::max(bottom, in.y);
	}
	left = std::max(std::floor(left), 0.F);
	top = std::max(std::floor(top), 0.F);
	right = std::min(std::ceil(right), static_cast<float>(ShadeTextureSize.width));
	bottom = std::min(std::ceil(bottom), static_cast<float>(ShadeTextureSize.height));
	return SDL_FRect { left, top, std::max(right - left, 0.F), std::max(bottom - top, 0.F) };
}

/**
 * @brief Darkens the floor by one light's joined shadows: its picture of the floor it can't reach (white where a body
 * is in its way), each point as dark as that light's share of the light there (CastAlphaAt), over the part `rect` of
 * the picture (ShadeRect).
 */
void DrawShade(SDL_Renderer *renderer, const ShadeGroup &group, const SDL_FRect &rect, float toLogical)
{
	const float left = rect.x;
	const float top = rect.y;
	const float right = rect.x + rect.w;
	const float bottom = rect.y + rect.h;
	const float pictureWidth = static_cast<float>(ShadeTextureSize.width);
	const float pictureHeight = static_cast<float>(ShadeTextureSize.height);
	if (right - left < 1 || bottom - top < 1)
		return;
	const float least = HalfTileWidth * Scale;
	const int across = std::clamp(static_cast<int>(std::ceil((right - left) / least)), 1, ShadeSteps);
	const int down = std::clamp(static_cast<int>(std::ceil((bottom - top) / least)), 1, ShadeSteps);
	std::array<SDL_Vertex, (ShadeSteps + 1) * (ShadeSteps + 1)> vertices;
	std::array<int, ShadeSteps * ShadeSteps * 6> indices;
	bool any = false;
	for (int j = 0; j <= down; j++) {
		for (int i = 0; i <= across; i++) {
			const float px = left + (right - left) * static_cast<float>(i) / static_cast<float>(across);
			const float py = top + (bottom - top) * static_cast<float>(j) / static_cast<float>(down);
			float x;
			float y;
			WorldOf((px - ViewportOffsetX) / Scale, py / Scale, x, y);
			const float dark = CastAlphaAt(group.light, x, y, 1.F, group.atBody);
			any = any || dark > 0.004F;
			const Uint8 shade = ColourByte(dark);
			vertices[static_cast<size_t>(j * (across + 1) + i)] = SDL_Vertex { { px * toLogical, py * toLogical }, { shade, shade, shade, 255 },
				{ px / pictureWidth, py / pictureHeight } };
		}
	}
	if (!any)
		return;
	int used = 0;
	for (int j = 0; j < down; j++) {
		for (int i = 0; i < across; i++) {
			const int corner = j * (across + 1) + i;
			for (const int v : { corner, corner + 1, corner + across + 2, corner, corner + across + 2, corner + across + 1 })
				indices[static_cast<size_t>(used++)] = v;
		}
	}
	SDL_RenderGeometry(renderer, ShadeTexture, vertices.data(), (across + 1) * (down + 1), indices.data(), used);
}

/**
 * @brief Draws every body's shadows on the floor, the shadows of one light joined into one.
 *
 * A shadow is the floor a light can't reach. Two bodies in one light's way don't make the floor behind both darker
 * than one does, and a body standing in another's shadow adds none of its own from that light. Drawn one by one, each
 * shadow darkened the floor again. So for each light the bodies in its way are added up in white on a picture of the
 * floor (it can't get whiter than white), and that picture darkens the floor once, by that light's share of the light
 * at each point.
 * @return False where the renderer can't do that, or feel("joined") is off: each body then draws its own (DrawCasterShadows).
 */
bool DrawJoinedShadows(SDL_Renderer *renderer, Size worldSize, float toLogical)
{
	if (atmosphere::Setting(atmosphere::Knob::Joined) <= 0 || !EnsureShade(renderer, worldSize))
		return false;
	// The whole picture is wiped once a frame: a graphics driver may lose what a drawn-on picture holds (on a device
	// reset, alt-tab or a window change), and what was left there would darken the floor.
	ShadeUsedAll = true;
	static std::vector<CastJob> jobs;
	static std::vector<ShadeGroup> groups;
	jobs.clear();
	size_t groupCount = 0;
	for (size_t i = FloorEnd; i < Draws.size(); i++) {
		const Draw &draw = Draws[i];
		if (IsOutline(draw.style) || draw.style != SpriteStyle::Normal || draw.caster == Caster::None)
			continue;
		float wx;
		float wy;
		if (!CastsShadows(draw, wx, wy))
			continue;
		jobs.push_back(CastJob { i, wx, wy, LightsOnPoint(wx, wy) });
		const LightsOn &on = jobs.back().on;
		for (int n = 0; n < on.count; n++) {
			const light::ShadowLight &source = on.lights[static_cast<size_t>(n)];
			if (!Joins(source))
				continue;
			size_t g = 0;
			while (g < groupCount && groups[g].light.source != source.source)
				g++;
			if (g == groupCount) {
				if (groups.size() == groupCount)
					groups.emplace_back();
				groups[g].light = source;
				groups[g].atBody = 0;
				groups[g].jobs.clear();
				groupCount++;
			}
			groups[g].atBody = std::max(groups[g].atBody, light::LinearOf(source, wx, wy));
			groups[g].jobs.push_back(jobs.size() - 1);
		}
	}
	// A body standing in an object's shadow is as dark as the floor there.
	static std::vector<Blocker> blockers;
	blockers.clear();
	ShadeKept.assign(Draws.size(), Kept {});
	const float margin = static_cast<float>(TILE_WIDTH * 3 * Scale) * toLogical;
	for (const CastJob &job : jobs) {
		const Draw &draw = Draws[job.draw];
		if (!IsObject(draw.caster) || !OnScreen(DestOf(draw, toLogical), margin))
			continue;
		const Footprint foot = FootprintOf(FallbackRaster(draw, false));
		blockers.push_back(Blocker { job.draw, job.x + foot.middleX, job.y + foot.middleY, foot.halfX, foot.halfY, foot.boxTall });
	}
	for (const CastJob &job : jobs) {
		if (blockers.empty())
			break;
		float cx = job.x;
		float cy = job.y;
		float width = BodyWidth;
		float tall = BodyTall;
		for (const Blocker &blocker : blockers) {
			if (blocker.draw != job.draw)
				continue;
			cx = blocker.x;
			cy = blocker.y;
			width = blocker.halfX + blocker.halfY;
			tall = blocker.tall;
		}
		const float total = light::LinearAt(job.x, job.y);
		const float glows = light::GlowAt(job.x, job.y);
		Kept kept;
		for (int n = 0; n < job.on.count; n++) {
			const light::ShadowLight &source = job.on.lights[static_cast<size_t>(n)];
			if (!Joins(source))
				continue;
			Hidden hidden;
			const Blocker *behind = nullptr;
			for (const Blocker &blocker : blockers) {
				const Hidden by = HiddenBy(source, blocker, cx, cy, width, tall);
				if (by.across * by.up > hidden.across * hidden.up) {
					hidden = by;
					behind = &blocker;
				}
			}
			if (hidden.across * hidden.up <= 0)
				continue;
			// The shadow reaches `up` of the way up the body: drawn as a fade from its foot to its top that hides as
			// much of it.
			// As dark as the floor it stands on, which that object's shadow darkens by the light where the object stands.
			const float own = light::LinearOf(source, job.x, job.y);
			const float atBody = std::max(own, light::LinearOf(source, behind->x, behind->y));
			kept.foot *= 1 - CastAlpha(total, glows, own, source.glow, source.fill, hidden.across * std::min(hidden.up * 2, 1.F), atBody);
			kept.top *= 1 - CastAlpha(total, glows, own, source.glow, source.fill, hidden.across * std::max(hidden.up * 2 - 1, 0.F), atBody);
		}
		const float least = Draws[job.draw].caster == Caster::MyHero ? HeroLeastKept : LeastKept;
		kept.foot = std::max(kept.foot, least);
		kept.top = std::max(kept.top, least);
		ShadeKept[job.draw] = kept;
	}
	// The shade at each body's feet, and the shadows that aren't joined.
	for (const CastJob &job : jobs) {
		CastPass pass;
		pass.what = CastWhat::Feet;
		pass.on = &job.on;
		DrawCasterShadows(renderer, Draws[job.draw], toLogical, &pass);
	}
	SDL_Texture *screen = SDL_GetRenderTarget(renderer);
	for (size_t g = 0; g < groupCount; g++) {
		const ShadeGroup &group = groups[g];
		SDL_SetRenderTarget(renderer, ShadeTexture);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		if (ShadeUsedAll) {
			SDL_RenderClear(renderer);
		} else if (ShadeUsed.w > 0 && ShadeUsed.h > 0) {
			SDL_BlendMode before = SDL_BLENDMODE_NONE;
			SDL_GetRenderDrawBlendMode(renderer, &before);
			SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
			SDL_RenderFillRectF(renderer, &ShadeUsed);
			SDL_SetRenderDrawBlendMode(renderer, before);
		}
		ShadeUsedAll = false;
		ShadeUsed = SDL_FRect { 0, 0, 0, 0 };
		CastPass pass;
		pass.what = CastWhat::Cover;
		pass.source = group.light.source;
		for (const size_t job : group.jobs) {
			pass.on = &jobs[job].on;
			DrawCasterShadows(renderer, Draws[jobs[job].draw], toLogical, &pass);
		}
		SDL_SetRenderTarget(renderer, screen);
		// With room for the soft edges round the shapes drawn.
		if (pass.x1 >= pass.x0) {
			ShadeUsed = ShadeRect(pass.x0 - BlockMostSoft, pass.y0 - BlockMostSoft, pass.x1 + BlockMostSoft, pass.y1 + BlockMostSoft);
			DrawShade(renderer, group, ShadeUsed, toLogical);
		}
	}
	return true;
}

/**
 * @brief The light on a wall's face, from the floor in front of it, taken along the face so walls in a row meet without a
 * seam. Not from the floor behind the wall: a wall with arches has a strip beside each pillar in its other column, which
 * showed the light of the room behind the wall (light::FaceAt).
 */
Shading FaceLight(Point tile, bool right)
{
	const float tx = static_cast<float>(tile.x);
	const float ty = static_cast<float>(tile.y);
	const auto at = [](float x, float y) { return ToColour(light::FaceAt(x, y)); };
	if (right)
		return Gradient(at(tx - 0.5F, ty - 0.25F), at(tx + 0.5F, ty - 0.25F));
	return Gradient(at(tx - 0.25F, ty + 0.5F), at(tx - 0.25F, ty - 0.5F));
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
	const float share = FromBehind(tile, right);
	if (share <= 0)
		return shading;
	const float tx = static_cast<float>(tile.x);
	const float ty = static_cast<float>(tile.y);
	const Shading behind = right ? Gradient(BehindLight(tx - 0.5F, ty - 0.75F, true), BehindLight(tx + 0.5F, ty - 0.75F, true))
	                             : Gradient(BehindLight(tx - 0.75F, ty + 0.5F, false), BehindLight(tx - 0.75F, ty - 0.5F, false));
	for (int s = 0; s < shading.count; s++) {
		for (int c = 0; c < 3; c++)
			shading.colours[s][c] = std::max(shading.colours[s][c], behind.colours[s][c] * share);
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

/** @brief Rock with no open tile round it: the rock between and round the rooms, of which only the top shows. */
bool IsDeepRock(Point tile)
{
	for (int dy = -1; dy <= 1; dy++) {
		for (int dx = -1; dx <= 1; dx++) {
			const Point beside = tile + Displacement { dx, dy };
			if (InDungeonBounds(beside) && !light::Blocks(beside.x, beside.y))
				return false;
		}
	}
	return true;
}

/**
 * @brief Only the top of the rock on this tile shows, where the level's rock shows its top (light::RockTopsLit): rock with
 * no open tile round it (IsDeepRock), or rock whose piece draws nothing at the floor, so no wall's face stands on the tile,
 * only the top of the rock, raised over the floor (the Caves' rock right beside a room, and the top of its steps up).
 */
bool IsRockTop(Point tile)
{
	if (!light::RockTopsLit())
		return false;
	if (IsDeepRock(tile))
		return true;
	const MICROS &micros = DPieceMicros[light::UnpaintedPiece(tile.x, tile.y, dPiece[tile.x][tile.y])];
	return !micros.mt[0].hasValue() && !micros.mt[1].hasValue();
}

/**
 * @brief The light on the top of the rock (IsRockTop): the light round it from above (light::RockTopAt), across the
 * tile from left to right as on an open tile. Lit as a wall's face, each half from its own side, the rock took the light
 * the faces round it take from the rooms in front of them in one half only: a row of lit teeth along a wall's top, and
 * past them the rock tile by tile in blocks of light and dark, or black, with the top of the Caves' steps up cut off.
 */
Shading RockTopLight(Point tile, bool right)
{
	const float tx = static_cast<float>(tile.x);
	const float ty = static_cast<float>(tile.y);
	const auto at = [](float x, float y) { return ToColour(light::RockTopAt(x, y)); };
	if (right)
		return Gradient(at(tx, ty), at(tx + 0.25F, ty - 0.25F));
	return Gradient(at(tx - 0.25F, ty + 0.25F), at(tx, ty));
}

/** @brief The light on a whole piece standing on a tile, both of its columns lit as they are when the piece is drawn. */
Shading PieceLight(Point tile)
{
	const bool wall = light::Blocks(tile.x, tile.y);
	const Shading left = wall ? WallLight(tile, false) : OpenTileLight(tile, false);
	const Shading right = wall ? WallLight(tile, true) : OpenTileLight(tile, true);
	Shading shading;
	shading.count = 3;
	shading.stops = { 0.F, 0.5F, 1.F, 1.F };
	shading.colours[0] = left.colours[0];
	for (int c = 0; c < 3; c++)
		shading.colours[1][c] = (left.colours[1][c] + right.colours[0][c]) / 2;
	shading.colours[2] = right.colours[1];
	return shading;
}

/** @brief How much of the fires' light still reaches this high up a wall (tiles above its floor), 0 to 1. */
float FireLightKept(float rise)
{
	return std::clamp(1.F - (rise - FireLightHigh) / FireLightFade, 0.F, 1.F);
}

/**
 * @brief How much further down the screen (in game pixels) than a piece's own tile the floor is that its height counts
 * from, for a piece on a tile inside a house or under its roof: the floor of the tile right behind the house's front tile
 * in the same column of the screen.
 *
 * Tristram's houses draw their roofs from the tiles under them, up to five rows behind their front walls. Measured up
 * from such a tile's own floor, inside the house, the roof counted as low on a wall and kept the fires' light, in blocks
 * a column wide (Griswold's roof, the roof above Pepin's windows). Not from the front tile's own floor: along a wall, the
 * front tiles stand half a tile lower in every other column, and measured from them the lit windows in a wall lost some
 * of their glow in those columns. A house's outer walls (the tiles that stop light) count from their own floor: what
 * stands in front of them is a porch, a door or a lamp, not more of the house.
 */
int FrontTileBelow(const Draw &draw)
{
	constexpr int Step = TILE_HEIGHT / 2;
	// Further back than this, all of a piece is above the fires' light anyway.
	constexpr int MostInFront = 8;
	static_assert(static_cast<float>((MostInFront - 1) * Step) * RisePerPixel >= FireLightHigh + FireLightFade);
	if (!TileHasAny(draw.cell, TileProperties::Solid) || light::Blocks(draw.cell.x, draw.cell.y))
		return 0;
	// Down the screen, a column holds the left half of tile (x, y), the right half of (x, y + 1), the left half of
	// (x + 1, y + 1) and so on, each half a tile's height lower.
	Point tile = draw.cell;
	bool right = IsRightColumn(draw);
	int inFront = 0;
	while (inFront < MostInFront) {
		if (right)
			tile.x++;
		else
			tile.y++;
		right = !right;
		if (!InDungeonBounds(tile) || !TileHasAny(tile, TileProperties::Solid))
			break;
		inFront++;
	}
	return std::max(inFront - 1, 0) * Step;
}

/**
 * @brief In town, takes the fires' light off the part of a piece higher up than it reaches (FireLightHigh), leaving the
 * sky's. The piece stands on a tile whose floor is drawn `back` game pixels further up the screen than its own tile's.
 */
void FireLightUp(Shading &shading, const Draw &draw, int back)
{
	if (leveltype != DTYPE_TOWN)
		return;
	const float bottom = (static_cast<float>(draw.tileOrigin.y - back - (draw.y + draw.height - 1)) - HalfTileHeight) * RisePerPixel;
	const float keepBottom = FireLightKept(bottom);
	const float keepTop = FireLightKept(bottom + static_cast<float>(draw.height) * RisePerPixel);
	if (keepBottom >= 1 && keepTop >= 1)
		return;
	const Colour3 sky = ToColour(light::Sky());
	const auto fade = [&sky](const Colour3 &lit, float keep) {
		Colour3 out;
		for (int c = 0; c < 3; c++)
			out[c] = lit[c] - std::max(lit[c] - sky[c], 0.F) * (1 - keep);
		return out;
	};
	for (int s = 0; s < shading.count; s++) {
		shading.top[s] = fade(shading.colours[s], keepTop);
		shading.colours[s] = fade(shading.colours[s], keepBottom);
	}
	shading.fades = true;
}

/**
 * @brief The most and the least of a light a side takes, against the floor beside the object (which the light's table
 * is made for): a side turned to a low flame takes more of it than the floor it skims over, a side turned away only
 * what gets round to it.
 */
constexpr float ObjectToward = 1.4F;
constexpr float ObjectAway = 0.25F;
/** @brief How far round a side a light still reaches (0 only the half turned to it, 1 all the way round). */
constexpr float ObjectWrap = 0.2F;
/** @brief How much of the top's light the top of a low object's picture takes; a tall one (a rack, a cross) shows less top. */
constexpr float ObjectTop = 0.5F;
constexpr float ObjectLowPixels = 50.F;
constexpr float ObjectTallPixels = 110.F;
constexpr float ObjectTallTop = 0.3F;
/** @brief The height of the middle of an object's sides, in tiles. */
constexpr float ObjectMiddle = 0.35F;
/** @brief The body fills only part of its picture: the colours at the picture's edges reach at most this far past the body's. */
constexpr float ObjectEdgeReach = 2.5F;
/** @brief How far each way from a block's front corner its light turns from one side's to the other's, against its width. */
constexpr float ObjectCornerTurn = 0.04F;
/**
 * @brief The way each part faces: the left side, the middle (the corner between), the right side, the top. A round
 * thing's left and right edges are turned further round than a block's sides.
 */
using ObjectFacing = std::array<std::array<float, 3>, 4>;
/** @brief The most ways of facing FacingShades works out at once. */
constexpr size_t MaxFacings = 6;
constexpr ObjectFacing RoundFaces { { { -0.38F, 0.92F, 0.F }, { 0.707F, 0.707F, 0.F }, { 0.92F, -0.38F, 0.F }, { 0.F, 0.F, 1.F } } };
constexpr ObjectFacing BlockFaces { { { 0.F, 1.F, 0.F }, { 0.707F, 0.707F, 0.F }, { 1.F, 0.F, 0.F }, { 0.F, 0.F, 1.F } } };

/**
 * @brief How much of the light where an object stands each of some ways of facing takes, against the one light the game
 * gives the whole object (1 is as that; the floor beside it is what the light's table is made for).
 *
 * Each of the strongest lights there gives a face more of its light the more the face is turned to it, measured against
 * the floor under the same light; its bounce and the rest of the room's light fall on every face alike.
 * @return False where no light with a direction falls on it (or feel("objects") is off): one light for all of it.
 */
bool FacingShades(float wx, float wy, const std::array<float, 3> *faces, size_t count, float *shade)
{
	const float strength = std::clamp(atmosphere::Setting(atmosphere::Knob::Objects), 0.F, 2.F);
	const float total = light::LinearAt(wx, wy);
	const float drawnTotal = light::Drawn(total);
	if (strength <= 0 || total <= 1e-4F || drawnTotal <= 1e-4F || count > MaxFacings)
		return false;
	const LightsOn &on = LightsOnPoint(wx, wy);
	std::array<float, MaxFacings> taken {};
	float direct = 0;
	const auto shine = [&](const light::ShadowLight &source) {
		// Its bounce (fill) lights every side alike, with the rest of the room's light.
		const float own = (1 - source.fill) * light::LinearOf(source, wx, wy);
		if (own <= 0)
			return;
		const float dx = source.x - wx;
		const float dy = source.y - wy;
		const float dz = std::max(source.height, 0.05F) - ObjectMiddle;
		const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
		if (length < 1e-3F)
			return;
		direct += own;
		// How much a face turned this way to the light takes of it, and the floor (which faces up, under the whole
		// height of the light).
		const auto takes = [](float facing) { return std::clamp((facing + ObjectWrap) / (1 + ObjectWrap), 0.F, 1.F); };
		const float floorTakes = std::max(takes(std::max(source.height, 0.05F) / std::sqrt(dx * dx + dy * dy + source.height * source.height + 1e-4F)), 0.1F);
		for (size_t f = 0; f < count; f++) {
			const float facing = (dx * faces[f][0] + dy * faces[f][1] + dz * faces[f][2]) / length;
			taken[f] += own * std::clamp(takes(facing) / floorTakes, ObjectAway, ObjectToward);
		}
	};
	for (int n = 0; n < on.count; n++)
		shine(on.lights[static_cast<size_t>(n)]);
	if (on.overhead.strength > 0)
		shine(on.overhead);
	if (direct <= 0)
		return false;
	// The lights' shares are worked out one by one and the light there all together: they can differ a little.
	const float fits = direct > total ? total / direct : 1.F;
	const float others = std::max(total - direct, 0.F);
	for (size_t f = 0; f < count; f++) {
		const float against = light::Drawn(others + taken[f] * fits) / drawnTotal;
		shade[f] = std::max(1 + (against - 1) * strength, 0.05F);
	}
	return true;
}

/**
 * @brief The light on a solid object's sides. The game gives a whole object the one light of its tile, so it looks cut out and
 * laid on the lit floor. Here each side takes more of a light the more it is turned to it and less turned away, as a
 * real thing does, with the light bouncing round the room on every side.
 *
 * The picture has no shape to read (that takes a shape map per object, the next step), so the object is taken as one of
 * two simple shapes, as most of them are drawn. A block (a sarcophagus, a chest) is seen corner-on: its left side faces
 * +y, its right side +x, they meet at the corner nearest the eye, and each takes its own light. Anything else is taken
 * as round: the light goes smoothly from its left edge by its middle to its right edge. Both fade to the top's light up
 * the picture (the lid, the rim).
 */
Shading ObjectLight(const Draw &draw, const Colour3 &flat, float wx, float wy)
{
	std::array<float, 4> shade;
	if (!FacingShades(wx, wy, (draw.caster == Caster::ObjectBlock ? BlockFaces : RoundFaces).data(), shade.size(), shade.data()))
		return Uniform(flat);
	Raster &raster = FallbackRaster(draw, false);
	BodyBounds(raster);
	const float width = static_cast<float>(std::max(raster.width, 1));
	const float height = static_cast<float>(std::max(raster.height, 1));
	const float bodyLeft = static_cast<float>(raster.boundLeft) / width;
	const float bodyRight = static_cast<float>(raster.boundRight + 1) / width;
	// The corner nearest the eye, where the left side ends and the right one starts.
	const float middle = std::clamp((static_cast<float>(raster.boundCorner) + 0.5F) / width, 0.05F, 0.95F);
	std::array<float, 4> stops;
	std::array<float, 4> bottom;
	int count;
	if (draw.caster == Caster::ObjectBlock) {
		// Flat sides: each its own light, turning from one to the other over a little way at the corner.
		const float turn = std::max((bodyRight - bodyLeft) * ObjectCornerTurn, 1 / width);
		const float before = std::clamp(middle - turn, 0.01F, 0.98F);
		stops = { 0.F, before, std::clamp(middle + turn, before + 0.005F, 0.99F), 1.F };
		bottom = { shade[0], shade[0], shade[2], shade[2] };
		count = 4;
	} else {
		// Round: the light goes smoothly from the left edge by the middle to the right edge. The picture's own edges lie
		// past the body's: the colours there carry on the line from the middle through the body's edge.
		const auto pastEdge = [](float edge, float span) { return span > 1e-3F ? std::min(edge / span, ObjectEdgeReach) : 1.F; };
		stops = { 0.F, std::clamp(middle, 0.05F, 0.95F), 1.F, 1.F };
		bottom = {
			std::max(shade[1] + (shade[0] - shade[1]) * pastEdge(middle, middle - bodyLeft), 0.F),
			shade[1],
			std::max(shade[1] + (shade[2] - shade[1]) * pastEdge(1 - middle, bodyRight - middle), 0.F),
			0.F,
		};
		count = 3;
	}
	const float bodyPixels = static_cast<float>(raster.boundBottom - raster.boundTop + 1);
	const float low = std::clamp((ObjectTallPixels - bodyPixels) / (ObjectTallPixels - ObjectLowPixels), 0.F, 1.F);
	const float topShare = ObjectTop * (ObjectTallTop + (1 - ObjectTallTop) * low);
	const float bodyTop = static_cast<float>(raster.boundTop) / height;
	const float pastTop = std::min(1 / std::max(1 - bodyTop, 0.2F), ObjectEdgeReach);
	Shading shading;
	shading.count = count;
	shading.stops = stops;
	shading.fades = true;
	for (size_t s = 0; s < static_cast<size_t>(count); s++) {
		const float top = std::max(bottom[s] + (shade[3] - bottom[s]) * topShare * pastTop, 0.F);
		for (size_t c = 0; c < 3; c++) {
			shading.colours[s][c] = flat[c] * bottom[s];
			shading.top[s][c] = flat[c] * top;
		}
	}
	return shading;
}

/** @brief The way each of a shape map's pictures faces (HdArt::shape), in the world: +x, -x, +y, -y, up. */
constexpr std::array<std::array<float, 3>, NumShapeAxes> ShapeAxes { { { 1.F, 0.F, 0.F }, { -1.F, 0.F, 0.F }, { 0.F, 1.F, 0.F }, { 0.F, -1.F, 0.F }, { 0.F, 0.F, 1.F } } };

/**
 * @brief Draws a solid object whose HD art has a shape map, lit by its shape: every part of it takes each light by the
 * way that part faces, so a lid, a rim, a carved edge each catch the light as they would.
 *
 * Without shaders this is the art drawn once in the least light any of its ways of facing takes, then each way's
 * picture (MakeShape) added on top in the light that way takes over that. A part leaning two ways gets some of both.
 * @param kept How much of its light is left where it stands in another body's shadow (ShadeKept).
 * @return False if the draw isn't one (no shape map, or feel("shapes") is off): it is drawn lit by its sides.
 */
bool DrawShapedObject(SDL_Renderer *renderer, const Draw &draw, float toLogical, float kept)
{
	if (!IsObject(draw.caster) || draw.style != SpriteStyle::Normal || draw.light < 0 || (!draw.lightmapped && draw.tbl == nullptr))
		return false;
	if (atmosphere::Setting(atmosphere::Knob::Shapes) <= 0)
		return false;
	const ArtChoice art = CachedArt(renderer, draw);
	if (art.art == nullptr || !art.art->shaped)
		return false;
	const SDL_FRect dest = DestOf(draw, toLogical);
	if (!OnScreen(dest, 0))
		return true;
	float wx;
	float wy;
	StandsAt(draw, wx, wy);
	Colour3 flat = draw.hasTile ? LightAt(static_cast<float>(draw.cell.x), static_cast<float>(draw.cell.y)) : LightAt(wx, wy);
	for (float &part : flat)
		part *= kept;
	std::array<float, NumShapeAxes> shade;
	if (!FacingShades(wx, wy, ShapeAxes.data(), shade.size(), shade.data()))
		shade.fill(1.F);
	float least = std::numeric_limits<float>::max();
	for (size_t k = 0; k < NumShapeAxes; k++) {
		if (art.art->shape[k] != nullptr)
			least = std::min(least, shade[k]);
	}
	DrawShaded(renderer, draw, Uniform({ flat[0] * least, flat[1] * least, flat[2] * least }), toLogical, false);
	for (size_t k = 0; k < NumShapeAxes; k++) {
		SDL_Texture *texture = art.art->shape[k];
		if (texture == nullptr)
			continue;
		Colour3 more { flat[0] * (shade[k] - least), flat[1] * (shade[k] - least), flat[2] * (shade[k] - least) };
		// A colour mod stops at full: light over that is added again.
		for (int pass = 0; pass < 2 && std::max({ more[0], more[1], more[2] }) > 1.F / 255; pass++) {
			SDL_SetTextureColorMod(texture, ColourByte(more[0]), ColourByte(more[1]), ColourByte(more[2]));
			SDL_RenderCopyF(renderer, texture, nullptr, &dest);
			more = AboveFull(more);
		}
	}
	return true;
}

/** @brief The colour of the edge the lava gives a body standing by it (LavaEdge). */
constexpr Colour3 LavaEdgeColour { 1.F, 0.5F, 0.2F };

/**
 * @brief A hero or monster standing by lava takes an orange edge on the side turned to it (the Atmosphere option's idea
 * 30): that side of its picture a little brighter and more orange, the other a little darker, by the lava's share of
 * the light there (lava_edge in atmosphere.tsv is how strong).
 */
Shading LavaEdge(const Colour3 &flat, float wx, float wy)
{
	if (!light::HasLava())
		return Uniform(flat);
	const float edge = atmosphere::Strength(atmosphere::Knob::Lava) * atmosphere::Value("lava_edge", 0);
	const float total = light::LinearAt(wx, wy);
	if (edge <= 0 || total <= 1e-4F)
		return Uniform(flat);
	const LightsOn &on = LightsOnPoint(wx, wy);
	for (int n = 0; n < on.count; n++) {
		const light::ShadowLight &lava = on.lights[static_cast<size_t>(n)];
		if (lava.source != light::LavaLight)
			continue;
		const float dx = lava.x - wx;
		const float dy = lava.y - wy;
		const float away = std::sqrt(dx * dx + dy * dy);
		if (away < 1e-3F)
			break;
		const float share = std::clamp((1 - lava.fill) * light::LinearOf(lava, wx, wy) / total, 0.F, 1.F);
		// To the right on screen is +x, -y.
		const float side = (dx - dy) / away * ViewAcross;
		const float more = std::min(edge * share * std::abs(side), 0.8F);
		if (more < 0.02F)
			break;
		const float bright = std::max({ flat[0], flat[1], flat[2] });
		Colour3 toward;
		Colour3 turned;
		for (size_t c = 0; c < 3; c++) {
			toward[c] = flat[c] + LavaEdgeColour[c] * bright * more;
			turned[c] = flat[c] * (1 - 0.4F * more);
		}
		// The body fills the middle of its picture: the light turns from one side to the other across it.
		Shading shading;
		shading.count = 4;
		shading.stops = { 0.F, 0.38F, 0.62F, 1.F };
		const Colour3 &left = side < 0 ? toward : turned;
		const Colour3 &right = side < 0 ? turned : toward;
		shading.colours = { left, left, right, right };
		return shading;
	}
	return Uniform(flat);
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
		const bool right = IsRightColumn(draw);
		Shading shading;
		if (!light::Blocks(draw.cell.x, draw.cell.y))
			shading = OpenTileLight(draw.cell, right);
		else if (IsRockTop(draw.cell))
			shading = RockTopLight(draw.cell, right);
		else
			shading = WallLight(draw.cell, right);
		if (leveltype == DTYPE_TOWN)
			FireLightUp(shading, draw, -FrontTileBelow(draw));
		return shading;
	}
	float wx;
	float wy;
	StandsAt(draw, wx, wy);
	switch (draw.caster) {
	case Caster::MyHero: {
		// The game always draws the hero being played fully lit; here they take the light's colour but stay bright. In
		// town's evening they stand in its light like everyone else.
		const light::Rgb at = light::At(wx, wy);
		const light::Rgb full = light::Full();
		const float least = leveltype == DTYPE_TOWN ? 0.F : HeroLightFloor;
		return LavaEdge({ std::max(at.r, full.r * least), std::max(at.g, full.g * least), std::max(at.b, full.b * least) }, wx, wy);
	}
	case Caster::Hero:
		// Other heroes are drawn 5 light levels brighter than where they stand.
		return draw.light >= 0 ? LavaEdge(ToColour(light::Boost(light::At(wx, wy), 5)), wx, wy) : Uniform(ToColour(light::Full()));
	case Caster::Monster:
		return draw.light >= 0 ? LavaEdge(LightAt(wx, wy), wx, wy) : Uniform(ToColour(light::Full()));
	case Caster::MonsterRecoloured:
		return LavaEdge(LightAt(wx, wy), wx, wy);
	case Caster::MonsterUnlit:
		return Uniform(ToColour(light::Full()));
	case Caster::None:
	case Caster::Object:
	case Caster::ObjectBlock:
		break;
	}
	if (draw.light < 0)
		return Uniform(ToColour(light::Full()));
	// Town's trees draw their leaves again over the hero (from the tile in front of each tree, see scrollrt.cpp): they
	// take the light of the tree they cover, or they would shine by themselves.
	if (leveltype == DTYPE_TOWN && draw.hasTile && IsArchPiece(draw)) {
		Shading shading = PieceLight(draw.cell - Displacement { 1, 1 });
		FireLightUp(shading, draw, TILE_HEIGHT);
		return shading;
	}
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
		shading.stops = { 0.F, 0.5F, 1.F, 1.F };
		shading.colours[0] = left.colours[0];
		for (int c = 0; c < 3; c++)
			shading.colours[1][c] = (left.colours[1][c] + right.colours[0][c]) / 2;
		shading.colours[2] = right.colours[1];
		return shading;
	}
	const Colour3 flat = draw.hasTile ? LightAt(static_cast<float>(draw.cell.x), static_cast<float>(draw.cell.y)) : LightAt(wx, wy);
	// A solid object takes the light by its sides (unless it is see-through: a fading ghost of one keeps its one light).
	if (IsObject(draw.caster) && draw.style == SpriteStyle::Normal)
		return ObjectLight(draw, flat, wx, wy);
	return Uniform(flat);
}

/** @brief The most pieces of lava floor the heat haze is drawn over in a frame, and the bands each is drawn in. */
constexpr int MaxHazePieces = 500;
constexpr int HazeBands = 8;

/**
 * @brief The lava wavers in its heat (the Atmosphere option's idea 31): each piece of lava floor is drawn again over
 * itself, half see-through and pushed a little left and right in bands that run up the screen, so its surface swims.
 * Drawn before the light, so it is lit as the floor is and lies under everything standing on it. (`haze` in
 * atmosphere.tsv is how far it sways, in game pixels.)
 */
void DrawHeatHaze(SDL_Renderer *renderer, float toLogical)
{
	if (!light::HasLava())
		return;
	const float sway = std::min(atmosphere::Value("haze", 0) * atmosphere::Strength(atmosphere::Knob::Haze), 6.F);
	if (sway <= 0)
		return;
	const float time = vfx::Clock();
	std::array<SDL_Vertex, (HazeBands + 1) * 2> vertices;
	std::array<int, HazeBands * 6> indices;
	int used = 0;
	for (int r = 0; r < HazeBands; r++) {
		const int corner = r * 2;
		for (const int v : { corner, corner + 1, corner + 3, corner, corner + 3, corner + 2 })
			indices[static_cast<size_t>(used++)] = v;
	}
	int pieces = 0;
	for (size_t i = 0; i < FloorEnd && i < Draws.size() && pieces < MaxHazePieces; i++) {
		const Draw &draw = Draws[i];
		if (IsOutline(draw.style) || draw.kind != DrawKind::Tile)
			continue;
		// The floor's pieces don't carry their tile: it is the one under the middle of the piece's half of the diamond.
		float tileX;
		float tileY;
		WorldOf(static_cast<float>(draw.x) + static_cast<float>(draw.width) / 2, static_cast<float>(draw.y + draw.height) - HalfTileHeight, tileX, tileY);
		if (!light::IsLava(static_cast<int>(std::floor(tileX + 0.5F)), static_cast<int>(std::floor(tileY + 0.5F))))
			continue;
		const SDL_FRect dest = DestOf(draw, toLogical);
		if (!OnScreen(dest, 0))
			continue;
		const DrawTexture art = TextureFor(renderer, draw, false);
		if (art.texture == nullptr)
			continue;
		const Colour3 colour = ArtColour({ 1.F, 1.F, 1.F }, art);
		const SDL_Color shade { ColourByte(colour[0]), ColourByte(colour[1]), ColourByte(colour[2]), 140 };
		SDL_SetTextureColorMod(art.texture, 255, 255, 255);
		SDL_SetTextureAlphaMod(art.texture, 255);
		for (int r = 0; r <= HazeBands; r++) {
			const float v = static_cast<float>(r) / HazeBands;
			// The waves run up the screen, fixed to the world so they don't swim when the view moves.
			float wx;
			float wy;
			WorldOf(static_cast<float>(draw.x), static_cast<float>(draw.y) + v * static_cast<float>(draw.height), wx, wy);
			const float down = (wx + wy) * HalfTileHeight;
			const float wave = 0.65F * std::sin(down * 0.21F + time * 0.0075F) + 0.35F * std::sin(down * 0.47F + time * 0.0131F + 1.3F);
			const float dx = wave * sway * Scale * toLogical;
			const float y = dest.y + v * dest.h;
			vertices[static_cast<size_t>(r) * 2] = SDL_Vertex { { dest.x + dx, y }, shade, { 0.F, v } };
			vertices[static_cast<size_t>(r) * 2 + 1] = SDL_Vertex { { dest.x + dest.w + dx, y }, shade, { 1.F, v } };
		}
		SDL_RenderGeometry(renderer, art.texture, vertices.data(), static_cast<int>(vertices.size()), indices.data(), used);
		pieces++;
	}
}

/**
 * @brief Draws the recorded world with HD Lighting.
 *
 * The floor is drawn fully lit and darkened by the light over it. The floor in front of each wall follows, then the
 * shadows on the floor (DrawJoinedShadows), then everything standing on the floor, back to front.
 * @return False where HD Lighting isn't used, and nothing was drawn.
 */
bool RenderLit(SDL_Renderer *renderer, Size worldSize, float toLogical, const std::array<float, 3> &tint)
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
	LitHeroX = heroX;
	LitHeroY = heroY;
	const Mood mood = CurrentMood();
	light::Look look;
	look.levelBrightness = LightLevel;
	look.falloff = mood.falloff;
	look.warm = mood.warm;
	look.cool = mood.cool;
	look.tint = tint;
	IndexDirt();
	light::SetOpenGround(IsOpenGround);
	if (!light::Update(look, x0, y0, x1, y1, heroX, heroY))
		return false;
	FrameLit = true;
	LastLightsOn.known = false;
	MakeBlobTexture(renderer);

	const Shading unlit = Uniform({ 1.F, 1.F, 1.F });
	for (size_t i = 0; i < FloorEnd && i < Draws.size(); i++) {
		if (!IsOutline(Draws[i].style))
			DrawShaded(renderer, Draws[i], unlit, toLogical, true);
	}
	{
		const FramePartScope part(FramePart::Atmosphere);
		DrawHeatHaze(renderer, toLogical);
	}
	DrawFloorLight(renderer, toLogical);
	for (size_t i = FloorEnd; i < Draws.size(); i++) {
		if (IsTileBase(Draws[i]))
			DrawTileFloor(renderer, Draws[i], toLogical);
	}
	// The shadows lie on the floor, under everything that stands on it.
	WallShades.clear();
	NextWallShade = std::numeric_limits<size_t>::max();
	LastTileDrawKnown = false;
	DrawingAt = 0;
	const bool joined = DrawJoinedShadows(renderer, worldSize, toLogical);
	for (size_t i = FloorEnd; i < Draws.size(); i++) {
		const Draw &draw = Draws[i];
		DrawingAt = i;
		if (i > 0)
			DrawWallShadesAfter(renderer, i - 1);
		if (IsOutline(draw.style))
			continue;
		if (!joined && draw.style == SpriteStyle::Normal && draw.caster != Caster::None)
			DrawCasterShadows(renderer, draw, toLogical);
		const Kept kept = joined ? ShadeKept[i] : Kept {};
		if (DrawShapedObject(renderer, draw, toLogical, (kept.foot + kept.top) / 2))
			continue;
		if (draw.kind == DrawKind::Tile && draw.hasTile && draw.light >= 0 && !IsTileBase(draw) && IsDirt(draw.cell)) {
			DrawGroundLit(renderer, draw, toLogical);
			continue;
		}
		Shading shading = LightFor(draw, tint);
		if (kept.foot < 1 || kept.top < 1)
			Dim(shading, kept);
		if (IsTileBase(draw)) {
			DrawTileWall(renderer, draw, shading, toLogical);
			continue;
		}
		// Heroes, monsters and solid objects cast shadows of their own, in place of the one painted under them.
		DrawShaded(renderer, draw, shading, toLogical, draw.caster == Caster::None);
	}
	if (!Draws.empty())
		DrawWallShadesAfter(renderer, Draws.size() - 1);
	WallShades.clear();
	// Item and monster outlines glow on top of the light.
	const Shading glow = Uniform(tint);
	for (size_t i = 0; i < Draws.size(); i++) {
		if (IsOutline(Draws[i].style))
			DrawOutline(renderer, i, glow, toLogical);
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
	PendingOutline = nullptr;
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

uint16_t PieceToDraw(Point tile)
{
	const uint16_t piece = dPiece[tile.x][tile.y];
	return Recording && RecordingLit ? light::UnpaintedPiece(tile.x, tile.y, piece) : piece;
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

namespace {

/** @brief Which in-between a sprite drawn now shows: 0 its own frame, 1-2 towards the next, 3-4 towards the one before. */
uint8_t InBetween()
{
	const int step = std::clamp(static_cast<int>(SubFrame * 3), 0, 2);
	return static_cast<uint8_t>(step != 0 && SubFrameBackward ? step + 2 : step);
}

/** @brief Unique monsters' colour tables by address, with their names (RegisterRecolour). */
std::unordered_map<const uint8_t *, std::string> Recolours;

/** @brief The name of the unique monster's colour table a sprite is drawn in, or null. */
const std::string *RecolourOf(const uint8_t *trn)
{
	if (trn == nullptr)
		return nullptr;
	const auto it = Recolours.find(trn);
	return it != Recolours.end() ? &it->second : nullptr;
}

/** @brief What the sprite being recorded is, for its light and shadows: an object only while feel("objects") is on. */
Caster RecordedCaster()
{
	if (IsObject(CurrentCaster) && atmosphere::Setting(atmosphere::Knob::Objects) <= 0)
		return Caster::None;
	return CurrentCaster;
}

} // namespace

void RecordSpriteImpl(const Surface &out, Point position, ClxSprite clx, const uint8_t *trn, SpriteStyle style, uint8_t outlineColor, const Lightmap *lightmap)
{
	const bool perPixel = lightmap != nullptr && *GetOptions().Graphics.perPixelLighting && !RecordingLit;
	const int pad = IsOutline(style) ? 1 : 0;
	// The game draws an outline just before its sprite, in the same place.
	const Point at { out.region.x + position.x, out.region.y + position.y - clx.height() + 1 };
	bool outlined = false;
	if (IsOutline(style)) {
		PendingOutline = clx.pixelData();
		PendingOutlineAt = at;
		PendingOutlineLook = CurrentOutlineLook;
	} else if (PendingOutline != nullptr) {
		outlined = PendingOutline == clx.pixelData() && PendingOutlineAt == at;
		PendingOutline = nullptr;
	}
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
	    // Two in-betweens per frame split it in thirds; played backwards they are 3 and 4.
	    .inBetween = InBetween(),
	    .caster = IsOutline(style) ? Caster::None : RecordedCaster(),
	    .floor = !FloorDone,
	    .hasTile = HaveTile,
	    .cell = CurrentTile,
	    .tileOrigin = CurrentTileOrigin + Displacement { RecordRegion.x, RecordRegion.y },
	    .lightmapped = lightmap != nullptr,
	    .outlined = outlined,
	    // A sprite with an outline keeps its outline's look, so its art's outlines are made in that cut as it loads.
	    .outlineLook = IsOutline(style) ? CurrentOutlineLook : (outlined ? PendingOutlineLook : OutlineLook {}),
	    .recolour = IsOutline(style) ? nullptr : RecolourOf(trn),
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
	const bool itemPictures = base.starts_with("data/inv/objcurs");
	const auto registerList = [&](ClxSpriteList list, size_t direction) {
		for (size_t frame = 0; frame < list.numSprites(); frame++) {
			char name[32];
			std::snprintf(name, sizeof(name), "/d%zu_f%02zu", direction, frame);
			SpriteNames[list[frame].pixelData()] = SpriteName { base + name, list[frame].width(), list[frame].height() };
			// Item pictures are drawn in the UI as well (RenderUiSprites).
			if (itemPictures)
				RegisterUiSprite(list[frame].pixelData(), base + name, list[frame].width(), list[frame].height());
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

void RegisterRecolour(const uint8_t *trn, std::string_view name)
{
	if (trn == nullptr)
		return;
	std::string lower(name);
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	// A table that changes no colour (Genrl: the Skeleton King, the Butcher, Na-Krul) shows its type's own art.
	bool changesNothing = true;
	for (int i = 0; i < 256 && changesNothing; i++)
		changesNothing = trn[i] == i;
	if (changesNothing)
		lower.clear();
	// A freed table's address is reused by the next one loaded, which names it again here.
	ArtChoices.clear();
	Recolours[trn] = std::move(lower);
}

void ForgetSprites(ClxSpriteListOrSheet sprites)
{
	ArtChoices.clear();
	const auto forgetList = [](ClxSpriteList list) {
		for (size_t frame = 0; frame < list.numSprites(); frame++)
			SpriteNames.erase(list[frame].pixelData());
	};
	if (sprites.isSheet()) {
		const ClxSpriteSheet sheet = sprites.sheet();
		for (size_t direction = 0; direction < sheet.numLists(); direction++)
			forgetList(sheet[direction]);
	} else {
		forgetList(sprites.list());
	}
}

bool HasArt(std::string_view name)
{
	auto [it, inserted] = ArtExists.try_emplace(std::string(name));
	if (inserted) {
		std::string path = "hd\\" + it->first + ".png";
		std::replace(path.begin(), path.end(), '/', '\\');
		it->second = FindAsset(path).ok();
	}
	return it->second;
}

bool DrawUiSprite(ClxSprite sprite, Rectangle dest, float subFrame)
{
	// Only while the frames go up in layers with the HD world under the UI (PresentLayeredFrame calls RenderUi).
	if (!Active() || UiRenderedTicks == 0 || SDL_GetTicks() - UiRenderedTicks > 1000)
		return false;
	const auto it = SpriteNames.find(sprite.pixelData());
	if (it == SpriteNames.end() || !HasArt(it->second.name))
		return false;
	UiSprite = PendingUiSprite { it->second.name, dest, static_cast<uint8_t>(std::clamp(static_cast<int>(subFrame * 3), 0, 2)) };
	return true;
}

void RenderUi(SDL_Renderer *renderer)
{
	UiRenderedTicks = std::max<uint32_t>(SDL_GetTicks(), 1);
	if (!UiSprite)
		return;
	const PendingUiSprite pending = *UiSprite;
	UiSprite = std::nullopt;
	// The art is loaded for the renderer the world was drawn with this frame.
	if (renderer == nullptr || renderer != CacheRenderer)
		return;
	SDL_Texture *texture = nullptr;
	if (pending.inBetween != 0)
		texture = LoadArt(renderer, pending.name + "_m" + std::to_string(pending.inBetween), false, nullptr).texture;
	if (texture == nullptr)
		texture = LoadArt(renderer, pending.name, false, nullptr).texture;
	if (texture == nullptr)
		return;
	SDL_SetTextureColorMod(texture, 255, 255, 255);
	SDL_SetTextureAlphaMod(texture, 255);
	const SDL_Rect dest { pending.dest.position.x, pending.dest.position.y, pending.dest.size.width, pending.dest.size.height };
	SDL_RenderCopy(renderer, texture, nullptr, &dest);
}

namespace {
// The CEL loader names its art through this pointer (hd_hooks.cpp); it is set before main runs.
[[maybe_unused]] const bool RegisterSpritesHookSet = (RegisterSpritesHook = RegisterSprites, true);
} // namespace

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
	ArtExists.clear();
}

/** @brief With HD Lift Dark Shades, black becomes this shade (out of 255); lighter shades are lifted less, white not at all. */
constexpr float DarkShadeLift = 10.F;

/**
 * @brief Lifts the HD world's darkest shades a little (HD Lift Dark Shades), as a screen blend over it. Some screens (VA
 * panels) are slow to brighten very dark shades, so a dark scene that moves looks darker there until it stops; from a
 * little above black they change faster. It fades with the screen, so a fade still goes to black.
 */
void LiftDarkShades(SDL_Renderer *renderer, const SDL_Rect &clip, const std::array<float, 3> &tint)
{
	if (!*GetOptions().Graphics.hdLiftDarks)
		return;
	SDL_BlendMode before = SDL_BLENDMODE_NONE;
	SDL_GetRenderDrawBlendMode(renderer, &before);
	bool lifted = false;
#if SDL_VERSION_ATLEAST(2, 0, 6)
	// What is there plus the lift times what it lacks of white.
	static const SDL_BlendMode Screen = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE_MINUS_DST_COLOR, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
	    SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
	lifted = SDL_SetRenderDrawBlendMode(renderer, Screen) == 0;
#endif
	// Renderers without it add the lift to every shade.
	if (!lifted && SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD) != 0)
		return;
	SDL_SetRenderDrawColor(renderer, ColourByte(DarkShadeLift / 255.F * tint[0]), ColourByte(DarkShadeLift / 255.F * tint[1]), ColourByte(DarkShadeLift / 255.F * tint[2]), 255);
	SDL_RenderFillRect(renderer, &clip);
	SDL_SetRenderDrawBlendMode(renderer, before);
}

/**
 * @brief The Atmosphere option's darker screen edges (idea 6): the corners and edges of the view a touch darker, more so
 * deeper down, so the eye stays on the hero. It darkens what is there by a share, so the dark stays dark and the
 * spells drawn after it still shine at the edges.
 */
void DrawEdges(SDL_Renderer *renderer, const SDL_Rect &clip)
{
	const float strength = atmosphere::Strength(atmosphere::Knob::Edges);
	if (strength <= 0)
		return;
	const FramePartScope part(FramePart::Atmosphere);
	const float darken = std::clamp(atmosphere::Value("edges", 0.22F) * strength, 0.F, 0.9F);
	if (darken <= 0.005F)
		return;
	if (EdgesTexture == nullptr) {
		constexpr int Size = 256;
		std::vector<uint8_t> rgba(static_cast<size_t>(Size) * Size * 4, 0);
		for (int y = 0; y < Size; y++) {
			for (int x = 0; x < Size; x++) {
				const float u = (static_cast<float>(x) + 0.5F) / (Size / 2.F) - 1.F;
				const float v = (static_cast<float>(y) + 0.5F) / (Size / 2.F) - 1.F;
				// Clear round the middle, darkening smoothly from about halfway out, darkest in the corners.
				const float d = std::sqrt(u * u + v * v);
				const float t = std::clamp((d - 0.55F) / (1.42F - 0.55F), 0.F, 1.F);
				const float shape = t * t * (3 - 2 * t);
				rgba[(static_cast<size_t>(y) * Size + x) * 4 + 3] = static_cast<uint8_t>(shape * 255.F + 0.5F);
			}
		}
		EdgesTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, Size, Size);
		if (EdgesTexture == nullptr)
			return;
		SDL_SetTextureBlendMode(EdgesTexture, SDL_BLENDMODE_BLEND);
		SDL_SetTextureScaleMode(EdgesTexture, SDL_ScaleModeLinear);
		SDL_UpdateTexture(EdgesTexture, nullptr, rgba.data(), Size * 4);
	}
	SDL_SetTextureAlphaMod(EdgesTexture, ColourByte(darken));
	const SDL_Rect dest = clip;
	SDL_RenderCopy(renderer, EdgesTexture, nullptr, &dest);
}

/** @brief How far past the edge of the view tiles have their pictures read ahead, in tiles. */
constexpr int ReadAheadTilesPast = 5;

/**
 * @brief Asks the reader for the pictures of the tiles in and just round the view, before they are drawn: as the hero
 * walks, a tile's picture is in by the time the tile comes into view. Only looks again when the view has moved a tile.
 */
void ReadAheadTiles(Size worldSize)
{
#ifdef POE2_HD_READER
	if (CacheCels == nullptr || CacheCels != pDungeonCels.get() || ViewPosition == ReadAheadView)
		return;
	ReadAheadView = ViewPosition;
	if (PieceReadAhead.size() != MAXTILES + 1)
		PieceReadAhead.assign(MAXTILES + 1, false);
	// Across the screen a tile is one step of x - y; down it, one of x + y. Tall tiles below the view reach up into it.
	const int across = worldSize.width / TILE_WIDTH + ReadAheadTilesPast;
	const int up = worldSize.height / TILE_HEIGHT + ReadAheadTilesPast;
	const int down = up + MicroTileLen;
	std::vector<std::string> wanted;
	for (int v = -up; v <= down; v++) {
		for (int u = -across; u <= across; u++) {
			if (((u + v) & 1) != 0)
				continue;
			const Point tile = ViewPosition + Displacement { (u + v) / 2, (v - u) / 2 };
			if (!InDungeonBounds(tile))
				continue;
			const uint16_t piece = dPiece[tile.x][tile.y];
			if (piece >= PieceReadAhead.size() || PieceReadAhead[piece])
				continue;
			PieceReadAhead[piece] = true;
			for (int block = 0; block < MicroTileLen; block++) {
				const LevelCelBlock cel = DPieceMicros[piece].mt[block];
				if (!cel.hasValue())
					continue;
				const auto it = TileNames.find(GetDunFrame(CacheCels, cel.frame()));
				if (it == TileNames.end())
					continue;
				const std::string &name = it->second;
				const size_t slash = name.find('/', 6);
				if (PaletteVariant > 1 && slash != std::string::npos)
					wanted.push_back(PicturePath(name.substr(0, slash) + "_p" + std::to_string(PaletteVariant) + name.substr(slash)));
				wanted.push_back(PicturePath(name));
				// A floor tile with something growing over it has that in a picture of its own.
				if (cel.type() == TileType::TransparentSquare)
					wanted.push_back(PicturePath(name + "f"));
			}
		}
	}
	WantPictures(wanted);
#else
	(void)worldSize;
#endif
}

/** @brief A frame's share of reading ahead: takes in what the reader has read, and asks for what the view will want next. */
void ReadAhead(Size worldSize)
{
	PictureMadeThisFrame = false;
	if (SettledFrames < SettlingFrames)
		SettledFrames++;
#ifdef POE2_HD_READER
	if (!ReaderOn())
		return;
	UpdateReadDirs();
	CollectReadPictures();
	TidyReadPictures();
	// A level's first frames read their own pictures as they draw; the reader then goes on to what lies round them.
	if (SettledFrames >= SettlingFrames)
		ReadAheadTiles(worldSize);
#else
	(void)worldSize;
#endif
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
		DropReadPictures();
		LightTexture = nullptr;
		FieldTexture = nullptr;
		BlobTexture = nullptr;
		EdgesTexture = nullptr;
		ShadeTexture = nullptr;
		ShadeTextureSize = {};
		ShadeFailed = false;
		CacheRenderer = renderer;
	}
	CheckPalette();
	ReadAhead(worldSize);
	if (Rasters.size() > 20000) {
		// Every picture on screen is made again in the next frame: a likely freeze, so the play log notes it.
		TelemetryHdCacheDropped(Rasters.size());
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
	OutlineMadeThisFrame = false;
	ShownOutlines.Prune(SDL_GetTicks());
	OutlineBudgetEnd = SDL_GetPerformanceCounter() + static_cast<uint64_t>(static_cast<double>(SDL_GetPerformanceFrequency()) * OutlineBudgetMs / 1000);
#if SDL_VERSION_ATLEAST(2, 0, 18)
	// HD Lighting works out its own light, in colour and stopped by walls.
	if (RecordingLit && HaveViewOrigin && FloorDone && RenderLit(renderer, worldSize, toLogical, tint)) {
		DrawEdges(renderer, clip);
		LiftDarkShades(renderer, clip, tint);
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
	for (size_t i = 0; i < Draws.size(); i++) {
		if (IsOutline(Draws[i].style))
			DrawOutline(renderer, i, glow, toLogical);
	}
	LiftDarkShades(renderer, clip, tint);
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
uint16_t PieceToDraw(Point tile) { return dPiece[tile.x][tile.y]; }
void RegisterSprites(std::string_view, ClxSpriteListOrSheet) { }
void RegisterRecolour(const uint8_t *, std::string_view) { }
void ForgetSprites(ClxSpriteListOrSheet) { }
bool HasArt(std::string_view) { return false; }
bool DrawUiSprite(ClxSprite, Rectangle, float) { return false; }
void RenderUi(SDL_Renderer *) { }
void ClearCaches() { }
void Render(SDL_Renderer *, Size, Size) { }
bool CoversView(Size) { return false; }
bool ExportLevelTiles(const std::string &) { return false; }
const char *PackPalette(uint8_t) { return nullptr; }

#endif

} // namespace hd

} // namespace devilution
