#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/affix_glow.hpp"
#include "poe2/currency.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2AffixGlowTest : public ::testing::Test {
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
		LoadModifierData();
		ClearAffixGlow();
	}

	void TearDown() override
	{
		ClearAffixGlow();
		gbIsPoe2 = false;
		LoadModifierData();
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

	static Item MakeItem(std::string_view baseName, uint32_t seed, item_quality rarity, int itemLevel)
	{
		Item item = {};
		SetLootRarity(item, rarity);
		SetPoe2ItemLevel(item, itemLevel);
		SetRndSeed(seed);
		SetupAllItems(*MyPlayer, item, FindBase(baseName), AdvanceRndSeed(), 30, 1, /*onlygood=*/true, /*pregen=*/false, 0, /*forceNotUnique=*/true);
		item._iIdentified = true;
		return item;
	}

	static Item Crafted(CurrencyOrb orb, Item item)
	{
		EXPECT_TRUE(ApplyCurrencyOrb(orb, *MyPlayer, item));
		return item;
	}

	static int ModCount(const Item &item)
	{
		int count = 0;
		for (const Poe2ItemMod &mod : item._iPoe2Mods)
			count += mod.family != 0 ? 1 : 0;
		return count;
	}

	static bool Has(const Item &item, const Poe2ItemMod &mod)
	{
		for (const Poe2ItemMod &other : item._iPoe2Mods) {
			if (other.family == mod.family && other.tier == mod.tier && other.values == mod.values)
				return true;
		}
		return false;
	}

	/** A rare with four modifiers. */
	static Item MakeRare()
	{
		return Crafted(CurrencyOrb::Alchemy, MakeItem("Full Plate Mail", 11, ITEM_QUALITY_NORMAL, 70));
	}
};

TEST_F(Poe2AffixGlowTest, GlowHoldsThenEasesOutToNothing)
{
	EXPECT_FLOAT_EQ(AffixGlowCurve(0), 1.F);
	EXPECT_FLOAT_EQ(AffixGlowCurve(AffixGlowHoldMs), 1.F);
	float previous = 1.F;
	for (uint32_t elapsed = AffixGlowHoldMs + 100; elapsed < AffixGlowDurationMs; elapsed += 100) {
		const float strength = AffixGlowCurve(elapsed);
		EXPECT_LT(strength, previous) << elapsed;
		EXPECT_GT(strength, 0.F) << elapsed;
		previous = strength;
	}
	// Ease-out: most of the fading is done by the middle of it.
	EXPECT_LT(AffixGlowCurve(AffixGlowHoldMs + ((AffixGlowDurationMs - AffixGlowHoldMs) / 2)), 0.3F);
	EXPECT_FLOAT_EQ(AffixGlowCurve(AffixGlowDurationMs), 0.F);
	EXPECT_FLOAT_EQ(AffixGlowCurve(AffixGlowDurationMs * 3), 0.F);
}

TEST_F(Poe2AffixGlowTest, ExaltMarksOnlyTheNewModifier)
{
	const Item before = MakeRare();
	ASSERT_EQ(ModCount(before), 4);
	ClearAffixGlow();
	const Item after = Crafted(CurrencyOrb::Exalted, before);
	ASSERT_EQ(ModCount(after), 5);
	const NewAffixLines *glowing = GlowingAffixLines(after);
	ASSERT_NE(glowing, nullptr);
	EXPECT_EQ(std::popcount(glowing->poe2Mods), 1);
	for (size_t i = 0; i < after._iPoe2Mods.size(); i++) {
		if (after._iPoe2Mods[i].family == 0)
			continue;
		EXPECT_EQ(glowing->hasPoe2Mod(i), !Has(before, after._iPoe2Mods[i])) << i;
	}
}

TEST_F(Poe2AffixGlowTest, AlchemyAndTransmutationMarkEveryModifierTheyAdd)
{
	const Item normal = MakeItem("Full Plate Mail", 11, ITEM_QUALITY_NORMAL, 70);
	const Item rare = Crafted(CurrencyOrb::Alchemy, normal);
	const NewAffixLines *glowing = GlowingAffixLines(rare);
	ASSERT_NE(glowing, nullptr);
	EXPECT_EQ(std::popcount(glowing->poe2Mods), ModCount(rare));

	const Item magic = Crafted(CurrencyOrb::Transmutation, normal);
	glowing = GlowingAffixLines(magic);
	ASSERT_NE(glowing, nullptr);
	EXPECT_EQ(std::popcount(glowing->poe2Mods), 1);
	// The glow moved to the newest craft.
	EXPECT_EQ(GlowingAffixLines(rare), nullptr);
}

TEST_F(Poe2AffixGlowTest, ChaosMarksOnlyTheReplacement)
{
	const Item before = MakeRare();
	ClearAffixGlow();
	const Item after = Crafted(CurrencyOrb::Chaos, before);
	const NewAffixLines *glowing = GlowingAffixLines(after);
	ASSERT_NE(glowing, nullptr);
	EXPECT_EQ(std::popcount(glowing->poe2Mods), 1);
	int kept = 0;
	for (size_t i = 0; i < after._iPoe2Mods.size(); i++) {
		if (after._iPoe2Mods[i].family == 0 || glowing->hasPoe2Mod(i))
			continue;
		EXPECT_TRUE(Has(before, after._iPoe2Mods[i])) << i;
		kept++;
	}
	EXPECT_EQ(kept, ModCount(after) - 1);
}

TEST_F(Poe2AffixGlowTest, OrbsThatAddNothingLeaveNoGlow)
{
	const Item rare = MakeRare();
	ClearAffixGlow();
	// Divine rerolls every value, which would make every modifier look new.
	const Item divined = Crafted(CurrencyOrb::Divine, rare);
	EXPECT_EQ(GlowingAffixLines(divined), nullptr);
	const Item annulled = Crafted(CurrencyOrb::Annul, rare);
	EXPECT_EQ(GlowingAffixLines(annulled), nullptr);
	EXPECT_FALSE(MarkNewAffixes(rare, annulled, 100));
	EXPECT_EQ(AffixGlowStrength(100), 0.F);
}

TEST_F(Poe2AffixGlowTest, GlowEndsAfterItsDuration)
{
	const Item before = MakeRare();
	const Item after = Crafted(CurrencyOrb::Exalted, before);
	constexpr uint32_t Start = 50000;
	ASSERT_TRUE(MarkNewAffixes(before, after, Start));
	EXPECT_NE(GlowingAffixLines(after, Start), nullptr);
	EXPECT_FLOAT_EQ(AffixGlowStrength(Start), 1.F);
	EXPECT_NE(GlowingAffixLines(after, Start + AffixGlowDurationMs - 1), nullptr);
	EXPECT_GT(AffixGlowStrength(Start + AffixGlowDurationMs - 1), 0.F);
	EXPECT_EQ(GlowingAffixLines(after, Start + AffixGlowDurationMs), nullptr);
	EXPECT_FLOAT_EQ(AffixGlowStrength(Start + AffixGlowDurationMs), 0.F);
}

TEST_F(Poe2AffixGlowTest, GlowSurvivesTheTimerWrappingAround)
{
	const Item before = MakeRare();
	const Item after = Crafted(CurrencyOrb::Exalted, before);
	constexpr uint32_t Start = std::numeric_limits<uint32_t>::max() - 1000;
	ASSERT_TRUE(MarkNewAffixes(before, after, Start));
	EXPECT_NE(GlowingAffixLines(after, Start + 2000), nullptr);
	EXPECT_EQ(GlowingAffixLines(after, Start + AffixGlowDurationMs), nullptr);
}

TEST_F(Poe2AffixGlowTest, OtherItemsAndLaterChangesDontGlow)
{
	const Item before = MakeRare();
	const Item after = Crafted(CurrencyOrb::Exalted, before);
	ASSERT_TRUE(MarkNewAffixes(before, after, 1000));
	// Another item never shows this glow, even of the same base.
	const Item other = Crafted(CurrencyOrb::Alchemy, MakeItem("Full Plate Mail", 12, ITEM_QUALITY_NORMAL, 70));
	ASSERT_TRUE(MarkNewAffixes(before, after, 1000));
	EXPECT_EQ(GlowingAffixLines(other, 1001), nullptr);
	// Nor the item as it was before the craft, nor once another orb changed its modifiers.
	EXPECT_EQ(GlowingAffixLines(before, 1001), nullptr);
	Item annulled = after;
	ASSERT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Annul, *MyPlayer, annulled));
	EXPECT_EQ(GlowingAffixLines(annulled, 1001), nullptr);
	// The crafted item itself still glows, wherever it's kept: a copy is the same item.
	const Item copy = after;
	EXPECT_NE(GlowingAffixLines(copy, 1001), nullptr);
	ClearAffixGlow();
	EXPECT_EQ(GlowingAffixLines(after, 1001), nullptr);
}

TEST_F(Poe2AffixGlowTest, CombinedLinesShowWhichAreNew)
{
	const Item rare = MakeRare();
	std::vector<bool> fresh;
	const std::vector<std::string> plain = CombinedItemModText(rare._iPoe2Mods);
	const std::vector<std::string> none = CombinedItemModText(rare._iPoe2Mods, 0, fresh);
	EXPECT_EQ(none, plain);
	ASSERT_EQ(fresh.size(), plain.size());
	for (const bool line : fresh)
		EXPECT_FALSE(line);
	CombinedItemModText(rare._iPoe2Mods, 0x3F, fresh);
	ASSERT_EQ(fresh.size(), plain.size());
	for (const bool line : fresh)
		EXPECT_TRUE(line);
	// One new modifier: at least its own line is new, and not every line.
	size_t first = 0;
	while (rare._iPoe2Mods[first].family == 0)
		first++;
	CombinedItemModText(rare._iPoe2Mods, 1U << first, fresh);
	const auto count = std::count(fresh.begin(), fresh.end(), true);
	EXPECT_GE(count, 1);
	EXPECT_LT(static_cast<size_t>(count), fresh.size());
}

} // namespace
} // namespace devilution
