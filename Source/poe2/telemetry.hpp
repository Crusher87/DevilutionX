/**
 * @file poe2/telemetry.hpp
 *
 * POE2 Mashup play log: writes what happens in a game (levels, kills, hits, skills, flasks, drops and
 * crashes) to a text file in the poe2_logs folder, one line per event, so crashes can be traced and
 * the balance tuned from real play.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "poe2/player_data.hpp"
#include "poe2/telemetry_events.hpp"
#include "utils/fixed_point.hpp"

namespace devilution {

struct Item;
struct Monster;
struct Player;
enum class DamageType : uint8_t;

/** @brief Sessions kept in the poe2_logs folder; older ones move to poe2_logs/old when a new one starts. */
constexpr size_t MaxTelemetrySessions = 20;
/** @brief Files kept in poe2_logs/old (a session is a .log and an .events file); the oldest are deleted past this. */
constexpr size_t MaxTelemetryOldFiles = 400;
/** @brief A hit on the player is logged on its own when it takes at least this percent of their life. */
constexpr int TelemetryBigHitPercent = 10;
/** @brief A hit that leaves the player below this percent of their life is logged as a close call. */
constexpr int TelemetryCloseCallPercent = 20;
/** @brief Mana below this percent of the maximum counts as running low. */
constexpr int TelemetryLowManaPercent = 20;
/** @brief A life or mana flask drunk at or above this percent of the maximum is counted as wasted. */
constexpr int TelemetryWastedFlaskPercent = 90;
/** @brief Real-time milliseconds between stats lines. */
constexpr uint32_t TelemetryStatsIntervalMs = 60 * 1000;
/** @brief A gap between game ticks this long is logged as a stall. */
constexpr uint32_t TelemetryStallMs = 250;
/** @brief Damage dealt within this long of using a skill is credited to that skill. */
constexpr uint32_t TelemetrySkillCreditMs = 3000;
/** @brief Monster density: monsters this close (in tiles) count as near the player... */
constexpr int TelemetryNearTiles = 5;
/** @brief ...and awake monsters this close count as aware of them. */
constexpr int TelemetryAwareTiles = 10;
/** @brief This many monsters next to the player counts as surrounded. */
constexpr int TelemetrySurroundedCount = 4;
/** @brief This many monsters near the player at once is logged as a swarm. */
constexpr int TelemetrySwarmCount = 8;
/** @brief Game ticks per second, and the quiet stretch (10 s) logged as a lull. */
constexpr int TelemetryTicksPerSecond = 20;
constexpr int TelemetryLullTicks = 10 * TelemetryTicksPerSecond;
/** @brief Events kept in memory and written out when the game crashes. */
constexpr size_t TelemetryRecentEvents = 300;
/** @brief A fight ends after this long with no enemy near or aware of the player. */
constexpr int TelemetryFightEndTicks = 5 * 20;
/** @brief Crash reports kept in poe2_logs/crashes, each with copies of the save. */
constexpr size_t MaxTelemetryCrashReports = 10;
/** @brief Readable log lines are written to disk at least this often (crashes write them at once). */
constexpr uint32_t TelemetryFlushMs = 1000;
/** @brief Every this many stats lines, the damage dealt and taken summaries are written too. */
constexpr int TelemetrySummaryEvery = 5;

/** @brief Opens this session's log file and starts catching crashes. Safe to call more than once. */
void TelemetryInit();
/** @brief Whether a log file is open. */
bool TelemetryEnabled();
/** @brief Writes what's waiting in memory to both files now (they're otherwise written about once a second). */
void TelemetryFlush();
/** @brief Writes one line: time, level and the event. */
void TelemetryLog(std::string_view category, std::string_view text);
/**
 * @brief Notes an effect going off (a visual effect missile, a code-drawn effect, a missile's burst) with where
 * the hero is, so effects that replay can be traced. Each kind is written at most a few times a second; the rest
 * are counted and summed up once a second.
 */
void TelemetryEffect(std::string_view source, std::string_view what, int x, int y);
/** @brief Notes a step of loading a level, so a crash while loading shows the last step reached. */
void TelemetryStep(std::string_view step);
/** @brief Notes something on screen that had no picture to draw and was skipped, once per kind, so the cause can be found. */
void TelemetryMissingSprite(std::string_view what);

void TelemetryGameStarted(const Player &player, bool loaded);
void TelemetryGameEnded();
void TelemetryLevelEntered();
/** @brief Names what is about to hit the player (a monster or a trap) for the hit that follows. */
void TelemetryNoteAttacker(std::string_view attacker);
/** @brief Notes damage energy shield soaked from the hit about to be applied, so hits on energy shield count as hits taken. */
void TelemetryShieldAbsorbed(const Player &player, Fixed26_6 absorbed);
/** @brief How a monster's melee swing at the hero ended. */
enum class SwingOutcome : uint8_t {
	Landed,
	Evaded,
	Blocked,
	/** Avoided by the start of a dodge roll. */
	Dodged,
};
/** @brief Counts a monster's melee swing at the hero by how it ended, to show how well evasion and block hold up. */
void TelemetryMeleeSwing(const Player &player, SwingOutcome outcome);
/** @brief Counts the hero's attack roll against a monster (melee or arrow) and its hit chance, to show how often attacks miss. */
void TelemetryHeroAttackRoll(const Player &player, int hitChance, bool hit);
/** @brief Counts the hero's hit on a monster that survived it, and whether it light stunned, to tune POE2 light stun. */
void TelemetryMonsterHitByHero(const Player &player, bool lightStunned);
/** @brief Names what is about to damage a monster (minions, ignite, a herald); otherwise the last skill gets the credit. */
void TelemetryNoteDamageSource(std::string_view source);
/**
 * @brief While one is alive, damage to monsters that no source was named for (TelemetryNoteDamageSource) goes to this
 * skill at this gem level, rather than to the last skill used: a rune that went off long after it was laid, or a curse's
 * damage over time. With overTime set, the damage is summed as damage over time, like an ignite.
 */
class TelemetrySkillCreditScope {
public:
	TelemetrySkillCreditScope(std::string_view skill, int gemLevel, bool overTime = false);
	~TelemetrySkillCreditScope();
	TelemetrySkillCreditScope(const TelemetrySkillCreditScope &) = delete;
	TelemetrySkillCreditScope &operator=(const TelemetrySkillCreditScope &) = delete;

private:
	bool active_ = false;
	std::string previous_;
	bool previousOverTime_ = false;
};
/**
 * @brief A hit or tick of damage on a monster. The full damage is what the hit was worth; the life it took away (no
 * more than lifeBefore) is counted on its own, so overkill doesn't hide how hard a skill hits.
 */
void TelemetryMonsterDamaged(const Monster &monster, Fixed26_6 damage, Fixed26_6 lifeBefore);
void TelemetryMonsterKilled(const Monster &monster);
void TelemetryPlayerDamaged(const Player &player, DamageType damageType, Fixed26_6 damage);
void TelemetryPlayerDied(const Player &player);
void TelemetryLevelUp(const Player &player);
/** @brief A socketed gem was raised with an uncut gem: written to the gem log with every step's numbers. */
void TelemetryGemLevelChanged(const Player &player, SkillSlot skill, int fromLevel, int toLevel);
/** @brief A skill was used; gemLevel (when the skill comes from a socketed gem) keeps each gem level's stats apart. */
void TelemetrySkillUsed(const Player &player, std::string_view skill, int manaCost, int gemLevel = 0);
/** @brief A skill or spell that couldn't be used, and why. */
void TelemetrySkillFailed(const Player &player, std::string_view skill, std::string_view reason);
/**
 * @brief A flask was drunk.
 * @param lifePercent, manaPercent Life and mana before drinking, in percent of the maximum.
 */
void TelemetryFlaskUsed(const Player &player, const Item &flask, int chargesLeft, bool life, bool mana, int lifePercent, int manaPercent);
void TelemetryCharmUsed(const Player &player, std::string_view charm);
void TelemetryItemDropped(const Item &item, std::string_view source);
void TelemetryItemPickedUp(const Player &player, const Item &item);
/** @brief Orb or oil use on an item. */
void TelemetryCrafted(const Player &player, std::string_view currency, const Item &item);
/** @brief A line typed with mark() in the console, to find a moment in the log later. */
void TelemetryMark(std::string_view text);
/** @brief Once per game tick: tracks mana, stalls and build changes, and writes the stats line when it's due. */
void TelemetryTick();
/** @brief Once per drawn frame: the frame rate and slow frames for the stats line. */
void TelemetryFrameDrawn();
/**
 * @brief Notes the start of a level change, a game's start or a save being loaded (`what`), with its fades. The game
 * doesn't tick meanwhile, so TelemetryLoadingDone writes how long it took and starts the stall clock afresh: a level
 * change is loading time, not a freeze in play.
 */
void TelemetryLoadingStarted(std::string_view what);
/** @brief The screen has faded out and loading begins. */
void TelemetryLoadingFadedOut();
/** @brief Loading is done and the screen starts fading in. */
void TelemetryLoadingFadingIn();
/** @brief The game is back: writes the load line and restarts the stall clock. */
void TelemetryLoadingDone();
/** @brief Writes the error that is about to close the game, then the latest detailed events. */
void TelemetryFatal(std::string_view message);
/** @brief Notes what is being drawn (a fixed text, or nullptr when done), so a crash while drawing names it. */
void TelemetryDrawStep(const char *step);

/**
 * @brief Deep damage logging (log("damage", "deep") in the console): starts the breakdown of a hit.
 * @param incoming True for a hit on the hero, false for the hero's hit on a monster.
 */
void TelemetryHitStart(bool incoming, std::string_view what);
/** @brief Notes one step of the hit being worked out: the damage so far (raw), or a percentage when percent is set. */
void TelemetryHitStep(bool incoming, const char *step, int64_t value, bool percent = false, bool perPacket = false);
/** @brief Whether hits are being broken down, so callers can skip working out steps that are only logged. */
bool TelemetryDeepDamage();

/** @brief The console's log() command: shows each channel's loudness, or sets one ("all" sets every channel). */
std::string TelemetryLogLevelsText(std::string_view channel, std::string_view level);

/** @brief This session's totals and current rates, for the console's stats() command. */
std::string TelemetryStatsText();
/** @brief Damage dealt by each skill this game, most first. */
std::string TelemetrySkillDamageText();
/** @brief Damage taken this game by attacker, most first, with each one's hits and biggest hit. */
std::string TelemetryDamageTakenText();
/** @brief One line describing the character's gear, skills, supports, passives and belt. */
std::string TelemetryBuildText(const Player &player);

/** @brief For the log's other parts (the passive tree tracker): a line in the readable log only, with no event of its own. */
void TelemetryText(std::string_view category, std::string_view text);
/** @brief An event stamped with the session time and area, like the play log's own. */
TelemetryEvent TelemetryEventNow(LogChannel channel, LogLevel level, std::string_view kind);
/** @brief The poe2_logs folder (empty before TelemetryInit), this session's log file name, and the local date and time. */
const std::string &TelemetryFolder();
std::string_view TelemetrySessionName();
std::string TelemetryLocalStamp();

} // namespace devilution
