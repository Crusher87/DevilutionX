/**
 * @file poe2/defenses.hpp
 *
 * POE2 Mashup defenses: armour reduces physical hits, evasion avoids them and energy shield soaks damage before life.
 */
#pragma once

#include <cstdint>

#include "utils/fixed_point.hpp"

namespace devilution {

struct Item;
struct Player;

enum class DefenseType : uint8_t {
	Armour,
	Evasion,
	/** Half armour, half evasion, like POE2's mail. */
	ArmourEvasion,
	EnergyShield,
};

/** @brief Energy shield granted per point of defense on cloth bases. */
constexpr int EnergyShieldPerDefense = 4;
/** @brief POE2: energy shield starts recharging 4 seconds after the last hit. */
constexpr int EnergyShieldRechargeDelayTicks = 4 * 20;
/** @brief POE2: a recharging energy shield refills 12.5% of its maximum per second. */
constexpr int EnergyShieldRechargePerMille = 125;
/** @brief POE2: armour reduces physical damage by at most 90%. */
constexpr int MaxArmourReductionPercent = 90;

/** @brief Which defense an armor, helm or shield base grants. */
DefenseType ItemDefenseType(const Item &item);

/** @brief Armour from gear and class bonuses. */
int ArmourRating(const Player &player);
/** @brief Evasion from gear plus a fifth of dexterity; replaces armor class in monster hit rolls. */
int EvasionRating(const Player &player);
/** @brief The defense monster hit rolls use: evasion in POE2 mode, armor class otherwise. */
int HitDefense(const Player &player);

/** @brief Percent of a physical hit that armour removes: armour / (armour + 10 * damage), like POE2. */
int ArmourReductionPercent(int armour, Fixed26_6 damage);
/** @brief Physical damage after armour, in POE2 mode. */
Fixed26_6 ApplyArmour(const Player &player, Fixed26_6 damage);

/**
 * @brief Takes damage from energy shield first and restarts the recharge delay.
 * @return The damage left over for life.
 */
Fixed26_6 AbsorbWithEnergyShield(Player &player, Fixed26_6 damage);
/** @brief Advances energy shield recharge by one game tick. */
void RechargeEnergyShield(Player &player);

/** @brief Sets the new maximum, keeping the missing amount the same, like life does. */
void SetMaxEnergyShield(Player &player, int maxEnergyShield);

} // namespace devilution
