#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/item_search.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2ItemSearchTest : public ::testing::Test {
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
		// The tooltip lines are only written when the game draws.
		HeadlessMode = false;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadSpellData();
		LoadPlayerDataFiles();
		LoadModifierData();
	}

	void TearDown() override
	{
		HeadlessMode = true;
		gbIsPoe2 = false;
		LoadModifierData();
		UnloadModArchives();
	}

	static Item MakeItem(std::string_view baseName, item_quality rarity)
	{
		_item_indexes base = IDI_GOLD;
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == baseName && AllItemsList[i].dropRate > 0)
				base = static_cast<_item_indexes>(i);
		}
		Item item = {};
		SetLootRarity(item, rarity);
		SetPoe2ItemLevel(item, 30);
		SetRndSeed(1234);
		SetupAllItems(*MyPlayer, item, base, AdvanceRndSeed(), 30, 1, /*onlygood=*/true, /*pregen=*/false, 0, /*forceNotUnique=*/true);
		item._iIdentified = true;
		return item;
	}
};

TEST_F(Poe2ItemSearchTest, ParsesWordsQuotesAlternativesAndNegation)
{
	const std::vector<ItemSearchTerm> terms = ParseItemSearch("Fire \"of the\" !Ring bow|staff");
	ASSERT_EQ(terms.size(), 4);
	EXPECT_EQ(terms[0].alternatives, std::vector<std::string> { "fire" });
	EXPECT_EQ(terms[1].alternatives, std::vector<std::string> { "of the" });
	EXPECT_TRUE(terms[2].negated);
	EXPECT_EQ(terms[2].alternatives, std::vector<std::string> { "ring" });
	EXPECT_EQ(terms[3].alternatives, (std::vector<std::string> { "bow", "staff" }));
	EXPECT_TRUE(ParseItemSearch("   ").empty());
}

TEST_F(Poe2ItemSearchTest, TextMustHoldEveryWord)
{
	const std::string text = "storm string\nshort bow\nrare bow weapon\n+12% to fire resistance";
	EXPECT_TRUE(ItemSearchTextMatches(text, ParseItemSearch("bow fire")));
	EXPECT_FALSE(ItemSearchTextMatches(text, ParseItemSearch("bow lightning")));
	EXPECT_TRUE(ItemSearchTextMatches(text, ParseItemSearch("lightning|fire")));
	EXPECT_FALSE(ItemSearchTextMatches(text, ParseItemSearch("!rare")));
	EXPECT_TRUE(ItemSearchTextMatches(text, ParseItemSearch("\"fire resistance\"")));
	EXPECT_FALSE(ItemSearchTextMatches(text, ParseItemSearch("\"resistance fire\"")));
}

TEST_F(Poe2ItemSearchTest, ItemsMatchByBaseKindRarityAndTooltip)
{
	const Item hatchet = MakeItem("Dull Hatchet", ITEM_QUALITY_RARE);
	ASSERT_EQ(hatchet._iMagical, ITEM_QUALITY_RARE);
	const std::string text = ItemSearchText(hatchet);
	EXPECT_NE(text.find("dull hatchet"), std::string::npos) << text;
	EXPECT_NE(text.find("rare axe weapon"), std::string::npos) << text;
	EXPECT_NE(text.find("damage"), std::string::npos) << "the tooltip lines are searched too: " << text;

	ItemSearchBox box;
	EXPECT_FALSE(box.searching());
	EXPECT_TRUE(box.matches(hatchet));
	box.setQuery("HATCHET rare");
	EXPECT_TRUE(box.searching());
	EXPECT_TRUE(box.matches(hatchet));
	box.setQuery("hatchet !rare");
	EXPECT_FALSE(box.matches(hatchet));
	box.setQuery("ring|amulet");
	EXPECT_FALSE(box.matches(hatchet));
}

} // namespace
} // namespace devilution
