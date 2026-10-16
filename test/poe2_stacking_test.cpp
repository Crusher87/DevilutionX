#include <string_view>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/crafting_items.hpp"
#include "poe2/stacking.hpp"
#include "spells.h"

namespace devilution {
namespace {

class Poe2StackingTest : public ::testing::Test {
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

	static Item Make(std::string_view name)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == name) {
				Item item;
				InitializeItem(item, static_cast<_item_indexes>(i));
				item._iSeed = static_cast<uint32_t>(i);
				return item;
			}
		}
		ADD_FAILURE() << "no " << name;
		return {};
	}
};

TEST_F(Poe2StackingTest, CurrencyStacksAndOthersDont)
{
	const Item orb = Make("Chaos Orb");
	EXPECT_TRUE(IsStackable(orb));
	EXPECT_EQ(StackCount(orb), 1);
	EXPECT_EQ(orb._iMaxCharges, CurrencyStackMax);

	const Item potion = Make("Potion of Healing");
	EXPECT_FALSE(IsStackable(potion));
	EXPECT_EQ(StackCount(potion), 1);
}

TEST_F(Poe2StackingTest, OldSavesWithoutACountHoldOne)
{
	Item orb = Make("Chaos Orb");
	orb._iCharges = 0;
	orb._iMaxCharges = 0;
	EXPECT_EQ(StackCount(orb), 1);
	Item other = Make("Chaos Orb");
	EXPECT_TRUE(MergeStack(orb, other));
	EXPECT_EQ(StackCount(orb), 2);
}

TEST_F(Poe2StackingTest, MergeFillsUpToTheLimit)
{
	Item stack = Make("Chaos Orb");
	SetStackCount(stack, CurrencyStackMax - 5);
	Item more = Make("Chaos Orb");
	SetStackCount(more, 8);
	EXPECT_FALSE(MergeStack(stack, more));
	EXPECT_EQ(StackCount(stack), CurrencyStackMax);
	EXPECT_EQ(StackCount(more), 3);
	EXPECT_EQ(StackRoom(stack), 0);
}

TEST_F(Poe2StackingTest, DifferentKindsDontStack)
{
	const Item chaos = Make("Chaos Orb");
	const Item exalted = Make("Exalted Orb");
	EXPECT_FALSE(CanStackWith(chaos, exalted));

	Item omenA = Make("Omen");
	Item omenB = Make("Omen");
	SetCraftingItemVariant(omenA, 1);
	SetCraftingItemVariant(omenB, 2);
	EXPECT_FALSE(CanStackWith(omenA, omenB));
	SetCraftingItemVariant(omenB, 1);
	EXPECT_TRUE(CanStackWith(omenA, omenB));
}

TEST_F(Poe2StackingTest, SplitAndUse)
{
	Item stack = Make("Chaos Orb");
	SetStackCount(stack, 7);
	const std::optional<Item> half = SplitStack(stack);
	ASSERT_TRUE(half.has_value());
	EXPECT_EQ(StackCount(*half), 3);
	EXPECT_EQ(StackCount(stack), 4);

	EXPECT_TRUE(TakeOneFromStack(stack));
	EXPECT_EQ(StackCount(stack), 3);
	Item single = Make("Chaos Orb");
	EXPECT_FALSE(TakeOneFromStack(single));
	EXPECT_FALSE(SplitStack(single).has_value());
}

TEST_F(Poe2StackingTest, PickupJoinsTheInventoryStack)
{
	Player &player = *MyPlayer;
	player.InvList[0] = Make("Chaos Orb");
	player._pNumInv = 1;
	for (int i = 0; i < 5; i++) {
		ASSERT_TRUE(MergeIntoInventoryStacks(player, Make("Chaos Orb"), false));
	}
	EXPECT_EQ(player._pNumInv, 1);
	EXPECT_EQ(StackCount(player.InvList[0]), 6);
	EXPECT_FALSE(MergeIntoInventoryStacks(player, Make("Exalted Orb"), false));

	// A full stack makes the next one need a cell of its own.
	SetStackCount(player.InvList[0], CurrencyStackMax);
	EXPECT_FALSE(InventoryStacksHaveRoom(player, Make("Chaos Orb")));
	EXPECT_FALSE(MergeIntoInventoryStacks(player, Make("Chaos Orb"), false));
}

} // namespace
} // namespace devilution
