/**
 * @file poe2/missile_data.hpp
 *
 * POE2 Mashup state carried by a player's spell projectile, set from the spell's support gems.
 */
#pragma once

#include <cstdint>

#include "tables/spelldat.h"

namespace devilution {

struct Poe2MissileData {
	/** @brief The spell whose support gems shape this missile; Invalid for missiles without supports. */
	SpellID spell = SpellID::Invalid;
	/** @brief Monster this missile last hit, so piercing and chaining never hit it twice in a row. */
	int16_t lastHitMonster = -1;
	/** @brief Damage multiplier in percent. */
	int16_t damagePercent = 100;
	/** @brief Speed multiplier in percent, kept by forks and chains. */
	int16_t speedPercent = 100;
	int8_t piercesLeft = 0;
	int8_t chainsLeft = 0;
	bool canFork = false;
};

} // namespace devilution
