#include <string_view>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/crafting_items.hpp"
#include "poe2/economy_state.hpp"
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

TEST_F(Poe2StackingTest, BoughtAndFoundIdentifyScrollsStack)
{
	// The shop sells one Scroll of Identify base and the dungeon drops another; they are the same scroll.
	std::vector<size_t> bases;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (AllItemsList[i].iMiscId == IMISC_SCROLL && AllItemsList[i].iSpell == SpellID::Identify)
			bases.push_back(i);
	}
	ASSERT_GE(bases.size(), 2U);
	Item bought;
	InitializeItem(bought, static_cast<_item_indexes>(bases[0]));
	Item found;
	InitializeItem(found, static_cast<_item_indexes>(bases[1]));
	EXPECT_TRUE(CanStackWith(bought, found));
	EXPECT_TRUE(MergeStack(bought, found));
	EXPECT_EQ(StackCount(bought), 2);
}

TEST_F(Poe2StackingTest, SplitAndUse)
{
	Item stack = Make("Chaos Orb");
	SetStackCount(stack, 7);
	const std::optional<Item> half = SplitStack(stack);
	ASSERT_TRUE(half.has_value());
	EXPECT_EQ(StackCount(*half), 3);
	EXPECT_EQ(StackCount(stack), 4);
	EXPECT_NE(half->_iSeed, stack._iSeed);

	EXPECT_TRUE(TakeOneFromStack(stack));
	EXPECT_EQ(StackCount(stack), 3);
	Item single = Make("Chaos Orb");
	EXPECT_FALSE(TakeOneFromStack(single));
	EXPECT_FALSE(SplitStack(single).has_value());
}

TEST_F(Poe2StackingTest, SplitTakesTheChosenCount)
{
	Item stack = Make("Chaos Orb");
	SetStackCount(stack, 10);
	const std::optional<Item> part = SplitStack(stack, 7);
	ASSERT_TRUE(part.has_value());
	EXPECT_EQ(StackCount(*part), 7);
	EXPECT_EQ(StackCount(stack), 3);
	EXPECT_NE(part->_iSeed, stack._iSeed);
	EXPECT_FALSE(SplitStack(stack, 3).has_value()) << "At least one stays behind";
	EXPECT_FALSE(SplitStack(stack, 0).has_value());
	EXPECT_EQ(StackCount(stack), 3);
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

TEST_F(Poe2StackingTest, TenShardsInAStackBecomeTheirOrb)
{
	// Curtis 2026-10-29: shards are items in the bag, like POE2's; a stack of ten becomes the orb in its place.
	Item stack = Make("Chance Shard");
	ASSERT_TRUE(IsShard(stack));
	EXPECT_TRUE(IsStackable(stack));
	EXPECT_EQ(StackMax(stack), ShardsPerOrb);
	EXPECT_EQ(stack._iMaxCharges, ShardsPerOrb);
	EXPECT_FALSE(IsShard(Make("Orb of Chance")));
	EXPECT_EQ(StackMax(Make("Orb of Chance")), CurrencyStackMax);
	EXPECT_FALSE(CanStackWith(stack, Make("Regal Shard"))) << "each kind stacks on its own";

	SetStackCount(stack, 6);
	stack.position = { 3, 2 };
	const uint32_t seed = stack._iSeed;
	Item more = Make("Chance Shard");
	more._iSeed = seed + 1;
	SetStackCount(more, 3);
	EXPECT_TRUE(MergeStack(stack, more));
	EXPECT_EQ(StackCount(stack), 9);
	EXPECT_TRUE(IsShard(stack)) << "nine is still shards";

	more = Make("Chance Shard");
	SetStackCount(more, 5);
	EXPECT_FALSE(MergeStack(stack, more)) << "one fills the stack, four are left";
	EXPECT_EQ(StackCount(more), 4);
	EXPECT_FALSE(IsShard(stack));
	EXPECT_EQ(stack._iMiscId, IMISC_ORB_CHANCE);
	EXPECT_EQ(StackCount(stack), 1);
	EXPECT_EQ(stack._iSeed, seed);
	EXPECT_EQ(stack.position, Point(3, 2)) << "the orb takes the shards' place";
	EXPECT_TRUE(stack._iIdentified);
	EXPECT_EQ(stack._iCreateInfo, 0) << "rebuilt from its base and seed like a starting item";
	EXPECT_FALSE(CanStackWith(stack, more));

	// Fewer than ten never combine; a stack that somehow holds ten does.
	Item nine = Make("Artificer's Shard");
	SetStackCount(nine, 9);
	EXPECT_FALSE(CombineShardStack(nine));
	nine._iCharges = ShardsPerOrb;
	EXPECT_TRUE(CombineShardStack(nine));
	EXPECT_EQ(nine._iMiscId, IMISC_ORB_ARTIFICER);
}

TEST_F(Poe2StackingTest, PickedUpShardsFillTheStackIntoAnOrb)
{
	Player &player = *MyPlayer;
	player.InvList[0] = Make("Regal Shard");
	SetStackCount(player.InvList[0], 9);
	player._pNumInv = 1;
	ASSERT_TRUE(MergeIntoInventoryStacks(player, Make("Regal Shard"), false));
	EXPECT_EQ(player._pNumInv, 1);
	EXPECT_EQ(player.InvList[0]._iMiscId, IMISC_ORB_REGAL);
	// The orb is no shard stack, so the next shard needs a cell of its own.
	EXPECT_FALSE(InventoryStacksHaveRoom(player, Make("Regal Shard")));
}

} // namespace
} // namespace devilution
