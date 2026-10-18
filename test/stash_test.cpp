/**
 * @file stash_test.cpp
 *
 * Tests for the player stash system.
 *
 * These tests verify the functional behaviour of the shared item stash:
 * item placement, removal, page navigation, gold storage, transfer
 * operations between stash and inventory, and the dirty flag.
 *
 * All assertions are on game state (stash contents, grid cells, gold
 * values, dirty flag, inventory contents). No assertions on rendering,
 * pixel positions, or widget layout.
 */

#include <gtest/gtest.h>

#include "ui_test.hpp"

#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "loadsave.h"
#include "pfile.h"
#include "player.h"
#include "poe2/stacking.hpp"
#include "poe2/stash_layouts.hpp"
#include "poe2/stash_tabs.hpp"
#include "qol/stash.h"
#include "utils/file_util.h"
#include "utils/paths.h"

namespace devilution {
namespace {

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class StashTest : public UITest {
protected:
	void SetUp() override
	{
		UITest::SetUp();

		// Start each test with a completely clean stash.
		Stash = {};
		Stash.gold = 0;
		Stash.dirty = false;
	}

	// --- helpers ---

	/** @brief Create a simple 1×1 item (healing potion). */
	static Item MakeSmallItem()
	{
		Item item {};
		InitializeItem(item, IDI_HEAL);
		return item;
	}

	/** @brief Create a sword (larger than 1×1). */
	static Item MakeSword()
	{
		Item item {};
		InitializeItem(item, IDI_BARDSWORD);
		item._iIdentified = true;
		return item;
	}

	/** @brief Create a gold item with the given value. */
	static Item MakeGold(int value)
	{
		Item item {};
		item._itype = ItemType::Gold;
		item._ivalue = value;
		item._iMiscId = IMISC_NONE;
		return item;
	}

	/** @brief Count the number of non-empty cells in a stash grid page. */
	static int CountOccupiedCells(const StashStruct::StashGrid &grid)
	{
		int count = 0;
		for (const auto &row : grid) {
			for (StashStruct::StashCell cell : row) {
				if (cell != 0)
					count++;
			}
		}
		return count;
	}

	/** @brief Fill a stash page completely with 1×1 items. */
	void FillStashPage(unsigned page)
	{
		for (int x = 0; x < 10; x++) {
			for (int y = 0; y < 10; y++) {
				Item item = MakeSmallItem();
				Stash.SetPage(page);
				ASSERT_TRUE(AutoPlaceItemInStash(item, true))
				    << "Failed to place item at logical position on page " << page;
			}
		}
	}
};

// ---------------------------------------------------------------------------
// AutoPlaceItemInStash
// ---------------------------------------------------------------------------

TEST_F(StashTest, PlaceItem_EmptyStash)
{
	Item item = MakeSmallItem();

	bool placed = AutoPlaceItemInStash(item, true);

	EXPECT_TRUE(placed);
	EXPECT_FALSE(Stash.stashList.empty());
	EXPECT_EQ(Stash.stashList.size(), 1u);
	EXPECT_TRUE(Stash.dirty);
}

TEST_F(StashTest, PlaceItem_DryRunDoesNotMutate)
{
	Item item = MakeSmallItem();

	bool canPlace = AutoPlaceItemInStash(item, false);

	EXPECT_TRUE(canPlace);
	EXPECT_TRUE(Stash.stashList.empty()) << "Dry-run should not add item to stashList";
	EXPECT_FALSE(Stash.dirty) << "Dry-run should not set dirty flag";
}

TEST_F(StashTest, PlaceItem_GridCellOccupied)
{
	Item item = MakeSmallItem();

	ASSERT_TRUE(AutoPlaceItemInStash(item, true));

	// The item should occupy at least one cell on the current page.
	const auto &grid = Stash.stashGrids[Stash.GetPage()];
	EXPECT_GT(CountOccupiedCells(grid), 0);
}

TEST_F(StashTest, PlaceItem_MultipleItemsOnSamePage)
{
	Item item1 = MakeSmallItem();
	Item item2 = MakeSmallItem();
	Item item3 = MakeSword();

	EXPECT_TRUE(AutoPlaceItemInStash(item1, true));
	EXPECT_TRUE(AutoPlaceItemInStash(item2, true));
	EXPECT_TRUE(AutoPlaceItemInStash(item3, true));

	EXPECT_EQ(Stash.stashList.size(), 3u);
}

TEST_F(StashTest, PlaceItem_FullPageOverflowsToNextPage)
{
	Stash.SetPage(0);
	Stash.dirty = false;

	FillStashPage(0);

	size_t itemsAfterPage0 = Stash.stashList.size();

	// Page 0 should be completely full now. Placing another item should go to page 1.
	Item overflow = MakeSmallItem();
	Stash.SetPage(0); // Reset to page 0 so AutoPlace starts searching from page 0.
	EXPECT_TRUE(AutoPlaceItemInStash(overflow, true));

	EXPECT_EQ(Stash.stashList.size(), itemsAfterPage0 + 1);

	// The overflow item should be on page 1 (or later), not page 0.
	// Page 0 should still have only the original cells occupied.
	const auto &grid0 = Stash.stashGrids[0];
	EXPECT_EQ(CountOccupiedCells(grid0), 100) << "Page 0 should remain fully occupied";

	// Page 1 should have the overflow item.
	EXPECT_TRUE(Stash.stashGrids.count(1) > 0) << "Page 1 should have been created";
	EXPECT_GT(CountOccupiedCells(Stash.stashGrids[1]), 0) << "Overflow item should be on page 1";
}

TEST_F(StashTest, PlaceItem_SwordOccupiesCorrectArea)
{
	Item sword = MakeSword();
	const Size swordSize = GetInventorySize(sword);

	ASSERT_TRUE(AutoPlaceItemInStash(sword, true));

	const auto &grid = Stash.stashGrids[Stash.GetPage()];
	int occupiedCells = CountOccupiedCells(grid);
	EXPECT_EQ(occupiedCells, swordSize.width * swordSize.height)
	    << "Sword should occupy exactly " << swordSize.width << "×" << swordSize.height << " cells";
}

// ---------------------------------------------------------------------------
// Gold in stash
// ---------------------------------------------------------------------------

TEST_F(StashTest, PlaceGold_AddsToStashGold)
{
	Item gold = MakeGold(5000);

	EXPECT_TRUE(AutoPlaceItemInStash(gold, true));

	EXPECT_EQ(Stash.gold, 5000);
	EXPECT_TRUE(Stash.stashList.empty()) << "Gold should not be added to stashList";
	EXPECT_TRUE(Stash.dirty);
}

TEST_F(StashTest, PlaceGold_DryRunDoesNotMutate)
{
	Item gold = MakeGold(3000);

	EXPECT_TRUE(AutoPlaceItemInStash(gold, false));

	EXPECT_EQ(Stash.gold, 0) << "Dry-run should not change stash gold";
	EXPECT_FALSE(Stash.dirty);
}

TEST_F(StashTest, PlaceGold_AccumulatesMultipleDeposits)
{
	Item gold1 = MakeGold(1000);
	Item gold2 = MakeGold(2500);

	ASSERT_TRUE(AutoPlaceItemInStash(gold1, true));
	ASSERT_TRUE(AutoPlaceItemInStash(gold2, true));

	EXPECT_EQ(Stash.gold, 3500);
}

TEST_F(StashTest, PlaceGold_RejectsOverflow)
{
	Stash.gold = std::numeric_limits<int>::max() - 100;

	Item gold = MakeGold(200);

	EXPECT_FALSE(AutoPlaceItemInStash(gold, true))
	    << "Should reject gold that would cause integer overflow";
	EXPECT_EQ(Stash.gold, std::numeric_limits<int>::max() - 100)
	    << "Stash gold should be unchanged after rejected deposit";
}

// ---------------------------------------------------------------------------
// RemoveStashItem
// ---------------------------------------------------------------------------

TEST_F(StashTest, RemoveItem_ClearsGridAndList)
{
	Item item = MakeSmallItem();
	ASSERT_TRUE(AutoPlaceItemInStash(item, true));
	ASSERT_EQ(Stash.stashList.size(), 1u);

	Stash.dirty = false;
	Stash.RemoveStashItem(0);

	EXPECT_TRUE(Stash.stashList.empty());
	EXPECT_EQ(CountOccupiedCells(Stash.stashGrids[Stash.GetPage()]), 0)
	    << "Grid cells should be cleared after removing item";
	EXPECT_TRUE(Stash.dirty);
}

TEST_F(StashTest, RemoveItem_LastItemSwap)
{
	// Place two items, then remove the first. The second item should be
	// moved to index 0 in stashList, and grid references updated.
	Item item1 = MakeSmallItem();
	Item item2 = MakeSword();

	ASSERT_TRUE(AutoPlaceItemInStash(item1, true));
	ASSERT_TRUE(AutoPlaceItemInStash(item2, true));
	ASSERT_EQ(Stash.stashList.size(), 2u);

	// Remember the type of the second item.
	const ItemType secondItemType = Stash.stashList[1]._itype;

	Stash.RemoveStashItem(0);

	ASSERT_EQ(Stash.stashList.size(), 1u);
	// The former item at index 1 should now be at index 0.
	EXPECT_EQ(Stash.stashList[0]._itype, secondItemType);

	// Grid should reference the moved item correctly (cell value = index + 1 = 1).
	const auto &grid = Stash.stashGrids[Stash.GetPage()];
	bool foundReference = false;
	for (const auto &row : grid) {
		for (StashStruct::StashCell cell : row) {
			if (cell == 1) { // index 0 + 1
				foundReference = true;
			}
		}
	}
	EXPECT_TRUE(foundReference)
	    << "Grid should have updated references to the swapped item";
}

TEST_F(StashTest, RemoveItem_MiddleOfThree)
{
	// Place three items, remove the middle one. The last item should be
	// swapped into slot 1, and stashList should have size 2.
	Item item1 = MakeSmallItem();
	Item item2 = MakeSmallItem();
	Item item3 = MakeSmallItem();

	ASSERT_TRUE(AutoPlaceItemInStash(item1, true));
	ASSERT_TRUE(AutoPlaceItemInStash(item2, true));
	ASSERT_TRUE(AutoPlaceItemInStash(item3, true));
	ASSERT_EQ(Stash.stashList.size(), 3u);

	Stash.RemoveStashItem(1);

	EXPECT_EQ(Stash.stashList.size(), 2u);
}

// ---------------------------------------------------------------------------
// Page navigation
// ---------------------------------------------------------------------------

TEST_F(StashTest, SetPage_SetsCorrectPage)
{
	Stash.SetPage(5);
	EXPECT_EQ(Stash.GetPage(), 5u);

	Stash.SetPage(42);
	EXPECT_EQ(Stash.GetPage(), 42u);
}

TEST_F(StashTest, SetPage_ClampsToLastPage)
{
	// LastStashPage = 99 (CountStashPages - 1).
	Stash.SetPage(200);
	EXPECT_EQ(Stash.GetPage(), 99u);
}

TEST_F(StashTest, SetPage_SetsDirtyFlag)
{
	Stash.dirty = false;
	Stash.SetPage(3);
	EXPECT_TRUE(Stash.dirty);
}

TEST_F(StashTest, NextPage_AdvancesByOne)
{
	Stash.SetPage(0);
	Stash.dirty = false;

	Stash.NextPage();
	EXPECT_EQ(Stash.GetPage(), 1u);
	EXPECT_TRUE(Stash.dirty);
}

TEST_F(StashTest, NextPage_AdvancesByOffset)
{
	Stash.SetPage(5);

	Stash.NextPage(10);
	EXPECT_EQ(Stash.GetPage(), 15u);
}

TEST_F(StashTest, NextPage_ClampsAtLastPage)
{
	Stash.SetPage(98);

	Stash.NextPage(5);
	EXPECT_EQ(Stash.GetPage(), 99u) << "Should clamp to last page, not wrap around";
}

TEST_F(StashTest, NextPage_AlreadyAtLastPage)
{
	Stash.SetPage(99);

	Stash.NextPage();
	EXPECT_EQ(Stash.GetPage(), 99u) << "Should stay at last page";
}

TEST_F(StashTest, PreviousPage_GoesBackByOne)
{
	Stash.SetPage(5);
	Stash.dirty = false;

	Stash.PreviousPage();
	EXPECT_EQ(Stash.GetPage(), 4u);
	EXPECT_TRUE(Stash.dirty);
}

TEST_F(StashTest, PreviousPage_GoesBackByOffset)
{
	Stash.SetPage(20);

	Stash.PreviousPage(10);
	EXPECT_EQ(Stash.GetPage(), 10u);
}

TEST_F(StashTest, PreviousPage_ClampsAtPageZero)
{
	Stash.SetPage(2);

	Stash.PreviousPage(5);
	EXPECT_EQ(Stash.GetPage(), 0u) << "Should clamp to page 0, not underflow";
}

TEST_F(StashTest, PreviousPage_AlreadyAtPageZero)
{
	Stash.SetPage(0);

	Stash.PreviousPage();
	EXPECT_EQ(Stash.GetPage(), 0u) << "Should stay at page 0";
}

// ---------------------------------------------------------------------------
// Grid query helpers
// ---------------------------------------------------------------------------

TEST_F(StashTest, GetItemIdAtPosition_EmptyCell)
{
	Stash.SetPage(0);

	StashStruct::StashCell id = Stash.GetItemIdAtPosition({ 0, 0 });
	EXPECT_EQ(id, StashStruct::EmptyCell);
}

TEST_F(StashTest, IsItemAtPosition_EmptyCell)
{
	Stash.SetPage(0);

	EXPECT_FALSE(Stash.IsItemAtPosition({ 0, 0 }));
}

TEST_F(StashTest, GetItemIdAtPosition_OccupiedCell)
{
	Item item = MakeSmallItem();
	Stash.SetPage(0);
	ASSERT_TRUE(AutoPlaceItemInStash(item, true));

	// The first item should be placed at (0,0) in an empty stash.
	StashStruct::StashCell id = Stash.GetItemIdAtPosition({ 0, 0 });
	EXPECT_NE(id, StashStruct::EmptyCell);
	EXPECT_EQ(id, 0u) << "First item should have stashList index 0";
}

TEST_F(StashTest, IsItemAtPosition_OccupiedCell)
{
	Item item = MakeSmallItem();
	Stash.SetPage(0);
	ASSERT_TRUE(AutoPlaceItemInStash(item, true));

	EXPECT_TRUE(Stash.IsItemAtPosition({ 0, 0 }));
}

// ---------------------------------------------------------------------------
// TransferItemToInventory
// ---------------------------------------------------------------------------

TEST_F(StashTest, TransferToInventory_Success)
{
	// Clear inventory so there is room.
	StripPlayer();

	Item item = MakeSmallItem();
	Stash.SetPage(0);
	ASSERT_TRUE(AutoPlaceItemInStash(item, true));
	ASSERT_EQ(Stash.stashList.size(), 1u);

	TransferItemToInventory(*MyPlayer, 0);

	EXPECT_TRUE(Stash.stashList.empty()) << "Item should be removed from stash";
	EXPECT_EQ(CountOccupiedCells(Stash.stashGrids[0]), 0)
	    << "Grid should be cleared";

	// Item should now be in the player's inventory.
	bool foundInInventory = false;
	for (int i = 0; i < MyPlayer->_pNumInv; i++) {
		if (!MyPlayer->InvList[i].isEmpty()) {
			foundInInventory = true;
			break;
		}
	}
	EXPECT_TRUE(foundInInventory) << "Item should appear in player inventory";
}

TEST_F(StashTest, TransferToInventory_EmptyCell)
{
	// Transferring EmptyCell should be a no-op.
	TransferItemToInventory(*MyPlayer, StashStruct::EmptyCell);

	// Nothing should crash and stash should remain unchanged.
	EXPECT_TRUE(Stash.stashList.empty());
}

TEST_F(StashTest, TransferToInventory_InventoryFull)
{
	// Fill inventory completely so there's no room.
	ClearInventory();
	for (int i = 0; i < InventoryGridCells; i++) {
		Item filler = MakeSmallItem();
		MyPlayer->InvList[i] = filler;
		MyPlayer->InvGrid[i] = static_cast<int8_t>(i + 1);
	}
	MyPlayer->_pNumInv = InventoryGridCells;

	Item item = MakeSmallItem();
	Stash.SetPage(0);
	ASSERT_TRUE(AutoPlaceItemInStash(item, true));
	ASSERT_EQ(Stash.stashList.size(), 1u);

	TransferItemToInventory(*MyPlayer, 0);

	// Item should remain in stash because inventory is full.
	EXPECT_EQ(Stash.stashList.size(), 1u)
	    << "Item should remain in stash when inventory is full";
}

// ---------------------------------------------------------------------------
// TransferItemToStash
// ---------------------------------------------------------------------------

TEST_F(StashTest, TransferToStash_Success)
{
	StripPlayer();
	IsStashOpen = true;

	// Place an item in inventory slot 0.
	Item sword = MakeSword();
	int idx = PlaceItemInInventory(sword);
	ASSERT_GE(idx, 0);

	int invLocation = INVITEM_INV_FIRST + idx;

	TransferItemToStash(*MyPlayer, invLocation);

	// Item should now be in stash.
	EXPECT_FALSE(Stash.stashList.empty()) << "Item should appear in stash";

	// Item should be removed from inventory.
	EXPECT_TRUE(MyPlayer->InvList[idx].isEmpty() || MyPlayer->_pNumInv == 0)
	    << "Item should be removed from inventory";
}

TEST_F(StashTest, TransferToStash_InvalidLocation)
{
	// Transferring from location -1 should be a no-op.
	TransferItemToStash(*MyPlayer, -1);

	EXPECT_TRUE(Stash.stashList.empty());
}

// ---------------------------------------------------------------------------
// Dirty flag
// ---------------------------------------------------------------------------

TEST_F(StashTest, DirtyFlag_SetOnPlaceItem)
{
	Stash.dirty = false;

	Item item = MakeSmallItem();
	ASSERT_TRUE(AutoPlaceItemInStash(item, true));

	EXPECT_TRUE(Stash.dirty);
}

TEST_F(StashTest, DirtyFlag_SetOnPlaceGold)
{
	Stash.dirty = false;

	Item gold = MakeGold(100);
	ASSERT_TRUE(AutoPlaceItemInStash(gold, true));

	EXPECT_TRUE(Stash.dirty);
}

TEST_F(StashTest, DirtyFlag_SetOnRemoveItem)
{
	Item item = MakeSmallItem();
	ASSERT_TRUE(AutoPlaceItemInStash(item, true));
	Stash.dirty = false;

	Stash.RemoveStashItem(0);

	EXPECT_TRUE(Stash.dirty);
}

TEST_F(StashTest, DirtyFlag_SetOnPageChange)
{
	Stash.dirty = false;

	Stash.SetPage(1);

	EXPECT_TRUE(Stash.dirty);
}

TEST_F(StashTest, DirtyFlag_NotSetOnDryRun)
{
	Stash.dirty = false;

	Item item = MakeSmallItem();
	AutoPlaceItemInStash(item, false);

	EXPECT_FALSE(Stash.dirty);
}

// ---------------------------------------------------------------------------
// IsStashOpen flag
// ---------------------------------------------------------------------------

TEST_F(StashTest, IsStashOpen_InitiallyClosed)
{
	EXPECT_FALSE(IsStashOpen);
}

TEST_F(StashTest, IsStashOpen_CanBeToggled)
{
	IsStashOpen = true;
	EXPECT_TRUE(IsStashOpen);

	IsStashOpen = false;
	EXPECT_FALSE(IsStashOpen);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST_F(StashTest, PlaceItem_CurrentPagePreferred)
{
	// When the stash is empty, the item should be placed on the current page.
	Stash.SetPage(5);
	Stash.dirty = false;

	Item item = MakeSmallItem();
	ASSERT_TRUE(AutoPlaceItemInStash(item, true));

	EXPECT_TRUE(Stash.stashGrids.count(5) > 0)
	    << "Item should be placed on the current page (5)";
	EXPECT_GT(CountOccupiedCells(Stash.stashGrids[5]), 0);
}

TEST_F(StashTest, PlaceItem_WrapsAroundPages)
{
	// Set page to 99 (last page), fill it, then place another item.
	// It should wrap around to page 0.
	Stash.SetPage(99);
	FillStashPage(99);

	Item overflow = MakeSmallItem();
	Stash.SetPage(99); // Reset to page 99 so search starts there.
	EXPECT_TRUE(AutoPlaceItemInStash(overflow, true));

	// The item should have been placed on page 0 (wrapped around).
	EXPECT_TRUE(Stash.stashGrids.count(0) > 0)
	    << "Item should wrap around to page 0";
	EXPECT_GT(CountOccupiedCells(Stash.stashGrids[0]), 0);
}

TEST_F(StashTest, MultipleItemTypes_CoexistOnSamePage)
{
	Stash.SetPage(0);

	Item potion = MakeSmallItem();
	Item sword = MakeSword();

	ASSERT_TRUE(AutoPlaceItemInStash(potion, true));
	ASSERT_TRUE(AutoPlaceItemInStash(sword, true));

	EXPECT_EQ(Stash.stashList.size(), 2u);

	// Both items should be on page 0.
	const auto &grid = Stash.stashGrids[0];
	const Size swordSize = GetInventorySize(sword);
	int expectedCells = 1 + (swordSize.width * swordSize.height);
	EXPECT_EQ(CountOccupiedCells(grid), expectedCells);
}

TEST_F(StashTest, RemoveItem_ThenPlaceNew)
{
	// Place an item, remove it, then place a new one. The stash should
	// reuse the slot correctly.
	Item item1 = MakeSmallItem();
	ASSERT_TRUE(AutoPlaceItemInStash(item1, true));
	ASSERT_EQ(Stash.stashList.size(), 1u);

	Stash.RemoveStashItem(0);
	ASSERT_TRUE(Stash.stashList.empty());

	Item item2 = MakeSword();
	ASSERT_TRUE(AutoPlaceItemInStash(item2, true));
	EXPECT_EQ(Stash.stashList.size(), 1u);
}

TEST_F(StashTest, GoldStorageIndependentOfItems)
{
	// Gold and items use separate storage. Verify they don't interfere.
	Stash.SetPage(0);

	Item gold = MakeGold(5000);
	ASSERT_TRUE(AutoPlaceItemInStash(gold, true));

	Item item = MakeSmallItem();
	ASSERT_TRUE(AutoPlaceItemInStash(item, true));

	EXPECT_EQ(Stash.gold, 5000) << "Gold should be tracked separately";
	EXPECT_EQ(Stash.stashList.size(), 1u) << "Only the non-gold item should be in stashList";
}

// ---------------------------------------------------------------------------
// POE2 stash tabs
// ---------------------------------------------------------------------------

class Poe2StashTabsTest : public StashTest {
protected:
	void SetUp() override
	{
		StashTest::SetUp();
		if (IsSkipped())
			return;
		gbIsPoe2 = true;
		StashTabs.reset();
		Stash.SetPage(0);
		Stash.dirty = false;
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		StashTabs.reset();
		StashTest::TearDown();
	}

	static Item MakeOrb(item_misc_id miscId = IMISC_ORB_CHAOS, int count = 1)
	{
		Item item = MakeSmallItem();
		item._iMiscId = miscId;
		SetStackCount(item, count);
		return item;
	}

	static Item MakeUncutGem(int level)
	{
		Item item = MakeSmallItem();
		item._iMiscId = IMISC_UNCUT_SKILL;
		item._iCreateInfo = static_cast<uint16_t>(level);
		return item;
	}

	static Item MakeWaystone()
	{
		Item item = MakeSmallItem();
		item._iMiscId = IMISC_WAYSTONE;
		return item;
	}

	/** @brief A Diablo weapon that loads back in a Diablo game (the bard's sword is Hellfire's). */
	static Item MakeClub()
	{
		Item item {};
		InitializeItem(item, IDI_WARRCLUB);
		item._iIdentified = true;
		return item;
	}

	static Item MakeUnique(int uid)
	{
		Item item = MakeClub();
		item._iMagical = ITEM_QUALITY_UNIQUE;
		item._iUid = uid;
		return item;
	}

	static unsigned PageOf(StashTabKind kind)
	{
		for (unsigned page = 0; page < StashTabs.count(); page++) {
			if (StashTabs.kind(page) == kind)
				return page;
		}
		ADD_FAILURE() << "No tab of that kind";
		return 0;
	}
};

TEST_F(Poe2StashTabsTest, ItemsHaveAffinities)
{
	EXPECT_EQ(GetStashAffinity(MakeSmallItem()), StashAffinity::Flasks);
	EXPECT_EQ(GetStashAffinity(MakeOrb()), StashAffinity::Currency);
	Item sword = MakeSword();
	EXPECT_EQ(GetStashAffinity(sword), StashAffinity::None);
	sword._iMagical = ITEM_QUALITY_UNIQUE;
	EXPECT_EQ(GetStashAffinity(sword), StashAffinity::Uniques);
	EXPECT_EQ(GetStashAffinity(MakeWaystone()), StashAffinity::Waystones);
	Item tablet = MakeSmallItem();
	tablet._iMiscId = IMISC_TABLET;
	EXPECT_EQ(GetStashAffinity(tablet), StashAffinity::Waystones) << "Tablets live with the maps";
	EXPECT_EQ(GetStashAffinity(MakeUncutGem(5)), StashAffinity::Gems);
}

TEST_F(Poe2StashTabsTest, ANewStashHasThePremadeTabs)
{
	ASSERT_EQ(StashTabs.count(), DefaultStashTabs);
	for (unsigned page = 0; page < DefaultBlankStashTabs; page++)
		EXPECT_EQ(StashTabs.kind(page), StashTabKind::Blank);
	EXPECT_EQ(StashTabs.kind(DefaultBlankStashTabs + 0), StashTabKind::Currency);
	EXPECT_EQ(StashTabs.kind(DefaultBlankStashTabs + 1), StashTabKind::Gems);
	EXPECT_EQ(StashTabs.kind(DefaultBlankStashTabs + 2), StashTabKind::Maps);
	EXPECT_EQ(StashTabs.kind(DefaultBlankStashTabs + 3), StashTabKind::Uniques);
	EXPECT_EQ(StashTabs.displayName(DefaultBlankStashTabs), "Currency");
	EXPECT_EQ(StashTabs.displayName(0), "1");
}

TEST_F(Poe2StashTabsTest, AnAffinityBelongsToOneTab)
{
	StashTabs.setAffinity(0, StashAffinity::Currency, true);
	StashTabs.setAffinity(1, StashAffinity::Currency, true);
	EXPECT_FALSE(StashTabs.hasAffinity(0, StashAffinity::Currency));
	EXPECT_TRUE(StashTabs.hasAffinity(1, StashAffinity::Currency));
	EXPECT_EQ(StashTabs.affinityTabFor(MakeOrb()), 1u);
	EXPECT_EQ(StashTabs.affinityText(1), "Currency");
	StashTabs.setAffinity(1, StashAffinity::Currency, false);
	EXPECT_EQ(StashTabs.affinityTabFor(MakeOrb()), PageOf(StashTabKind::Currency)) << "Else the premade Currency tab";
	StashTabs.setAffinity(PageOf(StashTabKind::Gems), StashAffinity::Flasks, true);
	EXPECT_FALSE(StashTabs.hasAffinity(PageOf(StashTabKind::Gems), StashAffinity::Flasks)) << "Premade tabs have no affinities";
}

TEST_F(Poe2StashTabsTest, CtrlClickOnlyGoesToTheOpenTabWhenItTakesTheItem)
{
	StashTabs.setAffinity(0, StashAffinity::Currency, true);
	Stash.SetPage(0);
	EXPECT_FALSE(AutoPlaceItemInStash(MakeSword(), true)) << "A currency tab doesn't take a sword";
	EXPECT_FALSE(StashTabRefusal(0, MakeSword()).empty());
	EXPECT_TRUE(Stash.stashList.empty()) << "The sword doesn't go to another tab either";
	ASSERT_TRUE(AutoPlaceItemInStash(MakeOrb(), true));
	EXPECT_EQ(CountOccupiedCells(Stash.stashGrids[0]), 1);

	Stash.SetPage(1);
	ASSERT_TRUE(AutoPlaceItemInStash(MakeSword(), true)) << "A tab without affinities takes anything";
	EXPECT_GT(CountOccupiedCells(Stash.stashGrids[1]), 0);
	EXPECT_EQ(Stash.GetPage(), 1u);
}

TEST_F(Poe2StashTabsTest, CtrlClickFromTheInventoryLeavesRefusedItemsThere)
{
	StripPlayer();
	IsStashOpen = true;
	Stash.SetPage(PageOf(StashTabKind::Currency));
	const int idx = PlaceItemInInventory(MakeSword());
	ASSERT_GE(idx, 0);
	TransferItemToStash(*MyPlayer, INVITEM_INV_FIRST + idx);
	EXPECT_TRUE(Stash.stashList.empty());
	EXPECT_FALSE(MyPlayer->InvList[idx].isEmpty()) << "The sword stays in the inventory";

	Stash.SetPage(0);
	TransferItemToStash(*MyPlayer, INVITEM_INV_FIRST + idx);
	EXPECT_EQ(Stash.stashList.size(), 1u) << "A blank tab takes it";
	IsStashOpen = false;
}

TEST_F(Poe2StashTabsTest, DroppingOnATabThatDoesntTakeItIsRefused)
{
	IsStashOpen = true;
	StashTabs.setAffinity(0, StashAffinity::Gems, true);
	Stash.SetPage(0);
	MyPlayer->HoldItem = MakeSword();
	CheckStashItem(GetStashSlotCoord({ 2, 2 }) + Displacement { 5, 5 });
	EXPECT_FALSE(MyPlayer->HoldItem.isEmpty()) << "The sword stays on the cursor";
	EXPECT_TRUE(Stash.stashList.empty());

	MyPlayer->HoldItem = MakeUncutGem(3);
	CheckStashItem(GetStashSlotCoord({ 2, 2 }) + Displacement { 5, 5 });
	EXPECT_TRUE(MyPlayer->HoldItem.isEmpty()) << "A gem tab takes a gem";
	EXPECT_EQ(Stash.stashList.size(), 1u);
	MyPlayer->HoldItem.clear();
	IsStashOpen = false;
}

TEST_F(Poe2StashTabsTest, PagesStopAtTheLastTab)
{
	Stash.SetPage(50);
	EXPECT_EQ(Stash.GetPage(), DefaultStashTabs - 1);
	Stash.NextPage();
	EXPECT_EQ(Stash.GetPage(), DefaultStashTabs - 1);
}

TEST_F(Poe2StashTabsTest, AStashHasOneTabOfEachKindAndTwoBlankTabs)
{
	EXPECT_EQ(StashTabs.countOf(StashTabKind::Blank), MaxBlankStashTabs);
	EXPECT_FALSE(StashTabs.addTab()) << "No third blank tab";
	EXPECT_FALSE(StashTabs.addTab(StashTabKind::Gems)) << "No second Gems tab";
	EXPECT_FALSE(StashTabs.setKind(0, StashTabKind::Uniques));
	EXPECT_EQ(StashTabs.kind(0), StashTabKind::Blank);
	EXPECT_FALSE(StashTabs.setKind(PageOf(StashTabKind::Maps), StashTabKind::Blank));
	EXPECT_TRUE(StashTabs.setKind(0, StashTabKind::Blank)) << "Its own kind is fine";
	EXPECT_EQ(StashTabs.count(), DefaultStashTabs);

	// A tab let go of makes room for another of its kind.
	StashTabs.tabs.erase(StashTabs.tabs.begin() + PageOf(StashTabKind::Maps));
	EXPECT_TRUE(StashTabs.setKind(1, StashTabKind::Maps));
	EXPECT_TRUE(StashTabs.addTab());
	EXPECT_EQ(StashTabs.countOf(StashTabKind::Maps), 1u);
}

TEST_F(Poe2StashTabsTest, AMovedTabTakesItsItems)
{
	const unsigned currency = PageOf(StashTabKind::Currency);
	const unsigned last = StashTabs.count() - 1;
	Stash.SetPage(0);
	ASSERT_TRUE(AutoPlaceItemInStash(MakeClub(), true));
	Stash.SetPage(currency);
	ASSERT_TRUE(AutoPlaceItemInStash(MakeOrb(IMISC_ORB_EXALTED, 3), true));
	StashTabs.rename(0, "Clubs");
	Stash.SetPage(0);

	MoveStashTab(0, last);
	EXPECT_EQ(StashTabs.displayName(last), "Clubs");
	EXPECT_EQ(Stash.GetPage(), last) << "The open tab stays open";
	EXPECT_EQ(StashTabItems(last).size(), 1u);
	EXPECT_TRUE(StashTabItems(0).empty());
	EXPECT_EQ(StashTabs.kind(currency - 1), StashTabKind::Currency);
	const std::optional<unsigned> slot = CurrencyFixedSlot(MakeOrb(IMISC_ORB_EXALTED));
	ASSERT_TRUE(slot);
	EXPECT_NE(StashTabSlotItem(currency - 1, *slot), StashStruct::EmptyCell) << "The orb moved up with its tab";

	MoveStashTab(last, 0);
	EXPECT_EQ(StashTabs.displayName(0), "Clubs");
	EXPECT_EQ(StashTabItems(0).size(), 1u);
	EXPECT_NE(StashTabSlotItem(currency, *slot), StashStruct::EmptyCell);
}

TEST_F(Poe2StashTabsTest, ExtraTabsAreMergedIntoTheKeptOnes)
{
	// An older stash: four blank tabs and a second Uniques tab.
	StashTabs.tabs.insert(StashTabs.tabs.begin(), 2, StashTab {});
	StashTabs.tabs.push_back({ .kind = StashTabKind::Uniques });
	const unsigned secondUniques = StashTabs.count() - 1;
	Stash.SetPage(3);
	ASSERT_TRUE(AutoPlaceItemInStash(MakeClub(), true));
	Item unique = MakeUnique(2);
	ASSERT_TRUE(StoreInPremadeTab(secondUniques, unique));

	ConsolidateStashTabs();
	EXPECT_EQ(StashTabs.count(), DefaultStashTabs);
	EXPECT_EQ(StashTabs.countOf(StashTabKind::Blank), MaxBlankStashTabs);
	EXPECT_EQ(StashTabs.countOf(StashTabKind::Uniques), 1u);
	EXPECT_EQ(StashTabItems(0).size(), 1u) << "The club moved to the first blank tab";
	EXPECT_NE(StashTabSlotItem(PageOf(StashTabKind::Uniques), 2), StashStruct::EmptyCell) << "The unique moved to the Uniques tab";
	EXPECT_EQ(Stash.stashList.size(), 2u);
}

TEST_F(Poe2StashTabsTest, AnExtraTabStaysWhenItsItemsFitNowhereElse)
{
	StashTabs.tabs.insert(StashTabs.tabs.begin(), StashTab {});
	for (unsigned page = 0; page < MaxBlankStashTabs; page++) {
		Stash.SetPage(page);
		while (AutoPlaceItemInStash(MakeSmallItem(), true)) { }
	}
	Stash.SetPage(MaxBlankStashTabs);
	ASSERT_TRUE(AutoPlaceItemInStash(MakeClub(), true));
	const size_t items = Stash.stashList.size();

	ConsolidateStashTabs();
	EXPECT_EQ(StashTabs.count(), DefaultStashTabs + 1);
	EXPECT_EQ(StashTabItems(MaxBlankStashTabs).size(), 1u) << "The club stays on its tab";
	EXPECT_EQ(Stash.stashList.size(), items);
}

TEST_F(Poe2StashTabsTest, TabsSurviveSaving)
{
	StashTabs.rename(0, "Currency dump that is far too long");
	StashTabs.setColor(0, 7);
	StashTabs.setAffinity(1, StashAffinity::Gems, true);
	StashTabs.moveTab(DefaultStashTabs - 1, 0);
	const std::vector<uint8_t> data = SerializeStashTabs(StashTabs);

	StashTabSet loaded;
	uint8_t version = 0;
	ASSERT_TRUE(DeserializeStashTabs(data, loaded, &version));
	EXPECT_EQ(version, StashTabsVersion);
	ASSERT_EQ(loaded.count(), DefaultStashTabs);
	EXPECT_EQ(loaded.kind(0), StashTabKind::Uniques) << "Tabs keep their order";
	EXPECT_EQ(loaded.tabs[1].name, "Currency dump th");
	EXPECT_EQ(loaded.tabs[1].color, 7);
	EXPECT_TRUE(loaded.hasAffinity(2, StashAffinity::Gems));
	EXPECT_EQ(loaded.kind(DefaultBlankStashTabs + 1), StashTabKind::Currency);
	EXPECT_EQ(loaded.displayName(2), "2") << "Blank tabs are numbered among themselves";

	std::vector<uint8_t> broken = data;
	broken.pop_back();
	StashTabSet untouched;
	EXPECT_FALSE(DeserializeStashTabs(broken, untouched));
	EXPECT_EQ(untouched.count(), DefaultStashTabs);
}

TEST_F(Poe2StashTabsTest, VersionOneTabsStillLoad)
{
	// Version 1: count, then colour, affinities and name of each tab, and no kinds.
	const std::vector<uint8_t> data { 1, 2, 3, static_cast<uint8_t>(StashAffinity::Currency), 3, 'O', 'r', 'b', 5, 0, 0 };
	StashTabSet loaded;
	uint8_t version = 0;
	ASSERT_TRUE(DeserializeStashTabs(data, loaded, &version));
	EXPECT_EQ(version, 1);
	ASSERT_EQ(loaded.count(), 2u);
	EXPECT_EQ(loaded.tabs[0].name, "Orb");
	EXPECT_TRUE(loaded.hasAffinity(0, StashAffinity::Currency));
	EXPECT_EQ(loaded.kind(0), StashTabKind::Blank);
	EXPECT_EQ(loaded.kind(1), StashTabKind::Blank);
	loaded.addMissingPremadeTabs();
	EXPECT_EQ(loaded.count(), 6u) << "The premade tabs go after the old ones";
	EXPECT_EQ(loaded.kind(2), StashTabKind::Currency);
	EXPECT_EQ(loaded.kind(5), StashTabKind::Uniques);
}

TEST_F(Poe2StashTabsTest, CurrencyJoinsItsStackOnABlankTab)
{
	StashTabs.setAffinity(2, StashAffinity::Currency, true);
	Stash.SetPage(2);
	for (int i = 0; i < 5; i++)
		ASSERT_TRUE(AutoPlaceItemInStash(MakeOrb(), true));
	EXPECT_EQ(CountOccupiedCells(Stash.stashGrids[2]), 1);
	ASSERT_EQ(Stash.stashList.size(), 1u);
	EXPECT_EQ(StackCount(Stash.stashList[0]), 5);
}

TEST_F(Poe2StashTabsTest, SortingATabMergesStacksAndPacksItems)
{
	Stash.SetPage(1);
	// Two part stacks and a sword with gaps between them.
	Item orb = MakeOrb();
	SetStackCount(orb, 4);
	Stash.stashList.push_back(orb);
	Stash.stashList.back().position = { 9, 9 };
	Stash.stashGrids[1][9][9] = 1;
	SetStackCount(orb, 3);
	Stash.stashList.push_back(orb);
	Stash.stashList.back().position = { 5, 5 };
	Stash.stashGrids[1][5][5] = 2;
	ASSERT_TRUE(SortStashPage(1));
	ASSERT_EQ(Stash.stashList.size(), 1u);
	EXPECT_EQ(StackCount(Stash.stashList[0]), 7);
	EXPECT_EQ(Stash.stashGrids[1][0][0], 1);
	EXPECT_EQ(CountOccupiedCells(Stash.stashGrids[1]), 1);
}

TEST_F(Poe2StashTabsTest, CurrencyTabKeepsEachCurrencyInItsSlot)
{
	const unsigned page = PageOf(StashTabKind::Currency);
	Stash.SetPage(page);
	EXPECT_FALSE(AutoPlaceItemInStash(MakeSword(), true));
	for (int i = 0; i < 3; i++)
		ASSERT_TRUE(AutoPlaceItemInStash(MakeOrb(IMISC_ORB_CHAOS, 20), true));
	ASSERT_TRUE(AutoPlaceItemInStash(MakeOrb(IMISC_ORB_DIVINE, 2), true));

	const std::optional<unsigned> chaosSlot = CurrencyFixedSlot(MakeOrb(IMISC_ORB_CHAOS));
	const std::optional<unsigned> divineSlot = CurrencyFixedSlot(MakeOrb(IMISC_ORB_DIVINE));
	ASSERT_TRUE(chaosSlot && divineSlot);
	EXPECT_NE(*chaosSlot, *divineSlot);
	ASSERT_EQ(Stash.stashList.size(), 2u);
	const StashStruct::StashCell chaos = StashTabSlotItem(page, *chaosSlot);
	ASSERT_NE(chaos, StashStruct::EmptyCell);
	EXPECT_EQ(StoredStackCount(Stash.stashList[chaos]), 60) << "A currency slot holds more than an inventory stack";
	EXPECT_EQ(StoredStackCount(Stash.stashList[StashTabSlotItem(page, *divineSlot)]), 2);

	// Taking from the slot gives a full inventory stack and leaves the rest.
	const Item taken = TakeFromStash(chaos);
	EXPECT_EQ(StackCount(taken), CurrencyStackMax);
	EXPECT_EQ(StoredStackCount(Stash.stashList[StashTabSlotItem(page, *chaosSlot)]), 60 - CurrencyStackMax);
}

TEST_F(Poe2StashTabsTest, CurrencyWithoutASlotGoesToOther)
{
	const unsigned page = PageOf(StashTabKind::Currency);
	Stash.SetPage(page);
	Item oil = MakeSmallItem();
	oil._iMiscId = IMISC_OILACC;
	ASSERT_EQ(GetStashAffinity(oil), StashAffinity::Currency);
	ASSERT_TRUE(AutoPlaceItemInStash(oil, true));
	ASSERT_TRUE(AutoPlaceItemInStash(oil, true));
	const CurrencySectionRange other = GetCurrencySection(CurrencySection::Other);
	EXPECT_NE(StashTabSlotItem(page, other.first), StashStruct::EmptyCell);
	EXPECT_NE(StashTabSlotItem(page, other.first + 1), StashStruct::EmptyCell);
}

TEST_F(Poe2StashTabsTest, GemAndMapTabsFillTheirSlots)
{
	const unsigned gems = PageOf(StashTabKind::Gems);
	Stash.SetPage(gems);
	EXPECT_FALSE(AutoPlaceItemInStash(MakeWaystone(), true));
	for (int level = 1; level <= 3; level++)
		ASSERT_TRUE(AutoPlaceItemInStash(MakeUncutGem(level), true));
	EXPECT_EQ(StashTabItems(gems).size(), 3u);
	EXPECT_TRUE(GemListsBefore(MakeUncutGem(9), MakeUncutGem(2))) << "Higher levels list first";

	const unsigned maps = PageOf(StashTabKind::Maps);
	Stash.SetPage(maps);
	ASSERT_TRUE(AutoPlaceItemInStash(MakeWaystone(), true));
	EXPECT_EQ(StashTabItems(maps).size(), 1u);
	EXPECT_NE(StashTabSlotItem(maps, 0), StashStruct::EmptyCell);
}

TEST_F(Poe2StashTabsTest, PremadeTabsUseTheirExtraGrids)
{
	const unsigned gems = PageOf(StashTabKind::Gems);
	Stash.SetPage(gems);
	for (int i = 0; i < 105; i++)
		ASSERT_TRUE(AutoPlaceItemInStash(MakeUncutGem(1 + (i % 20)), true));
	EXPECT_EQ(StashTabItems(gems).size(), 105u);
	EXPECT_EQ(CountOccupiedCells(Stash.stashGrids[gems]), 100);
	EXPECT_EQ(CountOccupiedCells(Stash.stashGrids[StashGridKey(gems, 1)]), 5);
	EXPECT_EQ(StashGridPage(StashGridKey(gems, 1)), gems);

	// Removing an item from an extra grid clears it there.
	const StashStruct::StashCell last = StashTabSlotItem(gems, 104);
	ASSERT_NE(last, StashStruct::EmptyCell);
	TakeFromStash(last);
	EXPECT_EQ(CountOccupiedCells(Stash.stashGrids[StashGridKey(gems, 1)]), 4);
	EXPECT_EQ(StashTabItems(gems).size(), 104u);
}

TEST_F(Poe2StashTabsTest, UniqueCollectionHoldsOneOfEach)
{
	const unsigned page = PageOf(StashTabKind::Uniques);
	Stash.SetPage(page);
	ASSERT_GT(UniqueItems.size(), 5u);
	ASSERT_TRUE(AutoPlaceItemInStash(MakeUnique(3), true));
	EXPECT_NE(StashTabSlotItem(page, 3), StashStruct::EmptyCell);
	EXPECT_FALSE(AutoPlaceItemInStash(MakeUnique(3), true)) << "A second copy is refused";
	EXPECT_FALSE(StashTabRefusal(page, MakeUnique(3)).empty());
	EXPECT_FALSE(AutoPlaceItemInStash(MakeSword(), true)) << "Only uniques";
	ASSERT_TRUE(AutoPlaceItemInStash(MakeUnique(5), true));
	EXPECT_EQ(StashTabItems(page).size(), 2u);
}

TEST_F(Poe2StashTabsTest, CtrlClickSendsAUniqueToTheUniquesTab)
{
	const unsigned uniques = PageOf(StashTabKind::Uniques);
	Stash.SetPage(0);
	ASSERT_TRUE(AutoPlaceItemInStash(MakeUnique(4), true));
	EXPECT_NE(StashTabSlotItem(uniques, 4), StashStruct::EmptyCell) << "Into its slot on the Uniques tab";
	EXPECT_TRUE(StashTabItems(0).empty());
	ASSERT_TRUE(AutoPlaceItemInStash(MakeUnique(4), true)) << "A second copy stays on the open tab";
	EXPECT_EQ(StashTabItems(0).size(), 1u);
}

TEST_F(Poe2StashTabsTest, QuestUniquesAreInTheCollection)
{
	std::optional<size_t> crown;
	for (size_t uid = 0; uid < UniqueItems.size(); uid++) {
		if (UniqueItems[uid].UIItemId == AllItemsList[IDI_SKCROWN].iItemId)
			crown = uid;
	}
	ASSERT_TRUE(crown) << "The Undead Crown";
	const std::vector<UniqueCollectionEntry> &collection = UniqueCollection();
	ASSERT_LT(*crown, collection.size());
	EXPECT_TRUE(collection[*crown].listed) << "Leoric's crown has a slot on the Uniques tab";
}

TEST_F(Poe2StashTabsTest, RepairMovesMisplacedItems)
{
	const unsigned page = PageOf(StashTabKind::Currency);
	// An orb in the wrong slot and a sword: what a stash from before premade tabs could have there.
	Stash.stashList.push_back(MakeOrb(IMISC_ORB_CHAOS, 7));
	Stash.stashList.back().position = { 9, 9 };
	Stash.stashGrids[page][9][9] = 1;
	Stash.stashList.push_back(MakeSword());
	Stash.stashList.back().position = { 0, 2 };
	for (int y = 0; y < 3; y++)
		Stash.stashGrids[page][0][y] = 2;
	RepairStashTabs();
	ASSERT_EQ(Stash.stashList.size(), 2u);
	const std::optional<unsigned> slot = CurrencyFixedSlot(MakeOrb(IMISC_ORB_CHAOS));
	ASSERT_TRUE(slot);
	const StashStruct::StashCell orb = StashTabSlotItem(page, *slot);
	ASSERT_NE(orb, StashStruct::EmptyCell);
	EXPECT_EQ(StoredStackCount(Stash.stashList[orb]), 7);
	EXPECT_EQ(StashTabItems(page).size(), 1u) << "The sword left the currency tab";
	EXPECT_GT(CountOccupiedCells(Stash.stashGrids[0]), 0) << "It went to a blank tab";
}

class Poe2StashSaveTest : public Poe2StashTabsTest {
protected:
	void SetUp() override
	{
		Poe2StashTabsTest::SetUp();
		if (IsSkipped())
			return;
		saveDir_ = paths::BasePath() + "poe2_stash_test_" + ::testing::UnitTest::GetInstance()->current_test_info()->name() + DIRECTORY_SEPARATOR_STR;
		CreateDir(saveDir_.c_str());
		for (const std::string &file : ListFiles(saveDir_.c_str()))
			RemoveFile((saveDir_ + file).c_str());
		paths::SetPrefPath(saveDir_);
	}

	void TearDown() override
	{
		if (MyPlayer != nullptr)
			MyPlayer->poe2.hardcore = false;
		Poe2StashTabsTest::TearDown();
	}

	void SaveAndReload()
	{
		Stash.dirty = true;
		sfile_write_stash();
		Stash = {};
		StashTabs.reset();
		LoadStash();
	}

	std::string saveDir_;
};

TEST_F(Poe2StashSaveTest, PremadeTabsSurviveSavingAndLoading)
{
	const unsigned currency = PageOf(StashTabKind::Currency);
	const unsigned gems = PageOf(StashTabKind::Gems);
	const unsigned uniques = PageOf(StashTabKind::Uniques);
	StashTabs.rename(currency, "Money");
	Stash.SetPage(currency);
	for (int i = 0; i < 4; i++)
		ASSERT_TRUE(AutoPlaceItemInStash(MakeOrb(IMISC_ORB_EXALTED, 20), true));
	Stash.SetPage(gems);
	for (int i = 0; i < 103; i++)
		ASSERT_TRUE(AutoPlaceItemInStash(MakeUncutGem(4), true));
	Stash.SetPage(uniques);
	ASSERT_TRUE(AutoPlaceItemInStash(MakeUnique(2), true));
	Stash.SetPage(0);
	ASSERT_TRUE(AutoPlaceItemInStash(MakeClub(), true));
	Stash.SetPage(gems);

	SaveAndReload();

	EXPECT_EQ(Stash.GetPage(), gems);
	ASSERT_EQ(StashTabs.count(), DefaultStashTabs);
	EXPECT_EQ(StashTabs.kind(currency), StashTabKind::Currency);
	EXPECT_EQ(StashTabs.displayName(currency), "Money");
	const std::optional<unsigned> slot = CurrencyFixedSlot(MakeOrb(IMISC_ORB_EXALTED));
	ASSERT_TRUE(slot);
	const StashStruct::StashCell exalted = StashTabSlotItem(currency, *slot);
	ASSERT_NE(exalted, StashStruct::EmptyCell);
	EXPECT_EQ(StoredStackCount(Stash.stashList[exalted]), 80);
	EXPECT_EQ(StashTabItems(gems).size(), 103u) << "The gems in the tab's extra grid load too";
	EXPECT_NE(StashTabSlotItem(uniques, 2), StashStruct::EmptyCell);
	EXPECT_EQ(StashTabItems(0).size(), 1u);
}

TEST_F(Poe2StashSaveTest, HardcoreHasItsOwnStash)
{
	Stash.SetPage(PageOf(StashTabKind::Currency));
	ASSERT_TRUE(AutoPlaceItemInStash(MakeOrb(IMISC_ORB_DIVINE, 3), true));
	Stash.dirty = true;
	sfile_write_stash();

	MyPlayer->poe2.hardcore = true;
	Stash = {};
	StashTabs.reset();
	LoadStash();
	EXPECT_TRUE(Stash.stashList.empty()) << "The hardcore stash starts empty";
	Stash.SetPage(PageOf(StashTabKind::Gems));
	ASSERT_TRUE(AutoPlaceItemInStash(MakeUncutGem(2), true));
	SaveAndReload();
	EXPECT_EQ(StashTabItems(PageOf(StashTabKind::Gems)).size(), 1u);
	EXPECT_TRUE(StashTabItems(PageOf(StashTabKind::Currency)).empty());

	MyPlayer->poe2.hardcore = false;
	Stash = {};
	StashTabs.reset();
	LoadStash();
	EXPECT_EQ(StashTabItems(PageOf(StashTabKind::Currency)).size(), 1u) << "Softcore keeps its own";
	EXPECT_TRUE(StashTabItems(PageOf(StashTabKind::Gems)).empty());
}

TEST_F(Poe2StashSaveTest, AStashWithoutTabsLoadsItsPagesAsBlankTabs)
{
	// A stash saved before tabs: items on page 5, past the two blank tabs a stash has now.
	Item club = MakeClub();
	Stash.stashList.push_back(club);
	Stash.stashList.back().position = { 0, 2 };
	for (int y = 0; y < 3; y++)
		Stash.stashGrids[5][0][y] = 1;
	gbIsPoe2 = false; // writes no tabs
	Stash.dirty = true;
	sfile_write_stash();
	gbIsPoe2 = true;

	Stash = {};
	StashTabs.reset();
	LoadStash();
	ASSERT_EQ(StashTabs.count(), DefaultStashTabs) << "The extra pages go";
	EXPECT_EQ(StashTabItems(0).size(), 1u) << "The club moved to the first blank tab";
	for (const StashTabKind kind : PremadeStashTabKinds)
		EXPECT_GE(PageOf(kind), MaxBlankStashTabs) << "Premade tabs come after the blank ones";
}

TEST_F(Poe2StashSaveTest, ExtraTabsAreMergedWhenTheStashLoads)
{
	// Stashes used to start with four blank tabs.
	StashTabs.tabs.insert(StashTabs.tabs.begin(), 2, StashTab {});
	StashTabs.rename(2, "Old");
	Stash.SetPage(2);
	ASSERT_TRUE(AutoPlaceItemInStash(MakeClub(), true));
	Stash.SetPage(PageOf(StashTabKind::Gems));

	SaveAndReload();
	ASSERT_EQ(StashTabs.count(), DefaultStashTabs);
	EXPECT_EQ(StashTabItems(0).size(), 1u);
	EXPECT_EQ(Stash.GetPage(), PageOf(StashTabKind::Gems)) << "The open tab stays open";
}

} // namespace
} // namespace devilution
