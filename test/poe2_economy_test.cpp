#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "player.h"
#include "poe2/bases.hpp"
#include "poe2/economy.hpp"
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
};

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
			EXPECT_TRUE(GambleFamilyHas(family, item._itype)) << GambleFamilyName(family) << ": " << item.getName().str();
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

TEST_F(Poe2EconomyTest, ShardsMakeCurrencyAtTen)
{
	Player &player = *MyPlayer;
	EXPECT_EQ(AddShards(player, ShardKind::Regal, 9), 0);
	EXPECT_EQ(AddShards(player, ShardKind::Regal, 2), 1);
	EXPECT_EQ(player.poe2.economy.shards[static_cast<size_t>(ShardKind::Regal)], 1);
	EXPECT_EQ(AddShards(player, ShardKind::Chance, 25), 2);
	EXPECT_EQ(ShardCurrency(ShardKind::Artificer), IMISC_ORB_ARTIFICER);
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
	EXPECT_EQ(yield->full, ItemQuality(item) / 5);
	EXPECT_EQ(yield->extraPercent, (ItemQuality(item) % 5) * 20);
	EXPECT_EQ(yield->socketShards, 2);

	Item ring;
	ASSERT_TRUE(MakeGambleItem(*MyPlayer, ring, GambleFamily::Ring, 20));
	EXPECT_FALSE(GetSalvageYield(ring));
}

} // namespace
} // namespace devilution
