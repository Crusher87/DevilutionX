#include <gtest/gtest.h>

#include <cstdlib>
#include <deque>
#include <fstream>
#include <string>

#include "drlg_test.hpp"
#include "game_mode.hpp"
#include "levels/gendung.h"
#include "options.h"
#include "poe2/cathedral_floors.hpp"
#include "poe2/floors.hpp"

namespace devilution {
namespace {

TEST(Poe2CathedralFloors, ReadsAGrid)
{
	std::vector<std::string> grid;
	ASSERT_TRUE(ParseCathedralRoomGrid("_.E/.O./_.._", grid) == false);
	ASSERT_TRUE(ParseCathedralRoomGrid("_.E/.O./_..", grid));
	ASSERT_EQ(grid.size(), 3U);
	EXPECT_EQ(grid[0], " .E");
	EXPECT_EQ(grid[2], " ..");
}

TEST(Poe2CathedralFloors, TurnsAGrid)
{
	const std::vector<std::string> grid { "ab", "cd", "ef" };
	EXPECT_EQ(TurnCathedralGrid(grid, 0), grid);
	EXPECT_EQ(TurnCathedralGrid(grid, 1), (std::vector<std::string> { "eca", "fdb" }));
	EXPECT_EQ(TurnCathedralGrid(grid, 2), (std::vector<std::string> { "fe", "dc", "ba" }));
	EXPECT_EQ(TurnCathedralGrid(grid, 4), (std::vector<std::string> { "ba", "dc", "fe" }));
}

TEST(Poe2CathedralFloors, RoomsWaitTheirTurn)
{
	std::vector<CathedralRoomDef> rooms(3);
	rooms[0].id = "plain";
	rooms[1].id = "memorable";
	rooms[1].memorable = true;
	rooms[2].id = "other";
	for (CathedralRoomDef &room : rooms)
		room.grid = { "." };
	SetCathedralRooms(rooms);
	// Newest first: floor -1 used room 2, floor -3 used rooms 0 and 1.
	const std::array<uint64_t, 5> recent { 4, 0, 3, 0, 0 };
	EXPECT_EQ(CathedralRoomsToAvoid(recent), uint64_t { 4 | 2 });
}

bool HasTile(uint8_t tile)
{
	for (int y = 0; y < DMAXY; y++) {
		for (int x = 0; x < DMAXX; x++) {
			if (dungeon[x][y] == tile)
				return true;
		}
	}
	return false;
}

class Poe2CathedralFloorsLevelTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		paths::SetPrefPath(paths::BasePath() + "test/fixtures/");
		TestInitGame(true, false);
		LoadModArchives({ { "poe2" } });
		SetCathedralRooms({});
		Poe2FloorsReset();
		gbIsPoe2 = true;
		GetOptions().Gameplay.newCathedralFloors.SetValue(true);
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
		leveltype = DTYPE_CATHEDRAL;
		pMegaTiles = std::make_unique<MegaTile[]>(GetTileCount(leveltype));
		Poe2ApplyFreshFloor(level, false, seed, 1);
		CreateDungeon(seed, ENTRY_MAIN);
		if (const char *dir = std::getenv("POE2_DUMP_FLOORS")) {
			std::ofstream out(std::string(dir) + "/floor_" + std::to_string(level) + "_" + std::to_string(seed) + ".txt");
			for (int y = 0; y < DMAXY; y++) {
				for (int x = 0; x < DMAXX; x++)
					out << static_cast<int>(dungeon[x][y]) << (x + 1 < DMAXX ? " " : "\n");
			}
			const CathedralPlan &plan = CurrentCathedralPlan();
			std::ofstream areas(std::string(dir) + "/floor_" + std::to_string(level) + "_" + std::to_string(seed) + ".areas");
			for (const CathedralArea &area : plan.areas) {
				const Rectangle in = area.interior();
				areas << static_cast<int>(area.kind) << ' ' << area.step << ' ' << (area.design >= 0 ? GetCathedralRooms()[area.design].id : (area.corridor ? "passage" : "-"))
				      << ' ' << in.position.x << ' ' << in.position.y << ' ' << in.size.width << ' ' << in.size.height << '\n';
			}
		}
	}
};

TEST_F(Poe2CathedralFloorsLevelTest, BuildsEveryFloorWithItsStairs)
{
	for (int level = 1; level <= 4; level++) {
		for (uint32_t seed = 1; seed <= 25; seed++) {
			Build(level, seed * 7919U + static_cast<uint32_t>(level));
			ASSERT_TRUE(IsCathedralFunnelFloor()) << "level " << level << " seed " << seed;
			const CathedralPlan &plan = CurrentCathedralPlan();
			EXPECT_GE(plan.areas.size(), 10U);
			EXPECT_TRUE(HasTile(64)) << "no stairs up, level " << level << " seed " << seed;
			if (Quests[Q_LTBANNER].IsAvailable() && level == Quests[Q_LTBANNER]._qlevel)
				EXPECT_TRUE(SetPiece.size.width > 0);
			else
				EXPECT_TRUE(HasTile(57)) << "no stairs down, level " << level << " seed " << seed;
		}
	}
}

TEST_F(Poe2CathedralFloorsLevelTest, BuildingAgainGivesTheSameFloor)
{
	Build(2, 4242);
	std::vector<uint8_t> first(&dungeon[0][0], &dungeon[0][0] + (DMAXX * DMAXY));
	// A kept floor is built again from its seed: no fresh roll, so the same rooms are left out.
	LevelSeeds[2] = std::nullopt;
	CreateDungeon(4242, ENTRY_MAIN);
	std::vector<uint8_t> second(&dungeon[0][0], &dungeon[0][0] + (DMAXX * DMAXY));
	EXPECT_EQ(first, second);
}

TEST_F(Poe2CathedralFloorsLevelTest, OldFloorsStayOneSettingAway)
{
	GetOptions().Gameplay.newCathedralFloors.SetValue(false);
	Build(1, 99);
	EXPECT_FALSE(IsCathedralFunnelFloor());
	GetOptions().Gameplay.newCathedralFloors.SetValue(true);
	// The floor keeps the builder it was made with until it is built fresh.
	LevelSeeds[1] = std::nullopt;
	CreateDungeon(99, ENTRY_MAIN);
	EXPECT_FALSE(IsCathedralFunnelFloor());
	Build(1, 100);
	EXPECT_TRUE(IsCathedralFunnelFloor());
}

} // namespace
} // namespace devilution
