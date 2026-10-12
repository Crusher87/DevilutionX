#include <cstdint>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "admin_menu.hpp"
#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/currency.hpp"
#include "spells.h"

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
		return item;
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
	for (const std::string_view name : { "Orb of Transmutation", "Orb of Alchemy", "Chaos Orb", "Orb of Scouring" }) {
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
	Item item = Make("Long Sword", ITEM_QUALITY_NORMAL);
	ASSERT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Transmutation, *MyPlayer, item));
	EXPECT_EQ(item._iMagical, ITEM_QUALITY_MAGIC);
	EXPECT_TRUE(item._iIdentified);
	ExpectRecreates(item);
	EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Transmutation, *MyPlayer, item));
}

TEST_F(Poe2CurrencyTest, AlchemyAndChaosMakeRares)
{
	Item item = Make("Helm", ITEM_QUALITY_MAGIC);
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
	Item item = Make("Short Bow", ITEM_QUALITY_RARE);
	ASSERT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Scouring, *MyPlayer, item));
	EXPECT_EQ(item._iMagical, ITEM_QUALITY_NORMAL);
	ExpectRecreates(item);
}

TEST_F(Poe2CurrencyTest, RejectsWrongItems)
{
	Item normal = Make("Long Sword", ITEM_QUALITY_NORMAL);
	EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Chaos, *MyPlayer, normal));
	EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Scouring, *MyPlayer, normal));

	Item unique;
	ASSERT_TRUE(AdminGenerateItem(*MyPlayer, unique, FindBase("Short Bow"), ITEM_QUALITY_UNIQUE, 30, 99));
	EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Alchemy, *MyPlayer, unique));
}

TEST_F(Poe2CurrencyTest, KeepsTheGameRngState)
{
	SetRndSeed(4242);
	const uint32_t before = GetLCGEngineState();
	Item item = Make("Long Sword", ITEM_QUALITY_NORMAL);
	ASSERT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Alchemy, *MyPlayer, item));
	EXPECT_EQ(GetLCGEngineState(), before);
}

} // namespace
} // namespace devilution
