/**
 * @file poe2/gem_levels.hpp
 *
 * POE2 Mashup gem level curves: how much stronger and costlier each skill gem gets from level 1 to 20.
 *
 * Each gem follows a POE2 gem level by level, so some levels pay off more than others, as in POE2: Boneshatter
 * steps +10% a level early, dips to +4% at level 9 and climbs to +5% a level by 20, and Leap Slam holds about +7% a
 * level, then speeds up again near 20. Attack gems copy their POE2 gem's total; spells take the shape of
 * POE2's spells on top of what Diablo's spell level already adds.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "poe2/player_data.hpp"

namespace devilution {

struct Player;

/**
 * @brief A POE2 gem's numbers at levels 1 to 20, copied from poe2db.tw. Damage is the gem's damage % (or, for
 * spells, its minimum plus maximum damage); mana is its cost. Only the ratios between levels are used.
 */
struct Poe2GemTable {
	std::string_view name;
	std::array<int16_t, MaxGemLevel> damage;
	std::array<int16_t, MaxGemLevel> mana;
};

/** @brief Levels at which POE2's Spark gains a projectile (4 at level 1, 9 at level 20). */
constexpr std::array<uint8_t, 5> SparkProjectileLevels { 2, 7, 11, 15, 19 };

/** @brief How a gem grows from level 1 to 20, following a POE2 gem level by level. */
struct GemCurve {
	/** The POE2 gem whose level steps it follows, big and small steps alike. */
	const Poe2GemTable *table;
	/** Damage at level 20 in percent of level 1; 0 copies the POE2 gem's own growth, 100 means no growth from the gem. */
	int16_t damageAt20;
	/** Mana cost at level 20 in percent of level 1, along the POE2 gem's mana steps. */
	int16_t manaAt20;
	/** Gains a projectile at the levels POE2's Spark does. */
	bool sparkProjectiles;
	/** Why it follows that gem. */
	std::string_view note;
	/** The POE2 gem whose damage steps it follows when they differ from its mana steps; nullptr: the same gem. */
	const Poe2GemTable *damageSteps = nullptr;
};

/** @brief The curve of the gem that grants the skill. */
GemCurve GetGemCurve(SkillSlot skill);
/** @brief Value at a gem level, in percent of level 1, following the POE2 gem's steps to reach endAt20 (0: the POE2 gem's own total). */
int GemCurvePercent(const std::array<int16_t, MaxGemLevel> &steps, int endAt20, int gemLevel);

/** @brief Damage at a gem level along POE2's Fireball steps, scaled to reach endAt20 at level 20 (the table spells' curve). */
int FireballCurvePercent(int endAt20, int gemLevel);
/** @brief Damage at a gem level, in percent of the damage at level 1: the gem's curve alone. */
int GemCurveDamagePercent(SkillSlot skill, int gemLevel);
/**
 * @brief A use's damage at a gem level, in percent: the gem's curve times the skill's damage for its speed
 * (SkillSpeedDamagePercent: a slower skill hits harder).
 */
int GemDamagePercent(SkillSlot skill, int gemLevel);
/** @brief Mana cost at a gem level, in percent of the cost at level 1. */
int GemManaPercent(SkillSlot skill, int gemLevel);
/** @brief Projectiles the gem adds at a gem level. */
int GemExtraProjectiles(SkillSlot skill, int gemLevel);
/**
 * @brief Mana cost at a gem level before class and support changes: the base cost along the gem's curve, times the
 * skill's mana for its speed (SkillSpeedManaPercent), to the nearest point when that isn't 100.
 */
int GemBaseManaCost(SkillSlot skill, int gemLevel);
/** @brief Extra melee damage while Battle Cry is up, in percent, at a gem level. */
int BattleCryDamagePercent(int gemLevel);

/** @brief One line of a gem's numbers at a level, like "level 7: 160% damage, 13 mana". */
std::string GemLevelText(SkillSlot skill, int gemLevel);
/** @brief The gem's whole curve, levels 1 to 20, for the console. */
std::string GemCurveText(SkillSlot skill);
/** @brief The curve of the gem with this name (any case), for the console's gemcurve("name"). */
std::string GemCurveTextByName(std::string_view name);
/** @brief How much a level up adds, in percent, e.g. "+11% damage, +10% mana"; marks the bigger steps. */
std::string GemStepText(SkillSlot skill, int fromLevel, int toLevel);
/** @brief Every socketed gem: its level, its numbers now and at the next level, and what the next level needs. */
std::string SocketedGemsText(const Player &player);

} // namespace devilution
