/**
 * @file poe2/loot.hpp
 *
 * POE2 Mashup loot rules from the itemization bible (research/itemization-bundle-v5): a kill gives a number of loot
 * rolls by how tough the monster was, each roll picks a category (equipment, gold, currency, ...), and equipment
 * then rolls its rarity from a table for that kind of monster. The numbers are the bundle's starting defaults.
 */
#pragma once

#include <array>
#include <cstdint>

#include "items.h"

namespace devilution {

struct Monster;

/** @brief How tough the thing that dropped the loot was. */
enum class LootSource : uint8_t {
	Normal,
	Magic,
	Rare,
	/** A named unique monster. */
	Miniboss,
	/** A quest boss: the Butcher, the Skeleton King, Lazarus, Diablo and the like. */
	Boss,
};

enum class LootCategory : uint8_t {
	Equipment,
	Gold,
	Currency,
	FlaskOrCharm,
	Gem,
	/** Waystones. */
	Endgame,
	/** Diablo's scrolls, oils, elixirs and the like. */
	Misc,
	Count,
};

/** @brief How valuable a currency drop is; each orb belongs to one band. */
enum class CurrencyBand : uint8_t {
	Common,
	Uncommon,
	Rare,
	Premium,
	Chase,
	Count,
};

/** @brief A random number below `bound` from the game's generator, without the skew Diablo's 15-bit GenerateRnd gives big ranges. */
int LootRandom(int bound);

LootSource GetLootSource(const Monster &monster);

/** @brief Average loot rolls per kill, in thousandths: 0.18 for a normal monster up to 6 for a boss. */
int ExpectedLootRollsPerMille(LootSource source);
/** @brief The whole rolls, plus one more with the chance of the fraction. */
int RollLootCount(LootSource source);

/** @brief The category weights out of 100; bosses favour equipment and give less gold. */
std::array<int, static_cast<size_t>(LootCategory::Count)> LootCategoryWeights(LootSource source);
LootCategory RollLootCategory(LootSource source);

/** @brief Chance in hundredths of a percent (out of 10000) of each rarity for an equipment roll. */
std::array<int, 4> EquipmentRarityWeights(LootSource source);
item_quality RollEquipmentRarity(LootSource source);

/** @brief Rare monsters drop items one level higher, uniques and bosses two. */
int LootItemLevelBonus(LootSource source);

CurrencyBand RollCurrencyBand();
CurrencyBand OrbCurrencyBand(item_misc_id miscId);

/** @brief Natural rares have 4, 5 or 6 affixes, weighted 8:3:1. */
int RollNaturalRareAffixCount();

/**
 * @brief Percent weight for a base whose minimum level is `baseLevel` dropping at `itemLevel`:
 * bases far below the area level drop much less often, so the deep dungeon isn't full of Rags.
 */
int BaseRecencyPercent(int itemLevel, int baseLevel);

/** @brief How often each kind of equipment drops, before its bases are picked. */
int EquipmentClassWeight(ItemType type);

/** @brief Gold piles are fewer in POE2, so each is worth more. */
constexpr int GoldPileMultiplier = 4;

/** @brief Extra chances a boss gives on top of its rolls: a guaranteed rare, maybe currency, maybe a unique. */
constexpr int BossBonusCurrencyPercent = 35;
constexpr int BossBonusUniquePercent = 8;

/** @brief The rarity a dropped item was given, stored on the item so it comes back the same when loaded. */
std::optional<item_quality> GetLootRarity(const Item &item);
void SetLootRarity(Item &item, item_quality rarity);

} // namespace devilution
