#include <algorithm>
#include <array>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/currency.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2ModifiersTest : public ::testing::Test {
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
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		LoadModifierData();
		UnloadModArchives();
	}

	/** Makes a dropped item of the given base, rarity and POE2 item level, the way the loot rules do. */
	static Item MakeItem(_item_indexes base, uint32_t seed, item_quality rarity, int itemLevel)
	{
		Item item = {};
		SetLootRarity(item, rarity);
		SetPoe2ItemLevel(item, itemLevel);
		SetRndSeed(seed);
		SetupAllItems(*MyPlayer, item, base, AdvanceRndSeed(), 30, 1, /*onlygood=*/true, /*pregen=*/false, 0, /*forceNotUnique=*/true);
		item._iIdentified = true;
		return item;
	}

	static _item_indexes FindBase(std::string_view name)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == name && AllItemsList[i].dropRate > 0)
				return static_cast<_item_indexes>(i);
		}
		return IDI_GOLD;
	}

	static std::vector<Poe2ItemMod> Mods(const Item &item)
	{
		std::vector<Poe2ItemMod> mods;
		for (const Poe2ItemMod &mod : item._iPoe2Mods) {
			if (mod.family != 0)
				mods.push_back(mod);
		}
		return mods;
	}

	static const ModFamily &Family(const Poe2ItemMod &mod)
	{
		return *ItemModFamily(mod);
	}
};

TEST_F(Poe2ModifiersTest, TablesLoadEveryFamilyWithOrderedTiers)
{
	const std::vector<ModFamily> &families = ModifierFamilies();
	ASSERT_EQ(families.size(), 86U);
	int enabled = 0;
	for (size_t i = 0; i < families.size(); i++) {
		const ModFamily &family = families[i];
		if (family.enabled)
			enabled++;
		ASSERT_FALSE(family.tiers.empty()) << family.id;
		for (size_t t = 0; t < family.tiers.size(); t++) {
			EXPECT_EQ(family.tiers[t].tier, t + 1) << family.id;
			EXPECT_GT(family.tiers[t].weight, 0) << family.id;
			if (t > 0)
				EXPECT_LE(family.tiers[t].itemLevel, family.tiers[t - 1].itemLevel) << family.id << " T" << t + 1;
		}
		// Conflicts go both ways once loaded.
		for (const uint8_t other : family.conflicts) {
			const std::vector<uint8_t> &back = families[other].conflicts;
			EXPECT_NE(std::find(back.begin(), back.end(), static_cast<uint8_t>(i)), back.end()) << family.id << " and " << families[other].id;
		}
	}
	EXPECT_EQ(enabled, 42);

	const std::optional<uint8_t> allRes = FindModifierFamily("all_elemental_resistance");
	const std::optional<uint8_t> fireRes = FindModifierFamily("fire_resistance");
	ASSERT_TRUE(allRes && fireRes);
	const std::vector<uint8_t> &fireConflicts = families[*fireRes].conflicts;
	EXPECT_NE(std::find(fireConflicts.begin(), fireConflicts.end(), *allRes), fireConflicts.end());
}

TEST_F(Poe2ModifiersTest, RaresRollFourToSixLegalModsAndRecreateIdentically)
{
	std::array<int, 7> counts {};
	for (const std::string_view baseName : { "Long Sword", "Short Bow", "Full Plate Mail", "Ring", "Amulet", "Tower Shield", "Helm", "Leather Armor" }) {
		const _item_indexes base = FindBase(baseName);
		ASSERT_NE(base, IDI_GOLD) << baseName;
		for (uint32_t seed = 1; seed <= 300; seed++) {
			const int itemLevel = 1 + static_cast<int>(seed % 82);
			const Item item = MakeItem(base, seed, ITEM_QUALITY_RARE, itemLevel);
			ASSERT_EQ(item._iMagical, ITEM_QUALITY_RARE) << baseName;
			ASSERT_NE(item.dwBuff & CF_POE2_MODS, 0U);
			const std::vector<Poe2ItemMod> mods = Mods(item);
			ASSERT_LE(mods.size(), 6U);
			counts[mods.size()]++;

			int prefixes = 0;
			std::set<std::string> groups;
			for (const Poe2ItemMod &mod : mods) {
				const ModFamily &family = Family(mod);
				EXPECT_TRUE(family.enabled) << family.id;
				EXPECT_NE(family.slots & (1U << static_cast<uint32_t>(*ItemModSlot(item))), 0U) << family.id << " on " << baseName;
				EXPECT_LE(family.tiers[mod.tier - 1].itemLevel, itemLevel) << family.id;
				EXPECT_TRUE(groups.insert(family.group).second) << "two " << family.group << " on " << baseName;
				if (family.prefix)
					prefixes++;
				// Plate has no evasion, so evasion modifiers can't roll on it.
				if (baseName == "Full Plate Mail")
					EXPECT_NE(family.id, "flat_evasion");
			}
			EXPECT_LE(prefixes, MaxRareModsPerSide);
			EXPECT_LE(static_cast<int>(mods.size()) - prefixes, MaxRareModsPerSide);

			Item recreated;
			RecreateItem(*MyPlayer, recreated, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff);
			const std::vector<Poe2ItemMod> again = Mods(recreated);
			ASSERT_EQ(again.size(), mods.size());
			for (size_t i = 0; i < mods.size(); i++) {
				EXPECT_EQ(again[i].family, mods[i].family);
				EXPECT_EQ(again[i].tier, mods[i].tier);
				EXPECT_EQ(again[i].values, mods[i].values);
			}
			EXPECT_EQ(recreated._iPLHP, item._iPLHP);
			EXPECT_EQ(recreated._iAC, item._iAC);
		}
	}
	// Natural rares have 4, 5 or 6 modifiers, mostly 4; small items can run out of legal modifiers.
	EXPECT_GT(counts[4], counts[5]);
	EXPECT_GT(counts[5], counts[6]);
	EXPECT_GT(counts[6], 0);
}

TEST_F(Poe2ModifiersTest, MagicItemsHaveAPrefixOrSuffixAndAName)
{
	const _item_indexes base = FindBase("Ring");
	ASSERT_NE(base, IDI_GOLD);
	int two = 0;
	for (uint32_t seed = 1; seed <= 200; seed++) {
		const Item item = MakeItem(base, seed, ITEM_QUALITY_MAGIC, 40);
		ASSERT_EQ(item._iMagical, ITEM_QUALITY_MAGIC);
		const std::vector<Poe2ItemMod> mods = Mods(item);
		ASSERT_GE(mods.size(), 1U);
		ASSERT_LE(mods.size(), 2U);
		if (mods.size() == 2) {
			two++;
			EXPECT_NE(Family(mods[0]).prefix, Family(mods[1]).prefix);
		}
		const std::string name { item.getName().str() };
		for (const Poe2ItemMod &mod : mods)
			EXPECT_NE(name.find(Family(mod).word), std::string::npos) << name;
	}
	EXPECT_GT(two, 50);
	EXPECT_LT(two, 150);
}

TEST_F(Poe2ModifiersTest, ModifiersChangeTheItemAndShowTheirTier)
{
	const std::optional<uint8_t> life = FindModifierFamily("maximum_life");
	ASSERT_TRUE(life);
	Item ring = {};
	ApplyItemMod(ring, { static_cast<uint8_t>(*life + 1), 3, { 80, 0 } });
	EXPECT_EQ(ring._iPLHP, Fixed10_6::fromInt(80));
	const std::vector<std::string> lines = ItemModText({ static_cast<uint8_t>(*life + 1), 3, { 80, 0 } });
	ASSERT_EQ(lines.size(), 1U);
	EXPECT_EQ(lines[0], "+80 to maximum Life (T3)");

	const std::optional<uint8_t> hybrid = FindModifierFamily("life_armour_hybrid");
	ASSERT_TRUE(hybrid);
	const std::vector<std::string> hybridLines = ItemModText({ static_cast<uint8_t>(*hybrid + 1), 2, { 70, 50 } });
	ASSERT_EQ(hybridLines.size(), 2U);
	EXPECT_EQ(hybridLines[0], "+70 to maximum Life");
	EXPECT_EQ(hybridLines[1], "50% increased Armour (T2)");

	const std::optional<uint8_t> added = FindModifierFamily("local_added_physical");
	ASSERT_TRUE(added);
	Item sword = {};
	sword._iMinDam = 3;
	sword._iMaxDam = 8;
	ApplyItemMod(sword, { static_cast<uint8_t>(*added + 1), 5, { 4, 6 } });
	EXPECT_EQ(sword._iMinDam, 7);
	EXPECT_EQ(sword._iMaxDam, 14);
}

TEST_F(Poe2ModifiersTest, FlatArmourIsScaledToDiablo)
{
	const std::optional<uint8_t> armour = FindModifierFamily("flat_armour");
	ASSERT_TRUE(armour);
	const std::vector<Poe2ItemMod> mods = RollItemMods(ModSlot::BodyArmour, ModBaseArmour, 82, 6, 3, 7, 7);
	for (const Poe2ItemMod &mod : mods) {
		if (mod.family != *armour + 1)
			continue;
		const ModTier &tier = Family(mod).tiers[mod.tier - 1];
		EXPECT_GE(mod.values[0], (tier.min[0] * 15) / 100);
		EXPECT_LE(mod.values[0], ((tier.max[0] * 15) + 99) / 100);
	}
}

TEST_F(Poe2ModifiersTest, GearSkillLevelsRaiseMatchingSkills)
{
	Player &player = *MyPlayer;
	player.poe2.skillSlots[0] = SkillSlot::ForSpell(SpellID::Firebolt);
	player.poe2.slotLevels[0] = 5;
	player.poe2.skillSlots[1] = SkillSlot::ForSpell(SpellID::ChainLightning);
	player.poe2.slotLevels[1] = 5;
	player.poe2.skillSlots[2] = SkillSlot::ForAttack(WeaponSkill::Cleave);
	player.poe2.slotLevels[2] = 5;
	player.poe2.itemStats[static_cast<size_t>(Poe2ItemStat::SpellSkillLevels)] = 1;
	player.poe2.itemStats[static_cast<size_t>(Poe2ItemStat::FireSkillLevels)] = 2;
	player.poe2.itemStats[static_cast<size_t>(Poe2ItemStat::MeleeSkillLevels)] = 3;
	EXPECT_EQ(player.poe2.skillLevel(SpellID::Firebolt), 8);
	EXPECT_EQ(player.poe2.skillLevel(SpellID::ChainLightning), 6);
	EXPECT_EQ(player.poe2.skillLevel(WeaponSkill::Cleave), 8);
	// The gem itself keeps its own level.
	EXPECT_EQ(player.poe2.gemLevel(0), 5);
}

TEST_F(Poe2ModifiersTest, DivineOrbRerollsValuesButKeepsTheModifiers)
{
	const _item_indexes base = FindBase("Full Plate Mail");
	ASSERT_NE(base, IDI_GOLD);
	const Item item = MakeItem(base, 99, ITEM_QUALITY_RARE, 70);
	Item divined;
	RecreateItem(*MyPlayer, divined, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff | (1U << 9));
	const std::vector<Poe2ItemMod> before = Mods(item);
	const std::vector<Poe2ItemMod> after = Mods(divined);
	ASSERT_EQ(before.size(), after.size());
	bool changed = false;
	for (size_t i = 0; i < before.size(); i++) {
		EXPECT_EQ(before[i].family, after[i].family);
		EXPECT_EQ(before[i].tier, after[i].tier);
		changed = changed || before[i].values != after[i].values;
	}
	EXPECT_TRUE(changed);
}

TEST_F(Poe2ModifiersTest, HighUnidentifiedTiersCullWeakModifiers)
{
	std::array<int, 6> tiers {};
	for (uint32_t seed = 1; seed <= 5000; seed++)
		tiers[UnidentifiedTier(seed, 82)]++;
	EXPECT_GT(tiers[1], tiers[2]);
	EXPECT_GT(tiers[2], tiers[5]);
	EXPECT_GT(tiers[5], 0);
	for (uint32_t seed = 1; seed <= 500; seed++)
		EXPECT_EQ(UnidentifiedTier(seed, 19), 1);

	// A tier 5 rare only rolls modifiers needing item level 48 or more, unless that would remove the whole family.
	for (uint32_t seed = 1; seed <= 200; seed++) {
		for (const Poe2ItemMod &mod : RollItemMods(ModSlot::Ring, 0, 82, 6, MaxRareModsPerSide, seed, seed, 5)) {
			const ModFamily &family = Family(mod);
			const ModTier &tier = family.tiers[mod.tier - 1];
			if (tier.itemLevel < 48)
				EXPECT_EQ(mod.tier, std::count_if(family.tiers.begin(), family.tiers.end(), [](const ModTier &t) { return t.itemLevel >= 48; }) + 1) << family.id;
		}
	}
	// A tier 5 magic item keeps only the best tenth of each family's tiers (at least one).
	for (uint32_t seed = 1; seed <= 200; seed++) {
		for (const Poe2ItemMod &mod : RollItemMods(ModSlot::Ring, 0, 82, 2, MaxMagicModsPerSide, seed, seed, 5))
			EXPECT_EQ(mod.tier, 1) << Family(mod).id;
	}
}

TEST_F(Poe2ModifiersTest, OlderItemsKeepDiablosAffixes)
{
	const _item_indexes base = FindBase("Long Sword");
	ASSERT_NE(base, IDI_GOLD);
	Item item = MakeItem(base, 5, ITEM_QUALITY_RARE, 30);
	// An item level without the modifier flag is from before the modifier tables.
	Item older;
	RecreateItem(*MyPlayer, older, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff & ~CF_POE2_MODS);
	EXPECT_EQ(older._iPoe2Mods[0].family, 0);
	EXPECT_NE(older._iRarePowers[0], IPL_INVALID);
}

} // namespace
} // namespace devilution
