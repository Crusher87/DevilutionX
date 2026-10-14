/**
 * @file poe2/item_stats.hpp
 *
 * POE2 Mashup item stats that Diablo's item fields have no room for.
 */
#pragma once

#include <cstdint>

namespace devilution {

/** @brief Item stats Diablo's item fields have no room for; the player adds them up. */
enum class Poe2ItemStat : uint8_t {
	/** Increased attack speed in percent. */
	AttackSpeed,
	/** Increased cast speed in percent. */
	CastSpeed,
	Spirit,
	/** Increased minion damage in percent. */
	MinionDamage,
	MeleeSkillLevels,
	ProjectileSkillLevels,
	SpellSkillLevels,
	FireSkillLevels,
	LightningSkillLevels,
	Count,
};

} // namespace devilution
