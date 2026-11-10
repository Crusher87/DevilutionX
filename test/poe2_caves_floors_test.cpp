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

TEST(Poe2CavesFloors, ReadsTheRooms)
{
	SetCavesRooms({});
	paths::SetPrefPath(paths::BasePath() + "test/fixtures/");
	LoadCoreArchives();
	LoadModArchives({ { "poe2" } });
	const std::vector<CathedralRoomDef> &rooms = GetCavesRooms();
	ASSERT_EQ(rooms.size(), 41U);
	EXPECT_EQ(rooms[0].id, "lavalake");
	const auto arrival = std::find_if(rooms.begin(), rooms.end(), [](const CathedralRoomDef &room) { return room.id == "arrival"; });
	ASSERT_NE(arrival, rooms.end());
	EXPECT_EQ(arrival->grid[0], " UUU  ZZZ  ");
}

TEST(Poe2CavesFloors, MarksTurnWithTheirTiles)
{
	// The stalagmite stands on the tile whose north corner is the top left point.
	const std::vector<std::string> grid { "i..", "...", "..." };
	EXPECT_EQ(TurnCavesGrid(grid, 0), grid);
	EXPECT_EQ(TurnCavesGrid(grid, 1), (std::vector<std::string> { ".i.", "...", "..." }));
	EXPECT_EQ(TurnCavesGrid(grid, 2), (std::vector<std::string> { "...", ".i.", "..." }));
	EXPECT_EQ(TurnCavesGrid(grid, 4), (std::vector<std::string> { ".i.", "...", "..." }));
	// Corner points turn as they are.
	EXPECT_EQ(TurnCavesGrid({ "L..", "...", "..o" }, 2), (std::vector<std::string> { "o..", "...", "..L" }));
}

TEST(Poe2CavesFloors, RoomHistoriesStayApart)
{
	std::vector<CathedralRoomDef> rooms(3);
	for (CathedralRoomDef &room : rooms)
		room.grid = { "." };
	SetCathedralRooms(rooms);
	SetCatacombsRooms(rooms);
	SetCavesRooms(rooms);
	// Newest first: a Caves floor with room 0, then a Catacombs floor with room 1 or a Cathedral floor with room 2.
	const std::array<uint64_t, 5> afterCatacombs { 1 | CavesRoomsMark, 2 | CatacombsRoomsMark, 0, 0, 0 };
	EXPECT_EQ(CathedralRoomsToAvoid(afterCatacombs, false, true), uint64_t { 1 });
	EXPECT_EQ(CathedralRoomsToAvoid(afterCatacombs, true), uint64_t { 2 });
	const std::array<uint64_t, 5> afterCathedral { 1 | CavesRoomsMark, 4, 0, 0, 0 };
	EXPECT_EQ(CathedralRoomsToAvoid(afterCathedral), uint64_t { 4 });
	EXPECT_EQ(CathedralRoomsToAvoid(afterCathedral, false, true), uint64_t { 1 });
	SetCathedralRooms({});
	SetCatacombsRooms({});
	SetCavesRooms({});
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

class Poe2CavesFloorsLevelTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		paths::SetPrefPath(paths::BasePath() + "test/fixtures/");
		TestInitGame(true, false);
		LoadModArchives({ { "poe2" } });
		SetCavesRooms({});
		Poe2FloorsReset();
		gbIsPoe2 = true;
		GetOptions().Gameplay.newCavesFloors.SetValue(true);
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
		leveltype = DTYPE_CAVES;
		pMegaTiles = std::make_unique<MegaTile[]>(GetTileCount(leveltype));
		Poe2ApplyFreshFloor(level, false, seed, 1);
		CreateDungeon(seed, ENTRY_MAIN);
		if (const char *dir = std::getenv("POE2_DUMP_FLOORS")) {
			const std::string name = std::string(dir) + "/caves_" + std::to_string(level) + "_" + std::to_string(seed);
			std::ofstream out(name + ".txt");
			for (int y = 0; y < DMAXY; y++) {
				for (int x = 0; x < DMAXX; x++)
					out << static_cast<int>(dungeon[x][y]) << (x + 1 < DMAXX ? " " : "\n");
			}
			std::ofstream areas(name + ".areas");
			for (const CathedralArea &area : CurrentCathedralPlan().areas) {
				const Rectangle in = area.interior();
				areas << static_cast<int>(area.kind) << ' ' << area.step << ' ' << (area.design >= 0 ? GetCavesRooms()[area.design].id : (area.corridor ? "passage" : "-"))
				      << ' ' << area.turn << ' ' << in.position.x << ' ' << in.position.y << ' ' << in.size.width << ' ' << in.size.height << '\n';
			}
		}
	}

	/** The Anvil of Fury on level 10 when withQuest, as the quest table sets it. */
	static void SetQuests(bool withQuest)
	{
		Quests[Q_ANVIL]._qactive = withQuest ? QUEST_INIT : QUEST_NOTAVAIL;
		Quests[Q_ANVIL]._qlevel = 10;
	}
};

TEST_F(Poe2CavesFloorsLevelTest, BuildsEveryFloorWithItsStairsAndTheAnvil)
{
	std::map<std::string, int> roomsSeen;
	int floors = 0;
	int rooms = 0;
	int designed = 0;
	int themes = 0;
	for (int level = 9; level <= 12; level++) {
		for (uint32_t seed = 1; seed <= 30; seed++) {
			// Half the floors with the quest, half without.
			SetQuests(seed % 2 == 0);
			Build(level, seed * 7919U + static_cast<uint32_t>(level));
			const std::string where = "level " + std::to_string(level) + " seed " + std::to_string(seed);
			ASSERT_TRUE(IsCathedralFunnelFloor()) << where;
			const CathedralPlan &plan = CurrentCathedralPlan();
			ASSERT_TRUE(plan.caves) << where;
			EXPECT_GE(plan.areas.size(), 10U) << where;
			EXPECT_EQ(CountTile({ 51 }), 1) << "stairs up, " << where;
			EXPECT_EQ(CountTile({ 47 }), 1) << "stairs down, " << where;
			EXPECT_EQ(CountTile({ 156 }), level == 9 ? 1 : 0) << "stairs to town, " << where;
			if (level == 10 && seed % 2 == 0)
				EXPECT_EQ(SetPiece.size.width, 11) << "the Anvil of Fury, " << where;
			for (const CathedralArea &area : plan.areas) {
				if (area.design >= 0) {
					roomsSeen[GetCavesRooms()[area.design].id]++;
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
	EXPECT_GE(roomsSeen.size(), GetCavesRooms().size() - 4);
	for (const CathedralRoomDef &room : GetCavesRooms()) {
		if (roomsSeen.count(room.id) == 0)
			std::cout << "never built: " << room.id << "\n";
	}
}

TEST_F(Poe2CavesFloorsLevelTest, DrawsTheLavaStreamsFencesAndStalagmites)
{
	std::map<char, int> drawn;
	for (uint32_t seed = 1; seed <= 80; seed++) {
		SetQuests(false);
		Build(11, seed * 104729U);
		for (const CathedralLine &line : CurrentCathedralPlan().lines) {
			const uint8_t tile = dungeon[line.block.x][line.block.y];
			const std::string at = std::string(1, line.kind) + " at " + std::to_string(line.block.x) + "," + std::to_string(line.block.y) + " is " + std::to_string(tile);
			switch (line.kind) {
			case 'l': EXPECT_TRUE(tile >= 15 && tile <= 22) << at; break;
			case 'H': EXPECT_TRUE(tile == 44 || tile == 45) << at; break;
			case 'f': EXPECT_TRUE(tile >= 134 && tile <= 138) << at; break;
			case 'g': EXPECT_TRUE(tile == 146 || tile == 147) << at; break;
			case 'i': EXPECT_TRUE(IsAnyOf(tile, 52, 53, 54, 67)) << at; break;
			case 'T': EXPECT_TRUE(tile >= 55 && tile <= 66) << at; break;
			case 'L': {
				// The tile south-east of a lava point is lava, or the mouth of a stream.
				EXPECT_TRUE(tile >= 25 && tile <= 41) << at;
				break;
			}
			case 'o': EXPECT_NE(tile, 7) << at; break;
			default: break;
			}
			drawn[line.kind]++;
		}
	}
	for (const char kind : { 'l', 'H', 'f', 'g', 'i', 'T', 'L' })
		EXPECT_GT(drawn[kind], 0) << kind;
}

TEST_F(Poe2CavesFloorsLevelTest, BuildingAgainGivesTheSameFloor)
{
	SetQuests(true);
	Build(10, 4242);
	std::vector<uint8_t> first(&dungeon[0][0], &dungeon[0][0] + (DMAXX * DMAXY));
	LevelSeeds[10] = std::nullopt;
	CreateDungeon(4242, ENTRY_MAIN);
	std::vector<uint8_t> second(&dungeon[0][0], &dungeon[0][0] + (DMAXX * DMAXY));
	EXPECT_EQ(first, second);
}

TEST_F(Poe2CavesFloorsLevelTest, OldFloorsStayOneSettingAway)
{
	SetQuests(false);
	GetOptions().Gameplay.newCavesFloors.SetValue(false);
	Build(9, 99);
	EXPECT_FALSE(IsCathedralFunnelFloor());
	GetOptions().Gameplay.newCavesFloors.SetValue(true);
	LevelSeeds[9] = std::nullopt;
	CreateDungeon(99, ENTRY_MAIN);
	EXPECT_FALSE(IsCathedralFunnelFloor());
	Build(9, 100);
	EXPECT_TRUE(IsCathedralFunnelFloor());
}

/**
 * The tile pairs side by side on the new floors that Diablo's own Caves floors never make. Those are the places that
 * may look wrong in game; the test prints them so they can be checked there.
 */
TEST_F(Poe2CavesFloorsLevelTest, ReportsTilePairsDiabloNeverMakes)
{
	std::set<std::pair<int, int>> across;
	std::set<std::pair<int, int>> down;
	SetQuests(false);
	GetOptions().Gameplay.newCavesFloors.SetValue(false);
	for (int level = 9; level <= 12; level++) {
		for (uint32_t seed = 1; seed <= 80; seed++) {
			Build(level, seed * 31337U + static_cast<uint32_t>(level));
			for (int y = 0; y + 1 < DMAXY; y++) {
				for (int x = 0; x + 1 < DMAXX; x++) {
					across.insert({ dungeon[x][y], dungeon[x + 1][y] });
					down.insert({ dungeon[x][y], dungeon[x][y + 1] });
				}
			}
		}
	}
	GetOptions().Gameplay.newCavesFloors.SetValue(true);
	std::map<std::string, int> unseen;
	for (int level = 9; level <= 12; level++) {
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
		std::cout << "never in Diablo's caves: " << pair << " x" << count << "\n";
	SUCCEED();
}

} // namespace
} // namespace devilution
