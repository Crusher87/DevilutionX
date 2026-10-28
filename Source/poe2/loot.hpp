/**
 * @file poe2/loot.hpp
 *
 * POE2 Mashup loot rules from the itemization bible (research/itemization-bundle-v5): a kill gives a number of loot
 * rolls by how tough the monster was, each roll picks a category (equipment, gold, currency, ...), and equipment
 * then rolls its rarity from a table for that kind of monster. The category weights are the bundle's starting
 * defaults; the roll counts and rarity tables are lower, closer to POE2's campaign (research/loot-2026-10-23).
 */
#pragma once

#include <array>
#include <cstdint>

#include "items.h"

namespace devilution {

struct Monster;
struct Player;

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

/** @brief Average loot rolls per kill, in thousandths: 0.15 for a normal monster up to 4 for a boss. */
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
/** @brief The POE2 item level a currency starts dropping at. */
int CurrencyDropLevel(item_misc_id miscId);
/** @brief How often a currency drops compared with the others of its band. */
int CurrencyBandWeight(item_misc_id miscId);

/**
 * @brief The POE2 area level an item starts dropping at, for the items whose drop level follows POE2 rather than the
 * item table, 0 for the rest: jewels 20, the Amethyst Charm 40 and the Golden Charm 50, as in POE2.
 */
int Poe2AreaDropLevel(item_misc_id miscId);
/** @brief Hellfire's thrown trap runes, which never drop in POE2 mode; POE2's socket runes are augments, and still drop. */
bool IsHellfireRune(item_misc_id miscId);
/** @brief The area level POE2 drop levels are checked against: the map's inside a map, otherwise the floor's POE2 item level. */
int LootAreaLevel(int floorItemLevel);

/** @brief Lineage supports are POE2's endgame finds: they drop only in maps, area level 65 and up, never in the campaign. */
bool LineageSupportsDropAt(int areaLevel);
/** @brief In a map, one kill in this many drops a random lineage support; map bosses and signature bosses have their own 2%. */
constexpr int LineageAnyKillChance = 10000;

/** @brief Natural rares have 3 to 6 affixes, weighted 3:13:6:2, as RareModCount (poe2/modifiers.hpp). */
int RollNaturalRareAffixCount();

/**
 * @brief Percent weight for a base whose minimum level is `baseLevel` dropping at `itemLevel`:
 * bases far below the area level drop much less often, so the deep dungeon isn't full of Rags.
 */
int BaseRecencyPercent(int itemLevel, int baseLevel);

/** @brief How often each kind of equipment drops, before its bases are picked. */
int EquipmentClassWeight(ItemType type);

/**
 * @brief Equipment drops by bucket: its item type, except that the caster weapons rework's wands, sceptres and staves
 * and the quarterstaves have buckets of their own after the types (caster-weapons-plan-v2.md 6.5).
 */
enum class KindBucket : uint8_t {
	Wand,
	Sceptre,
	CasterStaff,
	Quarterstaff,
	Count,
};
constexpr size_t FirstKindBucket = static_cast<size_t>(ItemType::Amulet) + 1;
constexpr size_t EquipmentBucketCount = FirstKindBucket + static_cast<size_t>(KindBucket::Count);
/** @brief The drop bucket of an item row. */
size_t EquipmentBucketOf(int itemIndex);
/** @brief How often a bucket drops: its type's EquipmentClassWeight, or the kind's own weight. */
int EquipmentBucketWeight(size_t bucket);

/** @brief The highest POE2 item level, where the top modifier tier unlocks. */
constexpr int MaxPoe2ItemLevel = 82;

/**
 * @brief POE2-scale item level for loot from an area: 1.3 × the area level, plus the source's bonus, from 1 to 82.
 * The area level is Diablo's monster level plus 15 per difficulty, so Normal runs 1-41, Nightmare 21-61 and Hell 40-80.
 */
int Poe2ItemLevel(int areaLevel, int bonus = 0);
/**
 * @brief The POE2 item level of a monster's drops: the area level, +1 from rare monsters and +2 from uniques and
 * bosses (AreaItemLevel); without the area level table, Poe2CurrencyLevel.
 */
int Poe2ItemLevel(const Monster &monster, int difficulty);
/** @brief The item level of loot from an area: its area level plus the source's bonus, from 1 to 82. */
int AreaItemLevel(int areaLevel, int bonus = 0);
/**
 * @brief The POE2 item level a monster's currency drops check against: 1.3 × its Diablo level, as before item levels
 * moved to area levels, so each orb starts dropping where it did (Chaos at 10 ... Perfect at 58).
 */
int Poe2CurrencyLevel(const Monster &monster, int difficulty);

/** @brief Town vendors stock wares this far above the deepest area the player has reached. */
constexpr int VendorAreaLevelBonus = 2;

/**
 * @brief The premium stock takes POE2 equipment from this part of the vendors' area level up (Diablo's takes a quarter
 * of its level up). A sixth, since POE2's bases step from level 1 straight to 11 or 12: a quarter left areas 8-11 none.
 */
constexpr int PremiumAreaLevelDivisor = 6;

/**
 * @brief The area level town vendors stock for: the deepest floor the player has visited (floor 1 before any),
 * plus 15 per difficulty step, plus VendorAreaLevelBonus, at most MaxPoe2ItemLevel. It sets only the POE2 item
 * level and the POE2 base of a ware; CF_LEVEL keeps Diablo's vendor level. 0 when the area level table isn't loaded.
 */
int VendorAreaLevel(const Player &player, int difficulty);

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
