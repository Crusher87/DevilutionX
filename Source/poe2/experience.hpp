/**
 * @file poe2/experience.hpp
 *
 * POE2 Mashup levels 1-100 experience: base experience by monster level, the level-gap rule, boss multipliers,
 * the map death penalty and the check that moves a hero's experience onto this table.
 */
#pragma once

#include <cstdint>

namespace devilution {

struct Monster;
struct Player;

/** @brief The level a monster's base experience stops growing; monsters at or above it pay full whatever the gap. */
constexpr int ExperienceTopMonsterLevel = 80;
/** @brief A map death costs this share of the current level's experience (never below the level's start). */
constexpr int MapDeathExperiencePercent = 10;
/** @brief The Sparkling shrine gives this many white kills of the area's base experience. */
constexpr int SparklingShrineKills = 10;

/** @brief Loads the experience tables in POE2 mode, clears them otherwise. */
void LoadPoe2ExperienceData();

/** @brief Experience of a white monster of this level (levels past the table repeat its last row). */
uint32_t MonsterBaseExperience(int monsterLevel);

/** @brief Percent for the monster's type (monster_types.tsv xpPercent); 100 for uniques, bosses and without it. */
int MonsterTypeExperiencePercent(const Monster &monster);

/** @brief Unique and boss multiplier in percent from experience_bosses.tsv; 100 for other monsters. */
int MonsterBossExperiencePercent(const Monster &monster);

/**
 * @brief A kill's experience in POE2 mode: base experience at the monster's level x type x rarity and map
 * (MonsterExperiencePercent, applied once) x boss, summed in 64 bits and clamped to the table's top.
 */
uint32_t Poe2MonsterExperience(const Monster &monster);

/** @brief Permille of a kill's experience a hero of characterLevel gets from a monster of monsterLevel. */
int ExperienceLevelGapPermille(int characterLevel, int monsterLevel);

/**
 * @brief Experience a hero gains from an amount: the level-gap rule and, in multiplayer, the cap at a twentieth of the
 * level's threshold (no 200 x level cap in POE2).
 */
uint32_t Poe2ExperienceGain(int characterLevel, uint32_t experience, int monsterLevel, bool multiplayer);

/** @brief The Sparkling shrine's experience at this area level. */
uint32_t Poe2ShrineExperience(int areaLevel);

/** @brief The level of the current floor's monsters, as their level() reports it (currlevel when none are found). */
int CurrentFloorMonsterLevel();

/** @brief Experience after a map death: a tenth of the level's need lost, never below the level's start. */
uint32_t ExperienceAfterMapDeath(int characterLevel, uint32_t experience);

/** @brief The old 50-level Diablo threshold to leave a level (0 for level 0, the cap past 50). */
uint32_t DiabloExperienceThreshold(int level);

/**
 * @brief Keeps a hero's experience inside the band of their level on this table. Outside it, experience inside the old
 * Diablo band of that level moves over keeping its fraction (always under the next level); anything else is clamped
 * into the band. Does nothing outside POE2 mode or when it is already inside. Returns whether it changed anything.
 */
bool CorrectPoe2Experience(Player &player);

} // namespace devilution
