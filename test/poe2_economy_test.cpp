#include <array>
#include <string_view>

#include <gtest/gtest.h>

#include "cursor.h"
#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "player.h"
#include "poe2/bases.hpp"
#include "poe2/economy.hpp"
#include "poe2/loot.hpp"
#include "poe2/stacking.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2EconomyTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		*MyPlayer = {};
		MyPlayer->poe2.clear();
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		gbIsPoe2 = true;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadSpellData();
		LoadPlayerDataFiles();
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		UnloadModArchives();
	}

	/** Shards go in the bag, and placing an item there needs the cursor graphics from the game's MPQ. */
	static void SetUpTestSuite()
	{
		LoadCoreArchives();
		LoadGameArchives();
		haveCursors_ = HaveMainData();
		if (haveCursors_)
			InitCursor();
	}

	static bool haveCursors_;
};

bool Poe2EconomyTest::haveCursors_ = false;

TEST_F(Poe2EconomyTest, GamblePricesFollowTheFormula)
{
	EXPECT_EQ(GamblePrice(GambleFamily::Sword, 1), 106);
	EXPECT_EQ(GamblePrice(GambleFamily::Sword, 80), 644);
	for (int level = 1; level < 80; level++) {
		EXPECT_LT(GamblePrice(GambleFamily::Helmet, level), GamblePrice(GambleFamily::Shield, level));
		EXPECT_LT(GamblePrice(GambleFamily::Shield, level), GamblePrice(GambleFamily::Bow, level));
		EXPECT_LT(GamblePrice(GambleFamily::Bow, level), GamblePrice(GambleFamily::Ring, level));
		EXPECT_LT(GamblePrice(GambleFamily::Ring, level), GamblePrice(GambleFamily::Ring, level + 1));
	}
}

TEST_F(Poe2EconomyTest, GambleRaritiesFollowTheWeights)
{
	SetRndSeed(42);
	std::array<int, 4> counts {};
	constexpr int Rolls = 20000;
	for (int i = 0; i < Rolls; i++)
		counts[RollGambleRarity()]++;
	EXPECT_NEAR(counts[ITEM_QUALITY_NORMAL] * 1000 / Rolls, 520, 25);
	EXPECT_NEAR(counts[ITEM_QUALITY_MAGIC] * 1000 / Rolls, 360, 25);
	EXPECT_NEAR(counts[ITEM_QUALITY_RARE] * 1000 / Rolls, 120, 15);
	EXPECT_EQ(counts[ITEM_QUALITY_UNIQUE], 0) << "No uniques from vendors or the gambler";
}

TEST_F(Poe2EconomyTest, VendorRaritiesFollowTheWeights)
{
	SetRndSeed(42);
	std::array<int, 4> counts {};
	constexpr int Rolls = 20000;
	for (int i = 0; i < Rolls; i++)
		counts[RollVendorRarity()]++;
	EXPECT_NEAR(counts[ITEM_QUALITY_NORMAL] * 100 / Rolls, 75, 3);
	EXPECT_NEAR(counts[ITEM_QUALITY_MAGIC] * 100 / Rolls, 21, 3);
	EXPECT_NEAR(counts[ITEM_QUALITY_RARE] * 100 / Rolls, 4, 2);
	EXPECT_EQ(counts[ITEM_QUALITY_UNIQUE], 0);
}

TEST_F(Poe2EconomyTest, GamblesGiveItemsOfTheFamily)
{
	SetRndSeed(7);
	for (size_t i = 0; i < GambleFamilyCount; i++) {
		const auto family = static_cast<GambleFamily>(i);
		for (int tries = 0; tries < 20; tries++) {
			Item item;
			ASSERT_TRUE(MakeGambleItem(*MyPlayer, item, family, 20)) << GambleFamilyName(family);
			EXPECT_TRUE(GambleFamilyHas(family, item.IDidx)) << GambleFamilyName(family) << ": " << item.getName().str();
			EXPECT_TRUE(item._iIdentified);
		}
	}
}

TEST_F(Poe2EconomyTest, GambleChargesInTheCampaignOnly)
{
	Player &player = *MyPlayer;
	EXPECT_EQ(player.poe2.economy.gambleCharges, GambleStartCharges);
	for (int i = 0; i < GambleStartCharges; i++) {
		EXPECT_TRUE(CanGamble(player));
		SpendGambleCharge(player);
	}
	EXPECT_FALSE(CanGamble(player));
	OnUniqueMonsterKilledForGamble(player);
	EXPECT_TRUE(CanGamble(player));
	for (int i = 0; i < 200; i++)
		OnUniqueMonsterKilledForGamble(player);
	EXPECT_EQ(player.poe2.economy.gambleCharges, MaxGambleCharges);

	player.poe2.economy.gambleCharges = 0;
	player.poe2.mapsCompleted = 1;
	EXPECT_FALSE(GambleUsesCharges(player));
	EXPECT_TRUE(CanGamble(player));
}

/** A hero who isn't MyPlayer, so nothing is sent over the network. */
Player &OtherHero()
{
	Players.resize(2);
	MyPlayer = &Players[0];
	Player &player = Players[1];
	player = {};
	player.poe2.clear();
	return player;
}

int CountInBag(const Player &player, item_misc_id miscId)
{
	int count = 0;
	for (int i = 0; i < player._pNumInv; i++) {
		if (player.InvList[i]._iMiscId == miscId)
			count += StackCount(player.InvList[i]);
	}
	return count;
}

TEST_F(Poe2EconomyTest, ShardsAreItemsAtTheEndOfTheItemTable)
{
	// Curtis 2026-10-29: shards are real items, like POE2's; their rows come after every older row, which saves keep.
	const std::array<ShardKind, 4> kinds { ShardKind::Transmutation, ShardKind::Regal, ShardKind::Chance, ShardKind::Artificer };
	const std::array<std::string_view, 4> names { "Transmutation Shard", "Regal Shard", "Chance Shard", "Artificer's Shard" };
	// The item table had 1005 rows before the shards; saves store the row.
	constexpr size_t FirstShardRow = 1005;
	for (size_t k = 0; k < kinds.size(); k++) {
		const item_misc_id miscId = ShardMiscId(kinds[k]);
		ASSERT_TRUE(ShardKindForMiscId(miscId));
		EXPECT_EQ(*ShardKindForMiscId(miscId), kinds[k]);
		const size_t row = FirstShardRow + k;
		ASSERT_LT(row, AllItemsList.size());
		EXPECT_EQ(AllItemsList[row].iMiscId, miscId) << names[k];
		EXPECT_EQ(AllItemsList[row].iName, names[k]);
		EXPECT_EQ(AllItemsList[row].dropRate, 0) << "only disenchanting and salvaging give shards";
		EXPECT_FALSE(AllItemsList[row].iUsable);
	}
	EXPECT_FALSE(ShardKindForMiscId(IMISC_ORB_CHANCE));
	EXPECT_EQ(ShardCurrency(ShardKind::Artificer), IMISC_ORB_ARTIFICER);
	EXPECT_EQ(ShardStackText(ShardKind::Chance), "A stack of 10 shards becomes an Orb of Chance");
	EXPECT_EQ(ShardStackText(ShardKind::Regal), "A stack of 10 shards becomes a Regal Orb");
}

TEST_F(Poe2EconomyTest, ShardsGoInTheBagAndTenMakeAnOrb)
{
	if (!haveCursors_)
		GTEST_SKIP() << "needs the cursor graphics from spawn.mpq or DIABDAT.MPQ";
	Player &player = OtherHero();
	EXPECT_EQ(GainShards(player, ShardKind::Regal, 9), 0);
	EXPECT_EQ(player._pNumInv, 1);
	EXPECT_EQ(ShardsInBag(player, ShardKind::Regal), 9);
	EXPECT_EQ(FullestShardStack(player, ShardKind::Regal), 9);
	EXPECT_EQ(player.poe2.economy.shards[static_cast<size_t>(ShardKind::Regal)], 0) << "no hidden count any more";

	// Two more: one fills the stack, which becomes a Regal Orb, and one starts a new stack.
	EXPECT_EQ(GainShards(player, ShardKind::Regal, 2), 1);
	EXPECT_EQ(CountInBag(player, IMISC_ORB_REGAL), 1);
	EXPECT_EQ(ShardsInBag(player, ShardKind::Regal), 1);

	// 25 at once: two orbs and a stack of five.
	EXPECT_EQ(GainShards(player, ShardKind::Chance, 25), 2);
	EXPECT_EQ(CountInBag(player, IMISC_ORB_CHANCE), 2);
	EXPECT_EQ(ShardsInBag(player, ShardKind::Chance), 5);

	// The fullest stack fills first.
	EXPECT_EQ(GainShards(player, ShardKind::Artificer, 3), 0);
	EXPECT_EQ(GainShards(player, ShardKind::Artificer, 8), 1);
	EXPECT_EQ(CountInBag(player, IMISC_ORB_ARTIFICER), 1);
	EXPECT_EQ(ShardsInBag(player, ShardKind::Artificer), 1);
}

TEST_F(Poe2EconomyTest, CountedShardsBecomeItemsOnce)
{
	if (!haveCursors_)
		GTEST_SKIP() << "needs the cursor graphics from spawn.mpq or DIABDAT.MPQ";
	// Heroes from before shards were items had them counted; they go in the bag and the count empties.
	Player &player = OtherHero();
	player.poe2.economy.shards[static_cast<size_t>(ShardKind::Chance)] = 3;
	player.poe2.economy.shards[static_cast<size_t>(ShardKind::Transmutation)] = 9;
	ConvertCountedShards(player);
	EXPECT_EQ(ShardsInBag(player, ShardKind::Chance), 3);
	EXPECT_EQ(ShardsInBag(player, ShardKind::Transmutation), 9);
	EXPECT_EQ(player.poe2.economy.shards[static_cast<size_t>(ShardKind::Chance)], 0);
	EXPECT_EQ(player.poe2.economy.shards[static_cast<size_t>(ShardKind::Transmutation)], 0);
	const int items = player._pNumInv;
	ConvertCountedShards(player);
	EXPECT_EQ(player._pNumInv, items) << "only once";
	EXPECT_EQ(ShardsInBag(player, ShardKind::Chance), 3);
}

TEST_F(Poe2EconomyTest, DisenchantAndSalvageYields)
{
	SetRndSeed(11);
	Item item;
	ASSERT_TRUE(MakeGambleItem(*MyPlayer, item, GambleFamily::Sword, 20));

	item._iMagical = ITEM_QUALITY_NORMAL;
	EXPECT_FALSE(GetDisenchantYield(item));
	item._iMagical = ITEM_QUALITY_MAGIC;
	ASSERT_TRUE(GetDisenchantYield(item));
	EXPECT_EQ(GetDisenchantYield(item)->kind, ShardKind::Transmutation);
	item._iMagical = ITEM_QUALITY_UNIQUE;
	EXPECT_EQ(GetDisenchantYield(item)->kind, ShardKind::Chance);

	SetItemQuality(item, 13);
	item._iPoe2Craft.sockets = 2;
	const std::optional<SalvageYield> yield = GetSalvageYield(item);
	ASSERT_TRUE(yield);
	EXPECT_EQ(yield->currency, IMISC_ORB_WHETSTONE);
	// One per 5% quality, rounded up.
	EXPECT_EQ(yield->full, (ItemQuality(item) + 4) / 5);
	EXPECT_EQ(yield->socketShards, 2);
	EXPECT_EQ(SalvageYieldText(SalvageYield { IMISC_ORB_WHETSTONE, 3, 2 }), "3 Blacksmith's Whetstones and 2 Artificer's Shards");
	EXPECT_EQ(SalvageYieldText(SalvageYield { IMISC_ORB_WHETSTONE, 1, 0 }), "1 Blacksmith's Whetstone");
	EXPECT_EQ(SalvageYieldText(SalvageYield { IMISC_ORB_WHETSTONE, 0, 0 }), "nothing");

	Item ring;
	ASSERT_TRUE(MakeGambleItem(*MyPlayer, ring, GambleFamily::Ring, 20));
	EXPECT_FALSE(GetSalvageYield(ring));
}

TEST_F(Poe2EconomyTest, AnvilSalvagesUniquesForChanceShards)
{
	if (!haveCursors_)
		GTEST_SKIP() << "needs the cursor graphics from spawn.mpq or DIABDAT.MPQ";
	// Curtis 2026-10-23: uniques go on the anvil for a Chance Shard each, plus what their quality and sockets give.
	SetRndSeed(11);
	Item sword;
	ASSERT_TRUE(MakeGambleItem(*MyPlayer, sword, GambleFamily::Sword, 20));
	SetItemQuality(sword, 0);
	sword._iPoe2Craft.sockets = 0;
	sword._iMagical = ITEM_QUALITY_NORMAL;
	EXPECT_FALSE(SalvageGivesSomething(sword)) << "a plain item stays off the anvil's list";
	sword._iMagical = ITEM_QUALITY_MAGIC;
	EXPECT_FALSE(SalvageGivesSomething(sword));

	// A unique with no quality or sockets is listed all the same.
	sword._iMagical = ITEM_QUALITY_UNIQUE;
	EXPECT_TRUE(SalvageGivesSomething(sword));
	std::optional<SalvageYield> yield = GetSalvageYield(sword);
	ASSERT_TRUE(yield);
	EXPECT_EQ(yield->full, 0);
	EXPECT_EQ(yield->socketShards, 0);
	EXPECT_EQ(yield->chanceShards, 1);
	EXPECT_EQ(yield->chanceShards, GetDisenchantYield(sword)->shards) << "the same as disenchanting at Adria";
	EXPECT_EQ(SalvageYieldText(*yield), "1 Chance Shard");

	// Quality and sockets give their usual share on top.
	SetItemQuality(sword, 13);
	sword._iPoe2Craft.sockets = 1;
	yield = GetSalvageYield(sword);
	ASSERT_TRUE(yield);
	EXPECT_EQ(yield->currency, IMISC_ORB_WHETSTONE);
	EXPECT_EQ(yield->full, (ItemQuality(sword) + 4) / 5);
	EXPECT_EQ(yield->socketShards, 1);
	EXPECT_EQ(yield->chanceShards, 1);
	EXPECT_EQ(SalvageYieldText(SalvageYield { IMISC_ORB_WHETSTONE, 3, 1, 1 }), "3 Blacksmith's Whetstones, 1 Artificer's Shard and 1 Chance Shard");
	EXPECT_EQ(SalvageYieldText(SalvageYield { IMISC_ORB_WHETSTONE, 0, 2, 1 }), "2 Artificer's Shards and 1 Chance Shard");

	// Unique jewellery, which has no quality currency, and unique flasks.
	Item ring;
	ASSERT_TRUE(MakeGambleItem(*MyPlayer, ring, GambleFamily::Ring, 20));
	ring._iMagical = ITEM_QUALITY_UNIQUE;
	ASSERT_TRUE(SalvageGivesSomething(ring));
	EXPECT_EQ(GetSalvageYield(ring)->chanceShards, 1);
	EXPECT_EQ(GetSalvageYield(ring)->full, 0);
	Item flask {};
	flask._itype = ItemType::Misc;
	flask._iMiscId = IMISC_HEAL;
	flask._iMagical = ITEM_QUALITY_UNIQUE;
	ASSERT_TRUE(SalvageGivesSomething(flask));
	EXPECT_EQ(GetSalvageYield(flask)->chanceShards, 1);

	// Salvaging one puts its shard in the bag; a stack of ten becomes an Orb of Chance (Curtis 2026-10-29: told he got a
	// Chance Shard from the Butcher's Cleaver, he couldn't find it).
	Player &player = OtherHero();
	sword._iMagical = ITEM_QUALITY_UNIQUE;
	SetItemQuality(sword, 0);
	sword._iPoe2Craft.sockets = 0;
	player.InvList[0] = sword;
	player.InvGrid[0] = 1;
	player._pNumInv = 1;
	ASSERT_TRUE(SalvageInvItem(player, 0));
	EXPECT_EQ(player._pNumInv, 1) << "the sword went, its shard came";
	EXPECT_EQ(player.InvList[0]._iMiscId, IMISC_SHARD_CHANCE);
	EXPECT_EQ(ShardsInBag(player, ShardKind::Chance), 1);
	EXPECT_EQ(ShardsPerOrb, 10);
	EXPECT_EQ(GainShards(player, ShardKind::Chance, 8), 0);
	EXPECT_EQ(FullestShardStack(player, ShardKind::Chance), 9);
	player.InvList[1] = sword;
	player.InvGrid[1] = 2;
	player._pNumInv = 2;
	ASSERT_TRUE(SalvageInvItem(player, 1));
	EXPECT_EQ(ShardCurrency(ShardKind::Chance), IMISC_ORB_CHANCE);
	EXPECT_EQ(CountInBag(player, IMISC_ORB_CHANCE), 1);
	EXPECT_EQ(ShardsInBag(player, ShardKind::Chance), 0);
}

} // namespace
} // namespace devilution
