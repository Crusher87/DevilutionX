/**
 * @file poe2/weapon_skills.hpp
 *
 * POE2 Mashup weapon skills: each weapon type grants a skill, used with the weapon skill key.
 */
#pragma once

#include <string_view>

#include "poe2/player_data.hpp"

namespace devilution {

struct Item;
struct Player;

/** @brief The skill the given weapon grants, or WeaponSkill::None. */
WeaponSkill WeaponSkillForItem(const Item &item);
/** @brief The skill the player's equipped weapons grant. */
WeaponSkill WeaponSkillFor(const Player &player);
std::string_view WeaponSkillName(WeaponSkill skill);
std::string_view WeaponSkillDescription(WeaponSkill skill);
int WeaponSkillManaCost(WeaponSkill skill);
/** @brief Whether the skill is fired with a ranged attack. */
bool IsRangedWeaponSkill(WeaponSkill skill);

/** @brief Whether the weapon can be used for the skill, e.g. a mace for Ground Slam. */
bool ItemAllowsWeaponSkill(const Item &item, WeaponSkill skill);
/** @brief Whether either equipped weapon can be used for the skill. */
bool WeaponAllowsSkill(const Player &player, WeaponSkill skill);
/** @brief The weapons a skill needs, e.g. "sword or axe". */
std::string_view WeaponSkillWeapons(WeaponSkill skill);

/**
 * @brief Queues a weapon skill and attacks towards the cursor.
 * @return true if the skill was queued.
 */
bool TryUseWeaponSkill(Player &player, WeaponSkill skill);

/**
 * @brief Called when an attack starts: turns a queued skill into the active one and pays its mana.
 */
void ActivateQueuedWeaponSkill(Player &player, bool isRangedAttack);

/** @brief Extra animation frames skipped by the active skill's attack. */
int8_t WeaponSkillSkippedFrames(const Player &player);

} // namespace devilution
