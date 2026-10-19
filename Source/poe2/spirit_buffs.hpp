/**
 * @file poe2/spirit_buffs.hpp
 *
 * POE2 Mashup: POE2's buff and aura Spirit gems. Wind Dancer, Ghost Dance and Arctic Armour build stages; Barkskin
 * turns energy shield lost into armour; Eternal Rage, Combat Frenzy, Alchemist's Boon and Mana Remnants feed the
 * hero's resources; Overwhelming Presence and Attrition work on the monsters near the hero; Elemental Conflux favours
 * one element at a time; Trail of Caltrops and Blink change the dodge roll. Herald of Plague lives with the other
 * heralds in poe2/spirit.cpp.
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
enum class DamageType : uint8_t;

/** @brief POE2's Presence: the gems' auras reach monsters this many tiles from the hero. */
constexpr int PresenceTiles = 3;

constexpr int WindDancerMaxStages = 3;
constexpr int WindDancerEvasionPerStage = 10;
/** @brief Gale Force's cooldown, and its extra damage per stage spent, in percent. */
constexpr int GaleForceCooldownTicks = 5;
constexpr int GaleForceDamagePerStage = 50;
constexpr int GhostShroudMax = 3;
/** @brief After a hit takes a Ghost Shroud, energy shield regenerates by this share of evasion per second, in percent. */
constexpr int GhostDanceRegenPercent = 2;
constexpr int GhostDanceRegenTicks = 4 * 20;
constexpr int BarkskinTicks = 6 * 20;
constexpr int BarkskinMaxStacks = 8;
constexpr int ArcticArmourDamagePercent = 50;
/** @brief Arctic Armour's blast chills and builds freeze this much more, in percent. */
constexpr int ArcticArmourAilmentPercent = 200;
constexpr int ConfluxTicks = 8 * 20;
constexpr int CaltropTicks = 8 * 20;
constexpr int MaxCaltrops = 20;
constexpr int CaltropHinderTicks = 4 * 20;
constexpr int HeraldOfPlagueRadius = 2;
constexpr int HeraldOfPlagueHinderTicks = 4 * 20;
constexpr int ManaRemnantDropPercent = 25;
constexpr int ManaRemnantTicks = 8 * 20;
constexpr int ManaRemnantCritCooldownTicks = 2 * 20;
constexpr int MaxManaRemnants = 10;
/** @brief Attrition: 1% more hit damage per this many game ticks a rare or unique monster spent near the hero. */
constexpr int AttritionTicksPerPercent = 2 * 20;
/** @brief Attrition's Culling Strike: rares die at this share of their life, uniques at the second, in percent. */
constexpr int AttritionCullRarePercent = 10;
constexpr int AttritionCullUniquePercent = 5;

/** @brief Spirit gem values that grow with the gem's level, from level 1 to level 20. */
int WindDancerStageTicks(int gemLevel);
int GaleForceDamagePercent(int gemLevel);
int GhostShroudTicks(int gemLevel);
int BarkskinArmourPercent(int gemLevel);
int ArcticArmourStageTicks(int gemLevel);
int ArcticArmourMaxStages(int gemLevel);
int CombatFrenzyCooldownTicks(int gemLevel);
/** @brief Eternal Rage: Rage regenerated per second, in hundredths. */
int EternalRageCentiPerSecond(int gemLevel);
int ConfluxDamagePercent(int gemLevel);
/** @brief Alchemist's Boon: flask charges gained per second, in thousandths. */
int AlchemistChargesMilliPerSecond(int gemLevel);
int AlchemistMinionHealPercent(int gemLevel);
int PresenceStunThresholdPercent(int gemLevel);
int PresenceAilmentThresholdPercent(int gemLevel);
int CaltropDamagePercent(int gemLevel);
int HeraldOfPlagueHinderChance(int gemLevel);
int ManaRemnantMana(int gemLevel);
int AttritionMaxPercent(int gemLevel);
int AttritionCullTicks(int gemLevel);
int BlinkTiles(int gemLevel);
int BlinkCooldownTicks(int gemLevel);

/** @brief Wind Dancer's more evasion, in percent. */
int SpiritBuffEvasionPercent(const Player &player);
/** @brief Barkskin's armour, which can't go above the armour of the gear. */
int SpiritBuffArmour(const Player &player);
/** @brief A hit landed on the local hero; the shield took this much of it. */
void OnSpiritHitTaken(Player &player, Fixed26_6 shieldLost);
/** @brief A monster hit the local hero in melee: Arctic Armour blasts it. */
void OnSpiritMeleeHitTaken(Player &player, Monster &monster);
/** @brief The hero froze or electrocuted a monster: Combat Frenzy. */
void OnSpiritFreezeOrElectrocute(Player &player);
/** @brief The hero's attack hit critically: Mana Remnants. */
void OnSpiritCrit(Player &player, const Monster &monster);
/** @brief A monster the local hero hit died: Mana Remnants. */
void OnSpiritBuffKill(const Monster &monster);
/** @brief Elemental Conflux and Attrition: the more damage the monster takes from the local hero, in percent. */
int SpiritBuffDamagePercent(const Monster &monster, DamageType type);
/** @brief Elemental Conflux's element (a DamageType) while it's up, else -1. */
int ConfluxElement(const Player &player);
/** @brief Overwhelming Presence: the monster's stun or ailment threshold, in percent of the usual. */
int PresenceThresholdPercent(const Monster &monster, bool stun);
/** @brief Attrition's Culling Strike: whether the hit kills the rare or unique monster now. */
bool AttritionCulls(const Player &player, const Monster &monster);
/** @brief Alchemist's Boon: the share of a life flask's recovery that also heals the minions nearby, in percent. */
int FlaskMinionHealPercent(const Player &player);
/** @brief Blink replaces the dodge roll while its gem is on. */
[[nodiscard]] bool BlinkReplacesDodge(const Player &player);
/** @brief A dodge roll covered these tiles from the start: Trail of Caltrops. */
void OnSpiritDodge(Player &player, Point start, Point end);
/** @brief The buff's stages, armour or element, shown on its portrait beside the minions; empty for none. */
std::string SpiritBuffPortraitText(const Player &player, PersistentSkill skill);

/** @brief Once per game tick for the local hero. */
void ProcessSpiritBuffs(Player &player);
void ClearSpiritBuffs();
size_t CaltropCount();
size_t ManaRemnantCount();

} // namespace devilution
