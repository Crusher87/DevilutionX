#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "levels/gendung.h"
#include "monster.h"
#include "player.h"
#include "poe2/area_levels.hpp"
#include "poe2/waystones.hpp"
#include "tables/monstdat.h"
#include "tables/questdat.hpp"

namespace devilution {
namespace {

class Poe2AreaLevelsTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		*MyPlayer = {};
		MyPlayer->poe2.clear();
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		gbIsPoe2 = true;
		sgGameInitInfo.nDifficulty = DIFF_NORMAL;
		setlevel = false;
		setlvlnum = SL_NONE;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadMonsterData();
		LoadAreaLevels();
		GoTo(1, DTYPE_CATHEDRAL);
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		gbIsMultiplayer = false;
		sgGameInitInfo.nDifficulty = DIFF_NORMAL;
		setlevel = false;
		setlvlnum = SL_NONE;
		currlevel = 0;
		leveltype = DTYPE_TOWN;
		LoadAreaLevels();
		UnloadModArchives();
	}

	static void GoTo(int floor, dungeon_type type)
	{
		setlevel = false;
		setlvlnum = SL_NONE;
		currlevel = static_cast<uint8_t>(floor);
		leveltype = type;
	}

	static void GoToQuestLevel(_setlevels level, quest_id quest, int questFloor)
	{
		Quests[quest]._qlevel = static_cast<uint8_t>(questFloor);
		setlevel = true;
		setlvlnum = level;
		// The game keeps the quest level's number in currlevel while on it.
		currlevel = static_cast<uint8_t>(level);
		leveltype = DTYPE_CATHEDRAL;
	}

	static Monster MakeMonster(_monster_id type, UniqueMonsterType unique = UniqueMonsterType::None)
	{
		LevelMonsterTypes[0].type = type;
		Monster monster {};
		monster.levelType = 0;
		monster.uniqueType = unique;
		monster.flags = 0;
		return monster;
	}
};

TEST_F(Poe2AreaLevelsTest, FloorsFollowTheFit)
{
	EXPECT_EQ(AreaLevelForFloor(1), 2);
	EXPECT_EQ(AreaLevelForFloor(8), 13);
	EXPECT_EQ(AreaLevelForFloor(9), 14);
	EXPECT_EQ(AreaLevelForFloor(17), 20);
	EXPECT_EQ(AreaLevelForFloor(20), 25);
	EXPECT_EQ(AreaLevelForFloor(21), 26);
	EXPECT_EQ(AreaLevelForFloor(24), 31);
	EXPECT_EQ(AreaLevelForFloor(13), 32);
	EXPECT_EQ(AreaLevelForFloor(16), 37);
	// Every floor has a row, and no floor in the act order is lower than the one before it.
	constexpr int ActOrder[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 17, 18, 19, 20, 21, 22, 23, 24, 13, 14, 15, 16 };
	int previous = 0;
	for (const int floor : ActOrder) {
		EXPECT_GT(AreaLevelForFloor(floor), previous) << "floor " << floor;
		previous = AreaLevelForFloor(floor);
	}
	EXPECT_EQ(AreaLevelForFloor(0), 0);
	EXPECT_EQ(AreaLevelForFloor(NUMLEVELS), 0);
}

TEST_F(Poe2AreaLevelsTest, HiveAndCryptUseTheRawFloor)
{
	GoTo(17, DTYPE_NEST);
	EXPECT_EQ(AreaFloor(), 17);
	EXPECT_EQ(CurrentAreaLevel(), 20);
	GoTo(20, DTYPE_NEST);
	EXPECT_EQ(CurrentAreaLevel(), 25);
	GoTo(21, DTYPE_CRYPT);
	EXPECT_EQ(AreaFloor(), 21);
	EXPECT_EQ(CurrentAreaLevel(), 26);
	GoTo(24, DTYPE_CRYPT);
	EXPECT_EQ(CurrentAreaLevel(), 31);
	GoTo(13, DTYPE_HELL);
	EXPECT_EQ(CurrentAreaLevel(), 32);
}

TEST_F(Poe2AreaLevelsTest, TownIsAreaOne)
{
	GoTo(0, DTYPE_TOWN);
	EXPECT_EQ(CurrentAreaLevel(), 1);
	EXPECT_EQ(MakeMonster(MT_NZOMBIE).level(DIFF_NORMAL), 1U);
}

TEST_F(Poe2AreaLevelsTest, SetLevelsUseQuestFloor)
{
	GoToQuestLevel(SL_SKELKING, Q_SKELKING, 3);
	EXPECT_EQ(AreaFloor(), 3);
	EXPECT_EQ(CurrentAreaLevel(), 5);
	EXPECT_EQ(MakeMonster(MT_SKING, UniqueMonsterType::SkeletonKing).level(DIFF_NORMAL), 7U);
	GoToQuestLevel(SL_BONECHAMB, Q_SCHAMB, 6);
	EXPECT_EQ(CurrentAreaLevel(), 10);
	GoToQuestLevel(SL_POISONWATER, Q_PWATER, 2);
	EXPECT_EQ(CurrentAreaLevel(), 4);
	GoToQuestLevel(SL_VILEBETRAYER, Q_BETRAYER, 15);
	EXPECT_EQ(CurrentAreaLevel(), 36);
	EXPECT_EQ(MakeMonster(MT_ADVOCATE, UniqueMonsterType::Lazarus).level(DIFF_NORMAL), 38U);
}

TEST_F(Poe2AreaLevelsTest, WhitesAreTheAreaLevel)
{
	GoTo(8, DTYPE_CATACOMBS);
	EXPECT_EQ(MakeMonster(MT_NZOMBIE).level(DIFF_NORMAL), 13U);
	GoTo(16, DTYPE_HELL);
	EXPECT_EQ(MakeMonster(MT_BLOODCLW).level(DIFF_NORMAL), 37U);
}

TEST_F(Poe2AreaLevelsTest, DiabloIsLevel39)
{
	GoTo(16, DTYPE_HELL);
	// Diablo is a monster type, not a unique, yet counts as a boss.
	const Monster diablo = MakeMonster(MT_DIABLO);
	EXPECT_FALSE(diablo.isUnique());
	EXPECT_EQ(diablo.level(DIFF_NORMAL), 39U);
	EXPECT_EQ(diablo.level(DIFF_HELL), 69U);
}

TEST_F(Poe2AreaLevelsTest, NaKrulIsFloorPlusTwo)
{
	GoTo(24, DTYPE_CRYPT);
	EXPECT_EQ(MakeMonster(MT_NAKRUL).level(DIFF_NORMAL), 33U);
	EXPECT_EQ(MakeMonster(MT_NAKRUL, UniqueMonsterType::NaKrul).level(DIFF_NORMAL), 33U);
}

TEST_F(Poe2AreaLevelsTest, ButcherIsFloorPlusFive)
{
	GoTo(2, DTYPE_CATHEDRAL);
	EXPECT_EQ(MakeMonster(MT_CLEAVER, UniqueMonsterType::Butcher).level(DIFF_NORMAL), 9U);
}

TEST_F(Poe2AreaLevelsTest, GolemsKeepDiabloLevels)
{
	GoTo(16, DTYPE_HELL);
	Monster golem = MakeMonster(MT_GOLEM);
	golem.flags |= MFLAG_GOLEM;
	EXPECT_EQ(golem.level(DIFF_NORMAL), golem.diabloLevel(DIFF_NORMAL));
}

TEST_F(Poe2AreaLevelsTest, MapsUseInterimCurve)
{
	EXPECT_EQ(MapMonsterAreaLevel(1), 41);
	EXPECT_EQ(MapMonsterAreaLevel(8), 59);
	EXPECT_EQ(MapMonsterAreaLevel(16), 80);
	MyPlayer->poe2.mapTier = 8;
	MyPlayer->poe2.mapLevel = 13;
	GoTo(13, DTYPE_HELL);
	EXPECT_EQ(CurrentAreaLevel(), 59);
	// Maps don't add the difficulty's levels.
	EXPECT_EQ(CurrentAreaLevel(DIFF_HELL), 59);
	EXPECT_EQ(MakeMonster(MT_BLOODCLW).level(DIFF_NORMAL), 59U);
	MyPlayer->poe2.mapTier = 16;
	EXPECT_EQ(CurrentAreaLevel(), 80);
}

TEST_F(Poe2AreaLevelsTest, NightmareAddsFifteen)
{
	GoTo(1, DTYPE_CATHEDRAL);
	EXPECT_EQ(CurrentAreaLevel(DIFF_NORMAL), 2);
	EXPECT_EQ(CurrentAreaLevel(DIFF_NIGHTMARE), 17);
	EXPECT_EQ(CurrentAreaLevel(DIFF_HELL), 32);
	sgGameInitInfo.nDifficulty = DIFF_NIGHTMARE;
	EXPECT_EQ(CurrentAreaLevel(), 17);
	EXPECT_EQ(MakeMonster(MT_NZOMBIE).level(DIFF_NIGHTMARE), 17U);
}

TEST_F(Poe2AreaLevelsTest, MultiplayerIgnoresMapTier)
{
	MyPlayer->poe2.mapTier = 8;
	MyPlayer->poe2.mapLevel = 13;
	GoTo(13, DTYPE_HELL);
	gbIsMultiplayer = true;
	EXPECT_EQ(ActiveMapTier(), 0);
	EXPECT_EQ(CurrentAreaLevel(), 32);
	EXPECT_EQ(MakeMonster(MT_BLOODCLW).level(DIFF_NORMAL), 32U);
}

TEST_F(Poe2AreaLevelsTest, DiabloModeLevelUnchanged)
{
	gbIsPoe2 = false;
	GoTo(16, DTYPE_HELL);
	const Monster diablo = MakeMonster(MT_DIABLO);
	EXPECT_EQ(diablo.level(DIFF_NORMAL), diablo.diabloLevel(DIFF_NORMAL));
	EXPECT_EQ(diablo.level(DIFF_NORMAL), static_cast<unsigned int>(MonstersData[MT_DIABLO].level));
	EXPECT_EQ(diablo.level(DIFF_HELL), static_cast<unsigned int>(MonstersData[MT_DIABLO].level) + 30U);
	const Monster butcher = MakeMonster(MT_CLEAVER, UniqueMonsterType::Butcher);
	EXPECT_EQ(butcher.level(DIFF_NORMAL), butcher.diabloLevel(DIFF_NORMAL));
}

TEST_F(Poe2AreaLevelsTest, DiabloModeLoaderIsEmptyAndSafe)
{
	gbIsPoe2 = false;
	LoadAreaLevels();
	EXPECT_EQ(AreaLevelForFloor(1), 0);
	const Monster zombie = MakeMonster(MT_NZOMBIE);
	EXPECT_EQ(Poe2MonsterLevel(zombie, DIFF_NORMAL), 0);
}

} // namespace
} // namespace devilution
