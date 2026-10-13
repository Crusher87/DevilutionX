/**
 * @file poe2/weapon_skills.hpp
 *
 * POE2 Mashup weapon skills: each weapon type grants a skill, used with the weapon skill key.
 */
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "engine/displacement.hpp"
#include "engine/point.hpp"
#include "poe2/player_data.hpp"

namespace devilution {

struct Item;
struct Player;
enum class player_graphic : uint8_t;

/** @brief Where the arrows of Rain of Arrows come down, around the target. */
constexpr std::array<Displacement, 9> RainOfArrowsVolley = { { { 0, 0 }, { 2, 0 }, { -2, 0 }, { 0, 2 }, { 0, -2 }, { 2, 2 }, { -2, -2 }, { 2, -2 }, { -2, 2 } } };

/** @brief The skill the given weapon grants, or WeaponSkill::None. */
WeaponSkill WeaponSkillForItem(const Item &item);
/** @brief The skill the player's equipped weapons grant. */
WeaponSkill WeaponSkillFor(const Player &player);
std::string_view WeaponSkillName(WeaponSkill skill);
std::string_view WeaponSkillDescription(WeaponSkill skill);
int WeaponSkillManaCost(WeaponSkill skill);
/** @brief The mana cost with the skill's supports. */
int WeaponSkillManaCost(const Player &player, WeaponSkill skill);
/** @brief Whether the skill is fired with a ranged attack. */
bool IsRangedWeaponSkill(WeaponSkill skill);

/** @brief Whether the skill needs a certain weapon (or a shield); Battle Cry and the fire and lightning skills don't. */
bool WeaponSkillNeedsWeapon(WeaponSkill skill);

/** @brief Whether the weapon can be used for the skill, e.g. a mace for Ground Slam. */
bool ItemAllowsWeaponSkill(const Item &item, WeaponSkill skill);
/** @brief Whether either equipped weapon (or shield) can be used for the skill, or it needs none. */
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

/** @brief Bonus lightning damage of a Lightning Arrow, carried by the arrow missile. */
int LightningArrowDamage(const Player &player);

/**
 * @brief Plays a skill's own animation (as PM_SPELL); the skill goes off on its action frame.
 * @param graphic Attack for weapon skills, else one of the spell casting animations.
 */
void BeginSkillAnimation(Player &player, WeaponSkill skill, player_graphic graphic, Point target);

/**
 * @brief Makes a cast skill go off, on the action frame of its animation.
 * @return true if the skill ended the animation early.
 */
bool ExecuteSkillCast(Player &player, WeaponSkill skill, Point target);

/** @brief Plays the visual effects and sounds of a skill hitting or going off. */
void PlaySkillEffects(const Player &player, WeaponSkill skill, Point target);

} // namespace devilution
