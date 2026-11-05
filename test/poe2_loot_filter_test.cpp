#include <gtest/gtest.h>

#include "engine/palette.h"
#include "game_mode.hpp"
#include "items.h"
#include "poe2/loot_filter.hpp"

namespace devilution {
namespace {

Item MakeItem(ItemType type, item_class itemClass, item_quality quality)
{
	Item item {};
	item._itype = type;
	item._iClass = itemClass;
	item._iMagical = quality;
	return item;
}

TEST(Poe2LootFilterTest, OffShowsEverything)
{
	EXPECT_FALSE(IsItemHiddenByLootFilter(MakeItem(ItemType::Sword, ICLASS_WEAPON, ITEM_QUALITY_NORMAL), LootFilter::Off));
}

TEST(Poe2LootFilterTest, HidesLowQualityGear)
{
	const Item normalSword = MakeItem(ItemType::Sword, ICLASS_WEAPON, ITEM_QUALITY_NORMAL);
	const Item magicHelm = MakeItem(ItemType::Helm, ICLASS_ARMOR, ITEM_QUALITY_MAGIC);
	const Item normalRing = MakeItem(ItemType::Ring, ICLASS_MISC, ITEM_QUALITY_NORMAL);
	EXPECT_TRUE(IsItemHiddenByLootFilter(normalSword, LootFilter::HideNormal));
	EXPECT_TRUE(IsItemHiddenByLootFilter(normalRing, LootFilter::HideNormal));
	EXPECT_FALSE(IsItemHiddenByLootFilter(magicHelm, LootFilter::HideNormal));
	EXPECT_TRUE(IsItemHiddenByLootFilter(magicHelm, LootFilter::HideNormalAndMagic));
}

TEST(Poe2LootFilterTest, ValuableItemsAlwaysShow)
{
	for (const LootFilter filter : { LootFilter::HideNormal, LootFilter::HideNormalAndMagic }) {
		EXPECT_FALSE(IsItemHiddenByLootFilter(MakeItem(ItemType::Gold, ICLASS_GOLD, ITEM_QUALITY_NORMAL), filter));
		EXPECT_FALSE(IsItemHiddenByLootFilter(MakeItem(ItemType::Misc, ICLASS_MISC, ITEM_QUALITY_NORMAL), filter));
		EXPECT_FALSE(IsItemHiddenByLootFilter(MakeItem(ItemType::Axe, ICLASS_WEAPON, ITEM_QUALITY_RARE), filter));
		EXPECT_FALSE(IsItemHiddenByLootFilter(MakeItem(ItemType::Amulet, ICLASS_MISC, ITEM_QUALITY_UNIQUE), filter));
		EXPECT_FALSE(IsItemHiddenByLootFilter(MakeItem(ItemType::Sword, ICLASS_QUEST, ITEM_QUALITY_NORMAL), filter));
	}
}

TEST(Poe2LootFilterTest, GroundItemsGetARarityOutlineInPoe2)
{
	const bool wasPoe2 = gbIsPoe2;
	gbIsPoe2 = false;
	EXPECT_FALSE(GroundItemOutlineColor(MakeItem(ItemType::Sword, ICLASS_WEAPON, ITEM_QUALITY_RARE)).has_value());
	gbIsPoe2 = true;
	EXPECT_FALSE(GroundItemOutlineColor(Item {}).has_value());
	EXPECT_EQ(GroundItemOutlineColor(MakeItem(ItemType::Sword, ICLASS_WEAPON, ITEM_QUALITY_NORMAL)), PAL16_GRAY + 9);
	EXPECT_EQ(GroundItemOutlineColor(MakeItem(ItemType::Helm, ICLASS_ARMOR, ITEM_QUALITY_MAGIC)), PAL16_BLUE + 6);
	EXPECT_EQ(GroundItemOutlineColor(MakeItem(ItemType::Axe, ICLASS_WEAPON, ITEM_QUALITY_RARE)), PAL16_YELLOW + 4);
	EXPECT_EQ(GroundItemOutlineColor(MakeItem(ItemType::Amulet, ICLASS_MISC, ITEM_QUALITY_UNIQUE)), PAL16_ORANGE + 4);
	EXPECT_EQ(GroundItemOutlineColor(MakeItem(ItemType::Gold, ICLASS_GOLD, ITEM_QUALITY_NORMAL)), PAL16_YELLOW + 8);
	gbIsPoe2 = wasPoe2;
}

TEST(Poe2LootFilterTest, GroundOutlinesFadeIntoTheDark)
{
	EXPECT_EQ(LitGroundOutline(PAL16_BLUE + 6, 0), PAL16_BLUE + 6);
	EXPECT_EQ(LitGroundOutline(PAL16_BLUE + 6, 6), PAL16_BLUE + 9);
	EXPECT_EQ(LitGroundOutline(PAL16_GRAY + 9, 11), PAL16_GRAY + 14);
	EXPECT_FALSE(LitGroundOutline(PAL16_YELLOW + 4, 12).has_value());
	EXPECT_FALSE(LitGroundOutline(PAL16_YELLOW + 4, 15).has_value());
}

} // namespace
} // namespace devilution
