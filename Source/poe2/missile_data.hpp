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
	/** @brief Less damage in percent once it has pierced (Pierce I), and for each monster it pierces (Pierce III). */
	int8_t pierceLessPercent = 0;
	int8_t pierceEachLessPercent = 0;
	int8_t chainsLeft = 0;
	bool canFork = false;
	/** @brief The skill that fired the missile (a SkillSlot's kind and id), so its hits get that skill's supports. */
	uint8_t skillKind = 0;
	uint8_t skillId = 0;
	/** @brief Ricochet: bounces off the next wall towards a monster. */
	bool ricochet = false;
	/** @brief Fired by one of the hero's minions: it deals its own damage and hits like a spell, even an arrow. */
	bool minionShot = false;
	/** @brief How the effects draw it: 0 by its graphic, 1 the staff's own small bolt. */
	uint8_t look = 0;
	/**
	 * @brief The spell the play log credits this missile's hits to, when it isn't the last skill used: a rune's burst
	 * and what it throws out, however long after it was laid. For the log only: no supports or damage come of it.
	 */
	SpellID creditSpell = SpellID::Invalid;
	/** @brief The DamageType its hits deal instead of its graphic's (a granted skill's bolt on a flare), or -1 for the graphic's. */
	int8_t damageType = -1;
	/** @brief A per-target granted skill's cast this projectile belongs to (GrantedRuntime::castSerial), or 0. */
	uint16_t grantedCast = 0;
	/**
	 * @brief How the effects colour it, a vfx::Element, instead of its graphic's look (a bone bolt on a flare), or -1 for
	 * the graphic's. Its explosion takes it too. Cosmetic: never saved and never read by the game.
	 */
	int8_t vfxElement = -1;
};

} // namespace devilution
