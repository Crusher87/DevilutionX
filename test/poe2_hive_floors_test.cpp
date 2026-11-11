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

constexpr std::string_view HiveMods[] = { "hf", "poe2" };

TEST(Poe2HiveFloors, ReadsTheRooms)
{
	SetHiveRooms({});
	paths::SetPrefPath(paths::BasePath() + "test/fixtures/");
	LoadCoreArchives();
	LoadModArchives(HiveMods);
	const std::vector<CathedralRoomDef> &rooms = GetHiveRooms();
	ASSERT_EQ(rooms.size(), 40U);
	EXPECT_EQ(rooms[0].id, "broodhall");
	const auto spring = std::find_if(rooms.begin(), rooms.end(), [](const CathedralRoomDef &room) { return room.id == "spring"; });
	ASSERT_NE(spring, rooms.end());
	// The page's hive growth is the game's 'w', so it isn't read as the Caves' gate.
	EXPECT_EQ(spring->grid[1], "E..w...w..E");
	const auto larder = std::find_if(rooms.begin(), rooms.end(), [](const CathedralRoomDef &room) { return room.id == "larder"; });
	ASSERT_NE(larder, rooms.end());
	EXPECT_EQ(larder->grid[1], "E.Y.T.Y.T..");
	SetHiveRooms({});
}

TEST(Poe2HiveFloors, SlimePitsTurnWithTheirTiles)
{
	// A pit's marks sit on its four tiles, so a half turn moves them to the tiles across the room.
	const std::vector<std::string> grid { "SS...", "SS...", ".....", ".....", "....." };
	EXPECT_EQ(TurnCavesGrid(grid, 2), (std::vector<std::string> { ".....", ".....", "..SS.", "..SS.", "....." }));
}

TEST(Poe2HiveFloors, RoomHistoriesStayApart)
{
	std::vector<CathedralRoomDef> rooms(3);
	for (CathedralRoomDef &room : rooms)
		room.grid = { "." };
	SetCavesRooms(rooms);
	SetHiveRooms(rooms);
	// Newest first: a Hive floor with room 0, then a Caves floor with room 1.
	const std::array<uint64_t, 5> recent { 1 | HiveRoomsMark, 2 | CavesRoomsMark, 0, 0, 0 };
	EXPECT_EQ(CathedralRoomsToAvoid(recent, false, true, false, true), uint64_t { 1 });
	EXPECT_EQ(CathedralRoomsToAvoid(recent, false, true), uint64_t { 2 });
	EXPECT_EQ(CathedralRoomsToAvoid(recent, false, false, true), uint64_t { 0 });
	SetCavesRooms({});
	SetHiveRooms({});
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

class Poe2HiveFloorsLevelTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		paths::SetPrefPath(paths::BasePath() + "test/fixtures/");
		TestInitGame(true, false, true);
		LoadModArchives(HiveMods);
		SetHiveRooms({});
		Poe2FloorsReset();
		gbIsPoe2 = true;
		GetOptions().Gameplay.newHiveFloors.SetValue(true);
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		SetCathedralFunnelFloor(false);
	}

	void Build(int level, uint32_t seed, lvl_entry entry = ENTRY_MAIN)
	{
		LevelSeeds[level] = std::nullopt;
		currlevel = level;
		leveltype = DTYPE_NEST;
		pMegaTiles = std::make_unique<MegaTile[]>(GetTileCount(leveltype));
		Poe2ApplyFreshFloor(level, false, seed, 1);
		CreateDungeon(seed, entry);
		if (const char *dir = std::getenv("POE2_DUMP_FLOORS")) {
			const std::string name = std::string(dir) + "/hive_" + std::to_string(level) + "_" + std::to_string(seed);
			std::ofstream out(name + ".txt");
			for (int y = 0; y < DMAXY; y++) {
				for (int x = 0; x < DMAXX; x++)
					out << static_cast<int>(dungeon[x][y]) << (x + 1 < DMAXX ? " " : "\n");
			}
		}
	}
};

TEST_F(Poe2HiveFloorsLevelTest, BuildsEveryFloorWithItsStairs)
{
	std::map<std::string, int> roomsSeen;
	int floors = 0;
	int rooms = 0;
	int designed = 0;
	for (int level = 17; level <= 20; level++) {
		for (uint32_t seed = 1; seed <= 30; seed++) {
			Build(level, seed * 7919U + static_cast<uint32_t>(level));
			const std::string where = "level " + std::to_string(level) + " seed " + std::to_string(seed);
			ASSERT_TRUE(IsCathedralFunnelFloor()) << where;
			const CathedralPlan &plan = CurrentCathedralPlan();
			ASSERT_TRUE(plan.hive) << where;
			EXPECT_GE(plan.areas.size(), 10U) << where;
			// Level 17 is entered from town: its stairs up are the stairs to town. Level 20 has no stairs down.
			EXPECT_EQ(CountTile({ 20 }), level == 17 ? 0 : 1) << "stairs up, " << where;
			EXPECT_EQ(CountTile({ 24 }), level == 17 ? 1 : 0) << "stairs to town, " << where;
			EXPECT_EQ(CountTile({ 16 }), level == 20 ? 0 : 1) << "stairs down, " << where;
			for (const CathedralArea &area : plan.areas) {
				if (area.design >= 0) {
					roomsSeen[GetHiveRooms()[area.design].id]++;
					designed++;
				}
			}
			rooms += static_cast<int>(plan.areas.size());
			floors++;
		}
	}
	std::cout << floors << " floors: " << rooms << " rooms, " << designed << " designed\n";
	EXPECT_GE(roomsSeen.size(), GetHiveRooms().size() - 4);
	for (const CathedralRoomDef &room : GetHiveRooms()) {
		if (roomsSeen.count(room.id) == 0)
			std::cout << "never built: " << room.id << "\n";
	}
}

TEST_F(Poe2HiveFloorsLevelTest, DrawsTheSlimePitsGrowthAndColumns)
{
	std::map<char, int> drawn;
	for (uint32_t seed = 1; seed <= 80; seed++) {
		Build(18, seed * 104729U);
		const CathedralPlan &plan = CurrentCathedralPlan();
		Bitset2d<DMAXX, DMAXY> pit;
		for (const CathedralLine &line : plan.lines) {
			if (line.kind == 'S')
				pit.set(line.block.x, line.block.y);
		}
		for (const CathedralLine &line : plan.lines) {
			const uint8_t tile = dungeon[line.block.x][line.block.y];
			const std::string at = std::string(1, line.kind) + " at " + std::to_string(line.block.x) + "," + std::to_string(line.block.y) + " is " + std::to_string(tile);
			switch (line.kind) {
			case 'S':
				// The north-west mark of each pit is Hellfire's first pit tile.
				if (!pit.test(line.block.x - 1, line.block.y) && !pit.test(line.block.x, line.block.y - 1))
					EXPECT_EQ(tile, 107) << at;
				else
					EXPECT_TRUE(IsAnyOf(tile, 115, 131, 123)) << at;
				break;
			case 'w': EXPECT_TRUE(tile >= 29 && tile <= 32) << at; break;
			case 'o': EXPECT_NE(tile, 7) << at; break;
			default: break;
			}
			drawn[line.kind]++;
		}
	}
	for (const char kind : { 'S', 'w', 'o' })
		EXPECT_GT(drawn[kind], 0) << kind;
}

TEST_F(Poe2HiveFloorsLevelTest, BuildingAgainGivesTheSameFloor)
{
	Build(19, 4242);
	std::vector<uint8_t> first(&dungeon[0][0], &dungeon[0][0] + (DMAXX * DMAXY));
	LevelSeeds[19] = std::nullopt;
	CreateDungeon(4242, ENTRY_MAIN);
	std::vector<uint8_t> second(&dungeon[0][0], &dungeon[0][0] + (DMAXX * DMAXY));
	EXPECT_EQ(first, second);
}

TEST_F(Poe2HiveFloorsLevelTest, OldFloorsStayOneSettingAway)
{
	GetOptions().Gameplay.newHiveFloors.SetValue(false);
	Build(18, 99);
	EXPECT_FALSE(IsCathedralFunnelFloor());
	GetOptions().Gameplay.newHiveFloors.SetValue(true);
	LevelSeeds[18] = std::nullopt;
	CreateDungeon(99, ENTRY_MAIN);
	EXPECT_FALSE(IsCathedralFunnelFloor());
	Build(18, 100);
	EXPECT_TRUE(IsCathedralFunnelFloor());
}

TEST_F(Poe2HiveFloorsLevelTest, ComingFromTownStartsAtTheStairs)
{
	Build(17, 5150, ENTRY_TWARPDN);
	const CathedralPlan &plan = CurrentCathedralPlan();
	ASSERT_TRUE(plan.stairsUp);
	const Point stairs = *plan.stairsUp + Displacement { 0, -2 };
	const Point view = stairs.megaToWorld() + Displacement { 1, 3 };
	EXPECT_EQ(ViewPosition, view);
}

/**
 * The tile pairs side by side on the new floors that Hellfire's own Hive floors never make. Those are the places that
 * may look wrong in game; the test prints them so they can be checked there.
 */
TEST_F(Poe2HiveFloorsLevelTest, ReportsTilePairsHellfireNeverMakes)
{
	std::set<std::pair<int, int>> across;
	std::set<std::pair<int, int>> down;
	GetOptions().Gameplay.newHiveFloors.SetValue(false);
	for (int level = 17; level <= 20; level++) {
		for (uint32_t seed = 1; seed <= 40; seed++) {
			Build(level, seed * 31337U + static_cast<uint32_t>(level));
			for (int y = 0; y + 1 < DMAXY; y++) {
				for (int x = 0; x + 1 < DMAXX; x++) {
					across.insert({ dungeon[x][y], dungeon[x + 1][y] });
					down.insert({ dungeon[x][y], dungeon[x][y + 1] });
				}
			}
		}
	}
	GetOptions().Gameplay.newHiveFloors.SetValue(true);
	std::map<std::string, int> unseen;
	for (int level = 17; level <= 20; level++) {
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
		std::cout << "never in Hellfire's Hive: " << pair << " x" << count << "\n";
	SUCCEED();
}

} // namespace
} // namespace devilution
