#include "qol/stash.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <utility>

#ifdef USE_SDL3
#include <SDL3/SDL_keyboard.h>
#else
#include <SDL.h>
#endif

#include "DiabloUI/text_input.hpp"
#include "control/control.hpp"
#include "controls/plrctrls.h"
#include "cursor.h"
#include "engine/clx_sprite.hpp"
#include "engine/load_clx.hpp"
#include "engine/rectangle.hpp"
#include "engine/render/clx_render.hpp"
#include "engine/render/text_render.hpp"
#include "engine/size.hpp"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "hwcursor.hpp"
#include "inv.h"
#include "items.h"
#include "minitext.h"
#include "panels/stack_split_panel.hpp"
#include "panels/stash_tabs_panel.hpp"
#include "plrmsg.h"
#include "poe2/currency.hpp"
#include "poe2/item_search.hpp"
#include "poe2/stacking.hpp"
#include "poe2/stash_layouts.hpp"
#include "poe2/stash_tabs.hpp"
#include "stores.h"
#include "utils/display.h"
#include "utils/format_int.hpp"
#include "utils/language.h"
#include "utils/log.hpp"
#include "utils/sdl_compat.h"
#include "utils/str_cat.hpp"
#include "utils/utf8.hpp"

namespace devilution {

bool IsStashOpen;
StashStruct Stash;
bool IsWithdrawGoldOpen;

namespace {

constexpr unsigned CountStashPages = 100;
constexpr unsigned LastStashPage = CountStashPages - 1;

/** @brief Pages the player can reach: the POE2 stash only has the tabs added so far. */
unsigned UsableStashPages()
{
	return gbIsPoe2 ? std::clamp(StashTabs.count(), 1U, CountStashPages) : CountStashPages;
}

unsigned LastUsableStashPage()
{
	return UsableStashPages() - 1;
}

char GoldWithdrawText[21];
TextInputCursorState GoldWithdrawCursor;
std::optional<NumberInputState> GoldWithdrawInputState;

constexpr Size ButtonSize { 27, 16 };
/** Contains mappings for the buttons in the stash (2 navigation buttons, withdraw gold buttons, 2 navigation buttons) */
constexpr Rectangle StashButtonRect[] = {
	// clang-format off
	{ {  19, 19 }, ButtonSize }, // 10 left
	{ {  56, 19 }, ButtonSize }, // 1 left
	{ {  93, 19 }, ButtonSize }, // withdraw gold
	{ { 242, 19 }, ButtonSize }, // 1 right
	{ { 279, 19 }, ButtonSize }  // 10 right
	// clang-format on
};

OptionalOwnedClxSpriteList StashPanelArt;
OptionalOwnedClxSpriteList StashNavButtonArt;

/**
 * @param page The stash page index.
 * @param position Position to add the item to.
 * @param stashListIndex The item's StashList index
 * @param itemSize Size of item
 */
void AddItemToStashGrid(unsigned page, Point position, uint16_t stashListIndex, Size itemSize)
{
	for (const Point point : PointsInRectangle(Rectangle { position, itemSize })) {
		Stash.stashGrids[page][point.x][point.y] = stashListIndex + 1;
	}
}

std::optional<Point> FindTargetSlotUnderItemCursor(Point cursorPosition, Size itemSize)
{
	for (auto point : StashGridRange) {
		const Rectangle cell {
			GetStashSlotCoord(point),
			InventorySlotSizeInPixels + 1
		};

		if (cell.contains(cursorPosition)) {
			// When trying to paste into the stash we need to determine the top left cell of the nearest area that could fit the item, not the slot under the center/hot pixel.
			if (itemSize.height <= 1 && itemSize.width <= 1) {
				// top left cell of a 1x1 item is the same cell as the hot pixel, no work to do
				return point;
			}
			// Otherwise work out how far the central cell is from the top-left cell
			Displacement hotPixelCellOffset = { (itemSize.width - 1) / 2, (itemSize.height - 1) / 2 };
			// For even dimension items we need to work out if the cursor is in the left/right (or top/bottom) half of the central cell and adjust the offset so the item lands in the area most covered by the cursor.
			if (itemSize.width % 2 == 0 && cell.contains(cursorPosition + Displacement { INV_SLOT_HALF_SIZE_PX, 0 })) {
				// hot pixel was in the left half of the cell, so we want to increase the offset to preference the column to the left
				hotPixelCellOffset.deltaX++;
			}
			if (itemSize.height % 2 == 0 && cell.contains(cursorPosition + Displacement { 0, INV_SLOT_HALF_SIZE_PX })) {
				// hot pixel was in the top half of the cell, so we want to increase the offset to preference the row above
				hotPixelCellOffset.deltaY++;
			}
			// Then work out the top left cell of the nearest area that could fit this item (as pasting on the edge of the stash would otherwise put it out of bounds)
			point.y = std::clamp(point.y - hotPixelCellOffset.deltaY, 0, StashGridSize.height - itemSize.height);
			point.x = std::clamp(point.x - hotPixelCellOffset.deltaX, 0, StashGridSize.width - itemSize.width);
			return point;
		}
	}

	return {};
}

bool IsItemAllowedInStash(const Item &item)
{
	return item._iMiscId != IMISC_ARENAPOT;
}

void CheckStashPaste(Point cursorPosition)
{
	Player &player = *MyPlayer;

	if (!IsItemAllowedInStash(player.HoldItem))
		return;

	if (player.HoldItem._itype == ItemType::Gold) {
		if (Stash.gold > std::numeric_limits<int>::max() - player.HoldItem._ivalue)
			return;
		Stash.gold += player.HoldItem._ivalue;
		player.HoldItem.clear();
		PlaySFX(SfxID::ItemGold);
		Stash.dirty = true;
		NewCursor(CURSOR_HAND);
		return;
	}

	// POE2 Mashup: a tab only takes what it's for; the player switches to a tab that takes it.
	if (gbIsPoe2) {
		if (const std::string_view refusal = StashTabRefusal(Stash.GetPage(), player.HoldItem); !refusal.empty()) {
			EventPlrMsg(refusal, UiFlags::ColorWhite);
			return;
		}
	}

	const Size itemSize = GetInventorySize(player.HoldItem);

	std::optional<Point> targetSlot = FindTargetSlotUnderItemCursor(cursorPosition, itemSize);
	if (!targetSlot)
		return;

	const Point firstSlot = *targetSlot;

	// POE2 Mashup: currency dropped on a matching stack joins it, and what doesn't fit stays on the cursor.
	if (const StashStruct::StashCell iv = Stash.GetItemIdAtPosition(firstSlot); iv != StashStruct::EmptyCell && itemSize == Size { 1, 1 }) {
		Item &stack = Stash.stashList[iv];
		if (CanStackWith(stack, player.HoldItem) && StackRoom(stack) > 0) {
			PlaySFX(ItemInvSnds[ItemCAnimTbl[player.HoldItem._iCurs]]);
			MergeStack(stack, player.HoldItem);
			Stash.dirty = true;
			NewCursor(player.HoldItem);
			return;
		}
	}

	// Check that no more than 1 item is replaced by the move
	StashStruct::StashCell stashIndex = StashStruct::EmptyCell;
	for (const Point point : PointsInRectangle(Rectangle { firstSlot, itemSize })) {
		const StashStruct::StashCell iv = Stash.GetItemIdAtPosition(point);
		if (iv == StashStruct::EmptyCell || stashIndex == iv)
			continue;
		if (stashIndex == StashStruct::EmptyCell) {
			stashIndex = iv; // Found first item
			continue;
		}
		return; // Found a second item
	}

	PlaySFX(ItemInvSnds[ItemCAnimTbl[player.HoldItem._iCurs]]);

	// Need to set the item anchor position to the bottom left so drawing code functions correctly.
	player.HoldItem.position = firstSlot + Displacement { 0, itemSize.height - 1 };

	if (stashIndex == StashStruct::EmptyCell) {
		Stash.stashList.emplace_back(player.HoldItem.pop());
		// stashList will have at most 10 000 items, up to 65 535 are supported with uint16_t indexes
		stashIndex = static_cast<uint16_t>(Stash.stashList.size() - 1);
	} else {
		// swap the held item and whatever was in the stash at this position
		std::swap(Stash.stashList[stashIndex], player.HoldItem);
		// then clear the space occupied by the old item
		for (auto &row : Stash.GetCurrentGrid()) {
			for (auto &itemId : row) {
				if (itemId - 1 == stashIndex)
					itemId = 0;
			}
		}
	}

	// Finally mark the area now occupied by the pasted item in the current page/grid.
	AddItemToStashGrid(Stash.GetPage(), firstSlot, stashIndex, itemSize);

	Stash.dirty = true;

	NewCursor(player.HoldItem);
}

void CheckStashCut(Point cursorPosition, bool automaticMove)
{
	Player &player = *MyPlayer;

	CloseGoldWithdraw();

	Point slot = InvalidStashPoint;

	for (auto point : StashGridRange) {
		const Rectangle cell {
			GetStashSlotCoord(point),
			InventorySlotSizeInPixels + 1
		};

		// check which inventory rectangle the mouse is in, if any
		if (cell.contains(cursorPosition)) {
			slot = point;
			break;
		}
	}

	if (slot == InvalidStashPoint) {
		return;
	}

	Item &holdItem = player.HoldItem;
	holdItem.clear();

	bool automaticallyMoved = false;
	const bool automaticallyEquipped = false;

	const StashStruct::StashCell iv = Stash.GetItemIdAtPosition(slot);
	// POE2 Mashup: shift-click on a currency stack opens a slider to pick how many to take (Curtis 2026-10-22).
	if (iv != StashStruct::EmptyCell && automaticMove && OpenStackSplit(StackSplitSource::Stash, iv))
		return;
	if (iv != StashStruct::EmptyCell) {
		holdItem = Stash.stashList[iv];
		if (automaticMove) {
			if (CanBePlacedOnBelt(player, holdItem)) {
				automaticallyMoved = AutoPlaceItemInBelt(player, holdItem, true, true);
			} else {
				automaticallyMoved = AutoEquip(player, holdItem, true, true);
			}
		}

		if (!automaticMove || automaticallyMoved) {
			Stash.RemoveStashItem(iv);
		}
	}

	if (!holdItem.isEmpty()) {
		CalcPlrInv(player, true);
		holdItem._iStatFlag = player.CanUseItem(holdItem);
		if (automaticallyEquipped) {
			PlaySFX(ItemInvSnds[ItemCAnimTbl[holdItem._iCurs]]);
		} else if (!automaticMove || automaticallyMoved) {
			PlaySFX(SfxID::GrabItem);
		}

		if (automaticMove) {
			if (!automaticallyMoved) {
				if (CanBePlacedOnBelt(player, holdItem)) {
					player.SaySpecific(HeroSpeech::IHaveNoRoom);
				} else {
					player.SaySpecific(HeroSpeech::ICantDoThat);
				}
			}

			holdItem.clear();
		} else {
			NewCursor(holdItem);
		}
	}
}

void WithdrawGold(Player &player, int amount)
{
	AddGoldToInventory(player, amount);
	Stash.gold -= amount;
	Stash.dirty = true;
}

} // namespace

Point GetStashSlotCoord(Point slot)
{
	constexpr int StashNextCell = INV_SLOT_SIZE_PX + 1; // spacing between each cell

	return GetPanelPosition(UiPanels::Stash, slot * StashNextCell + Displacement { 17, 48 });
}

void FreeStashGFX()
{
	StashNavButtonArt = std::nullopt;
	StashPanelArt = std::nullopt;
}

void InitStash()
{
	if (!HeadlessMode) {
		StashPanelArt = LoadClx("data\\stash.clx");
		StashNavButtonArt = LoadClx("data\\stashnavbtns.clx");
	}
}

void TransferItemToInventory(Player &player, uint16_t itemId)
{
	if (itemId == StashStruct::EmptyCell) {
		return;
	}

	const Item &item = Stash.stashList[itemId];
	if (item.isEmpty()) {
		return;
	}

	// POE2 Mashup: a big currency stack gives a full inventory stack.
	if (gbIsPoe2) {
		const Item taken = PeekStashTake(itemId);
		if (!AutoPlaceItemInInventory(player, taken)) {
			player.SaySpecific(HeroSpeech::IHaveNoRoom);
			return;
		}
		PlaySFX(ItemInvSnds[ItemCAnimTbl[taken._iCurs]]);
		TakeFromStash(itemId);
		return;
	}

	if (!AutoPlaceItemInInventory(player, item)) {
		player.SaySpecific(HeroSpeech::IHaveNoRoom);
		return;
	}

	PlaySFX(ItemInvSnds[ItemCAnimTbl[item._iCurs]]);

	Stash.RemoveStashItem(itemId);
}

int StashButtonPressed = -1;

void CheckStashButtonRelease(Point mousePosition)
{
	if (StashButtonPressed == -1)
		return;

	Rectangle stashButton = StashButtonRect[StashButtonPressed];
	stashButton.position = GetPanelPosition(UiPanels::Stash, stashButton.position);
	if (stashButton.contains(mousePosition)) {
		switch (StashButtonPressed) {
		case 0:
			Stash.PreviousPage(10);
			break;
		case 1:
			Stash.PreviousPage();
			break;
		case 2:
			if (!gbIsPoe2)
				StartGoldWithdraw();
			break;
		case 3:
			Stash.NextPage();
			break;
		case 4:
			Stash.NextPage(10);
			break;
		}
	}

	StashButtonPressed = -1;
}

void CheckStashButtonPress(Point mousePosition)
{
	// POE2 Mashup: the stash is drawn in code; its tab strip has the buttons.
	if (gbIsPoe2) {
		StashButtonPressed = -1;
		return;
	}
	Rectangle stashButton;

	for (int i = 0; i < 5; i++) {
		stashButton = StashButtonRect[i];
		stashButton.position = GetPanelPosition(UiPanels::Stash, stashButton.position);
		if (stashButton.contains(mousePosition)) {
			StashButtonPressed = i;
			return;
		}
	}

	StashButtonPressed = -1;
}

void DrawStash(const Surface &out)
{
	// POE2 Mashup: no gold box or withdraw button; the frame, tab strip and premade tabs are drawn in code.
	if (gbIsPoe2) {
		DrawPoe2StashFrame(out);
		if (StashTabs.kind(Stash.GetPage()) != StashTabKind::Blank) {
			DrawStashTabUi(out);
			return;
		}
	} else {
		RenderClxSprite(out, (*StashPanelArt)[0], GetPanelPosition(UiPanels::Stash));
	}

	if (StashButtonPressed != -1) {
		const Point stashButton = GetPanelPosition(UiPanels::Stash, StashButtonRect[StashButtonPressed].position);
		RenderClxSprite(out, (*StashNavButtonArt)[StashButtonPressed], stashButton);
	}

	constexpr Displacement offset { 0, INV_SLOT_SIZE_PX - 1 };

	if (gbIsPoe2)
		DrawPoe2StashGridBack(out);

	for (auto slot : StashGridRange) {
		const StashStruct::StashCell itemId = Stash.GetItemIdAtPosition(slot);
		if (itemId == StashStruct::EmptyCell) {
			continue; // No item in the given slot
		}
		const Item &item = Stash.stashList[itemId];
		InvDrawSlotBack(out, GetStashSlotCoord(slot) + offset, InventorySlotSizeInPixels, SlotBackQuality(item));
	}

	for (auto slot : StashGridRange) {
		const StashStruct::StashCell itemId = Stash.GetItemIdAtPosition(slot);
		if (itemId == StashStruct::EmptyCell) {
			continue; // No item in the given slot
		}

		const Item &item = Stash.stashList[itemId];
		if (item.position != slot) {
			continue; // Not the first slot of the item
		}

		const int frame = item._iCurs + CURSOR_FIRSTITEM;

		const Point position = GetStashSlotCoord(item.position) + offset;
		const ClxSprite sprite = GetInvItemSprite(frame);

		if (pcursstashitem == itemId) {
			const uint8_t color = GetOutlineColor(item, true);
			ClxDrawOutline(out, color, position, sprite);
		} else if (HeldCurrencyFits(item)) {
			ClxDrawOutline(out, PAL16_ORANGE + 2, position, sprite);
		}

		DrawItem(item, out, position, sprite);
		// POE2 Mashup: a search frames the items that match and dims the rest.
		if (gbIsPoe2 && StashSearch.searching())
			DrawItemSearchMark(out, { { position.x, position.y - sprite.height() + 1 }, { sprite.width(), sprite.height() } }, StashSearch.matches(item));
	}

	const Point position = GetPanelPosition(UiPanels::Stash);
	const UiFlags style = UiFlags::VerticalCenter | UiFlags::ColorWhite;
	const int textboxHeight = 13;

	if (!gbIsPoe2) {
		DrawString(out, StrCat(Stash.GetPage() + 1), { position + Displacement { 132, 0 }, { 57, textboxHeight } },
		    { .flags = UiFlags::AlignCenter | style });
	}
	// POE2 Mashup: gold isn't deposited; the league's gold shows in the inventory.
	if (!gbIsPoe2) {
		DrawString(out, FormatInteger(Stash.gold), { position + Displacement { 122, 19 }, { 107, textboxHeight } },
		    { .flags = UiFlags::AlignRight | style });
	}
	if (gbIsPoe2)
		DrawStashTabUi(out);
}

void CheckStashItem(Point mousePosition, bool isShiftHeld, bool isCtrlHeld)
{
	// POE2 Mashup: the tab strip, and the premade tabs' own layouts.
	if (gbIsPoe2 && HandlePoe2StashClick(mousePosition, isShiftHeld, isCtrlHeld))
		return;
	if (!MyPlayer->HoldItem.isEmpty()) {
		CheckStashPaste(mousePosition);
	} else if (isCtrlHeld) {
		TransferItemToInventory(*MyPlayer, pcursstashitem);
	} else {
		CheckStashCut(mousePosition, isShiftHeld);
	}
}

uint16_t CheckStashHLight(Point mousePosition)
{
	if (IsStashTabPopupOpen())
		return -1;
	if (gbIsPoe2 && StashTabs.kind(Stash.GetPage()) != StashTabKind::Blank) {
		const StashStruct::StashCell itemId = PremadeTabItemUnderCursor(mousePosition);
		if (itemId == StashStruct::EmptyCell || itemId >= Stash.stashList.size())
			return -1;
		const Item &item = Stash.stashList[itemId];
		InfoColor = item.getTextColor();
		InfoString = item.getName();
		FloatingInfoString = item.getName();
		if (item._iIdentified)
			PrintItemDetails(item);
		else
			PrintItemDur(item);
		PrintItemComparison(item);
		return itemId;
	}
	Point slot = InvalidStashPoint;
	for (auto point : StashGridRange) {
		const Rectangle cell {
			GetStashSlotCoord(point),
			InventorySlotSizeInPixels + 1
		};

		if (cell.contains(mousePosition)) {
			slot = point;
			break;
		}
	}

	if (slot == InvalidStashPoint)
		return -1;

	InfoColor = UiFlags::ColorWhite;

	const StashStruct::StashCell itemId = Stash.GetItemIdAtPosition(slot);
	if (itemId == StashStruct::EmptyCell) {
		return -1;
	}

	const Item &item = Stash.stashList[itemId];
	if (item.isEmpty()) {
		return -1;
	}

	InfoColor = item.getTextColor();
	InfoString = item.getName();
	FloatingInfoString = item.getName();
	if (item._iIdentified) {
		PrintItemDetails(item);
	} else {
		PrintItemDur(item);
	}
	// POE2 Mashup: Shift compares stash items with what's equipped, like items in the bag.
	if (gbIsPoe2)
		PrintItemComparison(item);

	return itemId;
}

bool UseStashItem(uint16_t c)
{
	if (MyPlayer->_pInvincible && MyPlayer->hasNoLife())
		return true;
	if (pcurs != CURSOR_HAND)
		return true;
	if (IsPlayerInStore())
		return true;

	Item *item = &Stash.stashList[c];

	constexpr int SpeechDelay = 10;
	if (item->IDidx == IDI_MUSHROOM) {
		MyPlayer->Say(HeroSpeech::NowThatsOneBigMushroom, SpeechDelay);
		return true;
	}
	if (item->IDidx == IDI_FUNGALTM) {
		PlaySFX(SfxID::ItemBook);
		MyPlayer->Say(HeroSpeech::ThatDidntDoAnything, SpeechDelay);
		return true;
	}

	if (!item->isUsable())
		return false;

	if (!MyPlayer->CanUseItem(*item)) {
		MyPlayer->Say(HeroSpeech::ICantUseThisYet);
		return true;
	}

	CloseGoldWithdraw();

	if (item->isScroll()) {
		return true;
	}

	if (item->_iMiscId > IMISC_RUNEFIRST && item->_iMiscId < IMISC_RUNELAST && leveltype == DTYPE_TOWN) {
		return true;
	}

	if (item->_iMiscId == IMISC_BOOK)
		PlaySFX(SfxID::ReadBook);
	else
		PlaySFX(ItemInvSnds[ItemCAnimTbl[item->_iCurs]]);

	UseItem(*MyPlayer, item->_iMiscId, item->_iSpell, -1);

	if (Stash.stashList[c]._iMiscId == IMISC_MAPOFDOOM)
		return true;
	// POE2 Mashup: currency goes on the cursor and one comes off the stack once it changes an item, as from the bag.
	if (gbIsPoe2 && IsCurrencyMiscId(Stash.stashList[c]._iMiscId)) {
		HoldCurrencyForUse(Stash.stashList[c]);
		SetStickyItemUse((SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
		return true;
	}
	if (Stash.stashList[c]._iMiscId == IMISC_NOTE) {
		InitQTextMsg(TEXT_BOOK9);
		CloseInventory();
		return true;
	}
	Stash.RemoveStashItem(c);

	return true;
}

void StashStruct::RemoveStashItem(StashStruct::StashCell iv)
{
	// Iterate through the grids and remove every reference to item (an item is in one grid; a POE2 premade tab's
	// items can be in its extra grids).
	for (auto &[_, grid] : Stash.stashGrids) {
		for (auto &row : grid) {
			for (StashStruct::StashCell &itemId : row) {
				if (itemId - 1 == iv) {
					itemId = 0;
				}
			}
		}
	}

	if (stashList.empty()) {
		return;
	}

	// If the item at the end of stash array isn't the one we removed, we need to swap its position in the array with the removed item
	const auto lastItemIndex = static_cast<StashStruct::StashCell>(stashList.size() - 1);
	if (lastItemIndex != iv) {
		stashList[iv] = stashList[lastItemIndex];

		for (auto &[_, grid] : Stash.stashGrids) {
			for (auto &row : grid) {
				for (StashStruct::StashCell &itemId : row) {
					if (itemId == lastItemIndex + 1) {
						itemId = iv + 1;
					}
				}
			}
		}
	}
	stashList.pop_back();
	Stash.dirty = true;
}

void StashStruct::SetPage(unsigned newPage)
{
	page = std::min(newPage, LastUsableStashPage());
	dirty = true;
}

void StashStruct::NextPage(unsigned offset)
{
	const unsigned lastPage = LastUsableStashPage();
	if (page <= lastPage) {
		page += std::min(offset, lastPage - page);
	} else {
		page = lastPage;
	}
	dirty = true;
}

void StashStruct::PreviousPage(unsigned offset)
{
	const unsigned lastPage = LastUsableStashPage();
	if (page <= lastPage) {
		page -= std::min(offset, page);
	} else {
		page = lastPage;
	}
	dirty = true;
}

void StashStruct::RefreshItemStatFlags()
{
	for (auto &item : Stash.stashList) {
		item.updateRequiredStatsCacheForPlayer(*MyPlayer);
	}
}

void StartGoldWithdraw()
{
	CloseGoldDrop();

	if (ChatFlag)
		ResetChat();

	const Point start = GetPanelPosition(UiPanels::Stash, { 67, 128 });
	SDL_Rect rect = MakeSdlRect(start.x, start.y, 180, 20);
	SDL_SetTextInputArea(ghMainWnd, &rect, /*cursor=*/0);

	IsWithdrawGoldOpen = true;
	GoldWithdrawText[0] = '\0';
	GoldWithdrawInputState.emplace(NumberInputState::Options {
	    .textOptions {
	        .value = GoldWithdrawText,
	        .cursor = &GoldWithdrawCursor,
	        .maxLength = sizeof(GoldWithdrawText) - 1,
	    },
	    .min = 0,
	    .max = std::min(RoomForGold(), Stash.gold),
	});
	SDLC_StartTextInput(ghMainWnd);
}

void WithdrawGoldKeyPress(SDL_Keycode vkey)
{
	Player &myPlayer = *MyPlayer;

	if (myPlayer.hasNoLife()) {
		CloseGoldWithdraw();
		return;
	}

	switch (vkey) {
	case SDLK_RETURN:
	case SDLK_KP_ENTER:
		if (const int value = GoldWithdrawInputState->value(); value != 0) {
			WithdrawGold(myPlayer, value);
			PlaySFX(SfxID::ItemGold);
		}
		CloseGoldWithdraw();
		break;
	case SDLK_ESCAPE:
		CloseGoldWithdraw();
		break;
	default:
		break;
	}
}

void DrawGoldWithdraw(const Surface &out)
{
	if (!IsWithdrawGoldOpen) {
		return;
	}

	const std::string_view amountText = GoldWithdrawText;
	const TextInputCursorState &cursor = GoldWithdrawCursor;

	const int dialogX = 30;

	ClxDraw(out, GetPanelPosition(UiPanels::Stash, { dialogX, 178 }), (*GoldBoxBuffer)[0]);

	// Pre-wrap the string at spaces, otherwise DrawString would hard wrap in the middle of words
	const std::string wrapped = WordWrapString(_("How many gold pieces do you want to withdraw?"), 200);

	// The split gold dialog is roughly 4 lines high, but we need at least one line for the player to input an amount.
	// Using a clipping region 50 units high (approx 3 lines with a lineheight of 17) to ensure there is enough room left
	//  for the text entered by the player.
	DrawString(out, wrapped, { GetPanelPosition(UiPanels::Stash, { dialogX + 31, 75 }), { 200, 50 } },
	    { .flags = UiFlags::ColorWhitegold | UiFlags::AlignCenter, .lineHeight = 17 });

	// Even a ten digit amount of gold only takes up about half a line. There's no need to wrap or clip text here so we
	// use the Point form of DrawString.
	DrawString(out, amountText, GetPanelPosition(UiPanels::Stash, { dialogX + 37, 128 }),
	    {
	        .flags = UiFlags::ColorWhite | UiFlags::PentaCursor,
	        .cursorPosition = static_cast<int>(cursor.position),
	        .highlightRange = { static_cast<int>(cursor.selection.begin), static_cast<int>(cursor.selection.end) },
	    });
}

void CloseGoldWithdraw()
{
	if (!IsWithdrawGoldOpen)
		return;
	SDLC_StopTextInput(ghMainWnd);
	IsWithdrawGoldOpen = false;
	GoldWithdrawInputState = std::nullopt;
}

bool HandleGoldWithdrawTextInputEvent(const SDL_Event &event)
{
	return HandleNumberInputEvent(event, *GoldWithdrawInputState);
}

bool AutoPlaceItemInStash(const Item &item, bool persistItem)
{
	if (!IsItemAllowedInStash(item))
		return false;

	if (item._itype == ItemType::Gold) {
		if (Stash.gold > std::numeric_limits<int>::max() - item._ivalue)
			return false;
		if (persistItem) {
			Stash.gold += item._ivalue;
			Stash.dirty = true;
		}
		return true;
	}

	const Size itemSize = GetInventorySize(item);
	const unsigned pageCount = UsableStashPages();

	// POE2 Mashup: items go to the open tab only, and only if it takes them; the player switches tabs for the rest.
	if (gbIsPoe2) {
		const unsigned page = Stash.GetPage();
		// A unique goes to its slot on the Uniques tab whichever tab is open, when the slot is free (Curtis 2026-10-16).
		if (item._iMagical == ITEM_QUALITY_UNIQUE && StashTabs.kind(page) != StashTabKind::Uniques) {
			for (unsigned uniquesPage = 0; uniquesPage < pageCount; uniquesPage++) {
				if (StashTabs.kind(uniquesPage) != StashTabKind::Uniques || !StashTabRefusal(uniquesPage, item).empty() || !PremadeTabHasRoom(uniquesPage, item))
					continue;
				if (persistItem) {
					Item rest = item;
					StoreInPremadeTab(uniquesPage, rest);
					EventPlrMsg(StrCat(_("Sent to "), StashTabs.displayName(uniquesPage)), UiFlags::ColorWhitegold);
				}
				return true;
			}
		}
		if (!StashTabRefusal(page, item).empty())
			return false;
		if (StashTabs.kind(page) != StashTabKind::Blank) {
			if (!PremadeTabHasRoom(page, item))
				return false;
			if (persistItem) {
				Item rest = item;
				StoreInPremadeTab(page, rest);
			}
			return true;
		}
		// Currency joins matching stacks on the tab when they have room for all of it.
		if (IsStackable(item)) {
			const std::vector<uint16_t> onPage = StashTabItems(page);
			int room = 0;
			for (const uint16_t index : onPage) {
				if (CanStackWith(Stash.stashList[index], item))
					room += StackRoom(Stash.stashList[index]);
			}
			if (room >= StackCount(item)) {
				if (persistItem) {
					Item rest = item;
					for (const uint16_t index : onPage) {
						if (rest.isEmpty())
							break;
						Item &stack = Stash.stashList[index];
						if (CanStackWith(stack, rest) && StackRoom(stack) > 0)
							MergeStack(stack, rest);
					}
					Stash.dirty = true;
				}
				return true;
			}
		}
	}

	// The current page and onward; POE2 Mashup: the open tab only.
	const unsigned lastPageCounter = gbIsPoe2 ? 1 : pageCount;
	for (unsigned pageCounter = 1; pageCounter <= lastPageCounter; pageCounter++) {
		// Wrap around if needed
		const unsigned pageIndex = ((Stash.GetPage() % pageCount) + pageCounter - 1) % pageCount;
		// Search all possible position in stash grid
		for (auto stashPosition : PointsInRectangle(Rectangle { { 0, 0 }, Size { 10 - (itemSize.width - 1), 10 - (itemSize.height - 1) } })) {
			// Check that all needed slots are free
			bool isSpaceFree = true;
			for (auto itemPoint : PointsInRectangle(Rectangle { stashPosition, itemSize })) {
				const uint16_t iv = Stash.stashGrids[pageIndex][itemPoint.x][itemPoint.y];
				if (iv != 0) {
					isSpaceFree = false;
					break;
				}
			}
			if (!isSpaceFree)
				continue;
			if (persistItem) {
				Stash.stashList.push_back(item);
				const auto stashIndex = static_cast<uint16_t>(Stash.stashList.size() - 1);
				Stash.stashList[stashIndex].position = stashPosition + Displacement { 0, itemSize.height - 1 };
				AddItemToStashGrid(pageIndex, stashPosition, stashIndex, itemSize);
				Stash.dirty = true;
				if (gbIsPoe2 && IsSortedStashTab(pageIndex))
					SortStashPage(pageIndex);
			}
			return true;
		}
	}

	return false;
}

bool SortStashPage(unsigned page)
{
	if (!Stash.stashGrids.contains(page))
		return true;
	const std::vector<Item> listBefore = Stash.stashList;
	const StashStruct::StashGrid gridBefore = Stash.stashGrids[page];
	StashStruct::StashGrid &grid = Stash.stashGrids[page];

	std::vector<uint16_t> onPage;
	for (const auto &column : grid) {
		for (const StashStruct::StashCell cell : column) {
			if (cell != 0 && std::find(onPage.begin(), onPage.end(), static_cast<uint16_t>(cell - 1)) == onPage.end())
				onPage.push_back(static_cast<uint16_t>(cell - 1));
		}
	}
	std::sort(onPage.begin(), onPage.end(), [](uint16_t a, uint16_t b) {
		return StashSortsBefore(Stash.stashList[a], Stash.stashList[b]);
	});

	// Fill the fullest stacks first, from the smaller ones after them.
	for (size_t i = 0; i < onPage.size(); i++) {
		for (size_t j = i + 1; j < onPage.size(); j++)
			MergeStack(Stash.stashList[onPage[i]], Stash.stashList[onPage[j]]);
	}

	for (auto &column : grid)
		column.fill(0);
	std::vector<uint16_t> emptied;
	bool fits = true;
	for (const uint16_t index : onPage) {
		Item &item = Stash.stashList[index];
		if (item.isEmpty()) {
			emptied.push_back(index);
			continue;
		}
		const Size itemSize = GetInventorySize(item);
		bool placed = false;
		// Row by row, so a tab of currency reads left to right.
		for (int y = 0; y + itemSize.height <= StashGridSize.height && !placed; y++) {
			for (int x = 0; x + itemSize.width <= StashGridSize.width && !placed; x++) {
				bool isSpaceFree = true;
				for (const Point point : PointsInRectangle(Rectangle { { x, y }, itemSize })) {
					if (grid[point.x][point.y] != 0) {
						isSpaceFree = false;
						break;
					}
				}
				if (!isSpaceFree)
					continue;
				item.position = Point { x, y } + Displacement { 0, itemSize.height - 1 };
				AddItemToStashGrid(page, { x, y }, index, itemSize);
				placed = true;
			}
		}
		if (!placed) {
			fits = false;
			break;
		}
	}
	if (!fits) {
		Stash.stashList = listBefore;
		Stash.stashGrids[page] = gridBefore;
		return false;
	}

	// The merged-away stacks are in no grid now; drop them, highest index first so the others keep theirs.
	std::sort(emptied.begin(), emptied.end(), std::greater<>());
	for (const uint16_t index : emptied)
		Stash.RemoveStashItem(index);
	Stash.dirty = true;
	return true;
}

namespace {

Point SlotCell(unsigned slot)
{
	const unsigned cell = slot % StashGridCells;
	return { static_cast<int>(cell % 10), static_cast<int>(cell / 10) };
}

unsigned GridIndexOfKey(unsigned key)
{
	if (!IsExtraStashGridKey(key))
		return 0;
	return ((key - ExtraStashGridBase) % (MaxStashGridsPerTab - 1)) + 1;
}

void PutInSlot(unsigned page, unsigned slot, const Item &item)
{
	const Point cell = SlotCell(slot);
	Stash.stashList.push_back(item);
	Stash.stashList.back().position = cell;
	const auto index = static_cast<uint16_t>(Stash.stashList.size() - 1);
	Stash.stashGrids[StashGridKey(page, slot / StashGridCells)][cell.x][cell.y] = index + 1;
	Stash.dirty = true;
}

std::optional<unsigned> FirstFreeSlot(unsigned page, unsigned first, unsigned count, unsigned capacity)
{
	for (unsigned slot = first; slot < first + count && slot < capacity; slot++) {
		if (StashTabSlotItem(page, slot) == StashStruct::EmptyCell)
			return slot;
	}
	return std::nullopt;
}

/** Room for more of the item on a currency stack of the Currency tab. */
int StoredStackRoom(const Item &stack, const Item &item)
{
	if (!CanStackWith(stack, item))
		return 0;
	return CurrencyTabStackMax - StoredStackCount(stack);
}

/** Moves what fits of item onto a stack of the Currency tab; true when all of it went. */
bool MergeStored(Item &stack, Item &item)
{
	const int moved = std::min(StoredStackRoom(stack, item), StoredStackCount(item));
	if (moved <= 0)
		return false;
	SetStoredStackCount(stack, StoredStackCount(stack) + moved);
	Stash.dirty = true;
	const int left = StoredStackCount(item) - moved;
	if (left <= 0) {
		item.clear();
		return true;
	}
	SetStoredStackCount(item, left);
	return false;
}

/** Where a premade tab keeps the item: a fixed slot, or the first free one in a range. */
struct PremadePlacement {
	std::optional<unsigned> fixedSlot;
	unsigned first = 0;
	unsigned count = 0;
	/** Stacks join matching ones in the range. */
	bool stacks = false;
};

std::optional<PremadePlacement> GetPremadePlacement(StashTabKind kind, const Item &item)
{
	if (!PremadeTabTakes(kind, item))
		return std::nullopt;
	const unsigned capacity = PremadeTabCapacity(kind);
	switch (kind) {
	case StashTabKind::Currency: {
		if (const std::optional<unsigned> slot = CurrencyFixedSlot(item))
			return PremadePlacement { .fixedSlot = slot };
		const CurrencySectionRange other = GetCurrencySection(CurrencySection::Other);
		return PremadePlacement { .fixedSlot = std::nullopt, .first = other.first, .count = other.count, .stacks = IsStackable(item) };
	}
	case StashTabKind::Uniques: {
		const std::optional<unsigned> slot = UniqueCollectionSlot(item);
		if (!slot || *slot >= capacity)
			return std::nullopt;
		return PremadePlacement { .fixedSlot = slot };
	}
	default:
		return PremadePlacement { .fixedSlot = std::nullopt, .first = 0, .count = capacity };
	}
}

/** The premade tab would keep the item in this slot. */
bool IsPremadeSlotRight(StashTabKind kind, unsigned slot, const Item &item)
{
	const std::optional<PremadePlacement> placement = GetPremadePlacement(kind, item);
	if (!placement)
		return false;
	if (placement->fixedSlot)
		return slot == *placement->fixedSlot;
	return slot >= placement->first && slot < placement->first + placement->count;
}

/** Stores a repaired item: its own premade tab first, then any tab, splitting stacks too big for a blank tab. */
void PlaceRepairedItem(Item item, unsigned page)
{
	if (StashTabs.kind(page) != StashTabKind::Blank && StoreInPremadeTab(page, item))
		return;
	for (unsigned other = 0; other < StashTabs.count() && !item.isEmpty(); other++) {
		if (StashTabs.kind(other) == StashTabKind::Currency)
			StoreInPremadeTab(other, item);
	}
	while (!item.isEmpty()) {
		Item part = item;
		if (IsStackable(item) && StoredStackCount(item) > CurrencyStackMax) {
			SetStackCount(part, CurrencyStackMax);
			SetStoredStackCount(item, StoredStackCount(item) - CurrencyStackMax);
		} else {
			if (IsStackable(part))
				SetStackCount(part, StoredStackCount(part));
			item.clear();
		}
		if (!AutoPlaceItemInAnyStashTab(part))
			LogError("Stash: no room for {}, it was lost", part._iIName);
	}
}

} // namespace

std::vector<uint16_t> StashTabItems(unsigned page)
{
	std::vector<uint16_t> items;
	const unsigned grids = StashTabs.kind(page) == StashTabKind::Blank ? 1 : MaxStashGridsPerTab;
	for (unsigned grid = 0; grid < grids; grid++) {
		const auto found = Stash.stashGrids.find(StashGridKey(page, grid));
		if (found == Stash.stashGrids.end())
			continue;
		for (const auto &column : found->second) {
			for (const StashStruct::StashCell cell : column) {
				if (cell != 0 && cell - 1U < Stash.stashList.size() && std::find(items.begin(), items.end(), static_cast<uint16_t>(cell - 1)) == items.end())
					items.push_back(static_cast<uint16_t>(cell - 1));
			}
		}
	}
	return items;
}

bool IsStashTabEmpty(unsigned page)
{
	for (unsigned grid = 0; grid < MaxStashGridsPerTab; grid++) {
		const auto found = Stash.stashGrids.find(StashGridKey(page, grid));
		if (found == Stash.stashGrids.end())
			continue;
		for (const auto &column : found->second) {
			for (const StashStruct::StashCell cell : column) {
				if (cell != 0)
					return false;
			}
		}
	}
	return true;
}

StashStruct::StashCell StashTabSlotItem(unsigned page, unsigned slot)
{
	if (slot >= MaxStashGridsPerTab * StashGridCells)
		return StashStruct::EmptyCell;
	const auto found = Stash.stashGrids.find(StashGridKey(page, slot / StashGridCells));
	if (found == Stash.stashGrids.end())
		return StashStruct::EmptyCell;
	const Point cell = SlotCell(slot);
	return found->second[cell.x][cell.y] - 1;
}

std::string_view StashTabRefusal(unsigned page, const Item &item)
{
	if (!gbIsPoe2 || item.isEmpty())
		return {};
	if (!StashTabs.accepts(page, item))
		return _("This tab doesn't take that item.");
	if (StashTabs.kind(page) == StashTabKind::Uniques) {
		const std::optional<unsigned> slot = UniqueCollectionSlot(item);
		if (slot && StashTabSlotItem(page, *slot) != StashStruct::EmptyCell)
			return _("That unique is already in the collection.");
	}
	return {};
}

bool PremadeTabHasRoom(unsigned page, const Item &item)
{
	const StashTabKind kind = StashTabs.kind(page);
	const std::optional<PremadePlacement> placement = GetPremadePlacement(kind, item);
	if (!placement)
		return false;
	if (placement->fixedSlot) {
		const StashStruct::StashCell index = StashTabSlotItem(page, *placement->fixedSlot);
		if (index == StashStruct::EmptyCell)
			return true;
		return IsStackable(item) && StoredStackRoom(Stash.stashList[index], item) >= StoredStackCount(item);
	}
	int room = 0;
	for (unsigned slot = placement->first; slot < placement->first + placement->count && slot < PremadeTabCapacity(kind); slot++) {
		const StashStruct::StashCell index = StashTabSlotItem(page, slot);
		if (index == StashStruct::EmptyCell)
			return true;
		if (placement->stacks)
			room += StoredStackRoom(Stash.stashList[index], item);
	}
	return placement->stacks && room >= StoredStackCount(item);
}

bool StoreInPremadeTab(unsigned page, Item &item)
{
	const StashTabKind kind = StashTabs.kind(page);
	const std::optional<PremadePlacement> placement = GetPremadePlacement(kind, item);
	if (!placement || item.isEmpty())
		return false;
	if (placement->fixedSlot) {
		const StashStruct::StashCell index = StashTabSlotItem(page, *placement->fixedSlot);
		if (index == StashStruct::EmptyCell) {
			PutInSlot(page, *placement->fixedSlot, item);
			item.clear();
			return true;
		}
		return IsStackable(item) && MergeStored(Stash.stashList[index], item);
	}
	const unsigned capacity = PremadeTabCapacity(kind);
	if (placement->stacks) {
		for (unsigned slot = placement->first; slot < placement->first + placement->count && slot < capacity; slot++) {
			const StashStruct::StashCell index = StashTabSlotItem(page, slot);
			if (index != StashStruct::EmptyCell && MergeStored(Stash.stashList[index], item))
				return true;
		}
	}
	const std::optional<unsigned> slot = FirstFreeSlot(page, placement->first, placement->count, capacity);
	if (!slot)
		return false;
	PutInSlot(page, *slot, item);
	item.clear();
	return true;
}

Item PeekStashTake(uint16_t index)
{
	if (index >= Stash.stashList.size())
		return {};
	Item taken = Stash.stashList[index];
	if (IsStackable(taken))
		SetStackCount(taken, std::min(StoredStackCount(taken), CurrencyStackMax));
	return taken;
}

Item TakeFromStash(uint16_t index)
{
	if (index >= Stash.stashList.size())
		return {};
	Item taken = PeekStashTake(index);
	Item &stored = Stash.stashList[index];
	if (IsStackable(stored) && StoredStackCount(stored) > CurrencyStackMax) {
		SetStoredStackCount(stored, StoredStackCount(stored) - CurrencyStackMax);
		Stash.dirty = true;
	} else {
		Stash.RemoveStashItem(index);
	}
	return taken;
}

Item TakeFromStash(uint16_t index, int count)
{
	if (index >= Stash.stashList.size() || count <= 0)
		return {};
	Item &stored = Stash.stashList[index];
	const int have = StoredStackCount(stored);
	count = std::min(count, CurrencyStackMax);
	if (!IsStackable(stored) || count >= have)
		return TakeFromStash(index);
	Item taken = stored;
	GenerateNewSeed(taken); // Its own seed, so the two parts are told apart.
	SetStackCount(taken, count);
	SetStoredStackCount(stored, have - count);
	Stash.dirty = true;
	return taken;
}

bool AutoPlaceItemInAnyStashTab(const Item &item)
{
	if (!gbIsPoe2)
		return AutoPlaceItemInStash(item, true);
	const unsigned current = Stash.GetPage();
	bool placed = false;
	for (unsigned page = 0; page < StashTabs.count() && !placed; page++) {
		Stash.SetPage(page);
		placed = AutoPlaceItemInStash(item, true);
	}
	while (!placed && StashTabs.addOverflowTab()) {
		Stash.SetPage(StashTabs.count() - 1);
		placed = AutoPlaceItemInStash(item, true);
	}
	Stash.SetPage(current);
	return placed;
}

namespace {

/** @brief Gives each grid the key of its tab's new page; pages past the tabs keep theirs. */
template <typename NewPage>
void RekeyStashGrids(NewPage newPage)
{
	ankerl::unordered_dense::map<unsigned, StashStruct::StashGrid> grids;
	for (auto &[key, grid] : Stash.stashGrids) {
		const std::optional<unsigned> page = newPage(StashGridPage(key));
		if (page)
			grids[StashGridKey(*page, GridIndexOfKey(key))] = grid;
	}
	Stash.stashGrids = std::move(grids);
	Stash.dirty = true;
}

/** @brief Removes an empty tab; the tabs after it move up a page. */
void RemoveEmptyStashTab(unsigned page)
{
	const unsigned open = Stash.GetPage();
	RekeyStashGrids([page](unsigned owner) -> std::optional<unsigned> {
		if (owner == page)
			return std::nullopt;
		return owner > page ? owner - 1 : owner;
	});
	StashTabs.tabs.erase(StashTabs.tabs.begin() + page);
	Stash.SetPage(open > page ? open - 1 : open);
}

/** @brief A tab too many: a second tab of a premade kind, or a blank tab after the first two. */
bool IsSurplusStashTab(unsigned page)
{
	const StashTabKind kind = StashTabs.kind(page);
	unsigned before = 0;
	for (unsigned other = 0; other < page; other++) {
		if (StashTabs.kind(other) == kind)
			before++;
	}
	return before >= (kind == StashTabKind::Blank ? MaxBlankStashTabs : 1U);
}

bool PlaceOnBlankTab(unsigned page, const Item &item)
{
	const unsigned open = Stash.GetPage();
	Stash.SetPage(page);
	const bool placed = AutoPlaceItemInStash(item, true);
	Stash.SetPage(open);
	return placed;
}

/** @brief Stores the item on the tabs that are kept: its premade tab, else a blank one. What doesn't fit stays in item. */
bool PlaceOnKeptTabs(Item &item, unsigned surplusPage)
{
	for (unsigned page = 0; page < StashTabs.count() && !item.isEmpty(); page++) {
		if (page != surplusPage && StashTabs.kind(page) != StashTabKind::Blank && !IsSurplusStashTab(page))
			StoreInPremadeTab(page, item);
	}
	// A blank tab holds stacks of an inventory stack's size at most.
	while (!item.isEmpty()) {
		Item part = item;
		const bool split = IsStackable(item) && StoredStackCount(item) > CurrencyStackMax;
		if (IsStackable(part))
			SetStackCount(part, std::min(StoredStackCount(item), CurrencyStackMax));
		bool placed = false;
		for (unsigned page = 0; page < StashTabs.count() && !placed; page++) {
			if (page != surplusPage && StashTabs.kind(page) == StashTabKind::Blank && !IsSurplusStashTab(page))
				placed = PlaceOnBlankTab(page, part);
		}
		if (!placed)
			return false;
		if (split)
			SetStoredStackCount(item, StoredStackCount(item) - CurrencyStackMax);
		else
			item.clear();
	}
	return true;
}

} // namespace

void MoveStashTab(unsigned from, unsigned to)
{
	const unsigned count = StashTabs.count();
	if (from >= count || to >= count || from == to)
		return;
	const auto newPage = [from, to](unsigned page) -> unsigned {
		if (page == from)
			return to;
		if (from < to && page > from && page <= to)
			return page - 1;
		if (to < from && page >= to && page < from)
			return page + 1;
		return page;
	};
	const unsigned open = Stash.GetPage();
	RekeyStashGrids([&newPage](unsigned page) -> std::optional<unsigned> { return newPage(page); });
	StashTabs.moveTab(from, to);
	Stash.SetPage(newPage(open));
}

void ConsolidateStashTabs()
{
	if (!gbIsPoe2)
		return;
	// From the last tab back, so removing one leaves the pages still to look at where they were.
	for (unsigned page = StashTabs.count(); page-- > 0;) {
		if (!IsSurplusStashTab(page))
			continue;
		std::vector<uint16_t> indices = StashTabItems(page);
		std::vector<Item> items;
		items.reserve(indices.size());
		for (const uint16_t index : indices)
			items.push_back(Stash.stashList[index]);
		std::sort(indices.begin(), indices.end(), std::greater<>());
		for (const uint16_t index : indices)
			Stash.RemoveStashItem(index);
		bool emptied = true;
		for (Item &item : items) {
			if (PlaceOnKeptTabs(item, page))
				continue;
			// It stays on this tab, which stays too.
			emptied = false;
			const bool back = StashTabs.kind(page) == StashTabKind::Blank ? PlaceOnBlankTab(page, item) : StoreInPremadeTab(page, item);
			if (!back && !AutoPlaceItemInAnyStashTab(item))
				LogError("Stash: no room for {}, it was lost", item._iIName);
		}
		if (emptied && IsStashTabEmpty(page))
			RemoveEmptyStashTab(page);
	}
}

void FitOneCellStashItems()
{
	for (auto &[key, grid] : Stash.stashGrids) {
		for (int x = 0; x < 10; x++) {
			for (int y = 0; y < 10; y++) {
				const StashStruct::StashCell cell = grid[x][y];
				if (cell == 0 || cell - 1U >= Stash.stashList.size())
					continue;
				const Item &item = Stash.stashList[cell - 1];
				if (TakesOneCell(item) && item.position != Point { x, y })
					grid[x][y] = 0;
			}
		}
	}
}

void RepairStashTabs()
{
	if (!gbIsPoe2)
		return;
	std::vector<unsigned> cellCount(Stash.stashList.size());
	for (const auto &[key, grid] : Stash.stashGrids) {
		for (const auto &column : grid) {
			for (const StashStruct::StashCell cell : column) {
				if (cell != 0 && cell - 1U < cellCount.size())
					cellCount[cell - 1]++;
			}
		}
	}
	std::vector<std::pair<uint16_t, unsigned>> moving;
	const auto isMoving = [&moving](unsigned index) {
		return std::any_of(moving.begin(), moving.end(), [index](const auto &entry) { return entry.first == index; });
	};
	for (auto &[key, grid] : Stash.stashGrids) {
		const unsigned page = StashGridPage(key);
		const StashTabKind kind = StashTabs.kind(page);
		const bool extraGrid = IsExtraStashGridKey(key);
		if (kind == StashTabKind::Blank && !extraGrid)
			continue;
		const unsigned capacity = PremadeTabCapacity(kind);
		for (int x = 0; x < 10; x++) {
			for (int y = 0; y < 10; y++) {
				const StashStruct::StashCell cell = grid[x][y];
				if (cell == 0)
					continue;
				const unsigned index = cell - 1U;
				if (index >= Stash.stashList.size()) {
					grid[x][y] = 0;
					continue;
				}
				const unsigned slot = (GridIndexOfKey(key) * StashGridCells) + static_cast<unsigned>((y * 10) + x);
				const bool right = kind != StashTabKind::Blank && page < StashTabs.count() && cellCount[index] == 1
				    && slot < capacity && IsPremadeSlotRight(kind, slot, Stash.stashList[index]);
				if (right) {
					Stash.stashList[index].position = { x, y };
				} else if (!isMoving(index)) {
					moving.emplace_back(static_cast<uint16_t>(index), page);
				}
			}
		}
	}
	if (moving.empty())
		return;
	std::vector<std::pair<Item, unsigned>> items;
	for (const auto &[index, page] : moving)
		items.emplace_back(Stash.stashList[index], page);
	std::sort(moving.begin(), moving.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
	for (const auto &[index, page] : moving)
		Stash.RemoveStashItem(index);
	for (auto &[item, page] : items)
		PlaceRepairedItem(item, page < StashTabs.count() ? page : 0);
	Stash.dirty = true;
}

} // namespace devilution
