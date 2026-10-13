/**
 * @file poe2/flasks.hpp
 *
 * POE2 Mashup flasks: potions in the first belt slots are reusable and refill from kills.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

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
uint8_t FlaskChargesPerUseFor(const Item &item);
/** @brief Shown in the item's info box, e.g. "Recovers 240 life over 4 seconds". */
std::string FlaskDescription(const Item &item);

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
