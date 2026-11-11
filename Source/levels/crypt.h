/**
 * @file levels/crypt.h
 *
 * Interface of the cathedral level generation algorithms.
 */
#pragma once

#include <optional>

#include "levels/gendung.h"
#include "utils/bitset2d.hpp"

namespace devilution {

extern int UberRow;
extern int UberCol;
extern bool IsUberRoomOpened;
extern bool IsUberLeverActivated;
extern int UberDiabloMonsterIndex;

extern const Miniset L5STAIRSUP;

void InitCryptPieces();
void SetCryptRoom();
void SetCornerRoom();
void FixCryptDirtTiles();
bool PlaceCryptStairs(lvl_entry entry);
/** @param keepOut POE2: blocks the random pillars and floor stars keep out of (the new floors' designed rooms). */
void CryptSubstitution(const Bitset2d<DMAXX, DMAXY> *keepOut = nullptr);
void SetCryptSetPieceRoom();
void PlaceCryptLights();

/** POE2: the new Crypt floors (poe2/cathedral_floors.hpp). */
struct CathedralPlan;
/** @brief The floor's quest room: Na-Krul's prison on level 24, the Cornerstone of the World's room on level 21. */
bool CryptNeedsQuestRoom();
/** @brief Puts the floor's quest room with its north-west block at position. */
void PlaceCryptQuestRoom(Point position);
/** @brief The stairs up (to town on level 21) and down (none on level 24) where the plan put them. */
bool PlaceCryptFunnelStairs(const CathedralPlan &plan, lvl_entry entry);
/** @brief The designed rooms' carved demons and succubi, blood down the walls and stars in the floor. */
void StampCryptMarks(const CathedralPlan &plan);

} // namespace devilution
