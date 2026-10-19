/**
 * @file poe2/spirit_uses.hpp
 *
 * POE2 Mashup: POE2's Spirit gems you also use from the skill tray. Iron Ward stores the physical damage armour stops
 * and Plague Bearer part of the hero's poison damage, each released as a nova; Convalescence starts energy shield
 * recharge at once; Grim Feast stores life from fallen skeletons and spends it raising them; and the three banners
 * build Glory from attack hits and plant a banner whose area empowers the hero and the minions in it.
 */
#pragma once

#include <cstdint>
#include <string>

#include "engine/point.hpp"
#include "poe2/player_data.hpp"
#include "utils/fixed_point.hpp"

namespace devilution {

struct Monster;
struct Player;

constexpr int IronWardMinRadius = 3;
constexpr int IronWardMaxRadius = 5;
/** @brief Iron Ward's nova builds stun this much more, in percent. */
constexpr int IronWardStunPercent = 300;
constexpr int PlagueBearerStorePercent = 4;
constexpr int PlagueBearerRadius = 2;
/** @brief Plague Bearer's nova poisons for this share of its damage over PlagueBearerPoisonTicks, like POE2's poison. */
constexpr int PlagueBearerPoisonPercent = 40;
constexpr int PlagueBearerPoisonTicks = 2 * 20;
constexpr int ConvalescenceManaCost = 20;
constexpr int ConvalescenceCooldownTicks = 30 * 20;
/** @brief Grim Feast: a skeleton that dies within this many tiles leaves a remnant of this share of its life. */
constexpr int GrimFeastTiles = 3;
constexpr int GrimFeastRemnantPercent = 35;
constexpr int GrimFeastRemnantTicks = 8 * 20;
constexpr int GrimFeastCooldownTicks = 20;
constexpr int MaxGrimRemnants = 12;
constexpr int GloryForBanner = 200;
constexpr int MaxGlory = 200;
constexpr int BannerTiles = 5;
constexpr int WarBannerDamagePercent = 25;
constexpr int WarBannerAccuracyPercent = 50;
constexpr int WarBannerAttackSpeedPercent = 25;
constexpr int DefianceBannerDefencePercent = 30;
constexpr int DefianceBannerMovementPercent = 30;
constexpr int DreadBannerMaxResistance = 4;
/** @brief Dread Banner's flask charges, in thousandths per second. */
constexpr int DreadBannerChargesMilliPerSecond = 500;

/** @brief Spirit gem values that grow with the gem's level, from level 1 to level 20. */
int IronWardStorePercent(int gemLevel);
/** @brief The most Iron Ward and Plague Bearer hold, in percent of the hero's maximum life. */
int SpiritStoreCapPercent(int gemLevel);
int ConvalescenceTicks(int gemLevel);
/** @brief The most Grim Feast holds, in percent of the strongest skeleton's life. */
int GrimFeastCapPercent(int gemLevel);
int BannerTicks(int gemLevel);

/** @brief The Spirit gems used from the skill tray here. */
[[nodiscard]] bool IsSpiritUseGem(PersistentSkill skill);

enum class SpiritUseResult : uint8_t {
	Used,
	Off,
	Town,
	/** Nothing stored yet. */
	Empty,
	NotEnoughGlory,
	Cooldown,
	NoMana,
	ShieldFull,
	NothingToRaise,
};

/** @brief Uses the gem from the skill tray: releases its store, starts its buff or plants its banner. */
SpiritUseResult UseSpiritGem(Player &player, PersistentSkill skill);
/** @brief Whether using the gem would do anything now, to grey out its tray icon. */
[[nodiscard]] bool SpiritGemReady(const Player &player, PersistentSkill skill);
/** @brief What the gem holds and the most it can: stored damage or life in whole points, or Glory. */
struct SpiritStore {
	int value;
	int cap;
};
[[nodiscard]] SpiritStore SpiritGemStore(const Player &player, PersistentSkill skill);

/** @brief Armour stopped this much of a physical hit on the local hero: Iron Ward stores it. */
void OnSpiritArmourStopped(const Player &player, Fixed26_6 stopped);
/** @brief The local hero's poison dealt this much: Plague Bearer stores part of it. */
void OnSpiritPoisonDealt(const Player &player, Fixed26_6 damage);
/** @brief One of the hero's minions died: Grim Feast. */
void OnSpiritMinionDied(const Player &owner, const Monster &minion);
/** @brief An attack of the local hero hit the monster: the banners build Glory. */
void OnSpiritAttackHit(const Player &player, const Monster &monster);
/** @brief Convalescence keeps hits from resetting the energy shield recharge. */
[[nodiscard]] bool RechargeCantBeInterrupted(const Player &player);

/** @brief War Banner: more attack damage, accuracy and attack speed for the hero standing in it, in percent. */
[[nodiscard]] int BannerAttackDamagePercent(const Player &player);
[[nodiscard]] int BannerAccuracyPercent(const Player &player);
[[nodiscard]] int BannerAttackSpeedPercent(const Player &player);
/** @brief War Banner: the more damage a minion of the hero standing in it deals, in percent. */
[[nodiscard]] int BannerMinionDamagePercent(const Player &owner, const Monster &minion);
/** @brief Defiance Banner: more armour and evasion, and walk frames skipped for its movement speed. */
[[nodiscard]] int BannerDefencePercent(const Player &player);
int BannerWalkSkippedFrames(const Player &player, int walkFrames);
/** @brief Dread Banner: the higher maximum resistances. */
[[nodiscard]] int BannerMaxResistance(const Player &player);

/** @brief The store, Glory or buff time shown on the gem's portrait beside the minions; empty for none. */
std::string SpiritUsePortraitText(const Player &player, PersistentSkill skill);

/** @brief Once per game tick for the local hero. */
void ProcessSpiritUses(Player &player);
void ClearSpiritUses();
size_t GrimRemnantCount();

} // namespace devilution
