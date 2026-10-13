/**
 * @file panels/skills_panel.hpp
 *
 * POE2 Mashup skills panel: every skill in the skill bar with its support sockets, like POE2's skill gem panel.
 */
#pragma once

#include "engine/point.hpp"
#include "engine/surface.hpp"

namespace devilution {

bool IsSkillsPanelOpen();
void OpenSkillsPanel();
void CloseSkillsPanel();
void ToggleSkillsPanel();

void DrawSkillsPanel(const Surface &out);
/** @brief Handles a left click inside the panel. */
void HandleSkillsPanelClick(Point mousePosition);

} // namespace devilution
