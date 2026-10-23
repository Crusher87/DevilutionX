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
/** @brief How long Flammability lasts after the last fire hit, like POE2's 8 seconds on monsters. */
constexpr int FlammabilityTicks = 8 * 20;
constexpr int IgnitePercentPerSecond = 20;
/** @brief POE2: shocked monsters take 20% more damage for 4 seconds. */
constexpr int ShockTicks = 4 * 20;
constexpr int ShockDamagePercent = 20;

/** @brief POE2: cold hits always chill, slowing by up to 50% for 2 seconds. */
constexpr int ChillTicks = 2 * 20;
constexpr int MinChillPercent = 10;
constexpr int MaxChillPercent = 50;
/** @brief Cold damage adding up to half the monster's life freezes it for 2 seconds. */
constexpr int FreezeTicks = 2 * 20;
constexpr int FreezeThresholdPercent = 50;
/** @brief Physical damage adding up to the monster's life heavy stuns it for 2 seconds. */
constexpr int HeavyStunTicks = 2 * 20;
constexpr int HeavyStunThresholdPercent = 100;
/** @brief Freeze and stun buildup fades by this share of the threshold per second. */
constexpr int BuildupDecayPercentPerSecond = 20;
/** @brief Rares and uniques build up freeze and stun slower, in percent of the damage. */
constexpr int RareBuildupPercent = 60;
constexpr int UniqueBuildupPercent = 40;

/** @brief Rarity and modifiers of a monster; plain for uniques, minions and outside POE2 mode. */
struct MonsterAffixes {
	MonsterRarity rarity = MonsterRarity::Normal;
	uint8_t mods = 0;

	[[nodiscard]] bool has(MonsterMod mod) const { return (mods & (1U << static_cast<uint8_t>(mod))) != 0; }
};

/** @brief Derived from the monster's item seed, so it is the same after loading a game and on every machine. */
MonsterAffixes GetMonsterAffixes(const Monster &monster);
/** @brief Applies life, armour and resistance modifiers once, when the monster is created (damage is per hit). */
void ApplyMonsterRarity(Monster &monster);
/** @brief Percent each hit of the monster is scaled by for Hard Hitting and the waystone's monster damage. */
int MonsterModsDamagePercent(const Monster &monster);
/**
 * @brief Makes a plain monster magic or rare after it was created, to lead a pack: picks a nearby item seed that rolls
 * that rarity and adds the rarity's life (its damage is a percent per hit). Does nothing to a monster that can't
 * roll one or already has one.
 */
void PromoteMonsterRarity(Monster &monster, MonsterRarity rarity);

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
/** @brief Whether the monster loses this tick to a freeze, a heavy stun or a chill; advances the chill's slow. */
bool MonsterSkipsTick(Monster &monster);
/** @brief Whether the monster is frozen or heavy stunned. */
bool IsMonsterImmobilized(const Monster &monster);
/** @brief How much of a cold or physical hit counts towards a freeze or heavy stun, in percent. */
int MonsterBuildupPercent(const Monster &monster);

/** @brief Where an ailment comes from, for the rules that stop some of them (the Sickness Road, design v8 §5). */
enum class AilmentSource : uint8_t {
	/** Melee and bow hits, attack skills and minions. */
	Strike,
	/** Spells, Heralds and curses. */
	Spell,
	/** An ailment passed on from one monster to another. */
	Spread,
};

/**
 * @brief Inflicts ailments for a hit: fire may ignite and lightning may shock, more likely the bigger the hit;
 * magic (cold) hits chill and build up freeze; physical hits build up heavy stun.
 */
void TryInflictAilment(Monster &monster, const Player &player, DamageType damageType, Fixed26_6 damage, AilmentSource source = AilmentSource::Strike);
/** @brief Whether the monster is ignited, shocked, chilled or poisoned. */
bool CarriesAilment(const Monster &monster);
/**
 * @brief Passes a monster's ignite, shock, chill and poison to another, each replacing only a weaker or shorter one
 * (the Sickness Road's contagion). Never onto a player's minion.
 * @return Whether anything passed.
 */
bool CopyAilments(const Monster &from, Monster &to, const Player &player, bool withIgnite = true);
/**
 * @brief Chance in per mille that a hit inflicts an ailment, like POE2: 1% per 5% of the monster's ailment threshold
 * dealt as fire (its Flammability), 1% per 4% as lightning. Unique monsters' threshold is half their life.
 */
int AilmentChancePerMille(const Monster &monster, DamageType damageType, Fixed26_6 damage);

} // namespace devilution
