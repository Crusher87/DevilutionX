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

TEST(Poe2CatacombsFloors, ReadsTheRooms)
{
	SetCatacombsRooms({});
	paths::SetPrefPath(paths::BasePath() + "test/fixtures/");
	LoadCoreArchives();
	LoadModArchives({ { "poe2" } });
	const std::vector<CathedralRoomDef> &rooms = GetCatacombsRooms();
	ASSERT_GE(rooms.size(), 30U);
	EXPECT_EQ(rooms[0].id, "colhall");
	const auto arrival = std::find_if(rooms.begin(), rooms.end(), [](const CathedralRoomDef &room) { return room.id == "arrival"; });
	ASSERT_NE(arrival, rooms.end());
	EXPECT_EQ(arrival->grid[2], "...UU..ZZ..");
}

TEST(Poe2CatacombsFloors, RoomHistoriesStayApart)
{
	std::vector<CathedralRoomDef> rooms(3);
	for (CathedralRoomDef &room : rooms)
		room.grid = { "." };
	SetCathedralRooms(rooms);
	SetCatacombsRooms(rooms);
	// Newest first: a Catacombs floor with room 0, a Cathedral floor with room 1.
	const std::array<uint64_t, 5> recent { 1 | CatacombsRoomsMark, 2, 0, 0, 0 };
	EXPECT_EQ(CathedralRoomsToAvoid(recent, true), uint64_t { 1 });
	EXPECT_EQ(CathedralRoomsToAvoid(recent, false), uint64_t { 2 });
	SetCathedralRooms({});
	SetCatacombsRooms({});
}

/** Diablo's own variants of a tile (drlg_l2.cpp BTYPESL2) count as the tile; blood on the floor counts as floor. */
constexpr uint8_t Variants[161] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0, 0, 17, 18, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 2, 2, 2, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 0, 3, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

int Base(uint8_t tile)
{
	if (tile == 108 || tile == 110)
		return 3;
	return tile < 161 && Variants[tile] != 0 ? Variants[tile] : tile;
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

class Poe2CatacombsFloorsLevelTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		paths::SetPrefPath(paths::BasePath() + "test/fixtures/");
		TestInitGame(true, false);
		LoadModArchives({ { "poe2" } });
		SetCatacombsRooms({});
		Poe2FloorsReset();
		gbIsPoe2 = true;
		GetOptions().Gameplay.newCatacombsFloors.SetValue(true);
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
		leveltype = DTYPE_CATACOMBS;
		pMegaTiles = std::make_unique<MegaTile[]>(GetTileCount(leveltype));
		Poe2ApplyFreshFloor(level, false, seed, 1);
		CreateDungeon(seed, ENTRY_MAIN);
		if (const char *dir = std::getenv("POE2_DUMP_FLOORS")) {
			const std::string name = std::string(dir) + "/catacombs_" + std::to_string(level) + "_" + std::to_string(seed);
			std::ofstream out(name + ".txt");
			for (int y = 0; y < DMAXY; y++) {
				for (int x = 0; x < DMAXX; x++)
					out << static_cast<int>(dungeon[x][y]) << (x + 1 < DMAXX ? " " : "\n");
			}
			std::ofstream areas(name + ".areas");
			for (const CathedralArea &area : CurrentCathedralPlan().areas) {
				const Rectangle in = area.interior();
				areas << static_cast<int>(area.kind) << ' ' << area.step << ' ' << (area.design >= 0 ? GetCatacombsRooms()[area.design].id : (area.corridor ? "passage" : "-"))
				      << ' ' << in.position.x << ' ' << in.position.y << ' ' << in.size.width << ' ' << in.size.height << '\n';
			}
		}
	}

	/** Which quest's room the level holds, as the quest table set it. */
	void SetQuests(int level)
	{
		for (const quest_id id : { Q_BLOOD, Q_SCHAMB, Q_BLIND, Q_ZHAR })
			Quests[id]._qactive = QUEST_NOTAVAIL;
		constexpr quest_id ByLevel[] { Q_BLOOD, Q_SCHAMB, Q_BLIND, Q_ZHAR };
		const quest_id id = ByLevel[level - 5];
		Quests[id]._qactive = QUEST_INIT;
		Quests[id]._qlevel = level;
	}
};

TEST_F(Poe2CatacombsFloorsLevelTest, BuildsEveryFloorWithItsStairsAndQuestRoom)
{
	std::map<std::string, int> roomsSeen;
	int floors = 0;
	int rooms = 0;
	int designed = 0;
	int themes = 0;
	for (int level = 5; level <= 8; level++) {
		for (uint32_t seed = 1; seed <= 30; seed++) {
			// Half the floors with their quest room, half without.
			if (seed % 2 == 0)
				SetQuests(level);
			else
				for (const quest_id id : { Q_BLOOD, Q_SCHAMB, Q_BLIND, Q_ZHAR })
					Quests[id]._qactive = QUEST_NOTAVAIL;
			Build(level, seed * 7919U + static_cast<uint32_t>(level));
			const std::string where = "level " + std::to_string(level) + " seed " + std::to_string(seed);
			ASSERT_TRUE(IsCathedralFunnelFloor()) << where;
			const CathedralPlan &plan = CurrentCathedralPlan();
			ASSERT_TRUE(plan.catacombs) << where;
			EXPECT_GE(plan.areas.size(), 10U) << where;
			EXPECT_EQ(CountTile({ 72 }), 1) << "stairs up, " << where;
			EXPECT_EQ(CountTile({ 71 }), 1) << "stairs down, " << where;
			EXPECT_EQ(CountTile({ 158 }), level == 5 ? 1 : 0) << "stairs to town, " << where;
			if (seed % 2 == 0 && level != 8)
				EXPECT_GT(SetPiece.size.width, 0) << "quest room, " << where;
			if (seed % 2 == 0 && level == 8)
				EXPECT_GT(themeCount, 0) << "Zhar's library, " << where;
			for (const CathedralArea &area : plan.areas) {
				if (area.design >= 0) {
					roomsSeen[GetCatacombsRooms()[area.design].id]++;
					designed++;
				}
			}
			rooms += static_cast<int>(plan.areas.size());
			themes += themeCount;
			floors++;
		}
	}
	std::cout << floors << " floors: " << rooms << " rooms, " << designed << " designed, " << themes << " theme rooms\n";
	// Over 120 floors nearly every room of the collection turns up.
	EXPECT_GE(roomsSeen.size(), GetCatacombsRooms().size() - 4);
	for (const CathedralRoomDef &room : GetCatacombsRooms()) {
		if (roomsSeen.count(room.id) == 0)
			std::cout << "never built: " << room.id << "\n";
	}
}

TEST_F(Poe2CatacombsFloorsLevelTest, DrawsTheInnerWallsDoorsAndArchways)
{
	int doors = 0;
	int arches = 0;
	int pillars = 0;
	for (uint32_t seed = 1; seed <= 60; seed++) {
		Build(6, seed * 104729U);
		for (const CathedralLine &line : CurrentCathedralPlan().lines) {
			const uint8_t tile = dungeon[line.block.x][line.block.y];
			if (line.kind == 'd') {
				EXPECT_TRUE(tile == 4 || tile == 5) << "door at " << line.block.x << "," << line.block.y << " is " << static_cast<int>(tile);
				doors++;
			} else if (line.kind == 'a') {
				// Of two archways ending at one corner, the north-south one is a door.
				EXPECT_TRUE((tile >= 39 && tile <= 45) || tile == 4 || Base(tile) == 1) << "archway at " << line.block.x << "," << line.block.y << " is " << static_cast<int>(tile);
				if (tile >= 39 && tile <= 45)
					arches++;
			} else {
				EXPECT_NE(tile, 3) << "wall at " << line.block.x << "," << line.block.y << " came out as floor";
			}
		}
		for (const Point p : CurrentCathedralPlan().pillars) {
			if (dungeon[p.x][p.y] == 6 || dungeon[p.x][p.y] == 83)
				pillars++;
		}
	}
	EXPECT_GT(doors, 0);
	EXPECT_GT(arches, 0);
	EXPECT_GT(pillars, 0);
}

TEST_F(Poe2CatacombsFloorsLevelTest, BuildingAgainGivesTheSameFloor)
{
	Build(7, 4242);
	std::vector<uint8_t> first(&dungeon[0][0], &dungeon[0][0] + (DMAXX * DMAXY));
	LevelSeeds[7] = std::nullopt;
	CreateDungeon(4242, ENTRY_MAIN);
	std::vector<uint8_t> second(&dungeon[0][0], &dungeon[0][0] + (DMAXX * DMAXY));
	EXPECT_EQ(first, second);
}

TEST_F(Poe2CatacombsFloorsLevelTest, OldFloorsStayOneSettingAway)
{
	GetOptions().Gameplay.newCatacombsFloors.SetValue(false);
	Build(5, 99);
	EXPECT_FALSE(IsCathedralFunnelFloor());
	GetOptions().Gameplay.newCatacombsFloors.SetValue(true);
	LevelSeeds[5] = std::nullopt;
	CreateDungeon(99, ENTRY_MAIN);
	EXPECT_FALSE(IsCathedralFunnelFloor());
	Build(5, 100);
	EXPECT_TRUE(IsCathedralFunnelFloor());
}

/**
 * The tile pairs side by side on the new floors that Diablo's own Catacombs floors never make. Those are the places
 * that may look wrong in game; the test prints them so they can be checked there.
 */
TEST_F(Poe2CatacombsFloorsLevelTest, ReportsTilePairsDiabloNeverMakes)
{
	std::set<std::pair<int, int>> across;
	std::set<std::pair<int, int>> down;
	GetOptions().Gameplay.newCatacombsFloors.SetValue(false);
	for (int level = 5; level <= 8; level++) {
		for (uint32_t seed = 1; seed <= 60; seed++) {
			Build(level, seed * 31337U + static_cast<uint32_t>(level));
			for (int y = 0; y + 1 < DMAXY; y++) {
				for (int x = 0; x + 1 < DMAXX; x++) {
					across.insert({ Base(dungeon[x][y]), Base(dungeon[x + 1][y]) });
					down.insert({ Base(dungeon[x][y]), Base(dungeon[x][y + 1]) });
				}
			}
		}
	}
	GetOptions().Gameplay.newCatacombsFloors.SetValue(true);
	std::map<std::string, int> unseen;
	std::map<std::string, int> unseenDesigned;
	for (int level = 5; level <= 8; level++) {
		for (uint32_t seed = 1; seed <= 60; seed++) {
			Build(level, seed * 7717U + static_cast<uint32_t>(level));
			const CathedralPlan &plan = CurrentCathedralPlan();
			for (int y = 0; y + 1 < DMAXY; y++) {
				for (int x = 0; x + 1 < DMAXX; x++) {
					const bool designed = plan.designed.test(x, y) || plan.designed.test(x + 1, y) || plan.designed.test(x, y + 1);
					auto &bucket = designed ? unseenDesigned : unseen;
					if (across.count({ Base(dungeon[x][y]), Base(dungeon[x + 1][y]) }) == 0)
						bucket[std::to_string(dungeon[x][y]) + " then " + std::to_string(dungeon[x + 1][y])]++;
					if (down.count({ Base(dungeon[x][y]), Base(dungeon[x][y + 1]) }) == 0)
						bucket[std::to_string(dungeon[x][y]) + " over " + std::to_string(dungeon[x][y + 1])]++;
				}
			}
		}
	}
	for (const auto &[pair, count] : unseen)
		std::cout << "plain rooms: " << pair << " x" << count << "\n";
	for (const auto &[pair, count] : unseenDesigned)
		std::cout << "designed rooms: " << pair << " x" << count << "\n";
	SUCCEED();
}

} // namespace
} // namespace devilution
