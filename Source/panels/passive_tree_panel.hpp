/**
 * @file panels/passive_tree_panel.hpp
 *
 * POE2 Mashup passive tree panel.
 */
#pragma once

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#else
#include <SDL.h>
#endif

#include "engine/surface.hpp"

namespace devilution {

bool IsPassiveTreeOpen();
void OpenPassiveTree();
void ClosePassiveTree();
void TogglePassiveTree();

/** @return true if the event was handled by the passive tree. */
bool PassiveTreeHandleEvent(const SDL_Event &event);
void DrawPassiveTree(const Surface &out);

} // namespace devilution
