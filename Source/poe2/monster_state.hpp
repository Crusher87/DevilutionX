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
	uint8_t freezeTicks = 0;
	/** @brief Cold damage built up towards a freeze, as a raw Fixed26_6 value. */
	int32_t freezeBuildup = 0;
	uint8_t heavyStunTicks = 0;
	/** @brief Physical damage built up towards a heavy stun, as a raw Fixed26_6 value. */
	int32_t stunBuildup = 0;
};

} // namespace devilution
