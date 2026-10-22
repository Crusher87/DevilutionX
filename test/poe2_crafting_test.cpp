#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/bases.hpp"
#include "poe2/craft_state.hpp"
#include "poe2/crafting_items.hpp"
#include "poe2/currency.hpp"
#include "poe2/desecration.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2CraftingTest : public ::testing::Test {
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

	static int Prefixes(const Item &item)
	{
		int count = 0;
		for (const Poe2ItemMod &mod : Mods(item))
			count += ItemModFamily(mod)->prefix ? 1 : 0;
		return count;
	}

	static Item Rebuilt(const Item &item)
	{
		Item rebuilt;
		RecreateItem(*MyPlayer, rebuilt, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff, item._iPoe2Craft);
		return rebuilt;
	}

	static void ExpectSameMods(const Item &a, const Item &b)
	{
		const std::vector<Poe2ItemMod> modsA = Mods(a);
		const std::vector<Poe2ItemMod> modsB = Mods(b);
		ASSERT_EQ(modsA.size(), modsB.size());
		for (size_t i = 0; i < modsA.size(); i++) {
			EXPECT_EQ(modsA[i].family, modsB[i].family);
			EXPECT_EQ(modsA[i].tier, modsB[i].tier);
			EXPECT_EQ(modsA[i].values, modsB[i].values);
		}
	}

	static void GiveOmen(OmenId omen)
	{
		Item &item = MyPlayer->InvList[MyPlayer->_pNumInv];
		item = {};
		item._itype = ItemType::Misc;
		item._iMiscId = IMISC_OMEN;
		SetCraftingItemVariant(item, static_cast<uint8_t>(omen));
		MyPlayer->_pNumInv++;
	}

	/** Uses an orb that must work. */
	static Item Crafted(CurrencyOrb orb, Item item, uint8_t variant = 0)
	{
		EXPECT_TRUE(ApplyCurrencyOrb(orb, *MyPlayer, item, variant));
		return item;
	}

	/** A ware from Griswold's stock, made from its seed the way the shop rebuilds it. */
	static Item MakeSmithWare(uint32_t seed, item_quality rarity, int itemLevel)
	{
		Item probe = {};
		SetLootRarity(probe, rarity);
		SetPoe2ItemLevel(probe, itemLevel);
		Item ware;
		RecreateItem(*MyPlayer, ware, FindBase("Ash Staff"), static_cast<uint16_t>(20 | CF_SMITH), seed, 0, probe.dwBuff | CF_POE2 | CF_HELLFIRE, {});
		return ware;
	}

	static CurrencyOrb QualityOrbFor(const Item &item)
	{
		switch (ItemQualityKind(item)) {
		case QualityKind::Weapon:
			return CurrencyOrb::Whetstone;
		case QualityKind::Caster:
			return CurrencyOrb::Etcher;
		default:
			return CurrencyOrb::Scrap;
		}
	}
};

TEST_F(Poe2CraftingTest, CraftStatePacksAndUnpacks)
{
	Poe2Craft craft;
	craft.explicitMods = true;
	craft.rarity = ITEM_QUALITY_RARE;
	craft.rolledMagic = true;
	craft.mods[0] = { 12, 3, 0 };
	craft.mods[1] = { 40, 1, 17 };
	craft.mods[2] = { UnrevealedModFamily, 2, 0 };
	craft.sockets = 2;
	craft.augments = { 3, 9, 0 };
	craft.catalyst = 4;
	craft.enchant = 7;
	craft.enchantRoll = 5;
	craft.magnitude = -14;
	craft.desecrated = true;
	EXPECT_EQ(UnpackCraftState(PackCraftState(craft)), craft);
	EXPECT_TRUE(UnpackCraftState({}).isEmpty());
	auto bytes = PackCraftState(craft);
	bytes[5] ^= 0x20;
	EXPECT_TRUE(UnpackCraftState(bytes).isEmpty());
}

TEST_F(Poe2CraftingTest, TransmuteAugmentRegalBuildUpAnItem)
{
	Item item = MakeItem("Full Plate Mail", 7, ITEM_QUALITY_NORMAL, 60);
	ASSERT_TRUE(CanCraftMods(item));
	ASSERT_EQ(Mods(item).size(), 0U);
	item = Crafted(CurrencyOrb::Transmutation, item);
	EXPECT_EQ(item._iMagical, ITEM_QUALITY_MAGIC);
	EXPECT_EQ(Mods(item).size(), 1U);
	item = Crafted(CurrencyOrb::Augmentation, item);
	EXPECT_EQ(Mods(item).size(), 2U);
	EXPECT_EQ(Prefixes(item), 1);
	item = Crafted(CurrencyOrb::Regal, item);
	EXPECT_EQ(item._iMagical, ITEM_QUALITY_RARE);
	EXPECT_EQ(Mods(item).size(), 3U);
	ExpectSameMods(item, Rebuilt(item));
	EXPECT_EQ(std::string_view(item._iIName), std::string_view(Rebuilt(item)._iIName));
}

TEST_F(Poe2CraftingTest, ShopWaresCraftOneModifierAtATime)
{
	// Curtis 2026-10-22: a Regal Orb made a bought magic staff a new rare with five modifiers, as wares rerolled whole.
	int regals = 0;
	for (uint32_t seed = 1; seed <= 300 && regals < 4; seed++) {
		Item ware = MakeSmithWare(seed, ITEM_QUALITY_MAGIC, 30);
		if (!CanCraftMods(ware) || ware._iMagical != ITEM_QUALITY_MAGIC || Mods(ware).size() != 2)
			continue;
		ASSERT_NE(ware._iCreateInfo & CF_TOWN, 0);
		const std::vector<Poe2ItemMod> before = Mods(ware);
		Item rare = Crafted(CurrencyOrb::Regal, ware);
		EXPECT_EQ(rare._iMagical, ITEM_QUALITY_RARE);
		ASSERT_EQ(Mods(rare).size(), 3U) << seed;
		EXPECT_LE(Prefixes(rare), 2);
		for (const Poe2ItemMod &mod : before) {
			const std::vector<Poe2ItemMod> after = Mods(rare);
			EXPECT_TRUE(std::any_of(after.begin(), after.end(), [&](const Poe2ItemMod &other) { return other.family == mod.family && other.values == mod.values; }));
		}
		// It stays a ware and rebuilds the same, and quality currency adds no modifier.
		EXPECT_NE(rare._iCreateInfo & CF_TOWN, 0);
		ExpectSameMods(rare, Rebuilt(rare));
		Item polished = rare;
		if (ApplyCurrencyOrb(QualityOrbFor(rare), *MyPlayer, polished))
			ExpectSameMods(polished, rare);
		regals++;
	}
	EXPECT_GT(regals, 0);

	// A normal ware takes Transmutation, Augmentation and an Artificer's socket like a drop.
	for (uint32_t seed = 1; seed <= 300; seed++) {
		Item ware = MakeSmithWare(seed, ITEM_QUALITY_NORMAL, 30);
		if (!CanCraftMods(ware) || ware._iMagical != ITEM_QUALITY_NORMAL || MaxAugmentSockets(ware) == 0)
			continue;
		ware = Crafted(CurrencyOrb::Transmutation, ware);
		EXPECT_EQ(Mods(ware).size(), 1U);
		ware = Crafted(CurrencyOrb::Augmentation, ware);
		EXPECT_EQ(Mods(ware).size(), 2U);
		ware = Crafted(CurrencyOrb::Artificer, ware);
		EXPECT_EQ(ware._iPoe2Craft.sockets, 1);
		ExpectSameMods(ware, Rebuilt(ware));
		EXPECT_EQ(Rebuilt(ware)._iMagical, ITEM_QUALITY_MAGIC);
		ware = Crafted(CurrencyOrb::Scouring, ware);
		EXPECT_EQ(ware._iMagical, ITEM_QUALITY_NORMAL);
		EXPECT_EQ(Rebuilt(ware)._iMagical, ITEM_QUALITY_NORMAL);
		return;
	}
	FAIL() << "no normal ware to craft";
}

TEST_F(Poe2CraftingTest, CraftingNeverRepeatsAStatTheItemHas)
{
	// Curtis 2026-10-22: a staff with the Physical Damage and Accuracy hybrid had a second "+ to Accuracy Rating".
	EXPECT_EQ(ModEffectBits(ModEffect::None), 0U);
	EXPECT_NE(ModEffectBits(ModEffect::PhysicalPercentAndAccuracy) & ModEffectBits(ModEffect::Accuracy), 0U);
	EXPECT_NE(ModEffectBits(ModEffect::LifeAndDefensePercent) & ModEffectBits(ModEffect::DefensePercent), 0U);
	const std::optional<uint8_t> hybrid = FindModifierFamily("physical_accuracy_hybrid");
	const std::optional<uint8_t> accuracy = FindModifierFamily("accuracy_rating");
	ASSERT_TRUE(hybrid && accuracy);
	const uint8_t weakest = ModifierFamilies()[*hybrid].tiers.back().tier;
	for (uint32_t seed = 1; seed <= 12; seed++) {
		Item staff = MakeItem("Ash Staff", seed, ITEM_QUALITY_NORMAL, 70);
		ASSERT_TRUE(CanCraftMods(staff));
		Poe2Craft craft = CurrentCraft(staff);
		craft.rarity = ITEM_QUALITY_RARE;
		ASSERT_TRUE(AddCraftMod(craft, { static_cast<uint8_t>(*hybrid + 1), weakest, {} }, seed));
		staff = RebuildWithCraft(*MyPlayer, staff, staff.dwBuff, craft);
		for (int i = 0; i < 8 && ApplyCurrencyOrb(CurrencyOrb::Exalted, *MyPlayer, staff); i++) { }
		const std::vector<Poe2ItemMod> mods = Mods(staff);
		EXPECT_LE(Prefixes(staff), MaxRareModsPerSide);
		EXPECT_LE(static_cast<int>(mods.size()) - Prefixes(staff), MaxRareModsPerSide);
		for (const Poe2ItemMod &mod : mods)
			EXPECT_NE(mod.family, *accuracy + 1) << seed;
	}
}

TEST_F(Poe2CraftingTest, AlchemyGivesExactlyFourModifiers)
{
	for (uint32_t seed = 1; seed <= 10; seed++) {
		Item item = MakeItem("Full Plate Mail", seed, ITEM_QUALITY_NORMAL, 70);
		item = Crafted(CurrencyOrb::Alchemy, item);
		EXPECT_EQ(item._iMagical, ITEM_QUALITY_RARE);
		EXPECT_EQ(Mods(item).size(), 4U) << seed;
		EXPECT_LE(Prefixes(item), 3);
		EXPECT_GE(Prefixes(item), 1);
	}
}

TEST_F(Poe2CraftingTest, OneAugmentThenExaltMakesItRare)
{
	Item item = Crafted(CurrencyOrb::Transmutation, MakeItem("Full Plate Mail", 9, ITEM_QUALITY_NORMAL, 60));
	EXPECT_TRUE(CurrencyFitsItem(CurrencyOrb::Augmentation, item));
	item = Crafted(CurrencyOrb::Augmentation, item);
	ASSERT_EQ(Mods(item).size(), 2U);
	// A second Augmentation has no room, so the item isn't outlined for it.
	EXPECT_FALSE(CurrencyFitsItem(CurrencyOrb::Augmentation, item));
	EXPECT_TRUE(CurrencyFitsItem(CurrencyOrb::Exalted, item));
	item = Crafted(CurrencyOrb::Exalted, item);
	EXPECT_EQ(item._iMagical, ITEM_QUALITY_RARE);
	EXPECT_EQ(Mods(item).size(), 3U);
}

TEST_F(Poe2CraftingTest, AlchemyRerollsAMagicItem)
{
	for (uint32_t seed = 1; seed <= 10; seed++) {
		Item item = Crafted(CurrencyOrb::Transmutation, MakeItem("Full Plate Mail", seed, ITEM_QUALITY_NORMAL, 70));
		ASSERT_EQ(item._iMagical, ITEM_QUALITY_MAGIC);
		item = Crafted(CurrencyOrb::Alchemy, item);
		EXPECT_EQ(item._iMagical, ITEM_QUALITY_RARE);
		EXPECT_EQ(Mods(item).size(), 4U) << seed;
	}
}

TEST_F(Poe2CraftingTest, ExaltFillsUpToSixThenStops)
{
	Item item = Crafted(CurrencyOrb::Alchemy, MakeItem("Leather Armor", 3, ITEM_QUALITY_NORMAL, 70));
	const std::vector<Poe2ItemMod> before = Mods(item);
	item = Crafted(CurrencyOrb::Exalted, item);
	ASSERT_EQ(Mods(item).size(), 5U);
	// The modifiers already there keep their values.
	for (const Poe2ItemMod &mod : before) {
		const std::vector<Poe2ItemMod> after = Mods(item);
		EXPECT_TRUE(std::any_of(after.begin(), after.end(), [&](const Poe2ItemMod &other) { return other.family == mod.family && other.values == mod.values; }));
	}
	item = Crafted(CurrencyOrb::Exalted, item);
	EXPECT_EQ(Mods(item).size(), 6U);
	EXPECT_EQ(Prefixes(item), 3);
	EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Exalted, *MyPlayer, item));
}

TEST_F(Poe2CraftingTest, AnnulRemovesAndChaosReplaces)
{
	Item item = Crafted(CurrencyOrb::Alchemy, MakeItem("Full Plate Mail", 11, ITEM_QUALITY_NORMAL, 70));
	const Item annulled = Crafted(CurrencyOrb::Annul, item);
	EXPECT_EQ(Mods(annulled).size(), 3U);
	const Item chaosed = Crafted(CurrencyOrb::Chaos, item);
	EXPECT_EQ(Mods(chaosed).size(), 4U);
	std::set<uint8_t> families;
	for (const Poe2ItemMod &mod : Mods(item))
		families.insert(mod.family);
	int kept = 0;
	for (const Poe2ItemMod &mod : Mods(chaosed))
		kept += families.count(mod.family) != 0 ? 1 : 0;
	EXPECT_GE(kept, 3);
}

TEST_F(Poe2CraftingTest, DivineRerollsValuesOnly)
{
	Item item = Crafted(CurrencyOrb::Alchemy, MakeItem("Full Plate Mail", 21, ITEM_QUALITY_NORMAL, 80));
	bool changed = false;
	Item divined = item;
	for (int i = 0; i < 5 && !changed; i++) {
		divined = Crafted(CurrencyOrb::Divine, divined);
		const std::vector<Poe2ItemMod> before = Mods(item);
		const std::vector<Poe2ItemMod> after = Mods(divined);
		ASSERT_EQ(before.size(), after.size());
		for (size_t m = 0; m < before.size(); m++) {
			EXPECT_EQ(before[m].family, after[m].family);
			EXPECT_EQ(before[m].tier, after[m].tier);
			changed = changed || before[m].values != after[m].values;
		}
	}
	EXPECT_TRUE(changed);
	ExpectSameMods(divined, Rebuilt(divined));
}

TEST_F(Poe2CraftingTest, GreaterExaltedRollsHighLevelModifiers)
{
	for (uint32_t seed = 1; seed <= 8; seed++) {
		Item item = Crafted(CurrencyOrb::Alchemy, MakeItem("Full Plate Mail", seed, ITEM_QUALITY_NORMAL, 82));
		const std::vector<Poe2ItemMod> before = Mods(item);
		item = Crafted(CurrencyOrb::GreaterExalted, item);
		for (const Poe2ItemMod &mod : Mods(item)) {
			if (std::none_of(before.begin(), before.end(), [&](const Poe2ItemMod &old) { return old.family == mod.family; }))
				EXPECT_GE(ModTierItemLevel(mod.family, mod.tier), GreaterOrbModLevel);
		}
	}
}

TEST_F(Poe2CraftingTest, OmensSteerExaltsAndAreUsedUp)
{
	Item item = Crafted(CurrencyOrb::Alchemy, MakeItem("Full Plate Mail", 5, ITEM_QUALITY_NORMAL, 70));
	const int prefixes = Prefixes(item);
	if (prefixes >= 3)
		GTEST_SKIP();
	GiveOmen(OmenId::SinistralExaltation);
	item = Crafted(CurrencyOrb::Exalted, item);
	EXPECT_EQ(Prefixes(item), prefixes + 1);
	ConsumeCraftOmens(*MyPlayer);
	EXPECT_EQ(MyPlayer->_pNumInv, 0);

	GiveOmen(OmenId::GreaterExaltation);
	Item twice = Crafted(CurrencyOrb::Alchemy, MakeItem("Full Plate Mail", 6, ITEM_QUALITY_NORMAL, 70));
	twice = Crafted(CurrencyOrb::Exalted, twice);
	EXPECT_EQ(Mods(twice).size(), 6U);
}

TEST_F(Poe2CraftingTest, EssencesGuaranteeTheirModifier)
{
	const std::optional<uint8_t> life = FindModifierFamily("maximum_life");
	ASSERT_TRUE(life);
	Item item = Crafted(CurrencyOrb::Transmutation, MakeItem("Full Plate Mail", 9, ITEM_QUALITY_NORMAL, 70));
	item = Crafted(CurrencyOrb::Essence, item, EssenceVariant(EssenceKind::Vigor, EssenceTier::Greater));
	EXPECT_EQ(item._iMagical, ITEM_QUALITY_RARE);
	const std::vector<Poe2ItemMod> mods = Mods(item);
	const auto it = std::find_if(mods.begin(), mods.end(), [&](const Poe2ItemMod &mod) { return mod.family == *life + 1; });
	ASSERT_NE(it, mods.end());
	EXPECT_GE(ModTierItemLevel(it->family, it->tier), EssenceModLevelFloors[2]);

	// A normal item can't take a lesser essence; a perfect one needs a rare.
	Item normal = MakeItem("Full Plate Mail", 9, ITEM_QUALITY_NORMAL, 70);
	EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Essence, *MyPlayer, normal, EssenceVariant(EssenceKind::Vigor, EssenceTier::Lesser)));
	Item rare = Crafted(CurrencyOrb::Alchemy, normal);
	rare = Crafted(CurrencyOrb::Essence, rare, EssenceVariant(EssenceKind::Vigor, EssenceTier::Perfect));
	EXPECT_EQ(Mods(rare).size(), 4U);
}

TEST_F(Poe2CraftingTest, QualityCurrencyFitsItsKind)
{
	Item armour = MakeItem("Iron Cap", 4, ITEM_QUALITY_NORMAL, 50);
	const int quality = ItemQuality(armour);
	EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Whetstone, *MyPlayer, armour));
	armour = Crafted(CurrencyOrb::Scrap, armour);
	EXPECT_EQ(ItemQuality(armour), std::min(quality + 2, MaxCraftedQuality));
	for (int i = 0; i < 10; i++)
		ApplyCurrencyOrb(CurrencyOrb::Scrap, *MyPlayer, armour);
	EXPECT_EQ(ItemQuality(armour), std::max(quality, MaxCraftedQuality));
}

TEST_F(Poe2CraftingTest, CraftingKeepsShrineAndOilBonuses)
{
	// A Gloomy Shrine's +2 armour and an oil's damage aren't in the item's seed (Curtis 2026-10-19: quality lowered a
	// robe's energy shield).
	Item armour = MakeItem("Iron Cap", 4, ITEM_QUALITY_NORMAL, 50);
	armour._iAC += 2;
	const int armourBefore = armour._iAC;
	armour = Crafted(CurrencyOrb::Scrap, armour);
	EXPECT_EQ(armour._iAC, armourBefore);
	Item weapon = MakeItem("Heavy Hatchet", 4, ITEM_QUALITY_NORMAL, 50);
	weapon._iMaxDam += 3;
	const int maxBefore = weapon._iMaxDam;
	weapon = Crafted(CurrencyOrb::Transmutation, weapon);
	EXPECT_EQ(weapon._iMaxDam, maxBefore);
}

TEST_F(Poe2CraftingTest, ArtificerAndAugments)
{
	Item helmet = Crafted(CurrencyOrb::Alchemy, MakeItem("Iron Cap", 13, ITEM_QUALITY_NORMAL, 60));
	const int baseAc = helmet._iPLAC;
	EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Augment, *MyPlayer, helmet, static_cast<uint8_t>(AugmentId::IronRune)));
	helmet = Crafted(CurrencyOrb::Artificer, helmet);
	EXPECT_EQ(helmet._iPoe2Craft.sockets, 1);
	EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Artificer, *MyPlayer, helmet));
	helmet = Crafted(CurrencyOrb::Augment, helmet, static_cast<uint8_t>(AugmentId::IronRune));
	EXPECT_GT(helmet._iPLAC, baseAc);
	ExpectSameMods(helmet, Rebuilt(helmet));
	EXPECT_EQ(Rebuilt(helmet)._iPLAC, helmet._iPLAC);

	// Defence percent counts on the player; a talisman replaces the rune when every socket is full.
	helmet = Crafted(CurrencyOrb::Augment, helmet, static_cast<uint8_t>(AugmentId::HunterTalisman));
	UniqueRuleTotals totals {};
	AddCraftRules(helmet, totals);
	EXPECT_EQ(totals[static_cast<size_t>(UniqueRule::DamageVsRareUnique)], 15);
	EXPECT_EQ(helmet._iPLAC, baseAc);

	Item two = Crafted(CurrencyOrb::Alchemy, MakeItem("Heavy Hatchet", 2, ITEM_QUALITY_NORMAL, 60));
	EXPECT_EQ(MaxAugmentSockets(two), 1);

	Item ring = MakeItem("Ruby Ring", 2, ITEM_QUALITY_NORMAL, 40);
	EXPECT_EQ(MaxAugmentSockets(ring), 0);
	EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Artificer, *MyPlayer, ring));
}

TEST_F(Poe2CraftingTest, VaalCorruptsAndLocksTheItem)
{
	int enchanted = 0;
	for (uint32_t seed = 1; seed <= 30; seed++) {
		Item item = Crafted(CurrencyOrb::Alchemy, MakeItem("Full Plate Mail", seed, ITEM_QUALITY_NORMAL, 70));
		item = Crafted(CurrencyOrb::Vaal, item);
		EXPECT_NE(item.dwBuff & CF_POE2_CORRUPTED, 0U);
		EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Exalted, *MyPlayer, item));
		ExpectSameMods(item, Rebuilt(item));
		if (item._iPoe2Craft.enchant != 0) {
			enchanted++;
			EXPECT_FALSE(CorruptionEnchantText(item).empty());
		}
	}
	EXPECT_GT(enchanted, 0);
}

TEST_F(Poe2CraftingTest, ChanceMakesAUniqueOrDestroys)
{
	int made = 0;
	int destroyed = 0;
	for (uint32_t seed = 1; seed <= 40; seed++) {
		Item item = MakeItem("Full Plate Mail", seed, ITEM_QUALITY_NORMAL, 70);
		if (!ApplyCurrencyOrb(CurrencyOrb::Chance, *MyPlayer, item))
			GTEST_SKIP() << "no uniques on this base";
		if (CurrencyDestroyedItem)
			destroyed++;
		else if (item._iMagical == ITEM_QUALITY_UNIQUE)
			made++;
	}
	EXPECT_GT(made, 0);
	EXPECT_GT(destroyed, 0);
}

TEST_F(Poe2CraftingTest, CraftingItemsGetAKindAndName)
{
	for (const std::string_view name : { "Essence", "Omen", "Catalyst" }) {
		const _item_indexes base = FindBase(name);
		ASSERT_NE(base, IDI_GOLD) << name;
		Item item = {};
		SetPoe2ItemLevel(item, 80);
		SetRndSeed(17);
		SetupAllItems(*MyPlayer, item, base, AdvanceRndSeed(), 40, 1, true, false, 0, true);
		EXPECT_TRUE(IsCraftingVariantItem(item)) << name;
		EXPECT_NE(CraftingItemVariant(item), 0) << name;
		EXPECT_NE(std::string_view(item._iIName), name);
		EXPECT_FALSE(CraftingItemText(item).empty()) << name;
	}
}

TEST_F(Poe2CraftingTest, DesecrationOffersThreeAndReveals)
{
	ASSERT_LT(ModifierFamilies().size(), DesecratedFamilyBase);
	for (uint32_t seed = 1; seed <= 6; seed++) {
		Item item = Crafted(CurrencyOrb::Alchemy, MakeItem("Full Plate Mail", seed, ITEM_QUALITY_NORMAL, 80));
		item = Crafted(CurrencyOrb::Desecrate, item);
		EXPECT_TRUE(DesecrationRevealItem);
		ASSERT_TRUE(HasUnrevealedMod(item));
		EXPECT_TRUE(item._iPoe2Craft.desecrated);
		// Another bone brings the choice back without being used up.
		EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Desecrate, *MyPlayer, item));
		EXPECT_TRUE(DesecrationRevealItem);

		const std::vector<Poe2CraftMod> offers = DesecrationOffers(item, false);
		ASSERT_EQ(offers.size(), DesecrationOfferCount);
		EXPECT_EQ(offers, DesecrationOffers(item, false));
		std::set<uint8_t> families;
		bool prefix = false;
		for (const Poe2CraftMod &mod : item._iPoe2Craft.mods) {
			if (mod.family == UnrevealedModFamily)
				prefix = IsCraftPrefix(mod);
		}
		for (const Poe2CraftMod &offer : offers) {
			families.insert(offer.family);
			EXPECT_EQ(IsCraftPrefix(offer), prefix);
			EXPECT_FALSE(DesecratedModText(item, offer).empty());
		}
		EXPECT_EQ(families.size(), DesecrationOfferCount);

		const size_t before = Mods(item).size();
		ASSERT_TRUE(RevealDesecratedMod(*MyPlayer, item, offers[0]));
		EXPECT_FALSE(HasUnrevealedMod(item));
		EXPECT_EQ(Mods(item).size(), before + (IsDesecratedFamily(offers[0].family) ? 0 : 1));
		ExpectSameMods(item, Rebuilt(item));
		EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Desecrate, *MyPlayer, item));
	}
}

TEST_F(Poe2CraftingTest, DesecrationNeedsARare)
{
	Item item = Crafted(CurrencyOrb::Transmutation, MakeItem("Full Plate Mail", 3, ITEM_QUALITY_NORMAL, 80));
	EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Desecrate, *MyPlayer, item));
}

TEST_F(Poe2CraftingTest, NecromancyOmensPickTheSide)
{
	for (const auto &[omen, prefix] : { std::pair { OmenId::SinistralNecromancy, true }, std::pair { OmenId::DextralNecromancy, false } }) {
		MyPlayer->_pNumInv = 0;
		GiveOmen(omen);
		Item item = Crafted(CurrencyOrb::Alchemy, MakeItem("Full Plate Mail", 8, ITEM_QUALITY_NORMAL, 80));
		item = Crafted(CurrencyOrb::Desecrate, item);
		for (const Poe2CraftMod &mod : item._iPoe2Craft.mods) {
			if (mod.family == UnrevealedModFamily)
				EXPECT_EQ(IsCraftPrefix(mod), prefix);
		}
		ConsumeCraftOmens(*MyPlayer);
		EXPECT_EQ(MyPlayer->_pNumInv, 0);
	}
}

TEST_F(Poe2CraftingTest, DesecratedModsAddTheirRules)
{
	Item item = Crafted(CurrencyOrb::Alchemy, MakeItem("Full Plate Mail", 4, ITEM_QUALITY_NORMAL, 80));
	Poe2Craft craft = item._iPoe2Craft;
	RemoveCraftMod(craft, 0);
	ASSERT_TRUE(AddCraftMod(craft, { static_cast<uint8_t>(DesecratedFamilyBase + static_cast<uint8_t>(DesecratedModId::HungerOfTheWound) - 1), 1, 0 }, 1));
	item = RebuildWithCraft(*MyPlayer, item, item.dwBuff, craft);
	EXPECT_TRUE(HasDesecratedMod(item, DesecratedModId::HungerOfTheWound));
	EXPECT_EQ(Mods(item).size(), 3U);
	UniqueRuleTotals totals {};
	AddCraftRules(item, totals);
	EXPECT_EQ(totals[static_cast<size_t>(UniqueRule::MissingLifeDamage)], 2);
	EXPECT_EQ(totals[static_cast<size_t>(UniqueRule::LessLifeRecovery)], 15);
	// It still takes its slot: the item has room for two more.
	item = Crafted(CurrencyOrb::Exalted, item);
	item = Crafted(CurrencyOrb::Exalted, item);
	EXPECT_FALSE(ApplyCurrencyOrb(CurrencyOrb::Exalted, *MyPlayer, item));
	EXPECT_EQ(UnpackCraftState(PackCraftState(item._iPoe2Craft)), item._iPoe2Craft);
}

TEST_F(Poe2CraftingTest, HollowSaintStrengthensAugmentsAndTakesASlot)
{
	Item item = Crafted(CurrencyOrb::Alchemy, MakeItem("Iron Cap", 4, ITEM_QUALITY_NORMAL, 80));
	item = Crafted(CurrencyOrb::Artificer, item);
	item = Crafted(CurrencyOrb::Augment, item, static_cast<uint8_t>(AugmentId::IronRune));
	const int plain = item._iPLAC;
	Poe2Craft craft = item._iPoe2Craft;
	RemoveCraftMod(craft, 0);
	ASSERT_TRUE(AddCraftMod(craft, { static_cast<uint8_t>(DesecratedFamilyBase + static_cast<uint8_t>(DesecratedModId::HollowSaint) - 1), 1, 0 }, 1));
	Item hollow = RebuildWithCraft(*MyPlayer, item, item.dwBuff, craft);
	Poe2Craft without = craft;
	without.augments = {};
	const Item bare = RebuildWithCraft(*MyPlayer, item, item.dwBuff, without);
	Item plainBare = item;
	plainBare._iPoe2Craft.augments = {};
	plainBare = RebuildWithCraft(*MyPlayer, item, item.dwBuff, plainBare._iPoe2Craft);
	EXPECT_GT(hollow._iPLAC - bare._iPLAC, plain - plainBare._iPLAC);
	EXPECT_EQ(CraftModLimit(hollow, MaxRareModsPerSide), 5);
}

} // namespace
} // namespace devilution
