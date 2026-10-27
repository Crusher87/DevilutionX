/**
 * @file poe2/skill_resources.hpp
 *
 * POE2 Mashup build resources and the effects the first wave of new skills share: Rage, Combo and Frenzy
 * charges on the hero, Amplify Damage and armour break on monsters, and bursts that hit everything around a spot.
 */
#pragma once

#include <cstdint>

#include "engine/point.hpp"
#include "tables/misdat.h"
#include "utils/fixed_point.hpp"

namespace devilution {

struct Monster;
struct Player;

constexpr int MaxRage = 30;
/** @brief Ticks Rage holds after a hit before it starts to drain. */
constexpr uint8_t RageHoldTicks = 2 * 20;
constexpr int MaxCombo = 10;
/** @brief Ticks Combo lasts without a hit. */
constexpr uint8_t ComboTicks = 3 * 20;
constexpr int MaxFrenzyCharges = 3;
constexpr uint16_t FrenzyChargeTicks = 10 * 20;
/** @brief Each Frenzy charge: more attack damage in percent. */
constexpr int FrenzyChargeDamagePercent = 5;
/** @brief Fists of Thunder bursts with lightning every this many hits in a row. */
constexpr int FistsOfThunderBurstEvery = 3;

/** @brief Amplify Damage: more physical damage taken, at gem level 1 and per level after; lasts 8 seconds. */
constexpr int AmplifyDamagePercent = 30;
constexpr int AmplifyDamagePercentPerLevel = 1;
constexpr uint8_t AmplifyDamageTicks = 8 * 20;
/** @brief Smite's armour break: more physical damage taken for 5 seconds. */
constexpr int ArmourBreakDamagePercent = 25;
constexpr uint8_t ArmourBreakTicks = 5 * 20;
/** @brief Riposte is ready for 3 seconds after a block. */
constexpr uint8_t RiposteTicks = 3 * 20;

/** @brief More attack damage from Rage and Frenzy charges, in percent (100 is none). */
int ResourceAttackDamagePercent(const Player &player);
void GainFrenzyCharge(Player &player);
/** @brief Adds a point of Combo; returns the new Combo. */
int AddCombo(Player &player);
/** @brief Whether the hero's next Fists of Thunder hit is the one that bursts with lightning, its finisher. */
bool FistsOfThunderNextHitBursts(const Player &player);
void AddRage(Player &player, int rage);
/** @brief A melee skill hit a monster: Battle Rage builds Rage. */
void OnSkillMeleeHit(Player &player);

/** @brief Curses the monsters around a spot with Amplify Damage. */
void CastAmplifyDamage(const Player &player, Point target, int gemLevel);
/** @brief Blasphemy: Amplify Damage at part strength on one monster for a few ticks, unless a stronger one lasts longer. */
void ApplyAuraAmplifyDamage(const Player &player, Monster &monster, int gemLevel, int strengthPercent, int ticks);
void BreakMonsterArmour(Monster &monster, const Player *breaker = nullptr);
/** @brief Knocks a monster out of the fight: a heavy stun. */
void HeavyStunMonster(Monster &monster);

/** @brief Hits every monster within the radius of a spot for the damage, on behalf of the player. */
void SkillBurst(const Player &player, Point center, int radius, DamageType type, Fixed26_6 damage, MissileGraphicID effect);

} // namespace devilution
