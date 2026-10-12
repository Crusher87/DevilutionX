#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/flasks.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2FlasksTest : public ::testing::Test {
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

TEST_F(Poe2FlasksTest, PotionsInTheFirstFourSlotsAreFlasks)
{
	Player &player = *MyPlayer;
	InitializeItem(player.SpdList[0], IDI_HEAL);
	InitializeItem(player.SpdList[4], IDI_HEAL);
	InitializeItem(player.SpdList[1], IDI_BOOK1);
	EXPECT_TRUE(IsFlaskSlot(player, 0));
	EXPECT_FALSE(IsFlaskSlot(player, 1)) << "only potions are flasks";
	EXPECT_FALSE(IsFlaskSlot(player, 2)) << "empty slot";
	EXPECT_FALSE(IsFlaskSlot(player, 4)) << "slots 5-8 keep normal potions";

	gbIsPoe2 = false;
	EXPECT_FALSE(IsFlaskSlot(player, 0));
}

TEST_F(Poe2FlasksTest, DrinkingUsesChargesNotThePotion)
{
	Player &player = *MyPlayer;
	InitializeItem(player.SpdList[0], IDI_MANA);
	ASSERT_EQ(player.poe2.flaskCharges[0], MaxFlaskCharges);

	for (int use = 0; use < MaxFlaskCharges / FlaskChargesPerUse; use++)
		ASSERT_TRUE(UseFlask(player, 0)) << "use " << use;
	EXPECT_EQ(player.poe2.flaskCharges[0], 0);
	EXPECT_FALSE(player.SpdList[0].isEmpty());
	EXPECT_FALSE(CanUseFlask(player, 0));
	EXPECT_FALSE(UseFlask(player, 0));
}

TEST_F(Poe2FlasksTest, KillsRefillCharges)
{
	Player &player = *MyPlayer;
	player.poe2.flaskCharges.fill(0);
	GainFlaskCharges(player, 4);
	EXPECT_EQ(player.poe2.flaskCharges[3], 4);
	GainFlaskCharges(player, 200);
	EXPECT_EQ(player.poe2.flaskCharges[2], MaxFlaskCharges);

	player.poe2.flaskCharges.fill(0);
	RefillFlasks(player);
	EXPECT_EQ(player.poe2.flaskCharges[1], MaxFlaskCharges);
}

} // namespace
} // namespace devilution
