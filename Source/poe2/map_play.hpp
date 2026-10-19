/**
 * @file poe2/map_play.hpp
 *
 * POE2 Mashup: what a map's waystone modifiers do while the hero is in it. Monster spawn stats are in
 * monster_mods.cpp; this covers hits on the hero, ailments, ground patches, death explosions and the hero's
 * penalties.
 */
#pragma once

#include <cstdint>

#include "engine/point.hpp"
#include "utils/fixed_point.hpp"

namespace devilution {

struct Monster;
struct Player;
enum class DamageType : uint8_t;

/** @brief Map poison deals this much of the hit per second for 2 seconds; bleeding this much for 4 seconds. */
constexpr int MapPoisonPercentPerSecond = 20;
constexpr int MapPoisonTicks = 2 * 20;
constexpr int MapBleedPercentPerSecond = 15;
constexpr int MapBleedTicks = 4 * 20;
/** @brief "Periodically": the penalty is on this many seconds out of every cycle. */
constexpr int MapPeriodicCycleTicks = 12 * 20;
constexpr int MapPeriodicActiveTicks = 4 * 20;
/** @brief Ground patches per point of the modifier, their radius, and what standing in one does. */
constexpr int MapPatchesPerPoint = 6;
constexpr int MapPatchRadius = 2;
constexpr int BurningGroundLifePerMillePerSecond = 30;
constexpr int ChilledGroundSlowPercent = 30;
constexpr int ShockedGroundDamageTakenPercent = 20;
/** @brief Magic and rare monsters explode for this much of their life per point of the modifier. */
constexpr int DeathExplosionLifePercent = 4;
constexpr int DeathExplosionMaxPlayerLifePercent = 25;

/**
 * @brief Adds the map's extra elemental damage and armour break to a monster's melee hit on the hero (after
 * the hero's armour; the extra damage meets the hero's resistances), and may poison or make the hero bleed.
 */
Fixed26_6 ApplyMapMonsterHit(Player &player, Fixed26_6 physical);
/** @brief A monster's missile or spell hit the hero in a map: may poison or make the hero bleed. */
void OnMapMonsterSpellHit(Player &player, Fixed26_6 damage);
/** @brief How much of the hero's resistances map monsters' hits ignore. */
[[nodiscard]] int MapPenetration();

/** @brief The hero's damage, flask charges, recovery, skill costs, auras and leech in the map, in percent. */
[[nodiscard]] int MapPlayerDamagePercent();
[[nodiscard]] int MapPlayerSlowPercent(const Player &player);
[[nodiscard]] int MapFlaskChargePercent();
[[nodiscard]] int MapRecoveryPercent();
[[nodiscard]] int MapSkillCostPercent();
[[nodiscard]] int MapAuraPercent();
[[nodiscard]] int MapLeechPercent();
/** @brief More damage the hero takes, standing on shocked ground. */
[[nodiscard]] int MapDamageTakenPercent(const Player &player);

/** @brief The map monsters' extra elemental resistance, and how much less exposure, shock and crits do to them. */
[[nodiscard]] int MapMonsterResistance();
[[nodiscard]] int MapExposurePercent();
[[nodiscard]] int MapCritBonusPercent();
[[nodiscard]] int MapThresholdPercent();
/** @brief Map monsters that act faster now and then take a second turn in a tick. */
[[nodiscard]] bool MapMonsterExtraTick(Monster &monster);
/** @brief Map monsters' extra life regeneration, in tenths of a percent per second. */
[[nodiscard]] int MapMonsterRegenTenths();
/** @brief More experience map monsters give, in percent. */
[[nodiscard]] int MapExperiencePercent();

/** @brief Ground patches, ailments and penalties on the hero; once per game tick. */
void ProcessMapPlayer(Player &player);
/** @brief Lays the map's ground patches; after a map level loads. */
void PlaceMapGroundPatches(uint32_t seed, Point entrance);
/** @brief A monster died in a map: magic and rare ones may explode. */
void OnMapMonsterDeath(const Monster &monster);
/** @brief Forgets the hero's map ailments and patches, leaving the map. */
void ClearMapPlay();

} // namespace devilution
