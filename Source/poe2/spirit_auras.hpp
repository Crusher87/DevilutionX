/**
 * @file poe2/spirit_auras.hpp
 *
 * POE2 Mashup: more of POE2's buff Spirit gems. Briarpatch turns attacks into thorny ground when you cast; Magma
 * Barrier and Scavenged Plating are for heroes who block and break armour; Time of Need heals now and then; Charge
 * Regulation spends Frenzy charges on speed; Withering Presence withers the monsters near the hero; Trinity rewards
 * mixing the elements; Siphon Elements makes infusions from ailments. Herald of Blood lives with the other heralds in
 * poe2/spirit.cpp.
 */
#pragma once

#include <cstdint>
#include <string>

#include "poe2/player_data.hpp"
#include "utils/fixed_point.hpp"

namespace devilution {

struct Monster;
struct Player;
enum class DamageType : uint8_t;

/** @brief Briarpatch stores this share of the physical damage of attack hits, in percent. */
constexpr int BriarpatchStorePercent = 10;
constexpr int BriarpatchCooldownTicks = 8 * 20;
constexpr int BriarpatchTicks = 6 * 20;
constexpr int BriarpatchRadius = 2;
/** @brief The thorny ground hurts and hinders what's in it this often; it deals what was stored over its whole time. */
constexpr int BriarpatchPulseTicks = 10;
constexpr int BriarpatchHinderTicks = 2 * 20;
constexpr int MagmaBarrierBlockPercent = 25;
constexpr int MagmaBarrierTiles = 3;
/** @brief Magma Barrier's spray adds this share of the shield's armour as fire, in percent. */
constexpr int MagmaBarrierArmourPercent = 30;
constexpr int ScavengedPlatingMaxStacks = 10;
constexpr int ScavengedPlatingTicks = 8 * 20;
constexpr int ScavengedPlatingArmourPerStack = 4;
/** @brief Charge Regulation: a Frenzy charge buys this much speed, and it takes the next when this little is left. */
constexpr int ChargeRegulationTicksPerCharge = 8 * 20;
constexpr int ChargeRegulationRefillTicks = 2 * 20;
constexpr int WitheringPresenceMaxStacks = 5;
constexpr int WitheringPresenceTicks = 6 * 20;
constexpr int TrinityMaxResonance = 100;
/** @brief Trinity: an elemental hit takes this much Resonance from each of the other two elements. */
constexpr int TrinityResonanceLost = 2;
/** @brief Trinity: after this long without an elemental hit, Resonance fades by a point every other tick (10 a second). */
constexpr int TrinityIdleTicks = 8 * 20;
constexpr int TrinityResonancePerStep = 30;
constexpr int HeraldOfBloodPercent = 15;
constexpr int HeraldOfBloodRadius = 2;

/** @brief Values that grow with the gem's level, from level 1 to level 20. */
int BriarpatchCapPercent(int gemLevel);
int MagmaBarrierChargeTicks(int gemLevel);
int MagmaBarrierDamage(int gemLevel);
int ScavengedPlatingThorns(int gemLevel);
int TimeOfNeedTicks(int gemLevel);
int TimeOfNeedHealPercent(int gemLevel);
int ChargeRegulationSpeedPercent(int gemLevel);
int WitheringPresencePulseTicks(int gemLevel);
int TrinityResonanceGained(int gemLevel);
/** @brief Trinity's more elemental damage for every 30 Resonance of all three elements together, in percent. */
int TrinityPercentPerStep(int gemLevel);
int SiphonElementsChance(int gemLevel);
int SiphonElementsPickupTiles(int gemLevel);
int HeraldOfBloodBleedChance(int gemLevel);

/** @brief An attack hit of the hero dealt this much physical damage: Briarpatch. */
void OnSpiritPhysicalAttack(const Player &player, int32_t physicalRaw);
/** @brief The hero cast a spell: Briarpatch. */
void OnSpiritSpellCast(const Player &player);
/** @brief The hero blocked a hit: Magma Barrier. */
void OnSpiritBlocked(Player &player);
/** @brief The hero broke the monster's armour, where it wasn't broken: Scavenged Plating. */
void OnSpiritArmourBroken(const Player &player, const Monster &monster);
/** @brief A monster hit the hero in melee: Scavenged Plating's thorns. */
void OnAuraMeleeHitTaken(Player &player, Monster &monster);
/** @brief A hit of the hero dealt damage of this type: Trinity. */
void OnSpiritElementalHit(const Player &player, DamageType type);
/** @brief The hero froze, shocked (lightning) or ignited (fire) a monster; cold is DamageType::Magic: Siphon Elements. */
void OnSpiritElementalAilment(const Player &player, const Monster &monster, DamageType type);

/** @brief Scavenged Plating's more armour, in percent. */
int ScavengedArmourPercent(const Player &player);
/** @brief Charge Regulation's more attack and cast speed, in percent. */
int SpiritAuraSpeedPercent(const Player &player);
/** @brief Trinity's more damage of the element (fire, lightning or cold), in percent. */
int TrinityDamagePercent(const Player &player, DamageType type);
/** @brief How far the hero picks up infusions from: Siphon Elements reaches further. */
int InfusionPickupTiles(const Player &player);

/** @brief The gem's stacks, charge or Resonance on its portrait beside the minions; empty for none. */
std::string SpiritAuraPortraitText(const Player &player, PersistentSkill skill);

/** @brief Once per game tick for the local hero. */
void ProcessSpiritAuras(Player &player);
void ClearSpiritAuras();

} // namespace devilution
