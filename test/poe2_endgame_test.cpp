#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "player.h"
#include "poe2/craft_state.hpp"
#include "poe2/crafting_items.hpp"
#include "poe2/endgame.hpp"
#include "poe2/waystones.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2EndgameTest : public ::testing::Test {
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
		currlevel = 0;
		UnloadModArchives();
	}
};

Item MakeItem(item_misc_id miscId, int seed, uint8_t variant = 0)
{
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (AllItemsList[i].iMiscId != miscId)
			continue;
		Item item {};
		InitializeItem(item, static_cast<_item_indexes>(i));
		item._iSeed = seed;
		item.dwBuff |= CF_POE2;
		SetCraftingItemVariant(item, variant);
		FinishEndgameItem(item);
		return item;
	}
	return {};
}

TEST_F(Poe2EndgameTest, TabletsRollKindsRaritiesAndUses)
{
	int withMods = 0;
	for (int seed = 1; seed <= 200; seed++) {
		const Item tablet = MakeItem(IMISC_TABLET, seed);
		ASSERT_TRUE(IsTablet(tablet));
		EXPECT_NE(GetTabletKind(tablet), TabletKind::None);
		EXPECT_EQ(TabletUsesLeft(tablet), TabletUses);
		const TabletData &data = GetTablet(GetTabletKind(tablet));
		for (const Poe2CraftMod &mod : tablet._iPoe2Craft.mods) {
			if (mod.family == 0)
				continue;
			withMods++;
			const auto tabletMod = static_cast<TabletMod>(mod.family);
			EXPECT_TRUE(tabletMod == data.pool[0] || tabletMod == data.pool[1]);
		}
		EXPECT_FALSE(EndgameItemText(tablet).empty());
	}
	EXPECT_GT(withMods, 50);
}

TEST_F(Poe2EndgameTest, TabletModValuesFollowTheBundle)
{
	EXPECT_EQ(TabletModValue(TabletMod::Bountiful, 1, false), 10);
	EXPECT_EQ(TabletModValue(TabletMod::Bountiful, MaxCraftSalt, false), 35);
	EXPECT_EQ(TabletModValue(TabletMod::Breeding, 1, true), 6);
	EXPECT_EQ(TabletModAxis(TabletMod::Cartographic), MapAxis::WaystoneFind);
}

TEST_F(Poe2EndgameTest, TabletsAndTheAtlasMultiplyIntoTheMap)
{
	Player &player = *MyPlayer;
	MapTablet &tablet = player.poe2.mapRun.tablets[0];
	tablet.kind = static_cast<uint8_t>(TabletKind::Bounty);
	tablet.mods[0] = { static_cast<uint8_t>(TabletMod::Bountiful), 1, 1 };
	player.poe2.atlas.ranks[static_cast<size_t>(AtlasNode::PreciousGround)] = 5;
	MapEffects effects;
	effects.multiply(MapAxis::ItemRarity, 20);
	AddEndgameMapEffects(player, effects);
	// 120 x 110 (tablet) x 120 (Atlas).
	EXPECT_EQ(effects[MapAxis::ItemRarity], 120 * 110 / 100 * 120 / 100);
}

TEST_F(Poe2EndgameTest, AtlasPointsComeFromMaps)
{
	Player &player = *MyPlayer;
	EXPECT_FALSE(AllocateAtlasNode(player, AtlasNode::PackedPaths));
	player.poe2.mapsCompleted = 2;
	player.poe2.atlas.pinnaclesDefeated = 1;
	EXPECT_EQ(AtlasPointsEarned(player), 4);
	EXPECT_TRUE(AllocateAtlasNode(player, AtlasNode::OneLife));
	EXPECT_FALSE(AllocateAtlasNode(player, AtlasNode::OneLife)) << "keystones have one rank";
	EXPECT_TRUE(AllocateAtlasNode(player, AtlasNode::PackedPaths));
	EXPECT_EQ(AtlasPointsSpent(player), 2);
	EXPECT_EQ(MapRevivesFor(player, 5), 1);
	EXPECT_TRUE(RefundAtlasNode(player, AtlasNode::OneLife));
	EXPECT_EQ(MapRevivesFor(player, 5), 5);
	player.poe2.atlas.ranks[static_cast<size_t>(AtlasNode::DeadlierBargain)] = 1;
	EXPECT_EQ(WaystoneModStrengthPercent(player), 120);
}

TEST_F(Poe2EndgameTest, FragmentsRespectAreaLevel)
{
	for (int seed = 1; seed <= 100; seed++) {
		Item fragment = MakeItem(IMISC_MAP_FRAGMENT, seed, 1);
		RollFragmentSet(fragment, 66);
		const FragmentSet set = GetFragmentSet(fragment);
		ASSERT_NE(set, FragmentSet::None);
		EXPECT_LE(GetFragmentSet(set).minimumAreaLevel, 66);
	}
	const Item key = MakeItem(IMISC_MAP_KEY, 3, static_cast<uint8_t>(FragmentSet::Hunger));
	EXPECT_TRUE(IsMapKey(key));
	EXPECT_EQ(GetFragmentSet(key), FragmentSet::Hunger);
	EXPECT_EQ(std::string_view(key._iIName), "Starved Gate");
}

TEST_F(Poe2EndgameTest, TabletsBringTheirEncounters)
{
	Player &player = *MyPlayer;
	Item waystone {};
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (AllItemsList[i].iMiscId == IMISC_WAYSTONE) {
			InitializeItem(waystone, static_cast<_item_indexes>(i));
			break;
		}
	}
	// Six modifiers: three tablet slots.
	waystone._iPoe2Craft.explicitMods = true;
	for (uint8_t i = 0; i < 6; i++)
		waystone._iPoe2Craft.mods[i] = { static_cast<uint8_t>(1 + i), 1, 1 };
	player._pNumInv = 0;
	for (int i = 0; i < 4; i++) {
		player.InvList[player._pNumInv++] = MakeItem(IMISC_TABLET, 10 + i, static_cast<uint8_t>(TabletKind::Antiquarian));
	}
	int strongboxes = 0;
	for (int seed = 0; seed < 100; seed++) {
		waystone._iSeed = seed;
		PrepareMapEndgame(player, waystone);
		EXPECT_EQ(player.poe2.mapRun.tablets[2].kind, static_cast<uint8_t>(TabletKind::Antiquarian));
		if (player.poe2.mapRun.has(Encounter::Strongbox))
			strongboxes++;
	}
	// 14% natural plus 75% for each of three tablets.
	EXPECT_GT(strongboxes, 90);

	// Using the map spends one use of each of the three tablets.
	SpendMapTablets(player);
	EXPECT_EQ(TabletUsesLeft(player.InvList[0]), TabletUses - 1);
	EXPECT_EQ(TabletUsesLeft(player.InvList[3]), TabletUses);
}

} // namespace
} // namespace devilution
