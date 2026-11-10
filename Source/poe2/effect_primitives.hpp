/**
 * @file poe2/effect_primitives.hpp
 *
 * POE2 Mashup: small effects that more than one system can grant (build spec step 4). Each adds up its sources: the
 * gear's stats (Poe2ItemStat, from the ModEffect a rune, soulstone, relic, essence or modifier names), and where they
 * have one the shrine buffs and the passive tree. Gear stats that the tree also has (flask recovery, stun buildup,
 * energy shield recharge) are added to the tree's totals in CalcPlrItemVals, so the code that reads those reads both.
 */
#pragma once

#include <cstdint>
#include <utility>

namespace devilution {

struct Monster;
struct Player;
struct SkillSlot;
enum class DamageType : uint8_t;

/** @brief Leech from gear is capped here, life and mana each, in percent of the hit (build spec step 5). */
constexpr int MaxLeechPercent = 4;
/** @brief Damage taken is recouped evenly over this many seconds, as in POE2. */
constexpr int RecoupSeconds = 4;

/** @brief Gain this percent of each hit's damage as extra damage of the type (fire, lightning or cold). */
[[nodiscard]] int GainAsExtraPercent(const Player &player, DamageType type);
/** @brief Whether the hero avoids ailments: poison, bleeding and chilled ground leave them alone. */
[[nodiscard]] bool AvoidsAilments(const Player &player);

/** @brief Percent of an attack hit's damage the gear leeches as life, or as mana; at most MaxLeechPercent. */
[[nodiscard]] int GearLeechPercent(const Player &player, bool mana);
/**
 * @brief Leeches from the hit the skill dealt, if it is an attack: life and mana, at once like the Leech supports, and
 * less in a map that says so. @p damageRaw is the hit's damage as dealt, a raw Fixed26_6 value.
 */
void LeechFromHit(Player &player, SkillSlot skill, int32_t damageRaw);

/** @brief The physical damage range the gear deals back to a melee attacker, in whole points; 0 to 0 for none. */
[[nodiscard]] std::pair<int, int> ThornsDamage(const Player &player);
/** @brief A monster's melee hit landed on the local hero: the gear's thorns hit it back, with the game's seeded roll. */
void DealThorns(Player &player, Monster &monster);

/** @brief Percent of maximum life, or of maximum mana, the gear recovers on a kill (on top of the flat amounts). */
[[nodiscard]] int PercentOnKill(const Player &player, bool mana);

/** @brief The cold damage range the gear adds to melee hits, in whole points; cold meets magic resistance. */
[[nodiscard]] std::pair<int, int> AddedColdDamage(const Player &player);

/** @brief Percent of armour that applies to hits of the element: the unique rule for all elements and the gear's own. */
[[nodiscard]] int ArmourAppliesToElementPercent(const Player &player, DamageType type);

/** @brief Added to the hero's stun threshold, which is their maximum life: the gear's flat points and its share of energy shield, raw. */
[[nodiscard]] int32_t StunThresholdBonusRaw(const Player &player);
/**
 * @brief A hit's stun damage measured against the hero's maximum life: the hit, made smaller by the threshold the
 * gear adds (StunThresholdBonusRaw) and scaled by the threshold percent. The same as before with no gear threshold.
 */
[[nodiscard]] int64_t StunDamageAgainstLife(const Player &player, int32_t damageRaw, int thresholdPercent);

/** @brief Percent of hit damage taken that is recouped as life. */
[[nodiscard]] int LifeRecoupPercent(const Player &player);
/** @brief The hero took a hit's damage (raw): its recouped share joins what is still to come back. */
void NoteRecoup(Player &player, int64_t damageTakenRaw);
/** @brief Once per game tick: pays out what is left to recoup evenly over the RecoupSeconds since the last hit (not saved, like energy shield). */
void ProcessRecoup(Player &player);

// The runes' primitives (build spec step 5). Each is 0 (or 100 for a percent of a whole) outside POE2.
/** @brief The percent of its usual damage the hero's ignites deal: 100 plus the gear's increased magnitude. */
[[nodiscard]] int IgniteMagnitudePercent(const Player &player);
/** @brief The gear's increased magnitude of the shocks the hero inflicts, in percent. */
[[nodiscard]] int ShockMagnitudeBonus(const Player &player);
/** @brief Life the gear regenerates a second, in hundredths of a percent of maximum life. */
[[nodiscard]] int GearLifeRegenPerTenThousand(const Player &player);
/** @brief Percent of skills' mana costs the gear pays with life instead, at most 100. */
[[nodiscard]] int GearManaCostAsLifePercent(const Player &player);
/** @brief The gear's added chance for attacks to crit, in tenths of a percent. */
[[nodiscard]] int GearAttackCritTenths(const Player &player);
/** @brief The gear's increased movement speed, in percent. */
[[nodiscard]] int GearMovementSpeedPercent(const Player &player);

// The soulstones' primitives (build spec step 6). Each is 0 (or 100 for a percent of a whole) outside POE2.
/** @brief How long the window of ArmourFromRecentHitLoss is: the life lost to hits in the last 8 seconds. */
constexpr int RecentHitLossSeconds = 8;
/** @brief Percent of a poison's or a bleed's damage the hero's gear gives it: 100 plus the gear's increased magnitude. */
[[nodiscard]] int PoisonMagnitudePercent(const Player &player);
[[nodiscard]] int BleedMagnitudePercent(const Player &player);
/** @brief Increased fire, lightning and cold damage of the hero's attacks from gear, in percent. */
[[nodiscard]] int GearElementalAttackDamage(const Player &player);
/** @brief Percent of the monster's resistance to the element (fire, lightning or cold) the hero's attack hits get past. */
[[nodiscard]] int GearAttackPenetration(const Player &player, DamageType type);
/** @brief What the gear adds to the maximum resistance of the element: fire, lightning or cold (magic). */
[[nodiscard]] int GearMaxResistance(const Player &player, DamageType type);
/** @brief Percent of a cast curse's duration the hero's gear gives it: 100 plus the gear's increased skill duration. */
[[nodiscard]] int SkillDurationPercent(const Player &player);
/** @brief A skill's cooldown in game ticks after the gear's faster cooldown recovery; at least 1 when it was. */
[[nodiscard]] int CooldownTicks(const Player &player, int ticks);
/** @brief The maximum mana an item gives for its own armour: one point per ManaPerArmour points; 0 without the stat. */
[[nodiscard]] int ManaFromItemArmour(int manaPerArmour, int armour);
/** @brief Notes life lost to a hit, for ArmourFromRecentHitLoss. */
void NoteHitLifeLoss(const Player &player, int64_t lifeLostRaw);
/** @brief Once per game tick: the life lost to hits more than RecentHitLossSeconds ago drops out (not saved). */
void ProcessRecentHitLoss(Player &player);
/** @brief The armour the gear gives from the life lost to hits in the last RecentHitLossSeconds. */
[[nodiscard]] int RecentHitLossArmour(const Player &player);
/** @brief Physical damage the hero's armour stopped: the gear's share of it is recouped as life (local hero only). */
void NotePreventedPhysical(const Player &player, int64_t stoppedRaw);

} // namespace devilution
