#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "engine/clx_sprite.hpp"
#include "engine/point.hpp"
#include "engine/rectangle.hpp"
#include "engine/size.hpp"

struct SDL_Renderer;

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

void RecordTile(const Surface &out, const Lightmap &lightmap, Point position, TileType tile, const uint8_t *src, int height, MaskType mask, const uint8_t *tbl);
/** @param lightmap The per-pixel light the sprite is drawn with, if any. */
void RecordSprite(const Surface &out, Point position, ClxSprite clx, const uint8_t *trn, SpriteStyle style, uint8_t outlineColor = 0, const Lightmap *lightmap = nullptr);

/** @brief Names every frame of a loaded sprite file, so its HD art can be found. */
void RegisterSprites(std::string_view path, ClxSpriteListOrSheet sprites);

/** @brief Draws the recorded world in full colour. */
void Render(SDL_Renderer *renderer, Size worldSize, Size logicalSize);

/**
 * @brief Writes every dungeon frame of the loaded level, fully lit, as PNGs under `dir`/tiles/<level>/,
 * plus pieces.json saying where each frame sits in its dungeon piece (for the HD pack maker).
 */
bool ExportLevelTiles(const std::string &dir);

/** @brief Forgets all cached textures (for example when the renderer is recreated). */
void ClearCaches();

} // namespace hd

} // namespace devilution
