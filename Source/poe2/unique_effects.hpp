/**
 * @file poe2/unique_effects.hpp
 *
 * POE2 Mashup: what the rules of worn uniques (poe2/unique_rules.hpp) do in play. Combat, spells, defences
 * and recovery call these; each returns the plain result when no worn unique has the rule.
 */
#pragma once

#include <cstdint>

#include "engine/point.hpp"
#include "engine/world_tile.hpp"
#include "tables/spelldat.h"
#include "utils/fixed_point.hpp"
#include "tables/itemdat.h"

namespace devilution {

struct Monster;
struct Missile;
struct Player;
enum class DamageType : uint8_t;

/** @brief POE2: Low Life is 35% of maximum life or less. */
constexpr int LowLifePercent = 35;
/** @brief POE2: attacks critically hit 5% of the time for 100% more damage. */
constexpr int BaseAttackCritPercent = 5;
constexpr int BaseCritBonusPercent = 100;
/** @brief Bleeding deals 15% of the hit's physical damage per second for 5 seconds. */
constexpr int BleedTicks = 5 * 20;
constexpr int BleedPercentPerSecond = 15;
/** @brief Bleeding monsters are hindered: 30% slower. */
constexpr int HinderPercent = 30;
/** @brief Exposure: 20% more damage of its element for 4 seconds. */
constexpr int ExposureTicks = 4 * 20;
constexpr int ExposurePercent = 20;
constexpr int ElectrocuteTicks = 20;
constexpr int GuardStackTicks = 4 * 20;
/** @brief Game armour per 100 POE2 armour, as the bases scale it, for rules that count POE2 armour. */
constexpr int GameArmourPer100 = 13;
/** @brief Tiles a "metre" range covers: within 2 metres is next to the hero, within 5 metres is within 3 tiles. */
constexpr int CloseRangeTiles = 1;
constexpr int FarRangeTiles = 3;

[[nodiscard]] bool IsOnLowLife(const Player &player);
/** @brief Advances the unique rules' counters and timers by one game tick, and casts queued spell repeats. */
void ProcessUniqueRules(Player &player);

/** @brief Whether an attack critically hits; outside POE2 mode only the Warrior's class crits. */
[[nodiscard]] bool RollAttackCrit(const Player &player);
/** @brief The damage of a critical hit in percent of a normal one. */
[[nodiscard]] int CritDamagePercent(const Player &player);
[[nodiscard]] bool AttacksCannotBeEvaded(const Player &player);
/** @brief Attack speed that changes while playing (missing life, after a crit), in percent. */
[[nodiscard]] int UniqueAttackSpeedPercent(const Player &player);
/**
 * @brief Deals an attack hit's damage, raw and before shock, to the monster: converts and scales it by the rules,
 * inflicts ailments and counts the attack.
 * @return The damage dealt.
 */
Fixed26_6 ApplyPlayerAttackDamage(const Player &player, Monster &monster, Fixed26_6 damage, bool crit, bool ranged = false);
/** @brief Cull: kills a normal or magic monster the hero's hit left on low enough life. */
void ApplyCull(const Player &player, Monster &monster);
/** @brief Enemies the hero's arrows pierce (Two Fallen, One Arrow). */
[[nodiscard]] int ArrowPierceCount(const Player &player);
/** @brief Percent of the character damage bonus a non-Rogue's arrows add, from the tree (0 for none). */
[[nodiscard]] int ArrowDamageModPercent(const Player &player);
/** @brief The hero blocked a hit (Lachdanan's Guard). */
void OnPlayerBlocked(const Player &player);
/** @brief The hero swapped weapon sets; the weapon type held before (Knife in the Dark). */
void OnWeaponsSwapped(const Player &player, ItemType before);
/** @brief The hero drank a flask (Gharbad's Bargain). */
void OnFlaskDrunk(Player &player);
/** @brief Na-Krul's Jailer: minions' increased damage from the hero's spell damage, in percent. */
[[nodiscard]] int MinionSpellDamageBonus(const Player &player);
/** @brief Ceaseless Hands: more evasion from unarmed hit stacks, in percent. */
[[nodiscard]] int UnarmedEvasionPercent(const Player &player);

/** @brief The rules' multiplier on a spell's damage when it is cast, in percent. */
[[nodiscard]] int UniqueSpellDamagePercent(const Player &player, SpellID spell);
/** @brief The rules' multiplier on a spell hitting the monster, in percent; counts the hit. */
int UniqueSpellHitPercent(const Player &player, const Monster &monster, DamageType type);
/** @brief Counts a cast spell and queues repeats. */
void OnSpellCast(const Player &player, SpellID spell, WorldTilePosition target, int spellLevel);
[[nodiscard]] bool IsCastingRepeat(const Player &player);
/** @brief Deep Well: whether this skill is free, using up the free skill. */
/** @brief Casts a spell for free from a point, as a repeat at the given damage percent: it sets off no repeats of its own. */
void CastFreeSpell(Player &player, SpellID spell, Point source, Point target, int percent);
bool TakeFreeSkill(const Player &player);
/** @brief The rules' multiplier on mana costs, in percent. */
[[nodiscard]] int UniqueManaCostPercent(const Player &player);
/** @brief Mana a spell may also take from energy shield. */
[[nodiscard]] Fixed26_6 SpendableEnergyShield(const Player &player);
/** @brief Pays what it can of a spell's cost with energy shield. @return The cost left for mana. */
Fixed26_6 PaySpellCostWithEnergyShield(Player &player, Fixed26_6 cost);
/** @brief Counts mana spent for rules that grow with resources spent. */
void NoteResourceSpent(const Player &player, Fixed26_6 amount);
/** @brief Casts a cast slowed by the rules waits this much longer, in percent of the cast. */
[[nodiscard]] int UniqueCastSlowPercent(const Player &player);
/** @brief Movement slowed by the rules, in percent. */
[[nodiscard]] int UniqueMovementSlowPercent(const Player &player);
/** @brief Whether a slowed walk skips this game tick. */
bool UniqueSlowSkipsWalkTick(Player &player);

void OnMonsterKilled(Player &player, const Monster &monster);
void OnMinionDied(Player &owner);
/** @brief The owner's minions' damage, in percent. */
/** @param minionLowLife Whether the minion is below half life. */
[[nodiscard]] int UniqueMinionDamagePercent(const Player &owner, bool minionLowLife = false);
/** @brief The owner's minions' life, in percent. */
[[nodiscard]] int UniqueMinionLifePercent(const Player &owner);

/** @brief The rules' multiplier on the chance to ignite, in percent; 0 when the player can't ignite. */
[[nodiscard]] int UniqueIgniteChancePercent(const Player &player);
/** @brief The rules' multiplier on stun buildup, in percent. */
[[nodiscard]] int UniqueStunBuildupPercent(const Player &player);
/** @brief Called when the player shocks, ignites or chills a monster. */
void OnAilmentInflicted(const Player &player, Monster &monster, DamageType type, bool wasShocked);
void OnMonsterFrozen(const Player &player);
/** @brief Bleeding, exposure, electrocution and spreading ignites, each game tick. */
void ProcessUniqueMonsterAilments(Monster &monster);
/** @brief More damage the monster takes from exposure and electrocution, in percent. */
[[nodiscard]] int MonsterDamageTakenPercent(const Monster &monster, DamageType type);
[[nodiscard]] bool IsMonsterElectrocuted(const Monster &monster);
/** @brief How much a hindered (bleeding) monster is slowed, in percent. */
[[nodiscard]] int MonsterHinderPercent(const Monster &monster);

/** @brief The rules' multiplier on armour and evasion, in percent. */
[[nodiscard]] int UniqueDefencePercent(const Player &player);
/** @brief Evasion that also avoids spells. */
[[nodiscard]] int SpellEvasion(const Player &player);
/** @brief A hit's damage after the rules, before energy shield; also counts life lost recently. */
Fixed26_6 ApplyUniqueHitTaken(Player &player, Fixed26_6 damage, DamageType type);
/** @brief The share of a hit's life the hero must lose to be stunned, in percent of the usual. */
[[nodiscard]] int UniqueStunThresholdPercent(const Player &player);

/** @brief Life recovery after the rules, in percent. */
[[nodiscard]] int LifeRecoveryPercent(const Player &player, bool fromFlask);
/** @brief Percent of mana recovery left after uniques and desecrated modifiers lessen it. */
[[nodiscard]] int ManaRecoveryPercent(const Player &player);
/** @brief Life recovered also recovers some energy shield. */
void OnLifeRecovered(Player &player, int32_t raw);
[[nodiscard]] bool IsRecoveryLocked(const Player &player);
/** @brief Energy shield recharge rate, in percent; 0 when it can't recharge. */
[[nodiscard]] int EnergyShieldRechargePercent(const Player &player);
/** @brief Life regenerated per second by a defensive modifier taken from a rare monster. */
[[nodiscard]] int StolenRegenPerMille(const Player &player);

} // namespace devilution
