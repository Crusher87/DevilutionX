/**
 * @file panels/stash_tabs_panel.hpp
 *
 * POE2 Mashup stash, drawn in code: a strip of tabs across the top (arrows scroll it, "..." lists every tab),
 * each tab's settings (right-click a tab): name, colour, kind and affinities, and the layouts of the premade
 * Currency, Gems, Maps and Uniques tabs. Ctrl+right-click moves an item between inventory and stash, like
 * Ctrl+left-click.
 */
#pragma once

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#else
#include <SDL.h>
#endif

#include "engine/point.hpp"
#include "engine/surface.hpp"
#include "qol/stash.h"

namespace devilution {

bool IsStashTabPopupOpen();
/** @brief Whether the open stash's tab list drawer, beside the stash, is at that point. */
bool StashDrawerContains(Point position);
void CloseStashTabPopups();
/** @brief Draws the POE2 stash's frame and tab strip (no gold box), and a blank tab's header. */
void DrawPoe2StashFrame(const Surface &out);
/** @brief Draws the empty cells of a blank tab's grid. */
void DrawPoe2StashGridBack(const Surface &out);
/** @brief Draws a premade tab's layout and any open tab popup. */
void DrawStashTabUi(const Surface &out);
/** @brief The name of the empty premade slot under the mouse, drawn on top of everything with the item info box. */
void DrawStashTabTooltip(const Surface &out);
/** @brief The stash item under the mouse on a premade tab, or EmptyCell. */
StashStruct::StashCell PremadeTabItemUnderCursor(Point mousePosition);
/**
 * @brief A left click in the POE2 stash: the tab strip, the tab's buttons, and a premade tab's slots.
 * @return false when it's for the blank tab's grid.
 */
bool HandlePoe2StashClick(Point mousePosition, bool isShiftHeld, bool isCtrlHeld);
/** @brief Handles stash tab clicks and keys while the POE2 stash is open; true when the event was used. */
bool StashTabsHandleEvent(const SDL_Event &event);

} // namespace devilution
