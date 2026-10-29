#include <gtest/gtest.h>

#include "control/control.hpp"
#include "game_mode.hpp"
#include "objects.h"
#include "poe2/floor_chests.hpp"

namespace devilution {
namespace {

class Poe2FloorChestsTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		wasPoe2_ = gbIsPoe2;
		gbIsPoe2 = true;
		chest_ = {};
		chest_._otype = OBJ_CHEST2;
	}

	void TearDown() override
	{
		gbIsPoe2 = wasPoe2_;
	}

	Object chest_ {};
	bool wasPoe2_ = false;
};

TEST_F(Poe2FloorChestsTest, KindRoundTrips)
{
	EXPECT_EQ(GetFloorChestKind(chest_), FloorChestKind::Normal);
	for (const FloorChestKind kind : { FloorChestKind::Magic, FloorChestKind::Rare, FloorChestKind::Normal }) {
		SetFloorChestKind(chest_, kind);
		EXPECT_EQ(GetFloorChestKind(chest_), kind);
		EXPECT_EQ(chest_._oVar6, 0);
	}
	SetFloorChestKind(chest_, FloorChestKind::Strongbox, 5);
	EXPECT_EQ(GetFloorChestKind(chest_), FloorChestKind::Strongbox);
	EXPECT_EQ(chest_._oVar6, 5);
	EXPECT_EQ(chest_._oVar3, 0);
}

TEST_F(Poe2FloorChestsTest, LeftoverValuesAreNotAKind)
{
	// A reused object slot can carry another object's _oVar5 (a magic circle's 1 to 4).
	for (const int leftover : { 1, 2, 3, 4, 99, 104 }) {
		chest_._oVar5 = leftover;
		EXPECT_EQ(GetFloorChestKind(chest_), FloorChestKind::Normal) << leftover;
	}
	Object barrel {};
	barrel._otype = OBJ_BARREL;
	SetFloorChestKind(barrel, FloorChestKind::Rare);
	EXPECT_EQ(GetFloorChestKind(barrel), FloorChestKind::Normal);
}

TEST_F(Poe2FloorChestsTest, NamesShowTheirRarity)
{
	InfoColor = UiFlags::ColorWhite;
	SetFloorChestKind(chest_, FloorChestKind::Magic);
	DescribeFloorChest(chest_);
	EXPECT_EQ(InfoColor, UiFlags::ColorBlue);

	InfoColor = UiFlags::ColorWhite;
	SetFloorChestKind(chest_, FloorChestKind::Rare);
	DescribeFloorChest(chest_);
	EXPECT_EQ(InfoColor, UiFlags::ColorYellow);

	InfoColor = UiFlags::ColorWhite;
	InfoString = std::string_view("Chest");
	SetFloorChestKind(chest_, FloorChestKind::Strongbox, 4);
	DescribeFloorChest(chest_);
	EXPECT_EQ(InfoColor, UiFlags::ColorGold);
	EXPECT_EQ(InfoString.str(), "Strongbox");
}

TEST_F(Poe2FloorChestsTest, ClosedStrongboxStandsOut)
{
	chest_.selectionRegion = SelectionRegion::Bottom;
	EXPECT_FALSE(StrongboxStandsOut(chest_));
	SetFloorChestKind(chest_, FloorChestKind::Strongbox, 4);
	EXPECT_TRUE(StrongboxStandsOut(chest_));
	chest_.selectionRegion = SelectionRegion::None;
	EXPECT_FALSE(StrongboxStandsOut(chest_));
	// Opened, but its loot still waits for the guards to die.
	chest_._oVar6 |= 1 << 16;
	EXPECT_TRUE(StrongboxStandsOut(chest_));
}

TEST_F(Poe2FloorChestsTest, ClosedStrongboxBringsNoGuards)
{
	SetFloorChestKind(chest_, FloorChestKind::Strongbox, 4);
	for (int tick = 0; tick < 100; tick++)
		UpdateFloorChest(chest_);
	EXPECT_EQ(chest_._oVar6, 4);
	EXPECT_EQ(chest_._oVar3, 0);
}

TEST_F(Poe2FloorChestsTest, DiabloModeLeavesChestsAlone)
{
	gbIsPoe2 = false;
	InfoColor = UiFlags::ColorWhite;
	SetFloorChestKind(chest_, FloorChestKind::Rare);
	DescribeFloorChest(chest_);
	EXPECT_EQ(InfoColor, UiFlags::ColorWhite);
}

} // namespace
} // namespace devilution
