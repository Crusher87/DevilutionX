/**
 * @file poe2/defenses.hpp
 *
 * POE2 Mashup defenses: armour reduces physical hits, evasion avoids them and energy shield soaks damage before life.
 */
#pragma once

#include <array>
#include <cstdint>

#include "utils/fixed_point.hpp"

namespace devilution {

struct Item;
struct Monster;
struct Player;

enum class DefenseType : uint8_t {
	Armour,
	Evasion,
	/** Half armour, half evasion, like POE2's mail. */
	ArmourEvasion,
	EnergyShield,
	/** Half armour, half energy shield, like POE2's vestments and mantles. */
	ArmourEnergyShield,
	/** Half evasion, half energy shield, like POE2's garbs and jackets. */
	EvasionEnergyShield,
	/** A third each of armour, evasion and energy shield (Grand Regalia). */
	All,
};

/** @brief The fixed bonus some top POE2 body armour bases carry. */
struct BodyArmourImplicit {
	int life = 0;
	int spirit = 0;
	int allResistances = 0;
	int magicResistance = 0;
	int manaRegenPercent = 0;
	int evasionPercent = 0;
};

/** @brief Energy shield granted per point of defense on cloth bases. */
constexpr int EnergyShieldPerDefense = 3;
/** @brief POE2 light stun: a hit's chance is this percent of its share of the target's maximum life (PoB2 StunBaseMult). */
constexpr int LightStunMultiplierPercent = 200;
/** @brief POE2 light stun: a chance this low or lower doesn't stun at all (PoB2 MinStunChanceNeeded). */
constexpr int LightStunMinChancePercent = 20;
/** @brief POE2: energy shield starts recharging 4 seconds after the last hit. */
constexpr int EnergyShieldRechargeDelayTicks = 4 * 20;
/** @brief POE2: a recharging energy shield refills 12.5% of its maximum per second. */
constexpr int EnergyShieldRechargePerMille = 125;
/** @brief POE2: armour reduces physical damage by at most 90%. */
constexpr int MaxArmourReductionPercent = 90;

/** @brief Which defense an armor, helm or shield base grants. */
DefenseType ItemDefenseType(const Item &item);
/** @brief The implicit of the item's base, all zero for bases without one. */
BodyArmourImplicit GetBodyArmourImplicit(const Item &item);
/**
 * @brief How a hybrid base divides its defence (base_meta.tsv's split column): the percent of each kind of its line
 * but the last, which takes the rest. None (parts 0) divides it evenly, as items did before the bases took POE2's
 * numbers.
 */
struct DefenseSplit {
	uint8_t parts = 0;
	std::array<uint8_t, 2> percent {};
};

/**
 * @brief Splits an item's defense between armour, evasion and energy shield points (energy shield not yet
 * multiplied by EnergyShieldPerDefense), evenly or by the base's split. The first kind gets the rounding remainder.
 */
void SplitDefense(DefenseType type, int defense, int &armour, int &evasion, int &energyShield, const DefenseSplit &split = {});
/** @brief Armour, evasion and energy shield, energy shield as shown (already multiplied by EnergyShieldPerDefense). */
struct DefensePoints {
	int armour = 0;
	int evasion = 0;
	int energyShield = 0;
};
/**
 * @brief Splits an item's defence like SplitDefense, but the points its flat armour, evasion and energy shield modifiers
 * gave go to their own kind. Identified or not, so identifying an item never changes its defence (Curtis 2026-10-22).
 * @param base The defence from the base and flat modifiers.
 * @param bonus What the item's quality and % increases add to it: POE2 multiplies the two, (1 + increased) x (1 + quality).
 */
void SplitItemDefense(const Item &item, DefensePoints &base, DefensePoints &bonus);

/** @brief Armour from gear and class bonuses, and Barkskin's unless it's left out (the build log leaves it out). */
int ArmourRating(const Player &player, bool withSpiritBuffs = true);
/** @brief Evasion from gear plus a fifth of dexterity, and Wind Dancer's stages unless left out; replaces armor class in monster hit rolls. */
int EvasionRating(const Player &player, bool withSpiritBuffs = true);
/** @brief The defense monster hit rolls use: evasion in POE2 mode, armor class otherwise. */
int HitDefense(const Player &player);

/** @brief Percent of a physical hit that armour removes: armour / (armour + 10 * damage), like POE2. */
int ArmourReductionPercent(int armour, Fixed26_6 damage);
/** @brief Physical damage after armour, in POE2 mode, for a hit that lands on the player: Iron Ward stores what it stops. */
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

/** @brief POE2's Accuracy Rating for each character level and each point of Dexterity. */
constexpr int AccuracyPerLevel = 6;
constexpr int AccuracyPerDexterity = 6;
/** @brief POE2: attacks keep all their accuracy out to 2 metres (tiles here), then lose it evenly down to a tenth at 9. */
constexpr int AccuracyFalloffStartTiles = 2;
constexpr int AccuracyFalloffEndTiles = 9;
constexpr int MinAccuracyAtRangePercent = 10;

/** @brief The hero's Accuracy Rating, like POE2: 6 a level and 6 a point of Dexterity, raised by War Banner. */
int AccuracyRating(const Player &player);
/**
 * @brief A monster's Evasion Rating against the hero's attacks. It grows with the monster's level, so that a hero of the
 * same level with ordinary Dexterity hits about 9 in 10; Diablo's armour class makes some kinds harder to hit than others.
 * @param armorClass The monster's armour class after the hero's armour piercing.
 */
int MonsterEvasionRating(const Monster &monster, int armorClass);
/** @brief The hero's chance to hit, in percent, up close against an ordinary monster of the hero's level (the character panel). */
int TypicalHitChance(const Player &player);
/** @brief The share of its accuracy, in percent, an attack keeps at a distance in tiles (see AccuracyFalloffStartTiles). */
int AccuracyAtDistancePercent(int tiles);
/** @brief POE2's chance to hit, in percent: 125 x accuracy / (accuracy + 0.3 x evasion), from 5 to 100. */
int Poe2HitChance(int accuracy, int evasion);
/**
 * @brief POE2's chance, in percent, that a hit light stuns: twice the hit's share of the target's maximum life, up to
 * 100, and none at all when that comes to 20% or less. Both values in the same unit.
 */
int LightStunChance(int64_t damage, int64_t maxLife);

/**
 * @brief Whether a monster's attacks on the hero use POE2's accuracy against evasion: in POE2 mode, for monsters on the
 * monster level tables (not the player's minions). Others keep Diablo's to-hit roll.
 */
bool UsesPoe2MonsterAccuracy(const Monster &monster);
/**
 * @brief A monster's Accuracy Rating: the monster level table's accuracy at its level times clamp(baseToHit / its type's
 * to-hit ref, 30-250%) (plan v2 step 4, audit C7/N7).
 * @param baseToHit Its data or unique to-hit plus the attack's own offset, without the Nightmare/Hell bonus (the area
 * level carries the difficulty).
 */
int MonsterAccuracyRating(const Monster &monster, int baseToHit);
/**
 * @brief Chance in percent that a monster's attack hits the hero: POE2's accuracy against evasion, with the attack's
 * accuracy cut by distance (AccuracyAtDistancePercent for a shot). No character level term and no floor but POE2's 5%.
 */
int MonsterHitChance(const Monster &monster, int baseToHit, int evasion, int distancePercent = 100);
/**
 * @brief The hero's chance to block a monster's blow (@p melee) or missile, before the cap. In POE2 neither the hero's
 * nor the monster's level counts; Diablo keeps -2 x monster level on blows and -2 x the level gap on missiles.
 * @param monster nullptr for a trap's missile.
 */
int BlockChanceAgainstMonster(const Player &player, const Monster *monster, bool melee);

} // namespace devilution
