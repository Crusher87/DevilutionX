/**
 * @file poe2/monster_levels.hpp
 *
 * POE2 Mashup monster life and damage by level: a white monster's life, hit and accuracy at each monster level
 * (txtdata/poe2/monster_levels.tsv, written by tools/poe2_monster_tables.py), every type's Diablo-scale references
 * (monster_types.tsv), and the uniques' and bosses' multipliers (monster_bosses.tsv). Each monster keeps its relative
 * toughness: its life roll and damage are ratios to the average of the types on its floors, clamped to 30-250%.
 */
#pragma once

#include <cstdint>

#include "tables/monstdat.h"

namespace devilution {

struct Monster;

/** @brief A type's ratios are clamped to this band, in percent. */
constexpr int MinMonsterTypePercent = 30;
constexpr int MaxMonsterTypePercent = 250;

/** @brief A monster type's Diablo-scale references (in tenths) and ratios (in percent); refs of 0 mean "no row". */
struct MonsterTypeLevelRow {
	int lifeRef10 = 0;
	int damageRef10 = 0;
	int toHitRef10 = 0;
	int lifePercent = 100;
	int damagePercent = 100;
	int xpPercent = 100;
};

/** @brief A unique's or boss's life and damage in percent of a white at the floor's area level. */
struct MonsterBossLevelRow {
	int lifePercent = 100;
	int damagePercent = 100;
};

/** @brief Loads the three tables in POE2 mode; clears them otherwise (never fails). */
void LoadMonsterLevels();
/** @brief Whether the level table is loaded (POE2 mode with the mod's data). */
bool HasMonsterLevelTable();

/** @brief A white monster's life at a monster level (levels past the table repeat its ends); 0 without the table. */
int MonsterLevelLife(int level);
/** @brief A white monster's average hit at a monster level, in tenths. */
int MonsterLevelDamage10(int level);
/** @brief A white monster's accuracy rating at a monster level. */
int MonsterLevelAccuracy(int level);

/** @brief The type's row; a type missing from the table gets refs of 0, so every ratio is 100%. */
const MonsterTypeLevelRow &MonsterTypeRow(_monster_id type);

/**
 * @brief The boss row of a unique (by name, else the row for uniques not listed) or of Diablo and Na-Krul placed as
 * types; nullptr for every other monster.
 */
const MonsterBossLevelRow *MonsterBossRow(const Monster &monster);

/** @brief clamp(roll / lifeRef, 30%, 250%) in per mille; 1000 when the type has no ref. */
int MonsterLifeRatioPermille(_monster_id type, int roll);

/**
 * @brief Sets a monster's life from the table when it is created in POE2 mode on a floor with an area level: a white
 * at the area level times clamp(roll / lifeRef), a unique or boss times its multiplier. Marks the monster
 * MFLAG_POE2_TABLES. Returns false (and changes nothing) when Diablo's numbers stay: outside POE2 mode, without the
 * tables, or where the floor has no area level.
 * @param roll The monster's life roll from its data (ignored for uniques and bosses).
 */
bool ApplyPoe2MonsterLife(Monster &monster, int roll);

/**
 * @brief Percent a monster's rolled hit is scaled by in POE2 mode: the table's hit at its level times its type's (or
 * unique's) damage ratio over its own data average, times the area's monster damage (CurrentAreaMonsterDamagePercent:
 * 70% in Act 1), then Hard Hitting, the waystone's monster damage, the map boss
 * and encounter leaders. Its damage bytes stay its data. 100 outside POE2 mode and for the player's minions; a monster
 * whose own data average is 0 (Lachdanan) or that isn't on the tables keeps 100 before the modifiers.
 */
int Poe2MonsterDamagePercent(const Monster &monster);

/** @brief A rolled hit (any unit) times Poe2MonsterDamagePercent, in 64 bits and clamped. */
int ScaleMonsterHit(const Monster &monster, int rolled);

/**
 * @brief In POE2 mode, puts a monster loaded from a save that isn't on the tables yet (MFLAG_POE2_TABLES unset) onto
 * them: its life is rescaled from its Diablo life to the table's, keeping its roll, its other multipliers and its
 * current life fraction; its damage bytes go back to its data and its armour loses the Nightmare/Hell bonus.
 * The player's minions and berserk monsters are left alone. Doing it twice changes nothing.
 */
void RescaleLoadedMonster(Monster &monster);

/**
 * @brief A player's minion's level on the tables (plan v2 step 11): the minion level of its owner's skill gem
 * (MinionLevelForGemLevel: a skeleton gem's, Revive's or the active golem skill's); the King's dead, raised by no gem,
 * are the area's level. 0 outside POE2 mode or without an owner.
 */
int Poe2MinionLevel(const Monster &minion);
/**
 * @brief A minion's average hit in tenths before its owner's minion damage: the table's hit at @p level times
 * clamp(its own average / its type's damage ref, 30-250%), the monsters' rule with the minion's own damage as the roll.
 * @param damageSum Its damage bytes, min + max.
 */
int MinionHit10(_monster_id type, int level, int damageSum);
/**
 * @brief The per mille a player's minion's rolled hit is scaled by: MinionHit10 over its own average at @p level (on
 * the tables; 1000 off them), then @p ownerMinionDamagePercent, its owner's minion damage.
 */
int64_t MinionDamageScalePermille(_monster_id type, int level, int damageSum, int ownerMinionDamagePercent);
/** @brief A minion's life in 1/64ths: a white's at its level, times @p percent (100 for now; step 13 refits). */
int32_t MinionLifeRaw(int level, int percent);

/** @brief The life range a type shows in the monster info panel at an area level, POE2 tables. */
void Poe2TypeLifeRange(_monster_id type, int areaLevel, int &minLife, int &maxLife);

} // namespace devilution
