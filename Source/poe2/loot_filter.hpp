/**
 * @file poe2/loot_filter.hpp
 *
 * POE2 Mashup loot filter: hides low quality gear on the ground.
 */
#pragma once

#include <cstdint>
#include <optional>

#include "options.h"

namespace devilution {

struct Item;

/** @brief Whether the filter hides this ground item; only normal (and magic) weapons, armor and jewelry are ever hidden. */
bool IsItemHiddenByLootFilter(const Item &item, LootFilter filter);
/** @brief Same, using the Loot Filter gameplay option. */
bool IsItemHiddenByLootFilter(const Item &item);

/**
 * @brief The faint rarity outline every ground item gets in POE2 mode so it stands out from the floor.
 *
 * A palette index, or nothing when the item gets no outline (outside POE2 mode). The item under the cursor keeps its
 * brighter hover outline instead. A loot filter style can override this colour.
 */
std::optional<uint8_t> GroundItemOutlineColor(const Item &item);

/**
 * @brief Darkens a ground outline to the light on the item's tile, so items in the dark stay hidden.
 *
 * Takes a shade from a 16-shade palette group and the tile's light table index (0 brightest, 15 black); the outline
 * fades a shade per two light steps and is gone where the floor is near black.
 */
std::optional<uint8_t> LitGroundOutline(uint8_t color, int lightTableIndex);

} // namespace devilution
