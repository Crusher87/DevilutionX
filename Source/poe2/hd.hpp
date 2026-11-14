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

/** @brief The sprite being drawn moves towards the frame before it (townspeople play some frames backwards). */
extern bool SubFrameBackward;

/** @brief Sets SubFrame while a character is drawn. */
struct SubFrameScope {
	explicit SubFrameScope(float fraction, bool backward = false)
	{
		SubFrame = fraction;
		SubFrameBackward = backward;
	}
	~SubFrameScope()
	{
		SubFrame = 0;
		SubFrameBackward = false;
	}
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

/**
 * @brief How an outline the game draws in the world shows in HD: thicker round rare monsters and bosses, with a slow
 * glow round bosses and unique or quest items, and fading in on the thing under the cursor.
 */
struct OutlineLook {
	bool thick = false;
	bool glow = false;
	/** @brief It marks the thing under the cursor, so it fades in. */
	bool hovered = false;
	/** @brief The thing it goes round (OutlineThing), so the cursor's outline fades in from the one it had; 0 for none. */
	uint32_t thing = 0;
};

/** @brief Kinds of things with outlines, for OutlineThing. */
enum class ThingKind : uint8_t {
	Item = 1,
	Monster,
	Towner,
	Object,
	Player,
};

/** @brief A number for a thing with an outline that only that thing has. */
[[nodiscard]] inline uint32_t OutlineThing(ThingKind kind, int index)
{
	return (static_cast<uint32_t>(kind) << 24) | (static_cast<uint32_t>(index) & 0xFFFFFF);
}

/** @brief Set while the game draws an outline in the world. */
extern OutlineLook CurrentOutlineLook;

struct OutlineScope {
	explicit OutlineScope(OutlineLook look) { CurrentOutlineLook = look; }
	~OutlineScope() { CurrentOutlineLook = {}; }
	OutlineScope(const OutlineScope &) = delete;
	OutlineScope &operator=(const OutlineScope &) = delete;
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

/**
 * @brief The level piece to draw at a tile (dPiece coordinates): the level's own, except while a world lit with HD
 * Lighting is recorded. Then the shadows Diablo painted into the floor and walls (beside pillars, arches and wall ends,
 * all falling the same way) are left out, since that light casts its own, each away from its light.
 */
uint16_t PieceToDraw(Point tile);

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

using RecordUiSpriteFn = void (*)(const Surface &out, Point position, ClxSprite clx, const uint8_t *trn);

/**
 * @brief Notes a picture drawn in the UI (set by the HD renderer; null without it). Item pictures (inventory, stash,
 * belt, shops, the cursor) are shown with their HD art over the UI (PrepareUiSprites, RenderUiSprites).
 */
extern RecordUiSpriteFn RecordUiSpriteHook;

using RecordUiPictureFn = void (*)(const Surface &out, const Rectangle &box, const std::string &name, const uint8_t *before);

/**
 * @brief Notes a picture the game draws in code in the UI (a rune), with what was in its box before it went on (set by
 * the HD renderer; null without it). `before` holds the box's pixels a row at a time, box.size.width to a row. Where
 * there is HD art for it (hd/<name>.png), the art shows over the UI in the box as an item picture's does, where the
 * pixels it drew still show; without the art, or with HD off, the game's own drawing stays.
 */
extern RecordUiPictureFn RecordUiPictureHook;

/** @brief An item picture frame (data/inv/objcurs) and its HD art's name. */
void RegisterUiSprite(const uint8_t *pixels, const std::string &name, uint16_t width, uint16_t height);

/** @brief A one-cell shrunken copy of an item picture: it shows the full picture's HD art, shrunk the same way. */
void RegisterFittedUiSprite(ClxSprite small, ClxSprite full);

/**
 * @brief Before the UI overlay is made from the UI frame: where an item picture with HD art still shows, the frame
 * shows what was under it instead, since its HD art goes over the UI (RenderUiSprites). RestoreUiSprites puts it back.
 */
void PrepareUiSprites();
void RestoreUiSprites();

/** @brief Draws the HD art of this frame's item pictures over the UI, where their own pixels still show. */
void RenderUiSprites(SDL_Renderer *renderer);

/** @brief Names every frame of a loaded sprite file, so its HD art can be found. */
void RegisterSprites(std::string_view path, ClxSpriteListOrSheet sprites);

/** @brief Forgets the names of a sprite file's frames, before its pixels are freed (so other pictures don't take its art). */
void ForgetSprites(ClxSpriteListOrSheet sprites);

/** @brief Whether the HD pack has art of this name, as RegisterSprites names a frame (e.g. monsters/zombie/zombien/d0_f00). */
bool HasArt(std::string_view name);

/**
 * @brief Shows a sprite's HD art over the UI this frame, filling @p dest (UI pixels), for a preview in a panel.
 * Only one such sprite is shown at a time.
 * @param subFrame How far the sprite is towards its next frame, for the in-between drawings.
 * @return false when HD art isn't shown or the sprite has none: the caller then draws the game's own pixels.
 */
bool DrawUiSprite(ClxSprite sprite, Rectangle dest, float subFrame);

/** @brief Draws what DrawUiSprite asked for, over the UI. */
void RenderUi(SDL_Renderer *renderer);

using RegisterSpritesFn = void (*)(std::string_view path, ClxSpriteListOrSheet sprites);

/**
 * @brief RegisterSprites when the HD renderer is linked in, otherwise null.
 *
 * A pointer (in hd_hooks.cpp) so the CEL loader and the tests of the libraries using it don't link the whole renderer.
 */
extern RegisterSpritesFn RegisterSpritesHook;

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
