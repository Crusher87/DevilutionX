/**
 * @file panels/gemcutting_panel.hpp
 *
 * POE2 Mashup gemcutting panel: browse every gem by weapon type and tier, and cut an uncut gem into one.
 */
#pragma once

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#else
#include <SDL.h>
#endif

#include "engine/surface.hpp"
#include "poe2/gemcutting.hpp"

namespace devilution {

struct Player;

bool IsGemcuttingOpen();
/** @brief Opens the panel to cut the uncut gem at the given inventory or belt index. */
void OpenGemcutting(bool fromBelt, int index);
/** @brief Opens the panel just to look through the gems. */
void OpenGemcuttingBrowser();
void CloseGemcutting();

/** @return true if the event was handled by the panel. */
bool GemcuttingHandleEvent(const SDL_Event &event);
void DrawGemcutting(const Surface &out);
/** @brief The tab an uncut skill gem opens the panel on, for the hero's weapons and class. */
GemCategory DefaultSkillCategory(const Player &player);

} // namespace devilution
