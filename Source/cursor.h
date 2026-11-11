/**
 * @file cursor.h
 *
 * Interface of cursor tracking functionality.
 */
#pragma once

#include "cursor_defs.hpp"
#include "engine/clx_sprite.hpp"
#include "engine/point.hpp"
#include "engine/size.hpp"
#include "engine/surface.hpp"
#include "utils/attributes.h"

namespace devilution {

extern int pcursmonst;
extern DVL_API_FOR_TEST int8_t pcursinvitem;
extern uint16_t pcursstashitem;
extern int8_t pcursitem;

struct Object; // Defined in objects.h
extern Object *ObjectUnderCursor;

struct Player; // Defined in player.h
extern const Player *PlayerUnderCursor;
extern Point cursPosition;
extern DVL_API_FOR_TEST int pcurs;

void InitCursor();
void FreeCursor();
void ResetCursor();

struct Item;
/**
 * @brief Use the item sprite as the cursor (or show the default hand cursor if the item isEmpty)
 */
void NewCursor(const Item &item);

void NewCursor(int cursId);

void InitLevelCursor();
void CheckRportal();
void CheckTown();
void CheckCursMove();
/**
 * @brief Whether UI covers that screen point, so the world under it can't be hovered: the main panel and open side
 * panels, and in POE2 mode also the skill tray and its popup, the stash drawer, the talk menu, the quest reward
 * popup, party portraits, and the panels that take every click while open (passive tree, gemcutting, anvil, atlas,
 * stack split, stash tab popups and the like).
 */
bool IsCursorOverUi(Point position);

void DrawSoftwareCursor(const Surface &out, Point position, int cursId);

void DrawItem(const Item &item, const Surface &out, Point position, ClxSprite clx);

/** Returns the sprite for the given inventory index. */
ClxSprite GetInvItemSprite(int cursId);

ClxSprite GetHalfSizeItemSprite(int cursId);
ClxSprite GetHalfSizeItemSpriteRed(int cursId);
void CreateHalfSizeItemSprites();
void FreeHalfSizeItemSprites();

/** Returns the width and height for an inventory index. */
Size GetInvItemSize(int cursId);

} // namespace devilution
