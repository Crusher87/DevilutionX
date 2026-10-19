/**
 * @file poe2/skill_gems.hpp
 *
 * POE2 Mashup skill gems: socketed in the Skills panel, each gem grants a spell or a weapon attack,
 * and the skill tray says which mouse button or key uses it.
 */
#pragma once

#include <optional>
#include <string_view>

#include "poe2/player_data.hpp"

namespace devilution {

struct Item;
struct ItemData;
struct Player;

/** @brief The skill a gem grants; empty if the item isn't a skill gem. */
SkillSlot SkillForGemData(const ItemData &data);
SkillSlot SkillForGem(const Item &item);
bool IsSkillGem(const Item &item);

/** @brief The item table index of the gem that grants the skill. */
std::optional<size_t> SkillGemItemIndex(SkillSlot skill);
bool CreateSkillGemItem(Item &item, SkillSlot skill, int level = 1);

/** @brief A skill gem item's level, 1 to 20. */
int SkillGemItemLevel(const Item &item);
void SetSkillGemItemLevel(Item &item, int level);

std::string_view SkillName(SkillSlot skill);
int SkillManaCost(const Player &player, SkillSlot skill);

enum class SkillUsability : uint8_t {
	Ready,
	/** The character doesn't have the gem's strength, magic or dexterity. */
	Requirements,
	/** An attack skill without a suitable weapon. */
	Weapon,
	Mana,
	/** A spell that can't be cast in town. */
	Town,
	Empty,
	/** A Spirit gem without enough Spirit left to reserve. */
	Spirit,
};

SkillUsability GetSkillUsability(const Player &player, SkillSlot skill);

/** @brief The first Skills panel socket without a gem, if any. */
std::optional<size_t> FirstFreeSkillSlot(const Player &player);
/** @brief The slot holding the skill, if it's socketed. */
std::optional<size_t> FindSkillSlot(const Player &player, SkillSlot skill);

/**
 * @brief Puts a skill gem into a Skills panel socket.
 *
 * Tray slots that used the socket go back to their default when it's emptied or gets a Spirit gem.
 * @return The skill that was in the socket before (empty if none).
 */
SkillSlot SocketSkill(Player &player, size_t slot, SkillSlot skill, int level = 1);

/** @brief Only the left and right mouse buttons can move, interact or cast the readied spell. */
bool TraySlotTakesActions(TraySlot slot);
/** @brief Whether a slot can take the binding: gems and the weapon attack go anywhere, the other actions only on those two buttons. */
bool TrayBindingFits(TrayBinding binding, TraySlot slot);
/** @brief The first tray slot that uses the gem in a Skills panel socket, if any. */
std::optional<TraySlot> TraySlotForSocket(const Player &player, size_t socket);
/** @brief The skill a tray slot uses; empty unless it's bound to a socketed gem. */
SkillSlot TraySkill(const Player &player, TraySlot slot);

/**
 * @brief Sets what a tray slot does.
 *
 * Picking a gem that's on another slot swaps the two slots, like POE2.
 */
void BindTraySlot(Player &player, TraySlot slot, TrayBinding binding);
/** @brief Puts a tray slot back to its default: move and attack on the left button, the readied spell on the right, else nothing. */
void ClearTraySlot(Player &player, TraySlot slot);

/**
 * @brief Puts a socketed skill on the first free key, then the middle mouse button, unless it's on the tray already.
 *
 * Spirit gems are active while socketed and need no tray slot, except invocations.
 * @return The slot the skill is on, if any.
 */
std::optional<TraySlot> AutoBindSocket(Player &player, size_t socket);
/** @brief Whether the skill is used from the skill tray: every skill but the Spirit gems, which are active while socketed, except invocations. */
bool SkillGoesOnTray(SkillSlot skill);

/** @brief Fills the tray of heroes from before it existed: each old bar slot (E, R, T, F, G, V) moves to the key with the same default letter, and V to Q. */
void MigrateOldSkillBar(Player &player);

/** @brief Gives a new hero the gem for their class's starting weapon. */
void GrantStarterSkill(Player &player);

} // namespace devilution
