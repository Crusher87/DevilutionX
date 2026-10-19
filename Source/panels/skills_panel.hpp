/**
 * @file panels/skills_panel.hpp
 *
 * POE2 Mashup skills panel: the 12 socketed skill gems with their support sockets, like POE2's skill gem panel.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "engine/point.hpp"
#include "engine/rectangle.hpp"
#include "engine/render/text_render.hpp"
#include "engine/surface.hpp"
#include "poe2/player_data.hpp"
#include "poe2/support_gems.hpp"

namespace devilution {

struct Player;

bool IsSkillsPanelOpen();
void OpenSkillsPanel();
void CloseSkillsPanel();
void ToggleSkillsPanel();

void DrawSkillsPanel(const Surface &out);
/** @brief Whether the position is on the panel or its open skill details. */
bool SkillsPanelContains(Point position);
/** @brief Handles a left click inside the panel. */
void HandleSkillsPanelClick(Point mousePosition);
/** @brief Right-click on a Spirit gem switches it on or off. @return true if the click was used. */
bool HandleSkillsPanelRightClick(Point mousePosition);
/** @brief Uses the Jeweller's Orb on the cursor on the skill under the mouse; true if it opened a socket. */
bool UseJewellerOnSkillsPanel(Player &player, Point mousePosition);
/** @brief The tooltip line on a locked support socket: which Jeweller's Orb opens it. */
std::string JewellerSocketHint(size_t socket);

/** @brief Palette color of a gem's attribute: red Strength, yellow Dexterity, blue Magic. */
uint8_t GemPaletteColor(GemColor color);
UiFlags GemTextColor(GemColor color);
/** @brief Draws a support gem's item graphic centred in a socket. */
void DrawSupportGemIcon(const Surface &out, const Rectangle &socket, SupportGem gem);

} // namespace devilution
