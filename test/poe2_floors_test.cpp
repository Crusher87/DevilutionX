#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "levels/gendung.h"
#include "multi.h"
#include "player.h"
#include "poe2/floors.hpp"
#include "portal.h"
#include "quests.h"

namespace devilution {
namespace {

class Poe2FloorsTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		MyPlayerId = 0;
		*MyPlayer = {};
		gbIsMultiplayer = false;
		sgGameInitInfo.fullQuests = 1; // single-player quests
		setlevel = false;
		currlevel = 3;
		LoadCoreArchives();
		LoadQuestData();
		InitQuests();
		for (Quest &quest : Quests)
			quest._qactive = QUEST_INIT;
		InitPortals();
		Poe2FloorsReset();
		for (int i = 0; i < NUMLEVELS; i++) {
			DungeonSeeds[i] = 1000 + i;
			LevelSeeds[i] = 77;
		}
	}

	void Visit(int level)
	{
		MyPlayer->_pLvlVisited[level] = true;
		Poe2FloorSaved(level, false);
	}

	void Wait(uint32_t ticks)
	{
		for (uint32_t i = 0; i < ticks; i++)
			Poe2FloorsTick();
	}
};

TEST_F(Poe2FloorsTest, UnvisitedFloorIsNotKept)
{
	EXPECT_FALSE(Poe2FloorIsKept(3, false));
	EXPECT_EQ(Poe2FloorVisits(3, false), 0);
}

TEST_F(Poe2FloorsTest, SavedFloorStaysForFifteenMinutes)
{
	Visit(3);
	EXPECT_TRUE(Poe2FloorIsKept(3, false));
	Wait(FloorKeepTicks);
	EXPECT_TRUE(Poe2FloorIsKept(3, false));
	Wait(1);
	EXPECT_FALSE(Poe2FloorIsKept(3, false));
}

TEST_F(Poe2FloorsTest, TownIsAlwaysKept)
{
	MyPlayer->_pLvlVisited[0] = true;
	Poe2FloorSaved(0, false);
	Wait(FloorKeepTicks * 10);
	EXPECT_TRUE(Poe2FloorIsKept(0, false));
}

TEST_F(Poe2FloorsTest, OpenPortalKeepsItsFloorUntilItCloses)
{
	Visit(3);
	ActivatePortal(*MyPlayer, { 20, 20 }, 3, DTYPE_CATHEDRAL, false);
	Wait(FloorKeepTicks * 3);
	EXPECT_TRUE(Poe2FloorIsKept(3, false));
	DeactivatePortal(*MyPlayer);
	EXPECT_TRUE(Poe2FloorIsKept(3, false)) << "the timer starts when the portal closes";
	Wait(FloorKeepTicks + 1);
	EXPECT_FALSE(Poe2FloorIsKept(3, false));
}

TEST_F(Poe2FloorsTest, FreshRollChangesTheSeedAndCountsTheVisit)
{
	Visit(3);
	const uint32_t seed = Poe2RollFreshFloor(3, false);
	EXPECT_NE(seed, 1003U);
	EXPECT_EQ(DungeonSeeds[3], seed);
	EXPECT_FALSE(LevelSeeds[3].has_value());
	EXPECT_FALSE(Poe2FloorIsKept(3, false));
	EXPECT_EQ(Poe2FloorVisits(3, false), 2);
	EXPECT_NE(Poe2RollFreshFloor(3, false), seed) << "every roll is a new floor";
	EXPECT_EQ(Poe2FloorVisits(3, false), 3);
}

TEST_F(Poe2FloorsTest, RollsAreTheSameForTheSameSaveState)
{
	Visit(3);
	const std::vector<uint8_t> before = Poe2PackFloors();
	const uint32_t first = Poe2RollFreshFloor(3, false);
	ASSERT_TRUE(Poe2UnpackFloors(before));
	EXPECT_EQ(Poe2RollFreshFloor(3, false), first);
}

TEST_F(Poe2FloorsTest, SaveRoundTripKeepsRecordsAndClock)
{
	Visit(3);
	Visit(5);
	Wait(123);
	MyPlayer->_pSLvlVisited[SL_SKELKING] = true;
	Poe2FloorSaved(SL_SKELKING, true);
	const std::vector<uint8_t> packed = Poe2PackFloors();
	Poe2FloorsReset();
	EXPECT_FALSE(Poe2FloorIsKept(3, false));
	ASSERT_TRUE(Poe2UnpackFloors(packed));
	EXPECT_EQ(Poe2GameTicks, 123U);
	EXPECT_TRUE(Poe2FloorIsKept(3, false));
	EXPECT_TRUE(Poe2FloorIsKept(5, false));
	EXPECT_TRUE(Poe2FloorIsKept(SL_SKELKING, true));
	EXPECT_FALSE(Poe2FloorIsKept(4, false));
}

TEST_F(Poe2FloorsTest, UnreadableSaveDataIsRefused)
{
	const std::vector<uint8_t> junk { 9, 1, 2 };
	EXPECT_FALSE(Poe2UnpackFloors(junk));
	EXPECT_FALSE(Poe2UnpackFloors({}));
}

TEST_F(Poe2FloorsTest, LoadingKeepsOnlyTheFloorYouStandOnAndThePortalFloor)
{
	Visit(2);
	Visit(3);
	Visit(4);
	Visit(7);
	ActivatePortal(*MyPlayer, { 20, 20 }, 7, DTYPE_CATACOMBS, false);
	currlevel = 3;
	Poe2FloorsAfterLoadGame();
	EXPECT_FALSE(Poe2FloorIsKept(2, false));
	EXPECT_TRUE(Poe2FloorIsKept(3, false));
	EXPECT_FALSE(Poe2FloorIsKept(4, false));
	EXPECT_TRUE(Poe2FloorIsKept(7, false));
}

TEST_F(Poe2FloorsTest, FinishedFinalBossFloorStaysAsItWas)
{
	Visit(16);
	Quests[Q_DIABLO]._qactive = QUEST_DONE;
	Wait(FloorKeepTicks * 2);
	EXPECT_TRUE(Poe2FloorIsKept(16, false));
	Quests[Q_DIABLO]._qactive = QUEST_ACTIVE;
	EXPECT_FALSE(Poe2FloorIsKept(16, false));
}

TEST_F(Poe2FloorsTest, EnteringAStaleFloorRollsItFresh)
{
	Visit(3);
	Wait(FloorKeepTicks + 1);
	currlevel = 3;
	Poe2BeginFloorVisit(false, ENTRY_MAIN);
	EXPECT_TRUE(Poe2FloorFresh);
	EXPECT_NE(DungeonSeeds[3], 1003U);
	EXPECT_EQ(Poe2FloorVisits(3, false), 2);
}

TEST_F(Poe2FloorsTest, EnteringAKeptFloorLoadsIt)
{
	Visit(3);
	Wait(10);
	currlevel = 3;
	Poe2BeginFloorVisit(false, ENTRY_MAIN);
	EXPECT_FALSE(Poe2FloorFresh);
	EXPECT_EQ(DungeonSeeds[3], 1003U);
}

TEST_F(Poe2FloorsTest, CtrlClickForcesAFreshFloorOnce)
{
	Visit(3);
	currlevel = 3;
	Poe2ForceFreshFloor = true;
	Poe2BeginFloorVisit(false, ENTRY_MAIN);
	EXPECT_TRUE(Poe2FloorFresh);
	EXPECT_FALSE(Poe2ForceFreshFloor);
	Visit(3);
	Poe2BeginFloorVisit(false, ENTRY_MAIN);
	EXPECT_FALSE(Poe2FloorFresh);
}

TEST_F(Poe2FloorsTest, LoadingAGameNeverRolls)
{
	Visit(3);
	Wait(FloorKeepTicks + 1);
	currlevel = 3;
	Poe2BeginFloorVisit(false, ENTRY_LOAD);
	EXPECT_FALSE(Poe2FloorFresh);
	EXPECT_EQ(DungeonSeeds[3], 1003U);
}

TEST_F(Poe2FloorsTest, FirstVisitKeepsTheHeroSeed)
{
	currlevel = 3;
	Poe2BeginFloorVisit(false, ENTRY_MAIN);
	EXPECT_FALSE(Poe2FloorFresh);
	EXPECT_EQ(DungeonSeeds[3], 1003U);
	EXPECT_EQ(Poe2FloorVisits(3, false), 1);
}

TEST_F(Poe2FloorsTest, MultiplayerOnlyRollsOnRequest)
{
	gbIsMultiplayer = true;
	Visit(3);
	Wait(FloorKeepTicks + 1);
	currlevel = 3;
	Poe2BeginFloorVisit(false, ENTRY_MAIN);
	EXPECT_FALSE(Poe2FloorFresh) << "the timer is off in multiplayer";
	gbIsMultiplayer = false;
}

TEST_F(Poe2FloorsTest, RewardPendingStopsOnceTheQuestIsDone)
{
	Quest &quest = Quests[Q_ANVIL];
	currlevel = quest._qlevel;
	quest._qactive = QUEST_ACTIVE;
	EXPECT_TRUE(quest.IsRewardPending());
	quest._qactive = QUEST_DONE;
	EXPECT_TRUE(quest.IsAvailable()) << "the quest's room still appears";
	EXPECT_FALSE(quest.IsRewardPending()) << "but not its item";
}

} // namespace
} // namespace devilution
