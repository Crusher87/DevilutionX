#include <gtest/gtest.h>

#include <algorithm>

#include "automap.h"
#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "levels/gendung.h"
#include "options.h"
#include "poe2/quest_tracker.hpp"
#include "tables/questdat.hpp"

namespace devilution {
namespace {

class Poe2QuestTrackerTest : public ::testing::Test {
public:
	static void SetUpTestSuite()
	{
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadQuestData();
		LoadQuestTracker();
	}

	static void TearDownTestSuite()
	{
		UnloadModArchives();
	}

	void SetUp() override
	{
		gbIsPoe2 = true;
		AutomapActive = false;
		GetOptions().Gameplay.showCornerMap.SetValue(true);
		GetOptions().Gameplay.enableQuestTracking.SetValue(true);
		for (int i = 0; i < MAXQUESTS; i++) {
			Quests[i] = {};
			Quests[i]._qidx = static_cast<quest_id>(i);
			Quests[i]._qactive = QUEST_NOTAVAIL;
			Quests[i]._qlevel = QuestsData[i]._qdlvl;
			Quests[i]._qslvl = QuestsData[i]._qslvl;
		}
		currlevel = 0;
		setlevel = false;
		setlvlnum = SL_NONE;
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		currlevel = 0;
	}

	static Quest &Start(quest_id id, uint8_t stage = 0)
	{
		Quest &quest = Quests[id];
		quest._qactive = QUEST_ACTIVE;
		quest._qlog = true;
		quest._qvar1 = stage;
		return quest;
	}
};

/** @brief Every letter is 5 pixels wide. */
int Measure(std::string_view text)
{
	return static_cast<int>(text.size()) * 5;
}

TEST_F(Poe2QuestTrackerTest, OneShortLinePerQuest)
{
	Start(Q_BUTCHER);
	const std::optional<QuestTrackerLine> line = TrackQuest(Quests[Q_BUTCHER]);
	ASSERT_TRUE(line.has_value());
	EXPECT_EQ(line->title, "The Butcher");
	EXPECT_EQ(line->step, "kill him on level 2");
}

TEST_F(Poe2QuestTrackerTest, OnlyQuestsInTheLogShow)
{
	Quest &butcher = Start(Q_BUTCHER);
	butcher._qlog = false;
	EXPECT_FALSE(TrackQuest(butcher).has_value()) << "a quest the hero hasn't heard of yet";
	butcher._qlog = true;
	butcher._qactive = QUEST_INIT;
	EXPECT_FALSE(TrackQuest(butcher).has_value());
	butcher._qactive = QUEST_DONE;
	EXPECT_FALSE(TrackQuest(butcher).has_value()) << "done and nothing to hand in";
}

TEST_F(Poe2QuestTrackerTest, StepsFollowTheQuestsStage)
{
	Quest &mushroom = Start(Q_MUSHROOM, QS_TOMEGIVEN);
	EXPECT_EQ(TrackQuest(mushroom)->step, "find a black mushroom on level 9");
	mushroom._qvar1 = QS_MUSHPICKED;
	EXPECT_EQ(TrackQuest(mushroom)->step, "take the mushroom to Adria");
	mushroom._qvar1 = QS_MUSHGIVEN;
	EXPECT_EQ(TrackQuest(mushroom)->step, "get a demon brain for Pepin");
	mushroom._qvar1 = QS_BRAINGIVEN;
	EXPECT_EQ(TrackQuest(mushroom)->step, "take the elixir to Adria");
}

TEST_F(Poe2QuestTrackerTest, DoneQuestsWithARewardWaitingStay)
{
	Quest &water = Start(Q_PWATER);
	water._qactive = QUEST_DONE;
	ASSERT_TRUE(TrackQuest(water).has_value());
	EXPECT_EQ(TrackQuest(water)->step, "see Pepin for your reward");
	water._qvar1 = 2;
	EXPECT_FALSE(TrackQuest(water).has_value()) << "rewarded";
}

TEST_F(Poe2QuestTrackerTest, FloorNames)
{
	EXPECT_EQ(QuestFloorName(0), "town");
	EXPECT_EQ(QuestFloorName(2), "level 2");
	EXPECT_EQ(QuestFloorName(16), "level 16");
	EXPECT_EQ(QuestFloorName(17), "Nest 1");
	EXPECT_EQ(QuestFloorName(21), "Crypt 1");
}

TEST_F(Poe2QuestTrackerTest, EveryQuestUnderWayHasAStep)
{
	for (const QuestTrackerRow &row : GetQuestTrackerRows()) {
		EXPECT_FALSE(row.title.empty()) << static_cast<int>(row.quest);
		EXPECT_FALSE(row.step.empty()) << static_cast<int>(row.quest);
		EXPECT_LE(row.title.size() + row.step.size(), 48u) << row.step << ": keep the lines short";
	}
	for (int i = 0; i < MAXQUESTS; i++) {
		if (i == Q_TRADER)
			continue; // Never offered.
		const auto hasRow = std::any_of(GetQuestTrackerRows().begin(), GetQuestTrackerRows().end(),
		    [&](const QuestTrackerRow &row) { return row.quest == i && !row.done && !row.stage; });
		EXPECT_TRUE(hasRow) << QuestsData[i]._qlstr << " has no step for any stage";
	}
}

TEST_F(Poe2QuestTrackerTest, QuestsOnThisFloorComeFirstAndTheListStaysShort)
{
	Start(Q_BUTCHER);
	Start(Q_PWATER);
	Start(Q_SKELKING, 1);
	Start(Q_LTBANNER, 1);
	Start(Q_ROCK);
	currlevel = 4;
	const std::vector<QuestTrackerLine> lines = TrackedQuests(QuestTrackerMaxLines);
	ASSERT_EQ(lines.size(), static_cast<size_t>(QuestTrackerMaxLines));
	EXPECT_EQ(lines[0].quest, Q_LTBANNER) << "Ogden's Sign is on level 4";
	EXPECT_EQ(lines[1].quest, Q_BUTCHER) << "then the quest log's order";
	EXPECT_EQ(lines[2].quest, Q_PWATER);
	EXPECT_EQ(lines[3].quest, Q_SKELKING);
}

TEST_F(Poe2QuestTrackerTest, LongLinesAreCut)
{
	EXPECT_EQ(FitTextToWidth("kill him", 100, Measure), "kill him");
	EXPECT_EQ(FitTextToWidth("kill him on level 2", 50, Measure), "kill hi...");
	EXPECT_EQ(FitTextToWidth("kill him on level 2", 40, Measure), "kill...") << "no space before the dots";
	EXPECT_EQ(FitTextToWidth("kill him", 10, Measure), "");
	EXPECT_EQ(FitTextToWidth("k\xC3\xA4mpfe", 25, Measure), "k...") << "a letter of two bytes is never split";
}

TEST_F(Poe2QuestTrackerTest, HidesWithTheCornerMap)
{
	EXPECT_TRUE(QuestTrackerShown());
	GetOptions().Gameplay.enableQuestTracking.SetValue(false);
	EXPECT_FALSE(QuestTrackerShown());
	GetOptions().Gameplay.enableQuestTracking.SetValue(true);
	GetOptions().Gameplay.showCornerMap.SetValue(false);
	EXPECT_FALSE(QuestTrackerShown());
	GetOptions().Gameplay.showCornerMap.SetValue(true);
	AutomapActive = true;
	EXPECT_FALSE(QuestTrackerShown()) << "the Tab map is open";
	AutomapActive = false;
	gbIsPoe2 = false;
	EXPECT_FALSE(QuestTrackerShown());
}

} // namespace
} // namespace devilution
