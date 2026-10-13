/**
 * @file poe2/gem_levels.hpp
 *
 * POE2 Mashup gem level curves: how much stronger and costlier each skill gem gets from level 1 to 20.
 *
 * Every gem follows the shape of POE2's gems: big steps early, smaller ones later (Boneshatter goes
 * 100%, 142%, 191%, 244%, 312% at levels 1, 5, 10, 15, 20). Each gem has its own total growth, taken
 * from a similar POE2 gem, so a level is worth more on some skills than on others.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "poe2/player_data.hpp"

namespace devilution {

struct Player;

/**
 * @brief How far along its curve a gem is at each level, in thousandths: 0 at level 1, 1000 at level 20.
 * From Boneshatter's damage (100% to 312%), filled in between POE2's published levels.
 */
constexpr std::array<int16_t, MaxGemLevel> GemCurveShape { 0, 77, 154, 231, 308, 360, 412, 465, 517, 569, 612, 655, 698, 741, 784, 827, 870, 914, 957, 1000 };

/** @brief A gem's growth from level 1 to level 20, in percent of its level 1 value. */
struct GemCurve {
	/** Damage at level 20 (100 means it doesn't grow with the gem level, because the spell grows on its own). */
	int16_t damageAt20;
	/** Mana cost at level 20. */
	int16_t manaAt20;
	/** Extra projectiles: one more every this many levels (0 for none). */
	uint8_t projectileEvery;
	/** The POE2 gem its growth was taken from. */
	std::string_view poe2Gem;
};

/** @brief The curve of the gem that grants the skill. */
GemCurve GetGemCurve(SkillSlot skill);
/** @brief Value at a gem level for a curve that is 100 at level 1 and endAt20 at level 20. */
int GemCurvePercent(int endAt20, int gemLevel);

/** @brief Damage at a gem level, in percent of the damage at level 1. */
int GemDamagePercent(SkillSlot skill, int gemLevel);
/** @brief Mana cost at a gem level, in percent of the cost at level 1. */
int GemManaPercent(SkillSlot skill, int gemLevel);
/** @brief Projectiles the gem adds at a gem level. */
int GemExtraProjectiles(SkillSlot skill, int gemLevel);
/** @brief Mana cost at a gem level before class and support changes. */
int GemBaseManaCost(SkillSlot skill, int gemLevel);
/** @brief Extra melee damage while Battle Cry is up, in percent, at a gem level. */
int BattleCryDamagePercent(int gemLevel);

/** @brief One line of a gem's numbers at a level, like "level 7: 160% damage, 13 mana". */
std::string GemLevelText(SkillSlot skill, int gemLevel);
/** @brief The gem's whole curve, levels 1 to 20, for the console. */
std::string GemCurveText(SkillSlot skill);
/** @brief The curve of the gem with this name (any case), for the console's gemcurve("name"). */
std::string GemCurveTextByName(std::string_view name);
/** @brief Every socketed gem: its level, its numbers now and at the next level, and what the next level needs. */
std::string SocketedGemsText(const Player &player);

} // namespace devilution
