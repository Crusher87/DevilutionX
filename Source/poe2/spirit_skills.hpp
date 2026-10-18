/**
 * @file poe2/spirit_skills.hpp
 *
 * POE2 Mashup: the Spirit skills added after the first ones (golem variants, Guardian, Shadow Warrior,
 * Revive, Mana Shield, Vizjerei Focus, Berserk and Reflect), and the element infusions that
 * Meteor, Thunderstorm, Frozen Orb and Apocalypse eat for a bigger cast.
 */
#pragma once

#include <cstdint>

#include "engine/point.hpp"
#include "poe2/player_data.hpp"
#include "utils/fixed_point.hpp"

namespace devilution {

struct Monster;
struct Player;
enum class DamageType : uint8_t;

/** @brief Blood Golem's blows heal its owner for this share of the damage. */
constexpr int BloodGolemHealPercent = 20;
constexpr int BloodGolemLifePercent = 150;
/** @brief Fire Golem's blows add this share as fire, and it burns what stands next to it. */
constexpr int FireGolemFirePercent = 30;
constexpr uint8_t GuardianCooldownTicks = 8 * 20;
constexpr int GuardianRange = 7;
/** @brief Shadow Warrior repeats a skill at this share of its damage. */
constexpr int ShadowWarriorPercent = 40;
constexpr uint8_t ReviveCooldownTicks = 2 * 20;
/** @brief Mana Shield: this share of the damage taken comes from mana while there is mana. */
constexpr int ManaShieldPercent = 30;
/** @brief Vizjerei Focus: more spell damage for every 100 maximum mana, and extra cost in percent of maximum mana. */
constexpr int VizjereiPercentPer100Mana = 4;
constexpr int VizjereiManaCostPercent = 6;
constexpr int BerserkDamagePercent = 20;
constexpr int BerserkDamageTakenPercent = 15;
constexpr int ReflectPercent = 30;
/** @brief A monster killed while burning, shocked or chilled leaves a mote of that element this often, in percent. */
constexpr int InfusionDropPercent = 25;
constexpr uint16_t InfusionMoteTicks = 15 * 20;
/** @brief An infused cast deals this much more damage, in percent. */
constexpr int InfusionDamagePercent = 60;

enum InfusionElement : uint8_t {
	Infusion_Fire = 1 << 0,
	Infusion_Lightning = 1 << 1,
	Infusion_Cold = 1 << 2,
};

/** @brief The golem skill that keeps the golem up: the first active of Summon Golem, Blood Golem and Fire Golem. */
PersistentSkill ActiveGolemSkill(const Player &player);
/** @brief The golem's life in percent of Summon Golem's. */
int GolemLifePercent(const Player &player);
/** @brief A minion of the player hit a monster for the damage. */
void OnMinionHitMonster(Player &owner, const Monster &minion, Monster &target, Fixed26_6 damage);

bool ShadowWarriorActive(const Player &player);
/** @brief A monster the local player hit died; Revive may raise it. */
void OnReviveCandidateKilled(const Player &player, const Monster &monster);
/** @brief How many minions Revive keeps up (0 when it isn't up); the King's dead share the larger of this and 3. */
int ReviveMinionLimit(const Player &player);

/** @brief Berserk's more damage taken and Mana Shield's share from mana. */
Fixed26_6 ApplySpiritHitTaken(Player &player, Fixed26_6 damage);
/** @brief Berserk's more attack damage, in percent. */
int SpiritAttackDamagePercent(const Player &player);
/** @brief Vizjerei Focus's more spell damage, in percent. */
int SpiritSpellDamagePercent(const Player &player);
/** @brief Vizjerei Focus's extra mana cost of a spell, as a raw Fixed26_6 value. */
int32_t SpiritSpellExtraManaRaw(const Player &player);
/** @brief The player hit a monster in melee. */
void OnSpiritMeleeHit(Player &player);
/** @brief A monster hit the player in melee for the damage; Reflect turns part of it back. */
void ReflectMeleeHit(Player &player, Monster &monster, Fixed26_6 damage);

/** @brief A monster the local player hit died; it may leave an infusion mote. */
void TryDropInfusion(const Monster &monster);
/**
 * @brief Meteor, Thunderstorm, Frozen Orb and Apocalypse eat an infusion, the skill's own element first.
 * @return the infusion eaten (an InfusionElement), or 0 if the skill doesn't take one or none is carried.
 */
uint8_t TakeInfusion(Player &player, SkillSlot skill);
DamageType InfusionDamageType(uint8_t infusion);

/** @brief Guardian, Revive, Fire Golem's burning and the infusion motes; once per game tick for the local player. */
void ProcessSpiritSkills(Player &player);
void ClearSpiritSkills();
size_t InfusionMoteCount();

} // namespace devilution
