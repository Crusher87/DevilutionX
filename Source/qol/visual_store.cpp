/**
 * @file qol/visual_store.cpp
 *
 * Implementation of visual grid-based store UI.
 */
#include "qol/visual_store.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>

#include "control/control.hpp"
#include "controls/control_mode.hpp"
#include "controls/plrctrls.h"
#include "cursor.h"
#include "engine/clx_sprite.hpp"
#include "engine/load_clx.hpp"
#include "engine/points_in_rectangle_range.hpp"
#include "engine/rectangle.hpp"
#include "engine/render/clx_render.hpp"
#include "engine/render/text_render.hpp"
#include "engine/size.hpp"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "inv.h"
#include "items.h"
#include "minitext.h"
#include "options.h"
#include "panels/info_box.hpp"
#include "panels/ui_panels.hpp"
#include "player.h"
#include "poe2/loot.hpp"
#include "poe2/stacking.hpp"
#include "qol/stash.h"
#include "spells.h"
#include "stores.h"
#include "utils/format_int.hpp"
#include "utils/language.h"
#include "utils/str_cat.hpp"

namespace devilution {

bool IsVisualStoreOpen;
VisualStoreState VisualStore;
int16_t pcursstoreitem = -1;
int16_t pcursstorebtn = -1;
std::vector<BuybackEntry> SmithBuyback;
std::vector<BuybackEntry> WitchBuyback;
Poe2ShopTab ActivePoe2ShopTab = Poe2ShopTab::Weapons;

bool UseVisualStoreUi()
{
	return gbIsPoe2 || *GetOptions().Gameplay.storeUi == StoreUi::VisualGrid;
}

namespace {

/** POE2 Mashup: where an item on the shop's current tab really lives, so buying it takes it from there. */
enum class ShopSource : uint8_t {
	Smith,
	Premium,
	Witch,
	Healer,
	Boy,
	Buyback,
};

struct ShopOrigin {
	ShopSource source;
	size_t index;
};

/** POE2 Mashup: the items on the current tab, copied from the vendor's lists, and where each came from. */
std::vector<Item> ShopView;
std::vector<ShopOrigin> ShopOrigins;

/** Buttons for POE2 tabs start at this id, after the four classic buttons. */
constexpr int Poe2TabButtonFirst = 10;
constexpr int Poe2TabWidth = 72;
constexpr int Poe2TabGap = 1;

std::vector<BuybackEntry> *VendorBuyback()
{
	switch (VisualStore.vendor) {
	case VisualStoreVendor::Smith:
		return &SmithBuyback;
	case VisualStoreVendor::Witch:
		return &WitchBuyback;
	default:
		return nullptr;
	}
}

bool ItemOnShopTab(const Item &item, Poe2ShopTab tab)
{
	switch (tab) {
	case Poe2ShopTab::Weapons:
		return item._iClass == ICLASS_WEAPON;
	case Poe2ShopTab::Armour:
		return item._iClass == ICLASS_ARMOR;
	case Poe2ShopTab::Other:
		return item._iClass != ICLASS_WEAPON && item._iClass != ICLASS_ARMOR;
	default:
		return true;
	}
}

void AddToShopView(std::span<Item> items, ShopSource source)
{
	for (size_t i = 0; i < items.size(); i++) {
		if (!items[i].isEmpty() && ItemOnShopTab(items[i], ActivePoe2ShopTab)) {
			ShopView.push_back(items[i]);
			ShopOrigins.push_back({ source, i });
		}
	}
}

void RebuildShopView()
{
	ShopView.clear();
	ShopOrigins.clear();
	if (ActivePoe2ShopTab == Poe2ShopTab::Buyback) {
		if (std::vector<BuybackEntry> *buyback = VendorBuyback(); buyback != nullptr) {
			// Newest sale first.
			for (size_t i = buyback->size(); i-- > 0;) {
				ShopView.push_back((*buyback)[i].item);
				ShopOrigins.push_back({ ShopSource::Buyback, i });
			}
		}
	} else {
		switch (VisualStore.vendor) {
		case VisualStoreVendor::Smith:
			// One list of everything Griswold sells, basic and premium, split into tabs by kind of item.
			AddToShopView({ PremiumItems.data(), PremiumItems.size() }, ShopSource::Premium);
			AddToShopView({ SmithItems.data(), SmithItems.size() }, ShopSource::Smith);
			break;
		case VisualStoreVendor::Witch:
			AddToShopView({ WitchItems.data(), WitchItems.size() }, ShopSource::Witch);
			break;
		case VisualStoreVendor::Healer:
			AddToShopView({ HealerItems.data(), HealerItems.size() }, ShopSource::Healer);
			break;
		case VisualStoreVendor::Boy:
			if (!BoyItem.isEmpty()) {
				ShopView.push_back(BoyItem);
				ShopOrigins.push_back({ ShopSource::Boy, 0 });
			}
			break;
		}
	}
	for (Item &item : ShopView)
		item._iStatFlag = MyPlayer->CanUseItem(item);
}

std::string_view Poe2ShopTabName(Poe2ShopTab tab)
{
	switch (tab) {
	case Poe2ShopTab::Weapons:
		return _("Weapons");
	case Poe2ShopTab::Armour:
		return _("Armour");
	case Poe2ShopTab::Other:
		return _("Other");
	case Poe2ShopTab::Items:
		return _("Items");
	default:
		return _("Buyback");
	}
}

Rectangle Poe2TabRect(size_t i)
{
	return { GetPanelPosition(UiPanels::Stash, { 14 + (static_cast<int>(i) * (Poe2TabWidth + Poe2TabGap)), 21 }), { Poe2TabWidth, 22 } };
}

/** @brief Remembers a sold item so it can be bought back. */
void AddToBuyback(const Item &item, int price)
{
	std::vector<BuybackEntry> *buyback = VendorBuyback();
	if (!gbIsPoe2 || buyback == nullptr)
		return;
	if (buyback->size() >= MaxBuybackItems)
		buyback->erase(buyback->begin());
	buyback->push_back({ item, price });
}

OptionalOwnedClxSpriteList VisualStorePanelArt;
OptionalOwnedClxSpriteList VisualStoreNavButtonArt;
OptionalOwnedClxSpriteList VisualStoreRepairAllButtonArt;
OptionalOwnedClxSpriteList VisualStoreRepairButtonArt;

int VisualStoreButtonPressed = -1;

constexpr Size ButtonSize { 27, 16 };

/** Contains mappings for the buttons in the visual store (tabs, repair) */
constexpr Rectangle VisualStoreButtonRect[] = {
	// Tab buttons (Smith only) - positioned below title
	{ { 14, 21 }, { 72, 22 } },      // Basic tab
	{ { 14 + 73, 21 }, { 72, 22 } }, // Premium tab
	{ { 233, 315 }, { 48, 24 } },    // Repair All Btn
	{ { 286, 315 }, { 24, 24 } },    // Repair Btn
};

constexpr int TabButtonBasic = 0;
constexpr int TabButtonPremium = 1;
constexpr int RepairAllBtn = 2;
constexpr int RepairBtn = 3;

/** @brief Get the items array for a specific vendor/tab combination. */
std::span<Item> GetVendorItems(VisualStoreVendor vendor, VisualStoreTab tab)
{
	switch (vendor) {
	case VisualStoreVendor::Smith: {
		if (tab == VisualStoreTab::Premium) {
			return { PremiumItems.data(), static_cast<size_t>(PremiumItems.size()) };
		}
		return { SmithItems.data(), static_cast<size_t>(SmithItems.size()) };
	}
	case VisualStoreVendor::Witch: {
		return { WitchItems.data(), static_cast<size_t>(WitchItems.size()) };
	}
	case VisualStoreVendor::Healer: {
		return { HealerItems.data(), static_cast<size_t>(HealerItems.size()) };
	}
	case VisualStoreVendor::Boy: {
		if (BoyItem.isEmpty()) {
			return {};
		}
		return { &BoyItem, 1 };
	}
	}
	return {};
}

/** @brief Check if the current vendor has tabs (Smith only). */
bool VendorHasTabs()
{
	return VisualStore.vendor == VisualStoreVendor::Smith;
}

/** @brief Check if the current vendor accepts items for sale. */
bool VendorAcceptsSale()
{
	switch (VisualStore.vendor) {
	case VisualStoreVendor::Smith:
	case VisualStoreVendor::Witch: {
		return true;
	}
	case VisualStoreVendor::Healer:
	case VisualStoreVendor::Boy: {
		return false;
	}
	}
	return false;
}

/** @brief Calculate the sell price for an item (1/4 of value, 18% in POE2). */
int GetSellPrice(const Item &item)
{
	int value = item._ivalue;
	if (item._iMagical != ITEM_QUALITY_NORMAL && item._iIdentified)
		value = item._iIvalue;
	return VendorSellValue(value) * StackCount(item);
}

/** @brief Rebuild the grid layout for the current vendor/tab. */
void RefreshVisualStoreLayout()
{
	if (gbIsPoe2)
		RebuildShopView();
	VisualStore.pages.clear();
	std::span<Item> items = GetVisualStoreItems();

	if (items.empty()) {
		VisualStore.pages.emplace_back();
		VisualStorePage &page = VisualStore.pages.back();
		memset(page.grid, 0, sizeof(page.grid));
		return;
	}

	auto createNewPage = [&]() -> VisualStorePage & {
		VisualStore.pages.emplace_back();
		VisualStorePage &page = VisualStore.pages.back();
		memset(page.grid, 0, sizeof(page.grid));
		return page;
	};

	VisualStorePage *currentPage = &createNewPage();

	for (uint16_t i = 0; i < static_cast<uint16_t>(items.size()); i++) {
		const Item &item = items[i];
		if (item.isEmpty())
			continue;

		const Size itemSize = GetInventorySize(item);
		bool placed = false;

		// Try to place in current page
		for (auto stashPosition : PointsInRectangle(Rectangle { { 0, 0 }, Size { VisualStoreGridWidth - (itemSize.width - 1), VisualStoreGridHeight - (itemSize.height - 1) } })) {
			bool isSpaceFree = true;
			for (auto itemPoint : PointsInRectangle(Rectangle { stashPosition, itemSize })) {
				if (currentPage->grid[itemPoint.x][itemPoint.y] != 0) {
					isSpaceFree = false;
					break;
				}
			}

			if (isSpaceFree) {
				for (auto itemPoint : PointsInRectangle(Rectangle { stashPosition, itemSize })) {
					currentPage->grid[itemPoint.x][itemPoint.y] = i + 1;
				}
				currentPage->items.push_back({ i, stashPosition + Displacement { 0, itemSize.height - 1 } });
				placed = true;
				break;
			}
		}

		if (!placed) {
			// Start new page
			currentPage = &createNewPage();
			// Try placing again in new page
			for (auto stashPosition : PointsInRectangle(Rectangle { { 0, 0 }, Size { VisualStoreGridWidth - (itemSize.width - 1), VisualStoreGridHeight - (itemSize.height - 1) } })) {
				bool isSpaceFree = true;
				for (auto itemPoint : PointsInRectangle(Rectangle { stashPosition, itemSize })) {
					if (currentPage->grid[itemPoint.x][itemPoint.y] != 0) {
						isSpaceFree = false;
						break;
					}
				}

				if (isSpaceFree) {
					for (auto itemPoint : PointsInRectangle(Rectangle { stashPosition, itemSize })) {
						currentPage->grid[itemPoint.x][itemPoint.y] = i + 1;
					}
					currentPage->items.push_back({ i, stashPosition + Displacement { 0, itemSize.height - 1 } });
					placed = true;
					break;
				}
			}
		}
	}

	if (VisualStore.currentPage >= VisualStore.pages.size())
		VisualStore.currentPage = VisualStore.pages.empty() ? 0 : static_cast<unsigned>(VisualStore.pages.size() - 1);
}

} // namespace

void InitVisualStore()
{
	if (HeadlessMode)
		return;

	VisualStorePanelArt = LoadClx("data\\store.clx");
	VisualStoreNavButtonArt = LoadClx("data\\tabBtnUp.clx");
	VisualStoreRepairAllButtonArt = LoadClx("data\\repairAllBtn.clx");
	VisualStoreRepairButtonArt = LoadClx("data\\repairSingleBtn.clx");
}

void FreeVisualStoreGFX()
{
	VisualStoreNavButtonArt = std::nullopt;
	VisualStorePanelArt = std::nullopt;
	VisualStoreRepairAllButtonArt = std::nullopt;
	VisualStoreRepairButtonArt = std::nullopt;
}

std::vector<Poe2ShopTab> Poe2ShopTabs()
{
	switch (VisualStore.vendor) {
	case VisualStoreVendor::Smith:
		return { Poe2ShopTab::Weapons, Poe2ShopTab::Armour, Poe2ShopTab::Other, Poe2ShopTab::Buyback };
	case VisualStoreVendor::Witch:
		return { Poe2ShopTab::Items, Poe2ShopTab::Buyback };
	default:
		return { Poe2ShopTab::Items };
	}
}

void SetPoe2ShopTab(Poe2ShopTab tab)
{
	ActivePoe2ShopTab = tab;
	VisualStore.currentPage = 0;
	pcursstoreitem = -1;
	pcursstorebtn = -1;
	RefreshVisualStoreLayout();
}

int VisualStoreItemPrice(int index)
{
	if (gbIsPoe2 && index >= 0 && static_cast<size_t>(index) < ShopOrigins.size() && ShopOrigins[index].source == ShopSource::Buyback) {
		const std::vector<BuybackEntry> *buyback = VendorBuyback();
		if (buyback != nullptr && ShopOrigins[index].index < buyback->size())
			return (*buyback)[ShopOrigins[index].index].price;
	}
	const std::span<Item> items = GetVisualStoreItems();
	return index >= 0 && static_cast<size_t>(index) < items.size() ? items[index]._iIvalue : 0;
}

void OpenVisualStore(VisualStoreVendor vendor)
{
	IsVisualStoreOpen = true;
	invflag = true; // Open inventory panel alongside
	RememberInteractionSpot();

	VisualStore.vendor = vendor;
	VisualStore.activeTab = VisualStoreTab::Basic;
	VisualStore.currentPage = 0;
	if (gbIsPoe2) {
		ActivePoe2ShopTab = Poe2ShopTabs().front();
		RebuildShopView();
	}

	pcursstoreitem = -1;
	pcursstorebtn = -1;

	// Refresh item stat flags for current player
	std::span<Item> items = GetVisualStoreItems();
	for (Item &item : items) {
		item._iStatFlag = MyPlayer->CanUseItem(item);
	}

	RefreshVisualStoreLayout();

	// Initialize controller focus to the visual store grid; with a mouse, leave the cursor where it is.
	if (ControlMode != ControlTypes::KeyboardAndMouse)
		FocusOnVisualStore();
}

void CloseVisualStore()
{
	CancelPendingPurchase();
	if (IsVisualStoreOpen) {
		IsVisualStoreOpen = false;
		invflag = false;
		pcursstoreitem = -1;
		pcursstorebtn = -1;
		VisualStoreButtonPressed = -1;
		VisualStore.pages.clear();
	}
}

void SetVisualStoreTab(VisualStoreTab tab)
{
	if (!VendorHasTabs())
		return;

	VisualStore.activeTab = tab;
	VisualStore.currentPage = 0;
	pcursstoreitem = -1;
	pcursstorebtn = -1;

	// Refresh item stat flags
	std::span<Item> items = GetVisualStoreItems();
	for (Item &item : items) {
		item._iStatFlag = MyPlayer->CanUseItem(item);
	}

	RefreshVisualStoreLayout();
}

void VisualStoreNextPage()
{
	if (VisualStore.currentPage + 1 < VisualStore.pages.size()) {
		VisualStore.currentPage++;
		pcursstoreitem = -1;
		pcursstorebtn = -1;
	}
}

void VisualStorePreviousPage()
{
	if (VisualStore.currentPage > 0) {
		VisualStore.currentPage--;
		pcursstoreitem = -1;
		pcursstorebtn = -1;
	}
}

int GetRepairCost(const Item &item)
{
	if (item.isEmpty() || item._iDurability == item._iMaxDur || item._iMaxDur == DUR_INDESTRUCTIBLE)
		return 0;

	const int due = item._iMaxDur - item._iDurability;
	if (item._iMagical != ITEM_QUALITY_NORMAL && item._iIdentified) {
		return 30 * item._iIvalue * due / (item._iMaxDur * 100 * 2);
	} else {
		return std::max(item._ivalue * due / (item._iMaxDur * 2), 1);
	}
}

void VisualStoreRepairAll()
{
	Player &myPlayer = *MyPlayer;
	int totalCost = 0;

	// Check body items
	for (auto &item : myPlayer.InvBody) {
		totalCost += GetRepairCost(item);
	}

	// Check inventory items
	for (int i = 0; i < myPlayer._pNumInv; i++) {
		totalCost += GetRepairCost(myPlayer.InvList[i]);
	}

	if (totalCost == 0)
		return;

	if (!PlayerCanAfford(totalCost)) {

		return;
	}

	// Execute repairs
	TakePlrsMoney(totalCost);

	for (auto &item : myPlayer.InvBody) {
		if (!item.isEmpty() && item._iMaxDur != DUR_INDESTRUCTIBLE)
			item._iDurability = item._iMaxDur;
	}

	for (int i = 0; i < myPlayer._pNumInv; i++) {
		Item &item = myPlayer.InvList[i];
		if (!item.isEmpty() && item._iMaxDur != DUR_INDESTRUCTIBLE)
			item._iDurability = item._iMaxDur;
	}

	PlaySFX(SfxID::ItemGold);
	CalcPlrInv(myPlayer, true);
}

void VisualStoreRepair()
{
	NewCursor(CURSOR_REPAIR);
}

void VisualStoreRepairItem(int invIndex)
{
	Player &myPlayer = *MyPlayer;
	Item *item = nullptr;

	if (invIndex < INVITEM_INV_FIRST) {
		item = &myPlayer.InvBody[invIndex];
	} else if (invIndex <= INVITEM_INV_LAST) {
		item = &myPlayer.InvList[invIndex - INVITEM_INV_FIRST];
	} else {
		return; // Belt items don't have durability
	}

	if (item->isEmpty())
		return;

	int cost = GetRepairCost(*item);
	if (cost <= 0)
		return;

	if (!PlayerCanAfford(cost)) {

		return;
	}

	TakePlrsMoney(cost);
	item->_iDurability = item->_iMaxDur;
	PlaySFX(SfxID::ItemGold);
	CalcPlrInv(myPlayer, true);
}

Point GetVisualStoreSlotCoord(Point slot)
{
	constexpr int SlotSpacing = INV_SLOT_SIZE_PX + 1;
	// Grid starts below the header area
	return GetPanelPosition(UiPanels::Stash, slot * SlotSpacing + Displacement { 17, 44 });
}

Rectangle GetVisualBtnCoord(int btnId)
{
	if (btnId >= Poe2TabButtonFirst)
		return Poe2TabRect(static_cast<size_t>(btnId - Poe2TabButtonFirst));
	return { GetPanelPosition(UiPanels::Stash, VisualStoreButtonRect[btnId].position), VisualStoreButtonRect[btnId].size };
}

int GetVisualStoreItemCount()
{
	std::span<Item> items = GetVisualStoreItems();
	int count = 0;
	for (const Item &item : items) {
		if (!item.isEmpty())
			count++;
	}
	return count;
}

std::span<Item> GetVisualStoreItems()
{
	if (gbIsPoe2)
		return { ShopView.data(), ShopView.size() };
	return GetVendorItems(VisualStore.vendor, VisualStore.activeTab);
}

int GetVisualStorePageCount()
{
	return std::max(1, static_cast<int>(VisualStore.pages.size()));
}

void DrawVisualStore(const Surface &out)
{
	if (!VisualStorePanelArt)
		return;

	RenderClxSprite(out, (*VisualStorePanelArt)[0], GetPanelPosition(UiPanels::Stash));

	const Point panelPos = GetPanelPosition(UiPanels::Stash);
	const UiFlags styleWhite = UiFlags::VerticalCenter | UiFlags::ColorWhite;
	const UiFlags styleTabPushed = UiFlags::VerticalCenter | UiFlags::ColorButtonpushed;
	constexpr int TextHeight = 13;

	// Draw tab buttons
	UiFlags basicStyle = VisualStore.activeTab == VisualStoreTab::Basic ? styleWhite : styleTabPushed;
	UiFlags premiumStyle = VisualStore.activeTab == VisualStoreTab::Premium ? styleWhite : styleTabPushed;
	if (gbIsPoe2) {
		// POE2 Mashup: one tab per kind of item, plus buyback.
		const std::vector<Poe2ShopTab> tabs = Poe2ShopTabs();
		for (size_t i = 0; i < tabs.size(); i++) {
			const Rectangle tabRect = Poe2TabRect(i);
			const bool active = tabs[i] == ActivePoe2ShopTab;
			RenderClxSprite(out, (*VisualStoreNavButtonArt)[active ? 0 : 1], tabRect.position);
			DrawString(out, Poe2ShopTabName(tabs[i]), tabRect, { .flags = UiFlags::AlignCenter | (active ? styleWhite : styleTabPushed) });
		}
		if (ShopView.empty()) {
			DrawString(out, ActivePoe2ShopTab == Poe2ShopTab::Buyback ? _("Items you sell here can be bought back.") : _("Nothing for sale here."),
			    { panelPos + Displacement { 20, 150 }, { 280, TextHeight } }, { .flags = UiFlags::AlignCenter | UiFlags::ColorButtonpushed });
		}
	} else
		switch (VisualStore.vendor) {
		case VisualStoreVendor::Smith: {
			const Rectangle regBtnPos = { GetPanelPosition(UiPanels::Stash, VisualStoreButtonRect[TabButtonBasic].position), VisualStoreButtonRect[TabButtonBasic].size };
			RenderClxSprite(out, (*VisualStoreNavButtonArt)[VisualStore.activeTab != VisualStoreTab::Basic], regBtnPos.position);
			DrawString(out, _("Basic"), regBtnPos, { .flags = UiFlags::AlignCenter | basicStyle });

			const Rectangle premBtnPos = { GetPanelPosition(UiPanels::Stash, VisualStoreButtonRect[TabButtonPremium].position), VisualStoreButtonRect[TabButtonPremium].size };
			RenderClxSprite(out, (*VisualStoreNavButtonArt)[VisualStore.activeTab != VisualStoreTab::Premium], premBtnPos.position);
			DrawString(out, _("Premium"), premBtnPos, { .flags = UiFlags::AlignCenter | premiumStyle });
			break;
		}
		case VisualStoreVendor::Witch:
		case VisualStoreVendor::Boy:
		case VisualStoreVendor::Healer: {
			const Rectangle miscBtnPos = { GetPanelPosition(UiPanels::Stash, VisualStoreButtonRect[TabButtonBasic].position), VisualStoreButtonRect[TabButtonBasic].size };
			RenderClxSprite(out, (*VisualStoreNavButtonArt)[VisualStoreButtonPressed == TabButtonBasic], miscBtnPos.position);
			DrawString(out, _("Misc"), miscBtnPos, { .flags = UiFlags::AlignCenter | basicStyle });
			break;
		}
		default: {
			break;
		}
		}

	if (VisualStore.currentPage >= VisualStore.pages.size())
		return;

	const VisualStorePage &page = VisualStore.pages[VisualStore.currentPage];
	std::span<Item> allItems = GetVisualStoreItems();

	constexpr Displacement offset { 0, INV_SLOT_SIZE_PX - 1 };

	// First pass: draw item slot backgrounds
	for (int y = 0; y < VisualStoreGridHeight; y++) {
		for (int x = 0; x < VisualStoreGridWidth; x++) {
			const uint16_t itemPlusOne = page.grid[x][y];
			if (itemPlusOne == 0)
				continue;

			const Item &item = allItems[itemPlusOne - 1];
			Point position = GetVisualStoreSlotCoord({ x, y }) + offset;
			InvDrawSlotBack(out, position, InventorySlotSizeInPixels, item._iMagical);
		}
	}

	// Second pass: draw item sprites
	for (const auto &vsItem : page.items) {
		const Item &item = allItems[vsItem.index];
		Point position = GetVisualStoreSlotCoord(vsItem.position) + offset;

		const int frame = item._iCurs + CURSOR_FIRSTITEM;
		const ClxSprite sprite = GetInvItemSprite(frame);

		// Draw highlight outline if this item is hovered
		if (pcursstoreitem == vsItem.index) {
			const uint8_t color = GetOutlineColor(item, true);
			ClxDrawOutline(out, color, position, sprite);
		}

		DrawItem(item, out, position, sprite);
	}

	// Draw player gold at bottom
	uint32_t totalGold = MyPlayer->_pGold + Stash.gold;
	DrawString(out, StrCat(_("Gold: "), FormatInteger(totalGold)),
	    { panelPos + Displacement { 20, 320 }, { 280, TextHeight } },
	    { .flags = styleWhite });

	// Draw Repair All
	if (VisualStore.vendor == VisualStoreVendor::Smith) {
		const Rectangle repairAllBtnPos = { GetPanelPosition(UiPanels::Stash, VisualStoreButtonRect[RepairAllBtn].position), VisualStoreButtonRect[RepairAllBtn].size };
		RenderClxSprite(out, (*VisualStoreRepairAllButtonArt)[VisualStoreButtonPressed == RepairAllBtn], repairAllBtnPos.position);

		const Rectangle repairBtnPos = { GetPanelPosition(UiPanels::Stash, VisualStoreButtonRect[RepairBtn].position), VisualStoreButtonRect[RepairBtn].size };
		RenderClxSprite(out, (*VisualStoreRepairButtonArt)[VisualStoreButtonPressed == RepairBtn], repairBtnPos.position);
	}
}

int16_t CheckVisualStoreHLight(Point mousePosition)
{
	// Check buttons first
	if (gbIsPoe2 && MyPlayer->HoldItem.isEmpty()) {
		const std::vector<Poe2ShopTab> tabs = Poe2ShopTabs();
		for (size_t i = 0; i < tabs.size(); i++) {
			if (Poe2TabRect(i).contains(mousePosition)) {
				InfoString = Poe2ShopTabName(tabs[i]);
				FloatingInfoString = Poe2ShopTabName(tabs[i]);
				InfoColor = UiFlags::ColorWhite;
				pcursstorebtn = static_cast<int16_t>(Poe2TabButtonFirst + i);
				return -1;
			}
		}
	}
	if (MyPlayer->HoldItem.isEmpty()) {
		for (int i = 0; i < 4; i++) {
			// POE2 Mashup: the tabs above replace the basic, premium and misc tabs.
			if (gbIsPoe2 && i < RepairAllBtn)
				continue;
			// Skip tab buttons if vendor doesn't have tabs
			if (!VendorHasTabs() && i != TabButtonBasic)
				continue;

			Rectangle button = VisualStoreButtonRect[i];
			button.position = GetPanelPosition(UiPanels::Stash, button.position);

			if (button.contains(mousePosition)) {
				if (i == TabButtonBasic) {
					if (VendorHasTabs()) {
						InfoString = _("Basic");
						FloatingInfoString = _("Basic");
						AddInfoBoxString(_("Basic items"));
						AddInfoBoxString(_("Basic items"), true);
					} else {
						InfoString = _("Misc");
						FloatingInfoString = _("Misc");
						AddInfoBoxString(_("Miscellaneous items"));
						AddInfoBoxString(_("Miscellaneous items"), true);
					}
					InfoColor = UiFlags::ColorWhite;
					pcursstorebtn = TabButtonBasic;
					return -1;
				} else if (i == TabButtonPremium) {
					InfoString = _("Premium");
					FloatingInfoString = _("Premium");
					AddInfoBoxString(_("Premium items"));
					AddInfoBoxString(_("Premium items"), true);
					InfoColor = UiFlags::ColorWhite;
					pcursstorebtn = TabButtonPremium;
					return -1;
				} else if (i == RepairAllBtn) {
					int totalCost = 0;
					Player &myPlayer = *MyPlayer;
					for (auto &item : myPlayer.InvBody)
						totalCost += GetRepairCost(item);
					for (int j = 0; j < myPlayer._pNumInv; j++)
						totalCost += GetRepairCost(myPlayer.InvList[j]);

					InfoString = _("Repair All");
					FloatingInfoString = _("Repair All");
					if (totalCost > 0) {
						AddInfoBoxString(StrCat(FormatInteger(totalCost), " Gold"));
						AddInfoBoxString(StrCat(FormatInteger(totalCost), " Gold"), true);
					} else {
						AddInfoBoxString(_("Nothing to repair"));
						AddInfoBoxString(_("Nothing to repair"), true);
					}
					InfoColor = UiFlags::ColorWhite;
					pcursstorebtn = RepairAllBtn;
					return -1;
				} else if (i == RepairBtn) {
					InfoString = _("Repair");
					FloatingInfoString = _("Repair");
					AddInfoBoxString(_("Repair a single item"));
					AddInfoBoxString(_("Repair a single item"), true);
					InfoColor = UiFlags::ColorWhite;
					pcursstorebtn = RepairBtn;
					return -1;
				}
			}
		}
	}

	if (VisualStore.currentPage >= VisualStore.pages.size())
		return -1;

	const VisualStorePage &page = VisualStore.pages[VisualStore.currentPage];
	std::span<Item> allItems = GetVisualStoreItems();

	for (int y = 0; y < VisualStoreGridHeight; y++) {
		for (int x = 0; x < VisualStoreGridWidth; x++) {
			const uint16_t itemPlusOne = page.grid[x][y];
			if (itemPlusOne == 0)
				continue;

			const int itemIndex = itemPlusOne - 1;
			const Item &item = allItems[itemIndex];

			const Rectangle cell {
				GetVisualStoreSlotCoord({ x, y }),
				InventorySlotSizeInPixels + 1
			};

			if (cell.contains(mousePosition)) {
				const int price = VisualStoreItemPrice(itemIndex);
				const bool canAfford = PlayerCanAfford(price);

				InfoString = item.getName();
				FloatingInfoString = item.getName();
				InfoColor = canAfford ? item.getTextColor() : UiFlags::ColorRed;

				if (item._iIdentified) {
					PrintItemDetails(item);
				} else {
					PrintItemDur(item);
				}

				if (gbIsPoe2) {
					PrintItemComparison(item);
					CurrentInfoLineStyle = InfoLineStyle::Note;
					AddInfoBoxString(StrCat(FormatInteger(price), " Gold"), true);
					CurrentInfoLineStyle = InfoLineStyle::Requirement;
					AddInfoBoxString(_("Click to pick up, then place it in your inventory to buy; Ctrl+click buys at once"), true);
					CurrentInfoLineStyle = InfoLineStyle::Default;
				} else {
					AddInfoBoxString(StrCat(FormatInteger(price), " Gold"));
				}

				return static_cast<int16_t>(itemIndex);
			}
		}
	}

	return -1;
}

namespace {

/** @brief A ware picked up at a POE2 Mashup vendor, paid for once it's put in the inventory. */
struct PendingWare {
	int16_t itemIndex;
	uint32_t seed;
	int price;
};
std::optional<PendingWare> PendingPurchase;

/** @brief Takes a bought ware off the vendor's lists. */
void RemoveBoughtWare(int16_t itemIndex)
{
	if (gbIsPoe2) {
		// POE2 Mashup: take the item from the list it came from.
		const ShopOrigin origin = ShopOrigins[itemIndex];
		switch (origin.source) {
		case ShopSource::Premium:
			PremiumItems[origin.index].clear();
			SpawnPremium(*MyPlayer);
			break;
		case ShopSource::Smith:
			SmithItems.erase(SmithItems.begin() + static_cast<ptrdiff_t>(origin.index));
			break;
		case ShopSource::Witch:
			// The first 3 items are always in stock.
			if (origin.index >= 3)
				WitchItems.erase(WitchItems.begin() + static_cast<ptrdiff_t>(origin.index));
			break;
		case ShopSource::Healer:
			if (origin.index >= (gbIsMultiplayer ? 3U : 2U))
				HealerItems.erase(HealerItems.begin() + static_cast<ptrdiff_t>(origin.index));
			break;
		case ShopSource::Boy:
			BoyItem.clear();
			break;
		case ShopSource::Buyback:
			if (std::vector<BuybackEntry> *buyback = VendorBuyback(); buyback != nullptr && origin.index < buyback->size())
				buyback->erase(buyback->begin() + static_cast<ptrdiff_t>(origin.index));
			break;
		}
		pcursstoreitem = -1;
		RefreshVisualStoreLayout();
		return;
	}

	// Remove item from store (vendor-specific handling)
	switch (VisualStore.vendor) {
	case VisualStoreVendor::Smith: {
		if (VisualStore.activeTab == VisualStoreTab::Premium) {
			// Premium items get replaced
			PremiumItems[itemIndex].clear();
			SpawnPremium(*MyPlayer);
		} else {
			// Basic items are removed
			SmithItems.erase(SmithItems.begin() + itemIndex);
		}
		break;
	}
	case VisualStoreVendor::Witch: {
		// First 3 items are pinned, don't remove them
		if (itemIndex >= 3) {
			WitchItems.erase(WitchItems.begin() + itemIndex);
		}
		break;
	}
	case VisualStoreVendor::Healer: {
		// First 2-3 items are pinned
		if (itemIndex >= (gbIsMultiplayer ? 3 : 2)) {
			HealerItems.erase(HealerItems.begin() + itemIndex);
		}
		break;
	}
	case VisualStoreVendor::Boy: {
		BoyItem.clear();
		break;
	}
	}

	pcursstoreitem = -1;
	RefreshVisualStoreLayout();
}

} // namespace

std::optional<int> VisualStoreSellPrice(const Item &item)
{
	if (!IsVisualStoreOpen || !VendorAcceptsSale() || !CanSellToCurrentVendor(item))
		return std::nullopt;
	return GetSellPrice(item);
}

bool HasPendingPurchase()
{
	return PendingPurchase.has_value();
}

void CancelPendingPurchase()
{
	if (!PendingPurchase)
		return;
	Player &player = *MyPlayer;
	if (!player.HoldItem.isEmpty() && player.HoldItem._iSeed == PendingPurchase->seed) {
		player.HoldItem.clear();
		NewCursor(CURSOR_HAND);
	}
	PendingPurchase = std::nullopt;
}

void SettlePendingPurchase()
{
	if (!PendingPurchase)
		return;
	const Player &player = *MyPlayer;
	if (!player.HoldItem.isEmpty() && player.HoldItem._iSeed == PendingPurchase->seed)
		return; // still on the cursor
	// The ware left the cursor for the inventory, belt or an equipment slot: pay for it.
	const PendingWare ware = *PendingPurchase;
	PendingPurchase = std::nullopt;
	const std::span<Item> wares = GetVisualStoreItems();
	if (ware.itemIndex >= static_cast<int16_t>(wares.size()) || wares[ware.itemIndex]._iSeed != ware.seed)
		return;
	const uint32_t totalGold = player._pGold + Stash.gold;
	TakePlrsMoney(std::min<int>(ware.price, static_cast<int>(totalGold)));
	PlaySFX(SfxID::ItemGold);
	RemoveBoughtWare(ware.itemIndex);
}

void CheckVisualStoreItem(Point mousePosition, bool isCtrlHeld, bool isShiftHeld)
{
	// Check if clicking on an item to buy
	int16_t itemIndex = CheckVisualStoreHLight(mousePosition);
	if (itemIndex < 0)
		return;

	std::span<Item> items = GetVisualStoreItems();
	if (itemIndex >= static_cast<int16_t>(items.size()))
		return;

	Item &item = items[itemIndex];
	if (item.isEmpty())
		return;

	// Check if player can afford the item
	int price = VisualStoreItemPrice(itemIndex);
	uint32_t totalGold = MyPlayer->_pGold + Stash.gold;
	if (totalGold < static_cast<uint32_t>(price)) {
		// InitDiabloMsg(EMSG_NOT_ENOUGH_GOLD);
		return;
	}

	if (gbIsPoe2 && !isCtrlHeld) {
		// POE2 Mashup: a click picks the ware up and it's bought once it's put in the inventory; Ctrl+click buys it at once.
		Player &player = *MyPlayer;
		player.HoldItem = item;
		player.HoldItem._iStatFlag = player.CanUseItem(player.HoldItem);
		NewCursor(player.HoldItem);
		PendingPurchase = PendingWare { itemIndex, item._iSeed, price };
		PlaySFX(SfxID::GrabItem);
		return;
	}

	// Check if player has room for the item
	if (!StoreAutoPlace(item, false)) {
		// InitDiabloMsg(EMSG_INVENTORY_FULL);
		return;
	}

	// Execute the purchase
	TakePlrsMoney(price);
	StoreAutoPlace(item, true);
	PlaySFX(ItemInvSnds[ItemCAnimTbl[item._iCurs]]);

	RemoveBoughtWare(itemIndex);
}

void CheckVisualStorePaste(Point mousePosition)
{
	if (HasPendingPurchase()) {
		// Putting a picked-up ware back on the shelf.
		CancelPendingPurchase();
		return;
	}
	if (!VendorAcceptsSale())
		return;

	Player &player = *MyPlayer;
	if (player.HoldItem.isEmpty())
		return;

	// Check if the item can be sold to this vendor
	if (!CanSellToCurrentVendor(player.HoldItem)) {
		player.SaySpecific(HeroSpeech::ICantDoThat);
		return;
	}

	// Calculate sell price
	int sellPrice = GetSellPrice(player.HoldItem);

	// Add gold to player
	AddGoldToInventory(player, sellPrice);
	PlaySFX(SfxID::ItemGold);
	AddToBuyback(player.HoldItem, sellPrice);

	// Clear the held item
	player.HoldItem.clear();
	NewCursor(CURSOR_HAND);
	if (gbIsPoe2)
		RefreshVisualStoreLayout();
}

bool CanSellToCurrentVendor(const Item &item)
{
	if (item.isEmpty())
		return false;

	switch (VisualStore.vendor) {
	case VisualStoreVendor::Smith: {
		return SmithWillBuy(item);
	}
	case VisualStoreVendor::Witch: {
		return WitchWillBuy(item);
	}
	case VisualStoreVendor::Healer:
	case VisualStoreVendor::Boy: {
		return false;
	}
	}
	return false;
}

void SellItemToVisualStore(int invIndex)
{
	if (!VendorAcceptsSale())
		return;

	Player &player = *MyPlayer;
	Item &item = player.InvList[invIndex];

	if (!CanSellToCurrentVendor(item)) {
		player.SaySpecific(HeroSpeech::ICantDoThat);
		return;
	}

	// Calculate sell price
	int sellPrice = GetSellPrice(item);

	// Add gold to player
	AddGoldToInventory(player, sellPrice);
	PlaySFX(SfxID::ItemGold);
	AddToBuyback(item, sellPrice);

	// Remove item from inventory
	player.RemoveInvItem(invIndex);
	if (gbIsPoe2)
		RefreshVisualStoreLayout();
}

void CheckVisualStoreButtonPress(Point mousePosition)
{
	if (!MyPlayer->HoldItem.isEmpty())
		return;

	if (gbIsPoe2) {
		const size_t tabCount = Poe2ShopTabs().size();
		for (size_t i = 0; i < tabCount; i++) {
			if (Poe2TabRect(i).contains(mousePosition)) {
				VisualStoreButtonPressed = static_cast<int>(Poe2TabButtonFirst + i);
				return;
			}
		}
	}

	for (int i = 0; i < 4; i++) {
		if (gbIsPoe2 && i < RepairAllBtn)
			continue;
		// Skip tab buttons if vendor doesn't have tabs
		if (!VendorHasTabs() && i != TabButtonBasic)
			continue;

		Rectangle button = VisualStoreButtonRect[i];
		button.position = GetPanelPosition(UiPanels::Stash, button.position);

		if (button.contains(mousePosition)) {
			VisualStoreButtonPressed = i;
			return;
		}
	}

	VisualStoreButtonPressed = -1;
}

void CheckVisualStoreButtonRelease(Point mousePosition)
{
	if (VisualStoreButtonPressed == -1)
		return;

	if (VisualStoreButtonPressed >= Poe2TabButtonFirst) {
		const std::vector<Poe2ShopTab> tabs = Poe2ShopTabs();
		const size_t tab = static_cast<size_t>(VisualStoreButtonPressed - Poe2TabButtonFirst);
		if (tab < tabs.size() && Poe2TabRect(tab).contains(mousePosition))
			SetPoe2ShopTab(tabs[tab]);
		VisualStoreButtonPressed = -1;
		return;
	}

	Rectangle button = VisualStoreButtonRect[VisualStoreButtonPressed];
	button.position = GetPanelPosition(UiPanels::Stash, button.position);

	if (button.contains(mousePosition)) {
		switch (VisualStoreButtonPressed) {
		case TabButtonBasic: {
			SetVisualStoreTab(VisualStoreTab::Basic);
			break;
		}
		case TabButtonPremium: {
			SetVisualStoreTab(VisualStoreTab::Premium);
			break;
		}
		case RepairAllBtn: {
			VisualStoreRepairAll();
			break;
		}
		case RepairBtn: {
			VisualStoreRepair();
			break;
		}
		}
	}

	VisualStoreButtonPressed = -1;
}

} // namespace devilution
