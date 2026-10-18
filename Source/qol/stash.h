/**
 * @file qol/stash.h
 *
 * Interface of player stash.
 */
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <ankerl/unordered_dense.h>

#include "engine/point.hpp"
#include "engine/points_in_rectangle_range.hpp"
#include "items.h"
#include "utils/attributes.h"

namespace devilution {

class StashStruct {
public:
	using StashCell = uint16_t;
	using StashGrid = std::array<std::array<StashCell, 10>, 10>;
	static constexpr StashCell EmptyCell = -1;

	void RemoveStashItem(StashCell iv);
	ankerl::unordered_dense::map<unsigned, StashGrid> stashGrids;
	std::vector<Item> stashList;
	int gold;
	bool dirty = false;

	unsigned GetPage() const
	{
		return page;
	}

	StashGrid &GetCurrentGrid()
	{
		return stashGrids[GetPage()];
	}

	/**
	 * @brief Returns the 0-based index of the item at the specified position, or EmptyCell if no item occupies that slot
	 * @param gridPosition x,y coordinate of the current stash page
	 * @return a value which can be used to index into stashList or StashStruct::EmptyCell
	 */
	StashCell GetItemIdAtPosition(Point gridPosition)
	{
		// Because StashCell is an unsigned type we can let this underflow
		return GetCurrentGrid()[gridPosition.x][gridPosition.y] - 1;
	}

	bool IsItemAtPosition(Point gridPosition)
	{
		return GetItemIdAtPosition(gridPosition) != EmptyCell;
	}

	void SetPage(unsigned newPage);
	void NextPage(unsigned offset = 1);
	void PreviousPage(unsigned offset = 1);

	/** @brief Updates _iStatFlag for all stash items. */
	void RefreshItemStatFlags();

private:
	/** Current Page */
	unsigned page;
};

constexpr Point InvalidStashPoint { -1, -1 };

extern DVL_API_FOR_TEST bool IsStashOpen;
extern DVL_API_FOR_TEST StashStruct Stash;

extern bool IsWithdrawGoldOpen;
extern int WithdrawGoldValue;

inline constexpr Size StashGridSize { 10, 10 };
inline constexpr PointsInRectangle<int> StashGridRange { { { 0, 0 }, StashGridSize } };

Point GetStashSlotCoord(Point slot);
void InitStash();
void FreeStashGFX();
void TransferItemToInventory(Player &player, uint16_t itemId);
/**
 * @brief Render the inventory panel to the given buffer.
 */
void DrawStash(const Surface &out);
void CheckStashItem(Point mousePosition, bool isShiftHeld = false, bool isCtrlHeld = false);
bool UseStashItem(uint16_t cii);
uint16_t CheckStashHLight(Point mousePosition);
void CheckStashButtonRelease(Point mousePosition);
void CheckStashButtonPress(Point mousePosition);

void StartGoldWithdraw();
void WithdrawGoldKeyPress(SDL_Keycode vkey);
void DrawGoldWithdraw(const Surface &out);
void CloseGoldWithdraw();
bool HandleGoldWithdrawTextInputEvent(const SDL_Event &event);

/**
 * @brief Checks whether the given item can be placed on the stash.
 * If 'persistItem' is 'True', the item is also placed in the inventory.
 * @param item The item to be checked.
 * @param persistItem Pass 'True' to actually place the item in the inventory. The default is 'False'.
 * @return 'True' in case the item can be placed on the player's inventory and 'False' otherwise.
 */
bool AutoPlaceItemInStash(const Item &item, bool persistItem);

/**
 * @brief POE2 Mashup: sorts a stash page and merges its currency stacks.
 * @return false if the sorted items would not fit, and the page is left as it was.
 */
bool SortStashPage(unsigned page);

/** @brief POE2 Mashup: the stash items on a tab, from all its grids, each once. */
std::vector<uint16_t> StashTabItems(unsigned page);
bool IsStashTabEmpty(unsigned page);
/** @brief POE2 Mashup: the stash item in a premade tab's slot, or EmptyCell. */
StashStruct::StashCell StashTabSlotItem(unsigned page, unsigned slot);
/** @brief POE2 Mashup: why the tab won't take the item, or empty when it will. */
std::string_view StashTabRefusal(unsigned page, const Item &item);
/** @brief POE2 Mashup: whether all of the item fits on the premade tab. */
bool PremadeTabHasRoom(unsigned page, const Item &item);
/**
 * @brief POE2 Mashup: puts as much of the item on the premade tab as fits, in its slot; the rest stays in item.
 * @return Whether all of it went in.
 */
bool StoreInPremadeTab(unsigned page, Item &item);
/** @brief POE2 Mashup: what taking the stash item gives: a currency stack bigger than an inventory stack gives a full inventory stack. */
Item PeekStashTake(uint16_t index);
/** @brief POE2 Mashup: takes PeekStashTake's item out of the stash. */
Item TakeFromStash(uint16_t index);
/** @brief POE2 Mashup: puts the item on whichever tab takes it, adding a tab if need be; for when it can go nowhere else. */
bool AutoPlaceItemInAnyStashTab(const Item &item);
/** @brief POE2 Mashup: items a premade tab doesn't hold the way it holds them are stored again; for loading older stashes. */
void RepairStashTabs();
/** @brief POE2 Mashup: stash items saved when they were bigger let go of the cells they no longer cover. */
void FitOneCellStashItems();
/** @brief POE2 Mashup: moves a tab to another place in the order, its grids and items with it. */
void MoveStashTab(unsigned from, unsigned to);
/**
 * @brief POE2 Mashup: a stash keeps one tab of each premade kind and two blank tabs (Curtis 2026-10-17). Items on any
 * others move to the tabs that are kept, and the emptied tabs go; a tab whose items fit nowhere else stays.
 */
void ConsolidateStashTabs();

} // namespace devilution
