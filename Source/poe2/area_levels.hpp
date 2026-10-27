/**
 * @file poe2/area_levels.hpp
 *
 * POE2 Mashup area levels: every dungeon floor has one (txtdata/poe2/area_levels.tsv), and it sets the level of the
 * monsters there. Loot keeps Diablo's monster level until item levels move to area levels.
 */
#pragma once

namespace devilution {

struct Monster;

/** @brief Nightmare and Hell add this much to a floor's area level per step, until Acts 4-6 exist. */
constexpr int AreaLevelsPerDifficulty = 15;

/** @brief The level of the monsters in a map of a tier: 41 at tier 1, 59 at tier 8, 80 at tier 16 (interim curve). */
constexpr int MapMonsterAreaLevel(int tier)
{
	return 38 + ((26 * tier) + 5) / 10;
}

/** @brief A floor's monster damage percent when its row gives none. */
constexpr int DefaultAreaMonsterDamagePercent = 100;

/** @brief Reads the area level table; does nothing (and never fails) outside POE2 mode. */
void LoadAreaLevels();

/** @brief The Normal area level of a raw dungeon level, or 0 when the table has no row for it. */
int AreaLevelForFloor(int floor);

/**
 * @brief The percent of their table hit the monsters of a raw dungeon level deal on Normal (the table's
 * monsterDamagePercent column: 70 in Act 1, plan step 14); 100 without a row.
 */
int AreaMonsterDamagePercentForFloor(int floor);

/**
 * @brief The floor whose area level applies here: the quest's floor on a quest level, otherwise the raw dungeon level.
 *
 * Never ItemsGetCurrlevel(), which folds the Hive and the Crypt onto floors 9-17; the table is keyed by the raw level.
 */
int AreaFloor();

/**
 * @brief The area level here: the map curve in a map, else the floor's table value plus 15 per difficulty step,
 * 1 in town, and 0 when the floor has no row.
 */
int CurrentAreaLevel(int difficulty);
/** @brief CurrentAreaLevel() at the game's difficulty. */
int CurrentAreaLevel();

/**
 * @brief The percent of their hit the monsters here deal: the floor's AreaMonsterDamagePercentForFloor() on Normal
 * (Act 1's monsters hit 30% softer); 100 in maps, in town, on Nightmare and Hell (whose area levels are past Act 1's)
 * and outside POE2 mode.
 */
int CurrentAreaMonsterDamagePercent();

/**
 * @brief A monster's POE2 level: the area level, +2 for uniques, Diablo and Na-Krul (however they were placed),
 * +5 for the Butcher. 0 when the monster keeps Diablo's level: outside POE2 mode, for the player's golems and
 * minions, and where the area level is unknown.
 */
int Poe2MonsterLevel(const Monster &monster, int difficulty);

} // namespace devilution
