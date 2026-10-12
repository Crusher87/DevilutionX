/**
 * @file poe2/flasks.hpp
 *
 * POE2 Mashup flasks: potions in the first belt slots are reusable and refill from kills.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace devilution {

struct Item;
struct Monster;
struct Player;

/** @brief Belt slots 1 to 4 hold flasks. */
constexpr size_t NumFlasks = 4;
constexpr uint8_t FlaskChargesPerUse = 10;
constexpr uint8_t MaxFlaskCharges = 30;

/** @brief Potions that work as flasks: healing, mana and rejuvenation. */
bool IsFlaskPotion(const Item &item);

/** @brief Whether the belt slot holds a flask (POE2 mode only). */
bool IsFlaskSlot(const Player &player, size_t beltSlot);

bool CanUseFlask(const Player &player, size_t beltSlot);

/**
 * @brief Drinks the flask in a belt slot without using up the potion.
 * @return false (with a message) when the flask doesn't have enough charges.
 */
bool UseFlask(Player &player, size_t beltSlot);

/** @brief Flask charges gained for helping to kill a monster. */
uint8_t FlaskChargesForKill(const Monster &monster);

void GainFlaskCharges(Player &player, uint8_t charges);

void RefillFlasks(Player &player);

} // namespace devilution
