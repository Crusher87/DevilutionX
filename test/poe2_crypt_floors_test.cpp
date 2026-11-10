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

constexpr std::string_view CryptMods[] = { "hf", "poe2" };

/** Hellfire's Crypt tiles the tests look for (levels/crypt.cpp). */
constexpr uint8_t StairsUpTile = 54;
constexpr uint8_t TownStairsTile = 62;
constexpr uint8_t StairsDownTile = 52;
constexpr uint8_t NaKrulGateTile = 103;
constexpr uint8_t CryptStarTile = 172;

TEST(Poe2CryptFloors, ReadsTheRooms)
{
	SetCryptRooms({});
	paths::SetPrefPath(paths::BasePath() + "test/fixtures/");
	LoadCoreArchives();
	LoadModArchives(CryptMods);
	const std::vector<CathedralRoomDef> &rooms = GetCryptRooms();
	ASSERT_EQ(rooms.size(), 40U);
	EXPECT_EQ(rooms[0].id, "burialnave");
	// Curtis's three changes of 2026-11-10: more kinds of decoration in each.
	const auto find = [&rooms](std::string_view id) { return std::find_if(rooms.begin(), rooms.end(), [id](const CathedralRoomDef &room) { return room.id == id; }); };
	const auto niches = find("niches");
	ASSERT_NE(niches, rooms.end());
	EXPECT_EQ(niches->grid[0], "P#r#1#c#2#s");
	const auto weeping = find("weeping");
	ASSERT_NE(weeping, rooms.end());
	EXPECT_EQ(weeping->options.size(), 2U);
	const auto crucified = find("crucified");
	ASSERT_NE(crucified, rooms.end());
	EXPECT_EQ(crucified->options.size(), 3U);
	SetCryptRooms({});
}

TEST(Poe2CryptFloors, RoomHistoriesStayApart)
{
	std::vector<CathedralRoomDef> rooms(3);
	for (CathedralRoomDef &room : rooms)
		room.grid = { "." };
	SetCathedralRooms(rooms);
	SetCryptRooms(rooms);
	// Newest first: a Crypt floor with room 0, then a Cathedral floor with room 1.
	const std::array<uint64_t, 5> recent { 1 | CryptRoomsMark, 2, 0, 0, 0 };
	EXPECT_EQ(CathedralRoomsToAvoid(recent, false, false, false, false, true), uint64_t { 1 });
	EXPECT_EQ(CathedralRoomsToAvoid(recent), uint64_t { 2 });
	SetCathedralRooms({});
	SetCryptRooms({});
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

class Poe2CryptFloorsLevelTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		paths::SetPrefPath(paths::BasePath() + "test/fixtures/");
		TestInitGame(true, false, true);
		LoadModArchives(CryptMods);
		SetCryptRooms({});
		Poe2FloorsReset();
		gbIsPoe2 = true;
		GetOptions().Gameplay.newCryptFloors.SetValue(true);
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
		leveltype = DTYPE_CRYPT;
		pMegaTiles = std::make_unique<MegaTile[]>(GetTileCount(leveltype));
		Poe2ApplyFreshFloor(level, false, seed, 1);
		CreateDungeon(seed, entry);
		if (const char *dir = std::getenv("POE2_DUMP_FLOORS")) {
			const std::string name = std::string(dir) + "/crypt_" + std::to_string(level) + "_" + std::to_string(seed);
			std::ofstream out(name + ".txt");
			for (int y = 0; y < DMAXY; y++) {
				for (int x = 0; x < DMAXX; x++)
					out << static_cast<int>(dungeon[x][y]) << (x + 1 < DMAXX ? " " : "\n");
			}
		}
	}
};

TEST_F(Poe2CryptFloorsLevelTest, BuildsEveryFloorWithItsStairsAndQuestRooms)
{
	std::map<std::string, int> roomsSeen;
	int floors = 0;
	int rooms = 0;
	int designed = 0;
	for (int level = 21; level <= 24; level++) {
		for (uint32_t seed = 1; seed <= 30; seed++) {
			Build(level, seed * 7919U + static_cast<uint32_t>(level));
			const std::string where = "level " + std::to_string(level) + " seed " + std::to_string(seed);
			ASSERT_TRUE(IsCathedralFunnelFloor()) << where;
			const CathedralPlan &plan = CurrentCathedralPlan();
			ASSERT_TRUE(plan.crypt) << where;
			EXPECT_GE(plan.areas.size(), 10U) << where;
			// Level 21 is entered from town: its stairs up are the stairs to town. Level 24 has no stairs down.
			EXPECT_EQ(CountTile({ StairsUpTile }), level == 21 ? 0 : 1) << "stairs up, " << where;
			EXPECT_EQ(CountTile({ TownStairsTile }), level == 21 ? 1 : 0) << "stairs to town, " << where;
			EXPECT_EQ(CountTile({ StairsDownTile }), level == 24 ? 0 : 1) << "stairs down, " << where;
			// Na-Krul's prison on level 24 and the Cornerstone's room on level 21, each in a hall of its own.
			EXPECT_EQ(CountTile({ NaKrulGateTile }), level == 24 ? 1 : 0) << "Na-Krul's gate, " << where;
			if (level == 24 || level == 21) {
				ASSERT_TRUE(plan.questPiece) << where;
				const WorldTileSize size = level == 24 ? WorldTileSize(4, 6) : WorldTileSize(5, 5);
				EXPECT_EQ(SetPiece.size, size) << where;
				EXPECT_TRUE(std::any_of(plan.areas.begin(), plan.areas.end(), [](const CathedralArea &area) { return area.kind == CathedralAreaKind::QuestPiece; })) << where;
			} else {
				EXPECT_FALSE(plan.questPiece) << where;
			}
			if (level == 24)
				EXPECT_FALSE(plan.stairsDown) << where;
			for (const CathedralArea &area : plan.areas) {
				if (area.design >= 0) {
					roomsSeen[GetCryptRooms()[area.design].id]++;
					designed++;
				}
			}
			rooms += static_cast<int>(plan.areas.size());
			floors++;
		}
	}
	std::cout << floors << " floors: " << rooms << " rooms, " << designed << " designed\n";
	EXPECT_GE(roomsSeen.size(), GetCryptRooms().size() - 4);
	for (const CathedralRoomDef &room : GetCryptRooms()) {
		if (roomsSeen.count(room.id) == 0)
			std::cout << "never built: " << room.id << "\n";
	}
}

TEST_F(Poe2CryptFloorsLevelTest, DrawsTheCarvingsBloodAndStars)
{
	std::map<char, int> marks;
	std::map<char, int> drawn;
	for (uint32_t seed = 1; seed <= 80; seed++) {
		Build(22, seed * 104729U);
		const CathedralPlan &plan = CurrentCathedralPlan();
		for (const CathedralProp &prop : plan.props) {
			const Point b = prop.block;
			const uint8_t north = dungeon[b.x][b.y - 1];
			const uint8_t west = dungeon[b.x - 1][b.y];
			const std::string at = std::string(1, prop.kind) + " at " + std::to_string(b.x) + "," + std::to_string(b.y);
			switch (prop.kind) {
			case '+':
				marks['+']++;
				if (dungeon[b.x][b.y] == CryptStarTile)
					drawn['+']++;
				break;
			case 'Q':
				marks['Q']++;
				if (IsAnyOf(north, 199, 200, 201, 202) || IsAnyOf(west, 199, 200, 201, 202))
					drawn['Q']++;
				break;
			case 'B':
				marks['B']++;
				// The leaking walls are Hellfire's tiles 146 to 157 and its leaking pillars 160 to 162.
				if ((north >= 146 && north <= 162) || (west >= 146 && west <= 162))
					drawn['B']++;
				break;
			default:
				// No random pillar or star lands on a designed room's prop.
				EXPECT_FALSE(IsAnyOf(dungeon[b.x][b.y], 167, 168, 169, 170, 171, CryptStarTile)) << at;
				break;
			}
		}
	}
	for (const char kind : { '+', 'Q', 'B' }) {
		std::cout << kind << ": " << drawn[kind] << " of " << marks[kind] << " drawn\n";
		EXPECT_GT(marks[kind], 0) << kind;
		// Almost every mark has its wall or floor. A carving needs a plain wall block: beside a doorway the wall behind
		// is a wall end, which has no carved tile, and the wall is left plain.
		EXPECT_GE(drawn[kind] * 4, marks[kind] * 3) << kind;
	}
}

TEST_F(Poe2CryptFloorsLevelTest, BuildingAgainGivesTheSameFloor)
{
	Build(23, 4242);
	std::vector<uint8_t> first(&dungeon[0][0], &dungeon[0][0] + (DMAXX * DMAXY));
	LevelSeeds[23] = std::nullopt;
	CreateDungeon(4242, ENTRY_MAIN);
	std::vector<uint8_t> second(&dungeon[0][0], &dungeon[0][0] + (DMAXX * DMAXY));
	EXPECT_EQ(first, second);
}

TEST_F(Poe2CryptFloorsLevelTest, OldFloorsStayOneSettingAway)
{
	GetOptions().Gameplay.newCryptFloors.SetValue(false);
	Build(22, 99);
	EXPECT_FALSE(IsCathedralFunnelFloor());
	GetOptions().Gameplay.newCryptFloors.SetValue(true);
	LevelSeeds[22] = std::nullopt;
	CreateDungeon(99, ENTRY_MAIN);
	EXPECT_FALSE(IsCathedralFunnelFloor());
	Build(22, 100);
	EXPECT_TRUE(IsCathedralFunnelFloor());
}

TEST_F(Poe2CryptFloorsLevelTest, ComingFromTownStartsAtTheStairs)
{
	Build(21, 5150, ENTRY_TWARPDN);
	const CathedralPlan &plan = CurrentCathedralPlan();
	ASSERT_TRUE(plan.stairsUp);
	const Point stairs = *plan.stairsUp + Displacement { 0, -3 };
	const Point view = stairs.megaToWorld() + Displacement { 3, 5 };
	EXPECT_EQ(ViewPosition, view);
}

/**
 * The tile pairs side by side on the new floors that Hellfire's own Crypt floors never make. Those are the places that
 * may look wrong in game; the test prints them so they can be checked there.
 */
TEST_F(Poe2CryptFloorsLevelTest, ReportsTilePairsHellfireNeverMakes)
{
	std::set<std::pair<int, int>> across;
	std::set<std::pair<int, int>> down;
	GetOptions().Gameplay.newCryptFloors.SetValue(false);
	for (int level = 21; level <= 24; level++) {
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
	GetOptions().Gameplay.newCryptFloors.SetValue(true);
	std::map<std::string, int> unseen;
	for (int level = 21; level <= 24; level++) {
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
		std::cout << "never in Hellfire's Crypt: " << pair << " x" << count << "\n";
	SUCCEED();
}

} // namespace
} // namespace devilution
