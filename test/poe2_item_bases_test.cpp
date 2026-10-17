#include <string_view>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/bases.hpp"
#include "poe2/defenses.hpp"
#include "poe2/item_stats.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
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
		LoadModifierData();
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		LoadModifierData();
		UnloadModArchives();
	}

	static _item_indexes FindBase(std::string_view name)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == name)
				return static_cast<_item_indexes>(i);
		}
		ADD_FAILURE() << name;
		return IDI_GOLD;
	}

	/** A normal item of the base, built the way a drop is. */
	static Item MakeItem(std::string_view name, uint32_t seed, int quality = 0)
	{
		Item item {};
		SetLootRarity(item, ITEM_QUALITY_NORMAL);
		SetItemQuality(item, quality);
		SetRndSeed(seed);
		SetupAllItems(*MyPlayer, item, FindBase(name), AdvanceRndSeed(), 30, 1, /*onlygood=*/false, /*pregen=*/false, 0, /*forceNotUnique=*/true);
		return item;
	}

	static int Stat(const Item &item, Poe2ItemStat stat)
	{
		return item._iPoe2Stats[static_cast<size_t>(stat)];
	}
};

TEST_F(Poe2ItemBasesTest, BundleBasesReplaceTheOldOnes)
{
	for (const std::string_view name : { "Shortsword", "Heavy Imperial Edge", "Ash Staff", "Light Crossbow", "Quick Bone Sceptre", "Wooden Focus", "Iron Cap", "Hide Buckler", "Ruby Ring", "Sage Amulet" }) {
		const _item_indexes base = FindBase(name);
		EXPECT_GT(AllItemsList[base].dropRate, 0) << name;
		EXPECT_TRUE(IsItemAvailable(base)) << name;
		EXPECT_NE(BaseMetaFor(base), nullptr) << name;
	}
	// The old bases stay for items that already exist, but no longer drop.
	for (const std::string_view name : { "Long Sword", "Wrapped Quarterstaff", "Siege Crossbow", "Omen Sceptre", "Crystal Focus", "Great Helm", "Ring" })
		EXPECT_EQ(AllItemsList[FindBase(name)].dropRate, 0) << name;
	EXPECT_EQ(AllItemsList[FindBase("Light Crossbow")].itype, ItemType::Bow);
	EXPECT_EQ(AllItemsList[FindBase("Quick Bone Sceptre")].itype, ItemType::Mace);
	EXPECT_EQ(AllItemsList[FindBase("Wooden Focus")].itype, ItemType::Shield);
	// Diablo's uniques keep dropping on the new bases.
	EXPECT_EQ(AllItemsList[FindBase("Fine Short Bow")].iItemId, UITYPE_SHORTBOW);
	EXPECT_EQ(AllItemsList[FindBase("Ruby Ring")].iItemId, UITYPE_RING);
}

TEST_F(Poe2ItemBasesTest, EveryBundleBaseIsUsable)
{
	int bases = 0;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		const BaseMeta *meta = BaseMetaFor(static_cast<int>(i));
		if (meta == nullptr)
			continue;
		bases++;
		const ItemData &data = AllItemsList[i];
		EXPECT_GT(data.dropRate, 0) << data.iName;
		if (data.iClass == ICLASS_WEAPON)
			EXPECT_GT(data.iMaxDam, data.iMinDam) << data.iName;
		if (data.iClass == ICLASS_ARMOR) {
			EXPECT_GT(data.iMinAC, 0) << data.iName;
			EXPECT_TRUE(meta->defense.has_value()) << data.iName;
		}
	}
	EXPECT_EQ(bases, 384);
}

TEST_F(Poe2ItemBasesTest, HeavyAndSwiftBasesTradeDamageForSpeed)
{
	const Item heavy = MakeItem("Heavy Shortsword", 1);
	const Item balanced = MakeItem("Shortsword", 1);
	const Item swift = MakeItem("Swift Shortsword", 1);
	EXPECT_LT(Stat(heavy, Poe2ItemStat::AttackSpeed), 0);
	EXPECT_EQ(Stat(balanced, Poe2ItemStat::AttackSpeed), 0);
	EXPECT_GE(Stat(swift, Poe2ItemStat::AttackSpeed), 10);
	EXPECT_GT(heavy._iMinDam + heavy._iMaxDam, swift._iMinDam + swift._iMaxDam);

	const Item quick = MakeItem("Quick Pilgrim Staff", 1);
	const Item focused = MakeItem("Focused Pilgrim Staff", 1);
	EXPECT_GT(Stat(quick, Poe2ItemStat::CastSpeed), Stat(focused, Poe2ItemStat::CastSpeed));
	// One implicit each (Curtis 2026-10-16): the quick staff casts faster, the focused one gives Intelligence.
	EXPECT_GT(focused._iPLMag, quick._iPLMag);
}

TEST_F(Poe2ItemBasesTest, ImplicitsRollFromTheSeedAndSurviveRebuilding)
{
	bool varied = false;
	int first = -1;
	for (uint32_t seed = 1; seed <= 40; seed++) {
		const Item ring = MakeItem("Ruby Ring", seed);
		EXPECT_GE(ring._iPLFR, 12);
		EXPECT_LE(ring._iPLFR, 16);
		Item rebuilt;
		RecreateItem(*MyPlayer, rebuilt, ring.IDidx, ring._iCreateInfo, ring._iSeed, ring._ivalue, ring.dwBuff);
		EXPECT_EQ(rebuilt._iPLFR, ring._iPLFR);
		varied = varied || (first >= 0 && ring._iPLFR != first);
		first = ring._iPLFR;
	}
	EXPECT_TRUE(varied);
	EXPECT_EQ(MakeItem("Iron Ring", 1)._iPLDamMod, 3);
	ASSERT_EQ(ItemImplicitText(MakeItem("Iron Ring", 1)).size(), 1U);
	EXPECT_EQ(ItemImplicitText(MakeItem("Iron Ring", 1))[0], "Adds 2 to 4 Physical Damage to Attacks");
}

TEST_F(Poe2ItemBasesTest, DefenceLinesComeFromTheBase)
{
	EXPECT_EQ(ItemDefenseType(MakeItem("Iron Cap", 1)), DefenseType::Armour);
	EXPECT_EQ(ItemDefenseType(MakeItem("Leather Hood", 1)), DefenseType::Evasion);
	EXPECT_EQ(ItemDefenseType(MakeItem("Cloth Circlet", 1)), DefenseType::EnergyShield);
	EXPECT_EQ(ItemDefenseType(MakeItem("Brigand Helm", 1)), DefenseType::ArmourEvasion);
	EXPECT_EQ(ItemDefenseType(MakeItem("Veiled Hood", 1)), DefenseType::EvasionEnergyShield);
	EXPECT_EQ(ItemDefenseType(MakeItem("Pilgrim Mask", 1)), DefenseType::All);
	EXPECT_EQ(ItemDefenseType(MakeItem("Wooden Focus", 1)), DefenseType::EnergyShield);
	EXPECT_EQ(ItemModBaseTags(MakeItem("Leather Hood", 1)), ModBaseEvasion);
	EXPECT_EQ(ItemModSlot(MakeItem("Wooden Focus", 1)), ModSlot::Focus);
	EXPECT_EQ(ItemModSlot(MakeItem("Shiv", 1)), ModSlot::Dagger);
	EXPECT_EQ(ItemModSlot(MakeItem("Sledge", 1)), ModSlot::TwoHandMelee);
}

TEST_F(Poe2ItemBasesTest, QualityRaisesTheBasesOwnStat)
{
	const Item sword = MakeItem("Shortsword", 3, 15);
	EXPECT_EQ(ItemQuality(sword), 15);
	EXPECT_EQ(sword._iPLDam, 15);
	EXPECT_FALSE(IsExceptional(sword));
	Item rebuilt;
	RecreateItem(*MyPlayer, rebuilt, sword.IDidx, sword._iCreateInfo, sword._iSeed, sword._ivalue, sword.dwBuff);
	EXPECT_EQ(rebuilt._iPLDam, 15);

	const Item helm = MakeItem("Iron Cap", 3, 25);
	EXPECT_EQ(helm._iPLAC, 25);
	EXPECT_TRUE(IsExceptional(helm));

	const Item plain = MakeItem("Quick Ashen Staff", 3);
	const Item fine = MakeItem("Quick Ashen Staff", 3, 20);
	EXPECT_EQ(fine._iPLMag, plain._iPLMag + (plain._iPLMag * 20 / 100));
	EXPECT_EQ(fine._iPLDam, 0);

	// Rings have no quality.
	EXPECT_FALSE(HasQuality(MakeItem("Ruby Ring", 3)));
}

TEST_F(Poe2ItemBasesTest, QuarterstaffsRollWeaponAffixesNotSpells)
{
	const _item_indexes base = FindBase("Ash Staff");
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

} // namespace
} // namespace devilution
