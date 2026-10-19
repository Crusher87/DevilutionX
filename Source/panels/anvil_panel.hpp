/**
 * @file panels/anvil_panel.hpp
 *
 * POE2 Mashup town anvil, like POE2's salvage bench: pick an item from the inventory and break it down for
 * quality currency and Artificer's Shards (Curtis 2026-10-16).
 */
#pragma once

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#else
#include <SDL.h>
#endif

#include "engine/surface.hpp"

namespace devilution {

bool IsAnvilOpen();
void OpenAnvil();
void CloseAnvil();

/** @return true if the event was handled by the panel. */
bool AnvilHandleEvent(const SDL_Event &event);
void DrawAnvil(const Surface &out);

} // namespace devilution
