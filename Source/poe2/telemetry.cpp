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

#ifdef _WIN32
// Suppress definitions of `min` and `max` macros by <windows.h>:
#define NOMINMAX 1
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// <windows.h> defines these as empty; this file uses them as names.
#undef near
#undef far
#endif

namespace devilution {

namespace {

FILE *LogFile = nullptr;
/** @brief The gem log: one file kept across sessions, so a gem's levels and results can be followed over time. */
FILE *GemLogFile = nullptr;
/** @brief Every mark("...") note ever typed, with the date and session: kept forever, unlike the session logs, so no note is lost. */
FILE *NotesFile = nullptr;
/** @brief The session log's name, so each note says which session it came from. */
std::string SessionName;
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
	/** Frames drawn, how many took longer than a 30 fps frame, and the slowest, to keep the frame rate honest. */
	int frames = 0;
	int slowFrames = 0;
	uint32_t worstFrameMs = 0;
	uint32_t frameMs = 0;
	int levelsEntered = 0;
	int64_t experience = 0;
	int gold = 0;
	/** Monster density, sampled every game tick: hostile monsters near the player, aware ones, and how often none or a crowd. */
	int densityTicks = 0;
	int64_t nearSum = 0;
	int64_t awareSum = 0;
	int peakNear = 0;
	int quietTicks = 0;
	int surroundedTicks = 0;
	int swarms = 0;
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
	int monstersAtEntry = 0;
};
LevelVisit CurrentLevel;

/** @brief Ticks since an enemy was last near, and whether a swarm is in progress, for the lull and swarm events. */
int QuietStreakTicks = 0;
bool InSwarm = false;
uint32_t LastSwarmLogMs = 0;

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

#if defined(_WIN32) && (defined(__x86_64__) || defined(_M_X64))
/**
 * @brief Windows ends the game on an access violation before std::signal sees it in some cases, so the
 * crash is caught here first: the exception, where it happened and the call stack, as offsets into the
 * exe that the build's link map (devilutionx.map) turns into function names.
 */
LONG WINAPI HandleWindowsException(EXCEPTION_POINTERS *info)
{
	const DWORD code = info->ExceptionRecord->ExceptionCode;
	switch (code) {
	case EXCEPTION_ACCESS_VIOLATION:
	case EXCEPTION_STACK_OVERFLOW:
	case EXCEPTION_ILLEGAL_INSTRUCTION:
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
	case EXCEPTION_PRIV_INSTRUCTION:
	case EXCEPTION_IN_PAGE_ERROR:
		break;
	default:
		return EXCEPTION_CONTINUE_SEARCH;
	}
	static bool written = false;
	if (written || LogFile == nullptr)
		return EXCEPTION_CONTINUE_SEARCH;
	written = true;

	const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
	const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
	const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
	const uintptr_t end = base + nt->OptionalHeader.SizeOfImage;
	const auto where = [&](uintptr_t address) {
		char text[48];
		if (address >= base && address < end)
			std::snprintf(text, sizeof(text), "exe+0x%llx", static_cast<unsigned long long>(address - base));
		else
			std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(address));
		return std::string(text);
	};

	const auto address = reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress);
	std::fprintf(LogFile, "%s %s [crash] exception 0x%08lx at %s", Clock(Now() - SessionStartMs).c_str(), LevelLabel().c_str(), static_cast<unsigned long>(code), where(address).c_str());
	if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2) {
		const ULONG_PTR kind = info->ExceptionRecord->ExceptionInformation[0];
		std::fprintf(LogFile, " (%s 0x%llx)", kind == 0 ? "reading" : kind == 1 ? "writing" : "executing", static_cast<unsigned long long>(info->ExceptionRecord->ExceptionInformation[1]));
	}
	std::fprintf(LogFile, ", image base 0x%llx\n", static_cast<unsigned long long>(base));

	// Walk the stack from the faulting frame with the exe's own unwind tables.
	std::fputs("Call stack:", LogFile);
	if (code != EXCEPTION_STACK_OVERFLOW) {
		CONTEXT context = *info->ContextRecord;
		for (int frame = 0; frame < 48 && context.Rip != 0; frame++) {
			std::fprintf(LogFile, " %s", where(context.Rip).c_str());
			DWORD64 imageBase = 0;
			PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
			if (function == nullptr) {
				context.Rip = *reinterpret_cast<DWORD64 *>(context.Rsp);
				context.Rsp += 8;
				continue;
			}
			PVOID handlerData = nullptr;
			DWORD64 establisherFrame = 0;
			RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData, &establisherFrame, nullptr);
		}
	}
	std::fputc('\n', LogFile);
	WriteRecentEvents();
	std::fflush(LogFile);
	return EXCEPTION_CONTINUE_SEARCH;
}
#endif

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

std::string DensityText(const TelemetryCounts &counts)
{
	if (counts.densityTicks == 0)
		return "density: no dungeon time";
	const double ticks = counts.densityTicks;
	return std::format("density: {:.1f} monsters within {:d} tiles on average (peak {:d}), {:.1f} aware within {:d}, "
	                   "no enemy near {:.0f}% of the time, surrounded {:.0f}%, {:d} swarms",
	    counts.nearSum / ticks, TelemetryNearTiles, counts.peakNear, counts.awareSum / ticks, TelemetryAwareTiles,
	    counts.quietTicks * 100 / ticks, counts.surroundedTicks * 100 / ticks, counts.swarms);
}

/** @brief Monsters that fight the player: alive, not minions, not towners. */
bool IsHostile(const Monster &monster)
{
	return !monster.isPlayerMinion() && !monster.hasNoLife() && monster.talkMsg == TEXT_NONE;
}

int HostileMonsterCount()
{
	int count = 0;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		if (IsHostile(Monsters[ActiveMonsters[i]]))
			count++;
	}
	return count;
}

/** @brief Samples how many monsters are around the player this tick. */
void TrackDensity(const Player &player, uint32_t now)
{
	int near = 0;
	int aware = 0;
	int adjacent = 0;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &monster = Monsters[ActiveMonsters[i]];
		if (!IsHostile(monster))
			continue;
		const int distance = player.position.tile.WalkingDistance(monster.position.tile);
		if (distance <= 1)
			adjacent++;
		if (distance <= TelemetryNearTiles)
			near++;
		if (distance <= TelemetryAwareTiles && monster.activeForTicks > 0)
			aware++;
	}
	const bool quiet = aware == 0 && near == 0;
	const bool surrounded = adjacent >= TelemetrySurroundedCount;
	Count([&](TelemetryCounts &counts) {
		counts.densityTicks++;
		counts.nearSum += near;
		counts.awareSum += aware;
		counts.peakNear = std::max(counts.peakNear, near);
		if (quiet)
			counts.quietTicks++;
		if (surrounded)
			counts.surroundedTicks++;
	});

	// A long stretch with nothing to fight is logged when it ends, so slow pacing shows up.
	if (quiet) {
		QuietStreakTicks++;
	} else {
		if (QuietStreakTicks >= TelemetryLullTicks)
			TelemetryLog("lull", std::format("{:.1f} s with no enemy near, ended by {:d} monsters", QuietStreakTicks / static_cast<double>(TelemetryTicksPerSecond), std::max(near, aware)));
		QuietStreakTicks = 0;
	}

	if (near >= TelemetrySwarmCount && !InSwarm) {
		InSwarm = true;
		Count([](TelemetryCounts &counts) { counts.swarms++; });
		if (now - LastSwarmLogMs >= 10000) {
			LastSwarmLogMs = now;
			TelemetryLog("swarm", std::format("{:d} monsters within {:d} tiles, {:d} next to the player, life {:d}%", near, TelemetryNearTiles, adjacent, Percent(player._pHitPoints, player._pMaxHP)));
		}
	} else if (near < TelemetrySwarmCount / 2) {
		InSwarm = false;
	}
}

std::string RatesText(const TelemetryCounts &counts, double minutes)
{
	const int lowManaPercent = counts.ticks > 0 ? counts.lowManaTicks * 100 / counts.ticks : 0;
	return StrCat(DensityText(counts), "; ", std::format("kills {:d} ({:.1f}/min, {:d} rare, {:d} unique), damage dealt {:d} ({:.0f}/min), "
	                                                     "hits taken {:d} ({:d} big, {:d} close calls, {:d} damage), "
	                                                     "skills {:d} ({:d} failed, {:d} mana, low on mana {:d}% of the time), "
	                                                     "flasks {:d} ({:d} wasted), charms {:d}, "
	                                                     "xp {:d} ({:.0f}/min), gold {:d} ({:.0f}/min), "
	                                                     "drops {:s} picked up {:s} (normal/magic/rare/unique), crafts {:d}, stalls {:d}, "
	                                                     "fps {:.0f} ({:d} frames under 30 fps, slowest {:d} ms)",
	                                             counts.kills, counts.kills / minutes, counts.rareKills, counts.uniqueKills, Whole(counts.damageDealtRaw), Whole(counts.damageDealtRaw) / minutes, counts.hitsTaken, counts.bigHits, counts.closeCalls, Whole(counts.damageTakenRaw), counts.skillUses, counts.failedSkills, counts.manaSpent, lowManaPercent, counts.flaskUses, counts.wastedFlasks, counts.charmUses, counts.experience, counts.experience / minutes, counts.gold, counts.gold / minutes, CountsByRarity(counts.drops), CountsByRarity(counts.pickups), counts.crafts, counts.stalls,
	                                             counts.frameMs > 0 ? counts.frames * 1000.0 / counts.frameMs : 0.0, counts.slowFrames, counts.worstFrameMs));
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
	    std::format("{:.1f} s: {:d} kills of {:d} monsters ({:d}% cleared), {:d} drops, {:d} damage taken", (Now() - CurrentLevel.startMs) / 1000.0, CurrentLevel.kills,
	        CurrentLevel.monstersAtEntry, CurrentLevel.monstersAtEntry > 0 ? CurrentLevel.kills * 100 / CurrentLevel.monstersAtEntry : 0,
	        CurrentLevel.drops, Whole(CurrentLevel.damageTakenRaw))));
	CurrentLevel = {};
}

std::string SkillDamageLine(std::string_view name, const SkillDamage &damage)
{
	const int average = damage.hits > 0 ? Whole(damage.raw) / damage.hits : 0;
	std::string text = std::format("{:s} {:d} ({:d} hits, {:d} average, {:d} kills", name, Whole(damage.raw), damage.hits, average, damage.kills);
	if (damage.kills > 0)
		StrAppend(text, std::format(", {:.1f} s to kill", damage.killMs / 1000.0 / damage.kills));
	if (damage.uses > 0)
		StrAppend(text, std::format(", {:d} uses", damage.uses));
	if (damage.mana > 0)
		StrAppend(text, std::format(", {:d} mana, {:.1f} damage per mana", damage.mana, static_cast<double>(Whole(damage.raw)) / damage.mana));
	if (damage.lastUseMs > damage.firstUseMs)
		StrAppend(text, std::format(", {:.0f} damage per second in use", Whole(damage.raw) * 1000.0 / (damage.lastUseMs - damage.firstUseMs + TelemetrySkillCreditMs)));
	text += ')';
	return text;
}

/** @brief The local date and time, for the files kept across sessions. */
std::string LocalStamp()
{
	const std::time_t now = std::time(nullptr);
	std::tm local {};
#ifdef _WIN32
	localtime_s(&local, &now);
#else
	localtime_r(&now, &local);
#endif
	char stamp[32];
	std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
	return stamp;
}

/** @brief Writes a line to the gem log, with the date and time, and to the session log. */
void GemLog(std::string_view text)
{
	TelemetryLog("gem", text);
	if (GemLogFile == nullptr)
		return;
	const std::string stamp = LocalStamp();
	std::fprintf(GemLogFile, "%s %.*s\n", stamp.c_str(), static_cast<int>(text.size()), text.data());
	std::fflush(GemLogFile);
}

/** @brief Each gem level's results this game, to the gem log. */
void WriteGemResults()
{
	for (const auto &[name, damage] : DamageBySkill) {
		if (name.find(" gem ") != std::string::npos)
			GemLog(StrCat("results: ", SkillDamageLine(name, damage)));
	}
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
	GemLogFile = OpenFile(StrCat(folder, "gem_levels.log").c_str(), "ab");
	NotesFile = OpenFile(StrCat(folder, "playtest_notes.txt").c_str(), "ab");
	SessionName = StrCat("session_", stamp, ".log");

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
#if defined(_WIN32) && (defined(__x86_64__) || defined(_M_X64))
	AddVectoredExceptionHandler(1, &HandleWindowsException);
#endif
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

namespace {

/** @brief One kind of effect's lines in the current second. */
struct EffectRate {
	uint32_t windowStart = 0;
	int written = 0;
	int skipped = 0;
	int lastX = 0;
	int lastY = 0;
};

constexpr int EffectLinesPerSecond = 3;
ankerl::unordered_dense::map<std::string, EffectRate> EffectRates;

std::string HeroTile()
{
	if (MyPlayer == nullptr)
		return {};
	return std::format(", hero at {:d},{:d}", MyPlayer->position.tile.x, MyPlayer->position.tile.y);
}

void FlushEffects(uint32_t now, bool all)
{
	for (auto &[key, rate] : EffectRates) {
		if (rate.skipped == 0 || (!all && now - rate.windowStart < 1000))
			continue;
		TelemetryLog("fx", std::format("{:s} x{:d} more in the last second, last at {:d},{:d}{:s}", key, rate.skipped, rate.lastX, rate.lastY, HeroTile()));
		rate.skipped = 0;
	}
}

} // namespace

void TelemetryEffect(std::string_view source, std::string_view what, int x, int y)
{
	if (LogFile == nullptr)
		return;
	const uint32_t now = Now();
	EffectRate &rate = EffectRates[StrCat(source, " ", what)];
	if (now - rate.windowStart >= 1000) {
		if (rate.skipped > 0)
			FlushEffects(now, false);
		rate.windowStart = now;
		rate.written = 0;
	}
	rate.lastX = x;
	rate.lastY = y;
	if (rate.written >= EffectLinesPerSecond) {
		rate.skipped++;
		return;
	}
	rate.written++;
	TelemetryLog("fx", StrCat(source, " ", what, " at ", x, ",", y, HeroTile()));
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
		// What each socketed gem does now and at its next level, to compare against the results later.
		GemLog(std::format("--- {:s}, level {:d} {:s}, {:s} ---", player._pName, player.getCharacterLevel(), player.getClassName(), loaded ? "loaded" : "new game"));
		std::string gems = SocketedGemsText(player);
		size_t start = 0;
		while (start < gems.size()) {
			const size_t end = std::min(gems.find('\n', start), gems.size());
			GemLog(std::string_view(gems).substr(start, end - start));
			start = end + 1;
		}
	}
}

void TelemetryGameEnded()
{
	if (LogFile == nullptr || GameStartMs == 0)
		return;
	EndLevelVisit();
	TelemetryLog("game", StrCat("ended. ", TelemetryStatsText()));
	TelemetryLog("damage", TelemetrySkillDamageText());
	WriteGemResults();
	GameStartMs = 0;
}

void TelemetryLevelEntered()
{
	EndLevelVisit();
	Engagements.clear();
	CurrentLevel.label = LevelLabel();
	CurrentLevel.startMs = Now();
	Count([](TelemetryCounts &counts) { counts.levelsEntered++; });
	CurrentLevel.monstersAtEntry = HostileMonsterCount();
	QuietStreakTicks = 0;
	InSwarm = false;
	TelemetryLog("level", std::format("entered, {:d} monsters", CurrentLevel.monstersAtEntry));
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
	// A new gem level unlocks: say what it would add to each socketed gem.
	const int unlocked = MaxGemLevelForCharacter(player.getCharacterLevel());
	if (unlocked == MaxGemLevelForCharacter(player.getCharacterLevel() - 1))
		return;
	for (size_t socket = 0; socket < player.poe2.skillSlots.size(); socket++) {
		const SkillSlot skill = player.poe2.skillSlots[socket];
		const int level = player.poe2.gemLevel(socket);
		if (skill.isEmpty() || level >= unlocked)
			continue;
		GemLog(std::format("{:s} can now go to gem level {:d} with a level {:d} uncut gem: {:s} {:s}", SkillName(skill), unlocked, unlocked, GemLevelText(skill, unlocked), GemStepText(skill, level, unlocked)));
	}
}

void TelemetryGemLevelChanged(const Player &player, SkillSlot skill, int fromLevel, int toLevel)
{
	if (&player != MyPlayer)
		return;
	GemLog(std::format("{:s} raised from gem level {:d} to {:d} at character level {:d}", SkillName(skill), fromLevel, toLevel, player.getCharacterLevel()));
	GemLog(StrCat("  before: ", GemLevelText(skill, fromLevel)));
	for (int level = fromLevel + 1; level <= toLevel; level++)
		GemLog(StrCat("  ", GemLevelText(skill, level), " ", GemStepText(skill, level - 1, level)));
	// The results so far at the old level, to compare with the new one.
	const auto it = std::find_if(DamageBySkill.begin(), DamageBySkill.end(), [&](const auto &entry) { return entry.first == std::format("{:s} gem {:d}", SkillName(skill), fromLevel); });
	if (it != DamageBySkill.end())
		GemLog(StrCat("  results at the old level: ", SkillDamageLine(it->first, it->second)));
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
	if (NotesFile == nullptr)
		return;
	// One line per note: date, session log, time into the session, where, and the note.
	const std::string line = StrCat(LocalStamp(), "\t", SessionName, "\t", Clock(Now() - SessionStartMs), "\t", LevelLabel(), "\t", text, "\n");
	std::fputs(line.c_str(), NotesFile);
	std::fflush(NotesFile);
}

void TelemetryStep(std::string_view step)
{
	TelemetryLog("load", step);
}

void TelemetryFrameDrawn()
{
	if (LogFile == nullptr || GameStartMs == 0)
		return;
	static uint32_t lastFrameMs = 0;
	const uint32_t now = Now();
	const uint32_t gap = now - lastFrameMs;
	lastFrameMs = now;
	// Longer gaps are pauses, menus and loading screens; the stall line covers those.
	if (gap > 1000)
		return;
	Count([gap](TelemetryCounts &counts) {
		counts.frames++;
		counts.frameMs += gap;
		if (gap > 33)
			counts.slowFrames++;
		counts.worstFrameMs = std::max(counts.worstFrameMs, gap);
	});
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
	if (leveltype != DTYPE_TOWN)
		TrackDensity(player, now);
	TrackBuild(player, now);
	if (now - WindowStartMs >= TelemetryStatsIntervalMs)
		WriteStatsLine(now);
	FlushEffects(now, false);
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
	for (const auto &[name, damage] : sorted)
		StrAppend(text, " ", SkillDamageLine(name, damage), ";");
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
