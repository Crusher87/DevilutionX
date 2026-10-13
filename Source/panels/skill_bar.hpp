/**
 * @file panels/skill_bar.hpp
 *
 * POE2 Mashup skill tray: the Town Portal button and 9 slots (the three mouse buttons and six keys), each using an
 * action or a skill gem from the Skills panel. Clicking a slot opens a popup to pick its skill and fill its supports.
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
/** @brief Whether the mouse is over the tray or its popup. */
bool IsSkillBarHovered();

/** @brief Draws the tray, and the slot popup when it's open. */
void DrawSkillBar(const Surface &out);

/** @brief Fills the info box for the hovered tray slot. */
void SetSkillBarInfo();

/**
 * @brief Handles a left click on the tray or its popup; a click elsewhere closes the popup.
 * @return true if the click was used.
 */
bool HandleSkillBarClick(Point mousePosition);
/**
 * @brief Handles a right click: clears a tray slot, takes a support out in the popup, or closes the popup.
 * @return true if the click was used.
 */
bool HandleSkillBarRightClick(Point mousePosition);

bool IsSkillTrayPopupOpen();
void CloseSkillTrayPopup();

/**
 * @brief Clicks a Skills panel socket: sockets the gem on the cursor, or takes the skill gem out.
 * @param canTakeOut Whether an empty-handed click picks the skill gem up.
 */
void ClickSkillSlot(Player &player, size_t slot, bool canTakeOut);

/** @brief Draws a skill's small icon, greyed out if it can't be used. */
void DrawSkillIcon(const Surface &out, Point bottomLeft, SkillSlot skill, bool ready);
/** @brief The key or button of the tray slot using a Skills panel socket's gem; empty if it's not on the tray. */
std::string SkillSlotKeyName(size_t slot);
std::string TraySlotKeyName(TraySlot slot);
/** @brief The attributes a skill gem needs that the character lacks, e.g. "Needs 30 Strength". */
std::string MissingSkillRequirements(const Player &player, SkillSlot skill);

/** @brief Uses the skill gem in a Skills panel socket towards the cursor. */
void UseSkillSlot(size_t slot);
/** @brief Uses what a tray slot holds; mouse actions like move and attack are handled by the click itself. */
void UseTraySlot(TraySlot slot);
/** @brief Opens a town portal for free, like POE2's portal button. */
void UseFreeTownPortal();

/**
 * @brief Sockets a skill gem from the inventory or belt into the first free Skills panel socket, and puts it on the tray.
 * @return false if there was no free socket.
 */
bool SocketSkillGemFromInventory(Player &player, bool fromBelt, int index);

/** @brief Puts support gems in the inventory, or at the player's feet if it's full. */
void GiveSupportGems(Player &player, const std::vector<SupportGem> &gems);

/**
 * @brief Changes the skill in a socket; supports the new skill can't use come back out as items.
 * @return The skill that was in the socket.
 */
SkillSlot ChangeSlotSkill(Player &player, size_t slot, SkillSlot skill, int level = 1);

/** @brief What happened when a support gem was socketed, for the player. */
std::string SupportSocketMessage(SupportSocketResult result, const Player &player, size_t slot, SupportGem gem);

/**
 * @brief Sockets the support gem on the cursor into a Skills panel socket's skill.
 * @param socket The support socket to fill; the first free one if empty.
 */
bool SocketHeldSupportGem(Player &player, size_t slot, std::optional<size_t> socket);

} // namespace devilution
