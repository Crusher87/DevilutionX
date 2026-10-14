/**
 * @file poe2/waystones.hpp
 *
 * POE2 Mashup endgame: waystones open maps. A map rebuilds a deep dungeon level from the waystone's seed and
 * fills it with tougher monsters. Like POE2's, waystones come in 16 tiers, are normal, magic or rare with up to
 * six modifiers that pair a danger with a reward, take currency, and every map ends with a boss.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "poe2/map_mods.hpp"

namespace devilution {

struct Item;
struct Monster;
struct Player;
enum class CurrencyOrb : uint8_t;

constexpr int MaxWaystoneTier = 16;
/** @brief The item level of a waystone's map: 64 plus its tier (bundle waystone_bases). */
constexpr int MapAreaLevelBase = 64;
/** @brief Per tier, map monsters get this much more life and damage, in percent. */
constexpr int MapLifePercentPerTier = 10;
constexpr int MapDamagePercentPerTier = 5;
/** @brief Per tier, this many more monsters per thousand are magic, and rare. */
constexpr int MapMagicPerMillePerTier = 12;
constexpr int MapRarePerMillePerTier = 6;
/** @brief A waystone has at most three prefixes and three suffixes, and as many revives as the six it could have less its modifiers. */
constexpr int WaystoneModsPerSide = 3;
constexpr int MaxMapRevives = 6;
/** @brief The map boss: this much more life per tier on top of four times its own, and more damage. */
constexpr int MapBossLifePercent = 400;
constexpr int MapBossLifePercentPerTier = 25;
constexpr int MapBossDamagePercent = 40;
/** @brief Chance the map boss drops a waystone of the map's tier, and of one tier higher. */
constexpr int MapBossWaystonePercent = 70;
constexpr int MapBossUpgradePercent = 25;

bool IsWaystone(const Item &item);
/** @brief The tier an older waystone or one from the item spawner gets from its Diablo item level. */
int WaystoneTierForLevel(int itemLevel);
/** @brief The tier of a waystone dropping at a POE2 item level: 1 below area level 66, then one per level. */
int WaystoneTierForItemLevel(int poe2ItemLevel);
int WaystoneTier(const Item &item);
int WaystoneModCount(const Item &item);
/** @brief Revives the map gives: six less the waystone's modifiers. */
int WaystoneRevives(const Item &item);
/** @brief Tablets the map takes: none for 0-1 modifiers, one for 2-3, two for 4-5, three for 6. */
int WaystoneTabletSlots(const Item &item);

/** @brief Rolls a waystone's tier, rarity and modifiers from its seed, and names it. */
void RollWaystone(Item &item, int tier);
/** @brief Names a waystone after its rarity and modifiers; rolls it first if it has nothing yet. */
void FinishWaystone(Item &item);
/** @brief The waystone's tooltip lines. */
std::vector<std::string> WaystoneText(const Item &item);
/**
 * @brief Uses currency on a waystone, like on equipment: Transmutation, Augmentation, Regal, Alchemy, Exalted,
 * Chaos, Annulment, Scouring, Divine and Vaal Orbs.
 * @return false (with a message) if the orb can't be used on it.
 */
bool CraftWaystone(CurrencyOrb orb, const Player &player, Item &item);

/** @brief The dungeon level a map of this tier rebuilds: Hell for low tiers, the Crypt (Hellfire) from tier 5. */
int MapLevelForTier(int tier, uint32_t seed);

/** @brief Tier of the map the local player is in, or 0 outside maps. */
int ActiveMapTier();
/** @brief What the map the local player is in adds up to; all zero outside maps. */
const MapEffects &ActiveMapEffects();
/** @brief Works out the open map's effects again, after it opens or loads. */
void RefreshMapEffects(const Player &player);

/**
 * @brief Rebuilds the map's level from the seed and remembers the map; doesn't travel there.
 * @return The dungeon level to travel to.
 */
int PrepareMap(Player &player, int tier, uint32_t seed);

/** @brief Opens the map of the waystone the player used, if they're in town; the caller then removes the waystone. */
bool UseWaystone(Player &player, Item &waystone);

/** @brief Forgets the map once the player goes to another dungeon level; call after a level loads. */
void UpdateActiveMap(Player &player);

/** @brief Picks the map boss among the monsters of a freshly made map level and makes it stronger. */
void ChooseMapBoss();
[[nodiscard]] bool IsMapBoss(const Monster &monster);
/** @brief The map boss died: the map is complete; drops its waystones. */
void OnMapBossKilled(const Monster &monster, bool sendmsg);
/** @brief The local player would die in a map: uses a revive to bring them back at the map's start instead. */
bool TryMapRevive(Player &player);

} // namespace devilution
