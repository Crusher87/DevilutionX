#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/regen.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2RegenTest : public ::testing::Test {
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

	static void RunSeconds(Player &player, int seconds)
	{
		for (int i = 0; i < seconds * 20; i++)
			ApplyRegen(player);
	}
};

TEST_F(Poe2RegenTest, ManaRegeneratesFourPercentPerSecond)
{
	Player &player = *MyPlayer;
	player._pMaxMana = Fixed26_6::fromInt(100);
	player._pMaxManaBase = Fixed26_6::fromInt(100);
	player._pMana = Fixed26_6::fromInt(0);
	player._pManaBase = Fixed26_6::fromInt(0);

	RunSeconds(player, 5);
	EXPECT_EQ(player._pMana.whole(), 20);
	EXPECT_EQ(player._pManaBase.whole(), 20);

	RunSeconds(player, 60);
	EXPECT_EQ(player._pMana, player._pMaxMana);
}

TEST_F(Poe2RegenTest, IncreasedManaRegenScalesTheRate)
{
	Player &player = *MyPlayer;
	player._pMaxMana = Fixed26_6::fromInt(100);
	player._pMana = Fixed26_6::fromInt(0);
	player.poe2.manaRegenBonus = 50;

	RunSeconds(player, 5);
	EXPECT_EQ(player._pMana.whole(), 30);
}

TEST_F(Poe2RegenTest, LifeOnlyRegeneratesFromGear)
{
	Player &player = *MyPlayer;
	player._pMaxHP = Fixed26_6::fromInt(100);
	player._pHitPoints = Fixed26_6::fromInt(50);
	player._pHPBase = Fixed26_6::fromInt(50);

	RunSeconds(player, 5);
	EXPECT_EQ(player._pHitPoints.whole(), 50);

	player.poe2.lifeRegen = 3;
	RunSeconds(player, 5);
	EXPECT_EQ(player._pHitPoints.whole(), 65);
	EXPECT_EQ(player._pHPBase.whole(), 65);

	RunSeconds(player, 60);
	EXPECT_EQ(player._pHitPoints, player._pMaxHP);
}

TEST_F(Poe2RegenTest, NothingRegeneratesOutsidePoe2OrWhenDead)
{
	Player &player = *MyPlayer;
	player._pMaxMana = Fixed26_6::fromInt(100);
	player._pMana = Fixed26_6::fromInt(0);
	player.poe2.lifeRegen = 10;
	player._pMaxHP = Fixed26_6::fromInt(100);
	player._pHitPoints = Fixed26_6::fromInt(0);

	leveltype = DTYPE_CATHEDRAL;
	RunSeconds(player, 5);
	EXPECT_EQ(player._pHitPoints.whole(), 0);
	leveltype = DTYPE_TOWN;

	player._pHitPoints = Fixed26_6::fromInt(50);
	gbIsPoe2 = false;
	RunSeconds(player, 5);
	EXPECT_EQ(player._pMana.whole(), 0);
	EXPECT_EQ(player._pHitPoints.whole(), 50);
}

TEST_F(Poe2RegenTest, RegenAffixesRollAndShowOnItems)
{
	int lifeAffixes = 0;
	int manaAffixes = 0;
	for (const PLStruct &suffix : ItemSuffixes) {
		if (suffix.power.type == IPL_LIFEREGEN) lifeAffixes++;
		if (suffix.power.type == IPL_MANAREGEN) manaAffixes++;
	}
	EXPECT_EQ(lifeAffixes, 4);
	EXPECT_EQ(manaAffixes, 3);

	Item ring {};
	ring._iPLLifeRegen = 5;
	ring._iPLManaRegen = 30;
	EXPECT_EQ(PrintItemPower(IPL_LIFEREGEN, ring).str(), "regenerate 5 life per second");
	EXPECT_EQ(PrintItemPower(IPL_MANAREGEN, ring).str(), "30% increased mana regeneration");
}

} // namespace
} // namespace devilution
