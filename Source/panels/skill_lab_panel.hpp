/**
 * @file panels/skill_lab_panel.hpp
 *
 * POE2 Mashup Skill Lab panel: the skill list and the controls down the left of the screen (see poe2/skill_lab.hpp).
 */
#pragma once

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#else
#include <SDL.h>
#endif

#include "engine/surface.hpp"

namespace devilution {

/** @return true if the event was for the panel. */
bool SkillLabPanelHandleEvent(const SDL_Event &event);
void DrawSkillLabPanel(const Surface &out);

} // namespace devilution
