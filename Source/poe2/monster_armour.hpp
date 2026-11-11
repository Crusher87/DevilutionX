/**
 * @file poe2/monster_armour.hpp
 *
 * POE2 Mashup monster armour (monster armour plan v1, Curtis 2026-10-31): each physical hit of the hero loses a share
 * to the monster's armour, POE2's armour / (armour + 10 x hit), at most 75%. Big hits barely notice it; many small hits
 * notice it more. Damage over time (bleed, poison, ignite) and the other damage types never meet it, and Diablo's
 * armour class still decides how often the hero hits.
 *
 * A monster's armour is the level table's (MonsterLevelArmour, which keeps POE2's ratio to a white's life) times its
 * kind's percent (monster_armour.tsv), the rare and magic "Plated" modifier and the waystone's "Armoured". Broken
 * armour counts as none. Nothing is saved: it is worked out on every hit.
 */
#pragma once

#include <cstdint>
#include <string_view>

namespace devilution {

struct Monster;
struct Player;
enum class DamageType : uint8_t;

/** @brief POE2's cap on a monster's physical damage reduction from armour (Monster.ot). */
constexpr int MaxMonsterArmourReductionPercent = 75;
/** @brief POE2's armour ratio: armour / (armour + ArmourHitRatio x hit). */
constexpr int ArmourHitRatio = 10;
/** @brief The strength dial on every monster's armour, in percent (100 is POE2's own; Curtis's choice). */
constexpr int ArmourScalePercent = 100;
/** @brief The "Plated" modifier: armour as if the kind had max(its percent, this) times PlatedModMultiplier. */
constexpr int PlatedModMinimumPercent = 50;
constexpr int PlatedModMultiplier = 2;
/** @brief The armour words of the banner and info box, by effective percent: up to Light, up to Armoured, then Heavy. */
constexpr int LightArmourMaxPercent = 30;
constexpr int ArmouredMaxPercent = 70;

/** @brief Reads monster_armour.tsv in POE2 mode; clears the table outside it. */
void LoadMonsterArmour();
/** @brief A monster kind's armour (a _monster_id) in percent of the level table: 0 when not listed or outside POE2. */
[[nodiscard]] int MonsterTypeArmourPercent(int monsterType);
/**
 * @brief A monster's armour in percent of the level table: its kind's, with the Plated modifier and the waystone's
 * "Armoured". 0 outside POE2 mode, for the player's minions and golems, and for monsters off the POE2 tables. Broken
 * armour isn't counted here (see MonsterArmourRating).
 */
[[nodiscard]] int MonsterArmourPercent(const Monster &monster);
/** @brief The monster's armour rating now: its level's armour times MonsterArmourPercent; 0 while its armour is broken. */
[[nodiscard]] int MonsterArmourRating(const Monster &monster);
/**
 * @brief POE2's reduction in percent from an armour rating against a hit (a raw Fixed26_6):
 * armour / (armour + 10 x hit), at most @p capPercent.
 */
[[nodiscard]] int ArmourReductionPercentCapped(int armour, int64_t rawHit, int capPercent);
/** @brief The monster's reduction in percent against a physical hit (a raw Fixed26_6). */
[[nodiscard]] int MonsterArmourReductionPercent(const Monster &monster, int64_t rawHit);

/** @brief How a hit meets the monster's armour. */
enum class ArmourHit : uint8_t {
	/** A hit of the hero's skill: armour applies unless Brutality III's roll for this packet said to ignore it. */
	Hit,
	/** Damage dealt alongside a hit (a support's extra damage, a burst): armour applies, Brutality's roll doesn't. */
	Extra,
	/** Thorns and the hero's minions: armour never applies. */
	Ignore,
};

/**
 * @brief A hero's packet on a monster, a raw Fixed26_6, after the monster's damage taken (MonsterDamageTakenPercent)
 * and, for physical damage in POE2 mode, its armour. The damage taken is worked out first, which makes Brutality's one
 * roll for the packet; the armour's reduction is then worked out from the hit as the attacker's more and less leave it,
 * before the monster's own damage taken, as in POE2. The local player's hits note the deep breakdown step, what armour
 * stopped (TelemetryArmourStopped) and the reduction for the poison and bleed bases of the hit in progress.
 */
[[nodiscard]] int64_t MonsterHitDamage(const Player &player, const Monster &monster, DamageType type, int64_t raw, ArmourHit armour = ArmourHit::Hit);

/** @brief The armour word for an effective percent: "", "Light armour", "Armoured" or "Heavy armour" (translated). */
[[nodiscard]] std::string_view ArmourTierText(int percent);
/** @brief The monster's armour word: "Armour broken" while broken, else ArmourTierText of MonsterArmourPercent. */
[[nodiscard]] std::string_view MonsterArmourText(const Monster &monster);

} // namespace devilution
