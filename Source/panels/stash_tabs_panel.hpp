/**
 * @file panels/stash_tabs_panel.hpp
 *
 * POE2 Mashup stash tab controls: the tab's name and colour at the top of the stash, a list of every tab
 * (left-click the name) and each tab's settings (right-click the name or a tab in the list): name, colour and
 * affinities. Ctrl+right-click moves an item between inventory and stash, like Ctrl+left-click.
 */
#pragma once

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#else
#include <SDL.h>
#endif

#include "engine/surface.hpp"

namespace devilution {

bool IsStashTabPopupOpen();
void CloseStashTabPopups();
/** @brief Draws the tab name over the stash and any open tab popup. */
void DrawStashTabUi(const Surface &out);
/** @brief Handles stash tab clicks and keys while the POE2 stash is open; true when the event was used. */
bool StashTabsHandleEvent(const SDL_Event &event);

} // namespace devilution
