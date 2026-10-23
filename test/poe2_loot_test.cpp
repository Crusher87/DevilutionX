#include <algorithm>
#include <array>
#include <numeric>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/gemcutting.hpp"
#include "poe2/loot.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/support_tiers.hpp"
#include "poe2/waystones.hpp"
#include "spells.h"
#include "stores.h"
#include "utils/is_of.hpp"

namespace devilution {
namespace {

class Poe2LootTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		gbIsPoe2 = true;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadSpellData();
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		UnloadModArchives();
	}

	static _item_indexes FindBase(std::string_view name)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == name && AllItemsList[i].dropRate > 0)
				return static_cast<_item_indexes>(i);
		}
		return IDI_GOLD;
	}

	/** Builds a dropped item the way a loot roll does. */
	static Item MakeDrop(_item_indexes base, item_quality rarity, uint32_t seed, int level = 20, int uidOffset = 0)
	{
		Item item = {};
		SetLootRarity(item, rarity);
		item.dwBuff |= (static_cast<uint32_t>(uidOffset) << 1) & CF_UIDOFFSET;
		SetupAllItems(*MyPlayer, item, base, seed, level, 1, true, false, uidOffset);
		item._iIdentified = true;
		return item;
	}

	template <typename F>
	static std::array<int, 8> Count(int trials, F roll)
	{
		std::array<int, 8> counts {};
		SetRndSeed(12345);
		for (int i = 0; i < trials; i++)
			counts[static_cast<size_t>(roll())]++;
		return counts;
	}
};

TEST_F(Poe2LootTest, KillsGiveRollsByToughness)
{
	EXPECT_EQ(ExpectedLootRollsPerMille(LootSource::Normal), 150);
	EXPECT_EQ(ExpectedLootRollsPerMille(LootSource::Rare), 1500);
	EXPECT_EQ(ExpectedLootRollsPerMille(LootSource::Boss), 4000);

	constexpr int Trials = 20000;
	SetRndSeed(7);
	int normal = 0;
	int rare = 0;
	for (int i = 0; i < Trials; i++) {
		normal += RollLootCount(LootSource::Normal);
		rare += RollLootCount(LootSource::Rare);
	}
	EXPECT_NEAR(normal / static_cast<double>(Trials), 0.15, 0.015);
	EXPECT_NEAR(rare / static_cast<double>(Trials), 1.5, 0.03) << "one roll, and a second half the time";
}

TEST_F(Poe2LootTest, CategoriesFollowTheBible)
{
	const auto weights = LootCategoryWeights(LootSource::Normal);
	EXPECT_EQ(std::accumulate(weights.begin(), weights.end(), 0), 100);
	EXPECT_EQ(weights[static_cast<size_t>(LootCategory::Equipment)], 48);
	EXPECT_EQ(weights[static_cast<size_t>(LootCategory::Gold)], 20);
	EXPECT_EQ(weights[static_cast<size_t>(LootCategory::Gem)], 5);

	const auto boss = LootCategoryWeights(LootSource::Boss);
	EXPECT_EQ(boss[static_cast<size_t>(LootCategory::Equipment)], 96);
	EXPECT_EQ(boss[static_cast<size_t>(LootCategory::Gold)], 10);
	EXPECT_EQ(boss[static_cast<size_t>(LootCategory::Misc)], 0);
}

TEST_F(Poe2LootTest, RarityTablesMatchTheSource)
{
	constexpr int Trials = 40000;
	const auto normal = Count(Trials, [] { return RollEquipmentRarity(LootSource::Normal); });
	EXPECT_NEAR(normal[ITEM_QUALITY_NORMAL] / static_cast<double>(Trials), 0.75, 0.01);
	EXPECT_NEAR(normal[ITEM_QUALITY_MAGIC] / static_cast<double>(Trials), 0.2275, 0.01);
	EXPECT_NEAR(normal[ITEM_QUALITY_RARE] / static_cast<double>(Trials), 0.02, 0.003);

	const auto rare = Count(Trials, [] { return RollEquipmentRarity(LootSource::Rare); });
	EXPECT_NEAR(rare[ITEM_QUALITY_RARE] / static_cast<double>(Trials), 0.17, 0.01);

	const auto boss = Count(Trials, [] { return RollEquipmentRarity(LootSource::Boss); });
	EXPECT_NEAR(boss[ITEM_QUALITY_RARE] / static_cast<double>(Trials), 0.30, 0.01);
	EXPECT_NEAR(boss[ITEM_QUALITY_UNIQUE] / static_cast<double>(Trials), 0.05, 0.005);

	EXPECT_EQ(LootItemLevelBonus(LootSource::Rare), 1);
	EXPECT_EQ(LootItemLevelBonus(LootSource::Boss), 2);
}

/** Equipment drops a kill gives on average, and how many of them are of each rarity. */
std::array<double, 4> ExpectedEquipment(LootSource source)
{
	const auto categories = LootCategoryWeights(source);
	const double equipment = ExpectedLootRollsPerMille(source) / 1000.0 * categories[static_cast<size_t>(LootCategory::Equipment)]
	    / std::accumulate(categories.begin(), categories.end(), 0);
	const std::array<int, 4> rarity = EquipmentRarityWeights(source);
	std::array<double, 4> result {};
	for (size_t i = 0; i < result.size(); i++)
		result[i] = equipment * rarity[i] / 10000.0;
	return result;
}

TEST_F(Poe2LootTest, CampaignLootIsMostlyNormalAndMagic)
{
	// The campaign's mix of monsters: 3% rare, 12% magic, the rest normal.
	const double rareMonsters = RareMonsterPerMille / 1000.0;
	const double magicMonsters = MagicMonsterPerMille / 1000.0;
	std::array<double, 4> total {};
	for (size_t i = 0; i < total.size(); i++) {
		total[i] = (1 - rareMonsters - magicMonsters) * ExpectedEquipment(LootSource::Normal)[i]
		    + magicMonsters * ExpectedEquipment(LootSource::Magic)[i] + rareMonsters * ExpectedEquipment(LootSource::Rare)[i];
	}
	const double all = std::accumulate(total.begin(), total.end(), 0.0);
	// Curtis's 2026-10-23 run had 18% rare and 29% magic: about one rare in sixteen now.
	EXPECT_GT(total[ITEM_QUALITY_RARE] / all, 0.04);
	EXPECT_LT(total[ITEM_QUALITY_RARE] / all, 0.08);
	EXPECT_GT(total[ITEM_QUALITY_MAGIC] / all, 0.25);
	EXPECT_LT(total[ITEM_QUALITY_MAGIC] / all, 0.38);
	// Fewer than a rare per hundred kills.
	EXPECT_LT(total[ITEM_QUALITY_RARE] * 100, 1.0);
}

TEST_F(Poe2LootTest, BossesDropAHandfulNotAPile)
{
	// A quest boss: its rolls plus the guaranteed rare. Snotspill dropped 4 rares on the old numbers (3.75 on average).
	const std::array<double, 4> boss = ExpectedEquipment(LootSource::Boss);
	const double rares = boss[ITEM_QUALITY_RARE] + 1;
	EXPECT_GT(rares, 1.5);
	EXPECT_LT(rares, 2.0);
	EXPECT_LT(std::accumulate(boss.begin(), boss.end(), 0.0) + 1, 4.0);
	// A rare monster: about one and a half rolls, a rare one time in five.
	const std::array<double, 4> rare = ExpectedEquipment(LootSource::Rare);
	EXPECT_LT(rare[ITEM_QUALITY_RARE], 0.2);
}

TEST_F(Poe2LootTest, MapBonusesRaiseRarityAndWaystones)
{
	LootBonus bonus;
	bonus.rarity = 200;
	bonus.waystones = 300;
	const std::array<int, 4> weights = EquipmentRarityWeights(LootSource::Normal, bonus);
	EXPECT_EQ(weights[1], 4550);
	EXPECT_EQ(weights[2], 400);
	EXPECT_EQ(weights[3], 50);
	EXPECT_EQ(weights[0], 10000 - 4550 - 400 - 50);
	EXPECT_EQ(EquipmentRarityWeights(LootSource::Normal), EquipmentRarityWeights(LootSource::Normal, {}));
	const auto categories = LootCategoryWeights(LootSource::Normal, bonus);
	EXPECT_EQ(categories[static_cast<size_t>(LootCategory::Endgame)], 3 * LootCategoryWeights(LootSource::Normal)[static_cast<size_t>(LootCategory::Endgame)]);
}

TEST_F(Poe2LootTest, UniqueRollsPickATierFirst)
{
	constexpr int Trials = 40000;
	const auto normal = Count(Trials, [] { return static_cast<int>(RollUniqueTier(LootSource::Normal)); });
	EXPECT_NEAR(normal[0] / static_cast<double>(Trials), 0.84, 0.01);
	EXPECT_NEAR(normal[1] / static_cast<double>(Trials), 0.12, 0.01);
	EXPECT_NEAR(normal[2] / static_cast<double>(Trials), 0.035, 0.005);

	const auto boss = Count(Trials, [] { return static_cast<int>(RollUniqueTier(LootSource::Boss)); });
	EXPECT_NEAR(boss[0] / static_cast<double>(Trials), 0.65, 0.01);
	EXPECT_NEAR(boss[2] / static_cast<double>(Trials), 0.10, 0.01);
	EXPECT_NEAR(boss[3] / static_cast<double>(Trials), 0.02, 0.005);

	EXPECT_EQ(LegacyUniqueTier(1), UniqueTier::Common);
	EXPECT_EQ(LegacyUniqueTier(8), UniqueTier::Common);
	EXPECT_EQ(LegacyUniqueTier(9), UniqueTier::Uncommon);
	EXPECT_EQ(LegacyUniqueTier(20), UniqueTier::Rare);
}

TEST_F(Poe2LootTest, NaturalRaresHaveThreeToSixAffixes)
{
	constexpr int Trials = 24000;
	const auto counts = Count(Trials, RollNaturalRareAffixCount);
	EXPECT_EQ(counts[3] + counts[4] + counts[5] + counts[6], Trials);
	EXPECT_NEAR(counts[3] / static_cast<double>(Trials), 3.0 / 24, 0.01);
	EXPECT_NEAR(counts[4] / static_cast<double>(Trials), 13.0 / 24, 0.015);
	EXPECT_NEAR(counts[6] / static_cast<double>(Trials), 2.0 / 24, 0.01);
}

TEST_F(Poe2LootTest, OldBasesFadeOut)
{
	EXPECT_EQ(BaseRecencyPercent(10, 9), 100);
	EXPECT_EQ(BaseRecencyPercent(10, 4), 70);
	EXPECT_EQ(BaseRecencyPercent(20, 9), 35);
	EXPECT_EQ(BaseRecencyPercent(30, 1), 10);
}

TEST_F(Poe2LootTest, OrbsSitInValueBands)
{
	EXPECT_EQ(OrbCurrencyBand(IMISC_ORB_TRANSMUTE), CurrencyBand::Common);
	EXPECT_EQ(OrbCurrencyBand(IMISC_ORB_REGAL), CurrencyBand::Uncommon);
	EXPECT_EQ(OrbCurrencyBand(IMISC_ORB_EXALTED), CurrencyBand::Rare);
	EXPECT_EQ(OrbCurrencyBand(IMISC_ORB_SCOURING), CurrencyBand::Rare);
	EXPECT_EQ(OrbCurrencyBand(IMISC_ORB_DIVINE), CurrencyBand::Premium);
}

TEST_F(Poe2LootTest, CurrencyWaitsForItsItemLevel)
{
	// The bundle's levels, with Alchemy held back to Regal's.
	EXPECT_EQ(CurrencyDropLevel(IMISC_ORB_TRANSMUTE), 1);
	EXPECT_EQ(CurrencyDropLevel(IMISC_ORB_AUGMENT), 1);
	EXPECT_EQ(CurrencyDropLevel(IMISC_ORB_ALCHEMY), 12);
	EXPECT_EQ(CurrencyDropLevel(IMISC_ORB_REGAL), 12);
	EXPECT_EQ(CurrencyDropLevel(IMISC_ORB_SCOURING), 20);
	EXPECT_EQ(CurrencyDropLevel(IMISC_ORB_LESSER_JEWELLER), 14) << "Act 2's first floor, never Act 1";
	EXPECT_EQ(CurrencyDropLevel(IMISC_ORB_DIVINE), 35);
	// Transmutation and Augmentation are the most common.
	EXPECT_GT(CurrencyBandWeight(IMISC_ORB_TRANSMUTE), CurrencyBandWeight(IMISC_ORB_WHETSTONE));
	EXPECT_GT(CurrencyBandWeight(IMISC_ORB_WHETSTONE), CurrencyBandWeight(IMISC_ORB_BAUBLE));
}

TEST_F(Poe2LootTest, DropsTakeTheRolledRarityAndRecreateTheSame)
{
	const _item_indexes base = FindBase("Shortsword");
	ASSERT_NE(base, IDI_GOLD);
	for (uint32_t seed = 1; seed <= 60; seed++) {
		const Item normal = MakeDrop(base, ITEM_QUALITY_NORMAL, seed);
		EXPECT_EQ(normal._iMagical, ITEM_QUALITY_NORMAL);

		const Item magic = MakeDrop(base, ITEM_QUALITY_MAGIC, seed);
		EXPECT_EQ(magic._iMagical, ITEM_QUALITY_MAGIC) << seed;

		const Item rare = MakeDrop(base, ITEM_QUALITY_RARE, seed);
		ASSERT_EQ(rare._iMagical, ITEM_QUALITY_RARE);
		const auto affixes = std::count_if(rare._iRarePowers.begin(), rare._iRarePowers.end(), [](item_effect_type p) { return p != IPL_INVALID; });
		EXPECT_GE(affixes, 3) << "four wanted; overlapping affixes can leave one out";
		EXPECT_LE(affixes, 6);

		for (const Item *item : { &normal, &magic, &rare }) {
			Item recreated;
			RecreateItem(*MyPlayer, recreated, item->IDidx, item->_iCreateInfo, item->_iSeed, item->_ivalue, item->dwBuff);
			EXPECT_EQ(recreated._iMagical, item->_iMagical);
			EXPECT_EQ(std::string(recreated._iIName), std::string(item->_iIName));
			EXPECT_EQ(recreated._iRarePowers, item->_iRarePowers);
			EXPECT_EQ(recreated._iPLDam, item->_iPLDam);
		}
	}
}

TEST_F(Poe2LootTest, UniqueRollsBuildTheChosenUnique)
{
	// Find a unique whose base still drops, and build it from its offset like a loot roll does.
	for (size_t uid = 0; uid < UniqueItems.size(); uid++) {
		const UniqueItem &unique = UniqueItems[uid];
		_item_indexes base = IDI_GOLD;
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iItemId == unique.UIItemId && AllItemsList[i].iMiscId != IMISC_UNIQUE && AllItemsList[i].itype != ItemType::Misc) {
				base = static_cast<_item_indexes>(i);
				break;
			}
		}
		if (base == IDI_GOLD)
			continue;
		const int level = std::max<int>(unique.UIMinLvl, 1);
		// The same list SetupAllItems picks from: this base's uniques at or below the level, in table order.
		std::vector<uint8_t> valid;
		for (size_t other = 0; other < UniqueItems.size(); other++) {
			if (UniqueItems[other].UIItemId == unique.UIItemId && level >= UniqueItems[other].UIMinLvl)
				valid.push_back(static_cast<uint8_t>(other));
		}
		const auto found = std::find(valid.begin(), valid.end(), static_cast<uint8_t>(uid));
		ASSERT_NE(found, valid.end());
		const int offset = static_cast<int>(valid.end() - found) - 1;
		if (offset > 15)
			continue;
		const Item item = MakeDrop(base, ITEM_QUALITY_UNIQUE, 99, level, offset);
		EXPECT_EQ(item._iMagical, ITEM_QUALITY_UNIQUE) << unique.UIName;
		EXPECT_EQ(item._iUid, static_cast<int>(uid)) << unique.UIName;

		Item recreated;
		RecreateItem(*MyPlayer, recreated, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff);
		EXPECT_EQ(recreated._iUid, item._iUid) << unique.UIName;
		return;
	}
	FAIL() << "no droppable unique found";
}

TEST_F(Poe2LootTest, ItemLevelsRunToEightyTwo)
{
	EXPECT_EQ(Poe2ItemLevel(1), 1);
	EXPECT_EQ(Poe2ItemLevel(30), 39) << "the end of Normal";
	EXPECT_EQ(Poe2ItemLevel(30 + 15), 59) << "the end of Nightmare";
	EXPECT_EQ(Poe2ItemLevel(30 + 30, 2), 80) << "a Hell boss";
	EXPECT_EQ(Poe2ItemLevel(80, 2), MaxPoe2ItemLevel);
	EXPECT_EQ(Poe2ItemLevel(0), 1);
}

TEST_F(Poe2LootTest, DropsKeepTheirItemLevel)
{
	const _item_indexes base = FindBase("Shortsword");
	ASSERT_NE(base, IDI_GOLD);
	Item item = {};
	SetLootRarity(item, ITEM_QUALITY_RARE);
	SetPoe2ItemLevel(item, 57);
	SetupAllItems(*MyPlayer, item, base, 1234, 20, 1, true, false);
	EXPECT_EQ(GetPoe2ItemLevel(item), 57);
	EXPECT_EQ(GetLootRarity(item), ITEM_QUALITY_RARE);

	Item recreated;
	RecreateItem(*MyPlayer, recreated, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff);
	EXPECT_EQ(GetPoe2ItemLevel(recreated), 57);
	EXPECT_EQ(recreated._iRarePowers, item._iRarePowers);

	// Older items have none, and flasks and gems use those bits for something else.
	Item old = {};
	SetupAllItems(*MyPlayer, old, base, 1234, 20, 1, true, false);
	EXPECT_EQ(GetPoe2ItemLevel(old), 0);
	Item flask = {};
	flask._itype = ItemType::Misc;
	flask.dwBuff = CF_POE2_FLASK_PREFIX | CF_POE2_FLASK_SUFFIX;
	EXPECT_EQ(GetPoe2ItemLevel(flask), 0);
}

TEST_F(Poe2LootTest, VendorsPayEighteenPercent)
{
	EXPECT_EQ(VendorSellValue(1000), 180);
	EXPECT_EQ(VendorSellValue(3), 1);
	gbIsPoe2 = false;
	EXPECT_EQ(VendorSellValue(1000), 250);
	gbIsPoe2 = true;
}

TEST_F(Poe2LootTest, AdriaOnlySellsCommonCurrency)
{
	for (uint32_t seed = 1; seed <= 40; seed++) {
		SetRndSeed(seed);
		SpawnWitch(16);
		for (const Item &item : WitchItems) {
			EXPECT_FALSE(IsAnyOf(item._iMiscId, IMISC_ORB_ALCHEMY, IMISC_ORB_CHAOS, IMISC_ORB_REGAL, IMISC_ORB_EXALTED, IMISC_ORB_DIVINE, IMISC_ORB_VAAL,
			    IMISC_WAYSTONE, IMISC_UNCUT_SKILL, IMISC_UNCUT_SUPPORT, IMISC_UNCUT_SPIRIT, IMISC_CHARM_GOLDEN, IMISC_CHARM_FIRE))
			    << item._iIName;
			if (item._itype == ItemType::Staff)
				EXPECT_EQ(GetPoe2ItemLevel(item), Poe2ItemLevel(16));
		}
	}
}

/** Breaks `count` barrels on a floor and keeps what they drop. */
std::vector<Item> ContainerDrops(int floor, dungeon_type type, int count, uint32_t firstSeed = 1)
{
	currlevel = static_cast<uint8_t>(floor);
	leveltype = type;
	std::vector<Item> drops;
	for (uint32_t seed = firstSeed; seed < firstSeed + static_cast<uint32_t>(count); seed++) {
		SetRndSeed(seed);
		ActiveItemCount = 0;
		for (int i = 0; i < MAXITEMS; i++)
			ActiveItems[i] = i;
		CreateRndItem({ 40, 40 }, false, false, false);
		for (int i = 0; i < ActiveItemCount; i++)
			drops.push_back(Items[ActiveItems[i]]);
		for (int x = 0; x < MAXDUNX; x++)
			for (int y = 0; y < MAXDUNY; y++)
				dItem[x][y] = 0;
	}
	return drops;
}

size_t CountMisc(const std::vector<Item> &drops, item_misc_id miscId)
{
	return std::count_if(drops.begin(), drops.end(), [miscId](const Item &item) { return item._iMiscId == miscId; });
}

TEST_F(Poe2LootTest, CharmsAndJewelsWaitForPoe2sLevels)
{
	EXPECT_EQ(Poe2AreaDropLevel(IMISC_JEWEL), 20);
	EXPECT_EQ(Poe2AreaDropLevel(IMISC_CHARM_MAGIC), 40) << "Amethyst Charm";
	EXPECT_EQ(Poe2AreaDropLevel(IMISC_CHARM_GOLDEN), 50) << "Golden Charm: the late campaign and maps";
	EXPECT_EQ(Poe2AreaDropLevel(IMISC_CHARM_FIRE), 0) << "the item table's level";

	// Cathedral floor 4 (area level 10 for a barrel): Curtis found jewels, Amethyst Charms and a Golden Charm there.
	const std::vector<Item> cathedral = ContainerDrops(4, DTYPE_CATHEDRAL, 4000);
	EXPECT_EQ(CountMisc(cathedral, IMISC_JEWEL), 0U);
	EXPECT_EQ(CountMisc(cathedral, IMISC_CHARM_MAGIC), 0U);
	EXPECT_EQ(CountMisc(cathedral, IMISC_CHARM_GOLDEN), 0U);
	EXPECT_GT(CountMisc(cathedral, IMISC_CHARM_FIRE) + CountMisc(cathedral, IMISC_CHARM_LIGHTNING), 0U) << "other charms still drop";

	// Hell floor 16 (42): jewels and the Amethyst Charm, still no Golden Charm.
	const std::vector<Item> hell = ContainerDrops(16, DTYPE_HELL, 4000);
	EXPECT_GT(CountMisc(hell, IMISC_JEWEL), 0U);
	EXPECT_GT(CountMisc(hell, IMISC_CHARM_MAGIC), 0U);
	EXPECT_EQ(CountMisc(hell, IMISC_CHARM_GOLDEN), 0U);

	// Past area level 50 the Golden Charm drops too.
	sgGameInitInfo.nDifficulty = DIFF_NIGHTMARE;
	const std::vector<Item> late = ContainerDrops(16, DTYPE_HELL, 4000);
	sgGameInitInfo.nDifficulty = DIFF_NORMAL;
	EXPECT_GT(CountMisc(late, IMISC_CHARM_GOLDEN), 0U);
}

TEST_F(Poe2LootTest, MapsCountAsTheirAreaLevel)
{
	currlevel = 13;
	setlevel = false;
	EXPECT_EQ(LootAreaLevel(30), 30);
	MyPlayer->poe2.mapTier = 3;
	MyPlayer->poe2.mapLevel = 13;
	EXPECT_EQ(LootAreaLevel(30), MapAreaLevelBase + 3);
	MyPlayer->poe2.mapTier = 0;
	MyPlayer->poe2.mapLevel = 0;
}

TEST_F(Poe2LootTest, LineageSupportsOnlyDropInMaps)
{
	EXPECT_FALSE(LineageSupportsDropAt(4)) << "Rathma's Covenant dropped on Cathedral floor 2";
	EXPECT_FALSE(LineageSupportsDropAt(42)) << "nor anywhere in the campaign";
	EXPECT_FALSE(LineageSupportsDropAt(MapAreaLevelBase));
	EXPECT_TRUE(LineageSupportsDropAt(MapAreaLevelBase + 1));

	currlevel = 13;
	setlevel = false;
	EXPECT_FALSE(LineageSupportsDropAt(LootAreaLevel(0)));
	MyPlayer->poe2.mapTier = 1;
	MyPlayer->poe2.mapLevel = 13;
	EXPECT_TRUE(LineageSupportsDropAt(LootAreaLevel(0)));
	MyPlayer->poe2.mapTier = 0;
	MyPlayer->poe2.mapLevel = 0;
	// A chase item: about one map in twenty from any monster (500 kills), on top of the map boss's 2%.
	EXPECT_GE(LineageAnyKillChance, 10000);
}

TEST_F(Poe2LootTest, HellfireRunesNeverDrop)
{
	EXPECT_TRUE(IsHellfireRune(IMISC_RUNEF));
	EXPECT_TRUE(IsHellfireRune(IMISC_GR_RUNEL));
	EXPECT_TRUE(IsHellfireRune(IMISC_RUNES));
	EXPECT_FALSE(IsHellfireRune(IMISC_AUGMENT)) << "POE2's socket runes still drop";
	for (const int floor : { 4, 16 }) {
		const std::vector<Item> drops = ContainerDrops(floor, floor == 4 ? DTYPE_CATHEDRAL : DTYPE_HELL, 6000);
		for (const Item &item : drops)
			EXPECT_FALSE(item.isRune()) << item._iIName;
	}
}

TEST_F(Poe2LootTest, UncutGemsTakeTheAreasLevel)
{
	// Cathedral floor 1 (a barrel's POE2 area level 3): skill gems, but no uncut support gem yet, as in POE2.
	const std::vector<Item> first = ContainerDrops(1, DTYPE_CATHEDRAL, 4000);
	EXPECT_EQ(CountMisc(first, IMISC_UNCUT_SUPPORT), 0U);
	EXPECT_GT(CountMisc(first, IMISC_UNCUT_SKILL), 0U);

	// Floor 4: level 1 supports only (POE2's next support level waits for area 16), skill gems up to the area's.
	const std::vector<Item> fourth = ContainerDrops(4, DTYPE_CATHEDRAL, 4000);
	EXPECT_GT(CountMisc(fourth, IMISC_UNCUT_SUPPORT), 0U);
	for (const Item &item : fourth) {
		if (item._iMiscId == IMISC_UNCUT_SUPPORT)
			EXPECT_EQ(UncutGemLevel(item), 1);
		if (item._iMiscId == IMISC_UNCUT_SKILL)
			EXPECT_EQ(UncutGemLevel(item), UncutGemLevelForArea(8));
	}

	// Hell floor 16 (42): POE2's level 3 support, our gem level 6.
	for (const Item &item : ContainerDrops(16, DTYPE_HELL, 3000)) {
		if (item._iMiscId == IMISC_UNCUT_SUPPORT)
			EXPECT_EQ(UncutGemLevel(item), UncutSupportGemLevel(3));
	}
}

/** Chests and theme rooms on the first floor once crashed: the equipment class table had no room. */
TEST_F(Poe2LootTest, ContainerDropsOnTheFirstFloor)
{
	currlevel = 1;
	leveltype = DTYPE_CATHEDRAL;
	for (uint32_t seed = 1; seed < 300; seed++) {
		SetRndSeed(seed);
		ActiveItemCount = 0;
		for (int i = 0; i < MAXITEMS; i++)
			ActiveItems[i] = i;
		CreateRndItem({ 40, 40 }, true, false, true);
		CreateRndItem({ 40, 40 }, false, false, true);
		CreateRndUseful({ 40, 40 }, false);
		CreateTypeItem({ 40, 40 }, false, ItemType::Gold, IMISC_NONE, false, true);
		for (int x = 0; x < MAXDUNX; x++)
			for (int y = 0; y < MAXDUNY; y++)
				dItem[x][y] = 0;
	}
}

} // namespace

} // namespace devilution
