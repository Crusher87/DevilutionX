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

/** @brief Belt slots 1 to 5 hold flasks. */
constexpr size_t NumFlasks = 5;
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
/** @brief Flasks come in 6 tiers by item level; "Full" potions are 2 tiers higher. */
constexpr size_t NumFlaskTiers = 6;
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

/** @brief POE2's flask bases from poe2db, scaled to Diablo's smaller life pools. */
constexpr FlaskTier FlaskTiers[NumFlaskTiers] = {
	{ 1, 60, 3 * 20, 50, 2 * 20 },
	{ 6, 110, 4 * 20, 70, 3 * 20 },
	{ 12, 170, 4 * 20, 90, 50 },
	{ 18, 240, 4 * 20, 120, 50 },
	{ 25, 330, 4 * 20, 160, 3 * 20 },
	{ 32, 450, 3 * 20, 210, 50 },
};

/** @brief Potions that work as flasks: healing, mana and rejuvenation. */
bool IsFlaskPotion(const Item &item);

/** @brief Whether the belt slot holds a flask (POE2 mode only). */
bool IsFlaskSlot(const Player &player, size_t beltSlot);

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

void RefillFlasks(Player &player);

/** @brief Recovers the life and mana of flasks being drunk; once per game tick. */
void ProcessFlaskRecovery(Player &player);

} // namespace devilution
