/**
 * @file poe2/loot_filter.hpp
 *
 * POE2 Mashup loot filter: hides low quality gear on the ground.
 */
#pragma once

#include "options.h"

namespace devilution {

struct Item;

/** @brief Whether the filter hides this ground item; only normal (and magic) weapons, armor and jewelry are ever hidden. */
bool IsItemHiddenByLootFilter(const Item &item, LootFilter filter);
/** @brief Same, using the Loot Filter gameplay option. */
bool IsItemHiddenByLootFilter(const Item &item);

} // namespace devilution
