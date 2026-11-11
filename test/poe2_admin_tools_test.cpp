#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include "admin_menu.hpp"
#include "controls/wasd_movement.hpp"
#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "spells.h"

namespace devilution {
namespace {

class Poe2AdminToolsTest : public ::testing::Test {
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
		LoadModArchives({ { "hf" } });
		LoadItemData();
		LoadSpellData();
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
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

TEST_F(Poe2AdminToolsTest, SpawnsEachQualityAndRecreatesFromSeed)
{
	for (const std::string_view baseName : { "Long Sword", "Short Bow", "Ring", "Helm" }) {
		const _item_indexes base = FindBase(baseName);
		ASSERT_NE(base, IDI_GOLD) << baseName;
		for (const item_quality quality : { ITEM_QUALITY_NORMAL, ITEM_QUALITY_MAGIC, ITEM_QUALITY_RARE }) {
			if (quality == ITEM_QUALITY_NORMAL && baseName == "Ring")
				continue; // Jewelry is always magic.
			Item item;
			ASSERT_TRUE(AdminGenerateItem(*MyPlayer, item, base, quality, 30, 1234)) << baseName << " quality " << quality;
			EXPECT_EQ(item.IDidx, base);
			EXPECT_EQ(item._iMagical, quality);

			// Spawned items must survive saving and multiplayer sync, which rebuild them from the seed.
			Item recreated;
			RecreateItem(*MyPlayer, recreated, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff);
			EXPECT_EQ(recreated._iMagical, item._iMagical) << baseName;
			EXPECT_EQ(std::string(recreated._iIName), std::string(item._iIName));
			EXPECT_EQ(recreated._iRarePowers, item._iRarePowers);
		}
	}
}

TEST_F(Poe2AdminToolsTest, SpawnsUniques)
{
	Item item;
	ASSERT_TRUE(AdminGenerateItem(*MyPlayer, item, FindBase("Short Bow"), ITEM_QUALITY_UNIQUE, 30, 99));
	EXPECT_EQ(item._iMagical, ITEM_QUALITY_UNIQUE);
	Item recreated;
	RecreateItem(*MyPlayer, recreated, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff);
	EXPECT_EQ(std::string(recreated._iIName), std::string(item._iIName));
}

TEST_F(Poe2AdminToolsTest, KeepsTheGameRandomState)
{
	SetRndSeed(4242);
	Item item;
	AdminGenerateItem(*MyPlayer, item, FindBase("Long Sword"), ITEM_QUALITY_RARE, 30, 7);
	EXPECT_EQ(GetLCGEngineState(), 4242U);
}

TEST(Poe2WasdMovementTest, KeysMapToScreenDirections)
{
	SetWasdMovementEnabled(true);
	EXPECT_TRUE(WasdMovementKeyPressed('w'));
	EXPECT_TRUE(WasdMovementKeyPressed('d'));
	AxisDirection dir = GetWasdMovementDirection();
	EXPECT_EQ(dir.y, AxisDirectionY_UP);
	EXPECT_EQ(dir.x, AxisDirectionX_RIGHT);

	// Opposite keys cancel out.
	EXPECT_TRUE(WasdMovementKeyPressed('s'));
	dir = GetWasdMovementDirection();
	EXPECT_EQ(dir.y, AxisDirectionY_NONE);

	EXPECT_TRUE(WasdMovementKeyReleased('w'));
	EXPECT_TRUE(WasdMovementKeyReleased('s'));
	EXPECT_TRUE(WasdMovementKeyReleased('d'));
	dir = GetWasdMovementDirection();
	EXPECT_EQ(dir.x, AxisDirectionX_NONE);
	EXPECT_EQ(dir.y, AxisDirectionY_NONE);

	EXPECT_FALSE(WasdMovementKeyPressed('q'));
}

TEST(Poe2WasdMovementTest, ClickToMoveLeavesKeysAlone)
{
	SetWasdMovementEnabled(false);
	EXPECT_FALSE(WasdMovementKeyPressed('w'));
	const AxisDirection dir = GetWasdMovementDirection();
	EXPECT_EQ(dir.y, AxisDirectionY_NONE);
	SetWasdMovementEnabled(true);
}

} // namespace
} // namespace devilution
