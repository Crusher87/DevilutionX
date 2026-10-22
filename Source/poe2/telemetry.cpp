#include "poe2/telemetry.hpp"

#include <algorithm>
#include <array>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
#include "diablo.h"
#include "game_mode.hpp"
#include "items.h"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "options.h"
#include "pfile.h"
#include "player.h"
#include "poe2/currency.hpp"
#include "poe2/defenses.hpp"
#include "poe2/gem_levels.hpp"
#include "poe2/level_census.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/spirit.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/telemetry_events.hpp"
#include "poe2/tree_telemetry.hpp"
#include "poe2/waystones.hpp"
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
	/** Damage energy shield soaked; damageTakenRaw is what reached life. */
	int64_t shieldTakenRaw = 0;
	/** The lowest life the hero fell to, in percent of maximum life. */
	int lowestLifePercent = 100;
	int swings = 0;
	int evaded = 0;
	int blocked = 0;
	int dodged = 0;
	/** The hero's attack rolls against monsters, the misses, and the hit chances summed for an average. */
	int heroAttacks = 0;
	int heroMisses = 0;
	int64_t hitChanceSum = 0;
	/** The hero's hits that left a monster standing, and how many of them light stunned it. */
	int stunnableHits = 0;
	int lightStuns = 0;
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
	/** Monsters killed by the first hit they took. */
	int oneHitKills = 0;
	/** Fights, and fights that took the hero under half of life and energy shield. */
	int fights = 0;
	int hardFights = 0;
	int goldSpent = 0;
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

/** @brief Damage one kind of attacker did to the hero this game, life and energy shield together. */
struct AttackerDamage {
	int64_t raw = 0;
	int hits = 0;
	int64_t biggestRaw = 0;
};

/** @brief A monster the player is fighting: when it was first hit, and by what. */
struct Engagement {
	uint32_t firstHitMs = 0;
	std::string lastSource;
	int overkill = 0;
	int hits = 0;
};

TelemetryCounts Totals;
TelemetryCounts Window;
uint32_t GameStartMs = 0;
uint32_t WindowStartMs = 0;
uint32_t LastKillMs = 0;
uint32_t LastTickMs = 0;
uint32_t LastStallLogMs = 0;
/** The longest wait between drawn frames since the last game tick, to tell a freeze from a pause. */
uint32_t FrameGapSinceTick = 0;
uint32_t LastFrameMs = 0;

std::string PendingAttacker;
/** Energy shield soaked from the hit being applied (raw), added to it when it's counted. */
int32_t PendingShieldRaw = 0;
/** The last hit the hero took, for the death line. */
std::string LastHitText;
std::string PendingDamageSource;
std::string LastSkill;
uint32_t LastSkillMs = 0;
std::vector<std::pair<std::string, SkillDamage>> DamageBySkill;
std::vector<std::pair<std::string, AttackerDamage>> DamageByAttacker;
/** Stats lines written this game; every few, the damage summaries are written too, so a crash doesn't lose them. */
int StatsLinesWritten = 0;
ankerl::unordered_dense::map<size_t, Engagement> Engagements;

/** @brief The level being played: where, since when, and how it's going. */
struct LevelVisit {
	std::string label;
	uint32_t startMs = 0;
	int kills = 0;
	int drops = 0;
	int64_t damageTakenRaw = 0;
	int64_t shieldTakenRaw = 0;
	int monstersAtEntry = 0;
	/** The middle level of the monsters there at entry: campaign floors have no area level, so this stands in for it. */
	int monsterLevel = 0;
	int heroLevel = 0;
	int fights = 0;
	int hardFights = 0;
};
LevelVisit CurrentLevel;
/** When the last level was entered, for the time between areas. */
uint32_t LastAreaMs = 0;

/** @brief Ticks since an enemy was last near, and whether a swarm is in progress, for the lull and swarm events. */
int QuietStreakTicks = 0;
bool InSwarm = false;
uint32_t LastSwarmLogMs = 0;

std::string LastBuild;
uint32_t LastBuildCheckMs = 0;
uint32_t LastExperience = 0;
int LastGold = 0;

/** @brief An event kept in memory: written ones were in the readable log already, the rest only go out on a crash. */
struct RecentEvent {
	std::string text;
	bool written = false;
};
std::array<RecentEvent, TelemetryRecentEvents> RecentEvents;
size_t RecentNext = 0;
/** The folder of the play log, and this session's file stamp. */
std::string LogFolder;
std::string SessionStamp;
uint32_t LastFlushMs = 0;
/** Game ticks so far, to tie a damage breakdown to the tick its hit was worked out in. */
uint32_t TickCount = 0;

uint32_t Now()
{
	return static_cast<uint32_t>(SDL_GetTicks());
}

std::string Clock(uint32_t ms)
{
	const uint32_t seconds = ms / 1000;
	return std::format("{:02d}:{:02d}:{:02d}.{:03d}", seconds / 3600, (seconds / 60) % 60, seconds % 60, ms % 1000);
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

std::string LevelLabel()
{
	if (leveltype == DTYPE_TOWN && !setlevel)
		return "town";
	if (setlevel)
		return std::format("set{:d}", static_cast<int>(setlvlnum));
	// A map rebuilds a campaign level, so it's named by its tier and the level it rebuilds.
	if (const int tier = ActiveMapTier(); tier > 0)
		return std::format("map{:d}-L{:d}", tier, static_cast<int>(currlevel));
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

void KeepRecent(std::string text, bool written)
{
	RecentEvents[RecentNext] = { std::move(text), written };
	RecentNext = (RecentNext + 1) % RecentEvents.size();
}

/** @brief Writes both files to disk now. */
void FlushAll()
{
	if (LogFile == nullptr)
		return;
	const uint32_t start = Now();
	std::fflush(LogFile);
	AddTelemetryWriteMs(Now() - start);
	FlushTelemetryEvents();
	LastFlushMs = Now();
}

/**
 * @brief Writes a line to the readable log. Lines go to disk about once a second rather than one by one;
 * a crash writes what's waiting first, and so do the lines that matter most (force).
 */
void WriteLine(std::string_view line, bool force = false)
{
	if (LogFile == nullptr)
		return;
	std::fwrite(line.data(), 1, line.size(), LogFile);
	std::fputc('\n', LogFile);
	KeepRecent(std::string(line), true);
	if (force || Now() - LastFlushMs >= TelemetryFlushMs)
		FlushAll();
}

/** @brief Keeps a detailed event in memory only; the latest are written out if the game crashes. */
void Remember(std::string_view category, std::string_view text)
{
	if (LogFile == nullptr)
		return;
	KeepRecent(StrCat(Clock(Now() - SessionStartMs), " ", LevelLabel(), " [", category, "] ", text), false);
}

/** @brief The events kept in memory, oldest first: only those not in the log yet, or all of them for a crash report. */
void WriteRecentEvents(FILE *file, bool all)
{
	if (file == nullptr)
		return;
	std::fputs(all ? "Latest events, oldest first:\n" : "Latest detailed events, oldest first:\n", file);
	for (size_t i = 0; i < RecentEvents.size(); i++) {
		const RecentEvent &event = RecentEvents[(RecentNext + i) % RecentEvents.size()];
		if (event.text.empty() || (event.written && !all))
			continue;
		std::fputs("  ", file);
		std::fputs(event.text.c_str(), file);
		std::fputc('\n', file);
	}
	std::fflush(file);
}

void WriteRecentEvents()
{
	FlushAll();
	WriteRecentEvents(LogFile, false);
}

/** @brief Writes a line to the readable log only; the caller writes the event that goes with it. */
void WriteText(std::string_view category, std::string_view text, bool force = false)
{
	if (LogFile == nullptr)
		return;
	WriteLine(StrCat(Clock(Now() - SessionStartMs), " ", LevelLabel(), " [", category, "] ", text), force);
}

/** @brief Stamps the next events with the time and area, and makes one. */
TelemetryEvent Event(LogChannel channel, LogLevel level, std::string_view kind)
{
	if (LogLevelOn(channel, level))
		SetTelemetryEventClock(Now() - SessionStartMs, LevelLabel());
	return TelemetryEvent(channel, level, kind);
}

/** @brief Files in a folder whose names start with a prefix, oldest first (names hold the start time). */
std::vector<std::string> FilesStartingWith(const std::string &folder, std::string_view prefix)
{
	std::vector<std::string> files;
	for (const std::string &file : ListFiles(folder.c_str())) {
		if (file.rfind(prefix, 0) == 0)
			files.push_back(file);
	}
	std::sort(files.begin(), files.end());
	return files;
}

/** @brief The session a file belongs to: its name up to the extension, so a session's .log and .events go together. */
std::string_view SessionOf(std::string_view file)
{
	return file.substr(0, file.find('.'));
}

/**
 * @brief Keeps the latest sessions in the folder and moves older ones to old/, which keeps the most recent
 * MaxTelemetryOldFiles files; history.tsv keeps a line for every session forever.
 */
void PruneOldSessions(const std::string &folder)
{
	const std::vector<std::string> files = FilesStartingWith(folder, "session_");
	std::vector<std::string_view> sessions;
	for (const std::string &file : files) {
		if (sessions.empty() || sessions.back() != SessionOf(file))
			sessions.push_back(SessionOf(file));
	}
	if (sessions.size() < MaxTelemetrySessions)
		return;
	const std::string oldFolder = StrCat(folder, "old", DIRECTORY_SEPARATOR_STR);
	RecursivelyCreateDir(oldFolder.c_str());
	const size_t extra = sessions.size() - (MaxTelemetrySessions - 1);
	for (const std::string &file : files) {
		if (SessionOf(file) < sessions[extra])
			RenameFile(StrCat(folder, file).c_str(), StrCat(oldFolder, file).c_str());
	}
	const std::vector<std::string> old = FilesStartingWith(oldFolder, "session_");
	for (size_t i = 0; i + MaxTelemetryOldFiles < old.size(); i++)
		RemoveFile(StrCat(oldFolder, old[i]).c_str());
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

/** @brief What the game was doing: drawing something (TelemetryDrawStep) or a step of the game logic. */
const char *DrawStep = nullptr;

std::string CrashContext()
{
	const char *step = nullptr;
	switch (gGameLogicStep) {
	case GameLogicStep::ProcessPlayers: step = "updating heroes"; break;
	case GameLogicStep::ProcessMonsters: step = "updating monsters"; break;
	case GameLogicStep::ProcessObjects: step = "updating objects"; break;
	case GameLogicStep::ProcessMissiles:
	case GameLogicStep::ProcessMissilesTown: step = "updating missiles"; break;
	case GameLogicStep::ProcessItems:
	case GameLogicStep::ProcessItemsTown: step = "updating items"; break;
	case GameLogicStep::ProcessTowners: step = "updating townsfolk"; break;
	default: step = DrawStep; break;
	}
	return step != nullptr ? StrCat(" while ", step) : std::string();
}

/** @brief Copies a file into the crash report, when it's there; saves kept as folders are left out. */
void CopyIntoCrashReport(const std::string &from, const std::string &to)
{
	if (from.empty() || !FileExists(from) || DirectoryExists(from))
		return;
	CopyFileOverwrite(from.c_str(), to.c_str());
}

/**
 * @brief Writes a crash report to poe2_logs/crashes: what happened, where (level, seed, build), the hero, the latest
 * events, and copies of this session's log, its events and the last save of the hero and stash, so the moment can be
 * loaded again. Only the latest MaxTelemetryCrashReports are kept.
 */
void WriteCrashReport(std::string_view what)
{
	static bool written = false;
	if (written || LogFile == nullptr || LogFolder.empty())
		return;
	written = true;
	FlushAll();
	const std::string folder = StrCat(LogFolder, "crashes", DIRECTORY_SEPARATOR_STR);
	RecursivelyCreateDir(folder.c_str());
	// Older reports go first, five files each.
	const std::vector<std::string> reports = FilesStartingWith(folder, "crash_");
	std::vector<std::string_view> stamps;
	for (const std::string &file : reports) {
		if (stamps.empty() || stamps.back() != SessionOf(file).substr(0, 21))
			stamps.push_back(SessionOf(file).substr(0, 21));
	}
	if (stamps.size() >= MaxTelemetryCrashReports) {
		const std::string_view keepFrom = stamps[stamps.size() - (MaxTelemetryCrashReports - 1)];
		for (const std::string &file : reports) {
			if (std::string_view(file).substr(0, 21) < keepFrom)
				RemoveFile(StrCat(folder, file).c_str());
		}
	}
	const std::string base = StrCat(folder, "crash_", SessionStamp.substr(0, 15));
	// The copies come first: if the hero's data is what broke, describing it below could fail too.
	CopyIntoCrashReport(StrCat(LogFolder, SessionName), StrCat(base, ".log"));
	CopyIntoCrashReport(StrCat(LogFolder, SessionOf(SessionName), ".events"), StrCat(base, ".events"));
	if (MyPlayer != nullptr) {
		CopyIntoCrashReport(pfile_hero_save_path(), StrCat(base, "_hero.sv"));
		CopyIntoCrashReport(pfile_stash_save_path(), StrCat(base, "_stash.sv"));
	}
	FILE *report = OpenFile(StrCat(base, ".txt").c_str(), "wb");
	if (report != nullptr) {
		std::fprintf(report, "POE2 Mashup crash report, %s %s, %s\n", PROJECT_NAME, PROJECT_VERSION, LocalStamp().c_str());
		std::fprintf(report, "What happened: %.*s\n", static_cast<int>(what.size()), what.data());
		std::fprintf(report, "Where: %s (dungeon level %d, level type %d, level seed %u), %.1f min into the session\n", LevelLabel().c_str(), static_cast<int>(currlevel),
		    static_cast<int>(leveltype), currlevel < NUMLEVELS ? DungeonSeeds[currlevel] : 0U, (Now() - SessionStartMs) / 60000.0);
		std::fprintf(report, "Session log: %s (copy beside this file)\n", SessionName.c_str());
		std::fputs("The hero and stash saves beside this file are the last ones written before the crash.\n\n", report);
		WriteRecentEvents(report, true);
		std::fflush(report);
		if (!LastBuild.empty())
			std::fprintf(report, "\nLast build: %s\n", LastBuild.c_str());
		std::fclose(report);
	}
	TelemetryLog("crash", StrCat("crash report written to crashes", DIRECTORY_SEPARATOR_STR, "crash_", SessionStamp.substr(0, 15), ".txt"));
	FlushAll();
}

void HandleCrashSignal(int signal)
{
	if (LogFile != nullptr) {
		std::fprintf(LogFile, "%s %s [crash] signal %d%s\n", Clock(Now() - SessionStartMs).c_str(), LevelLabel().c_str(), signal, CrashContext().c_str());
		WriteRecentEvents();
		WriteCrashReport(StrCat("signal ", signal, CrashContext()));
	}
	std::signal(signal, SIG_DFL);
	std::raise(signal);
}

#if defined(_WIN32) && (defined(__x86_64__) || defined(_M_X64))
/** @brief Function starts from the build's link map (devilutionx.map beside the exe), to name call stack frames. */
std::vector<std::pair<uint64_t, std::string>> MapSymbols;
uint64_t MapImageBase = 0;
bool MapSymbolsRead = false;

void ReadMapSymbols()
{
	if (MapSymbolsRead)
		return;
	MapSymbolsRead = true;
	wchar_t exePath[MAX_PATH];
	const DWORD length = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		return;
	std::wstring mapPath(exePath, length);
	const size_t dot = mapPath.rfind(L'.');
	if (dot == std::wstring::npos)
		return;
	mapPath.replace(dot, std::wstring::npos, L".map");
	FILE *file = _wfopen(mapPath.c_str(), L"r");
	if (file == nullptr)
		return;
	// A symbol line is 16 spaces, the address, 16 spaces and the name; "name = value" lines are assignments.
	constexpr char Gap[] = "                ";
	char line[1024];
	while (std::fgets(line, sizeof(line), file) != nullptr) {
		if (std::strncmp(line, Gap, 16) != 0 || line[16] != '0' || line[17] != 'x')
			continue;
		char *end = nullptr;
		const uint64_t address = std::strtoull(line + 16, &end, 16);
		if (end == nullptr || std::strncmp(end, Gap, 16) != 0)
			continue;
		std::string name(end + 16);
		while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
			name.pop_back();
		if (name.rfind("__ImageBase = ", 0) == 0)
			MapImageBase = address;
		if (name.empty() || name.find(" = ") != std::string::npos || address < 0x1000)
			continue;
		MapSymbols.emplace_back(address, std::move(name));
	}
	std::fclose(file);
	std::sort(MapSymbols.begin(), MapSymbols.end());
}

/** @brief Where an address is: an offset into the exe, and the function before it in the link map when there is one. */
std::string DescribeAddress(uintptr_t address, bool withName)
{
	const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
	const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
	const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
	char text[48];
	if (address < base || address >= base + nt->OptionalHeader.SizeOfImage) {
		std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(address));
		return text;
	}
	std::snprintf(text, sizeof(text), "exe+0x%llx", static_cast<unsigned long long>(address - base));
	if (!withName)
		return text;
	ReadMapSymbols();
	if (MapSymbols.empty() || MapImageBase == 0)
		return text;
	const uint64_t mapped = MapImageBase + (address - base);
	auto after = std::upper_bound(MapSymbols.begin(), MapSymbols.end(), mapped, [](uint64_t value, const auto &symbol) { return value < symbol.first; });
	if (after == MapSymbols.begin())
		return text;
	const auto &symbol = *(after - 1);
	// Functions local to a file aren't in the map, so the name is the nearest one before: "in or after" it.
	char offset[24];
	std::snprintf(offset, sizeof(offset), "+0x%llx", static_cast<unsigned long long>(mapped - symbol.first));
	return StrCat(text, " (", symbol.second, offset, ")");
}

/** @brief Walks the stack from a frame with the exe's own unwind tables and writes it: offsets first, then names. */
void WriteCallStack(const CONTEXT &start)
{
	std::vector<uintptr_t> frames;
	CONTEXT context = start;
	for (int frame = 0; frame < 48 && context.Rip != 0; frame++) {
		frames.push_back(static_cast<uintptr_t>(context.Rip));
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
	std::fputs("Call stack:", LogFile);
	for (const uintptr_t address : frames)
		std::fprintf(LogFile, " %s", DescribeAddress(address, false).c_str());
	std::fputc('\n', LogFile);
	std::fflush(LogFile);
	// Naming the frames reads the link map; it comes second so the offsets are already safe in the log.
	std::fputs("Call stack with names (innermost first):\n", LogFile);
	for (const uintptr_t address : frames)
		std::fprintf(LogFile, "  %s\n", DescribeAddress(address, true).c_str());
	std::fflush(LogFile);
}

/**
 * @brief Windows ends the game on an access violation before std::signal sees it in some cases, so the
 * crash is caught here first: the exception, where it happened, what the game was doing and the call stack.
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
	const auto address = reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress);
	std::fprintf(LogFile, "%s %s [crash] exception 0x%08lx at %s%s", Clock(Now() - SessionStartMs).c_str(), LevelLabel().c_str(), static_cast<unsigned long>(code), DescribeAddress(address, false).c_str(), CrashContext().c_str());
	if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2) {
		const ULONG_PTR kind = info->ExceptionRecord->ExceptionInformation[0];
		std::fprintf(LogFile, " (%s 0x%llx)", kind == 0 ? "reading" : kind == 1 ? "writing" : "executing", static_cast<unsigned long long>(info->ExceptionRecord->ExceptionInformation[1]));
	}
	std::fprintf(LogFile, ", image base 0x%llx\n", static_cast<unsigned long long>(base));
	std::fflush(LogFile);
	WriteRecentEvents();
	// With the stack gone there's no room to write a report; the log has the crash.
	if (code != EXCEPTION_STACK_OVERFLOW) {
		WriteCallStack(*info->ContextRecord);
		char text[64];
		std::snprintf(text, sizeof(text), "exception 0x%08lx at %s", static_cast<unsigned long>(code), DescribeAddress(address, false).c_str());
		WriteCrashReport(StrCat(text, CrashContext()));
	}
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

/**
 * @brief An encounter: from the first monster near or aware of the hero to a few seconds with none, leaving the
 * level or dying. Pressure is how far down the hero's life and energy shield went, 100 for a death.
 */
struct Fight {
	bool active = false;
	uint32_t startMs = 0;
	uint32_t lastBusyMs = 0;
	ankerl::unordered_dense::set<size_t> monsters;
	ankerl::unordered_dense::set<size_t> reachedMelee;
	std::array<int, NumRarities> rarities {};
	int64_t monsterLevelSum = 0;
	int highestMonsterLevel = 0;
	int heroLevel = 0;
	int peakNear = 0;
	int lowestLifePercent = 100;
	int lowestPoolPercent = 100;
	int lowestManaPercent = 100;
	int64_t lifeLostRaw = 0;
	int64_t shieldLostRaw = 0;
	int64_t biggestHitRaw = 0;
	std::string biggestHitFrom;
	int hits = 0;
	int bigHits = 0;
	int kills = 0;
	int oneHitKills = 0;
	int flasks = 0;
	int skills = 0;
	int manaSpent = 0;
	bool died = false;
};
Fight CurrentFight;

/** @brief Life and energy shield together, in percent of their maximum. */
int PoolPercent(const Player &player)
{
	const int64_t max = static_cast<int64_t>(player._pMaxHP.raw()) + static_cast<int64_t>(player.poe2.maxEnergyShield) * 64;
	if (max <= 0)
		return 100;
	const int64_t now = std::max<int64_t>(player._pHitPoints.raw(), 0) + static_cast<int64_t>(player.poe2.energyShield);
	return static_cast<int>(std::clamp<int64_t>(now * 100 / max, 0, 100));
}

void StartFight(const Player &player, uint32_t now)
{
	CurrentFight = {};
	CurrentFight.active = true;
	CurrentFight.startMs = CurrentFight.lastBusyMs = now;
	CurrentFight.heroLevel = player.getCharacterLevel();
}

void EndFight(std::string_view how)
{
	Fight &fight = CurrentFight;
	if (!fight.active)
		return;
	fight.active = false;
	// A monster that wandered past without a blow on either side isn't a fight.
	if (fight.monsters.empty() || (fight.kills == 0 && fight.hits == 0 && fight.skills == 0 && !fight.died))
		return;
	const int pressure = fight.died ? 100 : 100 - fight.lowestPoolPercent;
	const bool hard = fight.died || fight.lowestPoolPercent < 50;
	Count([&](TelemetryCounts &counts) {
		counts.fights++;
		if (hard)
			counts.hardFights++;
	});
	CurrentLevel.fights++;
	if (hard)
		CurrentLevel.hardFights++;
	const int count = static_cast<int>(fight.monsters.size());
	const int averageLevel = static_cast<int>(fight.monsterLevelSum / count);
	const double seconds = (fight.lastBusyMs - fight.startMs) / 1000.0;
	WriteText("fight", std::format("{:d} monsters ({:s} normal/magic/rare/unique, level {:d} on average, hero {:d}), {:.1f} s, {:s}: "
	                               "lowest life {:d}%, life and energy shield {:d}%, mana {:d}%, lost {:d} life and {:d} energy shield in {:d} hits ({:d} big), "
	                               "{:d} reached melee, {:d} kills ({:d} with one hit), {:d} flasks, pressure {:d}",
	                       count, CountsByRarity(fight.rarities), averageLevel, fight.heroLevel, seconds, how, fight.lowestLifePercent, fight.lowestPoolPercent, fight.lowestManaPercent, Whole(fight.lifeLostRaw), Whole(fight.shieldLostRaw), fight.hits, fight.bigHits, static_cast<int>(fight.reachedMelee.size()), fight.kills, fight.oneHitKills, fight.flasks, pressure));
	Event(LogChannel::Fights, LogLevel::Always, "fight")
	    .add("p", MyPlayerId)
	    .add("monsters", count)
	    .add("normal", fight.rarities[0])
	    .add("magic", fight.rarities[1])
	    .add("rare", fight.rarities[2])
	    .add("unique", fight.rarities[3])
	    .add("monsterLevel", averageLevel)
	    .add("highestMonsterLevel", fight.highestMonsterLevel)
	    .add("areaMonsterLevel", CurrentLevel.monsterLevel)
	    .add("heroLevel", fight.heroLevel)
	    .add("seconds", seconds)
	    .add("end", how)
	    .add("lowestLife", fight.lowestLifePercent)
	    .add("lowestPool", fight.lowestPoolPercent)
	    .add("lowestMana", fight.lowestManaPercent)
	    .add("lifeLost", Whole(fight.lifeLostRaw))
	    .add("shieldLost", Whole(fight.shieldLostRaw))
	    .add("hits", fight.hits)
	    .add("bigHits", fight.bigHits)
	    .add("biggestHit", Whole(fight.biggestHitRaw))
	    .add("biggestHitFrom", fight.biggestHitFrom)
	    .add("reachedMelee", static_cast<int>(fight.reachedMelee.size()))
	    .add("peakNear", fight.peakNear)
	    .add("kills", fight.kills)
	    .add("oneHitKills", fight.oneHitKills)
	    .add("flasks", fight.flasks)
	    .add("skills", fight.skills)
	    .add("mana", fight.manaSpent)
	    .add("died", fight.died)
	    .add("pressure", pressure);
}

/** @brief Samples how many monsters are around the player this tick, and follows the fight. */
void TrackDensity(const Player &player, uint32_t now)
{
	int near = 0;
	int aware = 0;
	int adjacent = 0;
	Fight &fight = CurrentFight;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &monster = Monsters[ActiveMonsters[i]];
		if (!IsHostile(monster))
			continue;
		const int distance = player.position.tile.WalkingDistance(monster.position.tile);
		if (distance <= 1)
			adjacent++;
		if (distance <= TelemetryNearTiles)
			near++;
		const bool isAware = distance <= TelemetryAwareTiles && monster.activeForTicks > 0;
		if (isAware)
			aware++;
		if (!isAware && distance > TelemetryNearTiles)
			continue;
		if (!fight.active)
			StartFight(player, now);
		if (fight.monsters.insert(monster.getId()).second) {
			fight.rarities[MonsterRarityIndex(monster)]++;
			const int level = monster.level(sgGameInitInfo.nDifficulty);
			fight.monsterLevelSum += level;
			fight.highestMonsterLevel = std::max(fight.highestMonsterLevel, level);
		}
		if (distance <= 1)
			fight.reachedMelee.insert(monster.getId());
	}
	if (fight.active) {
		if (near > 0 || aware > 0)
			fight.lastBusyMs = now;
		fight.peakNear = std::max(fight.peakNear, near);
		fight.lowestLifePercent = std::min(fight.lowestLifePercent, std::max(Percent(player._pHitPoints, player._pMaxHP), 0));
		fight.lowestPoolPercent = std::min(fight.lowestPoolPercent, PoolPercent(player));
		if (player._pMaxMana > 0)
			fight.lowestManaPercent = std::min(fight.lowestManaPercent, std::max(Percent(player._pMana, player._pMaxMana), 0));
		if (now - fight.lastBusyMs >= TelemetryFightEndTicks * 1000 / TelemetryTicksPerSecond)
			EndFight("no enemy near for 5 s");
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
		if (QuietStreakTicks >= TelemetryLullTicks) {
			const double seconds = QuietStreakTicks / static_cast<double>(TelemetryTicksPerSecond);
			WriteText("lull", std::format("{:.1f} s with no enemy near, ended by {:d} monsters", seconds, std::max(near, aware)));
			Event(LogChannel::Combat, LogLevel::Normal, "lull").add("seconds", seconds).add("monsters", std::max(near, aware));
		}
		QuietStreakTicks = 0;
	}

	if (near >= TelemetrySwarmCount && !InSwarm) {
		InSwarm = true;
		Count([](TelemetryCounts &counts) { counts.swarms++; });
		if (now - LastSwarmLogMs >= 10000) {
			LastSwarmLogMs = now;
			WriteText("swarm", std::format("{:d} monsters within {:d} tiles, {:d} next to the player, life {:d}%", near, TelemetryNearTiles, adjacent, Percent(player._pHitPoints, player._pMaxHP)));
			Event(LogChannel::Combat, LogLevel::Normal, "swarm").add("near", near).add("adjacent", adjacent).add("life", Percent(player._pHitPoints, player._pMaxHP));
		}
	} else if (near < TelemetrySwarmCount / 2) {
		InSwarm = false;
	}
}

std::string RatesText(const TelemetryCounts &counts, double minutes)
{
	const int lowManaPercent = counts.ticks > 0 ? counts.lowManaTicks * 100 / counts.ticks : 0;
	return StrCat(DensityText(counts), "; ", std::format("kills {:d} ({:.1f}/min, {:d} rare, {:d} unique), damage dealt {:d} ({:.0f}/min), "
	                                                     "hits taken {:d} ({:d} big, {:d} close calls, {:d} damage, {:d} to energy shield, lowest life {:d}%), "
	                                                     "melee swings {:d} ({:d} evaded, {:d} blocked, {:d} dodged), "
	                                                     "your attacks {:d} ({:d} missed, {:d}% to hit on average), "
	                                                     "light stuns {:d} of {:d} hits, "
	                                                     "skills {:d} ({:d} failed, {:d} mana, low on mana {:d}% of the time), "
	                                                     "flasks {:d} ({:d} wasted), charms {:d}, "
	                                                     "xp {:d} ({:.0f}/min), gold {:d} ({:.0f}/min), "
	                                                     "drops {:s} picked up {:s} (normal/magic/rare/unique), crafts {:d}, stalls {:d}, "
	                                                     "fps {:.0f} ({:d} frames under 30 fps, slowest {:d} ms)",
	                                             counts.kills, counts.kills / minutes, counts.rareKills, counts.uniqueKills, Whole(counts.damageDealtRaw), Whole(counts.damageDealtRaw) / minutes, counts.hitsTaken, counts.bigHits, counts.closeCalls, Whole(counts.damageTakenRaw), Whole(counts.shieldTakenRaw), counts.lowestLifePercent, counts.swings, counts.evaded, counts.blocked, counts.dodged, counts.heroAttacks, counts.heroMisses, counts.heroAttacks > 0 ? static_cast<int>(counts.hitChanceSum / counts.heroAttacks) : 0, counts.lightStuns, counts.stunnableHits, counts.skillUses, counts.failedSkills, counts.manaSpent, lowManaPercent, counts.flaskUses, counts.wastedFlasks, counts.charmUses, counts.experience, counts.experience / minutes, counts.gold, counts.gold / minutes, CountsByRarity(counts.drops), CountsByRarity(counts.pickups), counts.crafts, counts.stalls,
	                                             counts.frameMs > 0 ? counts.frames * 1000.0 / counts.frameMs : 0.0, counts.slowFrames, counts.worstFrameMs));
}

void WriteStatsLine(uint32_t now)
{
	const double minutes = std::max<uint32_t>(now - WindowStartMs, 1) / 60000.0;
	WriteText("stats", std::format("last {:.1f} min: {:s}", minutes, RatesText(Window, minutes)));
	Event(LogChannel::Perf, LogLevel::Normal, "stats")
	    .add("minutes", minutes)
	    .add("fps", Window.frameMs > 0 ? Window.frames * 1000.0 / Window.frameMs : 0.0)
	    .add("slowFrames", Window.slowFrames)
	    .add("worstFrameMs", Window.worstFrameMs)
	    .add("stalls", Window.stalls)
	    .add("kills", Window.kills)
	    .add("hitsTaken", Window.hitsTaken)
	    .add("lowManaPercent", Window.ticks > 0 ? Window.lowManaTicks * 100 / Window.ticks : 0)
	    .add("nearAverage", Window.densityTicks > 0 ? static_cast<double>(Window.nearSum) / Window.densityTicks : 0.0)
	    .add("quietPercent", Window.densityTicks > 0 ? Window.quietTicks * 100 / Window.densityTicks : 0)
	    .add("logBytes", TelemetryEventBytes())
	    .add("logWriteMs", TelemetryWriteMs());
	Window = {};
	WindowStartMs = now;
	if (++StatsLinesWritten % TelemetrySummaryEvery == 0) {
		WriteText("damage", TelemetrySkillDamageText());
		WriteText("taken", TelemetryDamageTakenText());
	}
}

/** @brief Items still lying on the level, by rarity: what the hero left behind. */
std::array<int, NumRarities> ItemsLeftOnFloor()
{
	std::array<int, NumRarities> left {};
	for (uint8_t i = 0; i < ActiveItemCount; i++) {
		const Item &item = Items[ActiveItems[i]];
		if (!item.isEmpty() && item._itype != ItemType::Gold)
			left[ItemRarityIndex(item)]++;
	}
	return left;
}

void EndLevelVisit(std::string_view how)
{
	if (CurrentLevel.startMs == 0)
		return;
	EndFight(how);
	const double seconds = (Now() - CurrentLevel.startMs) / 1000.0;
	const int cleared = CurrentLevel.monstersAtEntry > 0 ? CurrentLevel.kills * 100 / CurrentLevel.monstersAtEntry : 0;
	const std::array<int, NumRarities> left = ItemsLeftOnFloor();
	WriteLine(StrCat(Clock(Now() - SessionStartMs), " ", CurrentLevel.label, " [level] left after ",
	    std::format("{:.1f} s: {:d} kills of {:d} monsters ({:d}% cleared), {:d} drops, {:d} damage taken, {:d} to energy shield, {:d} fights ({:d} under half life), items left {:s}", seconds, CurrentLevel.kills,
	        CurrentLevel.monstersAtEntry, cleared, CurrentLevel.drops, Whole(CurrentLevel.damageTakenRaw), Whole(CurrentLevel.shieldTakenRaw),
	        CurrentLevel.fights, CurrentLevel.hardFights, CountsByRarity(left))));
	// Stamped with the level being left, not the one being loaded.
	if (LogLevelOn(LogChannel::Progress, LogLevel::Always))
		SetTelemetryEventClock(Now() - SessionStartMs, CurrentLevel.label);
	LevelCensusLeft(how);
	TelemetryEvent(LogChannel::Progress, LogLevel::Always, "area_left")
	    .add("p", MyPlayerId)
	    .add("seconds", seconds)
	    .add("kills", CurrentLevel.kills)
	    .add("monsters", CurrentLevel.monstersAtEntry)
	    .add("cleared", cleared)
	    .add("drops", CurrentLevel.drops)
	    .add("damageTaken", Whole(CurrentLevel.damageTakenRaw))
	    .add("shieldTaken", Whole(CurrentLevel.shieldTakenRaw))
	    .add("fights", CurrentLevel.fights)
	    .add("hardFights", CurrentLevel.hardFights)
	    .add("leftNormal", left[0])
	    .add("leftMagic", left[1])
	    .add("leftRare", left[2])
	    .add("leftUnique", left[3]);
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
	WriteText("settings", std::format("resolution {:d}x{:d}, {:s}, zoom {:s}, game speed {:d} ticks/s, difficulty {:d}", resolution.width, resolution.height, *options.Graphics.fullscreen ? "fullscreen" : "windowed", *options.Graphics.zoom ? "on" : "off", *options.Gameplay.tickRate, static_cast<int>(sgGameInitInfo.nDifficulty)));
	Event(LogChannel::Session, LogLevel::Always, "settings")
	    .add("width", resolution.width)
	    .add("height", resolution.height)
	    .add("fullscreen", *options.Graphics.fullscreen)
	    .add("zoom", *options.Graphics.zoom)
	    .add("tickRate", *options.Gameplay.tickRate)
	    .add("difficulty", static_cast<int>(sgGameInitInfo.nDifficulty));
}

/** @brief The build as data: the same as the build line, field by field. */
void WriteBuildEvent(const Player &player, LogLevel level)
{
	TelemetryEvent event = Event(LogChannel::Progress, level, "build");
	if (!event)
		return;
	JsonArray gear;
	for (const Item &item : player.InvBody) {
		if (!item.isEmpty())
			gear.add(item._iIName);
	}
	JsonArray skills;
	for (size_t i = 0; i < player.poe2.skillSlots.size(); i++) {
		const SkillSlot slot = player.poe2.skillSlots[i];
		if (slot.isEmpty())
			continue;
		std::string skill = "{\"name\":";
		AppendJsonString(skill, SkillName(slot));
		StrAppend(skill, ",\"gem\":", player.poe2.gemLevel(i));
		JsonArray supports;
		for (const SupportGem support : player.poe2.slotSupports[i]) {
			if (support != SupportGem::None)
				supports.add(SupportGemName(support));
		}
		JsonArray inside;
		for (size_t socket = 0; socket < MaxSupportSockets; socket++) {
			if (const SkillSlot spell = player.poe2.metaSkills[i][socket]; !spell.isEmpty())
				inside.add(StrCat(SkillName(spell), " gem ", static_cast<int>(player.poe2.metaSkillLevels[i][socket])));
		}
		StrAppend(skill, ",\"supports\":", supports.str(), ",\"inside\":", inside.str(), "}");
		skills.addRaw(skill);
	}
	JsonArray belt;
	for (const Item &item : player.SpdList) {
		if (!item.isEmpty())
			belt.add(item._iIName);
	}
	event.add("p", player.getId())
	    .add("heroLevel", player.getCharacterLevel())
	    .add("class", player.getClassName())
	    .add("xp", player._pExperience)
	    .add("life", player._pMaxHP.whole())
	    .add("mana", player._pMaxMana.whole())
	    .add("energyShield", player.poe2.maxEnergyShield)
	    .add("str", player._pStrength)
	    .add("mag", player._pMagic)
	    .add("dex", player._pDexterity)
	    .add("vit", player._pVitality)
	    .add("armour", ArmourRating(player, false))
	    .add("evasion", EvasionRating(player, false))
	    .add("block", std::clamp(player.GetBlockChance(false), 0, player.GetMaxBlockChance(gbIsPoe2)))
	    .add("fireRes", player._pFireResist)
	    .add("lightningRes", player._pLghtResist)
	    .add("magicRes", player._pMagResist)
	    .add("spirit", ReservedSpirit(player))
	    .add("maxSpirit", MaxSpirit(player))
	    .add("passives", player.poe2.passives.count())
	    .addRaw("gear", gear.str())
	    .addRaw("skills", skills.str())
	    .addRaw("belt", belt.str());
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
	WriteText("build", LastBuild);
	WriteBuildEvent(player, LogLevel::Normal);
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
	} else if (player._pGold < LastGold) {
		// Spent in a shop, or put in the stash.
		const int spent = LastGold - player._pGold;
		Count([&](TelemetryCounts &counts) { counts.goldSpent += spent; });
		Event(LogChannel::Loot, LogLevel::Normal, "gold_out").add("p", player.getId()).add("amount", spent).add("left", player._pGold);
	}
	LastGold = player._pGold;
}

void TrackStalls(uint32_t now)
{
	const uint32_t gap = now - LastTickMs;
	LastTickMs = now;
	// A freeze in the game logic can be followed by a catch-up tick before any frame is drawn, so the time since the
	// last frame counts too.
	const uint32_t frameGap = std::max(std::exchange(FrameGapSinceTick, 0), now - LastFrameMs);
	// Longer gaps are pauses, menus and loading screens rather than stutter.
	if (gap < TelemetryStallMs || gap > 5000)
		return;
	// The game waits while a menu is open, but frames keep coming: that's a pause, not a freeze.
	if (frameGap < TelemetryStallMs)
		return;
	Count([](TelemetryCounts &counts) { counts.stalls++; });
	if (now - LastStallLogMs < 10000)
		return;
	LastStallLogMs = now;
	WriteText("stall", std::format("game froze for {:d} ms ({:d} monsters, {:d} missiles)", gap, static_cast<int>(ActiveMonsterCount), static_cast<int>(Missiles.size())));
	Event(LogChannel::Perf, LogLevel::Normal, "stall").add("ms", gap).add("monsters", static_cast<int>(ActiveMonsterCount)).add("missiles", static_cast<int>(Missiles.size()));
}

} // namespace

void TelemetryInit()
{
	if (Initialized)
		return;
	Initialized = true;
	SessionStartMs = Now();

	const std::string folder = StrCat(paths::PrefPath(), "poe2_logs", DIRECTORY_SEPARATOR_STR);
	LogFolder = folder;
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
	SessionStamp = stamp;
	SessionName = StrCat("session_", stamp, ".log");

	char started[32];
	std::strftime(started, sizeof(started), "%Y-%m-%d %H:%M:%S", &local);
	WriteLine(StrCat("POE2 Mashup play log, ", PROJECT_NAME, " ", PROJECT_VERSION, ", started ", started));
	WriteLine("Each line: time since start, level (town, L<n> or set<n>), [event], details.");
	// The same events as data, one JSON object per line, for the reports.
	std::string header = std::format("{{\"k\":\"header\",\"version\":{:d},\"game\":\"POE2 Mashup\",\"build\":", TelemetryEventsVersion);
	AppendJsonString(header, StrCat(PROJECT_NAME, " ", PROJECT_VERSION));
	StrAppend(header, ",\"started\":\"", started, "\",\"log\":");
	AppendJsonString(header, SessionName);
	header += '}';
	if (!OpenTelemetryEvents(StrCat(folder, "session_", stamp, ".events"), header))
		WriteLine("The event file couldn't be opened; reports will only have this log.");
	FlushAll();
	// Whatever is still waiting goes to disk when the game closes.
	std::atexit([] { FlushAll(); });

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

void TelemetryText(std::string_view category, std::string_view text)
{
	WriteText(category, text);
}

TelemetryEvent TelemetryEventNow(LogChannel channel, LogLevel level, std::string_view kind)
{
	return Event(channel, level, kind);
}

const std::string &TelemetryFolder()
{
	return LogFolder;
}

std::string_view TelemetrySessionName()
{
	return SessionName;
}

std::string TelemetryLocalStamp()
{
	return LocalStamp();
}

void TelemetryFlush()
{
	FlushAll();
}

namespace {

/** @brief The channel and loudness of a line that has no event of its own. */
std::pair<LogChannel, LogLevel> ChannelOf(std::string_view category)
{
	if (category == "engine" || category == "bug")
		return { LogChannel::Warnings, LogLevel::Always };
	if (category == "fx")
		return { LogChannel::Effects, LogLevel::Normal };
	if (category == "stall" || category == "stats")
		return { LogChannel::Perf, LogLevel::Normal };
	if (category == "hit")
		return { LogChannel::Damage, LogLevel::Normal };
	if (category == "crash" || category == "load")
		return { LogChannel::Session, LogLevel::Always };
	return { LogChannel::Session, LogLevel::Normal };
}

} // namespace

void TelemetryLog(std::string_view category, std::string_view text)
{
	if (LogFile == nullptr)
		return;
	const auto [channel, level] = ChannelOf(category);
	// Loading steps and crashes go to disk at once, so a freeze or crash while loading still shows the last step.
	WriteText(category, text, channel == LogChannel::Session && level == LogLevel::Always);
	Event(channel, level, category).add("text", text);
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

void TelemetryMissingSprite(std::string_view what)
{
	static ankerl::unordered_dense::set<std::string> reported;
	if (LogFile == nullptr || reported.size() >= 64 || !reported.emplace(what).second)
		return;
	TelemetryLog("bug", StrCat("nothing to draw for ", what, ", skipped it", HeroTile()));
}

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

namespace {

/** @brief The hero at the start of the game, for the session's line in history.tsv. */
struct GameStart {
	std::string hero;
	std::string heroClass;
	int level = 0;
	bool loaded = false;
};
GameStart StartOfGame;

/** @brief Damage over time on one monster from one source (an ignite, a bleed), summed into one line per burn. */
struct Burn {
	std::string monster;
	std::string source;
	uint32_t firstMs = 0;
	uint32_t lastMs = 0;
	int64_t raw = 0;
	int ticks = 0;
};
ankerl::unordered_dense::map<std::string, Burn> Burns;

bool IsDamageOverTime(std::string_view source)
{
	return source == "ignite" || source == "bleeding" || source == "poison";
}

/** @brief Writes the burns that have stopped ticking (or all of them). */
void FlushBurns(uint32_t now, bool all)
{
	for (auto it = Burns.begin(); it != Burns.end();) {
		const Burn &burn = it->second;
		if (!all && now - burn.lastMs < 1000) {
			++it;
			continue;
		}
		const double seconds = (burn.lastMs - burn.firstMs) / 1000.0;
		Remember("dot", std::format("{:s} dealt {:d} to {:s} over {:.1f} s ({:d} ticks)", burn.source, Whole(burn.raw), burn.monster, seconds, burn.ticks));
		Event(LogChannel::Combat, LogLevel::Normal, "dot")
		    .add("p", MyPlayerId)
		    .add("source", burn.source)
		    .add("monster", burn.monster)
		    .add("damage", Whole(burn.raw))
		    .add("seconds", seconds)
		    .add("ticks", burn.ticks);
		it = Burns.erase(it);
	}
}

/** @brief One step of a hit being worked out, for the deep damage breakdown. */
struct HitStep {
	const char *name;
	int64_t value;
	bool percent;
	/** Steps of one packet of a hit split by damage type: dropped once that packet is written. */
	bool perPacket;
};

/** @brief The hit being worked out, one each way: the hero's hit on a monster, and a hit on the hero. */
struct HitBreakdown {
	bool active = false;
	uint32_t tick = 0;
	std::string what;
	std::vector<HitStep> steps;
};
std::array<HitBreakdown, 2> Breakdowns;

HitBreakdown *CurrentBreakdown(bool incoming)
{
	HitBreakdown &breakdown = Breakdowns[incoming ? 1 : 0];
	// A hit is worked out within one game tick; anything older belongs to a hit that never landed.
	if (!breakdown.active || breakdown.tick != TickCount)
		return nullptr;
	return &breakdown;
}

std::string StepsJson(const HitBreakdown &breakdown)
{
	JsonArray steps;
	for (const HitStep &step : breakdown.steps) {
		std::string json = "{\"step\":";
		AppendJsonString(json, step.name);
		if (step.percent)
			StrAppend(json, ",\"percent\":", static_cast<int>(step.value), "}");
		else
			StrAppend(json, std::format(",\"damage\":{:.2f}}}", step.value / 64.0));
		steps.addRaw(json);
	}
	return steps.str();
}

} // namespace

void TelemetryHitStart(bool incoming, std::string_view what)
{
	if (!TelemetryDeepDamage())
		return;
	HitBreakdown &breakdown = Breakdowns[incoming ? 1 : 0];
	breakdown.active = true;
	breakdown.tick = TickCount;
	breakdown.what = what;
	breakdown.steps.clear();
}

void TelemetryHitStep(bool incoming, const char *step, int64_t value, bool percent, bool perPacket)
{
	if (!TelemetryDeepDamage())
		return;
	if (HitBreakdown *breakdown = CurrentBreakdown(incoming); breakdown != nullptr && breakdown->steps.size() < 64)
		breakdown->steps.push_back({ step, value, percent, perPacket });
}

bool TelemetryDeepDamage()
{
	return LogLevelOn(LogChannel::Damage, LogLevel::Deep);
}

std::string TelemetryLogLevelsText(std::string_view channel, std::string_view level)
{
	if (!TelemetryEnabled())
		return "The play log isn't open.";
	if (!channel.empty()) {
		const std::optional<LogLevel> newLevel = ParseLogLevel(level);
		if (!newLevel)
			return "Levels are off, always, normal and deep. Example: log(\"damage\", \"deep\")";
		if (channel == "all") {
			for (size_t i = 0; i < static_cast<size_t>(LogChannel::Count); i++)
				SetLogLevel(static_cast<LogChannel>(i), *newLevel);
		} else if (const std::optional<LogChannel> parsed = ParseLogChannel(channel); parsed) {
			SetLogLevel(*parsed, *newLevel);
		} else {
			return StrCat("No channel called ", channel, ". Type log() to see them.");
		}
		TelemetryLog("log", StrCat(channel, " set to ", level, " for this session"));
	}
	std::string text = "Play log channels (log(\"channel\", \"off/always/normal/deep\") changes one until the game closes):";
	for (size_t i = 0; i < static_cast<size_t>(LogChannel::Count); i++) {
		const auto each = static_cast<LogChannel>(i);
		StrAppend(text, "\n  ", LogChannelName(each), ": ", LogLevelName(GetLogLevel(each)));
	}
	return text;
}

void TelemetryGameStarted(const Player &player, bool loaded)
{
	Totals = {};
	Window = {};
	DamageBySkill.clear();
	DamageByAttacker.clear();
	StatsLinesWritten = 0;
	Engagements.clear();
	Burns.clear();
	CurrentFight = {};
	LastSkill.clear();
	PendingAttacker.clear();
	PendingShieldRaw = 0;
	LastHitText.clear();
	PendingDamageSource.clear();
	LastBuild.clear();
	GameStartMs = WindowStartMs = LastKillMs = LastTickMs = LastAreaMs = Now();
	LastBuildCheckMs = 0;
	LastExperience = player._pExperience;
	LastGold = player._pGold;
	StartOfGame = { player._pName, std::string(player.getClassName()), player.getCharacterLevel(), loaded };
	const std::string_view mode = gbIsPoe2 ? "POE2" : (gbIsHellfire ? "Hellfire" : "Diablo");
	WriteText("game", std::format("{:s} {:s}: level {:d} {:s}, life {:d}/{:d}, mana {:d}/{:d}, mode {:s}", loaded ? "loaded" : "new game", player._pName, player.getCharacterLevel(), player.getClassName(), player._pHitPoints.whole(), player._pMaxHP.whole(), player._pMana.whole(), player._pMaxMana.whole(), mode), true);
	Event(LogChannel::Session, LogLevel::Always, "game_start")
	    .add("p", player.getId())
	    .add("hero", player._pName)
	    .add("class", player.getClassName())
	    .add("heroLevel", player.getCharacterLevel())
	    .add("xp", player._pExperience)
	    .add("loaded", loaded)
	    .add("mode", mode);
	if (LogFile != nullptr) {
		WriteSettings();
		WriteBuildEvent(player, LogLevel::Always);
		TelemetryTreeSnapshot(player, "start");
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

namespace {

/** @brief One line per game in history.tsv, which is never pruned, to follow a character across sessions. */
void WriteHistoryLine(double minutes)
{
	const std::string path = StrCat(LogFolder, "history.tsv");
	const bool isNew = !FileExists(path);
	FILE *file = OpenFile(path.c_str(), "ab");
	if (file == nullptr)
		return;
	if (isNew)
		std::fputs("date\tsession\thero\tclass\tloaded\tminutes\tstart level\tend level\txp\tdeaths\tkills\trare kills\tunique kills\tone-hit kill %\tfights\tfights under half\t"
		           "hits taken\tdamage taken\tto energy shield\tlowest life %\tflasks\twasted flasks\tdrops normal/magic/rare/unique\tgold in\tgold spent\tstalls\tfps\n",
		    file);
	const int level = MyPlayer != nullptr ? MyPlayer->getCharacterLevel() : 0;
	const TelemetryCounts &t = Totals;
	const std::string line = std::format("{:s}\t{:s}\t{:s}\t{:s}\t{:s}\t{:.1f}\t{:d}\t{:d}\t{:d}\t{:d}\t{:d}\t{:d}\t{:d}\t{:d}\t{:d}\t{:d}\t{:d}\t{:d}\t{:d}\t{:d}\t{:d}\t{:d}\t{:s}\t{:d}\t{:d}\t{:d}\t{:.0f}\n",
	    LocalStamp(), SessionName, StartOfGame.hero, StartOfGame.heroClass, StartOfGame.loaded ? "yes" : "no", minutes, StartOfGame.level, level, t.experience, t.deaths,
	    t.kills, t.rareKills, t.uniqueKills, t.kills > 0 ? t.oneHitKills * 100 / t.kills : 0, t.fights, t.hardFights, t.hitsTaken, Whole(t.damageTakenRaw), Whole(t.shieldTakenRaw),
	    t.lowestLifePercent, t.flaskUses, t.wastedFlasks, CountsByRarity(t.drops), t.gold, t.goldSpent, t.stalls, t.frameMs > 0 ? t.frames * 1000.0 / t.frameMs : 0.0);
	std::fputs(line.c_str(), file);
	std::fclose(file);
}

/** @brief The game's totals as data, and each skill's and attacker's. */
void WriteSummaryEvents(double minutes)
{
	const TelemetryCounts &t = Totals;
	Event(LogChannel::Session, LogLevel::Always, "summary")
	    .add("p", MyPlayerId)
	    .add("minutes", minutes)
	    .add("heroLevel", MyPlayer != nullptr ? MyPlayer->getCharacterLevel() : 0)
	    .add("startLevel", StartOfGame.level)
	    .add("xp", t.experience)
	    .add("deaths", t.deaths)
	    .add("levelsEntered", t.levelsEntered)
	    .add("kills", t.kills)
	    .add("rareKills", t.rareKills)
	    .add("uniqueKills", t.uniqueKills)
	    .add("oneHitKills", t.oneHitKills)
	    .add("fights", t.fights)
	    .add("hardFights", t.hardFights)
	    .add("damageDealt", Whole(t.damageDealtRaw))
	    .add("hitsTaken", t.hitsTaken)
	    .add("bigHits", t.bigHits)
	    .add("closeCalls", t.closeCalls)
	    .add("damageTaken", Whole(t.damageTakenRaw))
	    .add("shieldTaken", Whole(t.shieldTakenRaw))
	    .add("lowestLife", t.lowestLifePercent)
	    .add("swings", t.swings)
	    .add("evaded", t.evaded)
	    .add("blocked", t.blocked)
	    .add("dodged", t.dodged)
	    .add("skillUses", t.skillUses)
	    .add("failedSkills", t.failedSkills)
	    .add("manaSpent", t.manaSpent)
	    .add("lowManaPercent", t.ticks > 0 ? t.lowManaTicks * 100 / t.ticks : 0)
	    .add("flasks", t.flaskUses)
	    .add("wastedFlasks", t.wastedFlasks)
	    .add("charms", t.charmUses)
	    .add("dropsNormal", t.drops[0])
	    .add("dropsMagic", t.drops[1])
	    .add("dropsRare", t.drops[2])
	    .add("dropsUnique", t.drops[3])
	    .add("pickups", Sum(t.pickups))
	    .add("crafts", t.crafts)
	    .add("goldIn", t.gold)
	    .add("goldSpent", t.goldSpent)
	    .add("stalls", t.stalls)
	    .add("fps", t.frameMs > 0 ? t.frames * 1000.0 / t.frameMs : 0.0)
	    .add("slowFrames", t.slowFrames)
	    .add("nearAverage", t.densityTicks > 0 ? static_cast<double>(t.nearSum) / t.densityTicks : 0.0)
	    .add("quietPercent", t.densityTicks > 0 ? t.quietTicks * 100 / t.densityTicks : 0)
	    .add("surroundedPercent", t.densityTicks > 0 ? t.surroundedTicks * 100 / t.densityTicks : 0)
	    .add("swarms", t.swarms)
	    .add("logBytes", TelemetryEventBytes())
	    .add("logWriteMs", TelemetryWriteMs());
	for (const auto &[name, damage] : DamageBySkill) {
		Event(LogChannel::Session, LogLevel::Always, "skill_summary")
		    .add("skill", name)
		    .add("damage", Whole(damage.raw))
		    .add("hits", damage.hits)
		    .add("kills", damage.kills)
		    .add("killSeconds", damage.kills > 0 ? damage.killMs / 1000.0 / damage.kills : 0.0)
		    .add("uses", damage.uses)
		    .add("mana", damage.mana)
		    .add("activeSeconds", damage.lastUseMs > damage.firstUseMs ? (damage.lastUseMs - damage.firstUseMs + TelemetrySkillCreditMs) / 1000.0 : 0.0);
	}
	for (const auto &[name, damage] : DamageByAttacker) {
		Event(LogChannel::Session, LogLevel::Always, "attacker_summary")
		    .add("attacker", name)
		    .add("damage", Whole(damage.raw))
		    .add("hits", damage.hits)
		    .add("biggest", Whole(damage.biggestRaw));
	}
}

} // namespace

void TelemetryGameEnded()
{
	if (LogFile == nullptr || GameStartMs == 0)
		return;
	FlushBurns(Now(), true);
	EndLevelVisit("game ended");
	const double minutes = std::max<uint32_t>(Now() - GameStartMs, 1) / 60000.0;
	WriteText("game", StrCat("ended. ", TelemetryStatsText()));
	WriteText("damage", TelemetrySkillDamageText());
	WriteText("taken", TelemetryDamageTakenText());
	WriteSummaryEvents(minutes);
	WriteHistoryLine(minutes);
	if (MyPlayer != nullptr)
		TelemetryTreeSnapshot(*MyPlayer, "end");
	WriteGemResults();
	FlushAll();
	GameStartMs = 0;
}

void TelemetryLevelEntered()
{
	EndLevelVisit("left the level");
	Engagements.clear();
	Burns.clear();
	const uint32_t now = Now();
	CurrentLevel.label = LevelLabel();
	CurrentLevel.startMs = now;
	Count([](TelemetryCounts &counts) { counts.levelsEntered++; });
	CurrentLevel.monstersAtEntry = HostileMonsterCount();
	// Campaign floors have no area level yet: the middle level of the monsters there stands in for it.
	std::vector<int> levels;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &monster = Monsters[ActiveMonsters[i]];
		if (IsHostile(monster))
			levels.push_back(monster.level(sgGameInitInfo.nDifficulty));
	}
	if (!levels.empty()) {
		std::nth_element(levels.begin(), levels.begin() + levels.size() / 2, levels.end());
		CurrentLevel.monsterLevel = levels[levels.size() / 2];
	}
	QuietStreakTicks = 0;
	InSwarm = false;
	const double sinceLast = (now - LastAreaMs) / 60000.0;
	LastAreaMs = now;
	WriteText("level", std::format("entered, {:d} monsters (level {:d} in the middle)", CurrentLevel.monstersAtEntry, CurrentLevel.monsterLevel));
	if (MyPlayer == nullptr)
		return;
	const Player &player = *MyPlayer;
	CurrentLevel.heroLevel = player.getCharacterLevel();
	const int tier = ActiveMapTier();
	Event(LogChannel::Progress, LogLevel::Always, "area")
	    .add("p", player.getId())
	    .add("monsters", CurrentLevel.monstersAtEntry)
	    .add("monsterLevel", CurrentLevel.monsterLevel)
	    .add("mapTier", tier)
	    .add("areaLevel", tier > 0 ? MapAreaLevelBase + tier : 0)
	    .add("dungeonLevel", static_cast<int>(currlevel))
	    .add("levelType", static_cast<int>(leveltype))
	    .add("heroLevel", player.getCharacterLevel())
	    .add("xp", player._pExperience)
	    .add("gameMinutes", (now - GameStartMs) / 60000.0)
	    .add("minutesSinceLastArea", sinceLast);
	WriteBuildEvent(player, LogLevel::Always);
	LevelCensusEntered();
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
	// Damage a source was named for (minions, ignite, heralds) isn't the hit being broken down.
	const bool namedSource = !PendingDamageSource.empty();
	const std::string source = CurrentDamageSource();
	// Only the damage that took life away counts; the rest is overkill.
	const int64_t dealt = std::min(damage.raw(), lifeBefore.raw());
	Count([&](TelemetryCounts &counts) { counts.damageDealtRaw += dealt; });
	SkillDamage &skill = DamageFor(source);
	skill.raw += dealt;
	skill.hits++;

	const uint32_t now = Now();
	Engagement &engagement = Engagements[monster.getId()];
	if (engagement.firstHitMs == 0)
		engagement.firstHitMs = now;
	engagement.lastSource = source;
	engagement.hits++;
	if (damage > lifeBefore)
		engagement.overkill = Whole(damage.raw() - lifeBefore.raw());

	// Damage over time ticks many times a second: it's summed into one line per burn.
	if (IsDamageOverTime(source)) {
		Burn &burn = Burns[StrCat(monster.getId(), " ", source)];
		if (burn.ticks == 0) {
			burn.monster = monster.name();
			burn.source = source;
			burn.firstMs = now;
		}
		burn.lastMs = now;
		burn.raw += dealt;
		burn.ticks++;
		return;
	}
	const int lifeLeft = std::max(Whole(lifeBefore.raw() - damage.raw()), 0);
	if (namedSource && TelemetryDeepDamage()) {
		// Steps worked out for a named source's damage belong to it, not to the next part of the hit.
		if (HitBreakdown *breakdown = CurrentBreakdown(false); breakdown != nullptr)
			std::erase_if(breakdown->steps, [](const HitStep &step) { return step.perPacket; });
	}
	Remember("dealt", std::format("{:d} to {:s} with {:s}, {:d} life left", damage.whole(), monster.name(), source, lifeLeft));
	TelemetryEvent event = Event(LogChannel::Damage, LogLevel::Deep, "dealt");
	if (!event)
		return;
	event.add("p", MyPlayerId)
	    .add("source", source)
	    .add("monster", monster.name())
	    .add("rarity", RarityNames[MonsterRarityIndex(monster)])
	    .add("monsterLevel", monster.level(sgGameInitInfo.nDifficulty))
	    .add("damage", damage.raw() / 64.0)
	    .add("lifeBefore", lifeBefore.whole())
	    .add("lifeLeft", lifeLeft);
	if (HitBreakdown *breakdown = CurrentBreakdown(false); breakdown != nullptr) {
		if (!namedSource)
			event.add("hit", breakdown->what).addRaw("steps", StepsJson(*breakdown));
		// The next packet of the same hit keeps the steps before the split.
		std::erase_if(breakdown->steps, [](const HitStep &step) { return step.perPacket; });
	}
}

void TelemetryMonsterKilled(const Monster &monster)
{
	if (LogFile == nullptr)
		return;
	if (monster.isPlayerMinion()) {
		WriteText("minion", std::format("{:s} died", monster.name()));
		Event(LogChannel::Combat, LogLevel::Normal, "minion_died").add("p", MyPlayerId).add("minion", monster.name());
		return;
	}
	const uint32_t now = Now();
	const size_t rarity = MonsterRarityIndex(monster);
	const int monsterLevel = monster.level(sgGameInitInfo.nDifficulty);
	CurrentLevel.kills++;
	LevelCensusKill(monster);

	std::string fight;
	std::string killer = "minion or another monster";
	double seconds = 0;
	int hits = 0;
	int overkill = 0;
	const auto it = Engagements.find(monster.getId());
	if (it != Engagements.end()) {
		const Engagement &engagement = it->second;
		SkillDamage &skill = DamageFor(engagement.lastSource);
		skill.kills++;
		skill.killMs += now - engagement.firstHitMs;
		killer = engagement.lastSource;
		seconds = (now - engagement.firstHitMs) / 1000.0;
		hits = engagement.hits;
		overkill = engagement.overkill;
		// The report script reads this wording: new parts go at the end.
		fight = std::format(", killed by {:s} {:.1f} s after the first hit, {:d} overkill, {:d} hits", killer, seconds, overkill, hits);
		Engagements.erase(it);
	} else {
		fight = ", killed by a minion or another monster";
	}
	const bool oneHit = hits == 1;
	Count([&](TelemetryCounts &counts) {
		counts.kills++;
		if (rarity == 2)
			counts.rareKills++;
		if (rarity == 3)
			counts.uniqueKills++;
		if (oneHit)
			counts.oneHitKills++;
	});
	if (CurrentFight.active) {
		CurrentFight.kills++;
		if (oneHit)
			CurrentFight.oneHitKills++;
	}
	const double sinceLast = (now - LastKillMs) / 1000.0;
	WriteText("kill", std::format("{:s} ({:s}, monster level {:d}){:s}, {:.1f} s since the last kill", monster.name(), RarityNames[rarity], monsterLevel, fight, sinceLast));
	Event(LogChannel::Combat, LogLevel::Normal, "kill")
	    .add("p", MyPlayerId)
	    .add("monster", monster.name())
	    .add("rarity", RarityNames[rarity])
	    .add("monsterLevel", monsterLevel)
	    .add("heroLevel", MyPlayer != nullptr ? MyPlayer->getCharacterLevel() : 0)
	    .add("killer", killer)
	    .add("seconds", seconds)
	    .add("hits", hits)
	    .add("oneHit", oneHit)
	    .add("overkill", overkill)
	    .add("sinceLastKill", sinceLast);
	LastKillMs = now;
}

void TelemetryShieldAbsorbed(const Player &player, Fixed26_6 absorbed)
{
	if (&player == MyPlayer)
		PendingShieldRaw += absorbed.raw();
}

void TelemetryMeleeSwing(const Player &player, SwingOutcome outcome)
{
	if (&player != MyPlayer || LogFile == nullptr)
		return;
	Count([outcome](TelemetryCounts &counts) {
		counts.swings++;
		switch (outcome) {
		case SwingOutcome::Landed: break;
		case SwingOutcome::Evaded: counts.evaded++; break;
		case SwingOutcome::Blocked: counts.blocked++; break;
		case SwingOutcome::Dodged: counts.dodged++; break;
		}
	});
}

void TelemetryHeroAttackRoll(const Player &player, int hitChance, bool hit)
{
	if (&player != MyPlayer || LogFile == nullptr)
		return;
	Count([hitChance, hit](TelemetryCounts &counts) {
		counts.heroAttacks++;
		counts.hitChanceSum += hitChance;
		if (!hit)
			counts.heroMisses++;
	});
}

void TelemetryMonsterHitByHero(const Player &player, bool lightStunned)
{
	if (&player != MyPlayer || LogFile == nullptr)
		return;
	Count([lightStunned](TelemetryCounts &counts) {
		counts.stunnableHits++;
		if (lightStunned)
			counts.lightStuns++;
	});
}

void TelemetryPlayerDamaged(const Player &player, DamageType damageType, Fixed26_6 damage)
{
	const std::string attacker = PendingAttacker.empty() ? "unknown" : std::exchange(PendingAttacker, {});
	const int32_t shield = std::exchange(PendingShieldRaw, 0);
	if (&player != MyPlayer || (damage <= 0 && shield <= 0) || LogFile == nullptr)
		return;
	damage = std::max(damage, Fixed26_6::fromInt(0));
	const Fixed26_6 max = player._pMaxHP;
	const Fixed26_6 after = player._pHitPoints;
	const Fixed26_6 before = after + damage;
	// A big hit is measured against life and energy shield together, the pool the hit came out of.
	const int64_t pool = static_cast<int64_t>(max.raw()) + static_cast<int64_t>(player.poe2.maxEnergyShield) * 64;
	const int64_t total = static_cast<int64_t>(damage.raw()) + shield;
	const bool big = pool > 0 && total * 100 >= pool * TelemetryBigHitPercent;
	const bool closeCall = after > 0 && Percent(after, max) < TelemetryCloseCallPercent && Percent(before, max) >= TelemetryCloseCallPercent;
	const int lifePercent = max > 0 ? std::max(Percent(after, max), 0) : 100;
	Count([&](TelemetryCounts &counts) {
		counts.hitsTaken++;
		counts.damageTakenRaw += damage.raw();
		counts.shieldTakenRaw += shield;
		counts.lowestLifePercent = std::min(counts.lowestLifePercent, lifePercent);
		if (big)
			counts.bigHits++;
		if (closeCall)
			counts.closeCalls++;
	});
	CurrentLevel.damageTakenRaw += damage.raw();
	CurrentLevel.shieldTakenRaw += shield;
	if (Fight &fight = CurrentFight; fight.active) {
		fight.hits++;
		fight.lifeLostRaw += damage.raw();
		fight.shieldLostRaw += shield;
		if (big)
			fight.bigHits++;
		if (total > fight.biggestHitRaw) {
			fight.biggestHitRaw = total;
			fight.biggestHitFrom = attacker;
		}
		fight.lowestLifePercent = std::min(fight.lowestLifePercent, lifePercent);
		fight.lowestPoolPercent = std::min(fight.lowestPoolPercent, PoolPercent(player));
	}
	auto byAttacker = std::find_if(DamageByAttacker.begin(), DamageByAttacker.end(), [&](const auto &entry) { return entry.first == attacker; });
	if (byAttacker == DamageByAttacker.end())
		byAttacker = DamageByAttacker.insert(DamageByAttacker.end(), { attacker, AttackerDamage {} });
	byAttacker->second.raw += total;
	byAttacker->second.hits++;
	byAttacker->second.biggestRaw = std::max(byAttacker->second.biggestRaw, total);
	std::string text = std::format("took {:d} {:s} damage from {:s}", Whole(total), DamageTypeName(damageType), attacker);
	if (shield > 0)
		StrAppend(text, std::format(" ({:d} to energy shield)", Whole(shield)));
	StrAppend(text, std::format(", life {:d}/{:d}", after.whole(), max.whole()));
	if (player.poe2.maxEnergyShield > 0)
		StrAppend(text, std::format(", energy shield {:d}/{:d}", player.poe2.energyShield / 64, player.poe2.maxEnergyShield));
	LastHitText = text;
	if (closeCall)
		WriteText("close call", text);
	else if (big)
		WriteText("hit", text);
	else
		Remember("hit", text);
	TelemetryEvent event = Event(LogChannel::Combat, LogLevel::Normal, "hit_taken");
	event.add("p", player.getId())
	    .add("attacker", attacker)
	    .add("type", DamageTypeName(damageType))
	    .add("damage", total / 64.0)
	    .add("toShield", shield / 64.0)
	    .add("life", after.whole())
	    .add("maxLife", max.whole())
	    .add("shield", player.poe2.energyShield / 64)
	    .add("maxShield", player.poe2.maxEnergyShield)
	    .add("big", big)
	    .add("closeCall", closeCall);
	if (HitBreakdown *breakdown = CurrentBreakdown(true); breakdown != nullptr) {
		event.add("hit", breakdown->what).addRaw("steps", StepsJson(*breakdown));
		breakdown->active = false;
	}
}

void TelemetryPlayerDied(const Player &player)
{
	if (&player != MyPlayer)
		return;
	Count([](TelemetryCounts &counts) { counts.deaths++; });
	WriteText("death", std::format("died at character level {:d}{:s}", player.getCharacterLevel(), LastHitText.empty() ? "" : StrCat(", last hit: ", LastHitText)), true);
	Event(LogChannel::Session, LogLevel::Always, "death")
	    .add("p", player.getId())
	    .add("heroLevel", player.getCharacterLevel())
	    .add("lastHit", LastHitText);
	if (CurrentFight.active) {
		CurrentFight.died = true;
		EndFight("hero died");
	}
	WriteRecentEvents();
}

void TelemetryLevelUp(const Player &player)
{
	if (&player != MyPlayer)
		return;
	const double minutes = (Now() - GameStartMs) / 60000.0;
	WriteText("levelup", std::format("character level {:d}, life {:d}, mana {:d}, {:.1f} min into the game", player.getCharacterLevel(), player._pMaxHP.whole(), player._pMaxMana.whole(), minutes));
	Event(LogChannel::Progress, LogLevel::Always, "levelup")
	    .add("p", player.getId())
	    .add("heroLevel", player.getCharacterLevel())
	    .add("life", player._pMaxHP.whole())
	    .add("mana", player._pMaxMana.whole())
	    .add("gameMinutes", minutes)
	    .add("monsterLevel", CurrentLevel.monsterLevel);
	TelemetryTreeSnapshot(player, "levelup");
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
	Event(LogChannel::Skills, LogLevel::Always, "gem_level")
	    .add("p", player.getId())
	    .add("skill", SkillName(skill))
	    .add("from", fromLevel)
	    .add("to", toLevel)
	    .add("heroLevel", player.getCharacterLevel());
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
	if (CurrentFight.active) {
		CurrentFight.skills++;
		CurrentFight.manaSpent += manaCost;
	}
	if (LogFile != nullptr) {
		SkillDamage &stats = DamageFor(LastSkill);
		stats.uses++;
		stats.mana += manaCost;
		if (stats.firstUseMs == 0)
			stats.firstUseMs = LastSkillMs;
		stats.lastUseMs = LastSkillMs;
	}
	WriteText("skill", std::format("{:s}, {:d} mana, {:d}/{:d} left", LastSkill, manaCost, player._pMana.whole(), player._pMaxMana.whole()));
	Event(LogChannel::Skills, LogLevel::Normal, "skill")
	    .add("p", player.getId())
	    .add("skill", skill)
	    .add("gem", gemLevel)
	    .add("mana", manaCost)
	    .add("manaLeft", player._pMana.whole())
	    .add("maxMana", player._pMaxMana.whole());
}

void TelemetrySkillFailed(const Player &player, std::string_view skill, std::string_view reason)
{
	if (&player != MyPlayer)
		return;
	Count([](TelemetryCounts &counts) { counts.failedSkills++; });
	WriteText("skill failed", std::format("{:s}: {:s}, mana {:d}/{:d}", skill, reason, player._pMana.whole(), player._pMaxMana.whole()));
	Event(LogChannel::Skills, LogLevel::Normal, "skill_failed")
	    .add("p", player.getId())
	    .add("skill", skill)
	    .add("reason", reason)
	    .add("manaLeft", player._pMana.whole())
	    .add("maxMana", player._pMaxMana.whole());
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
	if (CurrentFight.active)
		CurrentFight.flasks++;
	WriteText("flask", std::format("{:s}, {:d} charges left, drunk at {:d}% life and {:d}% mana{:s}", flask._iIName, chargesLeft, lifePercent, manaPercent, wasted ? " (wasted)" : ""));
	Event(LogChannel::Combat, LogLevel::Normal, "flask")
	    .add("p", player.getId())
	    .add("flask", flask._iIName)
	    .add("life", life)
	    .add("mana", mana)
	    .add("chargesLeft", chargesLeft)
	    .add("lifePercent", lifePercent)
	    .add("manaPercent", manaPercent)
	    .add("wasted", wasted)
	    .add("inFight", CurrentFight.active);
}

void TelemetryCharmUsed(const Player &player, std::string_view charm)
{
	if (&player != MyPlayer)
		return;
	Count([](TelemetryCounts &counts) { counts.charmUses++; });
	WriteText("charm", std::format("{:s} went off, life {:d}/{:d}", charm, player._pHitPoints.whole(), player._pMaxHP.whole()));
	Event(LogChannel::Combat, LogLevel::Normal, "charm").add("p", player.getId()).add("charm", charm).add("life", player._pHitPoints.whole()).add("maxLife", player._pMaxHP.whole());
}

namespace {

/** @brief The fields every item event shares: name, base, rarity, item level, and whether it's currency. */
void AddItemFields(TelemetryEvent &event, const Item &item)
{
	event.add("item", item._iIName)
	    .add("base", item.IDidx >= 0 ? std::string_view(AllItemsList[item.IDidx].iName) : std::string_view())
	    .add("rarity", RarityNames[ItemRarityIndex(item)])
	    .add("itemLevel", item._iCreateInfo & CF_LEVEL)
	    .add("type", static_cast<int>(item._itype))
	    .add("currency", IsCurrencyMiscId(item._iMiscId));
}

} // namespace

void TelemetryItemDropped(const Item &item, std::string_view source)
{
	// Gold is counted as it's picked up, in the gold per minute.
	if (item.isEmpty() || item._itype == ItemType::Gold)
		return;
	const size_t rarity = ItemRarityIndex(item);
	Count([&](TelemetryCounts &counts) { counts.drops[rarity]++; });
	CurrentLevel.drops++;
	WriteText("drop", std::format("{:s} ({:s}, item level {:d}) from {:s}", item._iIName, RarityNames[rarity], item._iCreateInfo & CF_LEVEL, source));
	TelemetryEvent event = Event(LogChannel::Loot, LogLevel::Normal, "drop");
	if (!event)
		return;
	AddItemFields(event, item);
	event.add("source", source).add("monsterLevel", CurrentLevel.monsterLevel);
}

void TelemetryItemPickedUp(const Player &player, const Item &item)
{
	if (&player != MyPlayer || item.isEmpty() || item._itype == ItemType::Gold)
		return;
	const size_t rarity = ItemRarityIndex(item);
	Count([&](TelemetryCounts &counts) { counts.pickups[rarity]++; });
	WriteText("pickup", std::format("{:s} ({:s})", item._iIName, RarityNames[rarity]));
	TelemetryEvent event = Event(LogChannel::Loot, LogLevel::Normal, "pickup");
	if (!event)
		return;
	event.add("p", player.getId());
	AddItemFields(event, item);
}

void TelemetryCrafted(const Player &player, std::string_view currency, const Item &item)
{
	if (&player != MyPlayer)
		return;
	Count([](TelemetryCounts &counts) { counts.crafts++; });
	WriteText("craft", std::format("{:s} on {:s}, now {:s}", currency, item._iIName, RarityNames[ItemRarityIndex(item)]));
	TelemetryEvent event = Event(LogChannel::Loot, LogLevel::Normal, "craft");
	if (!event)
		return;
	event.add("p", player.getId()).add("with", currency);
	AddItemFields(event, item);
}

void TelemetryMark(std::string_view text)
{
	WriteText("mark", text, true);
	Event(LogChannel::Session, LogLevel::Always, "mark").add("text", text);
	FlushTelemetryEvents();
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
	DrawStep = nullptr;
	if (LogFile == nullptr || GameStartMs == 0)
		return;
	const uint32_t now = Now();
	const uint32_t gap = now - LastFrameMs;
	LastFrameMs = now;
	FrameGapSinceTick = std::max(FrameGapSinceTick, gap);
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
	TickCount++;
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
	if (leveltype != DTYPE_TOWN) {
		TrackDensity(player, now);
		LevelCensusTick(player, CurrentFight.active);
	}
	TrackBuild(player, now);
	if (now - WindowStartMs >= TelemetryStatsIntervalMs)
		WriteStatsLine(now);
	FlushEffects(now, false);
	FlushBurns(now, false);
	if (now - LastFlushMs >= TelemetryFlushMs)
		FlushAll();
}

void TelemetryFatal(std::string_view message)
{
	TelemetryLog("crash", StrCat(message, CrashContext()));
	WriteRecentEvents();
#if defined(_WIN32) && (defined(__x86_64__) || defined(_M_X64))
	// An error message, like a failed check, has no exception: the stack is taken from here.
	if (LogFile != nullptr) {
		CONTEXT context;
		RtlCaptureContext(&context);
		WriteCallStack(context);
	}
#endif
	WriteCrashReport(StrCat(message, CrashContext()));
}

void TelemetryDrawStep(const char *step)
{
	DrawStep = step;
}

std::string TelemetryStatsText()
{
	const double minutes = std::max<uint32_t>(Now() - GameStartMs, 1) / 60000.0;
	const int dropped = Sum(Totals.drops);
	return std::format("{:.1f} min played, {:d} deaths, {:d} levels entered, {:.0f} drops/hour: {:s}", minutes, Totals.deaths, Totals.levelsEntered,
	    dropped / minutes * 60, RatesText(Totals, minutes));
}

std::string TelemetryDamageTakenText()
{
	std::vector<std::pair<std::string, AttackerDamage>> sorted = DamageByAttacker;
	std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.second.raw > b.second.raw; });
	std::string text = "damage taken by attacker:";
	if (sorted.empty())
		return StrCat(text, " none yet");
	for (const auto &[name, damage] : sorted)
		StrAppend(text, std::format(" {:s} {:d} ({:d} hits, biggest {:d});", name, Whole(damage.raw), damage.hits, Whole(damage.biggestRaw)));
	return text;
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
		// The spells inside a meta gem, in brackets.
		for (size_t socket = 0; socket < MaxSupportSockets; socket++) {
			if (const SkillSlot inside = player.poe2.metaSkills[i][socket]; !inside.isEmpty())
				StrAppend(text, " [", SkillName(inside), " gem ", static_cast<int>(player.poe2.metaSkillLevels[i][socket]), "]");
		}
		for (const SupportGem support : player.poe2.slotSupports[i]) {
			if (support != SupportGem::None)
				StrAppend(text, " +", SupportGemName(support));
		}
		StrAppend(text, ";");
	}
	StrAppend(text, " | passives ", player.poe2.passives.count());
	StrAppend(text, std::format(" | defences: armour {:d}, evasion {:d}, energy shield {:d}, block {:d}%, resist fire {:d}% lightning {:d}% magic {:d}%, spirit {:d}/{:d}",
	                    ArmourRating(player, false), EvasionRating(player, false), player.poe2.maxEnergyShield, std::clamp(player.GetBlockChance(false), 0, player.GetMaxBlockChance(gbIsPoe2)),
	                    player._pFireResist, player._pLghtResist, player._pMagResist, ReservedSpirit(player), MaxSpirit(player)));
	StrAppend(text, " | belt:");
	for (const Item &item : player.SpdList) {
		if (!item.isEmpty())
			StrAppend(text, " ", item._iIName, ";");
	}
	return text;
}

} // namespace devilution
