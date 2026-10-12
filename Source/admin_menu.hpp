/**
 * @file admin_menu.hpp
 *
 * POE2 Mashup admin tools: an in-game menu for quickly testing new content.
 */
#pragma once

#include <string>

#include "items.h"

namespace devilution {

struct Player;

/** @brief Opens the admin tools from the in-game menu. */
void AdminMenuOpen(bool bActivate);

/**
 * @brief Rolls an item of the given base and quality, the way a monster of the given level would drop it.
 * @return true if an item of that quality could be rolled.
 */
bool AdminGenerateItem(const Player &player, Item &item, _item_indexes baseIndex, item_quality quality, int level, uint32_t firstSeed);

/** @brief Adds gold to the player's inventory, filling existing piles first. Returns the amount added. */
int AdminGiveGold(Player &player, int amount);

} // namespace devilution
