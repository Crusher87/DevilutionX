/**
 * @file panels/desecration_panel.hpp
 *
 * POE2 Mashup desecration reveal: pick which of three modifiers a desecrated item's unrevealed one becomes.
 */
#pragma once

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#else
#include <SDL.h>
#endif

#include "engine/surface.hpp"
#include "items.h"

namespace devilution {

bool IsDesecrationRevealOpen();
/** @brief Opens the choice for the item, which is worn, in the inventory or in the stash. */
void OpenDesecrationReveal(const Item &item);
void CloseDesecrationReveal();

/** @return true if the event was handled by the panel. */
bool DesecrationRevealHandleEvent(const SDL_Event &event);
void DrawDesecrationReveal(const Surface &out);

} // namespace devilution
