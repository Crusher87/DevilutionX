/**
 * @file poe2/skill_gems.hpp
 *
 * POE2 Mashup skill gems: socketed in the skill bar, each gem grants a spell or a weapon attack.
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
bool CreateSkillGemItem(Item &item, SkillSlot skill);

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

/** @brief The first skill bar slot without a gem, if any. */
std::optional<size_t> FirstFreeSkillSlot(const Player &player);
/** @brief The slot holding the skill, if it's socketed. */
std::optional<size_t> FindSkillSlot(const Player &player, SkillSlot skill);

/**
 * @brief Puts a skill gem into a skill bar slot.
 * @return The skill that was in the slot before (empty if none).
 */
SkillSlot SocketSkill(Player &player, size_t slot, SkillSlot skill);

/** @brief Gives a new hero the gem for their class's starting weapon. */
void GrantStarterSkill(Player &player);

} // namespace devilution
