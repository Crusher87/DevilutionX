#include <algorithm>
#include <cstdint>
#include <set>
#include <string>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "spells.h"
#include "utils/str_cat.hpp"

namespace devilution {
namespace {

class Poe2RareItemsTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		UnloadModArchives();
		LoadModArchives({ { "hf" } });
		LoadItemData();
		LoadSpellData();
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
	}

	/** Generates a level 30 magic-capable item of the given base, the way a dungeon drop is made. */
	static Item MakeItem(_item_indexes base, uint32_t seed)
	{
		Item item = {};
		item._iMiscId = AllItemsList[base].iMiscId;
		SetRndSeed(seed);
		SetupAllItems(*MyPlayer, item, base, AdvanceRndSeed(), 30, 1, /*onlygood=*/true, /*pregen=*/false, 0, /*forceNotUnique=*/true);
		item._iIdentified = true;
		return item;
	}

	static _item_indexes FindBase(std::string_view name)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == name && AllItemsList[i].dropRate > 0)
				return static_cast<_item_indexes>(i);
		}
		return IDI_GOLD;
	}
};

TEST_F(Poe2RareItemsTest, RaresRollWithDistinctAffixesAndRecreateIdentically)
{
	gbIsPoe2 = true;
	int rares = 0;
	for (const std::string_view baseName : { "Long Sword", "Short Bow", "Full Plate Mail", "Ring", "Amulet", "Tower Shield", "Helm" }) {
		const _item_indexes base = FindBase(baseName);
		ASSERT_NE(base, IDI_GOLD) << baseName;
		for (uint32_t seed = 1; seed <= 400; seed++) {
			const Item item = MakeItem(base, seed);
			EXPECT_NE(item.dwBuff & CF_POE2, 0U);
			if (item._iMagical != ITEM_QUALITY_RARE)
				continue;
			rares++;

			std::set<item_effect_type> powers;
			for (const item_effect_type power : item._iRarePowers) {
				if (power != IPL_INVALID)
					powers.insert(power);
			}
			const size_t numPowers = static_cast<size_t>(std::count_if(item._iRarePowers.begin(), item._iRarePowers.end(), [](item_effect_type p) { return p != IPL_INVALID; }));
			EXPECT_EQ(powers.size(), numPowers) << "duplicate affix on " << item._iIName;
			// Affixes sharing a stat would show the same combined total twice.
			std::set<std::string> lines;
			for (const item_effect_type power : powers)
				lines.insert(std::string(PrintItemPower(power, item).str()));
			EXPECT_EQ(lines.size(), powers.size()) << "overlapping affixes on " << item._iIName;
			EXPECT_GE(numPowers, 1U);
			EXPECT_LE(numPowers, MaxRareAffixes);
			EXPECT_EQ(std::string(item._iIName), GetRareItemName(item));
			EXPECT_EQ(item.getName().str(), GetRareItemName(item));

			// Multiplayer sync and the hero file rebuild items from seed; the rare must come back the same.
			Item recreated;
			RecreateItem(*MyPlayer, recreated, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff);
			EXPECT_EQ(recreated._iMagical, ITEM_QUALITY_RARE);
			EXPECT_EQ(recreated._iRarePowers, item._iRarePowers);
			EXPECT_EQ(std::string(recreated._iIName), std::string(item._iIName));
			EXPECT_EQ(recreated._iPLDam, item._iPLDam);
			EXPECT_EQ(recreated._iPLToHit, item._iPLToHit);
			EXPECT_EQ(recreated._iPLAC, item._iPLAC);
			EXPECT_EQ(recreated._iIvalue, item._iIvalue);

			// Saved levels don't store the affix list, so it's restored from the seed on load.
			Item loaded = item;
			loaded._iRarePowers.fill(IPL_INVALID);
			loaded._iRareTiers.fill(0);
			RestoreRareItemPowers(loaded);
			EXPECT_EQ(loaded._iRarePowers, item._iRarePowers);
			EXPECT_EQ(loaded._iRareTiers, item._iRareTiers);
			for (size_t i = 0; i < MaxRareAffixes; i++)
				EXPECT_EQ(item._iRarePowers[i] == IPL_INVALID, item._iRareTiers[i] == 0);
		}
	}
	EXPECT_GT(rares, 100);
}

TEST_F(Poe2RareItemsTest, AffixTiersRankByLevel)
{
	// Of the strength suffixes that roll on the same items, the highest level one is tier 1.
	const PLStruct *best = nullptr;
	const PLStruct *worst = nullptr;
	for (const PLStruct &affix : ItemSuffixes) {
		if (affix.power.type != IPL_STR || affix.PLGOE == GOE_EVIL)
			continue;
		if (best == nullptr || affix.PLMinLvl > best->PLMinLvl)
			best = &affix;
		if (worst == nullptr || affix.PLMinLvl < worst->PLMinLvl)
			worst = &affix;
	}
	ASSERT_NE(best, nullptr);
	ASSERT_NE(best, worst);
	EXPECT_EQ(AffixTier(ItemSuffixes, *best), 1);
	EXPECT_GT(AffixTier(ItemSuffixes, *worst), 1);
}

TEST_F(Poe2RareItemsTest, MagicItemNamesStillTranslate)
{
	// The rare roll uses one random value; regenerating a magic item's name must skip it too.
	gbIsPoe2 = true;
	int magic = 0;
	for (const std::string_view baseName : { "Long Sword", "Short Bow", "Full Plate Mail", "Ring" }) {
		const _item_indexes base = FindBase(baseName);
		for (uint32_t seed = 1; seed <= 300; seed++) {
			const Item item = MakeItem(base, seed);
			if (item._iMagical != ITEM_QUALITY_MAGIC)
				continue;
			magic++;
			EXPECT_EQ(item.getName().str(), std::string(item._iIName)) << "seed " << seed;
		}
	}
	EXPECT_GT(magic, 100);
}

TEST_F(Poe2RareItemsTest, NoRaresWithoutTheMod)
{
	gbIsPoe2 = false;
	const _item_indexes base = FindBase("Long Sword");
	for (uint32_t seed = 1; seed <= 400; seed++) {
		const Item item = MakeItem(base, seed);
		EXPECT_EQ(item.dwBuff & CF_POE2, 0U);
		EXPECT_NE(item._iMagical, ITEM_QUALITY_RARE);
	}
}

} // namespace
} // namespace devilution
