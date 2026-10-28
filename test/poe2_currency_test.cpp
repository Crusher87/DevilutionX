#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "admin_menu.hpp"
#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "player.h"
#include "poe2/currency.hpp"
#include "poe2/stacking.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2CurrencyTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		*MyPlayer = {};
		MyPlayer->setCharacterLevel(20);
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

	static Item Make(std::string_view baseName, item_quality quality)
	{
		Item item;
		EXPECT_TRUE(AdminGenerateItem(*MyPlayer, item, FindBase(baseName), quality, 20, 777)) << baseName;
		// Currency waits for identified items, so the hero has read these.
		item._iIdentified = true;
		return item;
	}

	static Item MakeWhere(std::string_view baseName, item_quality quality, const std::function<bool(const Item &)> &wanted)
	{
		for (int seed = 1; seed < 2000; seed++) {
			Item item;
			if (AdminGenerateItem(*MyPlayer, item, FindBase(baseName), quality, 30, seed) && wanted(item)) {
				item._iIdentified = true;
				return item;
			}
		}
		ADD_FAILURE() << "no " << baseName << " matched";
		return {};
	}

	static void ExpectRecreates(const Item &item)
	{
		Item recreated;
		RecreateItem(*MyPlayer, recreated, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff);
		EXPECT_EQ(recreated._iMagical, item._iMagical);
		EXPECT_EQ(std::string(recreated._iIName), std::string(item._iIName));
		EXPECT_EQ(recreated._iRarePowers, item._iRarePowers);
	}
};

TEST_F(Poe2CurrencyTest, EveryOrbHasAnItem)
{
	for (const std::string_view name : { "Orb of Transmutation", "Orb of Alchemy", "Chaos Orb", "Orb of Scouring", "Orb of Augmentation", "Regal Orb", "Exalted Orb", "Divine Orb", "Vaal Orb" }) {
		const _item_indexes base = FindBase(name);
		ASSERT_NE(base, IDI_GOLD) << name;
		EXPECT_NE(CurrencyOrbForMiscId(AllItemsList[base].iMiscId), CurrencyOrb::None) << name;
		Item orb;
		ASSERT_TRUE(AdminGenerateItem(*MyPlayer, orb, base, ITEM_QUALITY_NORMAL, 1, 5));
		EXPECT_EQ(orb.IDidx, base);
	}
}

TEST_F(Poe2CurrencyTest, TransmutationMakesMagic)
{
	Item item = Make("Shortsword", ITEM_QUALITY_NORMAL);
	ASSERT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Transmutation, *MyPlayer, item));
	EXPECT_EQ(item._iMagical, ITEM_QUALITY_MAGIC);
	EXPECT_TRUE(item._iIdentified);
	ExpectRecreates(item);
	EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Transmutation, *MyPlayer, item));
}

TEST_F(Poe2CurrencyTest, AlchemyAndChaosMakeRares)
{
	Item item = Make("Iron Cap", ITEM_QUALITY_MAGIC);
	ASSERT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Alchemy, *MyPlayer, item));
	EXPECT_EQ(item._iMagical, ITEM_QUALITY_RARE);
	ExpectRecreates(item);

	const int oldSeed = item._iSeed;
	ASSERT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Chaos, *MyPlayer, item));
	EXPECT_EQ(item._iMagical, ITEM_QUALITY_RARE);
	EXPECT_NE(item._iSeed, oldSeed);
	ExpectRecreates(item);
}

TEST_F(Poe2CurrencyTest, ScouringMakesNormal)
{
	Item item = Make("Crude Bow", ITEM_QUALITY_RARE);
	ASSERT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Scouring, *MyPlayer, item));
	EXPECT_EQ(item._iMagical, ITEM_QUALITY_NORMAL);
	ExpectRecreates(item);
}

TEST_F(Poe2CurrencyTest, RejectsWrongItems)
{
	Item normal = Make("Shortsword", ITEM_QUALITY_NORMAL);
	EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Chaos, *MyPlayer, normal));
	EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Scouring, *MyPlayer, normal));

	Item unique;
	ASSERT_TRUE(AdminGenerateItem(*MyPlayer, unique, FindBase("Crude Bow"), ITEM_QUALITY_UNIQUE, 30, 99));
	EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Alchemy, *MyPlayer, unique));
}

TEST_F(Poe2CurrencyTest, WaitsUntilTheItemIsIdentified)
{
	Item magic = Make("Iron Cap", ITEM_QUALITY_MAGIC);
	magic._iIdentified = false;
	EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Alchemy, *MyPlayer, magic));
	EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Whetstone, *MyPlayer, magic));
	EXPECT_FALSE(CurrencyFitsItem(CurrencyOrb::Whetstone, magic));
	EXPECT_EQ(magic._iMagical, ITEM_QUALITY_MAGIC);
	EXPECT_FALSE(magic._iIdentified);

	// A plain item has nothing to hide.
	Item normal = Make("Shortsword", ITEM_QUALITY_NORMAL);
	normal._iIdentified = false;
	EXPECT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Transmutation, *MyPlayer, normal));
}

TEST_F(Poe2CurrencyTest, KeepsTheGameRngState)
{
	SetRndSeed(4242);
	const uint32_t before = GetLCGEngineState();
	Item item = Make("Shortsword", ITEM_QUALITY_NORMAL);
	ASSERT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Alchemy, *MyPlayer, item));
	EXPECT_EQ(GetLCGEngineState(), before);
}

TEST_F(Poe2CurrencyTest, AugmentationAddsTheMissingAffix)
{
	Item item = MakeWhere("Shortsword", ITEM_QUALITY_MAGIC, [](const Item &candidate) {
		return candidate._iMagical == ITEM_QUALITY_MAGIC && (candidate._iPrePower == IPL_INVALID) != (candidate._iSufPower == IPL_INVALID);
	});
	const item_effect_type kept = item._iPrePower != IPL_INVALID ? item._iPrePower : item._iSufPower;
	ASSERT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Augmentation, *MyPlayer, item));
	EXPECT_EQ(item._iMagical, ITEM_QUALITY_MAGIC);
	EXPECT_NE(item._iPrePower, IPL_INVALID);
	EXPECT_NE(item._iSufPower, IPL_INVALID);
	EXPECT_TRUE(item._iPrePower == kept || item._iSufPower == kept);
	ExpectRecreates(item);
	EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Augmentation, *MyPlayer, item));
}

TEST_F(Poe2CurrencyTest, RegalKeepsTheMagicAffixes)
{
	Item item = MakeWhere("Iron Cap", ITEM_QUALITY_MAGIC, [](const Item &candidate) { return candidate._iMagical == ITEM_QUALITY_MAGIC; });
	const item_effect_type prefix = item._iPrePower;
	const item_effect_type suffix = item._iSufPower;
	ASSERT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Regal, *MyPlayer, item));
	EXPECT_EQ(item._iMagical, ITEM_QUALITY_RARE);
	const auto has = [&item](item_effect_type power) {
		return power == IPL_INVALID || std::find(item._iRarePowers.begin(), item._iRarePowers.end(), power) != item._iRarePowers.end();
	};
	EXPECT_TRUE(has(prefix));
	EXPECT_TRUE(has(suffix));
	ExpectRecreates(item);
}

TEST_F(Poe2CurrencyTest, ExaltedAddsAnAffixAndKeepsTheRest)
{
	Item item = MakeWhere("Crude Bow", ITEM_QUALITY_RARE, [](const Item &candidate) {
		return candidate._iMagical == ITEM_QUALITY_RARE && RareAffixCount(candidate) == 3;
	});
	const auto before = item._iRarePowers;
	ASSERT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Exalted, *MyPlayer, item));
	EXPECT_EQ(RareAffixCount(item), 4);
	for (size_t i = 0; i < 3; i++)
		EXPECT_EQ(item._iRarePowers[i], before[i]);
	ExpectRecreates(item);

	while (RareAffixCount(item) < MaxExaltedAffixes) {
		if (!ApplyCurrencyOrb(CurrencyOrb::Exalted, *MyPlayer, item))
			break;
	}
	EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Exalted, *MyPlayer, item));
}

TEST_F(Poe2CurrencyTest, DivineKeepsTheAffixes)
{
	Item item = Make("Crude Bow", ITEM_QUALITY_RARE);
	const auto powers = item._iRarePowers;
	const int seed = item._iSeed;
	ASSERT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Divine, *MyPlayer, item));
	EXPECT_EQ(item._iRarePowers, powers);
	EXPECT_EQ(item._iSeed, seed);
	ExpectRecreates(item);
}

TEST_F(Poe2CurrencyTest, VaalCorruptsForGood)
{
	for (int seed = 1; seed <= 12; seed++) {
		Item item;
		ASSERT_TRUE(AdminGenerateItem(*MyPlayer, item, FindBase("Iron Cap"), ITEM_QUALITY_RARE, 30, seed));
		item._iIdentified = true;
		ASSERT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Vaal, *MyPlayer, item));
		EXPECT_NE(item.dwBuff & CF_POE2_CORRUPTED, 0U);
		ExpectRecreates(item);
		EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Chaos, *MyPlayer, item));
		EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Vaal, *MyPlayer, item));
	}
}

} // namespace
TEST_F(Poe2CurrencyTest, UsingUpTheLastOrbBeforeTheItemDestroysThatItem)
{
	// Another player, so nothing is sent over the network, which the tests don't start.
	LoadPlayerDataFiles();
	Players.resize(2);
	MyPlayer = &Players[0];
	Player &player = Players[1];
	player = {};
	player.setCharacterLevel(20);
	// A normal item a failed Orb of Chance crumbles.
	Item target;
	for (size_t base = 0; base < AllItemsList.size() && target.isEmpty(); base++) {
		if (AllItemsList[base].dropRate == 0 || IsNoneOf(AllItemsList[base].iClass, ICLASS_WEAPON, ICLASS_ARMOR))
			continue;
		for (int seed = 1; seed < 200 && target.isEmpty(); seed++) {
			Item item;
			if (!AdminGenerateItem(player, item, static_cast<_item_indexes>(base), ITEM_QUALITY_NORMAL, 20, seed) || item._iMagical != ITEM_QUALITY_NORMAL)
				continue;
			Item probe = item;
			if (ApplyCurrencyOrb(CurrencyOrb::Chance, player, probe) && CurrencyDestroyedItem)
				target = item;
		}
	}
	CurrencyDestroyedItem = false;
	ASSERT_FALSE(target.isEmpty());
	Item orb;
	ASSERT_TRUE(AdminGenerateItem(player, orb, FindBase("Orb of Chance"), ITEM_QUALITY_NORMAL, 1, 5));
	ASSERT_EQ(StackCount(orb), 1);
	const Item bystander = Make("Iron Cap", ITEM_QUALITY_MAGIC);
	// The bag: the last Orb of Chance, the item, then another item (placed by hand: the tests load no item graphics).
	const Item bag[] = { orb, target, bystander };
	for (int i = 0; i < 3; i++) {
		player.InvList[i] = bag[i];
		player.InvGrid[i] = static_cast<int8_t>(i + 1);
	}
	player._pNumInv = 3;
	player._pOilType = orb._iMiscId;
	HoldCurrencyForUse(player.InvList[0]);

	ASSERT_TRUE(DoOil(player, NUM_INVLOC + 1));
	ASSERT_EQ(player._pNumInv, 1);
	// Before the fix the orb's removal moved the bystander into the item's place and it was destroyed instead.
	EXPECT_EQ(player.InvList[0]._iSeed, bystander._iSeed);
	EXPECT_EQ(player.InvList[0].IDidx, bystander.IDidx);
}

} // namespace devilution
