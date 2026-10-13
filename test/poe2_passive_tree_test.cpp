#include <algorithm>
#include <array>
#include <vector>

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

	static PassiveNodeId FindNode(std::string_view name)
	{
		for (const PassiveNode &node : PassiveTree()) {
			if (node.name == name)
				return node.id;
		}
		ADD_FAILURE() << name;
		return 0;
	}

	/** @brief The Warrior's road north: Strength, Strength, then Lachdanan's Oath. */
	static std::array<PassiveNodeId, 3> WarriorPath()
	{
		const PassiveNodeId oath = FindNode("Lachdanan's Oath");
		const PassiveNodeId before = PassiveTree()[oath].links[1];
		const PassiveNodeId first = PassiveTree()[before].links[0];
		return { first, before, oath };
	}
};

TEST_F(Poe2PassiveTreeTest, TreeLoadsWithAStartForEveryClass)
{
	ASSERT_GE(PassiveTree().size(), 400U);
	ASSERT_LE(PassiveTree().size(), MaxPassiveNodes);
	for (const HeroClass heroClass : { HeroClass::Warrior, HeroClass::Rogue, HeroClass::Sorcerer, HeroClass::Monk, HeroClass::Bard, HeroClass::Barbarian }) {
		EXPECT_TRUE(PassiveStartNode(heroClass).has_value()) << static_cast<int>(heroClass);
	}
	for (const PassiveNode &node : PassiveTree()) {
		for (const PassiveNodeId link : node.links) {
			ASSERT_LT(link, PassiveTree().size());
			const auto &back = PassiveTree()[link].links;
			EXPECT_NE(std::find(back.begin(), back.end(), node.id), back.end()) << "link " << node.id << "-" << link << " is one way";
		}
	}
}

TEST_F(Poe2PassiveTreeTest, EveryStartReachesTheWholeTree)
{
	for (const HeroClass heroClass : { HeroClass::Warrior, HeroClass::Rogue, HeroClass::Sorcerer, HeroClass::Monk, HeroClass::Bard, HeroClass::Barbarian }) {
		std::vector<bool> seen(PassiveTree().size());
		std::vector<PassiveNodeId> open { *PassiveStartNode(heroClass) };
		seen[open.back()] = true;
		while (!open.empty()) {
			const PassiveNodeId id = open.back();
			open.pop_back();
			for (const PassiveNodeId link : PassiveTree()[id].links) {
				if (!seen[link]) {
					seen[link] = true;
					open.push_back(link);
				}
			}
		}
		for (const PassiveNode &node : PassiveTree()) {
			if (!IsAscendancyNode(node))
				EXPECT_TRUE(seen[node.id]) << static_cast<int>(heroClass) << " can't reach " << node.id;
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
	const PassiveNodeId start = *PassiveStartNode(HeroClass::Warrior);
	EXPECT_TRUE(IsPassiveAllocated(player, start));

	const auto path = WarriorPath();
	EXPECT_FALSE(CanAllocatePassive(player, path[2]));
	EXPECT_FALSE(CanAllocatePassive(player, FindNode("Arkaine's Valor")));

	ASSERT_TRUE(AllocatePassive(player, path[0]));
	EXPECT_EQ(PassivePointsLeft(player), 8);
	EXPECT_FALSE(CanAllocatePassive(player, path[0]));
}

TEST_F(Poe2PassiveTreeTest, RefundsKeepTheTreeConnected)
{
	Player &player = *MyPlayer;
	const auto path = WarriorPath();
	for (const PassiveNodeId node : path)
		ASSERT_TRUE(AllocatePassive(player, node)) << node;
	EXPECT_FALSE(CanRefundPassive(player, path[1]));
	EXPECT_TRUE(RefundPassive(player, path[2]));
	EXPECT_TRUE(RefundPassive(player, path[1]));
	EXPECT_EQ(PassivePointsSpent(player), 1);
	ResetPassives(player);
	EXPECT_EQ(PassivePointsSpent(player), 0);
}

TEST_F(Poe2PassiveTreeTest, AllocatedStatsApplyToTheCharacter)
{
	Player &player = *MyPlayer;
	CalcPlrInv(player, false);
	const int strength = player._pStrength;
	const int maxLife = player._pMaxHP.whole();

	// +5 Strength, +5 Strength, then Lachdanan's Oath: 10% life and 15% armor.
	for (const PassiveNodeId node : WarriorPath())
		ASSERT_TRUE(AllocatePassive(player, node));

	EXPECT_EQ(player._pStrength, strength + 10);
	EXPECT_GT(player._pMaxHP.whole(), maxLife);
	const PassiveTotals totals = GetPassiveTotals(player);
	EXPECT_EQ(totals[static_cast<size_t>(PassiveStat::LifePercent)], 10);
	EXPECT_EQ(totals[static_cast<size_t>(PassiveStat::ArmorPercent)], 15);
}

TEST_F(Poe2PassiveTreeTest, ShrinesGiveAndTake)
{
	const PassiveNode &shrine = PassiveTree()[FindNode("Gloomy Shrine")];
	EXPECT_EQ(shrine.kind, PassiveKind::Shrine);
	bool hasDrawback = false;
	for (const auto &[stat, value] : shrine.stats) {
		if (value < 0)
			hasDrawback = true;
	}
	EXPECT_TRUE(hasDrawback);
	EXPECT_EQ(PassiveStatText(PassiveStat::Damage, -10), "10% reduced Damage");
	EXPECT_EQ(PassiveStatText(PassiveStat::Damage, 25), "25% increased Damage");
}

TEST_F(Poe2PassiveTreeTest, EveryClassHasTwoAscendancies)
{
	for (const HeroClass heroClass : { HeroClass::Warrior, HeroClass::Rogue, HeroClass::Sorcerer, HeroClass::Monk, HeroClass::Bard, HeroClass::Barbarian }) {
		int roots = 0;
		for (const PassiveNode &node : PassiveTree()) {
			if (node.kind == PassiveKind::Ascendancy && node.startClass == heroClass)
				roots++;
		}
		EXPECT_EQ(roots, 2) << static_cast<int>(heroClass);
	}
}

TEST_F(Poe2PassiveTreeTest, AscendancyUnlocksAtLevel15)
{
	Player &player = *MyPlayer;
	const PassiveNodeId knight = FindNode("Knight of Westmarch");
	const PassiveNodeId warlord = FindNode("Blood Warlord");
	EXPECT_EQ(AscendancyPointsTotal(player), 0);
	EXPECT_FALSE(CanAllocatePassive(player, knight));

	player.setCharacterLevel(15);
	EXPECT_EQ(AscendancyPointsTotal(player), 2);
	EXPECT_FALSE(CanAllocatePassive(player, FindNode("Sister of the Sightless Eye"))); // another class's
	ASSERT_TRUE(AllocatePassive(player, knight));
	EXPECT_EQ(ChosenAscendancy(player), knight);
	EXPECT_FALSE(CanAllocatePassive(player, warlord)); // only one ascendancy
	EXPECT_EQ(PassivePointsSpent(player), 0);          // choosing is free

	const PassiveNodeId first = PassiveTree()[knight].links[0];
	ASSERT_TRUE(AllocatePassive(player, first));
	const PassiveNodeId second = PassiveTree()[first].links[1];
	ASSERT_TRUE(AllocatePassive(player, second));
	EXPECT_EQ(AscendancyPointsLeft(player), 0);
	EXPECT_EQ(PassivePointsLeft(player), 14);
	EXPECT_FALSE(CanRefundPassive(player, knight));
	EXPECT_FALSE(CanRefundPassive(player, first));
	EXPECT_TRUE(RefundPassive(player, second));

	player.setCharacterLevel(45);
	EXPECT_EQ(AscendancyPointsTotal(player), MaxAscendancyPoints);
}

TEST_F(Poe2PassiveTreeTest, MultiplayerIgnoresPassives)
{
	Player &player = *MyPlayer;
	const auto path = WarriorPath();
	ASSERT_TRUE(AllocatePassive(player, path[0]));
	gbIsMultiplayer = true;
	EXPECT_EQ(GetPassiveTotals(player)[static_cast<size_t>(PassiveStat::Strength)], 0);
	EXPECT_FALSE(CanAllocatePassive(player, path[1]));
}

} // namespace
} // namespace devilution
