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
	EXPECT_EQ(WaystoneTierForLevel(60), 10);
	EXPECT_EQ(WaystoneTierForLevel(90), MaxWaystoneTier);
	EXPECT_EQ(WaystoneTierForItemLevel(40), 1);
	EXPECT_EQ(WaystoneTierForItemLevel(70), 5);
	EXPECT_EQ(WaystoneTierForItemLevel(99), MaxWaystoneTier);
}

Item MakeWaystone(int seed)
{
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (AllItemsList[i].iMiscId != IMISC_WAYSTONE)
			continue;
		Item item {};
		InitializeItem(item, static_cast<_item_indexes>(i));
		item._iSeed = seed;
		item._iCreateInfo = 30;
		return item;
	}
	return {};
}

TEST_F(Poe2WaystonesTest, RolledWaystonesFollowTheRules)
{
	int magic = 0;
	int rare = 0;
	for (int seed = 1; seed <= 300; seed++) {
		Item item = MakeWaystone(seed);
		ASSERT_TRUE(IsWaystone(item));
		RollWaystone(item, 7);
		EXPECT_EQ(WaystoneTier(item), 7);
		const int mods = WaystoneModCount(item);
		int prefixes = 0;
		for (const Poe2CraftMod &mod : item._iPoe2Craft.mods) {
			if (mod.family == 0)
				continue;
			EXPECT_LE(mod.family, WaystoneModIds);
			EXPECT_EQ(mod.tier, 2) << "tier 7 is the middle band";
			if (GetWaystoneMod(mod.family).prefix)
				prefixes++;
		}
		EXPECT_LE(prefixes, WaystoneModsPerSide);
		EXPECT_LE(mods - prefixes, WaystoneModsPerSide);
		switch (item._iPoe2Craft.rarity) {
		case ITEM_QUALITY_NORMAL:
			EXPECT_EQ(mods, 0);
			break;
		case ITEM_QUALITY_MAGIC:
			magic++;
			EXPECT_GE(mods, 1);
			EXPECT_LE(mods, 2);
			break;
		default:
			rare++;
			EXPECT_GE(mods, 3);
			EXPECT_LE(mods, 6);
			break;
		}
		EXPECT_EQ(WaystoneRevives(item), MaxMapRevives - mods);
		// The same seed rolls the same waystone.
		Item again = MakeWaystone(seed);
		RollWaystone(again, 7);
		EXPECT_EQ(again._iPoe2Craft.mods[0].family, item._iPoe2Craft.mods[0].family);
	}
	EXPECT_GT(magic, 50);
	EXPECT_GT(rare, 15);
}

TEST_F(Poe2WaystonesTest, TabletSlotsFollowModifiers)
{
	Item item = MakeWaystone(5);
	item._iPoe2Craft.explicitMods = true;
	const std::array<int, 7> slots { 0, 0, 1, 1, 2, 2, 3 };
	for (int count = 0; count <= 6; count++) {
		item._iPoe2Craft.mods = {};
		for (int i = 0; i < count; i++)
			item._iPoe2Craft.mods[static_cast<size_t>(i)] = { static_cast<uint8_t>(1 + i), 1, 1 };
		EXPECT_EQ(WaystoneTabletSlots(item), slots[static_cast<size_t>(count)]) << count;
	}
}

TEST_F(Poe2WaystonesTest, ModifierValuesStayInTheirBand)
{
	for (uint8_t id = 1; id <= WaystoneModIds; id++) {
		const WaystoneMod &data = GetWaystoneMod(id);
		EXPECT_FALSE(data.name.empty());
		for (uint8_t band = 1; band <= 3; band++) {
			for (uint8_t salt = 1; salt <= 31; salt += 5) {
				const int value = WaystoneModValue({ id, band, salt });
				EXPECT_GE(value, data.dangerValues[band - 1][0]);
				EXPECT_LE(value, data.dangerValues[band - 1][1]);
			}
			EXPECT_GT(WaystoneModReward({ id, band, 1 }), 0);
		}
		EXPECT_EQ(WaystoneModText({ id, 1, 1 }).size(), 2U);
	}
}

TEST_F(Poe2WaystonesTest, MapEffectsAddUpWithinTheWaystone)
{
	Player &player = *MyPlayer;
	const int level = PrepareMap(player, 3, 77);
	currlevel = static_cast<uint8_t>(level);
	// Ravenous (monster rarity) and Unyielding (monster rarity) add up; Tempestuous adds pack size.
	player.poe2.mapWaystone.explicitMods = true;
	player.poe2.mapWaystone.mods[0] = { 1, 1, 1 };
	player.poe2.mapWaystone.mods[1] = { 6, 1, 1 };
	player.poe2.mapWaystone.mods[2] = { 4, 1, 1 };
	RefreshMapEffects(player);
	const MapEffects &effects = ActiveMapEffects();
	EXPECT_EQ(effects[MapAxis::MonsterRarity], 100 + 15 + 14);
	EXPECT_EQ(effects[MapAxis::PackSize], 104);
	EXPECT_EQ(effects[MapAxis::ItemRarity], 100);
	EXPECT_GE(effects[MapDanger::MonsterDamage], 10);
	EXPECT_LE(effects[MapDanger::MonsterDamage], 14);
	EXPECT_GE(effects[MapDanger::MonsterLife], 15);

	// Outside the map nothing applies.
	currlevel = 0;
	EXPECT_EQ(ActiveMapEffects()[MapAxis::MonsterRarity], 100);
}

TEST_F(Poe2WaystonesTest, IndependentSourcesMultiply)
{
	MapEffects effects;
	effects.multiply(MapAxis::ItemRarity, 20);
	effects.multiply(MapAxis::ItemRarity, 15);
	EXPECT_EQ(effects[MapAxis::ItemRarity], 138);
}

TEST_F(Poe2WaystonesTest, RevivesAreUsedUp)
{
	Player &player = *MyPlayer;
	const int level = PrepareMap(player, 2, 5);
	currlevel = static_cast<uint8_t>(level);
	player._pMaxHP = Fixed26_6::fromInt(100);
	player._pMaxHPBase = player._pMaxHP;
	player.poe2.mapRevives = 1;
	EXPECT_TRUE(TryMapRevive(player));
	EXPECT_EQ(player._pHitPoints, player._pMaxHP);
	EXPECT_EQ(player.poe2.mapRevives, 0);
	EXPECT_FALSE(TryMapRevive(player));
	currlevel = 0;
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
