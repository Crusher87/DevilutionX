#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "player.h"
#include "poe2/waystones.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2WaystonesTest : public ::testing::Test {
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

TEST_F(Poe2WaystonesTest, WaystonesAreItems)
{
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (AllItemsList[i].iMiscId != IMISC_WAYSTONE)
			continue;
		Item item {};
		InitializeItem(item, static_cast<_item_indexes>(i));
		item._iCreateInfo = 30;
		EXPECT_TRUE(IsWaystone(item));
		EXPECT_EQ(WaystoneTier(item), 4);
		return;
	}
	FAIL() << "no waystone in the item data";
}

TEST_F(Poe2WaystonesTest, TiersFollowItemLevel)
{
	EXPECT_EQ(WaystoneTierForLevel(1), 1);
	EXPECT_EQ(WaystoneTierForLevel(15), 1);
	EXPECT_EQ(WaystoneTierForLevel(20), 2);
	EXPECT_EQ(WaystoneTierForLevel(60), MaxWaystoneTier);
}

TEST_F(Poe2WaystonesTest, MapsAvoidBossLevels)
{
	for (uint32_t seed = 0; seed < 12; seed++) {
		for (int tier = 1; tier <= MaxWaystoneTier; tier++) {
			const int level = MapLevelForTier(tier, seed);
			EXPECT_TRUE(level == 13 || level == 14 || (level >= 21 && level <= 23)) << level;
		}
	}
	EXPECT_LE(MapLevelForTier(1, 7), 14);
	EXPECT_GE(MapLevelForTier(5, 7), 21);
}

TEST_F(Poe2WaystonesTest, PreparingAMapRebuildsItsLevel)
{
	Player &player = *MyPlayer;
	const int level = MapLevelForTier(3, 1234);
	player._pLvlVisited[level] = true;
	LevelSeeds[level] = 99;

	EXPECT_EQ(PrepareMap(player, 3, 1234), level);
	EXPECT_FALSE(player._pLvlVisited[level]);
	EXPECT_FALSE(LevelSeeds[level].has_value());
	EXPECT_EQ(DungeonSeeds[level], 1234U);

	currlevel = 0;
	EXPECT_EQ(ActiveMapTier(), 0);
	currlevel = static_cast<uint8_t>(level);
	EXPECT_EQ(ActiveMapTier(), 3);

	// Going down to another level ends the map.
	currlevel = static_cast<uint8_t>(level + 1);
	leveltype = DTYPE_HELL;
	UpdateActiveMap(player);
	EXPECT_EQ(player.poe2.mapTier, 0);
}

} // namespace
} // namespace devilution
