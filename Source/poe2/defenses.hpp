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
/** @brief A hit stuns the player (hit recovery) only when it takes at least this percent of their maximum life. */
constexpr int PlayerStunPercent = 15;
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

/** @brief Resistances drop by this much for each dungeon area reached past the Cathedral, like POE2's act penalties. */
constexpr int ResistancePenaltyPerArea = 10;
/** @brief And by this much more on Nightmare and again on Hell. */
constexpr int ResistancePenaltyPerDifficulty = 15;
/** @brief The lowest a resistance can go with the penalty, so monsters deal at most 60% more. */
constexpr int MinPoe2Resistance = -60;

/** @brief The deepest dungeon level the character has reached (0 in town before ever leaving it). */
int DeepestDungeonLevel(const Player &player);
/** @brief How much the character's resistances are reduced, from depth and difficulty. */
int ResistancePenalty(const Player &player);
/** @brief Applies the penalty and the POE2 bounds to a resistance. */
int Poe2Resistance(int resistance, int penalty, int maxResistance);

/** @brief Sets the new maximum, keeping the missing amount the same, like life does. */
void SetMaxEnergyShield(Player &player, int maxEnergyShield);

} // namespace devilution
