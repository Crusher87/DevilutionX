/**
 * @file reforged_stash_shops_test.cpp
 *
 * Sanctuary Reforged's stash and shops: POE2 Mashup's look and comfort on Diablo's and Hellfire's own rules.
 *
 * The stash has POE2 Mashup's named and coloured tabs, all blank, each taking any item, kept in Reforged's own stash
 * file; a stash saved as plain pages keeps every page as a tab. The shops have POE2 Mashup's tabs, buyback and
 * pick-up-to-buy, with the vendors' own stock and prices.
 */
#include <algorithm>
#include <utility>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ui_test.hpp"

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "loadsave.h"
#include "mods/mod_identity.h"
#include "pfile.h"
#include "player.h"
#include "poe2/stash_tabs.hpp"
#include "qol/stash.h"
#include "qol/visual_store.h"
#include "stores.h"
#include "utils/file_util.h"
#include "utils/paths.h"
#include "utils/str_cat.hpp"

namespace devilution {
namespace {

std::string ReadWholeFile(const std::string &path)
{
	std::string contents;
	FILE *file = std::fopen(path.c_str(), "rb");
	if (file == nullptr)
		return contents;
	char buffer[4096];
	size_t read;
	while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
		contents.append(buffer, read);
	std::fclose(file);
	return contents;
}

/** @brief Makes the mod with this manifest the active one, so saves use its file extension. */
void ActivateModManifest(const std::string &manifestPath)
{
	ClearModIdentifiers();
	const std::string text = ReadWholeFile(paths::BasePath() + manifestPath);
	ASSERT_FALSE(text.empty()) << manifestPath << " isn't beside the tests";
	ModIdentifier mod;
	mod.manifest = ParseModManifest(text);
	mod.name = mod.manifest.name;
	mod.whitelisted = true;
	ActiveModIdentifiers.push_back(std::move(mod));
}

// ---------------------------------------------------------------------------
// The stash
// ---------------------------------------------------------------------------

class ReforgedStashTest : public UITest {
protected:
	void SetUp() override
	{
		UITest::SetUp();
		if (IsSkipped())
			return;
		gbIsPoe2 = false;
		gbIsReforged = true;
		Stash = {};
		StashTabs.reset();
		Stash.SetPage(0);
		Stash.dirty = false;
	}

	void TearDown() override
	{
		gbIsReforged = false;
		gbIsPoe2 = false;
		ClearModIdentifiers();
		StashTabs.reset();
		Stash = {};
		UITest::TearDown();
	}

	static Item MakePotion()
	{
		Item item {};
		InitializeItem(item, IDI_HEAL);
		return item;
	}

	/** @brief A Diablo weapon that loads back in a Diablo game. */
	static Item MakeClub()
	{
		Item item {};
		InitializeItem(item, IDI_WARRCLUB);
		item._iIdentified = true;
		return item;
	}

	static Item MakeUnique()
	{
		Item item = MakeClub();
		item._iMagical = ITEM_QUALITY_UNIQUE;
		item._iUid = 2;
		return item;
	}

	static Item MakeGold(int value)
	{
		Item item {};
		item._itype = ItemType::Gold;
		item._ivalue = value;
		return item;
	}

	/** @brief Every item on the page as its index in AllItemsList, sorted. */
	static std::vector<int> ItemsOnPage(unsigned page)
	{
		std::vector<int> ids;
		for (const uint16_t index : StashTabItems(page))
			ids.push_back(Stash.stashList[index].IDidx);
		std::sort(ids.begin(), ids.end());
		return ids;
	}
};

TEST_F(ReforgedStashTest, ANewStashHasBlankTabsOnly)
{
	ASSERT_EQ(StashTabs.count(), DefaultReforgedStashTabs);
	for (unsigned page = 0; page < StashTabs.count(); page++) {
		EXPECT_EQ(StashTabs.kind(page), StashTabKind::Blank) << page;
		EXPECT_EQ(StashTabs.displayName(page), StrCat(page + 1));
	}
	for (const StashTabKind kind : PremadeStashTabKinds) {
		EXPECT_FALSE(StashTabs.canHaveAnother(kind)) << StashTabKindName(kind);
		EXPECT_FALSE(StashTabs.addTab(kind)) << StashTabKindName(kind);
		EXPECT_FALSE(StashTabs.setKind(0, kind)) << StashTabKindName(kind);
	}
	StashTabs.addMissingPremadeTabs(0);
	EXPECT_EQ(StashTabs.count(), DefaultReforgedStashTabs) << "No premade tab is ever added";
	EXPECT_EQ(StashTabs.countOf(StashTabKind::Blank), DefaultReforgedStashTabs);
}

TEST_F(ReforgedStashTest, TabsCanBeAddedNamedAndColouredUpToAHundred)
{
	ASSERT_TRUE(StashTabs.addTab());
	EXPECT_EQ(StashTabs.count(), DefaultReforgedStashTabs + 1);
	StashTabs.rename(1, "Rings and amulets for later");
	StashTabs.setColor(1, 9);
	EXPECT_EQ(StashTabs.displayName(1), "Rings and amulet") << "Names are cut at 16 letters";
	EXPECT_EQ(StashTabs.tabs[1].color, 9);
	while (StashTabs.addTab()) { }
	EXPECT_EQ(StashTabs.count(), MaxStashTabs) << "As many tabs as Diablo had stash pages";
	Stash.SetPage(MaxStashTabs - 1);
	EXPECT_EQ(Stash.GetPage(), MaxStashTabs - 1);
}

TEST_F(ReforgedStashTest, EveryTabTakesAnyItem)
{
	// POE2's tab affinities don't exist here: ticking one does nothing and the tab still takes anything.
	StashTabs.setAffinity(2, StashAffinity::Uniques, true);
	StashTabs.setAffinity(2, StashAffinity::Flasks, true);
	EXPECT_EQ(StashTabs.affinityText(2), "");
	for (const Item &item : { MakePotion(), MakeClub(), MakeUnique() }) {
		for (unsigned page = 0; page < StashTabs.count(); page++) {
			EXPECT_TRUE(StashTabs.accepts(page, item)) << page;
			EXPECT_TRUE(StashTabRefusal(page, item).empty()) << page;
		}
		EXPECT_FALSE(StashTabs.affinityTabFor(item)) << "Nothing is sent to a tab of its own";
	}

	Stash.SetPage(2);
	ASSERT_TRUE(AutoPlaceItemInStash(MakeUnique(), true));
	ASSERT_TRUE(AutoPlaceItemInStash(MakePotion(), true));
	EXPECT_EQ(StashTabItems(2).size(), 2u) << "A unique stays on the open tab: there is no Uniques tab";
}

TEST_F(ReforgedStashTest, CtrlClickFillsTheOpenTabThenTheNextOnes)
{
	Stash.SetPage(1);
	for (int i = 0; i < 100; i++)
		ASSERT_TRUE(AutoPlaceItemInStash(MakePotion(), true));
	ASSERT_TRUE(AutoPlaceItemInStash(MakePotion(), true));
	EXPECT_EQ(StashTabItems(1).size(), 100u);
	EXPECT_EQ(StashTabItems(2).size(), 1u) << "As Diablo's pages: the open one, then the ones after it";
}

TEST_F(ReforgedStashTest, GoldIsKeptInTheStash)
{
	ASSERT_TRUE(AutoPlaceItemInStash(MakeGold(1234), true));
	EXPECT_EQ(Stash.gold, 1234);
}

TEST_F(ReforgedStashTest, AnEmptyTabCanBeRemoved)
{
	Stash.SetPage(3);
	ASSERT_TRUE(AutoPlaceItemInStash(MakeClub(), true));
	EXPECT_FALSE(RemoveStashTab(3)) << "Not while it holds something";
	EXPECT_TRUE(RemoveStashTab(1));
	EXPECT_EQ(StashTabs.count(), DefaultReforgedStashTabs - 1);
	EXPECT_EQ(Stash.GetPage(), 2u) << "The open tab moved up with its items";
	EXPECT_EQ(StashTabItems(2).size(), 1u);
	while (StashTabs.count() > 1 && RemoveStashTab(0)) { }
	EXPECT_EQ(StashTabs.count(), 1u);
	EXPECT_FALSE(RemoveStashTab(0)) << "The last tab stays";
}

class ReforgedStashSaveTest : public ReforgedStashTest {
protected:
	void SetUp() override
	{
		ReforgedStashTest::SetUp();
		if (IsSkipped())
			return;
		saveDir_ = paths::BasePath() + "reforged_stash_test_" + ::testing::UnitTest::GetInstance()->current_test_info()->name() + DIRECTORY_SEPARATOR_STR;
		CreateDir(saveDir_.c_str());
		for (const std::string &file : ListFiles(saveDir_.c_str()))
			RemoveFile((saveDir_ + file).c_str());
		paths::SetPrefPath(saveDir_);
		ActivateModManifest("mods/reforged/manifest.ini");
	}

	void Save()
	{
		Stash.dirty = true;
		sfile_write_stash();
	}

	void Reload()
	{
		Stash = {};
		StashTabs.reset();
		LoadStash();
	}

	std::string saveDir_;
};

TEST_F(ReforgedStashSaveTest, TabsAndItemsSurviveInReforgedsOwnStashFile)
{
	ASSERT_TRUE(StashTabs.addTab());
	StashTabs.rename(4, "Uniques");
	StashTabs.setColor(4, 13);
	StashTabs.rename(0, "Potions");
	Stash.SetPage(4);
	ASSERT_TRUE(AutoPlaceItemInStash(MakeUnique(), true));
	ASSERT_TRUE(AutoPlaceItemInStash(MakeClub(), true));
	Stash.SetPage(0);
	ASSERT_TRUE(AutoPlaceItemInStash(MakePotion(), true));
	ASSERT_TRUE(AutoPlaceItemInStash(MakeGold(500), true));
	Stash.SetPage(4);
	Save();

	const std::string path = pfile_stash_save_path();
	EXPECT_TRUE(path.ends_with(".srv") || path.ends_with("_srv" DIRECTORY_SEPARATOR_STR)) << path;
	EXPECT_TRUE(FileExists(path.c_str()));
	for (const std::string &file : ListFiles(saveDir_.c_str()))
		EXPECT_EQ(file.find("psv"), std::string::npos) << "POE2 Mashup's stash is never written: " << file;

	Reload();
	ASSERT_EQ(StashTabs.count(), DefaultReforgedStashTabs + 1);
	for (unsigned page = 0; page < StashTabs.count(); page++)
		EXPECT_EQ(StashTabs.kind(page), StashTabKind::Blank);
	EXPECT_EQ(StashTabs.displayName(0), "Potions");
	EXPECT_EQ(StashTabs.displayName(4), "Uniques");
	EXPECT_EQ(StashTabs.tabs[4].color, 13);
	EXPECT_EQ(Stash.GetPage(), 4u) << "The open tab stays open";
	EXPECT_EQ(ItemsOnPage(4), (std::vector<int> { IDI_WARRCLUB, IDI_WARRCLUB }));
	EXPECT_EQ(ItemsOnPage(0), (std::vector<int> { IDI_HEAL }));
	EXPECT_EQ(Stash.gold, 500);
}

TEST_F(ReforgedStashSaveTest, AStashOfPlainPagesKeepsEachPageAsATab)
{
	// As the Reforged build before tabs saved it: plain numbered pages, no tabs entry, items far down the pages.
	gbIsReforged = false;
	Stash = {};
	Stash.SetPage(0);
	ASSERT_TRUE(AutoPlaceItemInStash(MakeClub(), true));
	Stash.SetPage(7);
	ASSERT_TRUE(AutoPlaceItemInStash(MakePotion(), true));
	ASSERT_TRUE(AutoPlaceItemInStash(MakeUnique(), true));
	Stash.SetPage(41);
	ASSERT_TRUE(AutoPlaceItemInStash(MakePotion(), true));
	ASSERT_TRUE(AutoPlaceItemInStash(MakeGold(777), true));
	Stash.SetPage(7);
	Save();
	gbIsReforged = true;

	Reload();
	ASSERT_EQ(StashTabs.count(), 42u) << "A tab for every page up to the last one used";
	for (unsigned page = 0; page < StashTabs.count(); page++) {
		EXPECT_EQ(StashTabs.kind(page), StashTabKind::Blank);
		EXPECT_EQ(StashTabs.displayName(page), StrCat(page + 1)) << "Tab 8 is what was page 8";
	}
	EXPECT_EQ(ItemsOnPage(0), (std::vector<int> { IDI_WARRCLUB }));
	EXPECT_EQ(ItemsOnPage(7), (std::vector<int> { IDI_WARRCLUB, IDI_HEAL }));
	EXPECT_EQ(ItemsOnPage(41), (std::vector<int> { IDI_HEAL }));
	EXPECT_EQ(Stash.stashList.size(), 4u) << "No item lost";
	EXPECT_EQ(Stash.gold, 777);
	EXPECT_EQ(Stash.GetPage(), 7u);

	// Saved again, it is a tabbed stash, and the same.
	StashTabs.rename(41, "Far away");
	Save();
	Reload();
	EXPECT_EQ(StashTabs.count(), 42u);
	EXPECT_EQ(StashTabs.displayName(41), "Far away");
	EXPECT_EQ(Stash.stashList.size(), 4u);
}

TEST_F(ReforgedStashSaveTest, POE2TabKindsInTheFileBecomeBlankTabs)
{
	// Only POE2 Mashup writes premade tabs, never in Reforged's file; should one be there, it is a blank tab here.
	gbIsReforged = false;
	gbIsPoe2 = true;
	StashTabs.reset();
	Stash.SetPage(0);
	ASSERT_TRUE(AutoPlaceItemInStash(MakeClub(), true));
	Save();
	gbIsPoe2 = false;
	gbIsReforged = true;

	Reload();
	ASSERT_EQ(StashTabs.count(), DefaultStashTabs);
	for (unsigned page = 0; page < StashTabs.count(); page++) {
		EXPECT_EQ(StashTabs.kind(page), StashTabKind::Blank) << page;
		EXPECT_EQ(StashTabs.tabs[page].affinities, 0) << page;
	}
	EXPECT_EQ(Stash.stashList.size(), 1u);
}

// ---------------------------------------------------------------------------
// The shops
// ---------------------------------------------------------------------------

class ReforgedShopTest : public UITest {
protected:
	void SetUp() override
	{
		UITest::SetUp();
		if (IsSkipped())
			return;
		gbIsPoe2 = false;
		gbIsReforged = false;
		InitStores();
		// Room in the bag for what is bought and the gold of what is sold.
		StripPlayer();
		SetPlayerGold(20000);
	}

	void TearDown() override
	{
		if (IsVisualStoreOpen)
			CloseVisualStore();
		gbIsReforged = false;
		gbIsPoe2 = false;
		if (hellfireLoaded_) {
			gbIsHellfire = false;
			UnloadModArchives();
			LoadItemData();
		}
		InitStores();
		UITest::TearDown();
	}

	void UseHellfire()
	{
		gbIsHellfire = true;
		LoadModArchives({ { "hf", "reforged" } });
		LoadItemData();
		hellfireLoaded_ = true;
	}

	/** @brief Everything the town's vendors have for sale, as the game stocks them on entering town. */
	static std::vector<std::string> StockTown(uint32_t seed)
	{
		InitStores();
		SetRndSeed(seed);
		SetupTownStores();
		std::vector<std::string> stock;
		const auto add = [&stock](std::string_view list, const Item &item) {
			if (!item.isEmpty())
				stock.push_back(StrCat(list, " ", item._iSeed, " ", item.IDidx, " ", item._iIName, " ", item._iIvalue, " ", item._ivalue, " ", static_cast<int>(item._iMagical)));
		};
		for (const Item &item : SmithItems)
			add("smith", item);
		for (const Item &item : PremiumItems)
			add("premium", item);
		for (const Item &item : WitchItems)
			add("witch", item);
		for (const Item &item : HealerItems)
			add("healer", item);
		add("wirt", BoyItem);
		stock.push_back(StrCat("rng ", GetLCGEngineState()));
		return stock;
	}

	using Wares = std::vector<std::pair<uint32_t, int>>;

	/** @brief Every ware on the open shop's tabs, by seed, with the price the shop asks, sorted. */
	static Wares WaresOnTabs()
	{
		Wares wares;
		for (const Poe2ShopTab tab : Poe2ShopTabs()) {
			if (tab == Poe2ShopTab::Buyback)
				continue;
			SetPoe2ShopTab(tab);
			const std::span<Item> items = GetVisualStoreItems();
			size_t onPages = 0;
			for (const VisualStorePage &page : VisualStore.pages)
				onPages += page.items.size();
			EXPECT_EQ(onPages, items.size()) << "Every ware is on one of the tab's pages";
			for (size_t i = 0; i < items.size(); i++) {
				EXPECT_TRUE(ItemOnPoe2ShopTab(items[i], tab));
				wares.emplace_back(items[i]._iSeed, VisualStoreItemPrice(static_cast<int>(i)));
			}
		}
		std::sort(wares.begin(), wares.end());
		return wares;
	}

	/** @brief The wares as the vendor's talk menu sells them: each item once, at its own price. */
	template <typename List>
	static Wares Priced(const List &items)
	{
		Wares wares;
		for (const Item &item : items) {
			if (!item.isEmpty())
				wares.emplace_back(item._iSeed, item._iIvalue);
		}
		std::sort(wares.begin(), wares.end());
		return wares;
	}

	/** @brief A Diablo base of the kind, as the shop's tests stock it. */
	static Item MakeBase(ItemType type)
	{
		Item item {};
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].itype == type && AllItemsList[i].iLoc != ILOC_UNEQUIPABLE) {
				InitializeItem(item, static_cast<_item_indexes>(i));
				break;
			}
		}
		item._iIdentified = true;
		return item;
	}

	/** @brief Where to click on a ware laid out on the open page. */
	static Point WareCell(size_t ware)
	{
		return GetVisualStoreSlotCoord(VisualStore.pages[VisualStore.currentPage].items[ware].position) + Displacement { 2, 2 };
	}

	static Item MakeSword()
	{
		Item item {};
		InitializeItem(item, IDI_BARDSWORD);
		item._iIdentified = true;
		return item;
	}

	static Item MakeArmour()
	{
		return MakeBase(ItemType::LightArmor);
	}

	static int Gold()
	{
		return MyPlayer->_pGold + Stash.gold;
	}

	bool hellfireLoaded_ = false;
};

TEST_F(ReforgedShopTest, StockIsDiablosOwn)
{
	for (const uint32_t seed : { 3U, 42U, 1234U }) {
		gbIsReforged = false;
		const std::vector<std::string> diablo = StockTown(seed);
		gbIsReforged = true;
		EXPECT_EQ(StockTown(seed), diablo) << seed;
	}
}

TEST_F(ReforgedShopTest, StockIsHellfiresOwn)
{
	UseHellfire();
	for (const uint32_t seed : { 3U, 42U, 1234U }) {
		gbIsReforged = false;
		const std::vector<std::string> hellfire = StockTown(seed);
		gbIsReforged = true;
		EXPECT_EQ(StockTown(seed), hellfire) << seed;
	}
}

TEST_F(ReforgedShopTest, TheTabsListAllTheStockAtTheVendorsOwnPrices)
{
	gbIsReforged = true;
	StockTown(42);
	ASSERT_FALSE(BoyItem.isEmpty()) << "Wirt has an item with this seed";

	OpenVisualStore(VisualStoreVendor::Smith);
	Wares expected = Priced(SmithItems);
	const Wares premium = Priced(PremiumItems);
	expected.insert(expected.end(), premium.begin(), premium.end());
	std::sort(expected.begin(), expected.end());
	EXPECT_EQ(WaresOnTabs(), expected) << "Griswold's basic and premium wares, each once, at his own prices";
	CloseVisualStore();

	OpenVisualStore(VisualStoreVendor::Witch);
	EXPECT_EQ(WaresOnTabs(), Priced(WitchItems));
	CloseVisualStore();

	OpenVisualStore(VisualStoreVendor::Healer);
	EXPECT_EQ(WaresOnTabs(), Priced(HealerItems));
	CloseVisualStore();

	OpenVisualStore(VisualStoreVendor::Boy);
	const int wirt = BoyItem._iIvalue + (BoyItem._iIvalue / 2);
	EXPECT_EQ(WaresOnTabs(), (Wares { { BoyItem._iSeed, wirt } })) << "Wirt's price, as in his talk menu";
	CloseVisualStore();
}

TEST_F(ReforgedShopTest, HellfireStockIsOnTheTabsAndWirtAsksHellfiresPrice)
{
	UseHellfire();
	gbIsReforged = true;
	StockTown(42);
	// Hellfire's item pictures aren't in the test data, so the shop isn't laid out: the tabs are checked against the stock.
	const auto onOneTab = [](const Item &item) {
		const std::vector<Poe2ShopTab> tabs = Poe2ShopTabs();
		EXPECT_EQ(std::count_if(tabs.begin(), tabs.end(), [&item](Poe2ShopTab tab) { return tab != Poe2ShopTab::Buyback && ItemOnPoe2ShopTab(item, tab); }), 1)
		    << item._iIName;
	};
	VisualStore.vendor = VisualStoreVendor::Smith;
	for (const Item &item : SmithItems)
		onOneTab(item);
	for (const Item &item : PremiumItems) {
		if (!item.isEmpty())
			onOneTab(item);
	}
	VisualStore.vendor = VisualStoreVendor::Witch;
	for (const Item &item : WitchItems)
		onOneTab(item);
	ASSERT_FALSE(BoyItem.isEmpty());
	EXPECT_EQ(WirtPrice(BoyItem), BoyItem._iIvalue - (BoyItem._iIvalue / 4));
}

TEST_F(ReforgedShopTest, OnlyTabsWithStockAreShown)
{
	gbIsReforged = true;
	SmithItems.clear();
	PremiumItems.clear();
	SmithItems.push_back(MakeSword());
	SmithItems.push_back(MakeArmour());
	SmithItems.push_back(MakeArmour());

	OpenVisualStore(VisualStoreVendor::Smith);
	EXPECT_EQ(Poe2ShopTabs(), (std::vector<Poe2ShopTab> { Poe2ShopTab::Weapons, Poe2ShopTab::BodyArmour, Poe2ShopTab::Buyback }))
	    << "No helmets, shields or jewelry for sale: no tabs for them";
	EXPECT_EQ(ActivePoe2ShopTab, Poe2ShopTab::Weapons);
	ASSERT_EQ(GetVisualStoreItems().size(), 1u);

	// Buying the last weapon takes the Weapons tab away, and the shop shows its first tab.
	CheckVisualStoreItem(WareCell(0), /*isCtrlHeld=*/true, false);
	EXPECT_EQ(SmithItems.size(), 2u);
	EXPECT_EQ(Poe2ShopTabs(), (std::vector<Poe2ShopTab> { Poe2ShopTab::BodyArmour, Poe2ShopTab::Buyback }));
	EXPECT_EQ(ActivePoe2ShopTab, Poe2ShopTab::BodyArmour);
	EXPECT_EQ(GetVisualStoreItems().size(), 2u);
	CloseVisualStore();

	// Pepin and Wirt have one tab whatever they have.
	OpenVisualStore(VisualStoreVendor::Healer);
	EXPECT_EQ(Poe2ShopTabs(), (std::vector<Poe2ShopTab> { Poe2ShopTab::Items }));
	CloseVisualStore();
	BoyItem.clear();
	OpenVisualStore(VisualStoreVendor::Boy);
	EXPECT_EQ(Poe2ShopTabs(), (std::vector<Poe2ShopTab> { Poe2ShopTab::Items }));
}

TEST_F(ReforgedShopTest, ATabWithMoreThanAPageOfWaresHasPages)
{
	gbIsReforged = true;
	SmithItems.clear();
	PremiumItems.clear();
	for (int i = 0; i < 40; i++) {
		SmithItems.push_back(MakeSword());
		SmithItems.back()._iSeed = static_cast<uint32_t>(1000 + i);
	}
	OpenVisualStore(VisualStoreVendor::Smith);
	EXPECT_GT(GetVisualStorePageCount(), 1) << "Diablo's stock is never left out for want of room";
	size_t onPages = 0;
	for (const VisualStorePage &page : VisualStore.pages)
		onPages += page.items.size();
	EXPECT_EQ(onPages, 40u);
}

TEST_F(ReforgedShopTest, SoldItemsCanBeBoughtBackForWhatTheySoldFor)
{
	gbIsReforged = true;
	StockTown(42);
	OpenVisualStore(VisualStoreVendor::Smith);
	const int goldBefore = Gold();
	Item sword = MakeSword();
	sword._iSeed = 0xB0B;
	const int sellPrice = std::max(sword._ivalue / 4, 1);
	EXPECT_EQ(VisualStoreSellPrice(sword), sellPrice) << "Diablo's sale price, shown on the hero's own items";

	MyPlayer->HoldItem = sword;
	CheckVisualStorePaste(GetVisualStoreSlotCoord({ 0, 0 }));
	EXPECT_TRUE(MyPlayer->HoldItem.isEmpty());
	EXPECT_EQ(Gold(), goldBefore + sellPrice);
	ASSERT_EQ(SmithBuyback.size(), 1u);

	SetPoe2ShopTab(Poe2ShopTab::Buyback);
	ASSERT_EQ(GetVisualStoreItems().size(), 1u);
	EXPECT_EQ(VisualStoreItemPrice(0), sellPrice);
	CheckVisualStoreItem(WareCell(0), /*isCtrlHeld=*/true, false);
	EXPECT_TRUE(SmithBuyback.empty());
	EXPECT_EQ(Gold(), goldBefore) << "Bought back for what it sold for";
	bool back = false;
	for (int i = 0; i < MyPlayer->_pNumInv; i++)
		back = back || MyPlayer->InvList[i]._iSeed == sword._iSeed;
	for (const Item &item : MyPlayer->InvBody)
		back = back || (!item.isEmpty() && item._iSeed == sword._iSeed);
	EXPECT_TRUE(back);
}

TEST_F(ReforgedShopTest, AWarePickedUpIsPaidForInTheBag)
{
	gbIsReforged = true;
	SmithItems.clear();
	PremiumItems.clear();
	SmithItems.push_back(MakeArmour());
	OpenVisualStore(VisualStoreVendor::Smith);
	const int price = SmithItems[0]._iIvalue;
	const int goldBefore = Gold();

	CheckVisualStoreItem(WareCell(0), /*isCtrlHeld=*/false, false);
	EXPECT_TRUE(HasPendingPurchase()) << "On the cursor, not paid for yet";
	EXPECT_EQ(Gold(), goldBefore);
	EXPECT_EQ(SmithItems.size(), 1u);

	ASSERT_TRUE(AutoPlaceItemInInventory(*MyPlayer, MyPlayer->HoldItem, true));
	MyPlayer->HoldItem.clear();
	SettlePendingPurchase();
	EXPECT_FALSE(HasPendingPurchase());
	EXPECT_EQ(Gold(), goldBefore - price);
	EXPECT_TRUE(SmithItems.empty());
}

TEST_F(ReforgedShopTest, VanillaKeepsItsOwnGrid)
{
	EXPECT_FALSE(UsesShopTabs());
	gbIsReforged = true;
	EXPECT_TRUE(UsesShopTabs());
	gbIsReforged = false;
	gbIsPoe2 = true;
	EXPECT_TRUE(UsesShopTabs());
}

} // namespace
} // namespace devilution
