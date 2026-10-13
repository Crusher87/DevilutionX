#include "poe2/telemetry.hpp"

#include <algorithm>
#include <array>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include <ankerl/unordered_dense.h>
#include <config.h>

#include "appfat.h"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "options.h"
#include "player.h"
#include "poe2/gem_levels.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/support_gems.hpp"
#include "tables/misdat.h"
#include "utils/file_util.h"
#include "utils/format.hpp"
#include "utils/paths.h"
#include "utils/sdl_compat.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

FILE *LogFile = nullptr;
bool Initialized = false;
uint32_t SessionStartMs = 0;

/** @brief Item and monster rarities, in the order they're counted. */
constexpr size_t NumRarities = 4;
constexpr std::array<std::string_view, NumRarities> RarityNames { "normal", "magic", "rare", "unique" };

/** @brief Counts since the game started, and since the last stats line. */
struct TelemetryCounts {
	int kills = 0;
	int rareKills = 0;
	int uniqueKills = 0;
	int64_t damageTakenRaw = 0;
	int64_t damageDealtRaw = 0;
	int hitsTaken = 0;
	int bigHits = 0;
	int closeCalls = 0;
	int deaths = 0;
	int skillUses = 0;
	int failedSkills = 0;
	int manaSpent = 0;
	int lowManaTicks = 0;
	int ticks = 0;
	int flaskUses = 0;
	int wastedFlasks = 0;
	int charmUses = 0;
	std::array<int, NumRarities> drops {};
	std::array<int, NumRarities> pickups {};
	int crafts = 0;
	int stalls = 0;
	int levelsEntered = 0;
	int64_t experience = 0;
	int gold = 0;
};

/** @brief Damage a skill dealt this game. */
struct SkillDamage {
	int64_t raw = 0;
	int hits = 0;
	int kills = 0;
	/** Time from the first hit to the kill, summed over the kills. */
	uint32_t killMs = 0;
	int uses = 0;
	int mana = 0;
	/** When the skill was first and last used, for its damage per second while in use. */
	uint32_t firstUseMs = 0;
	uint32_t lastUseMs = 0;
};

/** @brief A monster the player is fighting: when it was first hit, and by what. */
struct Engagement {
	uint32_t firstHitMs = 0;
	std::string lastSource;
	int overkill = 0;
};

TelemetryCounts Totals;
TelemetryCounts Window;
uint32_t GameStartMs = 0;
uint32_t WindowStartMs = 0;
uint32_t LastKillMs = 0;
uint32_t LastTickMs = 0;
uint32_t LastStallLogMs = 0;

std::string PendingAttacker;
std::string PendingDamageSource;
std::string LastSkill;
uint32_t LastSkillMs = 0;
std::vector<std::pair<std::string, SkillDamage>> DamageBySkill;
ankerl::unordered_dense::map<size_t, Engagement> Engagements;

/** @brief The level being played: where, since when, and how it's going. */
struct LevelVisit {
	std::string label;
	uint32_t startMs = 0;
	int kills = 0;
	int drops = 0;
	int64_t damageTakenRaw = 0;
};
LevelVisit CurrentLevel;

std::string LastBuild;
uint32_t LastBuildCheckMs = 0;
uint32_t LastExperience = 0;
int LastGold = 0;

std::array<std::string, TelemetryRecentEvents> RecentEvents;
size_t RecentNext = 0;

uint32_t Now()
{
	return static_cast<uint32_t>(SDL_GetTicks());
}

std::string Clock(uint32_t ms)
{
	const uint32_t seconds = ms / 1000;
	return std::format("{:02d}:{:02d}:{:02d}.{:03d}", seconds / 3600, (seconds / 60) % 60, seconds % 60, ms % 1000);
}

std::string LevelLabel()
{
	if (leveltype == DTYPE_TOWN && !setlevel)
		return "town";
	if (setlevel)
		return std::format("set{:d}", static_cast<int>(setlvlnum));
	return std::format("L{:d}", static_cast<int>(currlevel));
}

std::string_view DamageTypeName(DamageType damageType)
{
	switch (damageType) {
	case DamageType::Physical: return "physical";
	case DamageType::Fire: return "fire";
	case DamageType::Lightning: return "lightning";
	case DamageType::Magic: return "magic";
	case DamageType::Acid: return "acid";
	default: return "other";
	}
}

size_t MonsterRarityIndex(const Monster &monster)
{
	if (monster.isUnique())
		return 3;
	switch (GetMonsterAffixes(monster).rarity) {
	case MonsterRarity::Rare: return 2;
	case MonsterRarity::Magic: return 1;
	default: return 0;
	}
}

size_t ItemRarityIndex(const Item &item)
{
	switch (item._iMagical) {
	case ITEM_QUALITY_MAGIC: return 1;
	case ITEM_QUALITY_RARE: return 2;
	case ITEM_QUALITY_UNIQUE: return 3;
	default: return 0;
	}
}

int Percent(Fixed26_6 value, Fixed26_6 max)
{
	if (max <= 0)
		return 100;
	return static_cast<int>(static_cast<int64_t>(value.raw()) * 100 / max.raw());
}

int Whole(int64_t raw)
{
	return static_cast<int>(raw >> 6);
}

void WriteLine(std::string_view line)
{
	if (LogFile == nullptr)
		return;
	std::fwrite(line.data(), 1, line.size(), LogFile);
	std::fputc('\n', LogFile);
	// Flush every line so the log survives a crash.
	std::fflush(LogFile);
}

/** @brief Keeps a detailed event in memory only; the latest are written out if the game crashes. */
void Remember(std::string_view category, std::string_view text)
{
	if (LogFile == nullptr)
		return;
	RecentEvents[RecentNext] = StrCat(Clock(Now() - SessionStartMs), " ", LevelLabel(), " [", category, "] ", text);
	RecentNext = (RecentNext + 1) % RecentEvents.size();
}

void WriteRecentEvents()
{
	if (LogFile == nullptr)
		return;
	std::fputs("Latest detailed events, oldest first:\n", LogFile);
	for (size_t i = 0; i < RecentEvents.size(); i++) {
		const std::string &event = RecentEvents[(RecentNext + i) % RecentEvents.size()];
		if (event.empty())
			continue;
		std::fputs("  ", LogFile);
		std::fputs(event.c_str(), LogFile);
		std::fputc('\n', LogFile);
	}
	std::fflush(LogFile);
}

void PruneOldSessions(const std::string &folder)
{
	std::vector<std::string> sessions;
	for (const std::string &file : ListFiles(folder.c_str())) {
		if (file.rfind("session_", 0) == 0)
			sessions.push_back(file);
	}
	if (sessions.size() < MaxTelemetrySessions)
		return;
	// Names hold the start time, so sorting puts the oldest first.
	std::sort(sessions.begin(), sessions.end());
	const size_t extra = sessions.size() - (MaxTelemetrySessions - 1);
	for (size_t i = 0; i < extra; i++)
		RemoveFile(StrCat(folder, sessions[i]).c_str());
}

#if SDL_VERSION_ATLEAST(2, 0, 0)
SDL_LogOutputFunction PreviousLogOutput = nullptr;
void *PreviousLogUserdata = nullptr;

void SDLCALL LogEngineMessage(void *userdata, int category, SDL_LogPriority priority, const char *message)
{
	if (priority >= SDL_LOG_PRIORITY_WARN)
		TelemetryLog("engine", message);
	if (PreviousLogOutput != nullptr)
		PreviousLogOutput(userdata, category, priority, message);
}
#endif

void HandleCrashSignal(int signal)
{
	if (LogFile != nullptr) {
		std::fprintf(LogFile, "%s %s [crash] signal %d\n", Clock(Now() - SessionStartMs).c_str(), LevelLabel().c_str(), signal);
		WriteRecentEvents();
	}
	std::signal(signal, SIG_DFL);
	std::raise(signal);
}

template <typename Fn>
void Count(Fn &&fn)
{
	fn(Totals);
	fn(Window);
}

std::string CountsByRarity(const std::array<int, NumRarities> &counts)
{
	std::string text;
	for (size_t i = 0; i < NumRarities; i++)
		StrAppend(text, i == 0 ? "" : "/", counts[i]);
	return text;
}

int Sum(const std::array<int, NumRarities> &counts)
{
	int sum = 0;
	for (const int count : counts)
		sum += count;
	return sum;
}

std::string RatesText(const TelemetryCounts &counts, double minutes)
{
	const int lowManaPercent = counts.ticks > 0 ? counts.lowManaTicks * 100 / counts.ticks : 0;
	return std::format("kills {:d} ({:.1f}/min, {:d} rare, {:d} unique), damage dealt {:d} ({:.0f}/min), "
	                   "hits taken {:d} ({:d} big, {:d} close calls, {:d} damage), "
	                   "skills {:d} ({:d} failed, {:d} mana, low on mana {:d}% of the time), "
	                   "flasks {:d} ({:d} wasted), charms {:d}, "
	                   "xp {:d} ({:.0f}/min), gold {:d} ({:.0f}/min), "
	                   "drops {:s} picked up {:s} (normal/magic/rare/unique), crafts {:d}, stalls {:d}",
	    counts.kills, counts.kills / minutes, counts.rareKills, counts.uniqueKills, Whole(counts.damageDealtRaw), Whole(counts.damageDealtRaw) / minutes,
	    counts.hitsTaken, counts.bigHits, counts.closeCalls, Whole(counts.damageTakenRaw),
	    counts.skillUses, counts.failedSkills, counts.manaSpent, lowManaPercent,
	    counts.flaskUses, counts.wastedFlasks, counts.charmUses,
	    counts.experience, counts.experience / minutes, counts.gold, counts.gold / minutes,
	    CountsByRarity(counts.drops), CountsByRarity(counts.pickups), counts.crafts, counts.stalls);
}

void WriteStatsLine(uint32_t now)
{
	const double minutes = std::max<uint32_t>(now - WindowStartMs, 1) / 60000.0;
	TelemetryLog("stats", std::format("last {:.1f} min: {:s}", minutes, RatesText(Window, minutes)));
	Window = {};
	WindowStartMs = now;
}

void EndLevelVisit()
{
	if (CurrentLevel.startMs == 0)
		return;
	WriteLine(StrCat(Clock(Now() - SessionStartMs), " ", CurrentLevel.label, " [level] left after ",
	    std::format("{:.1f} s: {:d} kills, {:d} drops, {:d} damage taken", (Now() - CurrentLevel.startMs) / 1000.0, CurrentLevel.kills,
	        CurrentLevel.drops, Whole(CurrentLevel.damageTakenRaw))));
	CurrentLevel = {};
}

SkillDamage &DamageFor(std::string_view source)
{
	for (auto &[name, damage] : DamageBySkill) {
		if (name == source)
			return damage;
	}
	return DamageBySkill.emplace_back(std::string(source), SkillDamage {}).second;
}

/** @brief What gets the credit for damage to a monster right now. */
std::string CurrentDamageSource()
{
	if (!PendingDamageSource.empty())
		return std::exchange(PendingDamageSource, {});
	if (!LastSkill.empty() && Now() - LastSkillMs <= TelemetrySkillCreditMs)
		return LastSkill;
	return "basic attack";
}

void WriteSettings()
{
	const Options &options = GetOptions();
	const Size resolution = *options.Graphics.resolution;
	TelemetryLog("settings", std::format("resolution {:d}x{:d}, {:s}, zoom {:s}, game speed {:d} ticks/s, difficulty {:d}", resolution.width, resolution.height, *options.Graphics.fullscreen ? "fullscreen" : "windowed", *options.Graphics.zoom ? "on" : "off", *options.Gameplay.tickRate, static_cast<int>(sgGameInitInfo.nDifficulty)));
}

void TrackBuild(const Player &player, uint32_t now)
{
	if (now - LastBuildCheckMs < 1000)
		return;
	LastBuildCheckMs = now;
	std::string build = TelemetryBuildText(player);
	if (build == LastBuild)
		return;
	LastBuild = std::move(build);
	TelemetryLog("build", LastBuild);
}

void TrackGains(const Player &player)
{
	if (player._pExperience > LastExperience) {
		const int64_t gained = player._pExperience - LastExperience;
		Count([&](TelemetryCounts &counts) { counts.experience += gained; });
	}
	LastExperience = player._pExperience;
	if (player._pGold > LastGold) {
		const int gained = player._pGold - LastGold;
		Count([&](TelemetryCounts &counts) { counts.gold += gained; });
	}
	LastGold = player._pGold;
}

void TrackStalls(uint32_t now)
{
	const uint32_t gap = now - LastTickMs;
	LastTickMs = now;
	// Longer gaps are pauses, menus and loading screens rather than stutter.
	if (gap < TelemetryStallMs || gap > 5000)
		return;
	Count([](TelemetryCounts &counts) { counts.stalls++; });
	if (now - LastStallLogMs < 10000)
		return;
	LastStallLogMs = now;
	TelemetryLog("stall", std::format("game froze for {:d} ms ({:d} monsters, {:d} missiles)", gap, static_cast<int>(ActiveMonsterCount), static_cast<int>(Missiles.size())));
}

} // namespace

void TelemetryInit()
{
	if (Initialized)
		return;
	Initialized = true;
	SessionStartMs = Now();

	const std::string folder = StrCat(paths::PrefPath(), "poe2_logs", DIRECTORY_SEPARATOR_STR);
	RecursivelyCreateDir(folder.c_str());
	PruneOldSessions(folder);

	const std::time_t now = std::time(nullptr);
	std::tm local {};
#ifdef _WIN32
	localtime_s(&local, &now);
#else
	localtime_r(&now, &local);
#endif
	char stamp[32];
	std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &local);
	LogFile = OpenFile(StrCat(folder, "session_", stamp, ".log").c_str(), "wb");
	if (LogFile == nullptr)
		return;

	char started[32];
	std::strftime(started, sizeof(started), "%Y-%m-%d %H:%M:%S", &local);
	WriteLine(StrCat("POE2 Mashup play log, ", PROJECT_NAME, " ", PROJECT_VERSION, ", started ", started));
	WriteLine("Each line: time since start, level (town, L<n> or set<n>), [event], details.");

#if SDL_VERSION_ATLEAST(2, 0, 0)
	SDL_GetLogOutputFunction(&PreviousLogOutput, &PreviousLogUserdata);
	SDL_SetLogOutputFunction(&LogEngineMessage, PreviousLogUserdata);
#endif
	for (const int signal : { SIGSEGV, SIGABRT, SIGFPE, SIGILL })
		std::signal(signal, &HandleCrashSignal);
	FatalErrorHook = &TelemetryFatal;
}

bool TelemetryEnabled()
{
	return LogFile != nullptr;
}

void TelemetryLog(std::string_view category, std::string_view text)
{
	if (LogFile == nullptr)
		return;
	WriteLine(StrCat(Clock(Now() - SessionStartMs), " ", LevelLabel(), " [", category, "] ", text));
}

void TelemetryGameStarted(const Player &player, bool loaded)
{
	Totals = {};
	Window = {};
	DamageBySkill.clear();
	Engagements.clear();
	LastSkill.clear();
	PendingAttacker.clear();
	PendingDamageSource.clear();
	LastBuild.clear();
	GameStartMs = WindowStartMs = LastKillMs = LastTickMs = Now();
	LastBuildCheckMs = 0;
	LastExperience = player._pExperience;
	LastGold = player._pGold;
	TelemetryLog("game", std::format("{:s} {:s}: level {:d} {:s}, life {:d}/{:d}, mana {:d}/{:d}, mode {:s}", loaded ? "loaded" : "new game", player._pName, player.getCharacterLevel(), player.getClassName(), player._pHitPoints.whole(), player._pMaxHP.whole(), player._pMana.whole(), player._pMaxMana.whole(), gbIsPoe2 ? "POE2" : (gbIsHellfire ? "Hellfire" : "Diablo")));
	if (LogFile != nullptr) {
		WriteSettings();
		// What each socketed gem does now and at its next level, to compare against the damage lines later.
		std::string gems = SocketedGemsText(player);
		std::replace(gems.begin(), gems.end(), '\n', '|');
		TelemetryLog("gems", gems);
	}
}

void TelemetryGameEnded()
{
	if (LogFile == nullptr || GameStartMs == 0)
		return;
	EndLevelVisit();
	TelemetryLog("game", StrCat("ended. ", TelemetryStatsText()));
	TelemetryLog("damage", TelemetrySkillDamageText());
	GameStartMs = 0;
}

void TelemetryLevelEntered()
{
	EndLevelVisit();
	Engagements.clear();
	CurrentLevel.label = LevelLabel();
	CurrentLevel.startMs = Now();
	Count([](TelemetryCounts &counts) { counts.levelsEntered++; });
	TelemetryLog("level", std::format("entered, {:d} monsters", static_cast<int>(ActiveMonsterCount)));
}

void TelemetryNoteAttacker(std::string_view attacker)
{
	PendingAttacker = attacker;
}

void TelemetryNoteDamageSource(std::string_view source)
{
	PendingDamageSource = source;
}

void TelemetryMonsterDamaged(const Monster &monster, Fixed26_6 damage, Fixed26_6 lifeBefore)
{
	if (LogFile == nullptr || damage <= 0 || lifeBefore <= 0)
		return;
	const std::string source = CurrentDamageSource();
	// Only the damage that took life away counts; the rest is overkill.
	const int64_t dealt = std::min(damage.raw(), lifeBefore.raw());
	Count([&](TelemetryCounts &counts) { counts.damageDealtRaw += dealt; });
	SkillDamage &skill = DamageFor(source);
	skill.raw += dealt;
	skill.hits++;

	Engagement &engagement = Engagements[monster.getId()];
	if (engagement.firstHitMs == 0)
		engagement.firstHitMs = Now();
	engagement.lastSource = source;
	if (damage > lifeBefore)
		engagement.overkill = Whole(damage.raw() - lifeBefore.raw());
	Remember("dealt", std::format("{:d} to {:s} with {:s}, {:d} life left", damage.whole(), monster.name(), source, std::max(Whole(lifeBefore.raw() - damage.raw()), 0)));
}

void TelemetryMonsterKilled(const Monster &monster)
{
	if (LogFile == nullptr)
		return;
	if (monster.isPlayerMinion()) {
		TelemetryLog("minion", std::format("{:s} died", monster.name()));
		return;
	}
	const uint32_t now = Now();
	const size_t rarity = MonsterRarityIndex(monster);
	Count([&](TelemetryCounts &counts) {
		counts.kills++;
		if (rarity == 2)
			counts.rareKills++;
		if (rarity == 3)
			counts.uniqueKills++;
	});
	CurrentLevel.kills++;

	std::string fight;
	const auto it = Engagements.find(monster.getId());
	if (it != Engagements.end()) {
		const Engagement &engagement = it->second;
		SkillDamage &killer = DamageFor(engagement.lastSource);
		killer.kills++;
		killer.killMs += now - engagement.firstHitMs;
		fight = std::format(", killed by {:s} {:.1f} s after the first hit, {:d} overkill", engagement.lastSource, (now - engagement.firstHitMs) / 1000.0, engagement.overkill);
		Engagements.erase(it);
	} else {
		fight = ", killed by a minion or another monster";
	}
	TelemetryLog("kill", std::format("{:s} ({:s}, monster level {:d}){:s}, {:.1f} s since the last kill", monster.name(), RarityNames[rarity], monster.level(sgGameInitInfo.nDifficulty), fight, (now - LastKillMs) / 1000.0));
	LastKillMs = now;
}

void TelemetryPlayerDamaged(const Player &player, DamageType damageType, Fixed26_6 damage)
{
	const std::string attacker = PendingAttacker.empty() ? "unknown" : std::exchange(PendingAttacker, {});
	if (&player != MyPlayer || damage <= 0 || LogFile == nullptr)
		return;
	const Fixed26_6 max = player._pMaxHP;
	const Fixed26_6 after = player._pHitPoints;
	const Fixed26_6 before = after + damage;
	const bool big = max > 0 && damage.raw() * 100 >= static_cast<int64_t>(max.raw()) * TelemetryBigHitPercent;
	const bool closeCall = after > 0 && Percent(after, max) < TelemetryCloseCallPercent && Percent(before, max) >= TelemetryCloseCallPercent;
	Count([&](TelemetryCounts &counts) {
		counts.hitsTaken++;
		counts.damageTakenRaw += damage.raw();
		if (big)
			counts.bigHits++;
		if (closeCall)
			counts.closeCalls++;
	});
	CurrentLevel.damageTakenRaw += damage.raw();
	const std::string text = std::format("took {:d} {:s} damage from {:s}, life {:d}/{:d}", damage.whole(), DamageTypeName(damageType), attacker, after.whole(), max.whole());
	if (closeCall)
		TelemetryLog("close call", text);
	else if (big)
		TelemetryLog("hit", text);
	else
		Remember("hit", text);
}

void TelemetryPlayerDied(const Player &player)
{
	if (&player != MyPlayer)
		return;
	Count([](TelemetryCounts &counts) { counts.deaths++; });
	TelemetryLog("death", std::format("died at character level {:d}", player.getCharacterLevel()));
	WriteRecentEvents();
}

void TelemetryLevelUp(const Player &player)
{
	if (&player != MyPlayer)
		return;
	TelemetryLog("levelup", std::format("character level {:d}, life {:d}, mana {:d}, {:.1f} min into the game", player.getCharacterLevel(), player._pMaxHP.whole(), player._pMaxMana.whole(), (Now() - GameStartMs) / 60000.0));
}

void TelemetrySkillUsed(const Player &player, std::string_view skill, int manaCost, int gemLevel)
{
	if (&player != MyPlayer)
		return;
	// Each gem level is tracked on its own, so the stats show what a level is worth.
	LastSkill = gemLevel > 0 ? std::format("{:s} gem {:d}", skill, gemLevel) : std::string(skill);
	LastSkillMs = Now();
	Count([&](TelemetryCounts &counts) {
		counts.skillUses++;
		counts.manaSpent += manaCost;
	});
	if (LogFile != nullptr) {
		SkillDamage &stats = DamageFor(LastSkill);
		stats.uses++;
		stats.mana += manaCost;
		if (stats.firstUseMs == 0)
			stats.firstUseMs = LastSkillMs;
		stats.lastUseMs = LastSkillMs;
	}
	TelemetryLog("skill", std::format("{:s}, {:d} mana, {:d}/{:d} left", LastSkill, manaCost, player._pMana.whole(), player._pMaxMana.whole()));
}

void TelemetrySkillFailed(const Player &player, std::string_view skill, std::string_view reason)
{
	if (&player != MyPlayer)
		return;
	Count([](TelemetryCounts &counts) { counts.failedSkills++; });
	TelemetryLog("skill failed", std::format("{:s}: {:s}, mana {:d}/{:d}", skill, reason, player._pMana.whole(), player._pMaxMana.whole()));
}

void TelemetryFlaskUsed(const Player &player, const Item &flask, int chargesLeft, bool life, bool mana, int lifePercent, int manaPercent)
{
	if (&player != MyPlayer)
		return;
	// A flask is wasted when nothing it restores was needed.
	const bool wasted = (!life || lifePercent >= TelemetryWastedFlaskPercent) && (!mana || manaPercent >= TelemetryWastedFlaskPercent);
	Count([&](TelemetryCounts &counts) {
		counts.flaskUses++;
		if (wasted)
			counts.wastedFlasks++;
	});
	TelemetryLog("flask", std::format("{:s}, {:d} charges left, drunk at {:d}% life and {:d}% mana{:s}", flask._iIName, chargesLeft, lifePercent, manaPercent, wasted ? " (wasted)" : ""));
}

void TelemetryCharmUsed(const Player &player, std::string_view charm)
{
	if (&player != MyPlayer)
		return;
	Count([](TelemetryCounts &counts) { counts.charmUses++; });
	TelemetryLog("charm", std::format("{:s} went off, life {:d}/{:d}", charm, player._pHitPoints.whole(), player._pMaxHP.whole()));
}

void TelemetryItemDropped(const Item &item, std::string_view source)
{
	// Gold is counted as it's picked up, in the gold per minute.
	if (item.isEmpty() || item._itype == ItemType::Gold)
		return;
	const size_t rarity = ItemRarityIndex(item);
	Count([&](TelemetryCounts &counts) { counts.drops[rarity]++; });
	CurrentLevel.drops++;
	TelemetryLog("drop", std::format("{:s} ({:s}, item level {:d}) from {:s}", item._iIName, RarityNames[rarity], item._iCreateInfo & CF_LEVEL, source));
}

void TelemetryItemPickedUp(const Player &player, const Item &item)
{
	if (&player != MyPlayer || item.isEmpty() || item._itype == ItemType::Gold)
		return;
	const size_t rarity = ItemRarityIndex(item);
	Count([&](TelemetryCounts &counts) { counts.pickups[rarity]++; });
	TelemetryLog("pickup", std::format("{:s} ({:s})", item._iIName, RarityNames[rarity]));
}

void TelemetryCrafted(const Player &player, std::string_view currency, const Item &item)
{
	if (&player != MyPlayer)
		return;
	Count([](TelemetryCounts &counts) { counts.crafts++; });
	TelemetryLog("craft", std::format("{:s} on {:s}, now {:s}", currency, item._iIName, RarityNames[ItemRarityIndex(item)]));
}

void TelemetryMark(std::string_view text)
{
	TelemetryLog("mark", text);
}

void TelemetryTick()
{
	if (LogFile == nullptr || GameStartMs == 0 || MyPlayer == nullptr)
		return;
	const uint32_t now = Now();
	const Player &player = *MyPlayer;
	const bool lowMana = player._pMaxMana > 0 && Percent(player._pMana, player._pMaxMana) < TelemetryLowManaPercent;
	Count([&](TelemetryCounts &counts) {
		counts.ticks++;
		if (lowMana)
			counts.lowManaTicks++;
	});
	TrackGains(player);
	TrackStalls(now);
	TrackBuild(player, now);
	if (now - WindowStartMs >= TelemetryStatsIntervalMs)
		WriteStatsLine(now);
}

void TelemetryFatal(std::string_view message)
{
	TelemetryLog("crash", message);
	WriteRecentEvents();
}

std::string TelemetryStatsText()
{
	const double minutes = std::max<uint32_t>(Now() - GameStartMs, 1) / 60000.0;
	const int dropped = Sum(Totals.drops);
	return std::format("{:.1f} min played, {:d} deaths, {:d} levels entered, {:.0f} drops/hour: {:s}", minutes, Totals.deaths, Totals.levelsEntered,
	    dropped / minutes * 60, RatesText(Totals, minutes));
}

std::string TelemetrySkillDamageText()
{
	std::vector<std::pair<std::string, SkillDamage>> sorted = DamageBySkill;
	std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.second.raw > b.second.raw; });
	std::string text = "damage by skill:";
	if (sorted.empty())
		StrAppend(text, " none yet");
	for (const auto &[name, damage] : sorted) {
		const int average = damage.hits > 0 ? Whole(damage.raw) / damage.hits : 0;
		StrAppend(text, std::format(" {:s} {:d} ({:d} hits, {:d} average, {:d} kills", name, Whole(damage.raw), damage.hits, average, damage.kills));
		if (damage.kills > 0)
			StrAppend(text, std::format(", {:.1f} s to kill", damage.killMs / 1000.0 / damage.kills));
		if (damage.uses > 0)
			StrAppend(text, std::format(", {:d} uses", damage.uses));
		if (damage.mana > 0)
			StrAppend(text, std::format(", {:d} mana, {:.1f} damage per mana", damage.mana, static_cast<double>(Whole(damage.raw)) / damage.mana));
		if (damage.lastUseMs > damage.firstUseMs)
			StrAppend(text, std::format(", {:.0f} damage per second in use", Whole(damage.raw) * 1000.0 / (damage.lastUseMs - damage.firstUseMs + TelemetrySkillCreditMs)));
		StrAppend(text, ");");
	}
	return text;
}

std::string TelemetryBuildText(const Player &player)
{
	std::string text = std::format("level {:d} {:s}, life {:d}, mana {:d}, str {:d} mag {:d} dex {:d} vit {:d} | gear:", player.getCharacterLevel(), player.getClassName(),
	    player._pMaxHP.whole(), player._pMaxMana.whole(), player._pStrength, player._pMagic, player._pDexterity, player._pVitality);
	for (const Item &item : player.InvBody) {
		if (!item.isEmpty())
			StrAppend(text, " ", item._iIName, ";");
	}
	StrAppend(text, " | skills:");
	for (size_t i = 0; i < player.poe2.skillSlots.size(); i++) {
		const SkillSlot slot = player.poe2.skillSlots[i];
		if (slot.isEmpty())
			continue;
		StrAppend(text, " ", SkillName(slot), " gem ", player.poe2.gemLevel(i));
		for (const SupportGem support : player.poe2.slotSupports[i]) {
			if (support != SupportGem::None)
				StrAppend(text, " +", SupportGemName(support));
		}
		StrAppend(text, ";");
	}
	StrAppend(text, " | passives ", player.poe2.passives.count(), " | belt:");
	for (const Item &item : player.SpdList) {
		if (!item.isEmpty())
			StrAppend(text, " ", item._iIName, ";");
	}
	return text;
}

} // namespace devilution
