#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "tables/spelldat.h"

namespace devilution {

namespace hd {

// The painted spell icons of the HD art, without the renderer: which files an icon is made of, and how its picture is
// put together and shrunk to the size it has on screen. Each icon is the shared stone plate in the colours the icon is
// drawn in (hd\spellicons\plate_<tint>.png) with its symbol and the symbol's shadow over it (hd\spellicons\big\NN.png
// for the 56x56 icons, small\NN.png for the 37x38 ones, NN being the icon's frame).

/** @brief The blank icon's frame (SpellIcon::Empty): the plate alone. */
constexpr uint8_t SpellIconEmptyFrame = 26;
/** @brief The last icon frame with a symbol (SpellIcon::Mana); the frames after it are the spell book's labels. */
constexpr uint8_t SpellIconLastFrame = 50;

/** @brief Whether an icon frame is shown with HD art at all (the label frames, 51 and up, never are). */
constexpr bool SpellIconHasHdArt(uint8_t frame)
{
	return frame <= SpellIconLastFrame;
}

/** @brief The plate's HD art name for the colours SetSpellTrans sets (spellicons/plate_spell); empty for a type it has none for. */
std::string_view SpellIconPlateName(SpellType tint);

/**
 * @brief The HD art name of an icon frame's symbol layer (spellicons/big/07, spellicons/small/07); empty for the blank
 * icon, which has none, and for the label frames.
 */
std::string SpellIconLayerName(uint8_t frame, bool large);

/**
 * @brief The name an icon's finished picture goes by: its plate's name, then for an icon with a symbol '+' and the
 * layer's name (spellicons/plate_spell+spellicons/big/07). Empty when the icon has no HD art (a label frame, or
 * colours with no plate).
 */
std::string SpellIconArtName(uint8_t frame, bool large, SpellType tint);

/**
 * @brief Lays one picture over another of the same size, both 4 bytes a pixel (red, green, blue, alpha) with straight
 * (not premultiplied) alpha; the result, in `under`, is straight alpha too.
 */
void LayPictureOver(uint8_t *under, int underPitch, const uint8_t *over, int overPitch, int width, int height);

/**
 * @brief Shrinks a picture (4 bytes a pixel, straight alpha) to a smaller size, each new pixel the average of the part
 * of the picture it covers (an area filter, so nothing is skipped and nothing shimmers). Colours are averaged by how
 * much they show (their alpha), so see-through pixels don't darken the edges. A size no smaller is filled the same way.
 */
void ShrinkPicture(const uint8_t *src, int srcWidth, int srcHeight, int srcPitch, uint8_t *dst, int dstWidth, int dstHeight, int dstPitch);

/**
 * @brief The first screen pixel a UI pixel covers when the UI is scaled up by `scale` taking the nearest pixel: the
 * first whose middle is in it. A UI rectangle from a to b covers the screen pixels from this of a up to this of b.
 */
int FirstScreenPixelOf(int uiPixel, float scale);

} // namespace hd

} // namespace devilution
