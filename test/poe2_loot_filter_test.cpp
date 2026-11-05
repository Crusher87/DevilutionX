#include <gtest/gtest.h>

#include <cstring>

#include "engine/assets.hpp"
#include "engine/palette.h"
#include "game_mode.hpp"
#include "items.h"
#include "options.h"
#include "poe2/corner_map.hpp"
#include "poe2/loot_filter.hpp"

namespace devilution {
namespace {

Item MakeItem(ItemType type, item_class itemClass, item_quality quality, const char *name = "")
{
	Item item {};
	item._itype = type;
	item._iClass = itemClass;
	item._iMagical = quality;
	std::strncpy(item._iName, name, sizeof(item._iName) - 1);
	return item;
}

class Poe2LootFilterTest : public ::testing::Test {
public:
	static void SetUpTestSuite()
	{
		gbIsPoe2 = true;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadLootFilter();
	}

	static void TearDownTestSuite()
	{
		UnloadModArchives();
		gbIsPoe2 = false;
		LoadLootFilter();
	}

	void SetUp() override
	{
		gbIsPoe2 = true;
		GetOptions().Gameplay.lootFilter.SetValue(LootFilter::Off);
	}

	void TearDown() override
	{
		GetOptions().Gameplay.lootFilter.SetValue(LootFilter::Off);
	}
};

TEST_F(Poe2LootFilterTest, TablesLoadFromThePoe2Mod)
{
	const LootStyle *divine = FindLootStyle("currency_s");
	ASSERT_NE(divine, nullptr);
	EXPECT_EQ(divine->text, UiFlags::ColorRed);
	EXPECT_EQ(divine->background, (LootRgb { 255, 255, 255 }));
	EXPECT_TRUE(divine->big);
	EXPECT_TRUE(divine->beam.has_value());
	EXPECT_EQ(divine->iconShape, MapIconShape::Star);
	EXPECT_EQ(divine->hideFrom, LootFilter::Off) << "the best currency never hides";
	ASSERT_NE(FindLootStyle("unknown"), nullptr);
	ASSERT_NE(FindLootStyle("currency_e"), nullptr);
	EXPECT_TRUE(FindLootStyle("currency_e")->beamTemp);
}

TEST_F(Poe2LootFilterTest, ItemsFallInNeverSinksTiers)
{
	EXPECT_EQ(LootFilterStyleKey(MakeItem(ItemType::Misc, ICLASS_MISC, ITEM_QUALITY_NORMAL, "Divine Orb")), "currency_s");
	EXPECT_EQ(LootFilterStyleKey(MakeItem(ItemType::Misc, ICLASS_MISC, ITEM_QUALITY_NORMAL, "Chaos Orb")), "currency_b");
	EXPECT_EQ(LootFilterStyleKey(MakeItem(ItemType::Misc, ICLASS_MISC, ITEM_QUALITY_NORMAL, "Greater Chaos Orb")), "currency_a");
	EXPECT_EQ(LootFilterStyleKey(MakeItem(ItemType::Misc, ICLASS_MISC, ITEM_QUALITY_NORMAL, "Orb of Transmutation")), "currency_supplymagic");
	EXPECT_EQ(LootFilterStyleKey(MakeItem(ItemType::Misc, ICLASS_MISC, ITEM_QUALITY_NORMAL, "Greater Essence of Ice")), "essence_c");
	EXPECT_EQ(LootFilterStyleKey(MakeItem(ItemType::Misc, ICLASS_MISC, ITEM_QUALITY_NORMAL, "Greater Ember Rune")), "socket_d");
	EXPECT_EQ(LootFilterStyleKey(MakeItem(ItemType::Misc, ICLASS_MISC, ITEM_QUALITY_NORMAL, "Ember Rune")), "socket_e");
	EXPECT_EQ(LootFilterStyleKey(MakeItem(ItemType::Misc, ICLASS_MISC, ITEM_QUALITY_NORMAL, "Omen of Corruption")), "omen");
	EXPECT_EQ(LootFilterStyleKey(MakeItem(ItemType::Sword, ICLASS_WEAPON, ITEM_QUALITY_NORMAL)), "normal");
	EXPECT_EQ(LootFilterStyleKey(MakeItem(ItemType::Helm, ICLASS_ARMOR, ITEM_QUALITY_MAGIC)), "magic");
	EXPECT_EQ(LootFilterStyleKey(MakeItem(ItemType::Ring, ICLASS_MISC, ITEM_QUALITY_RARE)), "rare_jewellery");
	EXPECT_EQ(LootFilterStyleKey(MakeItem(ItemType::Axe, ICLASS_WEAPON, ITEM_QUALITY_RARE)), "rare");
	EXPECT_EQ(LootFilterStyleKey(MakeItem(ItemType::Sword, ICLASS_QUEST, ITEM_QUALITY_NORMAL, "Divine Orb")), "quest") << "quest items go by their kind";
}

TEST_F(Poe2LootFilterTest, OffShowsEverything)
{
	EXPECT_FALSE(IsItemHiddenByLootFilter(MakeItem(ItemType::Sword, ICLASS_WEAPON, ITEM_QUALITY_NORMAL), LootFilter::Off));
	EXPECT_EQ(LootFilterStyle(MakeItem(ItemType::Misc, ICLASS_MISC, ITEM_QUALITY_NORMAL, "Divine Orb")), nullptr);
}

TEST_F(Poe2LootFilterTest, StricterLevelsHideMore)
{
	const Item normalSword = MakeItem(ItemType::Sword, ICLASS_WEAPON, ITEM_QUALITY_NORMAL);
	const Item magicHelm = MakeItem(ItemType::Helm, ICLASS_ARMOR, ITEM_QUALITY_MAGIC);
	const Item rareAxe = MakeItem(ItemType::Axe, ICLASS_WEAPON, ITEM_QUALITY_RARE);
	const Item transmute = MakeItem(ItemType::Misc, ICLASS_MISC, ITEM_QUALITY_NORMAL, "Orb of Transmutation");
	EXPECT_FALSE(IsItemHiddenByLootFilter(normalSword, LootFilter::Soft)) << "Soft shows everything";
	EXPECT_TRUE(IsItemHiddenByLootFilter(normalSword, LootFilter::Regular));
	EXPECT_FALSE(IsItemHiddenByLootFilter(magicHelm, LootFilter::SemiStrict));
	EXPECT_TRUE(IsItemHiddenByLootFilter(magicHelm, LootFilter::Strict));
	EXPECT_FALSE(IsItemHiddenByLootFilter(transmute, LootFilter::SemiStrict));
	EXPECT_TRUE(IsItemHiddenByLootFilter(transmute, LootFilter::Strict));
	EXPECT_FALSE(IsItemHiddenByLootFilter(rareAxe, LootFilter::Strict));
	EXPECT_TRUE(IsItemHiddenByLootFilter(rareAxe, LootFilter::VeryStrict));
}

TEST_F(Poe2LootFilterTest, ValuableItemsAlwaysShow)
{
	for (const LootFilter filter : { LootFilter::Soft, LootFilter::Strict, LootFilter::UberPlusStrict }) {
		EXPECT_FALSE(IsItemHiddenByLootFilter(MakeItem(ItemType::Misc, ICLASS_MISC, ITEM_QUALITY_NORMAL, "Divine Orb"), filter));
		EXPECT_FALSE(IsItemHiddenByLootFilter(MakeItem(ItemType::Sword, ICLASS_QUEST, ITEM_QUALITY_NORMAL), filter));
		EXPECT_FALSE(IsItemHiddenByLootFilter(MakeItem(ItemType::Misc, ICLASS_MISC, ITEM_QUALITY_NORMAL, "Orb of Annulment"), filter));
	}
}

TEST_F(Poe2LootFilterTest, OutsidePoe2NothingIsFiltered)
{
	gbIsPoe2 = false;
	EXPECT_FALSE(IsItemHiddenByLootFilter(MakeItem(ItemType::Sword, ICLASS_WEAPON, ITEM_QUALITY_NORMAL), LootFilter::UberPlusStrict));
	gbIsPoe2 = true;
}

TEST_F(Poe2LootFilterTest, GroundItemsGetARarityOutlineInPoe2)
{
	gbIsPoe2 = false;
	EXPECT_FALSE(GroundItemOutlineColor(MakeItem(ItemType::Sword, ICLASS_WEAPON, ITEM_QUALITY_RARE)).has_value());
	gbIsPoe2 = true;
	EXPECT_FALSE(GroundItemOutlineColor(Item {}).has_value());
	EXPECT_EQ(GroundItemOutlineColor(MakeItem(ItemType::Sword, ICLASS_WEAPON, ITEM_QUALITY_NORMAL)), PAL16_GRAY + 9);
	EXPECT_EQ(GroundItemOutlineColor(MakeItem(ItemType::Helm, ICLASS_ARMOR, ITEM_QUALITY_MAGIC)), PAL16_BLUE + 6);
	EXPECT_EQ(GroundItemOutlineColor(MakeItem(ItemType::Axe, ICLASS_WEAPON, ITEM_QUALITY_RARE)), PAL16_YELLOW + 4);
	EXPECT_EQ(GroundItemOutlineColor(MakeItem(ItemType::Amulet, ICLASS_MISC, ITEM_QUALITY_UNIQUE)), PAL16_ORANGE + 4);
	EXPECT_EQ(GroundItemOutlineColor(MakeItem(ItemType::Gold, ICLASS_GOLD, ITEM_QUALITY_NORMAL)), PAL16_YELLOW + 8);
}

TEST_F(Poe2LootFilterTest, WithTheFilterOnTheOutlineTakesTheFilterColour)
{
	GetOptions().Gameplay.lootFilter.SetValue(LootFilter::Soft);
	const Item divine = MakeItem(ItemType::Misc, ICLASS_MISC, ITEM_QUALITY_NORMAL, "Divine Orb");
	ASSERT_NE(LootFilterStyle(divine), nullptr);
	EXPECT_EQ(GroundItemOutlineColor(divine), NearestLootPaletteColour({ 255, 0, 0 }));
	// No border or background: the rarity outline stays.
	EXPECT_EQ(GroundItemOutlineColor(MakeItem(ItemType::Helm, ICLASS_ARMOR, ITEM_QUALITY_MAGIC)), PAL16_BLUE + 6);
}

TEST_F(Poe2LootFilterTest, TempBeamsRunOut)
{
	GetOptions().Gameplay.lootFilter.SetValue(LootFilter::Soft);
	const Item etcher = MakeItem(ItemType::Misc, ICLASS_MISC, ITEM_QUALITY_NORMAL, "Arcanist's Etcher");
	EXPECT_TRUE(LootBeamColour(etcher, 0).has_value());
	EXPECT_FALSE(LootBeamColour(etcher, LootTempBeamMs + 1).has_value());
	const Item divine = MakeItem(ItemType::Misc, ICLASS_MISC, ITEM_QUALITY_NORMAL, "Divine Orb");
	EXPECT_TRUE(LootBeamColour(divine, 100000).has_value()) << "top tiers keep their beam";
	GetOptions().Gameplay.lootFilterBeams.SetValue(false);
	EXPECT_FALSE(LootBeamColour(divine, 0).has_value());
	GetOptions().Gameplay.lootFilterBeams.SetValue(true);
}

TEST_F(Poe2LootFilterTest, NeverSinksIconShapes)
{
	for (const MapIconShape shape : { MapIconShape::Star, MapIconShape::Hexagon, MapIconShape::Pentagon, MapIconShape::Kite }) {
		EXPECT_TRUE(IsInMapIconShape(shape, 0, 0, 5));
		EXPECT_FALSE(IsInMapIconShape(shape, 5, 5, 5));
	}
	EXPECT_TRUE(IsInMapIconShape(MapIconShape::Star, 0, -4, 5)) << "the star's top point";
	EXPECT_FALSE(IsInMapIconShape(MapIconShape::Star, 2, -4, 5)) << "between two points";
	EXPECT_TRUE(IsInMapIconShape(MapIconShape::Moon, -4, 0, 5));
	EXPECT_FALSE(IsInMapIconShape(MapIconShape::Moon, 3, 0, 5)) << "the crescent's hollow";
}

} // namespace
} // namespace devilution
