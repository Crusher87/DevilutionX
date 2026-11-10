#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <string>

#include "drlg_test.hpp"
#include "game_mode.hpp"
#include "levels/gendung.h"
#include "options.h"
#include "poe2/cathedral_floors.hpp"
#include "poe2/floors.hpp"

namespace devilution {
namespace {

TEST(Poe2HellFloors, ReadsTheRooms)
{
	SetHellRooms({});
	paths::SetPrefPath(paths::BasePath() + "test/fixtures/");
	LoadCoreArchives();
	LoadModArchives({ { "poe2" } });
	const std::vector<CathedralRoomDef> &rooms = GetHellRooms();
	ASSERT_EQ(rooms.size(), 41U);
	EXPECT_EQ(rooms[0].id, "chainhall");
	// The page's chained bodies are the Cathedral's 'T'.
	EXPECT_EQ(rooms[0].grid[2], ".T.T...T.T.");
	const auto arrival = std::find_if(rooms.begin(), rooms.end(), [](const CathedralRoomDef &room) { return room.id == "arrival"; });
	ASSERT_NE(arrival, rooms.end());
	EXPECT_EQ(arrival->grid[1], ".UUUU.ZZZZ.");
	SetHellRooms({});
}

TEST(Poe2HellFloors, RoomHistoriesStayApart)
{
	std::vector<CathedralRoomDef> rooms(3);
	for (CathedralRoomDef &room : rooms)
		room.grid = { "." };
	SetCavesRooms(rooms);
	SetHellRooms(rooms);
	// Newest first: a Hell floor with room 0, then a Caves floor with room 1.
	const std::array<uint64_t, 5> recent { 1 | HellRoomsMark, 2 | CavesRoomsMark, 0, 0, 0 };
	EXPECT_EQ(CathedralRoomsToAvoid(recent, false, false, true), uint64_t { 1 });
	EXPECT_EQ(CathedralRoomsToAvoid(recent, false, true), uint64_t { 2 });
	EXPECT_EQ(CathedralRoomsToAvoid(recent), uint64_t { 0 });
	SetCavesRooms({});
	SetHellRooms({});
}

int CountTile(std::initializer_list<uint8_t> tiles)
{
	int n = 0;
	for (int y = 0; y < DMAXY; y++) {
		for (int x = 0; x < DMAXX; x++) {
			if (std::find(tiles.begin(), tiles.end(), dungeon[x][y]) != tiles.end())
				n++;
		}
	}
	return n;
}

class Poe2HellFloorsLevelTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		paths::SetPrefPath(paths::BasePath() + "test/fixtures/");
		TestInitGame(true, false);
		LoadModArchives({ { "poe2" } });
		SetHellRooms({});
		Poe2FloorsReset();
		gbIsPoe2 = true;
		GetOptions().Gameplay.newHellFloors.SetValue(true);
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		SetCathedralFunnelFloor(false);
	}

	void Build(int level, uint32_t seed)
	{
		LevelSeeds[level] = std::nullopt;
		currlevel = level;
		leveltype = DTYPE_HELL;
		pMegaTiles = std::make_unique<MegaTile[]>(GetTileCount(leveltype));
		Poe2ApplyFreshFloor(level, false, seed, 1);
		CreateDungeon(seed, ENTRY_MAIN);
		if (const char *dir = std::getenv("POE2_DUMP_FLOORS")) {
			const std::string name = std::string(dir) + "/hell_" + std::to_string(level) + "_" + std::to_string(seed);
			std::ofstream out(name + ".txt");
			for (int y = 0; y < DMAXY; y++) {
				for (int x = 0; x < DMAXX; x++)
					out << static_cast<int>(dungeon[x][y]) << (x + 1 < DMAXX ? " " : "\n");
			}
			std::ofstream areas(name + ".areas");
			for (const CathedralArea &area : CurrentCathedralPlan().areas) {
				const Rectangle in = area.interior();
				areas << static_cast<int>(area.kind) << ' ' << area.step << ' ' << (area.design >= 0 ? GetHellRooms()[area.design].id : (area.corridor ? "passage" : "-"))
				      << ' ' << area.turn << ' ' << in.position.x << ' ' << in.position.y << ' ' << in.size.width << ' ' << in.size.height << '\n';
			}
		}
	}

	/** The Warlord of Blood on level 13 when withQuest; the hell gate on level 15 still shut. */
	static void SetQuests(bool withQuest)
	{
		Quests[Q_WARLORD]._qactive = withQuest ? QUEST_INIT : QUEST_NOTAVAIL;
		Quests[Q_WARLORD]._qlevel = 13;
		Quests[Q_DIABLO]._qactive = QUEST_INIT;
	}
};

TEST_F(Poe2HellFloorsLevelTest, BuildsEveryFloorWithItsStairsAndQuests)
{
	std::map<std::string, int> roomsSeen;
	int floors = 0;
	int rooms = 0;
	int designed = 0;
	int themes = 0;
	for (int level = 13; level <= 15; level++) {
		for (uint32_t seed = 1; seed <= 40; seed++) {
			// Half the floors with the Warlord, half without.
			const bool warlord = seed % 2 == 0;
			SetQuests(warlord);
			Build(level, seed * 7919U + static_cast<uint32_t>(level));
			const std::string where = "level " + std::to_string(level) + " seed " + std::to_string(seed);
			ASSERT_TRUE(IsCathedralFunnelFloor()) << where;
			const CathedralPlan &plan = CurrentCathedralPlan();
			ASSERT_TRUE(plan.hell) << where;
			EXPECT_GE(plan.areas.size(), 10U) << where;
			EXPECT_EQ(CountTile({ 36 }), 1) << "stairs up, " << where;
			EXPECT_EQ(CountTile({ 134 }), level == 13 ? 1 : 0) << "stairs to town, " << where;
			// The stairs down are Diablo's, or the Warlord's in his chamber; level 15 has the pentagram instead.
			EXPECT_EQ(CountTile({ 45 }), level == 15 ? 0 : 1) << "stairs down, " << where;
			EXPECT_EQ(CountTile({ 98 }), level == 15 ? 1 : 0) << "pentagram, " << where;
			if (level == 13 && warlord)
				EXPECT_EQ(SetPiece.size.width, 8) << "the Warlord's chamber, " << where;
			for (const CathedralArea &area : plan.areas) {
				if (area.design >= 0) {
					roomsSeen[GetHellRooms()[area.design].id]++;
					designed++;
				}
			}
			rooms += static_cast<int>(plan.areas.size());
			themes += themeCount;
			floors++;
		}
	}
	std::cout << floors << " floors: " << rooms << " rooms, " << designed << " designed, " << themes << " theme rooms\n";
	EXPECT_GE(roomsSeen.size(), GetHellRooms().size() - 4);
	for (const CathedralRoomDef &room : GetHellRooms()) {
		if (roomsSeen.count(room.id) == 0)
			std::cout << "never built: " << room.id << "\n";
	}
}

TEST_F(Poe2HellFloorsLevelTest, DrawsTheWallsArchwaysAndPillars)
{
	int arches = 0;
	int open = 0;
	for (uint32_t seed = 1; seed <= 60; seed++) {
		SetQuests(false);
		Build(14, seed * 104729U);
		const CathedralPlan &plan = CurrentCathedralPlan();
		for (const CathedralLine &line : plan.lines) {
			const uint8_t tile = dungeon[line.block.x][line.block.y];
			const std::string at = std::string(1, line.kind) + " at " + std::to_string(line.block.x) + "," + std::to_string(line.block.y) + " is " + std::to_string(tile);
			if (line.kind == 'a') {
				// An archway's open middle, or a plain gap where the wall round it wasn't straight.
				EXPECT_TRUE(IsAnyOf(tile, 6, 60, 47, 48, 49, 50, 51, 95, 96, 97, 5)) << at;
				if (IsAnyOf(tile, 60) || dungeon[line.block.x][line.block.y - 1] == 53)
					arches++;
				else
					open++;
			}
			if (line.kind == '#')
				EXPECT_NE(tile, 6) << at;
		}
		for (const Point pillar : plan.pillars)
			EXPECT_NE(dungeon[pillar.x][pillar.y], 6) << "pillar at " << pillar.x << "," << pillar.y;
	}
	std::cout << arches << " archways, " << open << " plain gaps\n";
	EXPECT_GT(arches, 0);
}

TEST_F(Poe2HellFloorsLevelTest, BuildingAgainGivesTheSameFloor)
{
	SetQuests(true);
	Build(13, 4242);
	std::vector<uint8_t> first(&dungeon[0][0], &dungeon[0][0] + (DMAXX * DMAXY));
	LevelSeeds[13] = std::nullopt;
	CreateDungeon(4242, ENTRY_MAIN);
	std::vector<uint8_t> second(&dungeon[0][0], &dungeon[0][0] + (DMAXX * DMAXY));
	EXPECT_EQ(first, second);
}

TEST_F(Poe2HellFloorsLevelTest, OldFloorsStayOneSettingAway)
{
	SetQuests(false);
	GetOptions().Gameplay.newHellFloors.SetValue(false);
	Build(14, 99);
	EXPECT_FALSE(IsCathedralFunnelFloor());
	GetOptions().Gameplay.newHellFloors.SetValue(true);
	LevelSeeds[14] = std::nullopt;
	CreateDungeon(99, ENTRY_MAIN);
	EXPECT_FALSE(IsCathedralFunnelFloor());
	Build(14, 100);
	EXPECT_TRUE(IsCathedralFunnelFloor());
}

/**
 * The tile pairs side by side on the new floors that Diablo's own Hell floors never make. Those are the places that may
 * look wrong in game; the test prints them so they can be checked there.
 */
TEST_F(Poe2HellFloorsLevelTest, ReportsTilePairsDiabloNeverMakes)
{
	std::set<std::pair<int, int>> across;
	std::set<std::pair<int, int>> down;
	SetQuests(false);
	GetOptions().Gameplay.newHellFloors.SetValue(false);
	for (int level = 13; level <= 15; level++) {
		for (uint32_t seed = 1; seed <= 100; seed++) {
			Build(level, seed * 31337U + static_cast<uint32_t>(level));
			for (int y = 0; y + 1 < DMAXY; y++) {
				for (int x = 0; x + 1 < DMAXX; x++) {
					across.insert({ dungeon[x][y], dungeon[x + 1][y] });
					down.insert({ dungeon[x][y], dungeon[x][y + 1] });
				}
			}
		}
	}
	GetOptions().Gameplay.newHellFloors.SetValue(true);
	std::map<std::string, int> unseen;
	for (int level = 13; level <= 15; level++) {
		for (uint32_t seed = 1; seed <= 40; seed++) {
			Build(level, seed * 7717U + static_cast<uint32_t>(level));
			for (int y = 0; y + 1 < DMAXY; y++) {
				for (int x = 0; x + 1 < DMAXX; x++) {
					if (across.count({ dungeon[x][y], dungeon[x + 1][y] }) == 0)
						unseen[std::to_string(dungeon[x][y]) + " then " + std::to_string(dungeon[x + 1][y])]++;
					if (down.count({ dungeon[x][y], dungeon[x][y + 1] }) == 0)
						unseen[std::to_string(dungeon[x][y]) + " over " + std::to_string(dungeon[x][y + 1])]++;
				}
			}
		}
	}
	for (const auto &[pair, count] : unseen)
		std::cout << "never in Diablo's Hell: " << pair << " x" << count << "\n";
	SUCCEED();
}

} // namespace
} // namespace devilution
