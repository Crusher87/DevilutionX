#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <sstream>
#include <string>

#include "engine/assets.hpp"
#include "items.h"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "levels/themes.h"
#include "levels/trigs.h"
#include "monster.h"
#include "multi.h"
#include "objects.h"
#include "player.h"
#include "poe2/level_census.hpp"
#include "poe2/telemetry.hpp"
#include "tables/playerdat.hpp"
#include "utils/file_util.h"
#include "utils/paths.h"

namespace devilution {
namespace {

/** Carves a rectangle of floor and gives it a room number. */
void Carve(int x0, int y0, int x1, int y1, int8_t room)
{
	for (int x = x0; x <= x1; x++) {
		for (int y = y0; y <= y1; y++) {
			dPiece[x][y] = 0;
			dTransVal[x][y] = room;
		}
	}
}

void AddMonster(int x, int y)
{
	Monster &monster = Monsters[ActiveMonsterCount];
	monster = {};
	monster.position.tile = { static_cast<WorldTileCoord>(x), static_cast<WorldTileCoord>(y) };
	monster.hitPoints = Fixed26_6::fromInt(10);
	monster.talkMsg = TEXT_NONE;
	monster.ai = MonsterAIID::Zombie;
	monster.uniqueType = UniqueMonsterType::None;
	ActiveMonsters[ActiveMonsterCount] = static_cast<unsigned>(ActiveMonsterCount);
	ActiveMonsterCount++;
}

/**
 * A small floor: the up stairs in room 1 (10x10, one monster), a corridor (room 2, nothing in it) to room 3
 * (10x10, a group of five and the down stairs), and a closet below room 1 (room 4) holding one barrel.
 */
class Poe2LevelCensusTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		gbIsMultiplayer = false;
		gbIsPoe2 = false;
		leveltype = DTYPE_CATHEDRAL;
		setlevel = false;
		currlevel = 1;
		numthemes = 0;

		SOLData[0] = TileProperties::None;
		SOLData[1] = TileProperties::Solid;
		for (int x = 0; x < MAXDUNX; x++) {
			for (int y = 0; y < MAXDUNY; y++) {
				dPiece[x][y] = 1;
				dTransVal[x][y] = 0;
			}
		}
		Carve(20, 20, 29, 29, 1);
		Carve(30, 24, 39, 25, 2);
		Carve(40, 20, 49, 29, 3);
		Carve(22, 30, 25, 35, 4);

		numtrigs = 2;
		trigs[0] = { { 21, 21 }, WM_DIABPREVLVL, 0 };
		trigs[1] = { { 48, 28 }, WM_DIABNEXTLVL, 0 };

		ActiveMonsterCount = 0;
		AddMonster(22, 28);
		AddMonster(44, 24);
		AddMonster(45, 24);
		AddMonster(44, 25);
		AddMonster(45, 25);
		AddMonster(46, 26);

		ActiveObjectCount = 1;
		ActiveObjects[0] = 0;
		Objects[0] = {};
		Objects[0]._otype = OBJ_BARREL;
		Objects[0]._oBreak = 1;
		Objects[0].position = { 23, 33 };
		Objects[0].selectionRegion = SelectionRegion::Bottom;

		ActiveItemCount = 0;
		MyPlayer->position.tile = { 21, 22 };
	}

	void TearDown() override
	{
		ActiveMonsterCount = 0;
		ActiveObjectCount = 0;
		numtrigs = 0;
		leveltype = DTYPE_TOWN;
	}
};

TEST_F(Poe2LevelCensusTest, CountsRoomsGroupsAndTheWayThrough)
{
	const std::string text = LevelCensusText();
	EXPECT_NE(text.find("4 rooms (1 empty, 1 with only barrels or props, 1 with one monster)"), std::string::npos) << text;
	// Room 3 and the closet each have one way in; only the closet has nothing worth walking in for.
	EXPECT_NE(text.find("2 dead ends (1 with nothing in them)"), std::string::npos) << text;
	EXPECT_NE(text.find("6 monsters of 1 kinds in 2 groups (1 alone, 0 of 2-3, 1 of 4-6"), std::string::npos) << text;
	EXPECT_NE(text.find("1 barrels"), std::string::npos) << text;
	// Up stairs to down stairs goes through rooms 1, 2 and 3. Only the middle of the corridor (x 31 to 35) is more
	// than 8 steps from both the lone monster and the group.
	EXPECT_NE(text.find("through 3 rooms"), std::string::npos) << text;
	EXPECT_NE(text.find("longest empty stretch 5 tiles"), std::string::npos) << text;
	EXPECT_NE(text.find("You're in room 1: 100 tiles, 2 doors, 1 monsters"), std::string::npos) << text;
}

TEST_F(Poe2LevelCensusTest, WritesTheCensusAndPacingEvents)
{
	paths::SetPrefPath(::testing::TempDir() + "poe2_level_census_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + DIRECTORY_SEPARATOR_STR);
	TelemetryInit();
	ASSERT_TRUE(TelemetryEnabled());
	// The play log describes the hero at game start, which needs the class and item tables.
	UnloadModArchives();
	LoadModArchives({});
	LoadItemData();
	LoadPlayerDataFiles();
	MyPlayer->_pMaxHP = MyPlayer->_pHitPoints = Fixed26_6::fromInt(100);
	TelemetryGameStarted(*MyPlayer, false);
	// The play log calls these from TelemetryLevelEntered and when the level is left; the made-up monsters
	// here have no monster data for the rest of that to read.
	LevelCensusEntered();

	// Walk into the corridor and kill one of the group.
	MyPlayer->_pmode = PM_WALK_SIDEWAYS;
	LevelCensusTick(*MyPlayer, false);
	MyPlayer->position.tile = { 32, 24 };
	LevelCensusTick(*MyPlayer, false);
	LevelCensusKill(Monsters[1]);
	LevelCensusLeft("left the level");
	TelemetryFlush();

	std::string latest;
	const std::string folder = paths::PrefPath() + "poe2_logs" + DIRECTORY_SEPARATOR_STR;
	for (const std::string &file : ListFiles(folder.c_str())) {
		if (file.ends_with(".events") && file > latest)
			latest = file;
	}
	std::ifstream stream(folder + latest);
	std::stringstream contents;
	contents << stream.rdbuf();
	const std::string events = contents.str();
	EXPECT_NE(events.find(R"("k":"floor_census")"), std::string::npos);
	EXPECT_NE(events.find(R"("rooms":4,)"), std::string::npos);
	EXPECT_NE(events.find(R"("longestEmptyWalk":5,)"), std::string::npos);
	EXPECT_NE(events.find(R"("map":[)"), std::string::npos);
	EXPECT_NE(events.find(R"("k":"floor_pacing")"), std::string::npos);
	EXPECT_NE(events.find(R"("roomsVisited":2)"), std::string::npos);
	EXPECT_NE(events.find(R"("kills":1,)"), std::string::npos);
}

} // namespace
} // namespace devilution
