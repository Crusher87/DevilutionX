/**
 * @file floors.cpp
 *
 * POE2 Mashup: floors that are rebuilt fresh on each visit. See floors.hpp for the rules.
 */
#include "poe2/floors.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include "levels/gendung.h"
#include "msg.h"
#include "options.h"
#include "multi.h"
#include "player.h"
#include "poe2/cathedral_floors.hpp"
#include "poe2/telemetry.hpp"
#include "portal.h"
#include "quests.h"
#include "utils/str_cat.hpp"

namespace devilution {

uint32_t Poe2GameTicks = 0;
bool Poe2FloorFresh = false;
bool Poe2ForceFreshFloor = false;

namespace {

constexpr uint8_t FloorsVersion = 2;
constexpr size_t SetLevelCount = static_cast<size_t>(SL_LAST) + 1;

/** @brief What the save remembers about one floor. */
struct FloorRecord {
	/** A saved copy of this floor exists and may be loaded again. */
	bool kept = false;
	/** Game tick at which the hero last left (or saved) the floor. */
	uint32_t leftAt = 0;
	/** How many times the floor has been built for this hero. 0 means never. */
	uint16_t visits = 0;
	/** The builder and rooms of the floor's current build are settled (see Poe2FloorBuildFor). */
	bool stamped = false;
	/** Built by the new floor builder (poe2/cathedral_floors.hpp): the Cathedral's, Catacombs', Caves' or Hell's. */
	bool cathedralFunnel = false;
	/** The designed rooms this build had to leave out, so building it again gives the same floor. */
	uint64_t avoidRooms = 0;
	/** The rooms of this build still have to go into the room history. */
	bool roomsPending = false;
};

std::array<FloorRecord, NUMLEVELS> Floors;
std::array<FloorRecord, SetLevelCount> SetFloors;
/** Dice stream for floor rolls, so a replay or a multiplayer host rolls the same floors. */
uint32_t FloorRollState = 0;
/** The designed rooms of the last floors built by the new Cathedral builder, newest first. */
std::array<uint64_t, 5> RecentRooms {};

FloorRecord *RecordFor(int level, bool isSetLevel)
{
	if (isSetLevel) {
		if (level < 0 || static_cast<size_t>(level) >= SetFloors.size())
			return nullptr;
		return &SetFloors[level];
	}
	if (level < 0 || level >= NUMLEVELS)
		return nullptr;
	return &Floors[level];
}

bool EverVisited(int level, bool isSetLevel)
{
	if (MyPlayer == nullptr)
		return false;
	if (isSetLevel)
		return level >= 0 && static_cast<size_t>(level) < std::size(MyPlayer->_pSLvlVisited) && MyPlayer->_pSLvlVisited[level];
	return level >= 0 && level < NUMLEVELS && MyPlayer->_pLvlVisited[level];
}

bool MyPortalLeadsTo(int level, bool isSetLevel)
{
	if (MyPlayerId >= MAXPORTAL)
		return false;
	const Portal &portal = Portals[MyPlayerId];
	return portal.open && portal.level != 0 && portal.setlvl == isSetLevel && portal.level == level;
}

bool AnyoneElseOnFloor(int level, bool isSetLevel)
{
	for (const Player &player : Players) {
		if (!player.plractive || &player == MyPlayer)
			continue;
		if (player.plrIsOnSetLevel == isSetLevel && player.plrlevel == level)
			return true;
	}
	return false;
}

/** @brief Floors whose quest ends the game stay as they were once that quest is done, so the ending can't replay. */
bool FloorIsFinalBossRest(int level, bool isSetLevel)
{
	if (isSetLevel)
		return false;
	if (level == 16 && Quests[Q_DIABLO]._qactive == QUEST_DONE)
		return true;
	if (level == 24 && Quests[Q_NAKRUL]._qactive == QUEST_DONE)
		return true;
	return false;
}

uint32_t MixSeed(uint32_t value)
{
	// SplitMix-style mixing: spreads a counter into a seed that looks nothing like its neighbours.
	value ^= value >> 16;
	value *= 0x7FEB352DU;
	value ^= value >> 15;
	value *= 0x846CA68BU;
	value ^= value >> 16;
	return value;
}

void Put32(std::vector<uint8_t> &out, uint32_t value)
{
	for (int i = 0; i < 4; i++)
		out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

void Put16(std::vector<uint8_t> &out, uint16_t value)
{
	out.push_back(static_cast<uint8_t>(value));
	out.push_back(static_cast<uint8_t>(value >> 8));
}

uint32_t Get32(std::span<const uint8_t> data, size_t &pos)
{
	uint32_t value = 0;
	for (int i = 0; i < 4; i++)
		value |= static_cast<uint32_t>(data[pos + i]) << (8 * i);
	pos += 4;
	return value;
}

uint16_t Get16(std::span<const uint8_t> data, size_t &pos)
{
	const uint16_t value = static_cast<uint16_t>(data[pos] | (data[pos + 1] << 8));
	pos += 2;
	return value;
}

void Put64(std::vector<uint8_t> &out, uint64_t value)
{
	Put32(out, static_cast<uint32_t>(value));
	Put32(out, static_cast<uint32_t>(value >> 32));
}

uint64_t Get64(std::span<const uint8_t> data, size_t &pos)
{
	const uint64_t low = Get32(data, pos);
	const uint64_t high = Get32(data, pos);
	return low | (high << 32);
}

void PutRecord(std::vector<uint8_t> &out, const FloorRecord &record)
{
	out.push_back(record.kept ? 1 : 0);
	Put32(out, record.leftAt);
	Put16(out, record.visits);
	out.push_back(static_cast<uint8_t>((record.stamped ? 1 : 0) | (record.cathedralFunnel ? 2 : 0)));
	Put64(out, record.avoidRooms);
}

FloorRecord GetRecord(std::span<const uint8_t> data, size_t &pos, uint8_t version)
{
	FloorRecord record;
	record.kept = data[pos++] != 0;
	record.leftAt = Get32(data, pos);
	record.visits = Get16(data, pos);
	if (version >= 2) {
		const uint8_t flags = data[pos++];
		record.stamped = (flags & 1) != 0;
		record.cathedralFunnel = (flags & 2) != 0;
		record.avoidRooms = Get64(data, pos);
	} else if (record.visits > 0) {
		// Saved before the new Cathedral floors: every floor built so far came from Diablo's builder.
		record.stamped = true;
	}
	return record;
}

size_t RecordSize(uint8_t version)
{
	return version >= 2 ? 1 + 4 + 2 + 1 + 8 : 1 + 4 + 2;
}

} // namespace

void Poe2FloorsReset()
{
	Floors.fill({});
	SetFloors.fill({});
	Poe2GameTicks = 0;
	Poe2FloorFresh = false;
	Poe2ForceFreshFloor = false;
	FloorRollState = MixSeed(sgGameInitInfo.gameSeed[0] ^ 0x5F10EDU);
	RecentRooms.fill(0);
}

void Poe2FloorsTick()
{
	Poe2GameTicks++;
}

bool Poe2FloorIsKept(int level, bool isSetLevel)
{
	if (!isSetLevel && level == 0)
		return EverVisited(level, isSetLevel); // town never resets
	if (!EverVisited(level, isSetLevel))
		return false;
	if (FloorIsFinalBossRest(level, isSetLevel))
		return true;
	const FloorRecord *record = RecordFor(level, isSetLevel);
	if (record == nullptr || !record->kept)
		return false;
	if (MyPortalLeadsTo(level, isSetLevel))
		return true;
	return Poe2GameTicks - record->leftAt <= FloorKeepTicks;
}

void Poe2FloorSaved(int level, bool isSetLevel)
{
	FloorRecord *record = RecordFor(level, isSetLevel);
	if (record == nullptr)
		return;
	record->kept = true;
	record->leftAt = Poe2GameTicks;
	if (record->visits == 0)
		record->visits = 1;
}

void Poe2ForgetFloor(int level, bool isSetLevel)
{
	FloorRecord *record = RecordFor(level, isSetLevel);
	if (record == nullptr)
		return;
	record->kept = false;
}

uint32_t Poe2RollFreshFloor(int level, bool isSetLevel)
{
	FloorRecord *record = RecordFor(level, isSetLevel);
	if (record == nullptr)
		return 0;
	FloorRollState++;
	const uint32_t seed = MixSeed(FloorRollState ^ MixSeed(static_cast<uint32_t>(level) + (isSetLevel ? 0x1000U : 0U)));
	const uint16_t visits = static_cast<uint16_t>(std::min<int>(record->visits + 1, UINT16_MAX));
	Poe2ApplyFreshFloor(level, isSetLevel, seed, visits);
	return seed;
}

void Poe2ApplyFreshFloor(int level, bool isSetLevel, uint32_t seed, uint16_t visits)
{
	FloorRecord *record = RecordFor(level, isSetLevel);
	if (record == nullptr)
		return;
	record->kept = false;
	record->stamped = false;
	record->visits = std::max(record->visits, visits);
	// Set levels share that table with floors 1-9, so their seed is left alone: only their monsters would change.
	if (!isSetLevel && level < NUMLEVELS) {
		DungeonSeeds[level] = seed;
		LevelSeeds[level] = std::nullopt;
	}
	if (gbIsMultiplayer)
		DeltaClearLevel(static_cast<uint8_t>(level));
	TelemetryLog("floor", StrCat(isSetLevel ? "set level " : "floor ", level, " built fresh, visit ", record->visits, " seed ", seed));
}

uint16_t Poe2FloorVisits(int level, bool isSetLevel)
{
	const FloorRecord *record = RecordFor(level, isSetLevel);
	if (record == nullptr)
		return 0;
	return std::max<uint16_t>(record->visits, EverVisited(level, isSetLevel) ? 1 : 0);
}

void Poe2BeginFloorVisit(bool firstflag, lvl_entry lvldir)
{
	Poe2FloorFresh = false;
	const int level = setlevel ? static_cast<int>(setlvlnum) : static_cast<int>(currlevel);
	const bool force = Poe2ForceFreshFloor;
	Poe2ForceFreshFloor = false;
	if (firstflag || lvldir == ENTRY_LOAD)
		return;
	if (!setlevel && level == 0)
		return;
	if (!EverVisited(level, setlevel)) {
		// First time here: count the visit, no new roll needed (the hero's seed table already has one).
		FloorRecord *record = RecordFor(level, setlevel);
		if (record != nullptr && record->visits == 0)
			record->visits = 1;
		return;
	}
	if (FloorIsFinalBossRest(level, setlevel))
		return;
	if (gbIsMultiplayer) {
		// Every player has a different clock, so only the Ctrl+click roll is used, and only when nobody else
		// is already standing on that floor (they would keep a different copy of it).
		if (force && !AnyoneElseOnFloor(level, setlevel)) {
			const uint32_t seed = Poe2RollFreshFloor(level, setlevel);
			NetSendCmdFreshFloor(static_cast<uint8_t>(level), setlevel, seed, Poe2FloorVisits(level, setlevel));
			Poe2FloorFresh = true;
		}
		return;
	}
	if (force || !Poe2FloorIsKept(level, setlevel)) {
		Poe2RollFreshFloor(level, setlevel);
		Poe2FloorFresh = true;
	}
}

void Poe2FloorsAfterLoadGame()
{
	const int level = setlevel ? static_cast<int>(setlvlnum) : static_cast<int>(currlevel);
	for (int i = 0; i < NUMLEVELS; i++) {
		if (i == 0)
			continue;
		const bool keep = (!setlevel && i == level) || MyPortalLeadsTo(i, false);
		if (!keep)
			Floors[i].kept = false;
		else
			Floors[i].leftAt = Poe2GameTicks;
	}
	for (size_t i = 0; i < SetFloors.size(); i++) {
		const bool keep = (setlevel && static_cast<int>(i) == level) || MyPortalLeadsTo(static_cast<int>(i), true);
		if (!keep)
			SetFloors[i].kept = false;
		else
			SetFloors[i].leftAt = Poe2GameTicks;
	}
}

void Poe2PortalClosed(int level, bool isSetLevel)
{
	FloorRecord *record = RecordFor(level, isSetLevel);
	if (record == nullptr || !record->kept)
		return;
	record->leftAt = Poe2GameTicks;
}

std::vector<uint8_t> Poe2PackFloors()
{
	std::vector<uint8_t> out;
	out.reserve(1 + 4 + 4 + 1 + 1 + ((Floors.size() + SetFloors.size()) * RecordSize(FloorsVersion)) + (RecentRooms.size() * 8));
	out.push_back(FloorsVersion);
	Put32(out, Poe2GameTicks);
	Put32(out, FloorRollState);
	out.push_back(static_cast<uint8_t>(Floors.size()));
	out.push_back(static_cast<uint8_t>(SetFloors.size()));
	for (const FloorRecord &record : Floors)
		PutRecord(out, record);
	for (const FloorRecord &record : SetFloors)
		PutRecord(out, record);
	for (const uint64_t rooms : RecentRooms)
		Put64(out, rooms);
	return out;
}

bool Poe2UnpackFloors(std::span<const uint8_t> data)
{
	if (data.size() < 1 + 4 + 4 + 2)
		return false;
	size_t pos = 0;
	const uint8_t version = data[pos++];
	if (version == 0 || version > FloorsVersion)
		return false;
	const uint32_t ticks = Get32(data, pos);
	const uint32_t rollState = Get32(data, pos);
	const size_t floorCount = data[pos++];
	const size_t setCount = data[pos++];
	if (data.size() < pos + ((floorCount + setCount) * RecordSize(version)) + (version >= 2 ? RecentRooms.size() * 8 : 0))
		return false;
	Floors.fill({});
	SetFloors.fill({});
	for (size_t i = 0; i < floorCount; i++) {
		const FloorRecord record = GetRecord(data, pos, version);
		if (i < Floors.size())
			Floors[i] = record;
	}
	for (size_t i = 0; i < setCount; i++) {
		const FloorRecord record = GetRecord(data, pos, version);
		if (i < SetFloors.size())
			SetFloors[i] = record;
	}
	RecentRooms.fill(0);
	if (version >= 2) {
		for (uint64_t &rooms : RecentRooms)
			rooms = Get64(data, pos);
	}
	Poe2GameTicks = ticks;
	FloorRollState = rollState;
	return true;
}


Poe2FloorBuild Poe2FloorBuildFor(int level)
{
	FloorRecord *record = RecordFor(level, false);
	if (record == nullptr)
		return {};
	if (!record->stamped) {
		record->stamped = true;
		const bool catacombs = leveltype == DTYPE_CATACOMBS;
		const bool caves = leveltype == DTYPE_CAVES;
		const bool hell = leveltype == DTYPE_HELL;
		bool wanted = *GetOptions().Gameplay.newCathedralFloors;
		if (hell)
			wanted = *GetOptions().Gameplay.newHellFloors;
		else if (caves)
			wanted = *GetOptions().Gameplay.newCavesFloors;
		else if (catacombs)
			wanted = *GetOptions().Gameplay.newCatacombsFloors;
		// A kept floor that was never settled was built before its dungeon had the new builder: it stays Diablo's.
		record->cathedralFunnel = !record->kept && wanted;
		record->avoidRooms = record->cathedralFunnel ? CathedralRoomsToAvoid(RecentRooms, catacombs, caves, hell) : 0;
		record->roomsPending = record->cathedralFunnel;
	}
	return { record->cathedralFunnel, record->avoidRooms };
}

void Poe2NoteFloorRooms(int level, uint64_t rooms)
{
	FloorRecord *record = RecordFor(level, false);
	if (record == nullptr || !record->roomsPending)
		return;
	record->roomsPending = false;
	std::copy_backward(RecentRooms.begin(), RecentRooms.end() - 1, RecentRooms.end());
	RecentRooms[0] = rooms;
}

} // namespace devilution
