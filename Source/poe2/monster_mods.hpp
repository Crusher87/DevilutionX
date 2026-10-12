/**
 * @file poe2/monster_mods.hpp
 *
 * POE2 Mashup monster rarity and ailments: magic and rare monsters with modifiers, ignite and shock.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "utils/fixed_point.hpp"

namespace devilution {

struct Monster;
struct Player;
enum class DamageType : uint8_t;

enum class MonsterRarity : uint8_t {
	Normal,
	Magic,
	Rare,
};

enum class MonsterMod : uint8_t {
	ExtraLife,
	HardHitting,
	Regenerating,
	Resistant,
	Count,
};

/** @brief Out of 1000 monsters, how many roll magic and rare. */
constexpr int MagicMonsterPerMille = 120;
constexpr int RareMonsterPerMille = 30;
/** @brief Magic monsters get one modifier, rares three. */
constexpr int RareMonsterModCount = 3;
constexpr int ExtraLifePercent = 80;
constexpr int HardHittingPercent = 40;
/** @brief Extra life every magic and rare monster gets on top of its modifiers. */
constexpr int MagicMonsterLifePercent = 30;
constexpr int RareMonsterLifePercent = 60;
/** @brief Regenerating monsters heal this much of their life per second, in tenths of a percent. */
constexpr int RegenPerMillePerSecond = 15;

/** @brief POE2: ignite deals 20% of the hit per second for 4 seconds. */
constexpr int IgniteTicks = 4 * 20;
constexpr int IgnitePercentPerSecond = 20;
/** @brief POE2: shocked monsters take 20% more damage for 4 seconds. */
constexpr int ShockTicks = 4 * 20;
constexpr int ShockDamagePercent = 20;

/** @brief Rarity and modifiers of a monster; plain for uniques, minions and outside POE2 mode. */
struct MonsterAffixes {
	MonsterRarity rarity = MonsterRarity::Normal;
	uint8_t mods = 0;

	[[nodiscard]] bool has(MonsterMod mod) const { return (mods & (1U << static_cast<uint8_t>(mod))) != 0; }
};

/** @brief Derived from the monster's item seed, so it is the same after loading a game and on every machine. */
MonsterAffixes GetMonsterAffixes(const Monster &monster);
/** @brief Applies life, damage and resistance modifiers once, when the monster is created. */
void ApplyMonsterRarity(Monster &monster);

std::string_view MonsterModName(MonsterMod mod);
/** @brief Name shown for a rare monster, like POE2's two-word rare names. */
std::string RareMonsterName(const Monster &monster);

/** @brief Experience in percent of normal: magic and rare monsters give more. */
int MonsterExperiencePercent(const Monster &monster);
/** @brief Extra item drops: one for magic, two for rare. */
int MonsterExtraDrops(const Monster &monster);

/** @brief Heals regenerating monsters and burns ignited ones; once per game tick. */
void ProcessMonsterAilments(Monster &monster);

/** @brief Damage after the shock bonus. */
Fixed26_6 ApplyShock(const Monster &monster, Fixed26_6 damage);
/** @brief Fire hits may ignite and lightning hits may shock, more likely the bigger the hit. */
void TryInflictAilment(Monster &monster, const Player &player, DamageType damageType, Fixed26_6 damage);
/** @brief Chance in percent that a hit inflicts an ailment: twice the share of the monster's life it took. */
int AilmentChancePercent(const Monster &monster, Fixed26_6 damage);

} // namespace devilution
