#include <algorithm>
#include <string>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "panels/item_spawner.hpp"
#include "player.h"
#include "spells.h"

namespace devilution {
namespace {

class Poe2ItemSpawnerTest : public ::testing::Test {
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
		catalog = BuildItemSpawnerCatalog();
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
	}

	std::vector<std::string> Names(std::string_view query, SpawnerCategory category = SpawnerCategory::All) const
	{
		std::vector<std::string> names;
		for (const size_t i : FilterItemSpawnerCatalog(catalog, query, category))
			names.push_back(catalog[i].name);
		return names;
	}

	const SpawnerEntry &Entry(std::string_view name) const
	{
		const auto it = std::find_if(catalog.begin(), catalog.end(), [name](const SpawnerEntry &entry) { return entry.name == name; });
		EXPECT_NE(it, catalog.end()) << name;
		return *it;
	}

	std::vector<SpawnerEntry> catalog;
};

bool Contains(const std::vector<std::string> &names, std::string_view name)
{
	return std::find(names.begin(), names.end(), name) != names.end();
}

TEST_F(Poe2ItemSpawnerTest, SearchMatchesEveryWordCaseInsensitively)
{
	const std::vector<std::string> bows = Names("BOW");
	EXPECT_TRUE(Contains(bows, "Long Bow"));
	EXPECT_TRUE(Contains(bows, "Short Bow"));
	EXPECT_FALSE(Contains(bows, "Long Sword"));

	EXPECT_TRUE(Contains(Names("cleave"), "Cleave Gem"));
	EXPECT_TRUE(Contains(Names("long bow"), "Long Bow"));
	EXPECT_FALSE(Contains(Names("long bow"), "Short Bow"));
	EXPECT_EQ(Names("").size(), catalog.size());
}

TEST_F(Poe2ItemSpawnerTest, CategoriesFilterResults)
{
	EXPECT_TRUE(Contains(Names("", SpawnerCategory::Gems), "Arcing Sparks Gem"));
	EXPECT_FALSE(Contains(Names("", SpawnerCategory::Gems), "Long Bow"));
	EXPECT_TRUE(Contains(Names("", SpawnerCategory::Orbs), "Chaos Orb"));
	EXPECT_TRUE(Contains(Names("", SpawnerCategory::Uniques), "The Rift Bow"));
	EXPECT_TRUE(Contains(Names("", SpawnerCategory::Weapons), "Long Bow"));
	EXPECT_TRUE(Contains(Names("", SpawnerCategory::Jewelry), "Ring"));
	EXPECT_FALSE(Names("potion", SpawnerCategory::Misc).empty());
}

TEST_F(Poe2ItemSpawnerTest, SpawnsGemsAndRolledItems)
{
	Item gem;
	ASSERT_TRUE(CreateSpawnerItem(*MyPlayer, Entry("Arcing Sparks Gem"), ITEM_QUALITY_RARE, 30, 42, gem));
	EXPECT_EQ(gem.IDidx, Entry("Arcing Sparks Gem").baseIndex);

	Item bow;
	ASSERT_TRUE(CreateSpawnerItem(*MyPlayer, Entry("Long Bow"), ITEM_QUALITY_RARE, 30, 42, bow));
	EXPECT_EQ(bow._iMagical, ITEM_QUALITY_RARE);
}

TEST_F(Poe2ItemSpawnerTest, SpawnsTheChosenUniqueAndItSurvivesSaving)
{
	for (const std::string_view name : { "The Rift Bow", "Constricting Ring", "Gnat Sting" }) {
		const SpawnerEntry &entry = Entry(name);
		ASSERT_GE(entry.uniqueId, 0) << name;
		Item item;
		ASSERT_TRUE(CreateSpawnerItem(*MyPlayer, entry, ITEM_QUALITY_NORMAL, 1, 7, item)) << name;
		EXPECT_EQ(item._iMagical, ITEM_QUALITY_UNIQUE) << name;
		EXPECT_EQ(item._iUid, entry.uniqueId) << name;

		Item recreated;
		RecreateItem(*MyPlayer, recreated, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff);
		EXPECT_EQ(recreated._iUid, item._iUid) << name;
		EXPECT_EQ(std::string(recreated._iIName), std::string(item._iIName)) << name;
	}
}

} // namespace
} // namespace devilution
