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

	/** @brief The shortest road from the Warrior's start to a node, not counting the start. */
	static std::vector<PassiveNodeId> WarriorPathTo(std::string_view name)
	{
		const PassiveNodeId start = *PassiveStartNode(HeroClass::Warrior);
		const PassiveNodeId goal = FindNode(name);
		std::vector<int> previous(PassiveTree().size(), -1);
		std::vector<PassiveNodeId> open { start };
		previous[start] = start;
		for (size_t i = 0; i < open.size(); i++) {
			for (const PassiveNodeId link : PassiveTree()[open[i]].links) {
				if (previous[link] == -1 && !IsAscendancyNode(PassiveTree()[link])) {
					previous[link] = open[i];
					open.push_back(link);
				}
			}
		}
		std::vector<PassiveNodeId> path;
		for (PassiveNodeId node = goal; node != start; node = static_cast<PassiveNodeId>(previous[node]))
			path.insert(path.begin(), node);
		return path;
	}

	/** @brief The Warrior's sword pommel: two "+5 to any Attribute" nodes, a damage node, then King Leoric's Might. */
	static std::vector<PassiveNodeId> WarriorPath()
	{
		return WarriorPathTo("King Leoric's Might");
	}

	static int Total(const Player &player, PassiveStat stat)
	{
		return GetPassiveTotals(player)[static_cast<size_t>(stat)];
	}
};

TEST_F(Poe2PassiveTreeTest, TreeLoadsWithAStartForEveryClass)
{
	ASSERT_GE(PassiveTree().size(), 300U);
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
	EXPECT_FALSE(CanAllocatePassive(player, FindNode("Gloomy Shrine")));

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
	EXPECT_TRUE(RefundPassive(player, path[3]));
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

	// Two "+5 to any Attribute" nodes put into Strength, 5% damage, then King Leoric's Might: +20 Strength.
	const auto path = WarriorPath();
	ASSERT_EQ(path.size(), 4U);
	for (const PassiveNodeId node : path)
		ASSERT_TRUE(AllocatePassive(player, node, PassiveAttribute::Strength));

	EXPECT_EQ(player._pStrength, strength + 30);
	EXPECT_EQ(Total(player, PassiveStat::Strength), 30);
	EXPECT_EQ(Total(player, PassiveStat::Damage), 5);
	EXPECT_EQ(Total(player, PassiveStat::AnyAttribute), 0);
}

TEST_F(Poe2PassiveTreeTest, AttributeNodesGiveTheAttributePicked)
{
	Player &player = *MyPlayer;
	const auto path = WarriorPath();
	ASSERT_TRUE(PassiveNeedsAttributeChoice(path[0]));
	EXPECT_FALSE(PassiveNeedsAttributeChoice(path.back()));

	ASSERT_TRUE(AllocatePassive(player, path[0], PassiveAttribute::Dexterity));
	ASSERT_TRUE(AllocatePassive(player, path[1], PassiveAttribute::Vitality));
	EXPECT_EQ(ChosenPassiveAttribute(player, path[0]), PassiveAttribute::Dexterity);
	EXPECT_EQ(Total(player, PassiveStat::Dexterity), 5);
	EXPECT_EQ(Total(player, PassiveStat::Vitality), 5);
	EXPECT_EQ(Total(player, PassiveStat::Strength), 0);

	const auto stats = PassiveNodeStats(player, PassiveTree()[path[0]]);
	ASSERT_EQ(stats.size(), 1U);
	EXPECT_EQ(stats[0].first, PassiveStat::Dexterity);
	EXPECT_EQ(stats[0].second, 5);
	for (const PassiveNode &node : PassiveTree()) {
		if (node.kind == PassiveKind::Attribute && !IsPassiveAllocated(player, node.id)) {
			EXPECT_EQ(PassiveNodeStats(player, node)[0].first, PassiveStat::AnyAttribute);
			break;
		}
	}

	// Refunding forgets the pick, so taking it again can choose anew.
	ASSERT_TRUE(RefundPassive(player, path[1]));
	ASSERT_TRUE(AllocatePassive(player, path[1], PassiveAttribute::Magic));
	EXPECT_EQ(Total(player, PassiveStat::Magic), 5);
	EXPECT_EQ(Total(player, PassiveStat::Vitality), 0);
}

TEST_F(Poe2PassiveTreeTest, TheTreeHasFourClassesAndJewelSockets)
{
	int starts = 0;
	int sockets = 0;
	int attributes = 0;
	for (const PassiveNode &node : PassiveTree()) {
		if (node.kind == PassiveKind::Start)
			starts++;
		if (node.kind == PassiveKind::JewelSocket)
			sockets++;
		if (node.kind == PassiveKind::Attribute)
			attributes++;
	}
	EXPECT_EQ(starts, 4);
	EXPECT_GE(sockets, 4);
	EXPECT_GE(attributes, 40);
	EXPECT_EQ(PassiveStartNode(HeroClass::Bard), PassiveStartNode(HeroClass::Rogue));
	EXPECT_EQ(PassiveStartNode(HeroClass::Barbarian), PassiveStartNode(HeroClass::Warrior));
	EXPECT_EQ(PassiveNodeName(PassiveTree()[FindNode("Gloomy Shrine")]), "Gloomy Shrine");
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
			if (node.kind == PassiveKind::Ascendancy && node.startClass == PassiveTreeClass(heroClass))
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
	EXPECT_FALSE(IsPassiveNodeVisible(player, PassiveTree()[knight]));

	player.setCharacterLevel(15);
	EXPECT_TRUE(IsPassiveNodeVisible(player, PassiveTree()[knight]));
	EXPECT_TRUE(IsPassiveNodeVisible(player, PassiveTree()[warlord]));
	EXPECT_FALSE(IsPassiveNodeVisible(player, PassiveTree()[FindNode("Sister of the Sightless Eye")]));
	EXPECT_EQ(AscendancyPointsTotal(player), 2);
	EXPECT_FALSE(CanAllocatePassive(player, FindNode("Sister of the Sightless Eye"))); // another class's
	ASSERT_TRUE(AllocatePassive(player, knight));
	EXPECT_EQ(ChosenAscendancy(player), knight);
	EXPECT_FALSE(CanAllocatePassive(player, warlord)); // only one ascendancy
	EXPECT_FALSE(IsPassiveNodeVisible(player, PassiveTree()[warlord]));
	EXPECT_TRUE(IsPassiveNodeVisible(player, PassiveTree()[PassiveTree()[knight].links[0]]));
	EXPECT_EQ(PassivePointsSpent(player), 0); // choosing is free

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
