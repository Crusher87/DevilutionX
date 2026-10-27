#include <algorithm>
#include <array>
#include <optional>
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
#include "poe2/item_stats.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/unique_rules.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"
#include "utils/is_of.hpp"

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
	ASSERT_EQ(families.size(), 94U);
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
	// 57 with spell damage on staves (plan step 14).
	EXPECT_EQ(enabled, 57);

	const std::optional<uint8_t> allRes = FindModifierFamily("all_elemental_resistance");
	const std::optional<uint8_t> fireRes = FindModifierFamily("fire_resistance");
	ASSERT_TRUE(allRes && fireRes);
	const std::vector<uint8_t> &fireConflicts = families[*fireRes].conflicts;
	EXPECT_NE(std::find(fireConflicts.begin(), fireConflicts.end(), *allRes), fireConflicts.end());
}

TEST_F(Poe2ModifiersTest, RaresRollThreeToSixLegalModsAndRecreateIdentically)
{
	std::array<int, 7> counts {};
	for (const std::string_view baseName : { "Shortsword", "Fine Short Bow", "Full Plate Mail", "Ruby Ring", "Amber Amulet", "Wooden Tower Shield", "Iron Cap", "Leather Armor" }) {
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
	// Natural rares have 3 to 6 modifiers, mostly 4; small items can run out of legal modifiers.
	EXPECT_GT(counts[3], 0);
	EXPECT_GT(counts[4], counts[3]);
	EXPECT_GT(counts[4], counts[5]);
	EXPECT_GT(counts[5], counts[6]);
	EXPECT_GT(counts[6], 0);
}

TEST_F(Poe2ModifiersTest, NaturalRaresRollThreeModifiersNowAndThen)
{
	// Curtis 2026-10-23: 3 to 6 like POE2, mostly 4 or more; one rare in eight has 3.
	std::array<int, 7> fromRolls {};
	for (int roll = 0; roll < RareModCountRolls; roll++)
		fromRolls[RareModCountFromRoll(roll)]++;
	EXPECT_EQ(fromRolls[3], 3);
	EXPECT_EQ(fromRolls[4], 13);
	EXPECT_EQ(fromRolls[5], 6);
	EXPECT_EQ(fromRolls[6], 2);
	for (int roll = 0; roll < RareModCountRolls; roll++) {
		// The old roll was this one modulo 12 (4 below 8, 5 below 11, else 6): rares from before keep 5 or 6, and a 4 stays 3 or 4.
		const int oldRoll = roll % 12;
		const int oldCount = oldRoll < 8 ? 4 : oldRoll < 11 ? 5 : 6;
		if (oldCount == 4)
			EXPECT_TRUE(IsAnyOf(RareModCountFromRoll(roll), 3, 4)) << roll;
		else
			EXPECT_EQ(RareModCountFromRoll(roll), oldCount) << roll;
	}

	constexpr int Seeds = 24000;
	std::array<int, 7> counts {};
	for (uint32_t seed = 1; seed <= Seeds; seed++)
		counts[RareModCount(seed)]++;
	EXPECT_NEAR(counts[3] / static_cast<double>(Seeds), 3.0 / 24, 0.01);
	EXPECT_NEAR(counts[4] / static_cast<double>(Seeds), 13.0 / 24, 0.015);
	EXPECT_NEAR(counts[6] / static_cast<double>(Seeds), 2.0 / 24, 0.01);
}

TEST_F(Poe2ModifiersTest, MagicItemsHaveAPrefixOrSuffixAndAName)
{
	const _item_indexes base = FindBase("Ruby Ring");
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

TEST_F(Poe2ModifiersTest, ColdResistanceReadsAsTheMagicResistanceItGives)
{
	// POE2's cold resistance is the game's magic resistance, so the tooltip says so, like the compare panel.
	const std::optional<uint8_t> cold = FindModifierFamily("cold_resistance");
	ASSERT_TRUE(cold);
	const std::vector<std::string> lines = ItemModText({ static_cast<uint8_t>(*cold + 1), 4, { 10, 0 } });
	ASSERT_EQ(lines.size(), 1U);
	EXPECT_EQ(lines[0], "+10% to Magic Resistance");
	for (const ModFamily &family : ModifierFamilies()) {
		if (family.effect == ModEffect::MagicResistance) {
			EXPECT_EQ(family.text.find("Cold"), std::string::npos) << family.id;
			EXPECT_EQ(family.name.find("Cold"), std::string::npos) << family.id;
		}
	}
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
	EXPECT_EQ(lines[0], "+80 to maximum Life");

	const std::optional<uint8_t> hybrid = FindModifierFamily("life_armour_hybrid");
	ASSERT_TRUE(hybrid);
	const std::vector<std::string> hybridLines = ItemModText({ static_cast<uint8_t>(*hybrid + 1), 2, { 70, 50 } });
	ASSERT_EQ(hybridLines.size(), 2U);
	EXPECT_EQ(hybridLines[0], "+70 to maximum Life");
	EXPECT_EQ(hybridLines[1], "50% increased Armour");

	const std::optional<uint8_t> added = FindModifierFamily("local_added_physical");
	ASSERT_TRUE(added);
	Item sword = {};
	sword._iMinDam = 3;
	sword._iMaxDam = 8;
	ApplyItemMod(sword, { static_cast<uint8_t>(*added + 1), 5, { 4, 6 } });
	EXPECT_EQ(sword._iMinDam, 7);
	EXPECT_EQ(sword._iMaxDam, 14);
}

/** Harder monsters plan, step 10b: the bases took POE2's defence and damage, so flat modifiers take POE2's numbers too. */
TEST_F(Poe2ModifiersTest, FlatModsTakePoe2sNumbers)
{
	const auto family = [](std::string_view id) -> const ModFamily & { return ModifierFamilies()[*FindModifierFamily(id)]; };
	for (const std::string_view id : { "flat_armour", "flat_evasion", "flat_energy_shield", "local_added_physical", "local_added_fire", "local_added_lightning", "global_added_physical_attacks" })
		ASSERT_TRUE(FindModifierFamily(id)) << id;
	EXPECT_EQ(family("flat_armour").scale[0], 100);
	EXPECT_EQ(family("flat_evasion").scale[0], 100);
	// Energy shield counts in defence points, a third of what shows (EnergyShieldPerDefense).
	EXPECT_EQ(family("flat_energy_shield").scale[0], 33);
	for (const std::string_view id : { "local_added_physical", "local_added_fire", "local_added_lightning", "global_added_physical_attacks" })
		EXPECT_EQ(family(id).scale, (std::array<int16_t, 2> { 100, 100 })) << id;
	EXPECT_EQ(family("accuracy_rating").scale[0], 10) << "accuracy keeps its scale";

	// Items saved before then rebuild with the scales they rolled with.
	EXPECT_EQ(family("flat_armour").legacyScale[0], 15);
	EXPECT_EQ(family("flat_evasion").legacyScale[0], 15);
	EXPECT_EQ(family("flat_energy_shield").legacyScale[0], 10);
	for (const std::string_view id : { "local_added_physical", "local_added_fire", "local_added_lightning", "global_added_physical_attacks" })
		EXPECT_EQ(family(id).legacyScale, (std::array<int16_t, 2> { 25, 25 })) << id;
	EXPECT_EQ(family("maximum_life").legacyScale, family("maximum_life").scale);

	const uint8_t armour = *FindModifierFamily("flat_armour") + 1;
	int seen = 0;
	for (uint32_t seed = 1; seed <= 40; seed++) {
		for (const Poe2ItemMod &mod : RollItemMods(ModSlot::BodyArmour, ModBaseArmour, 82, 6, 3, seed, seed)) {
			if (mod.family != armour)
				continue;
			seen++;
			const ModTier &tier = Family(mod).tiers[mod.tier - 1];
			EXPECT_GE(mod.values[0], tier.min[0]);
			EXPECT_LE(mod.values[0], tier.max[0]);
		}
		const LegacyModScales legacy;
		EXPECT_TRUE(RollingLegacyModScales());
		for (const Poe2ItemMod &mod : RollItemMods(ModSlot::BodyArmour, ModBaseArmour, 82, 6, 3, seed, seed)) {
			if (mod.family != armour)
				continue;
			const ModTier &tier = Family(mod).tiers[mod.tier - 1];
			EXPECT_GE(mod.values[0], (tier.min[0] * 15) / 100);
			EXPECT_LE(mod.values[0], ((tier.max[0] * 15) + 99) / 100);
		}
	}
	EXPECT_GT(seen, 0);
	EXPECT_FALSE(RollingLegacyModScales()) << "the guard ends with its scope";
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

/**
 * Plan step 14 (Curtis 2026-10-27): staves roll POE2's increased Spell Damage, and it raises every spell hit with the
 * tree's spell damage. Quarterstaves, wands, sceptres, foci and amulets don't roll it (yet).
 */
TEST_F(Poe2ModifiersTest, StavesRollSpellDamage)
{
	const std::optional<uint8_t> index = FindModifierFamily("spell_damage");
	ASSERT_TRUE(index.has_value());
	const ModFamily &family = ModifierFamilies()[*index];
	EXPECT_TRUE(family.enabled);
	EXPECT_EQ(family.effect, ModEffect::SpellDamage);
	EXPECT_EQ(family.slots, 1U << static_cast<uint32_t>(ModSlot::Staff));

	const _item_indexes base = FindBase("Quick Pilgrim Staff");
	ASSERT_NE(base, IDI_GOLD);
	Item staff;
	int percent = 0;
	for (uint32_t seed = 1; seed <= 2000 && percent == 0; seed++) {
		const Item item = MakeItem(base, seed, ITEM_QUALITY_RARE, 40);
		for (const Poe2ItemMod &mod : Mods(item)) {
			if (mod.family == *index + 1) {
				staff = item;
				percent = mod.values[0];
			}
		}
	}
	ASSERT_GT(percent, 0) << "a rare staff with increased Spell Damage";
	EXPECT_EQ(staff._iPoe2Stats[static_cast<size_t>(Poe2ItemStat::SpellDamage)], percent);
	// Item level 40: tier 5 (40-49%) at best, tier 8 (12-19%) at worst.
	EXPECT_GE(percent, 12);
	EXPECT_LE(percent, 49);

	Player &player = *MyPlayer;
	player._pClass = HeroClass::Sorcerer;
	player.setCharacterLevel(40);
	player._pBaseStr = player._pBaseMag = player._pBaseDex = player._pBaseVit = 250;
	CalcPlrInv(player, false);
	EXPECT_EQ(UniqueSpellDamagePercent(player, SpellID::Firebolt), 100);
	player.InvBody[INVLOC_HAND_LEFT] = staff;
	CalcPlrInv(player, false);
	ASSERT_TRUE(player.InvBody[INVLOC_HAND_LEFT]._iStatFlag);
	EXPECT_EQ(player.poe2.rule(UniqueRule::SpellDamage), percent);
	EXPECT_EQ(UniqueSpellDamagePercent(player, SpellID::Firebolt), 100 + percent);
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
	const _item_indexes base = FindBase("Shortsword");
	ASSERT_NE(base, IDI_GOLD);
	Item item = MakeItem(base, 5, ITEM_QUALITY_RARE, 30);
	// An item level without the modifier flag is from before the modifier tables.
	Item older;
	RecreateItem(*MyPlayer, older, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff & ~CF_POE2_MODS);
	EXPECT_EQ(older._iPoe2Mods[0].family, 0);
	EXPECT_NE(older._iRarePowers[0], IPL_INVALID);
}

TEST_F(Poe2ModifiersTest, MagicFindRollsPoe2sRarityTiers)
{
	const std::optional<uint8_t> suffix = FindModifierFamily("item_rarity");
	const std::optional<uint8_t> prefix = FindModifierFamily("item_rarity_prefix");
	ASSERT_TRUE(suffix && prefix);
	// Saved crafts name families by row: the suffix keeps its old row and the prefix is the last.
	EXPECT_EQ(static_cast<size_t>(*prefix) + 1, ModifierFamilies().size());
	const ModFamily &suffixFamily = ModifierFamilies()[*suffix];
	const ModFamily &prefixFamily = ModifierFamilies()[*prefix];
	EXPECT_TRUE(suffixFamily.enabled);
	EXPECT_TRUE(prefixFamily.enabled);
	EXPECT_FALSE(suffixFamily.prefix);
	EXPECT_TRUE(prefixFamily.prefix);
	EXPECT_EQ(suffixFamily.effect, ModEffect::ItemRarity);
	EXPECT_EQ(prefixFamily.effect, ModEffect::ItemRarity);
	// Two groups, so one item can have both, as in POE2.
	EXPECT_NE(suffixFamily.group, prefixFamily.group);

	const auto bit = [](ModSlot slot) { return 1U << static_cast<uint32_t>(slot); };
	EXPECT_EQ(suffixFamily.slots, bit(ModSlot::Helmet) | bit(ModSlot::Gloves) | bit(ModSlot::Boots) | bit(ModSlot::Ring) | bit(ModSlot::Amulet));
	EXPECT_EQ(prefixFamily.slots, bit(ModSlot::Helmet) | bit(ModSlot::Ring) | bit(ModSlot::Amulet));

	// POE2's droppable tiers (item level: value): of Plunder, of Raiding, of Archaeology; Magpie's, Collector's, Hoarder's.
	struct Expected {
		int itemLevel;
		int min;
		int max;
	};
	const auto expectTiers = [](const ModFamily &family, const std::array<Expected, 3> &expected) {
		ASSERT_EQ(family.tiers.size(), expected.size()) << family.id;
		for (size_t t = 0; t < expected.size(); t++) {
			EXPECT_EQ(family.tiers[t].itemLevel, expected[t].itemLevel) << family.id << " T" << t + 1;
			EXPECT_EQ(family.tiers[t].min[0], expected[t].min) << family.id << " T" << t + 1;
			EXPECT_EQ(family.tiers[t].max[0], expected[t].max) << family.id << " T" << t + 1;
		}
	};
	expectTiers(suffixFamily, { { { 40, 15, 18 }, { 24, 11, 14 }, { 3, 6, 10 } } });
	expectTiers(prefixFamily, { { { 47, 16, 19 }, { 29, 12, 15 }, { 10, 8, 11 } } });

	const auto isRarity = [&](const Poe2ItemMod &mod) { return mod.family == *suffix + 1 || mod.family == *prefix + 1; };
	bool sawSuffix = false;
	bool sawPrefix = false;
	bool sawBoth = false;
	for (uint32_t seed = 1; seed <= 2000; seed++) {
		// Nothing below item level 3, no prefix below 10, and never on body armour.
		for (const Poe2ItemMod &mod : RollItemMods(ModSlot::Ring, 0, 2, 6, MaxRareModsPerSide, seed, seed))
			EXPECT_FALSE(isRarity(mod)) << seed;
		for (const Poe2ItemMod &mod : RollItemMods(ModSlot::Amulet, 0, 9, 6, MaxRareModsPerSide, seed, seed))
			EXPECT_NE(mod.family, *prefix + 1) << seed;
		for (const Poe2ItemMod &mod : RollItemMods(ModSlot::BodyArmour, ModBaseArmour, 82, 6, MaxRareModsPerSide, seed, seed))
			EXPECT_FALSE(isRarity(mod)) << seed;
		bool hasSuffix = false;
		bool hasPrefix = false;
		for (const Poe2ItemMod &mod : RollItemMods(ModSlot::Ring, 0, 82, 6, MaxRareModsPerSide, seed, seed)) {
			if (!isRarity(mod))
				continue;
			const ModTier &tier = Family(mod).tiers[mod.tier - 1];
			EXPECT_GE(mod.values[0], tier.min[0]);
			EXPECT_LE(mod.values[0], tier.max[0]);
			(mod.family == *suffix + 1 ? hasSuffix : hasPrefix) = true;
		}
		sawSuffix = sawSuffix || hasSuffix;
		sawPrefix = sawPrefix || hasPrefix;
		sawBoth = sawBoth || (hasSuffix && hasPrefix);
	}
	EXPECT_TRUE(sawSuffix);
	EXPECT_TRUE(sawPrefix);
	EXPECT_TRUE(sawBoth);

	// Both add to the gear rarity stat the loot rules read (the Gold Amulet's implicit uses it too).
	constexpr auto RarityStat = static_cast<size_t>(Poe2ItemStat::ItemRarity);
	Item ring = {};
	ApplyItemMod(ring, { static_cast<uint8_t>(*suffix + 1), 2, { 12, 0 } });
	ApplyItemMod(ring, { static_cast<uint8_t>(*prefix + 1), 1, { 17, 0 } });
	EXPECT_EQ(ring._iPoe2Stats[RarityStat], 29);
	const std::vector<std::string> lines = ItemModText({ static_cast<uint8_t>(*suffix + 1), 2, { 12, 0 } });
	ASSERT_EQ(lines.size(), 1U);
	EXPECT_EQ(lines[0], "12% increased Rarity of Items found");

	// A dropped helmet that rolls it carries the stat.
	const _item_indexes cap = FindBase("Iron Cap");
	ASSERT_NE(cap, IDI_GOLD);
	int found = 0;
	for (uint32_t seed = 1; seed <= 600 && found < 3; seed++) {
		const Item item = MakeItem(cap, seed, ITEM_QUALITY_RARE, 60);
		int rarity = 0;
		for (const Poe2ItemMod &mod : Mods(item)) {
			if (isRarity(mod))
				rarity += mod.values[0];
		}
		if (rarity == 0)
			continue;
		EXPECT_EQ(item._iPoe2Stats[RarityStat], rarity) << seed;
		found++;
	}
	EXPECT_GT(found, 0);
}

} // namespace
} // namespace devilution
