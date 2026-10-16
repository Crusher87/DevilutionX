/**
 * @file panels/skills_panel.hpp
 *
 * POE2 Mashup skills panel: the 12 socketed skill gems with their support sockets, like POE2's skill gem panel.
 */
#pragma once

#include <cstdint>

#include "engine/point.hpp"
#include "engine/rectangle.hpp"
#include "engine/render/text_render.hpp"
#include "engine/surface.hpp"
#include "poe2/player_data.hpp"
#include "poe2/support_gems.hpp"

namespace devilution {

bool IsSkillsPanelOpen();
void OpenSkillsPanel();
void CloseSkillsPanel();
void ToggleSkillsPanel();

void DrawSkillsPanel(const Surface &out);
/** @brief Whether the position is on the panel or its open skill details. */
bool SkillsPanelContains(Point position);
/** @brief Handles a left click inside the panel. */
void HandleSkillsPanelClick(Point mousePosition);

/** @brief Palette color of a gem's attribute: red Strength, yellow Dexterity, blue Magic. */
uint8_t GemPaletteColor(GemColor color);
UiFlags GemTextColor(GemColor color);
/** @brief Draws a support gem's item graphic centred in a socket. */
void DrawSupportGemIcon(const Surface &out, const Rectangle &socket, SupportGem gem);

} // namespace devilution
