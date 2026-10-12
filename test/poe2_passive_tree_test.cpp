#include <algorithm>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/passive_tree.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2PassiveTreeTest : public ::testing::Test {
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
		LoadPassiveTreeData();

		Player &player = *MyPlayer;
		player._pClass = HeroClass::Warrior;
		player.setCharacterLevel(10);
		player._pBaseStr = player._pStrength = 30;
		player._pBaseMag = player._pMagic = 10;
		player._pBaseDex = player._pDexterity = 20;
		player._pBaseVit = player._pVitality = 25;
		player._pMaxHPBase = player._pHPBase = Fixed26_6::fromInt(100);
		player._pMaxManaBase = player._pManaBase = Fixed26_6::fromInt(20);
		// Matches the unarmed light armor graphic so recalculating stats loads no art.
		player._pgfxnum = 0;
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		gbIsMultiplayer = false;
		LoadPassiveTreeData();
		UnloadModArchives();
	}

	static uint8_t FindNode(std::string_view name)
	{
		for (const PassiveNode &node : PassiveTree()) {
			if (node.name == name)
				return node.id;
		}
		ADD_FAILURE() << name;
		return 0;
	}
};

TEST_F(Poe2PassiveTreeTest, TreeLoadsWithAStartForEveryClass)
{
	ASSERT_GE(PassiveTree().size(), 50U);
	for (const HeroClass heroClass : { HeroClass::Warrior, HeroClass::Rogue, HeroClass::Sorcerer, HeroClass::Monk, HeroClass::Bard, HeroClass::Barbarian }) {
		EXPECT_TRUE(PassiveStartNode(heroClass).has_value()) << static_cast<int>(heroClass);
	}
	for (const PassiveNode &node : PassiveTree()) {
		for (const uint8_t link : node.links) {
			ASSERT_LT(link, PassiveTree().size());
			const auto &back = PassiveTree()[link].links;
			EXPECT_NE(std::find(back.begin(), back.end(), node.id), back.end()) << "link " << int(node.id) << "-" << int(link) << " is one way";
		}
	}
}

TEST_F(Poe2PassiveTreeTest, OnePointPerLevelAfterTheFirst)
{
	EXPECT_EQ(PassivePointsTotal(*MyPlayer), 9);
	EXPECT_EQ(PassivePointsLeft(*MyPlayer), 9);
}

TEST_F(Poe2PassiveTreeTest, NodesMustLinkToTheTree)
{
	Player &player = *MyPlayer;
	const uint8_t start = *PassiveStartNode(HeroClass::Warrior);
	EXPECT_TRUE(IsPassiveAllocated(player, start));

	const uint8_t juggernaut = FindNode("Juggernaut");
	EXPECT_FALSE(CanAllocatePassive(player, juggernaut));

	const uint8_t first = PassiveTree()[start].links[0];
	ASSERT_TRUE(AllocatePassive(player, first));
	EXPECT_EQ(PassivePointsLeft(player), 8);
	EXPECT_FALSE(CanAllocatePassive(player, first));
}

TEST_F(Poe2PassiveTreeTest, RefundsKeepTheTreeConnected)
{
	Player &player = *MyPlayer;
	// Warrior path: 8 -> 9 -> 10 -> Juggernaut.
	for (const uint8_t node : { 8, 9, 10, 11 })
		ASSERT_TRUE(AllocatePassive(player, node)) << int(node);
	EXPECT_FALSE(CanRefundPassive(player, 9));
	EXPECT_TRUE(RefundPassive(player, 11));
	EXPECT_TRUE(RefundPassive(player, 10));
	EXPECT_EQ(PassivePointsSpent(player), 2);
	ResetPassives(player);
	EXPECT_EQ(PassivePointsSpent(player), 0);
}

TEST_F(Poe2PassiveTreeTest, AllocatedStatsApplyToTheCharacter)
{
	Player &player = *MyPlayer;
	CalcPlrInv(player, false);
	const int strength = player._pStrength;
	const int maxLife = player._pMaxHP.whole();

	// 8: +8 Strength, 9: 4% life, 10: 8% armor, 11 Juggernaut: 12% life and 25% armor.
	for (const uint8_t node : { 8, 9, 10, 11 })
		ASSERT_TRUE(AllocatePassive(player, node));

	EXPECT_EQ(player._pStrength, strength + 8);
	EXPECT_GT(player._pMaxHP.whole(), maxLife);
	const PassiveTotals totals = GetPassiveTotals(player);
	EXPECT_EQ(totals[static_cast<size_t>(PassiveStat::LifePercent)], 16);
	EXPECT_EQ(totals[static_cast<size_t>(PassiveStat::ArmorPercent)], 33);
}

TEST_F(Poe2PassiveTreeTest, MultiplayerIgnoresPassives)
{
	Player &player = *MyPlayer;
	ASSERT_TRUE(AllocatePassive(player, 8));
	gbIsMultiplayer = true;
	EXPECT_EQ(GetPassiveTotals(player)[static_cast<size_t>(PassiveStat::Strength)], 0);
	EXPECT_FALSE(CanAllocatePassive(player, 9));
}

} // namespace
} // namespace devilution
