/**
 * @file poe2/rune_icons.hpp
 *
 * POE2 Mashup: runes are drawn in code (build spec step 3): a grey stone with its family's glyph carved in it, and a
 * glow that says its strength. The stash's Runes tab, the bag, the belt, the cursor and the info box all draw them so.
 */
#pragma once

#include <cstdint>

#include "engine/rectangle.hpp"

namespace devilution {

struct Item;
struct Surface;
enum class AugmentId : uint8_t;

/** @brief Whether the augment is drawn as a rune: a rune of a family, or a Rune of Legend. */
[[nodiscard]] bool IsDrawnRune(AugmentId id);
/** @brief Whether the item is a rune drawn in code (POE2 only). */
[[nodiscard]] bool IsDrawnRuneItem(const Item &item);
/**
 * @brief Draws the rune centred in the box.
 * @param dim Greyed, for a Runes tab slot with none stored: the glyph shows, but neither colour nor glow.
 */
void DrawRuneIcon(const Surface &out, const Rectangle &box, AugmentId id, bool dim);

} // namespace devilution
