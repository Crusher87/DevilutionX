/**
 * @file panels/atlas_panel.hpp
 *
 * POE2 Mashup Atlas passives: spend the points completed maps give on map rewards, encounters and keystones.
 */
#pragma once

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#else
#include <SDL.h>
#endif

#include "engine/surface.hpp"

namespace devilution {

bool IsAtlasOpen();
void ToggleAtlas();
void CloseAtlas();

/** @return true if the event was handled by the panel. */
bool AtlasHandleEvent(const SDL_Event &event);
void DrawAtlas(const Surface &out);

} // namespace devilution
