#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "engine/clx_sprite.hpp"
#include "engine/point.hpp"
#include "engine/rectangle.hpp"
#include "engine/size.hpp"

#ifdef USE_SDL1
#include "utils/sdl2_to_1_2_backports.h"
#else
struct SDL_Renderer;
#endif

namespace devilution {

struct Surface;
class Lightmap;
enum class TileType : uint8_t;
enum class MaskType : uint8_t;

/**
 * @brief POE2 Mashup HD graphics.
 *
 * The game still draws its 8-bit world every frame. While it does, every tile and sprite draw is
 * also recorded here. The recording is then replayed in full colour at twice the size: with the
 * upscaled art from the HD pack where it exists, and with the game's own pixels doubled where it
 * does not. Only used while zoomed in, so the world keeps its 2x size.
 */
namespace hd {

enum class SpriteStyle : uint8_t {
	Normal,
	Blended,
	Outline,
	/** @brief An outline that leaves out the sprite's painted shadow (palette colour 0), as the world's outlines do. */
	OutlineSkipZero,
};

/** @brief True while the world is being drawn into the 8-bit buffer for an HD frame. */
extern bool Recording;

/** @brief How far the sprite being drawn is towards its next animation frame (0 = exactly on a frame). */
extern float SubFrame;

/** @brief Sets SubFrame while a character is drawn. */
struct SubFrameScope {
	explicit SubFrameScope(float fraction) { SubFrame = fraction; }
	~SubFrameScope() { SubFrame = 0; }
	SubFrameScope(const SubFrameScope &) = delete;
	SubFrameScope &operator=(const SubFrameScope &) = delete;
};

/** @brief Who a sprite being drawn is: heroes and monsters cast shadows away from the lights around them. */
enum class Caster : uint8_t {
	None,
	Monster,
	/** @brief A monster in its own colours (a unique's), still in the light it stands in. */
	MonsterRecoloured,
	/** @brief A monster drawn in a colour that already says how it's lit (stone, infravision, enraged): kept as it is. */
	MonsterUnlit,
	Hero,
	/** @brief The hero being played, drawn bright whatever the light (the game's rule), still in its colour. */
	MyHero,
};

/** @brief Set while a hero or monster is drawn. */
extern Caster CurrentCaster;

struct CasterScope {
	explicit CasterScope(Caster caster) { CurrentCaster = caster; }
	~CasterScope() { CurrentCaster = Caster::None; }
	CasterScope(const CasterScope &) = delete;
	CasterScope &operator=(const CasterScope &) = delete;
};

/** @brief The dungeon tile whose walls and sprites are being drawn, and its position in the world buffer (DrawDungeon's). */
extern Point CurrentTile;
extern Point CurrentTileOrigin;
extern bool HaveTile;

struct TileScope {
	TileScope(Point tile, Point origin)
	{
		CurrentTile = tile;
		CurrentTileOrigin = origin;
		HaveTile = true;
	}
	~TileScope() { HaveTile = false; }
	TileScope(const TileScope &) = delete;
	TileScope &operator=(const TileScope &) = delete;
};

/** @brief The HD Graphics option is on and an HD pack is installed. */
bool Enabled();

/** @brief This frame is drawn in HD. */
bool Active();

/**
 * @brief Starts recording the world draws into the given 8-bit buffer region.
 * @param viewport Where the doubled world shows, in world buffer pixels.
 */
void BeginWorld(const Surface &out, Rectangle viewport);
void EndWorld();

/** @brief Where the tiles sit this frame: the view's first tile and where it is drawn in the world buffer (DrawGame's). */
void SetViewOrigin(Point tile, Point bufferPosition);

/** @brief The floor is drawn: everything recorded from here on stands on it (walls, objects, heroes, monsters). */
void EndFloor();

using RecordTileFn = void (*)(const Surface &out, const Lightmap &lightmap, Point position, TileType tile, const uint8_t *src, int height, MaskType mask, const uint8_t *tbl);
using RecordSpriteFn = void (*)(const Surface &out, Point position, ClxSprite clx, const uint8_t *trn, SpriteStyle style, uint8_t outlineColor, const Lightmap *lightmap);

/**
 * @brief Set by the HD renderer before it starts recording.
 *
 * Pointers (in hd_hooks.cpp) so the small render libraries and their tests don't link the whole renderer.
 */
extern RecordTileFn RecordTileHook;
extern RecordSpriteFn RecordSpriteHook;

inline void RecordTile(const Surface &out, const Lightmap &lightmap, Point position, TileType tile, const uint8_t *src, int height, MaskType mask, const uint8_t *tbl)
{
	RecordTileHook(out, lightmap, position, tile, src, height, mask, tbl);
}

/** @param lightmap The per-pixel light the sprite is drawn with, if any. */
inline void RecordSprite(const Surface &out, Point position, ClxSprite clx, const uint8_t *trn, SpriteStyle style, uint8_t outlineColor = 0, const Lightmap *lightmap = nullptr)
{
	RecordSpriteHook(out, position, clx, trn, style, outlineColor, lightmap);
}

/** @brief Names every frame of a loaded sprite file, so its HD art can be found. */
void RegisterSprites(std::string_view path, ClxSpriteListOrSheet sprites);

/** @brief Draws the recorded world in full colour. */
void Render(SDL_Renderer *renderer, Size worldSize, Size logicalSize);

/**
 * @brief This frame's HD world covers the whole world buffer of the given size, so the game's own 8-bit world
 * under it can't show anywhere and needn't be sent to the screen.
 */
bool CoversView(Size worldSize);

/** @brief The palette the HD pack's art for a dungeon type is made in (the first of its palettes). */
const char *PackPalette(uint8_t levelType);

/**
 * @brief Writes every dungeon frame of the loaded level, fully lit, as PNGs under `dir`/tiles/<level>/,
 * plus pieces.json saying where each frame sits in its dungeon piece (for the HD pack maker).
 */
bool ExportLevelTiles(const std::string &dir);

/** @brief Forgets all cached textures (for example when the renderer is recreated). */
void ClearCaches();

} // namespace hd

} // namespace devilution
