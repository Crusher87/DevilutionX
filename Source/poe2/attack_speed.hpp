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
 * row attacks at 100% and a spell keeps the time a held cast took before: its animation up to the cast frame. A row
 * may instead give a fixed attack time (attack_time_s: POE2's shield skills, which ignore the weapon but not increased
 * attack speed), flat time added after the speed (added_ms: Leap Slam's 650 ms, Comet's 1000), and time added by how
 * far a dash went (distance_ms, at its full length). A skill in several steps has a row for each step after the first,
 * keyed "Skill:step" (Double Swing's backhand, Fists of Thunder's finisher; see SkillStep).
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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
/** @brief Bare hands (and a shield bash): POE2's unarmed attacks, 1.65 a second (PoB2's data.unarmedWeaponData). */
constexpr int UnarmedAps = 165;

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

/** @brief A step of a skill that strikes in steps, each timed by its own row ("Flurry:backhand"); Main is the skill's own row. */
enum class SkillStep : uint8_t {
	Main,
	/** Double Swing's second strike. */
	Backhand,
	/** Fists of Thunder's every third strike, the one that bursts with lightning. */
	Finisher,
};

/** @brief A row of skill_speed.tsv. */
struct SkillSpeedRow {
	/** Uses per second in percent of the weapon's attacks per second. */
	int attackPercent = 100;
	/** A cast time in milliseconds, which increased cast speed shortens; 0 for none. */
	int castMs = 0;
	/** A fixed attack time in milliseconds instead of the weapon's, which increased attack speed shortens; 0 for none. */
	int attackMs = 0;
	/** Milliseconds added after the speed, which no speed shortens. */
	int addedMs = 0;
	/** Milliseconds added at a dash's full length, less for a shorter one. */
	int distanceMs = 0;
};

/** @brief The skill's attack speed from skill_speed.tsv, in percent of the weapon's (100 without a row). */
int SkillAttackSpeedPercent(SkillSlot skill, SkillStep step = SkillStep::Main);
/** @brief The skill's base cast time from skill_speed.tsv in milliseconds, or 0 without one. */
int SkillCastTimeMs(SkillSlot skill);
/** @brief The skill's row in skill_speed.tsv (the step's, else the skill's), or nullptr without one. */
const SkillSpeedRow *FindSkillSpeedRow(SkillSlot skill, SkillStep step = SkillStep::Main);
/** @brief The skill's key in skill_speed.tsv: Flurry for a weapon skill, Spell Firebolt for a spell, Default BowShot for a default attack; Flurry:backhand for a step. */
std::string SkillSpeedKey(SkillSlot skill, SkillStep step = SkillStep::Main);

/** @brief One attack of the skill or of its step, with any added time, in 1/65536 game ticks. */
uint32_t AttackTime(const Player &player, SkillSlot skill, SkillStep step = SkillStep::Main);
/** @brief One cast of the skill, with any added time, in 1/65536 game ticks; it plays the spell casting animation (a spell or a spell-like skill). */
uint32_t CastTime(const Player &player, SkillSlot skill, bool learnedSpell = true);
/**
 * @brief Whether a use of the skill is timed as a cast (its cast time and cast speed) rather than an attack: as its row
 * says (a cast time and no fixed attack time), else a spell, else castAnimation (a skill playing a casting animation).
 */
bool IsCastTimed(SkillSlot skill, bool castAnimation);
/**
 * @brief One use of a weapon skill or default attack, or of its step, as the game times it: AttackTime or CastTime (see
 * IsCastTimed), with the row's distance time for a dash that went distancePercent of its full length.
 */
uint32_t SkillUseTime(const Player &player, SkillSlot skill, bool castAnimation, SkillStep step = SkillStep::Main, int distancePercent = 0);
/**
 * @brief How much longer than the skill's own time its use takes because no action is quicker than MinActionTicks, in
 * percent: 100 unless SkillUseTime was raised to the least.
 */
int ShortestActionPercent(const Player &player, SkillSlot skill, bool castAnimation, SkillStep step = SkillStep::Main);

/** @brief The whole game ticks the next attack or cast takes, carrying the fraction left over to the one after. */
int TakeActionTicks(Player &player, uint32_t time);
/** @brief Splits an action's ticks around its action frame, as far through them as the frame is through the animation. */
int ActionTicksToActionFrame(int ticks, int actionFrame, int frames);
double ActionSeconds(uint32_t time);

/** @brief Loads txtdata/poe2/skill_speed.tsv again; in Diablo mode it only forgets what was loaded. */
void LoadSkillSpeedData();
/** @brief Sets a skill's row (by SkillSpeedKey) as if skill_speed.tsv had it, for tests and tools; castMs 0 for the default cast time. */
void SetSkillSpeed(const std::string &key, int attackPercent, int castMs);
void SetSkillSpeed(const std::string &key, const SkillSpeedRow &row);
/** @brief The rows of skill_speed.tsv whose key names no skill or step (logged when it loads); empty when all are known. */
std::vector<std::string> UnknownSkillSpeedKeys();
/** @brief Whether a skill_speed.tsv key names a weapon skill, a default attack or a spell, or one's step. */
bool IsKnownSkillSpeedKey(std::string_view key);

} // namespace devilution
