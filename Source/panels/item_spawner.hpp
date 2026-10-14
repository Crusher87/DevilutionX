/**
 * @file panels/item_spawner.hpp
 *
 * POE2 Mashup item spawner: a searchable item catalog for quickly testing new content.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#else
#include <SDL.h>
#endif

#include "engine/surface.hpp"
#include "items.h"

namespace devilution {

struct Player;

enum class SpawnerCategory : uint8_t {
	All,
	Weapons,
	Armor,
	Jewelry,
	Uniques,
	Gems,
	Orbs,
	Misc,
	Count,
};

struct SpawnerEntry {
	std::string name;
	/** @brief Short description shown next to the name, e.g. the item type and stat requirements. */
	std::string detail;
	SpawnerCategory category;
	_item_indexes baseIndex;
	/** @brief Index into UniqueItems, or -1. */
	int uniqueId = -1;
	item_cursor_graphic cursor;
	/** @brief Whether the item rolls with the chosen quality and level. */
	bool rollsQuality = false;
	/** @brief A unique flask or charm (BeltUniqueId), or 0. */
	uint8_t beltUnique = 0;
	/** @brief Which essence, omen, augment or catalyst (poe2/crafting_items.hpp), or 0. */
	uint8_t variant = 0;
};

std::string_view SpawnerCategoryName(SpawnerCategory category);

/** @brief Every item the spawner offers, grouped by category. */
std::vector<SpawnerEntry> BuildItemSpawnerCatalog();

/** @brief Entries in the category that contain every word of the query (case-insensitive). */
std::vector<size_t> FilterItemSpawnerCatalog(const std::vector<SpawnerEntry> &catalog, std::string_view query, SpawnerCategory category);

/**
 * @brief Creates the item for a catalog entry.
 * @return true if an item of that quality could be rolled.
 */
bool CreateSpawnerItem(const Player &player, const SpawnerEntry &entry, item_quality quality, int level, uint32_t seed, Item &item);

/** @brief Puts the item in the inventory, or on the ground at the player's feet when it doesn't fit. */
void GiveItemToPlayer(Player &player, Item &item);

bool IsItemSpawnerOpen();
void OpenItemSpawner();
void CloseItemSpawner();
void ToggleItemSpawner();

/** @return true if the event was handled by the spawner. */
bool ItemSpawnerHandleEvent(const SDL_Event &event);
void DrawItemSpawner(const Surface &out);

} // namespace devilution
