/**
 * @file poe2_caster_affixes_test.cpp
 *
 * The caster weapons rework's modifier pools (caster-weapons-plan-v2.md 5.2 to 5.4): the families tools/poe2_modifiers.py
 * appends from tools/poe2_caster_affixes.tsv, their item levels and sizes on the game's curve, one damage prefix per
 * staff or wand, and first-element exclusivity.
 */
#include <algorithm>
#include <array>
#include <bit>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "poe2/crafting_items.hpp"
#include "poe2/modifiers.hpp"
#include "spells.h"
#include "utils/is_of.hpp"

namespace devilution {
namespace {

class Poe2CasterAffixesTest : public ::testing::Test {
public:
	void SetUp() override
	{
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

	static const ModFamily &Family(std::string_view id)
	{
		const std::optional<uint8_t> index = FindModifierFamily(id);
		if (!index) {
			ADD_FAILURE() << id;
			return ModifierFamilies().front();
		}
		return ModifierFamilies()[*index];
	}

	static const ModFamily &Family(const Poe2ItemMod &mod)
	{
		return *ItemModFamily(mod);
	}
};

/** The game's value of a table value at a scale, as modifiers.cpp rounds it. */
int Scaled(int value, int scale)
{
	if (scale == 100 || value == 0)
		return value;
	const int scaled = ((value * scale) + (value > 0 ? 50 : -50)) / 100;
	return scaled == 0 ? (value > 0 ? 1 : -1) : scaled;
}

/** A tier as the plan's 5.2 table gives it: item level, then the game's value range of the first and second stat. */
struct ShownTier {
	int itemLevel;
	int min1;
	int max1;
	int min2 = 0;
	int max2 = 0;
};

void ExpectTiers(const ModFamily &family, const std::vector<ShownTier> &expected, bool lastOnly = false)
{
	ASSERT_GE(family.tiers.size(), expected.size()) << family.id;
	for (size_t i = 0; i < expected.size(); i++) {
		// "T1 ... last": the first entries are T1 on, the last one the weakest tier.
		const size_t t = lastOnly && i + 1 == expected.size() ? family.tiers.size() - 1 : i;
		const ModTier &tier = family.tiers[t];
		const ShownTier &want = expected[i];
		EXPECT_EQ(tier.itemLevel, want.itemLevel) << family.id << " T" << t + 1;
		EXPECT_EQ(Scaled(tier.min[0], family.scale[0]), want.min1) << family.id << " T" << t + 1;
		EXPECT_EQ(Scaled(tier.max[0], family.scale[0]), want.max1) << family.id << " T" << t + 1;
		if (want.max2 != 0) {
			EXPECT_EQ(Scaled(tier.min[1], family.scale[1]), want.min2) << family.id << " T" << t + 1;
			EXPECT_EQ(Scaled(tier.max[1], family.scale[1]), want.max2) << family.id << " T" << t + 1;
		}
	}
}

TEST_F(Poe2CasterAffixesTest, FamiliesComeAfterTheOldOnesOnTheNewSlotsOnly)
{
	const std::vector<ModFamily> &families = ModifierFamilies();
	ASSERT_EQ(families.size(), LegacyModFamilyCount + 76U);
	constexpr uint32_t NewSlots = (1U << static_cast<uint32_t>(ModSlot::Staff)) | (1U << static_cast<uint32_t>(ModSlot::Wand)) | (1U << static_cast<uint32_t>(ModSlot::Sceptre));
	constexpr uint32_t LegacySlots = (1U << static_cast<uint32_t>(ModSlot::LegacyStaff)) | (1U << static_cast<uint32_t>(ModSlot::LegacySceptre));
	size_t tiers = 0;
	for (size_t i = 0; i < families.size(); i++) {
		const ModFamily &family = families[i];
		if (i < LegacyModFamilyCount) {
			// The old families never roll on the new bases.
			EXPECT_EQ(family.slots & NewSlots, 0U) << family.id;
			continue;
		}
		tiers += family.tiers.size();
		// Each new family lists exactly one of the new slots.
		EXPECT_EQ(family.slots & ~NewSlots, 0U) << family.id;
		EXPECT_EQ(std::popcount(family.slots & NewSlots), 1) << family.id;
		EXPECT_EQ(family.slots & LegacySlots, 0U) << family.id;
		// The item level curve: T1 at 82 at most, each tier below the one before.
		for (size_t t = 0; t < family.tiers.size(); t++) {
			EXPECT_LE(family.tiers[t].itemLevel, 83 - static_cast<int>(t + 1)) << family.id << " T" << t + 1;
			EXPECT_GE(family.tiers[t].itemLevel, 1) << family.id;
			if (family.id.ends_with("_skill_levels"))
				EXPECT_GE(family.tiers[t].itemLevel, 20) << family.id << " T" << t + 1;
		}
	}
	EXPECT_EQ(tiers, 494U);
}

TEST_F(Poe2CasterAffixesTest, TransformMatchesThePlansTable)
{
	ExpectTiers(Family("staff_spell_damage"), { { 82, 105, 119 }, { 81, 95, 104 }, { 78, 75, 94 }, { 60, 65, 74 }, { 43, 55, 64 }, { 21, 45, 54 }, { 10, 35, 44 }, { 1, 25, 34 } });
	ExpectTiers(Family("staff_spell_damage_mana"), { { 82, 45, 49, 83, 90 }, { 3, 15, 19, 34, 40 } }, true);
	ExpectTiers(Family("staff_cast_speed"), { { 82, 25, 26 }, { 81, 22, 25 }, { 78, 19, 22 }, { 58, 16, 19 }, { 39, 13, 16 }, { 20, 10, 13 }, { 1, 7, 10 } });
	ExpectTiers(Family("staff_spell_skill_levels"), { { 82, 3, 3 }, { 72, 2, 2 }, { 32, 2, 2 }, { 20, 1, 1 } });
	for (const std::string_view element : { "fire", "cold", "lightning", "physical" })
		ExpectTiers(Family(std::string("staff_") + std::string(element) + "_skill_levels"), { { 82, 4, 4 }, { 72, 3, 3 }, { 47, 2, 2 }, { 23, 1, 1 }, { 20, 1, 1 } });
	ExpectTiers(Family("staff_maximum_mana"), { { 82, 150, 164 }, { 1, 10, 14 } }, true);
	EXPECT_EQ(Family("staff_maximum_mana").tiers[9].itemLevel, 8);
	ExpectTiers(Family("wand_spell_damage"), { { 82, 53, 60 }, { 1, 13, 17 } }, true);
	EXPECT_EQ(Family("wand_spell_damage").tiers[6].itemLevel, 10);
	ExpectTiers(Family("wand_cast_speed"), { { 82, 17, 18 }, { 1, 5, 6 } }, true);
	ExpectTiers(Family("wand_spell_skill_levels"), { { 82, 2, 2 }, { 72, 2, 2 }, { 32, 1, 1 }, { 20, 1, 1 } });
	ExpectTiers(Family("sceptre_spirit_percent"), { { 82, 61, 65 }, { 81, 56, 60 }, { 78, 51, 55 }, { 60, 45, 50 }, { 43, 39, 44 }, { 21, 33, 38 }, { 10, 27, 32 }, { 1, 20, 26 } });
	ExpectTiers(Family("sceptre_spirit_mana"), { { 82, 35, 38, 42, 45 }, { 3, 10, 14, 17, 20 } }, true);
	ExpectTiers(Family("sceptre_minion_skill_levels"), { { 82, 2, 2 }, { 72, 2, 2 }, { 32, 1, 1 }, { 20, 1, 1 } });

	// Half size on the offensive staff and wand families and every +level family; the rest, sceptre Spirit included, full.
	EXPECT_EQ(Family("staff_spell_damage_mana").scale[1], 100);
	EXPECT_EQ(Family("staff_intelligence").scale[0], 100);
	EXPECT_EQ(Family("wand_maximum_mana").scale[0], 50);
	EXPECT_EQ(Family("sceptre_maximum_mana").scale[0], 100);
	EXPECT_EQ(Family("sceptre_spirit_percent").scale[0], 100);
	EXPECT_EQ(Family("sceptre_minion_skill_levels").scale[0], 50);
	// A sceptre's Spirit tops out at 203.
	EXPECT_EQ(100 + Family("sceptre_spirit_percent").tiers[0].max[0] + Family("sceptre_spirit_mana").tiers[0].max[0], 203);
}

TEST_F(Poe2CasterAffixesTest, ThePlansDataChanges)
{
	// One damage group for a staff's and for a wand's spell, hybrid and element damage.
	for (const std::string_view kind : { "staff", "wand" }) {
		const std::string group = std::string(kind) + "_spell_damage";
		for (const std::string_view suffix : { "_spell_damage", "_spell_damage_mana", "_fire_spell_damage", "_cold_spell_damage", "_lightning_spell_damage", "_physical_spell_damage" })
			EXPECT_EQ(Family(std::string(kind) + std::string(suffix)).group, group) << kind << suffix;
		EXPECT_EQ(Family(std::string(kind) + "_light_mana_regeneration").text, "{0}% increased Mana Regeneration Rate; {1}% increased Light Radius");
		EXPECT_EQ(Family(std::string(kind) + "_ignite_magnitude").name, "Flammability Magnitude");
	}
	for (const std::string_view element : { "physical", "fire", "cold", "lightning" }) {
		const ModFamily &family = Family(std::string("sceptre_ally_added_") + std::string(element));
		EXPECT_EQ(family.effect, ModEffect::MinionAddedDamage) << element;
		EXPECT_TRUE(family.enabled) << element;
	}
	EXPECT_EQ(Family("sceptre_ally_life_regeneration").effect, ModEffect::MinionLifeRegen);
	EXPECT_TRUE(Family("sceptre_ally_life_regeneration").enabled);
	// Still off (5.4): the ally speeds and crits, gains as extra, spell crit, light radius, Presence and the ailments.
	for (const std::string_view id : { "sceptre_ally_attack_speed", "sceptre_ally_cast_speed", "sceptre_ally_crit_chance", "sceptre_ally_crit_damage", "staff_gain_as_fire",
	         "wand_spell_crit_chance", "staff_light_mana_regeneration", "sceptre_presence_radius", "staff_freeze_buildup", "wand_shock_chance" })
		EXPECT_FALSE(Family(id).enabled) << id;
	// The element families carry their element's tag.
	EXPECT_EQ(Family("staff_cold_spell_damage").baseTags, ModBaseColdSpell);
	EXPECT_EQ(Family("wand_fire_skill_levels").baseTags, ModBaseFireSpell);
	EXPECT_EQ(Family("staff_physical_spell_damage").baseTags, ModBasePhysicalSpell);
	EXPECT_EQ(Family("staff_spell_damage").baseTags, 0);
}

uint16_t ElementsOf(const std::vector<Poe2ItemMod> &mods)
{
	uint16_t elements = 0;
	for (const Poe2ItemMod &mod : mods) {
		if (const ModFamily *family = ItemModFamily(mod); family != nullptr)
			elements |= family->baseTags & ModBaseElementSpellBits;
	}
	return elements;
}

TEST_F(Poe2CasterAffixesTest, OneDamagePrefixAndOneElement)
{
	int elementItems = 0;
	for (uint32_t seed = 1; seed <= 2000; seed++) {
		for (const ModSlot slot : { ModSlot::Staff, ModSlot::Wand }) {
			// A staff without an element lock (Spriggan) can roll any element, but only one.
			const std::vector<Poe2ItemMod> mods = RollItemMods(slot, ModBaseElementSpellBits, 82, 6, MaxRareModsPerSide, seed, seed);
			const int damage = static_cast<int>(std::count_if(mods.begin(), mods.end(), [](const Poe2ItemMod &mod) { return ItemModFamily(mod)->group.ends_with("_spell_damage"); }));
			EXPECT_LE(damage, 1) << seed;
			const uint16_t elements = ElementsOf(mods);
			EXPECT_LE(std::popcount(elements), 1) << seed;
			if (elements != 0)
				elementItems++;
			// A cold-locked staff (Gelid) never rolls another element.
			const std::vector<Poe2ItemMod> gelid = RollItemMods(slot, ModBaseColdSpell, 82, 6, MaxRareModsPerSide, seed, seed);
			EXPECT_EQ(ElementsOf(gelid) & ~ModBaseColdSpell, 0) << seed;
		}
	}
	EXPECT_GT(elementItems, 500) << "Element modifiers do roll";

	// Crafting keeps the lock: an Exalted Orb's added modifier never brings a second element.
	for (uint32_t seed = 1; seed <= 500; seed++) {
		std::vector<Poe2ItemMod> mods = RollItemMods(ModSlot::Staff, ModBaseElementSpellBits, 82, 2, MaxRareModsPerSide, seed, seed);
		for (uint32_t extra = 0; extra < 4; extra++) {
			const std::optional<Poe2ItemMod> added = RollAddedMod(ModSlot::Staff, ModBaseElementSpellBits, 82, mods, MaxRareModsPerSide, seed * 31 + extra, ModRollFilter {});
			if (!added)
				break;
			mods.push_back(*added);
		}
		EXPECT_LE(std::popcount(ElementsOf(mods)), 1) << seed;
	}
}

TEST_F(Poe2CasterAffixesTest, LegacyAndOtherPoolsDontChange)
{
	// The old staves' and sceptres' pools have no new family, and the new pools none of the old.
	for (uint32_t seed = 1; seed <= 300; seed++) {
		for (const ModSlot slot : { ModSlot::LegacyStaff, ModSlot::LegacySceptre, ModSlot::BodyArmour, ModSlot::Ring }) {
			for (const Poe2ItemMod &mod : RollItemMods(slot, 0, 82, 6, MaxRareModsPerSide, seed, seed))
				EXPECT_LE(mod.family, LegacyModFamilyCount) << seed;
		}
		for (const ModSlot slot : { ModSlot::Staff, ModSlot::Wand, ModSlot::Sceptre }) {
			for (const Poe2ItemMod &mod : RollItemMods(slot, ModBaseElementSpellBits, 82, 6, MaxRareModsPerSide, seed, seed))
				EXPECT_GT(mod.family, LegacyModFamilyCount) << seed;
		}
	}
}

std::string FamilyIdFor(std::string_view id, ModSlot slot)
{
	const std::optional<uint8_t> family = FindFamilyForItem(id, slot);
	if (!family)
		return "(none)";
	const ModFamily &found = ModifierFamilies()[*family];
	// The named family the slot can't take: the essence or Profane modifier can't go on such an item.
	if ((found.slots & (1U << static_cast<uint32_t>(slot))) == 0)
		return "(not this slot)";
	return found.id;
}

TEST_F(Poe2CasterAffixesTest, EssencesAndProfaneModifiersFindTheNewSlotsFamilies)
{
	// The essences' families, then the Profane modifiers' (desecration.cpp ProfaneMods).
	struct Case {
		std::string_view id;
		std::string_view staff;
		std::string_view wand;
		std::string_view sceptre;
	};
	const Case cases[] = {
		{ "maximum_life", "(not this slot)", "(not this slot)", "(not this slot)" },
		{ "maximum_mana", "staff_maximum_mana", "wand_maximum_mana", "sceptre_maximum_mana" },
		{ "fire_resistance", "(not this slot)", "(not this slot)", "(not this slot)" },
		{ "cold_resistance", "(not this slot)", "(not this slot)", "(not this slot)" },
		{ "lightning_resistance", "(not this slot)", "(not this slot)", "(not this slot)" },
		{ "local_increased_physical", "(not this slot)", "(not this slot)", "(not this slot)" },
		{ "attack_speed", "(not this slot)", "(not this slot)", "(not this slot)" },
		{ "cast_speed", "staff_cast_speed", "wand_cast_speed", "(not this slot)" },
		{ "accuracy_rating", "(not this slot)", "(not this slot)", "(not this slot)" },
		{ "all_elemental_resistance", "(not this slot)", "(not this slot)", "(not this slot)" },
		// A new sceptre has its own % Spirit instead (plan 6.4).
		{ "spirit_flat", "(not this slot)", "(not this slot)", "(not this slot)" },
		{ "minion_damage", "(not this slot)", "(not this slot)", "sceptre_ally_damage" },
	};
	for (const Case &c : cases) {
		EXPECT_EQ(FamilyIdFor(c.id, ModSlot::Staff), c.staff) << c.id;
		EXPECT_EQ(FamilyIdFor(c.id, ModSlot::Wand), c.wand) << c.id;
		EXPECT_EQ(FamilyIdFor(c.id, ModSlot::Sceptre), c.sceptre) << c.id;
	}
	EXPECT_EQ(FamilyIdFor("movement_speed", ModSlot::Wand), "(not this slot)");
	EXPECT_EQ(FindFamilyForItem("no_such_family", ModSlot::Wand), std::nullopt);

	// Every essence names a family, and every item from before the rework gets exactly that family.
	std::vector<std::string_view> ids { "movement_speed" };
	for (int kind = 1; kind < static_cast<int>(EssenceKind::Count); kind++)
		ids.push_back(GetEssence(static_cast<EssenceKind>(kind)).family);
	for (const std::string_view id : ids) {
		for (int slot = 0; slot < static_cast<int>(ModSlot::Count); slot++) {
			if (IsAnyOf(static_cast<ModSlot>(slot), ModSlot::Staff, ModSlot::Wand, ModSlot::Sceptre))
				continue;
			EXPECT_EQ(FindFamilyForItem(id, static_cast<ModSlot>(slot)), FindModifierFamily(id)) << id << " slot " << slot;
		}
	}
}

} // namespace
} // namespace devilution
