/**
 * @file qol/visual_store.h
 *
 * Interface of visual grid-based store UI.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#else
#include <SDL.h>
#endif

#include "engine/point.hpp"
#include "engine/surface.hpp"
#include "items.h"
#include "utils/attributes.h"

namespace devilution {

enum class VisualStoreVendor : uint8_t {
	Smith,
	Witch,
	Healer,
	Boy
};

enum class VisualStoreTab : uint8_t {
	Basic = 0,
	Premium = 1
};

// Grid: 10x9 = 90 slots per page
inline constexpr int VisualStoreGridWidth = 10;
inline constexpr int VisualStoreGridHeight = 9;

struct VisualStoreItem {
	uint16_t index; // Index in the vendor's item list
	Point position; // Top-left position in the grid
};

struct VisualStorePage {
	std::vector<VisualStoreItem> items;
	uint16_t grid[VisualStoreGridWidth][VisualStoreGridHeight];
};

struct VisualStoreState {
	VisualStoreVendor vendor;
	VisualStoreTab activeTab; // For Smith: Regular vs Premium
	unsigned currentPage;
	std::vector<VisualStorePage> pages;
};

/** @brief Whether stores use the grid UI: always in POE2 mode, otherwise when the option picks it. */
bool UseVisualStoreUi();

/** @brief POE2 Mashup: the vendor's tabs, like POE2's vendor screen: one per kind of item, and buyback. */
enum class Poe2ShopTab : uint8_t {
	Weapons,
	OneHanded,
	TwoHanded,
	Bows,
	BodyArmour,
	Helmets,
	Shields,
	Jewelry,
	Staves,
	Supplies,
	Items,
	Buyback,
};

/** @brief POE2 Mashup: an item sold to a vendor, which can be bought back for what it sold for. */
struct BuybackEntry {
	Item item;
	int price;
};
constexpr size_t MaxBuybackItems = 30;
extern DVL_API_FOR_TEST std::vector<BuybackEntry> SmithBuyback;
extern DVL_API_FOR_TEST std::vector<BuybackEntry> WitchBuyback;
extern DVL_API_FOR_TEST Poe2ShopTab ActivePoe2ShopTab;
/** @brief The tabs the current vendor shows in POE2 mode. */
std::vector<Poe2ShopTab> Poe2ShopTabs();
void SetPoe2ShopTab(Poe2ShopTab tab);
/** @brief Whether a ware is listed on a POE2 shop tab. */
bool ItemOnPoe2ShopTab(const Item &item, Poe2ShopTab tab);
/** @brief The button id of the POE2 shop tab at an index, for GetVisualBtnCoord. */
int Poe2ShopTabButton(size_t index);
/** @brief The price of a store item as shown: the buyback price on the buyback tab. */
int VisualStoreItemPrice(int index);

extern DVL_API_FOR_TEST bool IsVisualStoreOpen;
extern DVL_API_FOR_TEST VisualStoreState VisualStore;
extern DVL_API_FOR_TEST int16_t pcursstoreitem; // Currently highlighted store item index (-1 if none)
extern DVL_API_FOR_TEST int16_t pcursstorebtn;

/**
 * @brief Load visual store graphics.
 */
void InitVisualStore();

/**
 * @brief Free visual store graphics.
 */
void FreeVisualStoreGFX();

/**
 * @brief Open the visual store for a vendor.
 * Opens both the store panel (left) and inventory panel (right).
 * @param vendor The vendor to open the store for.
 */
void OpenVisualStore(VisualStoreVendor vendor);

/**
 * @brief Close the visual store and inventory panels.
 */
void CloseVisualStore();

/**
 * @brief Set the active tab for Smith (Regular/Premium).
 * @param tab The tab to switch to.
 */
void SetVisualStoreTab(VisualStoreTab tab);

/**
 * @brief Navigate to the next page of store items.
 */
void VisualStoreNextPage();

/**
 * @brief Navigate to the previous page of store items.
 */
void VisualStorePreviousPage();

/**
 * @brief Render the visual store panel to the given buffer.
 */
void DrawVisualStore(const Surface &out);

/**
 * @brief POE2 Mashup: the shop's search box takes clicks on it and, while typing, the keyboard.
 * @return true when the event was used
 */
bool ShopSearchHandleEvent(const SDL_Event &event);

/**
 * @brief Handle a click on the visual store panel.
 * @param mousePosition The mouse position.
 */
void CheckVisualStoreItem(Point mousePosition, bool isCtrlHeld, bool isShiftHeld);

/**
 * @brief Handle dropping an item on the visual store to sell.
 * @param mousePosition The mouse position.
 */
void CheckVisualStorePaste(Point mousePosition);

/** @brief Whether a ware picked up at a POE2 Mashup vendor is on the cursor, not paid for yet. */
bool HasPendingPurchase();
/** @brief Puts a picked-up ware back on the vendor's shelf. */
void CancelPendingPurchase();
/** @brief Pays for a picked-up ware once it has left the cursor for the inventory. */
void SettlePendingPurchase();

/**
 * @brief Check for item highlight under the cursor.
 * @param mousePosition The mouse position.
 * @return The index of the highlighted item, or -1 if none.
 */
int16_t CheckVisualStoreHLight(Point mousePosition);

/**
 * @brief Handle button press in the visual store.
 * @param mousePosition The mouse position.
 */
void CheckVisualStoreButtonPress(Point mousePosition);

/**
 * @brief Handle button release in the visual store.
 * @param mousePosition The mouse position.
 */
void CheckVisualStoreButtonRelease(Point mousePosition);

/**
 * @brief Check if an item can be sold to the current vendor.
 * @param item The item to check.
 * @return true if the item can be sold.
 */
bool CanSellToCurrentVendor(const Item &item);

/** @brief Tells the player the open shop's vendor won't buy the item. */
void SayVendorWontBuy();

/**
 * @brief Sell an item from the player's inventory to the current vendor.
 * @param invIndex The inventory index of the item.
 */
void SellItemToVisualStore(int invIndex);

/** @brief What the open vendor pays for the item, if they'd buy it. */
std::optional<int> VisualStoreSellPrice(const Item &item);

/**
 * @brief Get the number of items for the current vendor/tab.
 * @return The item count.
 */
int GetVisualStoreItemCount();

/**
 * @brief Get the items array for the current vendor/tab.
 * @return A span of items.
 */
std::span<Item> GetVisualStoreItems();

/**
 * @brief Get the total number of pages for the current vendor/tab.
 * @return The page count.
 */
int GetVisualStorePageCount();

/**
 * @brief Convert a grid slot position to screen coordinates.
 * @param slot The grid slot position.
 * @return The screen coordinates.
 */
Point GetVisualStoreSlotCoord(Point slot);

/**
 * @brief Gets the point for a btn on the panel.
 * @param slot Btn id.
 * @return The screen coordinates.
 */
Rectangle GetVisualBtnCoord(int btnId);

/**
 * @brief Calculate the cost to repair an item.
 * @param item The item to repair.
 * @return The cost in gold.
 */
int GetRepairCost(const Item &item);

/**
 * @brief Repair a specific item from the player's inventory/body.
 * @param invIndex The inventory index of the item.
 */
void VisualStoreRepairItem(int invIndex);

} // namespace devilution
