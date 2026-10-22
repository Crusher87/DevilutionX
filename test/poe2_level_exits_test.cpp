#include <gtest/gtest.h>

#include "game_mode.hpp"
#include "levels/gendung.h"
#include "levels/trigs.h"
#include "player.h"
#include "quests.h"

namespace devilution {
namespace {

/** The hero stands on a level exit he walked onto, without clicking it (Curtis 2026-10-22, picture 11). */
class Poe2LevelExitsTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		MyPlayerId = 0;
		gbIsPoe2 = true;
		gbIsMultiplayer = false;
		gbIsSpawn = false;
		setlevel = false;
		currlevel = 2;
		leveltype = DTYPE_CATHEDRAL;
		MyPlayer->_pmode = PM_STAND;
		MyPlayer->position.tile = Exit;
		numtrigs = 0;
		for (Quest &quest : Quests)
			quest = {};
		ForgetChosenLevelExit();
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		numtrigs = 0;
		for (Quest &quest : Quests)
			quest = {};
		ForgetChosenLevelExit();
	}

	static constexpr Point Exit { 30, 40 };
};

TEST_F(Poe2LevelExitsTest, OnlyTheClickedExitIsChosen)
{
	EXPECT_FALSE(ChoseLevelExit(Exit));
	ChooseLevelExitAt(Exit);
	EXPECT_TRUE(ChoseLevelExit(Exit));
	EXPECT_FALSE(ChoseLevelExit(Exit + Displacement { 1, 0 }));
	ForgetChosenLevelExit();
	EXPECT_FALSE(ChoseLevelExit(Exit));
}

TEST_F(Poe2LevelExitsTest, StairsWalkedOntoDoNothing)
{
	numtrigs = 1;
	trigs[0] = { Exit, WM_DIABNEXTLVL, 3 };
	CheckTriggers();
	EXPECT_EQ(MyPlayer->_pmode, PM_STAND);

	// A click on other stairs doesn't count for these.
	ChooseLevelExitAt(Exit + Displacement { 5, 5 });
	CheckTriggers();
	EXPECT_EQ(MyPlayer->_pmode, PM_STAND);
}

TEST_F(Poe2LevelExitsTest, QuestEntranceWalkedOntoDoesNothing)
{
	// "To A Dark Passage", the Poisoned Water's entrance on level 2.
	Quest &water = Quests[Q_PWATER];
	water._qidx = Q_PWATER;
	water._qlevel = 2;
	water._qslvl = SL_POISONWATER;
	water._qactive = QUEST_ACTIVE;
	water._qlvltype = DTYPE_CAVES;
	water.position = Exit;
	CheckQuests();
	EXPECT_EQ(MyPlayer->_pmode, PM_STAND);
}

} // namespace
} // namespace devilution
