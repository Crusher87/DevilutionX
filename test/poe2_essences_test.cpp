/**
 * @file poe2_essences_test.cpp
 *
 * Build spec step 7: the 19 essence families at their four strengths, the seven Evil essences, their drops, and the
 * primitives their modifiers needed.
 */
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "monster.h"
#include "player.h"
#include "poe2/bases.hpp"
#include "poe2/craft_state.hpp"
#include "poe2/crafting_items.hpp"
#include "poe2/currency.hpp"
#include "poe2/desecration.hpp"
#include "poe2/effect_primitives.hpp"
#include "poe2/item_stats.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/stash_layouts.hpp"
#include "poe2/support_table.hpp"
#include "qol/stash.h"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"
#include "utils/is_of.hpp"

namespace devilution {
namespace {

int Stat(const Item &item, Poe2ItemStat stat)
{
	return item._iPoe2Stats[static_cast<size_t>(stat)];
}

void SetGearStat(Player &player, Poe2ItemStat stat, int value)
{
	player.poe2.itemStats[static_cast<size_t>(stat)] = static_cast<int16_t>(value);
}

class Poe2EssencesTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		MyPlayerId = 0;
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
		LoadPassiveTreeData();
		LoadModifierData();
	}

	void TearDown() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		MyPlayer->poe2.clear();
		gbIsPoe2 = false;
		Stash = {};
		StashTabs.reset();
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

	static Item MakeItem(std::string_view baseName, uint32_t seed, int itemLevel = 75)
	{
		Item item = {};
		SetLootRarity(item, ITEM_QUALITY_NORMAL);
		SetPoe2ItemLevel(item, itemLevel);
		SetRndSeed(seed);
		SetupAllItems(*MyPlayer, item, FindBase(baseName), AdvanceRndSeed(), 30, 1, /*onlygood=*/true, /*pregen=*/false, 0, /*forceNotUnique=*/true);
		item._iIdentified = true;
		EXPECT_FALSE(item.isEmpty()) << baseName;
		return item;
	}

	static Item Magic(std::string_view baseName, uint32_t seed)
	{
		Item item = MakeItem(baseName, seed);
		EXPECT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Transmutation, *MyPlayer, item)) << baseName;
		return item;
	}

	static Item Rare(std::string_view baseName, uint32_t seed)
	{
		Item item = MakeItem(baseName, seed);
		EXPECT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Alchemy, *MyPlayer, item)) << baseName;
		return item;
	}

	static bool Use(EssenceKind kind, EssenceTier tier, Item &item)
	{
		return ApplyCurrencyOrb(CurrencyOrb::Essence, *MyPlayer, item, EssenceVariant(kind, tier));
	}

	/** The essence used on a fresh item of the base (magic, or rare for a Perfect or Evil one), trying seeds until it takes. */
	static std::optional<Item> Essenced(EssenceKind kind, EssenceTier tier, std::string_view baseName)
	{
		const bool onRare = IsEvilEssence(kind) || tier == EssenceTier::Perfect;
		for (uint32_t seed = 1; seed <= 20; seed++) {
			Item item = onRare ? Rare(baseName, seed) : Magic(baseName, seed);
			if (Use(kind, tier, item))
				return item;
		}
		return std::nullopt;
	}

	/** The values of the item's modifier of the family, if it has one. */
	static std::optional<std::array<int16_t, 2>> ModValues(const Item &item, std::string_view familyId)
	{
		const std::optional<uint8_t> family = FindModifierFamily(familyId);
		if (!family)
			return std::nullopt;
		for (const Poe2ItemMod &mod : item._iPoe2Mods) {
			if (mod.family == *family + 1)
				return mod.values;
		}
		return std::nullopt;
	}

	static bool HasAnyFamily(const Item &item, const std::vector<uint8_t> &families)
	{
		return std::any_of(item._iPoe2Mods.begin(), item._iPoe2Mods.end(), [&](const Poe2ItemMod &mod) {
			return mod.family != 0 && std::find(families.begin(), families.end(), static_cast<uint8_t>(mod.family - 1)) != families.end();
		});
	}

	static Item EssenceItem(EssenceKind kind, EssenceTier tier)
	{
		Item item {};
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iMiscId == IMISC_ESSENCE && AllItemsList[i].dropRate > 0) {
				InitializeItem(item, static_cast<_item_indexes>(i));
				break;
			}
		}
		EXPECT_FALSE(item.isEmpty());
		item.dwBuff |= CF_POE2;
		SetCraftingItemVariant(item, EssenceVariant(kind, tier));
		FinishCraftingItem(item);
		item._iIdentified = true;
		return item;
	}

	static int MaxQualityWithCatalysts(Item item)
	{
		for (int i = 0; i < 40; i++) {
			if (!ApplyCurrencyOrb(CurrencyOrb::Catalyst, *MyPlayer, item, static_cast<uint8_t>(Catalyst::Vital)))
				break;
		}
		return ItemQuality(item);
	}

	static constexpr std::string_view Bases[] = {
		"Full Plate Mail",
		"Iron Cap",
		"Wooden Tower Shield",
		"Ruby Ring",
		"Gold Amulet",
		"Wooden Focus",
		"Shortsword",
		"Splitting Greataxe",
		"Crude Bow",
		"Gelid Staff",
		"Attuned Wand",
		"Fine Rattling Sceptre",
	};
};

constexpr EssenceTier Tiers[] = { EssenceTier::Lesser, EssenceTier::Normal, EssenceTier::Greater, EssenceTier::Perfect };

TEST_F(Poe2EssencesTest, EveryEssenceOfTodayDropsAndNamesItsModifiers)
{
	int current = 0;
	int evil = 0;
	for (int k = LegacyEssenceKindCount + 1; k < static_cast<int>(EssenceKind::Count); k++) {
		const EssenceKind kind = static_cast<EssenceKind>(k);
		const Essence &essence = GetEssence(kind);
		EXPECT_TRUE(essence.drops) << essence.name;
		EXPECT_FALSE(essence.text.empty()) << essence.name;
		if (IsEvilEssence(kind)) {
			evil++;
			EXPECT_EQ(essence.dropAct, 3) << essence.name;
			EXPECT_GT(essence.dropWeight, 0) << essence.name;
			// Each adds a modifier of its lines or leaves its mark, never both.
			EXPECT_NE(essence.lines.empty(), essence.special == EssenceSpecial::None) << essence.name;
		} else {
			current++;
			EXPECT_EQ(essence.special, EssenceSpecial::None) << essence.name;
			EXPECT_FALSE(essence.lines.empty()) << essence.name;
			EXPECT_FALSE(essence.perfect.empty()) << essence.name;
			EXPECT_FALSE(essence.perfectText.empty()) << essence.name;
		}
		for (const std::vector<std::string> *list : { &essence.lines, &essence.perfect }) {
			for (const std::string &id : *list) {
				const std::optional<uint8_t> family = FindModifierFamily(id);
				ASSERT_TRUE(family) << essence.name << " " << id;
				EXPECT_TRUE(ModifierFamilies()[*family].enabled) << essence.name << " " << id;
			}
		}
	}
	EXPECT_EQ(current, 19);
	EXPECT_EQ(evil, 7);
	EXPECT_EQ(GetEssence(EssenceKind::Body).name, "the Body");
	EXPECT_EQ(GetEssence(EssenceKind::SpellSorcery).name, "Sorcery");
	EXPECT_EQ(GetEssence(EssenceKind::Ruin).family, "chaos_resistance") << "Ruin stays chaos resistance";
	EXPECT_EQ(GetEssence(EssenceKind::Hatred).special, EssenceSpecial::Hatred);
	EXPECT_EQ(GetEssence(EssenceKind::Sin).special, EssenceSpecial::Sin);
}

TEST_F(Poe2EssencesTest, TheEssenceOnlyModifiersNeverRollNaturally)
{
	for (size_t i = EssenceModFamilyStart; i < ModifierFamilies().size(); i++) {
		const ModFamily &family = ModifierFamilies()[i];
		EXPECT_TRUE(family.enabled) << family.id;
		EXPECT_FALSE(family.natural) << family.id;
	}
	// Alchemy on many items never finds one.
	for (const std::string_view base : Bases) {
		for (uint32_t seed = 1; seed <= 15; seed++) {
			const Item item = Rare(base, seed);
			for (const Poe2ItemMod &mod : item._iPoe2Mods)
				EXPECT_TRUE(mod.family == 0 || static_cast<size_t>(mod.family - 1) < EssenceModFamilyStart || static_cast<size_t>(mod.family - 1) >= ModifierFamilies().size()) << base << " " << seed;
		}
	}
}

TEST_F(Poe2EssencesTest, EveryEssenceAddsOneOfItsModifiersSomewhereAtEveryStrength)
{
	for (int k = LegacyEssenceKindCount + 1; k < static_cast<int>(FirstEvilEssence); k++) {
		const EssenceKind kind = static_cast<EssenceKind>(k);
		const Essence &essence = GetEssence(kind);
		for (const EssenceTier tier : Tiers) {
			int worked = 0;
			for (const std::string_view base : Bases) {
				const Item probe = MakeItem(base, 1);
				const std::vector<uint8_t> families = EssenceFamiliesForItem(essence, tier, probe);
				Item item = tier == EssenceTier::Perfect ? Rare(base, 1) : Magic(base, 1);
				if (families.empty()) {
					EXPECT_FALSE(Use(kind, tier, item)) << essence.name << " on " << base;
					continue;
				}
				const std::optional<Item> crafted = Essenced(kind, tier, base);
				ASSERT_TRUE(crafted) << essence.name << " tier " << static_cast<int>(tier) << " on " << base;
				EXPECT_EQ(crafted->_iMagical, ITEM_QUALITY_RARE);
				EXPECT_TRUE(HasAnyFamily(*crafted, families)) << essence.name << " tier " << static_cast<int>(tier) << " on " << base;
				worked++;
			}
			EXPECT_GT(worked, 0) << essence.name << " tier " << static_cast<int>(tier);
		}
	}
}

TEST_F(Poe2EssencesTest, PerfectEssencesAddTheirOwnLines)
{
	struct Case {
		EssenceKind kind;
		std::string_view base;
		std::string_view family;
		int min;
		int max;
	};
	const Case cases[] = {
		{ EssenceKind::Body, "Full Plate Mail", "essence_life_percent", 8, 10 },
		{ EssenceKind::Mind, "Ruby Ring", "essence_mana_percent", 4, 6 },
		{ EssenceKind::Enhancement, "Gold Amulet", "essence_global_defences", 20, 30 },
		{ EssenceKind::Flames, "Shortsword", "essence_gain_as_fire", 15, 20 },
		{ EssenceKind::Flames, "Splitting Greataxe", "essence_gain_as_fire_2h", 25, 33 },
		{ EssenceKind::Ice, "Crude Bow", "essence_gain_as_cold", 15, 20 },
		{ EssenceKind::Electricity, "Splitting Greataxe", "essence_gain_as_lightning_2h", 25, 33 },
		{ EssenceKind::Ruin, "Full Plate Mail", "essence_max_chaos_resistance", 2, 3 },
		// Kept in tenths of a percent.
		{ EssenceKind::Seeking, "Shortsword", "essence_attack_crit", 40, 50 },
		{ EssenceKind::Alacrity, "Attuned Wand", "essence_mana_cost", 18, 20 },
		{ EssenceKind::Alacrity, "Wooden Focus", "essence_mana_cost", 18, 20 },
		{ EssenceKind::Alacrity, "Gelid Staff", "essence_mana_cost_staff", 28, 32 },
		{ EssenceKind::Opulence, "Ruby Ring", "essence_gold_find", 10, 15 },
		{ EssenceKind::Insulation, "Wooden Tower Shield", "essence_fire_recoup", 26, 30 },
		{ EssenceKind::Thawing, "Iron Cap", "essence_cold_recoup", 26, 30 },
		{ EssenceKind::Grounding, "Ruby Ring", "essence_lightning_recoup", 26, 30 },
		{ EssenceKind::Command, "Gold Amulet", "minion_skill_levels", 1, 4 },
	};
	for (const Case &c : cases) {
		const std::optional<Item> item = Essenced(c.kind, EssenceTier::Perfect, c.base);
		ASSERT_TRUE(item) << GetEssence(c.kind).name << " on " << c.base;
		const auto values = ModValues(*item, c.family);
		ASSERT_TRUE(values) << GetEssence(c.kind).name << " on " << c.base;
		EXPECT_GE((*values)[0], c.min) << c.family;
		EXPECT_LE((*values)[0], c.max) << c.family;
	}
	// The stats the new lines feed.
	const std::optional<Item> plate = Essenced(EssenceKind::Body, EssenceTier::Perfect, "Full Plate Mail");
	ASSERT_TRUE(plate);
	EXPECT_GE(Stat(*plate, Poe2ItemStat::LifePercent), 8);
	const std::optional<Item> ring = Essenced(EssenceKind::Opulence, EssenceTier::Perfect, "Ruby Ring");
	ASSERT_TRUE(ring);
	EXPECT_GE(Stat(*ring, Poe2ItemStat::GoldFind), 10);
	const std::optional<Item> amulet = Essenced(EssenceKind::Enhancement, EssenceTier::Perfect, "Gold Amulet");
	ASSERT_TRUE(amulet);
	const int defences = (*ModValues(*amulet, "essence_global_defences"))[0];
	EXPECT_GE(Stat(*amulet, Poe2ItemStat::ArmourPercent), defences);
	EXPECT_GE(Stat(*amulet, Poe2ItemStat::EvasionPercent), defences);
	EXPECT_GE(Stat(*amulet, Poe2ItemStat::EnergyShieldPercent), defences);
	const std::optional<Item> wand = Essenced(EssenceKind::Alacrity, EssenceTier::Perfect, "Attuned Wand");
	ASSERT_TRUE(wand);
	EXPECT_GE(Stat(*wand, Poe2ItemStat::ManaCostEfficiency), 18);
	// A Perfect essence swaps a modifier: the count stays.
	const Item rare = Rare("Full Plate Mail", 3);
	Item swapped = rare;
	ASSERT_TRUE(Use(EssenceKind::Body, EssenceTier::Perfect, swapped));
	EXPECT_EQ(RareAffixCount(swapped), RareAffixCount(rare));
}

TEST_F(Poe2EssencesTest, StrengthsNeedTheRightRarity)
{
	Item normal = MakeItem("Full Plate Mail", 4);
	EXPECT_FALSE(Use(EssenceKind::Body, EssenceTier::Lesser, normal));
	Item magic = Magic("Full Plate Mail", 4);
	EXPECT_FALSE(Use(EssenceKind::Body, EssenceTier::Perfect, magic));
	EXPECT_FALSE(Use(EssenceKind::Terror, EssenceTier::Normal, magic));
	Item rare = Rare("Full Plate Mail", 4);
	EXPECT_FALSE(Use(EssenceKind::Body, EssenceTier::Greater, rare));
	// The outline while it's held says the same.
	EXPECT_FALSE(CurrencyFitsItem(CurrencyOrb::Essence, rare, EssenceVariant(EssenceKind::Body, EssenceTier::Greater)));
	EXPECT_TRUE(CurrencyFitsItem(CurrencyOrb::Essence, rare, EssenceVariant(EssenceKind::Body, EssenceTier::Perfect)));
	EXPECT_TRUE(CurrencyFitsItem(CurrencyOrb::Essence, rare, EssenceVariant(EssenceKind::Hatred, EssenceTier::Normal)));
	EXPECT_TRUE(CurrencyFitsItem(CurrencyOrb::Essence, magic, EssenceVariant(EssenceKind::Body, EssenceTier::Lesser)));
	EXPECT_FALSE(CurrencyFitsItem(CurrencyOrb::Essence, magic, EssenceVariant(EssenceKind::Sin, EssenceTier::Normal)));
}

TEST_F(Poe2EssencesTest, GreaterEssencesAddHigherModifiers)
{
	const std::optional<uint8_t> life = FindModifierFamily("maximum_life");
	ASSERT_TRUE(life);
	for (uint32_t seed = 1; seed <= 6; seed++) {
		Item item = Magic("Full Plate Mail", seed);
		if (!Use(EssenceKind::Body, EssenceTier::Greater, item))
			continue;
		for (const Poe2ItemMod &mod : item._iPoe2Mods) {
			if (mod.family == *life + 1) {
				EXPECT_GE(ModTierItemLevel(mod.family, mod.tier), EssenceModLevelFloors[static_cast<size_t>(EssenceTier::Greater)]);
			}
		}
	}
}

TEST_F(Poe2EssencesTest, TerrorAddsTheLineOfTheItemsSlot)
{
	struct Case {
		std::string_view base;
		std::string_view family;
		int min;
		int max;
	};
	const Case cases[] = {
		{ "Iron Cap", "terror_minion_levels", 1, 1 },
		{ "Full Plate Mail", "terror_thorns", 41, 56 },
		{ "Wooden Tower Shield", "terror_block", 20, 24 },
		{ "Ruby Ring", "terror_mana_regen", 50, 59 },
		{ "Gold Amulet", "terror_life_recoup", 19, 21 },
	};
	for (const Case &c : cases) {
		const std::optional<Item> item = Essenced(EssenceKind::Terror, EssenceTier::Normal, c.base);
		ASSERT_TRUE(item) << c.base;
		const auto values = ModValues(*item, c.family);
		ASSERT_TRUE(values) << c.base;
		EXPECT_GE((*values)[0], c.min) << c.base;
		EXPECT_LE((*values)[0], c.max) << c.base;
	}
	const std::optional<Item> shield = Essenced(EssenceKind::Terror, EssenceTier::Normal, "Wooden Tower Shield");
	ASSERT_TRUE(shield);
	EXPECT_GE(Stat(*shield, Poe2ItemStat::IncreasedBlock), 20);
	// It has no line for weapons.
	Item sword = Rare("Shortsword", 2);
	const Item before = sword;
	EXPECT_FALSE(Use(EssenceKind::Terror, EssenceTier::Normal, sword));
	EXPECT_EQ(sword.dwBuff, before.dwBuff);
}

TEST_F(Poe2EssencesTest, AnguishPoisonsWithAttacks)
{
	const std::optional<Item> axe = Essenced(EssenceKind::Anguish, EssenceTier::Normal, "Splitting Greataxe");
	ASSERT_TRUE(axe);
	const auto values = ModValues(*axe, "anguish_poison");
	ASSERT_TRUE(values);
	EXPECT_GE((*values)[0], 30);
	EXPECT_LE((*values)[0], 40);
	EXPECT_GE((*values)[1], 40);
	EXPECT_LE((*values)[1], 60);
	EXPECT_EQ(Stat(*axe, Poe2ItemStat::PoisonChance), (*values)[0]);
	EXPECT_GE(Stat(*axe, Poe2ItemStat::PoisonMagnitude), (*values)[1]);
	Item ring = Rare("Ruby Ring", 2);
	EXPECT_FALSE(Use(EssenceKind::Anguish, EssenceTier::Normal, ring));
}

TEST_F(Poe2EssencesTest, HatredStrengthensSocketedAugmentsButNotResistances)
{
	Item plate = Rare("Full Plate Mail", 5);
	ASSERT_TRUE(Use(EssenceKind::Hatred, EssenceTier::Normal, plate));
	EXPECT_TRUE(HasHatredMark(plate._iPoe2Craft));
	EXPECT_EQ(CraftEnchantId(plate._iPoe2Craft), 0);
	// The same item without the mark.
	Poe2Craft plain = plate._iPoe2Craft;
	plain.enchant = static_cast<uint8_t>(plain.enchant & ~HatredEnchantBit);
	Item without;
	RecreateItem(*MyPlayer, without, plate.IDidx, plate._iCreateInfo, plate._iSeed, plate._ivalue, plate.dwBuff, plain);
	without._iIdentified = true;
	for (Item *item : { &plate, &without }) {
		for (const AugmentId rune : { AugmentId::LesserShael, AugmentId::Ral }) {
			ASSERT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Artificer, *MyPlayer, *item));
			ASSERT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Augment, *MyPlayer, *item, static_cast<uint8_t>(rune)));
		}
	}
	// Lesser Shael's 8 dexterity becomes 12; Lesser Ral's 8% fire resistance stays.
	EXPECT_EQ(plate._iPLDex - without._iPLDex, 4);
	EXPECT_EQ(plate._iPLFR, without._iPLFR);
	EXPECT_TRUE(HasHatredMark(plate._iPoe2Craft)) << "Socketing keeps it";
	// Once is enough.
	EXPECT_FALSE(Use(EssenceKind::Hatred, EssenceTier::Normal, plate));
	// Jewellery has no augment sockets.
	Item ring = Rare("Ruby Ring", 5);
	EXPECT_FALSE(Use(EssenceKind::Hatred, EssenceTier::Normal, ring));
}

TEST_F(Poe2EssencesTest, DestructionMakesTheNextCorruptionGiveTwoEnchantments)
{
	for (uint32_t seed = 1; seed <= 8; seed++) {
		Item plate = Rare("Full Plate Mail", seed);
		ASSERT_GE(CorruptionEnchantChoices(plate).size(), 2U);
		ASSERT_TRUE(Use(EssenceKind::Destruction, EssenceTier::Normal, plate));
		EXPECT_TRUE(HasDestructionMark(plate._iPoe2Craft));
		EXPECT_FALSE(EssenceMarkText(plate).empty());
		ASSERT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Vaal, *MyPlayer, plate)) << seed;
		EXPECT_NE(plate.dwBuff & CF_POE2_CORRUPTED, 0U);
		EXPECT_FALSE(HasDestructionMark(plate._iPoe2Craft)) << "Used up";
		EXPECT_NE(CraftEnchantId(plate._iPoe2Craft), 0) << seed;
		EXPECT_NE(SecondEnchantId(plate._iPoe2Craft), 0) << seed;
		EXPECT_NE(SecondEnchantId(plate._iPoe2Craft), CraftEnchantId(plate._iPoe2Craft)) << seed;
		EXPECT_FALSE(CorruptionEnchantText(plate).empty());
		EXPECT_FALSE(SecondCorruptionEnchantText(plate).empty());
		EXPECT_NE(CorruptionEnchantText(plate), SecondCorruptionEnchantText(plate));
		// The rebuild from the seed keeps both.
		Item rebuilt;
		RecreateItem(*MyPlayer, rebuilt, plate.IDidx, plate._iCreateInfo, plate._iSeed, plate._ivalue, plate.dwBuff, plate._iPoe2Craft);
		EXPECT_EQ(SecondCorruptionEnchantText(rebuilt), SecondCorruptionEnchantText(plate));
	}
}

TEST_F(Poe2EssencesTest, LiesAddsAHiddenDesecratedModifier)
{
	Item plate = Rare("Full Plate Mail", 6);
	const int mods = CraftModCount(CurrentCraft(plate));
	ASSERT_TRUE(Use(EssenceKind::Lies, EssenceTier::Normal, plate));
	EXPECT_TRUE(plate._iPoe2Craft.desecrated);
	EXPECT_TRUE(HasUnrevealedMod(plate));
	EXPECT_EQ(CraftModCount(CurrentCraft(plate)), mods) << "One removed, the hidden one added";
	EXPECT_FALSE(Use(EssenceKind::Lies, EssenceTier::Normal, plate)) << "Already desecrated";
}

TEST_F(Poe2EssencesTest, SinAllocatesANotableWhileTheBodyArmourIsWorn)
{
	Item plate = Rare("Full Plate Mail", 7);
	ASSERT_TRUE(Use(EssenceKind::Sin, EssenceTier::Normal, plate));
	const std::optional<uint16_t> node = SinNotable(plate._iPoe2Craft);
	ASSERT_TRUE(node);
	ASSERT_LT(*node, PassiveTree().size());
	EXPECT_TRUE(CanBeItemNotable(PassiveTree()[*node]));
	const std::vector<std::string> marks = EssenceMarkText(plate);
	ASSERT_FALSE(marks.empty());
	EXPECT_NE(marks.front().find(PassiveTree()[*node].name), std::string::npos) << marks.front();

	Player &player = *MyPlayer;
	const PassiveTotals bare = GetPassiveTotals(player);
	player.InvBody[INVLOC_CHEST] = plate;
	player.InvBody[INVLOC_CHEST]._iStatFlag = true;
	EXPECT_EQ(WornItemNotable(player), std::optional<PassiveNodeId>(*node));
	const PassiveTotals worn = GetPassiveTotals(player);
	for (const auto &[stat, value] : PassiveTree()[*node].stats)
		EXPECT_EQ(worn[static_cast<size_t>(stat)] - bare[static_cast<size_t>(stat)], value);
	// In the bag it does nothing.
	player.InvBody[INVLOC_CHEST] = {};
	EXPECT_EQ(GetPassiveTotals(player), bare);
	// Body armour only, once.
	EXPECT_FALSE(Use(EssenceKind::Sin, EssenceTier::Normal, plate));
	Item cap = Rare("Iron Cap", 7);
	EXPECT_FALSE(Use(EssenceKind::Sin, EssenceTier::Normal, cap));
	// The notables an item can give are the tree's plain ones.
	for (uint32_t seed = 0; seed < 50; seed++) {
		const std::optional<PassiveNodeId> rolled = RollItemNotable(seed);
		ASSERT_TRUE(rolled);
		EXPECT_EQ(PassiveTree()[*rolled].kind, PassiveKind::Notable);
		EXPECT_FALSE(PassiveTree()[*rolled].ascendancyRoot);
	}
}

TEST_F(Poe2EssencesTest, PainLetsJewelleryQualityReachThirty)
{
	const Item ring = Rare("Ruby Ring", 8);
	EXPECT_EQ(MaxQualityWithCatalysts(ring), 20);
	Item pained = ring;
	ASSERT_TRUE(Use(EssenceKind::Pain, EssenceTier::Normal, pained));
	EXPECT_TRUE(HasPainMark(pained._iPoe2Craft));
	EXPECT_EQ(MaxQualityWithCatalysts(pained), PainQualityLimit);
	EXPECT_LE(PainQualityLimit, MaxExceptionalQuality) << "The save keeps it";
	Item plate = Rare("Full Plate Mail", 8);
	EXPECT_FALSE(Use(EssenceKind::Pain, EssenceTier::Normal, plate));
}

TEST_F(Poe2EssencesTest, DropsFollowAreaLevelAndAct)
{
	const auto rolls = [](int areaLevel, int act) {
		std::vector<uint8_t> variants;
		for (uint32_t i = 0; i < 4000; i++)
			variants.push_back(RollEssenceDrop(i * 2654435761U, areaLevel, act));
		return variants;
	};
	// Lesser ones wherever essences drop at all (the loot's CurrencyDropLevel), better ones from their area levels.
	for (const uint8_t variant : rolls(25, 1)) {
		const EssenceKind kind = EssenceVariantKind(variant);
		EXPECT_TRUE(GetEssence(kind).drops);
		EXPECT_EQ(CurrentEssenceKind(kind), kind);
		EXPECT_FALSE(IsEvilEssence(kind)) << "Evil essences wait for act 3";
		EXPECT_EQ(EssenceVariantTier(variant), EssenceTier::Lesser);
	}
	std::array<int, 4> tiers {};
	int evilCount = 0;
	std::vector<EssenceKind> evilSeen;
	const std::vector<uint8_t> late = rolls(75, 3);
	for (const uint8_t variant : late) {
		const EssenceKind kind = EssenceVariantKind(variant);
		ASSERT_NE(kind, EssenceKind::None);
		EXPECT_EQ(CurrentEssenceKind(kind), kind);
		if (IsEvilEssence(kind)) {
			evilCount++;
			if (std::find(evilSeen.begin(), evilSeen.end(), kind) == evilSeen.end())
				evilSeen.push_back(kind);
		} else {
			tiers[static_cast<size_t>(EssenceVariantTier(variant))]++;
		}
	}
	for (size_t tier = 0; tier < tiers.size(); tier++)
		EXPECT_GT(tiers[tier], 0) << tier;
	EXPECT_GT(tiers[0], tiers[3]) << "Perfect ones are the rarest";
	EXPECT_EQ(evilSeen.size(), 7U);
	EXPECT_LT(evilCount * 100 / static_cast<int>(late.size()), 6) << "Evil essences are rare";
	EXPECT_GT(evilCount, 0);
	// Perfect ones wait for area level 72.
	for (const uint8_t variant : rolls(60, 3)) {
		if (!IsEvilEssence(EssenceVariantKind(variant))) {
			EXPECT_NE(EssenceVariantTier(variant), EssenceTier::Perfect);
		}
	}
}

TEST_F(Poe2EssencesTest, TooltipsSayWhatEachDoes)
{
	for (int k = LegacyEssenceKindCount + 1; k < static_cast<int>(EssenceKind::Count); k++) {
		const EssenceKind kind = static_cast<EssenceKind>(k);
		for (const EssenceTier tier : Tiers) {
			const Item essence = EssenceItem(kind, tier);
			const std::vector<std::string> lines = CraftingItemText(essence);
			ASSERT_FALSE(lines.empty()) << essence._iIName;
			for (const std::string &line : lines) {
				EXPECT_EQ(line.find('{'), std::string::npos) << line;
				EXPECT_LE(line.size(), 120U) << line;
			}
			const bool rare = IsEvilEssence(kind) || tier == EssenceTier::Perfect;
			EXPECT_EQ(lines.front().rfind(rare ? "Rare item" : "Magic item", 0), 0U) << lines.front();
			if (IsEvilEssence(kind))
				break;
		}
	}
	EXPECT_EQ(std::string_view(EssenceItem(EssenceKind::Body, EssenceTier::Lesser)._iIName), "Lesser Essence of the Body");
	EXPECT_EQ(std::string_view(EssenceItem(EssenceKind::Terror, EssenceTier::Normal)._iIName), "Essence of Terror");
}

TEST_F(Poe2EssencesTest, EssencesWorkFromTheBagAndOnTheEssencesTabsMiddleSlot)
{
	// Another player, so nothing is sent over the network, which the tests don't start.
	Players.resize(2);
	MyPlayer = &Players[0];
	Player &player = Players[1];
	player = {};
	player.setCharacterLevel(60);
	const uint8_t variant = EssenceVariant(EssenceKind::Body, EssenceTier::Greater);
	const auto hold = [&](int slot) {
		player._pOilType = IMISC_ESSENCE;
		player.poe2.currencyVariant = variant;
		HoldCurrencyForUse(player.InvList[slot]);
	};

	// The bag: the essence, then a magic body armour.
	const Item magic = Magic("Full Plate Mail", 9);
	player.InvList[0] = EssenceItem(EssenceKind::Body, EssenceTier::Greater);
	player.InvList[1] = magic;
	player.InvGrid[0] = 1;
	player.InvGrid[1] = 2;
	player._pNumInv = 2;
	hold(0);
	ASSERT_TRUE(DoOil(player, NUM_INVLOC + 1));
	ASSERT_EQ(player._pNumInv, 1) << "The essence is used up";
	EXPECT_EQ(player.InvList[0]._iSeed, magic._iSeed);
	EXPECT_EQ(player.InvList[0]._iMagical, ITEM_QUALITY_RARE);

	// The Essences tab's middle slot holds the item; the essence comes from the bag.
	Stash = {};
	StashTabs.reset();
	unsigned page = StashTabs.count();
	for (unsigned i = 0; i < StashTabs.count(); i++) {
		if (StashTabs.kind(i) == StashTabKind::Essences)
			page = i;
	}
	ASSERT_LT(page, StashTabs.count());
	Item gear = Magic("Full Plate Mail", 10);
	const uint32_t gearSeed = gear._iSeed;
	ASSERT_TRUE(StoreInEssenceCraftSlot(page, gear));
	const StashStruct::StashCell cell = StashTabSlotItem(page, EssenceCraftSlot);
	ASSERT_NE(cell, StashStruct::EmptyCell);
	player.InvList[0] = EssenceItem(EssenceKind::Body, EssenceTier::Greater);
	player.InvGrid[0] = 1;
	player.InvGrid[1] = 0;
	player._pNumInv = 1;
	hold(0);
	ASSERT_TRUE(DoOilOnStashItem(player, cell));
	EXPECT_EQ(player._pNumInv, 0) << "The essence is used up";
	const Item &crafted = Stash.stashList[StashTabSlotItem(page, EssenceCraftSlot)];
	EXPECT_EQ(crafted._iSeed, gearSeed);
	EXPECT_EQ(crafted._iMagical, ITEM_QUALITY_RARE);
	EXPECT_TRUE(ModValues(crafted, "maximum_life"));
}

TEST_F(Poe2EssencesTest, EssenceMarksSurviveTheCraftBytes)
{
	Poe2Craft craft {};
	craft.explicitMods = true;
	craft.rarity = ITEM_QUALITY_RARE;
	craft.enchant = static_cast<uint8_t>(HatredEnchantBit | 3);
	craft.enchantRoll = 5;
	craft.extension[EssenceFlagsByte] = static_cast<uint8_t>(DestructionMark | PainMark);
	SetSecondEnchant(craft, 7, 6);
	SetSinNotable(craft, 1234);

	const Poe2Craft state = UnpackCraftState(PackCraftState(craft));
	EXPECT_TRUE(HasHatredMark(state));
	EXPECT_EQ(CraftEnchantId(state), 3);
	EXPECT_EQ(state.enchantRoll, 5);

	const std::optional<std::array<uint8_t, CraftExtensionSaveSize>> bytes = PackCraftExtension(craft);
	ASSERT_TRUE(bytes);
	Poe2Craft loaded {};
	UnpackCraftExtension(*bytes, loaded);
	EXPECT_TRUE(HasDestructionMark(loaded));
	EXPECT_TRUE(HasPainMark(loaded));
	EXPECT_EQ(SecondEnchantId(loaded), 7);
	EXPECT_EQ(SecondEnchantRoll(loaded), 6);
	EXPECT_EQ(SinNotable(loaded), std::optional<uint16_t>(1234));
	// None of it: nothing saved, as before.
	EXPECT_FALSE(PackCraftExtension(Poe2Craft {}));
	EXPECT_FALSE(SinNotable(Poe2Craft {}));
}

TEST_F(Poe2EssencesTest, RecoupByElementAddsToTheGeneralOne)
{
	Player &player = *MyPlayer;
	SetGearStat(player, Poe2ItemStat::LifeRecoup, 20);
	SetGearStat(player, Poe2ItemStat::FireRecoup, 28);
	SetGearStat(player, Poe2ItemStat::ColdRecoup, 27);
	SetGearStat(player, Poe2ItemStat::LightningRecoup, 90);
	EXPECT_EQ(LifeRecoupPercent(player), 20);
	EXPECT_EQ(LifeRecoupPercent(player, DamageType::Physical), 20);
	EXPECT_EQ(LifeRecoupPercent(player, DamageType::Fire), 48);
	EXPECT_EQ(LifeRecoupPercent(player, DamageType::Cold), 47);
	EXPECT_EQ(LifeRecoupPercent(player, DamageType::Lightning), 100) << "Never more than all of it";
	EXPECT_EQ(LifeRecoupPercent(player, DamageType::Chaos), 20);
	gbIsPoe2 = false;
	EXPECT_EQ(LifeRecoupPercent(player, DamageType::Fire), 0);
	gbIsPoe2 = true;
}

TEST_F(Poe2EssencesTest, GoldManaCostAndBlockPrimitives)
{
	Player &player = *MyPlayer;
	EXPECT_EQ(GearGoldFindPercent(player), 0);
	EXPECT_EQ(GearManaCostPercent(player), 100);
	SetGearStat(player, Poe2ItemStat::GoldFind, 15);
	SetGearStat(player, Poe2ItemStat::ManaCostEfficiency, 25);
	EXPECT_EQ(GearGoldFindPercent(player), 15);
	EXPECT_EQ(GearManaCostPercent(player), 80);
	SetGearStat(player, Poe2ItemStat::ManaCostEfficiency, 30);
	EXPECT_EQ(GearManaCostPercent(player), 76);
	gbIsPoe2 = false;
	EXPECT_EQ(GearGoldFindPercent(player), 0);
	EXPECT_EQ(GearManaCostPercent(player), 100);
	gbIsPoe2 = true;

	EXPECT_EQ(ShieldBlockAfterIncrease(25, 0), 25);
	EXPECT_EQ(ShieldBlockAfterIncrease(25, 20), 30);
	EXPECT_EQ(ShieldBlockAfterIncrease(25, 24), 31);
	EXPECT_EQ(ShieldBlockAfterIncrease(0, 24), 0);
}

TEST_F(Poe2EssencesTest, GearPoisonsOnAttackHitsOnly)
{
	Player &player = *MyPlayer;
	player._pClass = HeroClass::Warrior;
	player.setCharacterLevel(20);
	LoadMonsterData();
	SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
	ActiveMonsterCount = 0;
	LevelMonsterTypes[0].type = MT_NZOMBIE;
	Monster &monster = Monsters[0];
	const auto reset = [&]() {
		monster = {};
		monster.levelType = 0;
		monster.talkMsg = TEXT_NONE;
		monster.mode = MonsterMode::Stand;
		monster.position.tile = { 41, 40 };
		monster.maxHitPoints = monster.hitPoints = Fixed26_6::fromInt(1000);
	};
	const SkillSlot attack = SkillSlot::ForDefault(0);
	const SkillSlot spell = SkillSlot::ForSpell(SpellID::Firebolt);
	ASSERT_NE(SkillTags(attack) & ST_Attack, 0U);
	ASSERT_EQ(SkillTags(spell) & ST_Attack, 0U);
	const int32_t base = Fixed26_6::fromInt(100).raw();

	reset();
	PoisonFromGear(player, monster, attack, base);
	EXPECT_EQ(monster.poe2.poisonTicks, 0) << "No chance on the gear";

	SetGearStat(player, Poe2ItemStat::PoisonChance, 100);
	EXPECT_EQ(GearPoisonOnHitChance(player), 100);
	SetRndSeed(7);
	PoisonFromGear(player, monster, spell, base);
	EXPECT_EQ(monster.poe2.poisonTicks, 0) << "Spells don't poison from it";
	PoisonFromGear(player, monster, attack, base);
	EXPECT_EQ(monster.poe2.poisonTicks, PoisonTicks);

	// About a third of the hits at 33%.
	SetGearStat(player, Poe2ItemStat::PoisonChance, 33);
	int poisoned = 0;
	SetRndSeed(11);
	for (int i = 0; i < 300; i++) {
		reset();
		PoisonFromGear(player, monster, attack, base);
		poisoned += monster.poe2.poisonTicks > 0 ? 1 : 0;
	}
	EXPECT_GT(poisoned, 60);
	EXPECT_LT(poisoned, 140);
}

} // namespace
} // namespace devilution
