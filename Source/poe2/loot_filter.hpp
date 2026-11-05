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

} // namespace devilution
