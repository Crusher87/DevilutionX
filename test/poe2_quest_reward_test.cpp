#include <gtest/gtest.h>

#include "engine/point.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/dun_tile_data.hpp"
#include "minitext.h"
#include "panels/quest_reward_panel.hpp"
#include "player.h"

namespace devilution {
namespace {

constexpr Point RewardTile { 51, 50 };
constexpr uint32_t RewardSeed = 0x1234567;

class Poe2QuestRewardTest : public ::testing::Test {
public:
	void SetUp() override
	{
		gbIsPoe2 = true;
		gbIsMultiplayer = false;
		leveltype = DTYPE_TOWN;
		qtextflag = false;
		Players.resize(1);
		MyPlayerId = 0;
		MyPlayer = &Players[0];
		MyPlayer->_pHitPoints = MyPlayer->_pMaxHP = Fixed26_6::fromInt(50);
		MyPlayer->position.tile = { 50, 50 };
		MyPlayer->position.future = { 50, 50 };
		ClearQuestRewards();

		// Griswold's reward, lying beside the hero as a normal ground item.
		Items[0] = {};
		Items[0]._itype = ItemType::Ring;
		Items[0].IDidx = static_cast<_item_indexes>(5);
		Items[0]._iSeed = static_cast<int32_t>(RewardSeed);
		Items[0].position = RewardTile;
		ActiveItems[0] = 0;
		ActiveItemCount = 1;
		dItem[RewardTile.x][RewardTile.y] = 1;
	}

	void TearDown() override
	{
		ClearQuestRewards();
		dItem[RewardTile.x][RewardTile.y] = 0;
		Items[0] = {};
		ActiveItemCount = 0;
		qtextflag = false;
		gbIsPoe2 = false;
	}
};

TEST(Poe2QuestRewardRuleTest, AFullBagTurnsOffPutInBag)
{
	EXPECT_EQ(QuestRewardActionFor(QuestRewardInput::PutInBagButton, false), QuestRewardAction::PutInBag);
	EXPECT_EQ(QuestRewardActionFor(QuestRewardInput::Enter, false), QuestRewardAction::PutInBag);
	EXPECT_EQ(QuestRewardActionFor(QuestRewardInput::PutInBagButton, true), QuestRewardAction::None);
	EXPECT_EQ(QuestRewardActionFor(QuestRewardInput::Enter, true), QuestRewardAction::None);
}

TEST(Poe2QuestRewardRuleTest, EveryWayOutLeavesTheItemOnTheGround)
{
	for (const bool bagFull : { false, true }) {
		EXPECT_EQ(QuestRewardActionFor(QuestRewardInput::LeaveButton, bagFull), QuestRewardAction::LeaveOnGround);
		EXPECT_EQ(QuestRewardActionFor(QuestRewardInput::Escape, bagFull), QuestRewardAction::LeaveOnGround);
		EXPECT_EQ(QuestRewardActionFor(QuestRewardInput::OutsidePanel, bagFull), QuestRewardAction::LeaveOnGround);
		EXPECT_EQ(QuestRewardActionFor(QuestRewardInput::Moved, bagFull), QuestRewardAction::LeaveOnGround);
		EXPECT_EQ(QuestRewardActionFor(QuestRewardInput::InsidePanel, bagFull), QuestRewardAction::None);
	}
}

TEST_F(Poe2QuestRewardTest, WaitsForTheTalkTextThenOpens)
{
	qtextflag = true;
	OfferQuestReward("Griswold the Blacksmith", 0);
	UpdateQuestRewardOffer();
	EXPECT_FALSE(IsQuestRewardOpen()) << "the towner's speech is still showing";
	EXPECT_EQ(PendingQuestRewardCount(), 1U);

	qtextflag = false;
	UpdateQuestRewardOffer();
	EXPECT_TRUE(IsQuestRewardOpen());
	EXPECT_EQ(PendingQuestRewardCount(), 0U);
}

TEST_F(Poe2QuestRewardTest, WalkingAwayClosesItAndLeavesTheItem)
{
	OfferQuestReward("Ogden the Tavern Owner", 0);
	UpdateQuestRewardOffer();
	ASSERT_TRUE(IsQuestRewardOpen());
	UpdateQuestRewardOffer();
	EXPECT_TRUE(IsQuestRewardOpen()) << "standing still keeps it open";

	MyPlayer->position.future = { 49, 50 };
	UpdateQuestRewardOffer();
	EXPECT_FALSE(IsQuestRewardOpen());
	EXPECT_EQ(PendingQuestRewardCount(), 0U);
	EXPECT_EQ(dItem[RewardTile.x][RewardTile.y], 1) << "the reward is still on the ground";
}

TEST_F(Poe2QuestRewardTest, GoesBackInTheQueueWhenTalkTextComesUp)
{
	OfferQuestReward("Pepin the Healer", 0);
	UpdateQuestRewardOffer();
	ASSERT_TRUE(IsQuestRewardOpen());
	qtextflag = true;
	EXPECT_TRUE(QuestRewardHeldBack());
	UpdateQuestRewardOffer();
	EXPECT_FALSE(IsQuestRewardOpen());
	EXPECT_EQ(PendingQuestRewardCount(), 1U);
	qtextflag = false;
	UpdateQuestRewardOffer();
	EXPECT_TRUE(IsQuestRewardOpen());
}

TEST_F(Poe2QuestRewardTest, ARewardPickedUpAnotherWayNeedsNoPopup)
{
	qtextflag = true;
	OfferQuestReward("Lester the Farmer", 0);
	dItem[RewardTile.x][RewardTile.y] = 0;
	qtextflag = false;
	UpdateQuestRewardOffer();
	EXPECT_FALSE(IsQuestRewardOpen());
	EXPECT_EQ(PendingQuestRewardCount(), 0U);
}

TEST_F(Poe2QuestRewardTest, OnlyPoe2SinglePlayerOffersRewards)
{
	gbIsPoe2 = false;
	OfferQuestReward("Griswold the Blacksmith", 0);
	EXPECT_EQ(PendingQuestRewardCount(), 0U);

	gbIsPoe2 = true;
	gbIsMultiplayer = true;
	OfferQuestReward("Griswold the Blacksmith", 0);
	EXPECT_EQ(PendingQuestRewardCount(), 0U);
	gbIsMultiplayer = false;
}

} // namespace
} // namespace devilution
