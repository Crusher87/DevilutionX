/**
 * @file reforged_floors_test.cpp
 *
 * Sanctuary Reforged takes POE2 Mashup's designed floor layouts and nothing that fills them: the floors of every area are
 * built by the designed-room builders while their New ... Floors setting is on, and Diablo's and Hellfire's own placement
 * puts the monsters, objects, shrines and theme rooms on them. No strongbox, bound demon or POE2 chest comes with them.
 */
#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

#include "drlg_test.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "levels/themes.h"
#include "levels/tile_properties.hpp"
#include "levels/trigs.h"
#include "monster.h"
#include "objects.h"
#include "options.h"
#include "poe2/bound_demons.hpp"
#include "poe2/cathedral_floors.hpp"
#include "poe2/floor_chests.hpp"
#include "poe2/floors.hpp"
#include "poe2/monster_mods.hpp"
#include "tables/itemdat.h"
#include "tables/misdat.h"
#include "tables/monstdat.h"
#include "tables/objdat.h"
#include "tables/spelldat.h"
#include "utils/str_cat.hpp"

namespace devilution {
namespace {

constexpr std::string_view ReforgedMods[] = { "hf", "reforged" };
constexpr std::string_view Poe2Mods[] = { "hf", "poe2" };

OptionEntryBoolean &FloorsSetting(dungeon_type type)
{
	GameplayOptions &gameplay = GetOptions().Gameplay;
	switch (type) {
	case DTYPE_CATACOMBS: return gameplay.newCatacombsFloors;
	case DTYPE_CAVES: return gameplay.newCavesFloors;
	case DTYPE_HELL: return gameplay.newHellFloors;
	case DTYPE_NEST: return gameplay.newHiveFloors;
	case DTYPE_CRYPT: return gameplay.newCryptFloors;
	default: return gameplay.newCathedralFloors;
	}
}

void ForgetRoomTables()
{
	SetCathedralRooms({});
	SetCatacombsRooms({});
	SetCavesRooms({});
	SetHellRooms({});
	SetHiveRooms({});
	SetCryptRooms({});
}

using Tiles = std::array<uint8_t, DMAXX * DMAXY>;

/** Build with the Cathedral's real tiles, so the walls are solid where the game has them (needs the game's data). */
bool UseCathedralTiles = false;

Tiles CopyTiles()
{
	Tiles tiles;
	for (int y = 0; y < DMAXY; y++) {
		for (int x = 0; x < DMAXX; x++)
			tiles[(y * DMAXX) + x] = dungeon[x][y];
	}
	return tiles;
}

class ReforgedFloorsTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		paths::SetPrefPath(paths::BasePath() + "test/fixtures/");
		TestInitGame(true, false, true);
		UnloadModArchives();
		LoadModArchives(ReforgedMods);
		ForgetRoomTables();
		Poe2FloorsReset();
		gbIsPoe2 = false;
		gbIsReforged = true;
		gbIsMultiplayer = false;
		setlevel = false;
		std::fill(std::begin(MyPlayer->_pLvlVisited), std::end(MyPlayer->_pLvlVisited), false);
		for (const dungeon_type type : { DTYPE_CATHEDRAL, DTYPE_CATACOMBS, DTYPE_CAVES, DTYPE_HELL, DTYPE_NEST, DTYPE_CRYPT })
			FloorsSetting(type).SetValue(true);
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		gbIsReforged = false;
		gbIsMultiplayer = false;
		std::fill(std::begin(MyPlayer->_pLvlVisited), std::end(MyPlayer->_pLvlVisited), false);
		for (const dungeon_type type : { DTYPE_CATHEDRAL, DTYPE_CATACOMBS, DTYPE_CAVES, DTYPE_HELL, DTYPE_NEST, DTYPE_CRYPT })
			FloorsSetting(type).SetValue(true);
		SetCathedralFunnelFloor(false);
		ForgetRoomTables();
		Poe2FloorsReset();
	}

	/** Builds a floor the way a first visit does: a fresh build, so the floor's setting decides its builder. */
	static Tiles BuildFresh(int level, uint32_t seed)
	{
		Poe2FloorsReset();
		Poe2ApplyFreshFloor(level, false, seed, 1);
		return Build(level, seed);
	}

	/** Builds the floor again from its seed, as entering a saved floor does: its build stays what it was settled on. */
	static Tiles Build(int level, uint32_t seed)
	{
		LevelSeeds[level] = std::nullopt;
		currlevel = static_cast<uint8_t>(level);
		leveltype = GetLevelType(level);
		if (UseCathedralTiles && leveltype == DTYPE_CATHEDRAL)
			pMegaTiles = LoadFileInMem<MegaTile>("levels\\l1data\\l1.til");
		else
			pMegaTiles = std::make_unique<MegaTile[]>(GetTileCount(leveltype));
		CreateDungeon(seed, ENTRY_MAIN);
		return CopyTiles();
	}

	/** The same floor in plain Hellfire, which never uses the designed rooms. */
	static Tiles BuildHellfire(int level, uint32_t seed)
	{
		gbIsReforged = false;
		const Tiles tiles = BuildFresh(level, seed);
		EXPECT_FALSE(IsCathedralFunnelFloor());
		gbIsReforged = true;
		return tiles;
	}

	/**
	 * The same floor in POE2 Mashup, from POE2's own rooms (Reforged's are copies of them without the reward column, which
	 * no layout reads).
	 */
	static Tiles BuildPoe2(int level, uint32_t seed)
	{
		gbIsReforged = false;
		gbIsPoe2 = true;
		UnloadModArchives();
		LoadModArchives(Poe2Mods);
		const Tiles tiles = BuildFresh(level, seed);
		EXPECT_TRUE(IsCathedralFunnelFloor());
		gbIsPoe2 = false;
		gbIsReforged = true;
		UnloadModArchives();
		LoadModArchives(ReforgedMods);
		return tiles;
	}

	/** POE2 Mashup's rooms of a dungeon, as a Reforged build from before Reforged had its own copies read them. */
	static std::vector<CathedralRoomDef> Poe2Rooms(RoomCollection collection)
	{
		gbIsReforged = false;
		gbIsPoe2 = true;
		UnloadModArchives();
		LoadModArchives(Poe2Mods);
		ForgetRoomTables();
		std::vector<CathedralRoomDef> rooms = ShippedRoomCollection(collection);
		ForgetRoomTables();
		gbIsPoe2 = false;
		gbIsReforged = true;
		UnloadModArchives();
		LoadModArchives(ReforgedMods);
		return rooms;
	}

	static void SetRoomCollection(RoomCollection collection, std::vector<CathedralRoomDef> rooms)
	{
		switch (collection) {
		case RoomCollection::Catacombs: SetCatacombsRooms(std::move(rooms)); break;
		case RoomCollection::Caves: SetCavesRooms(std::move(rooms)); break;
		case RoomCollection::Hell: SetHellRooms(std::move(rooms)); break;
		case RoomCollection::Hive: SetHiveRooms(std::move(rooms)); break;
		case RoomCollection::Crypt: SetCryptRooms(std::move(rooms)); break;
		default: SetCathedralRooms(std::move(rooms)); break;
		}
	}
};

struct Area {
	const char *name;
	int first;
	int last;
};

constexpr Area Areas[] = {
	{ "Cathedral", 1, 4 },
	{ "Catacombs", 5, 8 },
	{ "Caves", 9, 12 },
	{ "Hell", 13, 15 },
	{ "Hive", 17, 20 },
	{ "Crypt", 21, 24 },
};

TEST_F(ReforgedFloorsTest, EveryAreaIsBuiltFromItsDesignedRooms)
{
	for (const Area &area : Areas) {
		for (int level = area.first; level <= area.last; level++) {
			for (uint32_t n = 1; n <= 3; n++) {
				const uint32_t seed = (n * 7919U) + static_cast<uint32_t>(level);
				const std::string where = StrCat(area.name, " level ", level, " seed ", seed);
				const Tiles reforged = BuildFresh(level, seed);
				ASSERT_TRUE(IsCathedralFunnelFloor()) << where;
				const CathedralPlan &plan = CurrentCathedralPlan();
				EXPECT_GE(plan.areas.size(), 8U) << where;
				EXPECT_TRUE(plan.stairsUp.has_value() || level == 17 || level == 21) << where;
				const dungeon_type type = GetLevelType(level);
				EXPECT_EQ(plan.catacombs, type == DTYPE_CATACOMBS) << where;
				EXPECT_EQ(plan.caves, IsAnyOf(type, DTYPE_CAVES, DTYPE_NEST)) << where;
				EXPECT_EQ(plan.hell, type == DTYPE_HELL) << where;
				EXPECT_EQ(plan.hive, type == DTYPE_NEST) << where;
				EXPECT_EQ(plan.crypt, type == DTYPE_CRYPT) << where;
				// The very floor POE2 Mashup builds from the same seed: the layout is the designed builder's, unchanged.
				EXPECT_EQ(reforged, BuildPoe2(level, seed)) << where;
				// And not the floor Hellfire builds.
				EXPECT_NE(reforged, BuildHellfire(level, seed)) << where;
			}
		}
	}
}

TEST_F(ReforgedFloorsTest, WithItsSettingOffAnAreaIsBuiltAsHellfireBuildsIt)
{
	for (const Area &area : Areas) {
		FloorsSetting(GetLevelType(area.first)).SetValue(false);
		for (int level = area.first; level <= area.last; level++) {
			const uint32_t seed = 104729U + static_cast<uint32_t>(level);
			const std::string where = StrCat(area.name, " level ", level);
			const Tiles reforged = BuildFresh(level, seed);
			EXPECT_FALSE(IsCathedralFunnelFloor()) << where;
			EXPECT_EQ(reforged, BuildHellfire(level, seed)) << where;
		}
	}
}

TEST_F(ReforgedFloorsTest, DiablosFloorStaysHellfires)
{
	const Tiles reforged = BuildFresh(16, 4242);
	EXPECT_FALSE(IsCathedralFunnelFloor());
	EXPECT_EQ(reforged, BuildHellfire(16, 4242));
}

TEST_F(ReforgedFloorsTest, MultiplayerFloorsStayHellfires)
{
	gbIsMultiplayer = true;
	for (const int level : { 2, 6, 10, 14, 18, 22 }) {
		const Tiles reforged = BuildFresh(level, 31337);
		EXPECT_FALSE(IsCathedralFunnelFloor()) << level;
		EXPECT_EQ(reforged, BuildHellfire(level, 31337)) << level;
	}
}

TEST_F(ReforgedFloorsTest, AFloorVisitedBeforeTheLayoutsCameStaysHellfires)
{
	// A hero saved before Reforged had the layouts has its floor's monsters and objects in the save; built again from its
	// seed the floor has to come out as it was, so it keeps Hellfire's layout whatever the setting says.
	const Tiles hellfire = BuildHellfire(3, 777);
	Poe2FloorsReset();
	MyPlayer->_pLvlVisited[3] = true;
	const Tiles reforged = Build(3, 777);
	EXPECT_FALSE(IsCathedralFunnelFloor());
	EXPECT_EQ(reforged, hellfire);
}

TEST_F(ReforgedFloorsTest, AFloorKeepsItsLayoutWhenTheSettingChanges)
{
	const Tiles first = BuildFresh(2, 2024);
	ASSERT_TRUE(IsCathedralFunnelFloor());
	MyPlayer->_pLvlVisited[2] = true;
	FloorsSetting(DTYPE_CATHEDRAL).SetValue(false);
	EXPECT_EQ(Build(2, 2024), first) << "built again from its seed, as entering the saved floor does";
	EXPECT_TRUE(IsCathedralFunnelFloor());
}

TEST_F(ReforgedFloorsTest, FloorsAreNotBuiltFreshOnLaterVisits)
{
	currlevel = 2;
	leveltype = DTYPE_CATHEDRAL;
	MyPlayer->_pLvlVisited[2] = true;
	Poe2FloorSaved(2, false);
	const uint32_t seed = DungeonSeeds[2];
	// Far longer away than POE2 Mashup keeps a floor.
	for (uint32_t tick = 0; tick <= FloorKeepTicks * 2; tick++)
		Poe2FloorsTick();
	Poe2BeginFloorVisit(false, ENTRY_MAIN);
	EXPECT_FALSE(Poe2FloorFresh);
	EXPECT_EQ(DungeonSeeds[2], seed);
	// After loading a saved game too.
	Poe2FloorsAfterLoadGame();
	currlevel = 2;
	Poe2BeginFloorVisit(false, ENTRY_MAIN);
	EXPECT_FALSE(Poe2FloorFresh);
	EXPECT_EQ(DungeonSeeds[2], seed);
}

/** An update that changes the rooms: one room fewer, and another room's grid turned round. */
std::vector<CathedralRoomDef> UpdatedRooms(std::vector<CathedralRoomDef> rooms)
{
	rooms.pop_back();
	rooms.front().grid = TurnCathedralGrid(rooms.front().grid, 2);
	return rooms;
}

TEST_F(ReforgedFloorsTest, AFloorKeepsItsLayoutWhenAnUpdateChangesItsRooms)
{
	const Tiles saved = BuildFresh(2, 555);
	ASSERT_TRUE(IsCathedralFunnelFloor());
	const size_t roomCount = GetCathedralRooms().size();
	MyPlayer->_pLvlVisited[2] = true;
	Poe2FloorSaved(2, false);
	// What the save keeps beside the floor records.
	const std::vector<uint8_t> floors = Poe2PackFloors();
	const std::vector<uint8_t> rooms = PackKeptFloorRooms();
	const uint32_t seed = DungeonSeeds[2];

	// The game is updated and the save loaded again.
	SetCathedralRooms(UpdatedRooms(GetCathedralRooms()));
	ASSERT_TRUE(Poe2UnpackFloors(floors));
	UnpackKeptFloorRooms(rooms);
	Poe2FloorsAfterLoadGame();
	Poe2FloorsTick();
	EXPECT_FALSE(Poe2ForceFreshFloor) << "the floor the hero stands on isn't built anew";
	Poe2BeginFloorVisit(false, ENTRY_MAIN);
	EXPECT_FALSE(Poe2FloorFresh) << "nor when the hero comes back to it";
	EXPECT_EQ(DungeonSeeds[2], seed);
	EXPECT_EQ(Build(2, seed), saved) << "built from the rooms it was saved with";
	EXPECT_TRUE(IsCathedralFunnelFloor());
	EXPECT_EQ(GetCathedralRooms().size(), roomCount) << "while the floor is played";

	// Saved again, it still has its rooms; a new floor is built from today's.
	const std::vector<uint8_t> again = PackKeptFloorRooms();
	EXPECT_EQ(again, rooms);
	MyPlayer->_pLvlVisited[3] = false;
	currlevel = 3;
	leveltype = DTYPE_CATHEDRAL;
	Poe2ApplyFreshFloor(3, false, 777, 1);
	Poe2FloorBuildFor(3);
	EXPECT_EQ(GetCathedralRooms().size(), roomCount - 1);
}

TEST_F(ReforgedFloorsTest, AFloorWhoseRoomsTheSaveLacksIsStillNotBuiltAnew)
{
	BuildFresh(2, 555);
	ASSERT_TRUE(IsCathedralFunnelFloor());
	MyPlayer->_pLvlVisited[2] = true;
	Poe2FloorSaved(2, false);
	const std::vector<uint8_t> floors = Poe2PackFloors();
	const uint32_t seed = DungeonSeeds[2];

	SetCathedralRooms(UpdatedRooms(GetCathedralRooms()));
	ASSERT_TRUE(Poe2UnpackFloors(floors));
	UnpackKeptFloorRooms({});
	Poe2FloorsAfterLoadGame();
	Poe2FloorsTick();
	EXPECT_FALSE(Poe2ForceFreshFloor);
	Poe2BeginFloorVisit(false, ENTRY_MAIN);
	EXPECT_FALSE(Poe2FloorFresh) << "its monsters, objects and items are loaded as saved, as Diablo loads a visited floor";
	EXPECT_EQ(DungeonSeeds[2], seed);
	Build(2, seed);
	EXPECT_TRUE(IsCathedralFunnelFloor()) << "on today's rooms";
	EXPECT_FALSE(PackKeptFloorRooms().size() <= 2) << "which the save now keeps for it";
}

TEST_F(ReforgedFloorsTest, FloorsSavedWithPoe2sRoomsKeepTheirLayouts)
{
	// Reforged heroes saved before Reforged had its own rooms have floors built from POE2 Mashup's copy, with its reward
	// column. Without the column, the same seed builds the same floor.
	for (const Area &area : Areas) {
		const RoomCollection collection = RoomCollectionOf(GetLevelType(area.first));
		const std::vector<CathedralRoomDef> poe2Rooms = Poe2Rooms(collection);
		ASSERT_FALSE(poe2Rooms.empty()) << area.name;
		for (int level = area.first; level <= area.last; level++) {
			for (uint32_t n = 1; n <= 6; n++) {
				const uint32_t seed = (n * 104723U) + static_cast<uint32_t>(level);
				const std::string where = StrCat(area.name, " level ", level, " seed ", seed);
				SetRoomCollection(collection, poe2Rooms);
				const Tiles before = BuildFresh(level, seed);
				ASSERT_TRUE(IsCathedralFunnelFloor()) << where;
				MyPlayer->_pLvlVisited[level] = true;
				Poe2FloorSaved(level, false);
				const std::vector<uint8_t> floors = Poe2PackFloors();

				// This build, which reads Reforged's own rooms; the old save has no rooms entry.
				ForgetRoomTables();
				ASSERT_TRUE(Poe2UnpackFloors(floors));
				UnpackKeptFloorRooms({});
				EXPECT_EQ(Build(level, seed), before) << where;
				EXPECT_TRUE(IsCathedralFunnelFloor()) << where;
				MyPlayer->_pLvlVisited[level] = false;
			}
		}
	}
}

/** Real Cathedral floors filled the way a first visit fills them (needs the game's data for the walls and monsters). */
class ReforgedFloorsFilledTest : public ReforgedFloorsTest {
protected:
	void SetUp() override
	{
		ReforgedFloorsTest::SetUp();
		LoadGameArchives();
		if (!HaveMainData())
			GTEST_SKIP() << "MPQ assets (spawn.mpq or DIABDAT.MPQ) not found - skipping";
		UnloadModArchives();
		LoadModArchives(ReforgedMods);
		gbIsHellfire = true;
		sgGameInitInfo.nDifficulty = DIFF_NORMAL;
		LoadItemData();
		LoadSpellData();
		LoadMissileData();
		LoadMonsterData();
		LoadObjectData();
		InitQuests();
		leveltype = DTYPE_CATHEDRAL;
		const std::expected<void, std::string> sol = LoadLevelSOLData();
		ASSERT_TRUE(sol.has_value()) << sol.error();
		UseCathedralTiles = true;
	}

	void TearDown() override
	{
		UseCathedralTiles = false;
		ReforgedFloorsTest::TearDown();
		gbIsHellfire = false;
		pMegaTiles = nullptr;
		ActiveMonsterCount = 0;
	}

	/** The monster types the floor's seed gives, as LoadGameLevel picks them once the floor is built. */
	static std::vector<_monster_id> MonsterTypes(int level)
	{
		InitLevelMonsters();
		SetRndSeed(DungeonSeeds[level]);
		EXPECT_TRUE(GetLevelMTypes().has_value());
		std::vector<_monster_id> types;
		for (size_t i = 0; i < LevelMonsterTypeCount; i++)
			types.push_back(LevelMonsterTypes[i].type);
		return types;
	}

	/**
	 * Objects, monsters and theme rooms, in the order LoadGameLevel places them on a first visit.
	 * @return How many monsters InitMonsters placed on the map (the theme rooms add theirs after).
	 */
	static size_t Fill(int level)
	{
		InitL1Triggers();
		memset(dMonster, 0, sizeof(dMonster));
		memset(dPlayer, 0, sizeof(dPlayer));
		memset(dObject, 0, sizeof(dObject));
		memset(dItem, 0, sizeof(dItem));
		for (Monster &monster : Monsters)
			monster = {};
		ActiveMonsterCount = 0;
		std::iota(std::begin(ActiveMonsters), std::end(ActiveMonsters), 0U);
		MonsterTypes(level);
		InitThemes();
		HoldThemeRooms();
		InitGolems();
		InitObjects();
		// Golem slots and the skeletons waiting in sarcophagi are already there, off the map.
		const size_t waiting = ActiveMonsterCount;
		EXPECT_TRUE(InitMonsters().has_value());
		const size_t placed = ActiveMonsterCount - waiting;
		CreateThemeRooms();
		return placed;
	}
};

bool IsLootOrBlessing(const Object &object)
{
	if (object.IsChest() || object.IsBarrel() || object.IsShrine())
		return true;
	return IsAnyOf(object._otype, OBJ_SARC, OBJ_L5SARC, OBJ_BOOKSTAND, OBJ_ARMORSTAND, OBJ_ARMORSTANDN, OBJ_WEAPONRACK, OBJ_WEAPONRACKN,
	    OBJ_BLOODFTN, OBJ_PURIFYINGFTN, OBJ_MURKYFTN, OBJ_TEARFTN, OBJ_GOATSHRINE, OBJ_CAULDRON, OBJ_DECAP, OBJ_POD, OBJ_PODEX, OBJ_URN, OBJ_URNEX);
}

TEST_F(ReforgedFloorsFilledTest, DiablosOwnPlacementFillsTheDesignedFloors)
{
	int floors = 0;
	int floorsWithThemes = 0;
	for (int level = 1; level <= 4; level++) {
		for (uint32_t n = 1; n <= 6; n++) {
			const uint32_t seed = (n * 104729U) + static_cast<uint32_t>(level);
			const std::string where = StrCat("level ", level, " seed ", seed);
			MyPlayer->setLevel(static_cast<uint8_t>(level));

			// The monster types are the ones the seed gives on Hellfire's own layout.
			BuildHellfire(level, seed);
			const std::vector<_monster_id> hellfireTypes = MonsterTypes(level);
			BuildFresh(level, seed);
			ASSERT_TRUE(IsCathedralFunnelFloor()) << where;
			EXPECT_EQ(MonsterTypes(level), hellfireTypes) << where;

			const size_t placed = Fill(level);
			floors++;
			if (numthemes > 0)
				floorsWithThemes++;

			// Diablo's own count: a monster to every 30 open tiles, on top of the quest and unique monsters.
			size_t open = 0;
			for (int y = 16; y < 96; y++) {
				for (int x = 16; x < 96; x++) {
					if (!IsTileSolid({ x, y }))
						open++;
				}
			}
			EXPECT_GE(placed, open / 30) << where;
			size_t uniques = 0;
			for (size_t i = 0; i < ActiveMonsterCount; i++) {
				const Monster &monster = Monsters[ActiveMonsters[i]];
				const Point tile = monster.position.tile;
				// Golem slots and the skeletons waiting in sarcophagi stand off the map.
				if (std::abs(dMonster[tile.x][tile.y]) != static_cast<int>(ActiveMonsters[i]) + 1)
					continue;
				if (monster.isUnique())
					uniques++;
				EXPECT_FALSE(IsTileSolid(monster.position.tile)) << "monster in a wall, " << where << " at " << static_cast<int>(monster.position.tile.x) << "," << static_cast<int>(monster.position.tile.y);
				EXPECT_FALSE(IsBoundDemon(monster)) << where;
				EXPECT_FALSE(IsEssenceDemon(monster)) << where;
				EXPECT_EQ(GetMonsterAffixes(monster).rarity, MonsterRarity::Normal) << where;
			}
			EXPECT_LE(placed, open / 30 + (uniques * 10) + 10) << where;

			for (int i = 0; i < ActiveObjectCount; i++) {
				const Object &object = Objects[ActiveObjects[i]];
				EXPECT_EQ(GetFloorChestKind(object), FloorChestKind::Normal) << "no POE2 chests or strongboxes, " << where;
				// The designed rooms hold only their furniture; what gives loot or a blessing is Diablo's to place.
				if (IsCathedralDesignedTile(object.position))
					EXPECT_FALSE(IsLootOrBlessing(object)) << static_cast<int>(object._otype) << " in a designed room at " << object.position.x << "," << object.position.y << ", " << where;
			}
		}
	}
	// Diablo's theme rooms still find the plain side rooms.
	EXPECT_GE(floorsWithThemes * 2, floors);
}

TEST_F(ReforgedFloorsFilledTest, TheMonstersArePlacedAsInPlainHellfire)
{
	for (int level = 1; level <= 4; level++) {
		const uint32_t seed = 9973U + static_cast<uint32_t>(level);
		MyPlayer->setLevel(static_cast<uint8_t>(level));
		BuildFresh(level, seed);
		ASSERT_TRUE(IsCathedralFunnelFloor());
		InitL1Triggers();
		memset(dMonster, 0, sizeof(dMonster));
		memset(dPlayer, 0, sizeof(dPlayer));
		memset(dObject, 0, sizeof(dObject));
		MonsterTypes(level);
		InitThemes();
		HoldThemeRooms();
		InitObjects();
		const uint32_t state = GetLCGEngineState();
		std::array<std::vector<std::pair<Point, _monster_id>>, 2> placed;
		for (const bool reforged : { true, false }) {
			gbIsReforged = reforged;
			for (Monster &monster : Monsters)
				monster = {};
			ActiveMonsterCount = 0;
			std::iota(std::begin(ActiveMonsters), std::end(ActiveMonsters), 0U);
			memset(dMonster, 0, sizeof(dMonster));
			SetRndSeed(state);
			ASSERT_TRUE(InitMonsters().has_value());
			for (size_t i = 0; i < ActiveMonsterCount; i++) {
				const Monster &monster = Monsters[ActiveMonsters[i]];
				placed[reforged ? 0 : 1].emplace_back(monster.position.tile, monster.type().type);
			}
		}
		gbIsReforged = true;
		EXPECT_FALSE(placed[0].empty()) << level;
		EXPECT_EQ(placed[0], placed[1]) << "level " << level;
	}
}

} // namespace
} // namespace devilution
