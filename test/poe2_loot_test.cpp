#include <algorithm>
#include <array>
#include <numeric>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/loot.hpp"
#include "spells.h"
#include "stores.h"
#include "utils/is_of.hpp"

namespace devilution {
namespace {

class Poe2LootTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		gbIsPoe2 = true;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadSpellData();
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		UnloadModArchives();
	}

	static _item_indexes FindBase(std::string_view name)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == name && AllItemsList[i].dropRate > 0)
				return static_cast<_item_indexes>(i);
		}
		return IDI_GOLD;
	}

	/** Builds a dropped item the way a loot roll does. */
	static Item MakeDrop(_item_indexes base, item_quality rarity, uint32_t seed, int level = 20, int uidOffset = 0)
	{
		Item item = {};
		SetLootRarity(item, rarity);
		item.dwBuff |= (static_cast<uint32_t>(uidOffset) << 1) & CF_UIDOFFSET;
		SetupAllItems(*MyPlayer, item, base, seed, level, 1, true, false, uidOffset);
		item._iIdentified = true;
		return item;
	}

	template <typename F>
	static std::array<int, 8> Count(int trials, F roll)
	{
		std::array<int, 8> counts {};
		SetRndSeed(12345);
		for (int i = 0; i < trials; i++)
			counts[static_cast<size_t>(roll())]++;
		return counts;
	}
};

TEST_F(Poe2LootTest, KillsGiveRollsByToughness)
{
	EXPECT_EQ(ExpectedLootRollsPerMille(LootSource::Normal), 180);
	EXPECT_EQ(ExpectedLootRollsPerMille(LootSource::Boss), 6000);

	constexpr int Trials = 20000;
	SetRndSeed(7);
	int normal = 0;
	int rare = 0;
	for (int i = 0; i < Trials; i++) {
		normal += RollLootCount(LootSource::Normal);
		rare += RollLootCount(LootSource::Rare);
	}
	EXPECT_NEAR(normal / static_cast<double>(Trials), 0.18, 0.015);
	EXPECT_EQ(rare, 2 * Trials) << "two whole rolls every time";
}

TEST_F(Poe2LootTest, CategoriesFollowTheBible)
{
	const auto weights = LootCategoryWeights(LootSource::Normal);
	EXPECT_EQ(std::accumulate(weights.begin(), weights.end(), 0), 100);
	EXPECT_EQ(weights[static_cast<size_t>(LootCategory::Equipment)], 48);
	EXPECT_EQ(weights[static_cast<size_t>(LootCategory::Gold)], 22);

	const auto boss = LootCategoryWeights(LootSource::Boss);
	EXPECT_EQ(boss[static_cast<size_t>(LootCategory::Equipment)], 96);
	EXPECT_EQ(boss[static_cast<size_t>(LootCategory::Gold)], 11);
	EXPECT_EQ(boss[static_cast<size_t>(LootCategory::Misc)], 0);
}

TEST_F(Poe2LootTest, RarityTablesMatchTheSource)
{
	constexpr int Trials = 40000;
	const auto normal = Count(Trials, [] { return RollEquipmentRarity(LootSource::Normal); });
	EXPECT_NEAR(normal[ITEM_QUALITY_NORMAL] / static_cast<double>(Trials), 0.57, 0.01);
	EXPECT_NEAR(normal[ITEM_QUALITY_MAGIC] / static_cast<double>(Trials), 0.35, 0.01);
	EXPECT_NEAR(normal[ITEM_QUALITY_RARE] / static_cast<double>(Trials), 0.0775, 0.005);

	const auto boss = Count(Trials, [] { return RollEquipmentRarity(LootSource::Boss); });
	EXPECT_NEAR(boss[ITEM_QUALITY_RARE] / static_cast<double>(Trials), 0.65, 0.01);
	EXPECT_NEAR(boss[ITEM_QUALITY_UNIQUE] / static_cast<double>(Trials), 0.05, 0.005);

	EXPECT_EQ(LootItemLevelBonus(LootSource::Rare), 1);
	EXPECT_EQ(LootItemLevelBonus(LootSource::Boss), 2);
}

TEST_F(Poe2LootTest, NaturalRaresHaveFourToSixAffixes)
{
	constexpr int Trials = 24000;
	const auto counts = Count(Trials, RollNaturalRareAffixCount);
	EXPECT_EQ(counts[4] + counts[5] + counts[6], Trials);
	EXPECT_NEAR(counts[4] / static_cast<double>(Trials), 8.0 / 12, 0.015);
	EXPECT_NEAR(counts[6] / static_cast<double>(Trials), 1.0 / 12, 0.01);
}

TEST_F(Poe2LootTest, OldBasesFadeOut)
{
	EXPECT_EQ(BaseRecencyPercent(10, 9), 100);
	EXPECT_EQ(BaseRecencyPercent(10, 4), 70);
	EXPECT_EQ(BaseRecencyPercent(20, 9), 35);
	EXPECT_EQ(BaseRecencyPercent(30, 1), 10);
}

TEST_F(Poe2LootTest, OrbsSitInValueBands)
{
	EXPECT_EQ(OrbCurrencyBand(IMISC_ORB_TRANSMUTE), CurrencyBand::Common);
	EXPECT_EQ(OrbCurrencyBand(IMISC_ORB_REGAL), CurrencyBand::Uncommon);
	EXPECT_EQ(OrbCurrencyBand(IMISC_ORB_EXALTED), CurrencyBand::Rare);
	EXPECT_EQ(OrbCurrencyBand(IMISC_ORB_DIVINE), CurrencyBand::Premium);
}

TEST_F(Poe2LootTest, DropsTakeTheRolledRarityAndRecreateTheSame)
{
	const _item_indexes base = FindBase("Shortsword");
	ASSERT_NE(base, IDI_GOLD);
	for (uint32_t seed = 1; seed <= 60; seed++) {
		const Item normal = MakeDrop(base, ITEM_QUALITY_NORMAL, seed);
		EXPECT_EQ(normal._iMagical, ITEM_QUALITY_NORMAL);

		const Item magic = MakeDrop(base, ITEM_QUALITY_MAGIC, seed);
		EXPECT_EQ(magic._iMagical, ITEM_QUALITY_MAGIC) << seed;

		const Item rare = MakeDrop(base, ITEM_QUALITY_RARE, seed);
		ASSERT_EQ(rare._iMagical, ITEM_QUALITY_RARE);
		const auto affixes = std::count_if(rare._iRarePowers.begin(), rare._iRarePowers.end(), [](item_effect_type p) { return p != IPL_INVALID; });
		EXPECT_GE(affixes, 3) << "four wanted; overlapping affixes can leave one out";
		EXPECT_LE(affixes, 6);

		for (const Item *item : { &normal, &magic, &rare }) {
			Item recreated;
			RecreateItem(*MyPlayer, recreated, item->IDidx, item->_iCreateInfo, item->_iSeed, item->_ivalue, item->dwBuff);
			EXPECT_EQ(recreated._iMagical, item->_iMagical);
			EXPECT_EQ(std::string(recreated._iIName), std::string(item->_iIName));
			EXPECT_EQ(recreated._iRarePowers, item->_iRarePowers);
			EXPECT_EQ(recreated._iPLDam, item->_iPLDam);
		}
	}
}

TEST_F(Poe2LootTest, UniqueRollsBuildTheChosenUnique)
{
	// Find a unique whose base still drops, and build it from its offset like a loot roll does.
	for (size_t uid = 0; uid < UniqueItems.size(); uid++) {
		const UniqueItem &unique = UniqueItems[uid];
		_item_indexes base = IDI_GOLD;
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iItemId == unique.UIItemId && AllItemsList[i].iMiscId != IMISC_UNIQUE && AllItemsList[i].itype != ItemType::Misc) {
				base = static_cast<_item_indexes>(i);
				break;
			}
		}
		if (base == IDI_GOLD)
			continue;
		const int level = std::max<int>(unique.UIMinLvl, 1);
		// The same list SetupAllItems picks from: this base's uniques at or below the level, in table order.
		std::vector<uint8_t> valid;
		for (size_t other = 0; other < UniqueItems.size(); other++) {
			if (UniqueItems[other].UIItemId == unique.UIItemId && level >= UniqueItems[other].UIMinLvl)
				valid.push_back(static_cast<uint8_t>(other));
		}
		const auto found = std::find(valid.begin(), valid.end(), static_cast<uint8_t>(uid));
		ASSERT_NE(found, valid.end());
		const int offset = static_cast<int>(valid.end() - found) - 1;
		if (offset > 15)
			continue;
		const Item item = MakeDrop(base, ITEM_QUALITY_UNIQUE, 99, level, offset);
		EXPECT_EQ(item._iMagical, ITEM_QUALITY_UNIQUE) << unique.UIName;
		EXPECT_EQ(item._iUid, static_cast<int>(uid)) << unique.UIName;

		Item recreated;
		RecreateItem(*MyPlayer, recreated, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff);
		EXPECT_EQ(recreated._iUid, item._iUid) << unique.UIName;
		return;
	}
	FAIL() << "no droppable unique found";
}

TEST_F(Poe2LootTest, ItemLevelsRunToEightyTwo)
{
	EXPECT_EQ(Poe2ItemLevel(1), 1);
	EXPECT_EQ(Poe2ItemLevel(30), 39) << "the end of Normal";
	EXPECT_EQ(Poe2ItemLevel(30 + 15), 59) << "the end of Nightmare";
	EXPECT_EQ(Poe2ItemLevel(30 + 30, 2), 80) << "a Hell boss";
	EXPECT_EQ(Poe2ItemLevel(80, 2), MaxPoe2ItemLevel);
	EXPECT_EQ(Poe2ItemLevel(0), 1);
}

TEST_F(Poe2LootTest, DropsKeepTheirItemLevel)
{
	const _item_indexes base = FindBase("Shortsword");
	ASSERT_NE(base, IDI_GOLD);
	Item item = {};
	SetLootRarity(item, ITEM_QUALITY_RARE);
	SetPoe2ItemLevel(item, 57);
	SetupAllItems(*MyPlayer, item, base, 1234, 20, 1, true, false);
	EXPECT_EQ(GetPoe2ItemLevel(item), 57);
	EXPECT_EQ(GetLootRarity(item), ITEM_QUALITY_RARE);

	Item recreated;
	RecreateItem(*MyPlayer, recreated, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff);
	EXPECT_EQ(GetPoe2ItemLevel(recreated), 57);
	EXPECT_EQ(recreated._iRarePowers, item._iRarePowers);

	// Older items have none, and flasks and gems use those bits for something else.
	Item old = {};
	SetupAllItems(*MyPlayer, old, base, 1234, 20, 1, true, false);
	EXPECT_EQ(GetPoe2ItemLevel(old), 0);
	Item flask = {};
	flask._itype = ItemType::Misc;
	flask.dwBuff = CF_POE2_FLASK_PREFIX | CF_POE2_FLASK_SUFFIX;
	EXPECT_EQ(GetPoe2ItemLevel(flask), 0);
}

TEST_F(Poe2LootTest, VendorsPayEighteenPercent)
{
	EXPECT_EQ(VendorSellValue(1000), 180);
	EXPECT_EQ(VendorSellValue(3), 1);
	gbIsPoe2 = false;
	EXPECT_EQ(VendorSellValue(1000), 250);
	gbIsPoe2 = true;
}

TEST_F(Poe2LootTest, AdriaOnlySellsCommonCurrency)
{
	for (uint32_t seed = 1; seed <= 40; seed++) {
		SetRndSeed(seed);
		SpawnWitch(16);
		for (const Item &item : WitchItems) {
			EXPECT_FALSE(IsAnyOf(item._iMiscId, IMISC_ORB_ALCHEMY, IMISC_ORB_CHAOS, IMISC_ORB_REGAL, IMISC_ORB_EXALTED, IMISC_ORB_DIVINE, IMISC_ORB_VAAL,
			    IMISC_WAYSTONE, IMISC_UNCUT_SKILL, IMISC_UNCUT_SUPPORT, IMISC_UNCUT_SPIRIT, IMISC_CHARM_GOLDEN, IMISC_CHARM_FIRE))
			    << item._iIName;
			if (item._itype == ItemType::Staff)
				EXPECT_EQ(GetPoe2ItemLevel(item), Poe2ItemLevel(16));
		}
	}
}

} // namespace
} // namespace devilution
