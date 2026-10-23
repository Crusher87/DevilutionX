/**
 * POE2 Mashup town wares (harder monsters plan, step 10): vendors stock POE2 wares for the deepest area the hero has
 * reached, keep Diablo's vendor level in CF_LEVEL, and a bought ware rebuilds on the base it was stocked with.
 */
#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "items/validation.h"
#include "levels/gendung.h"
#include "loadsave.h"
#include "pfile.h"
#include "player.h"
#include "poe2/area_levels.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/stash_tabs.hpp"
#include "qol/stash.h"
#include "spells.h"
#include "stores.h"
#include "tables/playerdat.hpp"
#include "utils/file_util.h"
#include "utils/paths.h"

namespace devilution {
namespace {

class Poe2VendorItemsTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		*MyPlayer = {};
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		gbIsPoe2 = true;
		sgGameInitInfo.nDifficulty = DIFF_NORMAL;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadSpellData();
		LoadPlayerDataFiles();
		// Also loads the bases and the area levels the vendors stock for.
		LoadModifierData();
		MyPlayer->setCharacterLevel(30);
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		gbIsMultiplayer = false;
		LoadModifierData();
		UnloadModArchives();
	}

	/** Every ware of every POE2 vendor that sells equipment: Griswold, his premium stock, Adria and Wirt. */
	static std::vector<Item> StockAllVendors(uint32_t seed, int level = 16)
	{
		std::vector<Item> wares;
		SetRndSeed(seed);
		SpawnSmith(level);
		wares.insert(wares.end(), SmithItems.begin(), SmithItems.end());
		SpawnWitch(level);
		wares.insert(wares.end(), WitchItems.begin(), WitchItems.end());
		PremiumItems.clear();
		PremiumItemLevel = 1;
		SpawnPremium(*MyPlayer);
		wares.insert(wares.end(), PremiumItems.begin(), PremiumItems.end());
		BoyItem = {};
		BoyItemLevel = 0;
		SpawnBoy(MyPlayer->getCharacterLevel());
		if (!BoyItem.isEmpty())
			wares.push_back(BoyItem);
		// Adria's pinned potions and scrolls aren't town wares (no CF_TOWN): they rebuild like any other item.
		std::erase_if(wares, [](const Item &item) { return (item._iCreateInfo & CF_TOWN) == 0; });
		return wares;
	}

	static Item Rebuild(const Item &item)
	{
		Item rebuilt;
		RecreateItem(*MyPlayer, rebuilt, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff);
		return rebuilt;
	}

	static void ExpectSameWare(const Item &rebuilt, const Item &ware)
	{
		const std::string name = ware._iIName;
		EXPECT_EQ(rebuilt.IDidx, ware.IDidx) << name;
		EXPECT_EQ(rebuilt._iCreateInfo, ware._iCreateInfo) << name;
		EXPECT_EQ(rebuilt._iMagical, ware._iMagical) << name;
		EXPECT_EQ(std::string(rebuilt._iIName), name);
		EXPECT_EQ(rebuilt._iPrePower, ware._iPrePower) << name;
		EXPECT_EQ(rebuilt._iSufPower, ware._iSufPower) << name;
		EXPECT_EQ(rebuilt._iRarePowers, ware._iRarePowers) << name;
		for (size_t i = 0; i < ware._iPoe2Mods.size(); i++) {
			EXPECT_EQ(rebuilt._iPoe2Mods[i].family, ware._iPoe2Mods[i].family) << name << " mod " << i;
			EXPECT_EQ(rebuilt._iPoe2Mods[i].tier, ware._iPoe2Mods[i].tier) << name << " mod " << i;
			EXPECT_EQ(rebuilt._iPoe2Mods[i].values, ware._iPoe2Mods[i].values) << name << " mod " << i;
		}
		EXPECT_EQ(rebuilt._iPoe2Stats, ware._iPoe2Stats) << name;
		EXPECT_EQ(rebuilt._iAC, ware._iAC) << name;
		EXPECT_EQ(rebuilt._iMinDam, ware._iMinDam) << name;
		EXPECT_EQ(rebuilt._iMaxDam, ware._iMaxDam) << name;
		EXPECT_EQ(rebuilt._iPLDam, ware._iPLDam) << name;
		EXPECT_EQ(rebuilt._iPLAC, ware._iPLAC) << name;
		EXPECT_EQ(rebuilt._iPLToHit, ware._iPLToHit) << name;
		EXPECT_EQ(rebuilt._iIvalue, ware._iIvalue) << name;
		EXPECT_EQ(GetPoe2ItemLevel(rebuilt), GetPoe2ItemLevel(ware)) << name;
	}

	/** Saves the item in a single-player stash and loads it back, through the game save's item record. */
	static Item SaveAndLoad(Item item)
	{
		const std::string previousPrefPath = paths::PrefPath();
		const std::string folder = ::testing::TempDir() + "poe2_vendor_items_" + ::testing::UnitTest::GetInstance()->current_test_info()->name() + DIRECTORY_SEPARATOR_STR;
		CreateDir(folder.c_str());
		for (const std::string &file : ListFiles(folder.c_str()))
			RemoveFile((folder + file).c_str());
		paths::SetPrefPath(folder);

		Stash = {};
		StashTabs.reset();
		// Fixed cells: GetInventorySize needs the cursor art from the game files, which tests may not have.
		const Size size { 2, 3 };
		item.position = { 0, size.height - 1 };
		Stash.stashList.push_back(item);
		for (int x = 0; x < size.width; x++) {
			for (int y = 0; y < size.height; y++)
				Stash.stashGrids[0][x][y] = 1;
		}
		Stash.dirty = true;
		sfile_write_stash();

		Stash = {};
		StashTabs.reset();
		LoadStash();
		Item loaded {};
		if (Stash.stashList.size() == 1)
			loaded = Stash.stashList[0];
		else
			ADD_FAILURE() << Stash.stashList.size() << " items came back";
		Stash = {};
		StashTabs.reset();
		paths::SetPrefPath(previousPrefPath);
		return loaded;
	}
};

TEST_F(Poe2VendorItemsTest, EveryVendorItemRoundTripsByIdidx)
{
	int checked = 0;
	for (uint32_t seed = 1; seed <= 6; seed++) {
		for (const Item &ware : StockAllVendors(seed)) {
			ExpectSameWare(Rebuild(ware), ware);
			checked++;
		}
	}
	EXPECT_GT(checked, 100);
}

TEST_F(Poe2VendorItemsTest, WaresKeepTheirBaseWhenTheItemTableMoves)
{
	const std::vector<Item> wares = StockAllVendors(3);
	ASSERT_FALSE(wares.empty());
	// A later item table where no base drops at the vendor's level: picking the base again would find nothing.
	for (ItemData &data : AllItemsList)
		data.iMinMLvl = static_cast<uint8_t>(std::min(data.iMinMLvl + 70, 255));
	for (const Item &ware : wares)
		ExpectSameWare(Rebuild(ware), ware);
}

TEST_F(Poe2VendorItemsTest, BoughtWaresComeBackFromTheStash)
{
	int rares = 0;
	for (const Item &ware : StockAllVendors(5)) {
		if (ware._iMagical != ITEM_QUALITY_RARE || rares >= 4)
			continue;
		rares++;
		ExpectSameWare(SaveAndLoad(ware), ware);
	}
	EXPECT_GT(rares, 0);
}

TEST_F(Poe2VendorItemsTest, AnEmptyPoolTakesTheOneValueAPickTakes)
{
	// No item drops below monster level 0, so the pick after the two percent rolls finds nothing.
	uint32_t seed = 1;
	for (; seed < 10000; seed++) {
		SetRndSeed(seed);
		if (GenerateRnd(100) <= 40 && GenerateRnd(100) <= 25)
			break;
	}
	ASSERT_LT(seed, 10000U);
	SetRndSeed(seed);
	EXPECT_EQ(RndItemForMonsterLevel(-1), IDI_HEAL);
	const uint32_t after = GetLCGEngineState();
	SetRndSeed(seed);
	DiscardRandomValues(3);
	EXPECT_EQ(after, GetLCGEngineState());
}

TEST_F(Poe2VendorItemsTest, WirtCreateInfoStaysInSixBits)
{
	MyPlayer->setCharacterLevel(80);
	for (uint32_t seed = 1; seed <= 10; seed++) {
		BoyItem = {};
		BoyItemLevel = 0;
		SetRndSeed(seed);
		SpawnBoy(MyPlayer->getCharacterLevel());
		ASSERT_FALSE(BoyItem.isEmpty());
		EXPECT_EQ(BoyItem._iCreateInfo & CF_LEVEL, CF_LEVEL);
		EXPECT_EQ(BoyItem._iCreateInfo & ~CF_LEVEL, CF_BOY);
		ExpectSameWare(Rebuild(BoyItem), BoyItem);
	}
}

TEST_F(Poe2VendorItemsTest, TownItemsPassMpValidation)
{
	for (const int heroLevel : { 1, 30, 80 }) {
		MyPlayer->setCharacterLevel(heroLevel);
		for (uint32_t seed = 1; seed <= 4; seed++) {
			for (const Item &ware : StockAllVendors(seed)) {
				gbIsMultiplayer = true;
				const Item rebuilt = Rebuild(ware);
				EXPECT_TRUE(IsItemValid(*MyPlayer, rebuilt)) << ware._iIName << " at hero level " << heroLevel;
				EXPECT_LE(ware._iCreateInfo & CF_LEVEL, 30) << "Diablo's vendor level stays in CF_LEVEL";
				gbIsMultiplayer = false;
			}
		}
	}
}

TEST_F(Poe2VendorItemsTest, VendorsStockForTheDeepestAreaVisited)
{
	const int first = AreaLevelForFloor(1);
	ASSERT_GT(first, 0) << "the area level table";
	EXPECT_EQ(VendorAreaLevel(*MyPlayer, DIFF_NORMAL), first + VendorAreaLevelBonus) << "nothing visited yet";

	MyPlayer->_pLvlVisited[1] = true;
	MyPlayer->_pLvlVisited[8] = true;
	MyPlayer->_pLvlVisited[5] = true;
	const int deepest = std::max({ AreaLevelForFloor(1), AreaLevelForFloor(5), AreaLevelForFloor(8) });
	EXPECT_EQ(VendorAreaLevel(*MyPlayer, DIFF_NORMAL), deepest + VendorAreaLevelBonus);
	EXPECT_EQ(VendorAreaLevel(*MyPlayer, DIFF_NIGHTMARE), deepest + AreaLevelsPerDifficulty + VendorAreaLevelBonus);

	for (int floor = 1; floor < NUMLEVELS; floor++)
		MyPlayer->_pLvlVisited[floor] = true;
	EXPECT_EQ(VendorAreaLevel(*MyPlayer, DIFF_HELL), MaxPoe2ItemLevel);

	// The wares take the area's item level; CF_LEVEL keeps Diablo's vendor level.
	std::fill(std::begin(MyPlayer->_pLvlVisited), std::end(MyPlayer->_pLvlVisited), false);
	MyPlayer->_pLvlVisited[8] = true;
	const int areaLevel = VendorAreaLevel(*MyPlayer, DIFF_NORMAL);
	SetRndSeed(11);
	SpawnSmith(16);
	ASSERT_FALSE(SmithItems.empty());
	for (const Item &ware : SmithItems) {
		EXPECT_EQ(ware._iCreateInfo & CF_LEVEL, 16) << ware._iIName;
		if (EquipmentClassWeight(ware._itype) == 0)
			continue;
		EXPECT_EQ(GetPoe2ItemLevel(ware), areaLevel) << ware._iIName;
	}

	// Without the table (a game without POE2 data) the item level follows the vendor level as before.
	gbIsPoe2 = false;
	LoadAreaLevels();
	gbIsPoe2 = true;
	EXPECT_EQ(VendorAreaLevel(*MyPlayer, DIFF_NORMAL), 0);
	SetRndSeed(11);
	SpawnSmith(16);
	for (const Item &ware : SmithItems) {
		if (EquipmentClassWeight(ware._itype) > 0)
			EXPECT_EQ(GetPoe2ItemLevel(ware), Poe2ItemLevel(16)) << ware._iIName;
	}
}

TEST_F(Poe2VendorItemsTest, DiabloWaresStillPickTheirBase)
{
	// Diablo's own wares are rebuilt by picking the base from the seed again, as before.
	gbIsPoe2 = false;
	LoadModifierData();
	UnloadModArchives();
	LoadModArchives({ { "hf" } });
	LoadItemData();
	SetRndSeed(4);
	SpawnSmith(16);
	ASSERT_FALSE(SmithItems.empty());
	for (const Item &ware : SmithItems) {
		Item rebuilt;
		RecreateItem(*MyPlayer, rebuilt, IDI_GOLD, ware._iCreateInfo, ware._iSeed, ware._ivalue, ware.dwBuff);
		EXPECT_EQ(rebuilt.IDidx, ware.IDidx) << "the base comes from the seed, not the index passed in";
	}
}

} // namespace
} // namespace devilution
