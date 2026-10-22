/**
 * @file trigs.h
 *
 * Interface of functionality for triggering events when the player enters an area.
 */
#pragma once

#include "engine/point.hpp"
#include "interfac.h"
#include "levels/gendung.h"

namespace devilution {

#define MAXTRIGGERS 7

struct TriggerStruct {
	WorldTilePosition position;
	interface_mode _tmsg;
	int _tlvl;
};

extern bool trigflag;
extern int numtrigs;
extern TriggerStruct trigs[MAXTRIGGERS];
extern int TWarpFrom;

void InitNoTriggers();
bool IsWarpOpen(dungeon_type type);
void InitTownTriggers();
void InitL1Triggers();
void InitL2Triggers();
void InitL3Triggers();
void InitL4Triggers();
void InitHiveTriggers();
void InitCryptTriggers();
void InitSKingTriggers();
void InitSChambTriggers();
void InitPWaterTriggers();
void InitVPTriggers();
void Freeupstairs();
void CheckTrigForce();
void CheckTriggers();

/**
 * @brief POE2 Mashup: stairs and quest entrances take the local hero through only when they were clicked (or chosen
 * with the controller's secondary action), like the town portal. Walking onto one with the movement keys, or rolling or
 * dashing onto it, does nothing (Curtis 2026-10-22). Remembers the level exit the cursor points at.
 */
void ChooseLevelExitAt(Point tile);
void ForgetChosenLevelExit();
/** @brief Whether the local hero chose the level exit at this tile. */
bool ChoseLevelExit(Point tile);

/**
 * @brief Check if the provided position is in the entrance boundary of the entrance.
 * @param entrance The entrance to check.
 * @param position The position to check against the entrance boundary.
 */
bool EntranceBoundaryContains(Point entrance, Point position);

} // namespace devilution
