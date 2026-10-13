/**
 * @file panels/skill_bar.hpp
 *
 * POE2 Mashup skill bar: the socketed skill gems, each used with its own rebindable key.
 */
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "engine/point.hpp"
#include "engine/surface.hpp"
#include "poe2/player_data.hpp"
#include "poe2/support_gems.hpp"

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

/**
 * @brief Clicks a skill bar slot: sockets the gem on the cursor, or takes the skill gem out.
 * @param canTakeOut Whether an empty-handed click picks the skill gem up.
 */
void ClickSkillSlot(Player &player, size_t slot, bool canTakeOut);

/** @brief Draws a skill's small icon, greyed out if it can't be used. */
void DrawSkillIcon(const Surface &out, Point bottomLeft, SkillSlot skill, bool ready);
std::string SkillSlotKeyName(size_t slot);
/** @brief The attributes a skill gem needs that the character lacks, e.g. "Needs 30 Strength". */
std::string MissingSkillRequirements(const Player &player, SkillSlot skill);

/** @brief Uses the skill in a skill bar slot towards the cursor. */
void UseSkillSlot(size_t slot);

/**
 * @brief Sockets a skill gem from the inventory or belt into the first free skill bar slot.
 * @return false if there was no free slot.
 */
bool SocketSkillGemFromInventory(Player &player, bool fromBelt, int index);

/** @brief Puts support gems in the inventory, or at the player's feet if it's full. */
void GiveSupportGems(Player &player, const std::vector<SupportGem> &gems);

/**
 * @brief Changes the skill in a slot; supports the new skill can't use come back out as items.
 * @return The skill that was in the slot.
 */
SkillSlot ChangeSlotSkill(Player &player, size_t slot, SkillSlot skill);

/** @brief What happened when a support gem was socketed, for the player. */
std::string SupportSocketMessage(SupportSocketResult result, const Player &player, size_t slot, SupportGem gem);

/**
 * @brief Sockets the support gem on the cursor into a skill bar slot's skill.
 * @param socket The socket to fill; the first free one if empty.
 */
bool SocketHeldSupportGem(Player &player, size_t slot, std::optional<size_t> socket);

} // namespace devilution
