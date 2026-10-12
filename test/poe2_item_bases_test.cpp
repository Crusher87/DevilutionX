#include <string_view>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/defenses.hpp"
#include "spells.h"

namespace devilution {
namespace {

class Poe2ItemBasesTest : public ::testing::Test {
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
			if (AllItemsList[i].iName == name)
				return static_cast<_item_indexes>(i);
		}
		return IDI_GOLD;
	}
};

TEST_F(Poe2ItemBasesTest, NewBasesDrop)
{
	for (const std::string_view name : { "Wrapped Quarterstaff", "Aegis Quarterstaff", "Makeshift Crossbow", "Gemini Crossbow", "Rattling Sceptre", "Omen Sceptre", "Twig Focus", "Crystal Focus" }) {
		const _item_indexes base = FindBase(name);
		ASSERT_NE(base, IDI_GOLD) << name;
		EXPECT_GT(AllItemsList[base].dropRate, 0) << name;
		EXPECT_TRUE(IsItemAvailable(base)) << name;
	}
	EXPECT_EQ(AllItemsList[FindBase("Siege Crossbow")].itype, ItemType::Bow);
	EXPECT_EQ(AllItemsList[FindBase("Omen Sceptre")].itype, ItemType::Mace);
	EXPECT_EQ(AllItemsList[FindBase("Woven Focus")].itype, ItemType::Shield);
}

TEST_F(Poe2ItemBasesTest, QuarterstaffsRollWeaponAffixesNotSpells)
{
	const _item_indexes base = FindBase("Long Quarterstaff");
	ASSERT_NE(base, IDI_GOLD);
	int magic = 0;
	for (uint32_t seed = 1; seed <= 300; seed++) {
		Item item {};
		item._iMiscId = AllItemsList[base].iMiscId;
		SetRndSeed(seed);
		SetupAllItems(*MyPlayer, item, base, AdvanceRndSeed(), 30, 1, /*onlygood=*/true, /*pregen=*/false, 0, /*forceNotUnique=*/true);
		EXPECT_EQ(item._iSpell, SpellID::Null);
		EXPECT_EQ(item._iMaxCharges, 0);
		if (item._iMagical != ITEM_QUALITY_NORMAL)
			magic++;
	}
	EXPECT_GT(magic, 0);
}

TEST_F(Poe2ItemBasesTest, FociGiveEnergyShield)
{
	Item focus {};
	InitializeItem(focus, FindBase("Crystal Focus"));
	EXPECT_EQ(ItemDefenseType(focus), DefenseType::EnergyShield);
}

} // namespace
} // namespace devilution
