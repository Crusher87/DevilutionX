#include <algorithm>
#include <array>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/jewels.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/weapon_swap.hpp"
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
	static std::vector<PassiveNodeId> WarriorPathTo(PassiveNodeId goal)
	{
		const PassiveNodeId start = *PassiveStartNode(HeroClass::Warrior);
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

	/** @brief The node of that kind nearest the Warrior's start (lowest id on a tie), leaving out the ones given. */
	static PassiveNodeId NearestWarriorNode(PassiveKind kind, const std::vector<PassiveNodeId> &except = {})
	{
		std::optional<PassiveNodeId> best;
		size_t bestSteps = 0;
		for (const PassiveNode &node : PassiveTree()) {
			if (node.kind != kind || IsAscendancyNode(node) || std::find(except.begin(), except.end(), node.id) != except.end())
				continue;
			const size_t steps = WarriorPathTo(node.id).size();
			if (!best || steps < bestSteps) {
				best = node.id;
				bestSteps = steps;
			}
		}
		EXPECT_TRUE(best.has_value());
		return best.value_or(0);
	}

	/** @brief The way to the Warrior's nearest notable: three small nodes, then the notable. */
	static std::vector<PassiveNodeId> WarriorPath()
	{
		return WarriorPathTo(NearestWarriorNode(PassiveKind::Notable));
	}

	/** @brief What the nodes add up to for a stat, with "+5 to any Attribute" nodes put into the attribute given. */
	static int PathTotal(const std::vector<PassiveNodeId> &path, PassiveStat stat, PassiveStat picked)
	{
		int total = 0;
		for (const PassiveNodeId id : path) {
			for (const auto &[nodeStat, value] : PassiveTree()[id].stats) {
				if (nodeStat == stat || (nodeStat == PassiveStat::AnyAttribute && stat == picked))
					total += value;
			}
		}
		return total;
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

	// The way to an Attribute node and on to the nearest notable, attributes put into Strength.
	player.setCharacterLevel(40);
	std::vector<PassiveNodeId> path = WarriorPathTo(NearestWarriorNode(PassiveKind::Attribute));
	for (const PassiveNodeId node : WarriorPath()) {
		if (std::find(path.begin(), path.end(), node) == path.end())
			path.push_back(node);
	}
	for (const PassiveNodeId node : path)
		ASSERT_TRUE(AllocatePassive(player, node, PassiveAttribute::Strength) || IsPassiveAllocated(player, node)) << node;

	const int strengthGained = PathTotal(path, PassiveStat::Strength, PassiveStat::Strength);
	EXPECT_GE(strengthGained, 5);
	EXPECT_EQ(player._pStrength, strength + strengthGained);
	EXPECT_EQ(Total(player, PassiveStat::Strength), strengthGained);
	for (const auto &[stat, value] : PassiveTree()[WarriorPath().back()].stats)
		EXPECT_EQ(Total(player, stat), PathTotal(path, stat, PassiveStat::Strength)) << static_cast<int>(stat);
	EXPECT_EQ(Total(player, PassiveStat::AnyAttribute), 0);
}

TEST_F(Poe2PassiveTreeTest, AttributeNodesGiveTheAttributePicked)
{
	Player &player = *MyPlayer;
	player.setCharacterLevel(40);
	const PassiveNodeId first = NearestWarriorNode(PassiveKind::Attribute);
	const PassiveNodeId second = NearestWarriorNode(PassiveKind::Attribute, { first });
	ASSERT_TRUE(PassiveNeedsAttributeChoice(first));
	EXPECT_FALSE(PassiveNeedsAttributeChoice(WarriorPath().back()));

	for (const PassiveNodeId node : WarriorPathTo(first))
		ASSERT_TRUE(AllocatePassive(player, node, PassiveAttribute::Dexterity)) << node;
	for (const PassiveNodeId node : PassiveRoute(player, second)) {
		if (!IsPassiveAllocated(player, node))
			ASSERT_TRUE(AllocatePassive(player, node, PassiveAttribute::Vitality)) << node;
	}
	EXPECT_EQ(ChosenPassiveAttribute(player, first), PassiveAttribute::Dexterity);
	EXPECT_EQ(ChosenPassiveAttribute(player, second), PassiveAttribute::Vitality);
	const int dexterity = Total(player, PassiveStat::Dexterity);
	const int vitality = Total(player, PassiveStat::Vitality);
	EXPECT_GE(dexterity, 5);
	EXPECT_GE(vitality, 5);

	const auto stats = PassiveNodeStats(player, PassiveTree()[first]);
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
	const int magic = Total(player, PassiveStat::Magic);
	ASSERT_TRUE(RefundPassive(player, second));
	ASSERT_TRUE(AllocatePassive(player, second, PassiveAttribute::Magic));
	EXPECT_EQ(Total(player, PassiveStat::Magic), magic + 5);
	EXPECT_EQ(Total(player, PassiveStat::Vitality), vitality - 5);
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

TEST_F(Poe2PassiveTreeTest, WeaponSetPointsComeFromBooks)
{
	Player &player = *MyPlayer;
	const std::vector<PassiveNodeId> path = WarriorPath();
	ASSERT_GE(path.size(), 3U);
	EXPECT_EQ(WeaponSetPointsTotal(player), 0);
	EXPECT_FALSE(CanAllocatePassive(player, path[0], PassivePool::SetI));

	player.poe2.specBooks = 0b11;
	EXPECT_EQ(WeaponSetPointsTotal(player), 2 * WeaponSetPointsPerBook);
	ASSERT_TRUE(AllocatePassive(player, path[0], PassiveAttribute::Dexterity, PassivePool::SetI));
	EXPECT_EQ(WeaponSetPointsSpent(player, PassivePool::SetI), 1);
	EXPECT_EQ(WeaponSetPointsSpent(player, PassivePool::SetII), 0);
	// Like POE2, a set node spends a normal point...
	EXPECT_EQ(PassivePointsSpent(player), 1);

	// Set nodes only count while their set is out.
	player.poe2.weaponSet = 0;
	EXPECT_TRUE(IsPassiveAllocated(player, path[0]));
	player.poe2.weaponSet = 1;
	EXPECT_FALSE(IsPassiveAllocated(player, path[0]));
	EXPECT_FALSE(CanAllocatePassive(player, path[1], PassivePool::SetII));
	EXPECT_TRUE(CanAllocatePassive(player, path[1], PassivePool::SetI));
}

TEST_F(Poe2PassiveTreeTest, WeaponSetNodesGrowFromTheMainTree)
{
	Player &player = *MyPlayer;
	player.poe2.specBooks = 0b1;
	const std::vector<PassiveNodeId> path = WarriorPath();
	ASSERT_TRUE(AllocatePassive(player, path[0]));
	ASSERT_TRUE(AllocatePassive(player, path[1], PassiveAttribute::Strength, PassivePool::SetII));
	// The main node holds up the set node, so it can't be refunded.
	EXPECT_FALSE(CanRefundPassive(player, path[0]));
	EXPECT_TRUE(CanRefundPassive(player, path[1], PassivePool::SetII));
	EXPECT_FALSE(CanRefundPassive(player, path[1]));

	// Taking a set node with a main point moves it into the main tree.
	ASSERT_TRUE(AllocatePassive(player, path[1]));
	EXPECT_EQ(WeaponSetPointsSpent(player, PassivePool::SetII), 0);
	EXPECT_TRUE(IsPassiveInPool(player, path[1], PassivePool::Main));

	ResetPassives(player);
	EXPECT_EQ(WeaponSetPointsSpent(player, PassivePool::SetI) + WeaponSetPointsSpent(player, PassivePool::SetII), 0);
}

TEST_F(Poe2PassiveTreeTest, WeaponSetNodesPairUpForOnePoint)
{
	Player &player = *MyPlayer;
	player.poe2.specBooks = 0b1;
	const std::vector<PassiveNodeId> path = WarriorPath();
	ASSERT_TRUE(AllocatePassive(player, path[0]));
	ASSERT_TRUE(AllocatePassive(player, path[1], PassiveAttribute::Strength, PassivePool::SetI));
	EXPECT_EQ(PassivePointsSpent(player), 2);
	// ...but a Set II node that pairs with a Set I node is free.
	ASSERT_TRUE(AllocatePassive(player, path[1], PassiveAttribute::Dexterity, PassivePool::SetII));
	EXPECT_EQ(PassivePointsSpent(player), 2);

	// With no normal points left, only a node that pairs up can still be taken.
	const int left = PassivePointsLeft(player);
	player.setCharacterLevel(static_cast<uint8_t>(player.getCharacterLevel() - left));
	ASSERT_EQ(PassivePointsLeft(player), 0);
	EXPECT_TRUE(WeaponSetNeedsPassivePoint(player, PassivePool::SetI));
	EXPECT_FALSE(CanAllocatePassive(player, path[2], PassivePool::SetI));
}

TEST_F(Poe2PassiveTreeTest, BooksGivePointsToSpendInTheSets)
{
	Player &player = *MyPlayer;
	player.setCharacterLevel(1);
	const std::vector<PassiveNodeId> path = WarriorPath();
	ASSERT_EQ(PassivePointsLeft(player), 0);
	// A level 1 hero who reads a book can spend it: like POE2's, a book brings 2 normal points as well as 2 set points.
	player.poe2.specBooks = 0b1;
	EXPECT_EQ(PassivePointsLeft(player), PassivePointsPerBook);
	EXPECT_EQ(WeaponSetPointsTotal(player), WeaponSetPointsPerBook);
	ASSERT_TRUE(AllocatePassive(player, path[0], PassiveAttribute::Strength, PassivePool::SetI));
	ASSERT_TRUE(AllocatePassive(player, path[0], PassiveAttribute::Strength, PassivePool::SetII));
	ASSERT_TRUE(AllocatePassive(player, path[1], PassiveAttribute::Strength, PassivePool::SetI));
	ASSERT_TRUE(AllocatePassive(player, path[1], PassiveAttribute::Strength, PassivePool::SetII));
	EXPECT_EQ(WeaponSetPointsLeft(player, PassivePool::SetI), 0);
	EXPECT_EQ(PassivePointsLeft(player), 0);
}

TEST_F(Poe2PassiveTreeTest, OverspentWeaponSetsAreRefunded)
{
	Player &player = *MyPlayer;
	player.poe2.specBooks = 0b1;
	const std::vector<PassiveNodeId> path = WarriorPath();
	ASSERT_TRUE(AllocatePassive(player, path[0]));
	ASSERT_TRUE(AllocatePassive(player, path[1], PassiveAttribute::Strength, PassivePool::SetI));
	RefundOverspentWeaponSets(player);
	EXPECT_TRUE(IsPassiveInPool(player, path[1], PassivePool::SetI));

	// Books used to give 4 set points, so an older hero can have spent more than one book now gives.
	player.poe2.setPassives[0].set(path[2]);
	player.poe2.setPassives[0].set(path[3]);
	RefundOverspentWeaponSets(player);
	EXPECT_EQ(WeaponSetPointsSpent(player, PassivePool::SetI), 0);
	EXPECT_TRUE(IsPassiveInPool(player, path[0], PassivePool::Main));
}

TEST_F(Poe2PassiveTreeTest, RouteMarksTheQuickestWay)
{
	Player &player = *MyPlayer;
	const PassiveNodeId start = *PassiveStartNode(HeroClass::Warrior);
	const std::vector<PassiveNodeId> path = WarriorPath();
	std::vector<PassiveNodeId> expected { start };
	expected.insert(expected.end(), path.begin(), path.end());
	EXPECT_EQ(PassiveRoute(player, path.back()), expected);

	ASSERT_TRUE(AllocatePassive(player, path[0]));
	EXPECT_EQ(PassiveRoute(player, path.back()).size(), path.size());
	EXPECT_TRUE(PassiveRoute(player, path[0]).empty()); // taken already
	EXPECT_TRUE(PassiveRoute(player, FindNode("Knight of Westmarch")).empty()); // ascendancies aren't bought with points
}

TEST_F(Poe2PassiveTreeTest, ClassesSitFarApartLikePoe2)
{
	// Each class reaches its first notable in 4 or 5 points, a neighbour's in 14 or more and the opposite class's in 22 or more.
	const PassiveNodeId notable = NearestWarriorNode(PassiveKind::Notable);
	EXPECT_GE(WarriorPathTo(notable).size(), 4U);
	EXPECT_LE(WarriorPathTo(notable).size(), 5U);
	EXPECT_GE(WarriorPathTo(*PassiveStartNode(HeroClass::Monk)).size(), 22U);
	for (const HeroClass neighbour : { HeroClass::Rogue, HeroClass::Sorcerer })
		EXPECT_GE(WarriorPathTo(*PassiveStartNode(neighbour)).size(), 12U) << static_cast<int>(neighbour);
}

TEST_F(Poe2PassiveTreeTest, ANewTreeMovesJewelsNearTheStart)
{
	Player &player = *MyPlayer;
	ASSERT_GT(PassiveTreeHash(), 0U);
	player.poe2.passiveTreeHash = PassiveTreeHash() + 1;
	player.jewels[MaxJewelSockets - 1]._itype = ItemType::Misc;
	ASSERT_TRUE(AllocatePassive(player, WarriorPath()[0]));
	RefundPassivesForNewTree(player);
	EXPECT_EQ(PassivePointsSpent(player), 0);
	EXPECT_EQ(player.poe2.passiveTreeHash, PassiveTreeHash());

	const PassiveNodeId nearest = NearestWarriorNode(PassiveKind::JewelSocket);
	const std::optional<size_t> index = JewelSocketIndex(nearest);
	ASSERT_TRUE(index.has_value());
	int jewels = 0;
	for (const Item &jewel : player.jewels)
		jewels += jewel.isEmpty() ? 0 : 1;
	EXPECT_EQ(jewels, 1);
	EXPECT_FALSE(player.jewels[*index].isEmpty());
}

TEST_F(Poe2PassiveTreeTest, ItemTableHasEveryBookOfSpecialisation)
{
	int books = 0;
	for (const ItemData &item : AllItemsList) {
		if (item.iMiscId == IMISC_SPEC_BOOK)
			books++;
	}
	EXPECT_EQ(books, static_cast<int>(NumSpecBooks));
}

} // namespace
} // namespace devilution
