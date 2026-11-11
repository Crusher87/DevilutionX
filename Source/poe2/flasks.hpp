/**
 * @file poe2/flasks.hpp
 *
 * POE2 Mashup flasks: potions in the first belt slots are reusable and refill from kills.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace devilution {

struct Item;
struct Monster;
struct Player;

/** @brief Charges are kept for belt slots 1 to 5, where flasks went before there were two flask slots. */
constexpr size_t NumFlasks = 5;
/** @brief Belt slots 1 and 2 hold flasks, any mix of life and mana, like POE2's two; slots 3 to 8 hold charms (Curtis 2026-10-21). */
constexpr size_t FlaskBeltSlots = 2;
static_assert(FlaskBeltSlots <= NumFlasks);
/** @brief Flasks saved before the fifth slot. */
constexpr size_t OldNumFlasks = 4;
/** @brief A full flask has 6 uses, like POE2's 60-charge flasks. */
constexpr uint8_t FlaskChargesPerUse = 10;
constexpr uint8_t RejuvenationChargesPerUse = 20;
constexpr uint8_t MaxFlaskCharges = 60;
/** @brief An Ample flask holds 20 more charges. */
constexpr uint8_t AmpleFlaskCharges = 20;
constexpr uint8_t MaxFlaskChargesCap = MaxFlaskCharges + AmpleFlaskCharges;
/** @brief A Glassblower's Bauble adds 1% quality, up to 20%; each 1% adds 1% recovery. */
constexpr int MaxFlaskQuality = 20;
/** @brief Flask of the Springs: gains a charge every 3 seconds. */
constexpr int SpringsChargeTicks = 3 * 20;
/** @brief Flask of the Ox: 20% more armour while recovering. */
constexpr int OxArmourPercent = 20;
/** @brief Flask of Guarding: Guard for 2 seconds when drunk. */
constexpr int GuardingFlaskTicks = 2 * 20;
/** @brief Chance in 100 that a dropped flask is magic. */
constexpr int MagicFlaskChance = 35;

enum class FlaskPrefix : uint8_t {
	None,
	/** 40% more recovered. */
	Concentrated,
	/** Half recovered at once, 25% less in total. */
	Bubbling,
	/** All recovered at once, 40% less in total. */
	Seething,
	/** 20 more maximum charges. */
	Ample,
	/** 60% more recovered over twice the time. */
	Saturated,
	Count,
};

enum class FlaskSuffix : uint8_t {
	None,
	/** 20% more armour while recovering. */
	Ox,
	/** 25% fewer charges used. */
	Plenty,
	/** Gains a charge every 3 seconds. */
	Springs,
	/** Guard for 2 seconds when drunk. */
	Guarding,
	Count,
};
/** @brief Flasks come in 9 tiers by item level, like POE2's Lesser to Ultimate; "Full" potions are 2 tiers higher. */
constexpr size_t NumFlaskTiers = 9;
constexpr int FullFlaskTierBonus = 2;

struct FlaskTier {
	/** @brief Item level the tier starts at. */
	int level;
	int life;
	/** @brief Game ticks the life is recovered over. */
	int lifeTicks;
	int mana;
	int manaTicks;
};

/**
 * @brief POE2's life and mana flask bases (Path of Building's PoE2 data), at their required levels, which are area
 * levels here: a hero gains 12 life a level as in POE2 (Poe2LifePerLevel), so the amounts stay POE2's. The last tiers
 * drop in Nightmare, Hell and maps (Curtis 2026-10-30). Flasks saved before the ladder have Diablo's monster level,
 * 1 to 32, which lands on the first six tiers with about their old amounts.
 */
constexpr FlaskTier FlaskTiers[NumFlaskTiers] = {
	{ 1, 50, 3 * 20, 50, 2 * 20 },     // Lesser
	{ 4, 90, 5 * 20, 70, 3 * 20 },     // Medium
	{ 10, 150, 4 * 20, 90, 50 },       // Greater
	{ 16, 260, 5 * 20, 110, 50 },      // Grand
	{ 23, 340, 4 * 20, 165, 70 },      // Giant
	{ 30, 450, 4 * 20, 165, 50 },      // Colossal
	{ 40, 710, 5 * 20, 185, 2 * 20 },  // Gargantuan
	{ 50, 840, 4 * 20, 285, 70 },      // Transcendent
	{ 60, 920, 3 * 20, 310, 3 * 20 },  // Ultimate
};
static_assert(FlaskTiers[NumFlaskTiers - 1].level <= 63, "an item level is 6 bits (CF_LEVEL)");

/** @brief The tier a plain flask of the item level has, from 0 to NumFlaskTiers - 1. */
size_t FlaskTierForLevel(int itemLevel);

/** @brief Potions that work as flasks: healing, mana and rejuvenation. */
bool IsFlaskPotion(const Item &item);
/** @brief Whether an item base is a flask potion. */
bool IsFlaskBase(int itemIndex);
/**
 * @brief The item level a flask dropped where equipment is `areaLevel` is made at, which sets its tier: the area
 * level, as POE2's flasks go, within the 6 bits of CF_LEVEL. In multiplayer, Diablo's `itemLevel`, as items are
 * checked against monster levels there.
 */
int FlaskDropLevel(int areaLevel, int itemLevel);

/** @brief Whether the belt slot holds a flask (POE2 mode only). */
bool IsFlaskSlot(const Player &player, size_t beltSlot);
/** @brief Whether a belt slot takes the item: flasks in the first two slots, charms in the rest. */
bool BeltSlotTakes(const Item &item, size_t beltSlot);

bool CanUseFlask(const Player &player, size_t beltSlot);

/** @brief The flask's tier, from 0 to NumFlaskTiers - 1. */
size_t GetFlaskTier(const Item &item);
FlaskPrefix GetFlaskPrefix(const Item &item);
FlaskSuffix GetFlaskSuffix(const Item &item);
int GetFlaskQuality(const Item &item);
/** @brief Stores the flask's mods and quality and renames it to match. */
void SetFlaskMods(Item &item, FlaskPrefix prefix, FlaskSuffix suffix, int quality);
/** @brief Names the flask from its tier and mods, e.g. "Concentrated Greater Healing Flask of the Ox". */
void NameFlask(Item &item);
/**
 * @brief Gives a newly made flask its POE2 name, rolling mods for a dropped one; rebuilding it keeps the mods.
 * Does nothing to items that aren't flasks.
 */
void FinishFlask(Item &item);
uint8_t FlaskChargesPerUseFor(const Item &item);
uint8_t MaxFlaskChargesFor(const Item &item);
/** @brief Shown in the item's info box, e.g. "Recovers 240 life over 4 seconds". */
std::string FlaskDescription(const Item &item);
/** @brief The flask's mods and quality as info box lines. */
std::vector<std::string> FlaskModLines(const Item &item);
/** @brief The extra lines shown while Alt is held: affix names, charges, and how it refills. */
std::vector<std::string> FlaskAdvancedLines(const Item &item, const Player *player);
/** @brief Armour bonus percent while an Ox flask is recovering. */
int FlaskArmourPercent(const Player &player);

/**
 * @brief Drinks the flask in a belt slot without using up the potion.
 * @return false (with a message) when the flask doesn't have enough charges.
 */
bool UseFlask(Player &player, size_t beltSlot);

/** @brief Flask charges gained for helping to kill a monster. */
uint8_t FlaskChargesForKill(const Monster &monster);

void GainFlaskCharges(Player &player, uint8_t charges);

/** @brief Life and mana the passive tree recovers when the hero helps kill a monster. */
void RecoverOnKill(Player &player);

void RefillFlasks(Player &player);

/** @brief Recovers the life and mana of flasks being drunk; once per game tick. */
void ProcessFlaskRecovery(Player &player);

} // namespace devilution
