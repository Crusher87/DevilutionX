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
#include "utils/fixed_point.hpp"

namespace devilution {

struct Item;
struct Monster;
struct Player;
enum class DamageType : uint8_t;

/** @brief Sessions kept in the poe2_logs folder; older ones are deleted when a new one starts. */
constexpr size_t MaxTelemetrySessions = 20;
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
/** @brief Detailed events kept in memory and written out when the game crashes. */
constexpr size_t TelemetryRecentEvents = 30;
/** @brief Every this many stats lines, the damage dealt and taken summaries are written too. */
constexpr int TelemetrySummaryEvery = 5;

/** @brief Opens this session's log file and starts catching crashes. Safe to call more than once. */
void TelemetryInit();
/** @brief Whether a log file is open. */
bool TelemetryEnabled();
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
/** @brief Names what is about to damage a monster (minions, ignite, a herald); otherwise the last skill gets the credit. */
void TelemetryNoteDamageSource(std::string_view source);
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
/** @brief Writes the error that is about to close the game, then the latest detailed events. */
void TelemetryFatal(std::string_view message);
/** @brief Notes what is being drawn (a fixed text, or nullptr when done), so a crash while drawing names it. */
void TelemetryDrawStep(const char *step);

/** @brief This session's totals and current rates, for the console's stats() command. */
std::string TelemetryStatsText();
/** @brief Damage dealt by each skill this game, most first. */
std::string TelemetrySkillDamageText();
/** @brief Damage taken this game by attacker, most first, with each one's hits and biggest hit. */
std::string TelemetryDamageTakenText();
/** @brief One line describing the character's gear, skills, supports, passives and belt. */
std::string TelemetryBuildText(const Player &player);

} // namespace devilution
