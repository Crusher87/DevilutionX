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
	/** Jewels for the passive tree; they take the bible's augment weight until augments exist. */
	Jewel,
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

/** @brief A map's loot bonuses in percent, 100 being none: item rarity, item quantity and waystone drops. */
struct LootBonus {
	int rarity = 100;
	int quantity = 100;
	int waystones = 100;
	/** @brief How often equipment drops, and uniques among it (the Atlas keystones). */
	int equipment = 100;
	int unique = 100;
};

/** @brief Average loot rolls per kill, in thousandths: 0.18 for a normal monster up to 6 for a boss. */
int ExpectedLootRollsPerMille(LootSource source);
/** @brief The whole rolls, plus one more with the chance of the fraction. */
int RollLootCount(LootSource source, const LootBonus &bonus = {});

/** @brief The category weights out of 100; bosses favour equipment and give less gold. */
std::array<int, static_cast<size_t>(LootCategory::Count)> LootCategoryWeights(LootSource source, const LootBonus &bonus = {});
LootCategory RollLootCategory(LootSource source, const LootBonus &bonus = {});

/** @brief Chance in hundredths of a percent (out of 10000) of each rarity for an equipment roll. */
std::array<int, 4> EquipmentRarityWeights(LootSource source, const LootBonus &bonus = {});
item_quality RollEquipmentRarity(LootSource source, const LootBonus &bonus = {});

/** @brief How sought-after a unique is; a unique roll picks the tier first, then a unique of that tier. */
enum class UniqueTier : uint8_t {
	Common,
	Uncommon,
	Rare,
	Mythic,
	Count,
};

/** @brief Chance of each unique tier out of 10000: 84/12/3.5/0.5%, or 65/23/10/2% from rare monsters and bosses. */
std::array<int, static_cast<size_t>(UniqueTier::Count)> UniqueTierWeights(LootSource source);
UniqueTier RollUniqueTier(LootSource source);
/** @brief The tier of a Diablo unique by its level: 8 and below common, 9 to 19 uncommon, 20 and up rare. */
UniqueTier LegacyUniqueTier(int minLevel);

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

/** @brief The highest POE2 item level, where the top modifier tier unlocks. */
constexpr int MaxPoe2ItemLevel = 82;

/**
 * @brief POE2-scale item level for loot from an area: 1.3 × the area level, plus the source's bonus, from 1 to 82.
 * The area level is Diablo's monster level plus 15 per difficulty, so Normal runs 1-41, Nightmare 21-61 and Hell 40-80.
 */
int Poe2ItemLevel(int areaLevel, int bonus = 0);
/** @brief The POE2 item level of a monster's drops. */
int Poe2ItemLevel(const Monster &monster, int difficulty);

/** @brief The item's POE2 item level, or 0 if it has none (older items, and anything that isn't equipment). */
int GetPoe2ItemLevel(const Item &item);
/** @brief Stores the POE2 item level, and marks the item to roll its affixes from the itemization tables; only for equipment, whose bits aren't used by flask mods or gem levels. */
void SetPoe2ItemLevel(Item &item, int level);

/** @brief What a vendor pays for an item worth `value`: 18% in POE2, a quarter in Diablo. Never less than 1. */
int VendorSellValue(int value);

/** @brief Gold piles are fewer in POE2, so each is worth more. */
constexpr int GoldPileMultiplier = 4;

/** @brief Extra chances a boss gives on top of its rolls: a guaranteed rare, maybe currency, maybe a unique. */
constexpr int BossBonusCurrencyPercent = 35;
constexpr int BossBonusUniquePercent = 8;

/** @brief The rarity a dropped item was given, stored on the item so it comes back the same when loaded. */
std::optional<item_quality> GetLootRarity(const Item &item);
void SetLootRarity(Item &item, item_quality rarity);

} // namespace devilution
