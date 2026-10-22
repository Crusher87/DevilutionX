/**
 * @file quests.h
 *
 * Interface of functionality for handling quests.
 */
#pragma once

#include "monster.h"
#include "tables/questdat.hpp"

namespace devilution {

void InitQuests();

/**
 * @brief Deactivates quests from each quest pool at random to provide variety for single player games
 * @param seed The seed used to control which quests are deactivated
 * @param quests The available quest list, this function will make some of them inactive by the time it returns
 */
void InitialiseQuestPools(uint32_t seed, Quest quests[]);
void CheckQuests();
bool ForceQuests();
void CheckQuestKill(const Monster &monster, bool sendmsg);
int GetMapReturnLevel();
Point GetMapReturnPosition();
void LoadPWaterPalette();
void UpdatePWaterPalette();
/** @brief POE2 Mashup: true on the Poisoned Water Supply's level, foul or cleansed. It holds water, never lava. */
bool IsPoisonedWaterLevel();
/** @brief POE2 Mashup: true while that level's water is still foul (shown with l3pfoul.pal). */
bool IsPoisonedWaterFoul();
void ResyncMPQuests();
void ResyncQuests();
void SetMultiQuest(int q, quest_state s, bool log, int v1, int v2, int16_t qmsg);

} // namespace devilution
