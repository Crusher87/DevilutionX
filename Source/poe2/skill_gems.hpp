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

/** @brief Strength, magic and dexterity a skill gem needs. */
struct GemAttributeNeeds {
	int strength = 0;
	int magic = 0;
	int dexterity = 0;
};
/**
 * @brief POE2's attribute requirement x 0.75 at a gem level for a gem that puts sharePercent of its requirement on one
 * attribute (100 for a single-attribute gem): 0 at level 1, 12 at level 3, 50 at level 10, 119 at level 20.
 */
int GemAttributeRequirement(int gemLevel, int sharePercent);
/** @brief What the skill's gem needs at a gem level. Like POE2, it grows with the level; the gem's attributes keep their share. */
GemAttributeNeeds SkillGemAttributeNeeds(SkillSlot skill, int gemLevel);
/** @brief The level of the skill's own gem in the Skills panel or a meta gem, without gear's +levels (POE2 doesn't raise needs for those); 1 if not socketed. */
int SocketedSkillGemLevel(const Player &player, SkillSlot skill);
bool MeetsGemAttributeNeeds(const Player &player, const GemAttributeNeeds &needs);

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

/** @brief Whether a slot can take the binding: gems and the weapon set attacks go anywhere; the old mouse actions nowhere. */
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
