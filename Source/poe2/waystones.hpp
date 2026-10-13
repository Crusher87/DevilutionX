/**
 * @file poe2/waystones.hpp
 *
 * POE2 Mashup endgame: waystones open maps. A map rebuilds a deep dungeon level from the waystone's
 * seed and fills it with tougher monsters, more of them magic or rare the higher the tier.
 */
#pragma once

#include <cstdint>

namespace devilution {

struct Item;
struct Player;

constexpr int MaxWaystoneTier = 10;
/** @brief Per tier, map monsters get this much more life and damage, in percent. */
constexpr int MapLifePercentPerTier = 15;
constexpr int MapDamagePercentPerTier = 8;
/** @brief Per tier, this many more monsters per thousand are magic, and rare. */
constexpr int MapMagicPerMillePerTier = 20;
constexpr int MapRarePerMillePerTier = 10;

bool IsWaystone(const Item &item);
/** @brief Tier 1 from item level 15, one more every 5 levels, up to 10. */
int WaystoneTierForLevel(int itemLevel);
int WaystoneTier(const Item &item);

/** @brief The dungeon level a map of this tier rebuilds: Hell for low tiers, the Crypt (Hellfire) from tier 5. */
int MapLevelForTier(int tier, uint32_t seed);

/** @brief Tier of the map the local player is in, or 0 outside maps. */
int ActiveMapTier();

/**
 * @brief Rebuilds the map's level from the seed and remembers the map; doesn't travel there.
 * @return The dungeon level to travel to.
 */
int PrepareMap(Player &player, int tier, uint32_t seed);

/** @brief Opens the map of the waystone the player used, if they're in town; the caller then removes the waystone. */
bool UseWaystone(Player &player, Item &waystone);

/** @brief Forgets the map once the player goes to another dungeon level; call after a level loads. */
void UpdateActiveMap(Player &player);

} // namespace devilution
