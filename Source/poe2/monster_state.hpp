/**
 * @file poe2/monster_state.hpp
 *
 * POE2 Mashup per-monster state that only lasts while the level is loaded.
 */
#pragma once

#include <cstdint>

namespace devilution {

struct Poe2MonsterState {
	uint8_t igniteTicks = 0;
	int8_t igniteSource = -1;
	/** @brief Fire damage per tick as a raw Fixed26_6 value. */
	int32_t igniteDamagePerTick = 0;
	uint8_t shockTicks = 0;
	int regenRemainder = 0;
	uint8_t chillTicks = 0;
	/** @brief How much a chill slows the monster, in percent. */
	uint8_t chillPercent = 0;
	/** @brief Slowed ticks owed by the chill, in percent of a tick. */
	uint8_t chillCarry = 0;
	/** @brief Map hastes collect here; at 100 the monster takes an extra turn. */
	uint8_t hasteCarry = 0;
	uint8_t freezeTicks = 0;
	/** @brief Cold damage built up towards a freeze, as a raw Fixed26_6 value. */
	int32_t freezeBuildup = 0;
	/** @brief Bleeding from a unique's critical hits: physical damage per tick as a raw Fixed26_6 value. */
	uint8_t bleedTicks = 0;
	int8_t bleedSource = -1;
	int32_t bleedDamagePerTick = 0;
	/** @brief Exposure: takes more damage of one element (a DamageType). */
	uint8_t exposureTicks = 0;
	uint8_t exposureElement = 0;
	/** @brief Electrocuted: can't act and takes more lightning damage. */
	uint8_t electrocuteTicks = 0;
	/** @brief The ignite already spread to monsters around (Warden of Embers uniques). */
	bool igniteSpread = false;
	uint8_t heavyStunTicks = 0;
	/** @brief Physical damage built up towards a heavy stun, as a raw Fixed26_6 value. */
	int32_t stunBuildup = 0;
	/** @brief Cursed with Amplify Damage: takes more physical damage; the curse's strength in percent. */
	uint8_t amplifyTicks = 0;
	uint8_t amplifyPercent = 0;
	/** @brief Armour broken by Smite: takes more physical damage. */
	uint8_t armourBreakTicks = 0;
	/** @brief Curses from the skill table, in game ticks left: Weaken and Decrepify make it hit softer, Iron Maiden turns its
	 * melee hits back on it, Lower Resist and Death Mark make it take more damage, Confuse sets it on other monsters. */
	uint8_t weakenTicks = 0;
	uint8_t decrepifyTicks = 0;
	uint8_t ironMaidenTicks = 0;
	uint8_t lowerResistTicks = 0;
	uint8_t deathMarkTicks = 0;
	uint8_t confuseTicks = 0;
	/** @brief Confuse made the monster turn on its kind, so its flags go back when the curse ends. */
	bool confusedByCurse = false;
	/** @brief Haunt and Locust Swarm drain the monster and move on to another when it dies. */
	uint8_t hauntTicks = 0;
	uint8_t locustTicks = 0;
};

} // namespace devilution
