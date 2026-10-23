/**
 * @file poe2/regen.hpp
 *
 * POE2 Mashup regeneration: mana regenerates on its own, life only from gear.
 * Also POE2's life and mana pools, worked out from the hero's level and Magic each time the hero's stats are counted.
 */
#pragma once

#include "utils/fixed_point.hpp"

namespace devilution {

struct Player;

/** @brief Base mana regeneration in percent of maximum mana per second, like in POE2. */
constexpr int BaseManaRegenPercent = 4;

/** @brief Life every class gains a level in POE2 mode, like POE2's +12. */
constexpr int Poe2LifePerLevel = 12;
/** @brief POE2's mana pool for every class: 30 + 4 a level + 2 a Magic point. */
constexpr int Poe2BaseMana = 30;
constexpr int Poe2ManaPerLevel = 4;
constexpr int Poe2ManaPerMagic = 2;
/** @brief For mana, a class counts at least this much starting Magic (only the Barbarian has less). */
constexpr int Poe2MinManaMagic = 10;
/** @brief Highest maximum life or mana in POE2 mode (Diablo caps both at 2,000). */
constexpr int MaxPoe2LifeMana = 100000;

/**
 * @brief Life the hero gets on top of the saved base life, so the base grows by Poe2LifePerLevel a level instead of the
 * class's Diablo gain. Not saved: the saved base stays on Diablo's rules, so loading a hero never changes it.
 */
Fixed26_6 Poe2LevelLife(const Player &player);
/**
 * @brief Mana the hero gets on top of the saved base mana, so the base follows POE2's pool rule instead of Diablo's.
 * Not saved, like Poe2LevelLife; losses to shrines stay in the saved base.
 */
Fixed26_6 Poe2LevelMana(const Player &player);
/** @brief Mana from Magic on gear and the tree: 2 a point for every class in POE2 mode, the class's own rate in Diablo. */
int ManaFromItemMagic(const Player &player, int magic);

/** @brief Applies one game tick of life and mana regeneration (POE2 mode only). */
void ApplyRegen(Player &player);

} // namespace devilution
