/**
 * @file panels/skill_bar.hpp
 *
 * POE2 Mashup skill bar: the socketed skill gems, each used with its own rebindable key.
 */
#pragma once

#include <cstddef>

#include "engine/point.hpp"
#include "engine/surface.hpp"

namespace devilution {

struct Player;

bool IsSkillBarShown();
bool IsSkillBarHovered();

void DrawSkillBar(const Surface &out);

/** @brief Fills the info box for the hovered skill bar slot. */
void SetSkillBarInfo();

/**
 * @brief Handles a left click on the skill bar: socket the held gem, or take a gem out while the inventory is open.
 * @return true if the click was on the skill bar.
 */
bool HandleSkillBarClick(Point mousePosition);

/** @brief Uses the skill in a skill bar slot towards the cursor. */
void UseSkillSlot(size_t slot);

/**
 * @brief Sockets a skill gem from the inventory or belt into the first free skill bar slot.
 * @return false if there was no free slot.
 */
bool SocketSkillGemFromInventory(Player &player, bool fromBelt, int index);

} // namespace devilution
