/**
 * @file poe2/attack_speed.hpp
 *
 * POE2 Mashup attack and cast timing. An attack takes 1 / (weapon attacks per second x (1 + increased attack speed) x
 * the skill's attack speed), as in POE2: the weapon's base sets the pace (a Shortsword's 1.35 a second), and every
 * other speed source adds to one "% increased attack speed". A spell takes its cast time / (1 + increased cast speed).
 * The animation stretches or squeezes to fit (AnimationInfo::setTimedAnimation), its hit landing as far through the time
 * as it lands through the animation.
 *
 * The game moves in 20 ticks a second, so each attack takes whole ticks; the fraction left over (kept in 1/65536 ticks,
 * PlayerPoe2::actionTimeRemainder) goes to the next one, so attack after attack averages the exact time.
 *
 * The speed sources Diablo counted in whole animation frames count AttackSpeedPercentPerFrame each, as the passive tree's
 * did (SpeedPercentPerFrame): quick to fastest attack 10% to 40%, a Frenzy charge 10%, a support gem's frame 10%.
 * The tree's and the gear's speed, the uniques', War Banner's and the auras' were percents already.
 *
 * Skills' own speeds and spells' cast times come from txtdata/poe2/skill_speed.tsv (see SkillSpeedKey); a skill with no
 * row attacks at 100% and a spell keeps the time a held cast took before: its animation up to the cast frame.
 */
#pragma once

#include <cstdint>
#include <string>

#include "poe2/player_data.hpp"

namespace devilution {

struct Item;
struct Player;

/** @brief Action times count game ticks in 1/65536ths. */
constexpr int ActionTimeFractionBits = 16;
constexpr uint32_t ActionTimeOneTick = 1U << ActionTimeFractionBits;
/** @brief The fewest and most game ticks one attack or cast may take: 5 a second to one every 3 seconds. */
constexpr int MinActionTicks = 4;
constexpr int MaxActionTicks = 60;
/** @brief The % increased attack (or cast) speed each animation frame of Diablo's speed sources stood for. */
constexpr int AttackSpeedPercentPerFrame = 10;
/** @brief Each Frenzy charge: one frame before. */
constexpr int FrenzyChargeAttackSpeedPercent = AttackSpeedPercentPerFrame;
/**
 * @brief Attacks per second, in hundredths, of Diablo's own weapon bases (no base_meta row: uniques on old bases, the
 * starting gear), from the nearest POE2 base of their kind: Shortsword, Longsword, Shiv, Hatchet, Woodsman's Axe,
 * Cudgel, Sledge, Short Bow and Ash Staff (a POE2 caster staff or sceptre has none of its own and takes these too).
 */
constexpr int DefaultOneHandSwordAps = 135;
constexpr int DefaultTwoHandSwordAps = 115;
constexpr int DefaultDaggerAps = 150;
constexpr int DefaultOneHandAxeAps = 120;
constexpr int DefaultTwoHandAxeAps = 105;
constexpr int DefaultOneHandMaceAps = 110;
constexpr int DefaultTwoHandMaceAps = 95;
constexpr int DefaultBowAps = 130;
constexpr int DefaultStaffAps = 135;
/** @brief Bare hands (and a shield bash): POE2's unarmed attacks. */
constexpr int UnarmedAps = 140;

/** @brief The weapon's base attacks per second in hundredths: its POE2 base's, else its kind's default; 0 for anything else. */
int ItemBaseAttacksPerSecond(const Item &item);
/** @brief The weapon's attacks per second in hundredths with its own increased attack speed, as POE2's tooltip shows. */
int ItemAttacksPerSecond(const Item &item);
/** @brief The hero's weapon's attacks per second in hundredths, with its own increased attack speed; bare hands' without one. */
int WeaponAttacksPerSecond(const Player &player);

/** @brief The skill an attack with the weapon skill uses: the skill's, or for none the weapon's default attack. */
SkillSlot AttackSkillSlot(const Player &player, WeaponSkill skill);
/** @brief The hero's increased attack speed for the skill, in percent: everything but the weapon's own, which its attacks per second hold. */
int IncreasedAttackSpeedPercent(const Player &player, SkillSlot skill);
/** @brief The hero's increased cast speed for the skill, in percent; a learned spell takes its supports' too. */
int IncreasedCastSpeedPercent(const Player &player, SkillSlot skill, bool learnedSpell = true);

/** @brief The skill's attack speed from skill_speed.tsv, in percent of the weapon's (100 without a row). */
int SkillAttackSpeedPercent(SkillSlot skill);
/** @brief The skill's base cast time from skill_speed.tsv in milliseconds, or 0 without one. */
int SkillCastTimeMs(SkillSlot skill);
/** @brief The skill's key in skill_speed.tsv: Flurry for a weapon skill, Spell Firebolt for a spell, Default BowShot for a default attack. */
std::string SkillSpeedKey(SkillSlot skill);

/** @brief One attack of the skill (Double Swing: both strikes), in 1/65536 game ticks. */
uint32_t AttackTime(const Player &player, SkillSlot skill);
/** @brief One cast of the skill, in 1/65536 game ticks; it plays the spell casting animation (a spell or a spell-like skill). */
uint32_t CastTime(const Player &player, SkillSlot skill, bool learnedSpell = true);
/** @brief Double Swing's strike or backhand: their share of the use, by the animation frames each plays. */
uint32_t FlurryStrikeTime(uint32_t useTime, int attackFrames, bool backhand);

/** @brief The whole game ticks the next attack or cast takes, carrying the fraction left over to the one after. */
int TakeActionTicks(Player &player, uint32_t time);
/** @brief Splits an action's ticks around its action frame, as far through them as the frame is through the animation. */
int ActionTicksToActionFrame(int ticks, int actionFrame, int frames);
double ActionSeconds(uint32_t time);

/** @brief Loads txtdata/poe2/skill_speed.tsv again; in Diablo mode it only forgets what was loaded. */
void LoadSkillSpeedData();
/** @brief Sets a skill's row (by SkillSpeedKey) as if skill_speed.tsv had it, for tests and tools; castMs 0 for the default cast time. */
void SetSkillSpeed(const std::string &key, int attackPercent, int castMs);

} // namespace devilution
