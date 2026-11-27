#pragma once

#include <cstddef>
#include <cstdint>

namespace devilution {

namespace hd {

// POE2 Mashup: keeping the HD world's tiles flush, without the renderer. A tile's HD picture is cut to the pixels the
// game draws for it; where the picture itself was a little see-through inside that shape (a soft edge, a stray hole),
// the black under the world showed as a thin dark line between two tiles or at a building's foot.

/** @brief How far from solid art a see-through pixel is filled, in HD pixels, with feel("seams") at 1. */
constexpr int SeamReach = 4;
/** @brief The most feel("seams") can ask for. */
constexpr int MostSeamReach = 64;
/** @brief A pixel of HD art from this alpha up is as good as solid: made solid in its own colour, wherever it is. */
constexpr uint8_t SeamSolidAlpha = 240;

/** @brief How far see-through pixels are filled for a feel("seams") setting: 0 off, 1 the usual, above 1 that many HD pixels. */
int SeamReachOf(float setting);

/**
 * @brief Cuts a tile's HD picture (RGBA bytes, straight alpha) to the game's own shape for it, and closes its seams.
 *
 * Outside the game's shape the picture is cut away, and where the game's pixel is partly see-through the picture's
 * alpha is scaled by it, as before. Where the game's pixel is solid, a picture pixel that isn't solid is made solid if
 * solid art lies within `reach` pixels of it: its colour is its own over the colour spread from the solid art next to
 * it, so a fully see-through pixel takes its neighbours' colour and a nearly solid one keeps its own. Solid pixels are
 * never changed, and only solid art inside the game's shape gives its colour. A hole wider than twice the reach keeps its middle, so art that is open on purpose stays open.
 * @param pixels The picture, `height` rows of `pitch` bytes, four bytes a pixel: red, green, blue, alpha.
 * @param cover The game's own tile at its own size: 255 solid, 0 nothing, between see-through. `coverWidth` a row.
 * @param scale How many picture pixels to a game pixel, each way.
 * @param reach 0 only cuts (the picture's own alpha times the cover), as the game did before.
 * @return How many pixels were made solid.
 */
int CutTileArt(uint8_t *pixels, int width, int height, int pitch, const uint8_t *cover, int coverWidth, int scale, int reach);

/**
 * @brief How a wall or floor piece is lit by its shape, from the light each of its ways of facing takes.
 *
 * Each way's light is measured against the light the piece takes flat (`flat`: a floor facing up, a wall facing out of
 * its face), which it already has. A way of facing that takes exactly the flat light comes out as 1, whatever the
 * lights are: so a flat floor measured against a floor facing up takes the same light on every tile, and shows no step
 * where two tiles meet.
 * @param shade The light each way of facing takes, `count` of them.
 * @param has Which ways the piece's shape map has anything facing.
 * @param base The piece's art is drawn in this times its flat light...
 * @param extra ...and each way's picture added in this times it (0 for ways it doesn't have).
 * @return False when it is as well drawn flat: nothing faces any way, or all its ways take about the same light.
 */
bool ShapeLightShares(const float *shade, const bool *has, size_t count, float flat, float strength, float &base, float *extra);

} // namespace hd

} // namespace devilution
