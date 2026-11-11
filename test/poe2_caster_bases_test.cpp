/**
 * @file poe2_caster_bases_test.cpp
 *
 * The caster weapons rework's bases (caster-weapons-plan-v2.md section 3, step S9): POE2's sceptres, staves, wands and
 * the quarterstaves the bundle lacked, appended to the item table after every older row, and the Focused and Quick staves
 * and sceptres retired. The new staves, wands and sceptres hit for nothing.
 */
#include <array>
#include <bit>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "control/control.hpp"
#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "inv.h"
#include "items.h"
#include "options.h"
#include "panels/gemcutting_panel.hpp"
#include "player.h"
#include "poe2/bases.hpp"
#include "poe2/craft_state.hpp"
#include "poe2/crafting_items.hpp"
#include "poe2/currency.hpp"
#include "poe2/default_attack.hpp"
#include "poe2/desecration.hpp"
#include "poe2/gemcutting.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/spirit.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/weapon_kinds.hpp"
#include "poe2/weapon_skills.hpp"
#include "qol/visual_store.h"
#include "spells.h"
#include "tables/itemdat.h"
#include "tables/playerdat.hpp"
#include "utils/format.hpp"
#include "utils/is_of.hpp"

namespace devilution {
namespace {

/** The item rows from before the rework. */
constexpr size_t FirstNewRow = 951;
constexpr size_t NewRows = 54;

class Poe2CasterBasesTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		*MyPlayer = {};
		MyPlayer->poe2.clear();
		MyPlayer->_pClass = HeroClass::Sorcerer;
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

	static _item_indexes Row(std::string_view name)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == name)
				return static_cast<_item_indexes>(i);
		}
		ADD_FAILURE() << name;
		return IDI_GOLD;
	}

	static const BaseMeta &Meta(std::string_view name)
	{
		const BaseMeta *base = BaseMetaFor(Row(name));
		EXPECT_NE(base, nullptr) << name;
		static const BaseMeta None {};
		return base != nullptr ? *base : None;
	}

	/** An item of the base as a drop makes it (the S0 golden file's way). */
	static Item Make(_item_indexes base, uint32_t seed, item_quality rarity, int itemLevel)
	{
		Item item = {};
		SetLootRarity(item, rarity);
		SetPoe2ItemLevel(item, itemLevel);
		SetRndSeed(seed);
		SetupAllItems(*MyPlayer, item, base, AdvanceRndSeed(), 30, 1, /*onlygood=*/true, /*pregen=*/false, 0, /*forceNotUnique=*/true);
		item._iIdentified = true;
		return item;
	}

	static void Equip(Player &player, inv_body_loc hand, _item_indexes base)
	{
		Item &item = player.InvBody[hand];
		item = {};
		InitializeItem(item, base);
		item._iStatFlag = true;
	}
};

TEST_F(Poe2CasterBasesTest, NewRowsComeAfterTheOldOnesInThePlansOrder)
{
	// Later rows (shards, 2026-10-29) come after these.
	ASSERT_GE(AllItemsList.size(), FirstNewRow + NewRows);
	const auto kindsFrom = [](size_t first, size_t count, WeaponKind kind) {
		for (size_t i = first; i < first + count; i++)
			EXPECT_EQ(WeaponKindOf(static_cast<int>(i)), kind) << AllItemsList[i].iName;
	};
	// 13 sceptres, 14 staves, 13 wands, 14 quarterstaves.
	kindsFrom(FirstNewRow, 13, WeaponKind::Sceptre);
	kindsFrom(FirstNewRow + 13, 14, WeaponKind::CasterStaff);
	kindsFrom(FirstNewRow + 27, 13, WeaponKind::Wand);
	kindsFrom(FirstNewRow + 40, 14, WeaponKind::Quarterstaff);
	for (size_t i = FirstNewRow; i < FirstNewRow + NewRows; i++) {
		const ItemData &data = AllItemsList[i];
		const BaseMeta *base = BaseMetaFor(static_cast<int>(i));
		ASSERT_NE(base, nullptr) << data.iName;
		EXPECT_EQ(base->identity, "poe2") << data.iName;
		EXPECT_GT(data.dropRate, 0) << data.iName;
		EXPECT_EQ(data.iClass, ICLASS_WEAPON) << data.iName;
		EXPECT_EQ(base->level, data.iMinMLvl) << data.iName;
		if (IsCasterWeapon(static_cast<int>(i))) {
			EXPECT_EQ(data.iMinDam, 0) << data.iName;
			EXPECT_EQ(data.iMaxDam, 0) << data.iName;
			EXPECT_EQ(base->quality, QualityKind::None) << data.iName;
		} else {
			EXPECT_GT(data.iMaxDam, data.iMinDam) << data.iName;
			EXPECT_EQ(base->quality, QualityKind::Weapon) << data.iName;
		}
	}
	EXPECT_EQ(AllItemsList[Row("Withered Wand")].itype, ItemType::Mace);
	EXPECT_EQ(AllItemsList[Row("Withered Wand")].iLoc, ILOC_ONEHAND);
	EXPECT_EQ(AllItemsList[Row("Gelid Staff")].iLoc, ILOC_TWOHAND);
	EXPECT_EQ(AllItemsList[Row("Lupine Sceptre")].iSName, "Sceptre");
	// Left out of v1 (plan 3.4).
	for (const std::string_view id : { "crackling", "smooth", "arcing", "bolting" })
		EXPECT_EQ(FindPoe2ItemId("poe2:quarterstaff_" + std::string(id) + "_quarterstaff"), std::nullopt) << id;
	// The Focused and Quick staves and sceptres stay for saved items but no longer drop.
	for (int row = 544; row <= 575; row++) {
		EXPECT_EQ(AllItemsList[row].dropRate, 0) << AllItemsList[row].iName;
		EXPECT_TRUE(IsLegacyCaster(Make(static_cast<_item_indexes>(row), 1, ITEM_QUALITY_NORMAL, 1))) << AllItemsList[row].iName;
	}
}

TEST_F(Poe2CasterBasesTest, EveryNewNameIsItsOwn)
{
	// Bases are found by name (LoadBaseData, LoadPoe2Uniques): no new name may be any other row's.
	for (size_t i = FirstNewRow; i < FirstNewRow + NewRows; i++) {
		for (size_t j = 0; j < AllItemsList.size(); j++) {
			if (j != i)
				EXPECT_NE(AllItemsList[j].iName, AllItemsList[i].iName) << i << " " << j;
		}
	}
	// The clashing POE2 names take "Fine " (plan 3.5).
	for (const std::string_view name : { "Fine Rattling Sceptre", "Fine Stoic Sceptre", "Fine Omen Sceptre", "Fine Long Quarterstaff", "Fine Aegis Quarterstaff" })
		EXPECT_GE(static_cast<size_t>(Row(name)), FirstNewRow) << name;
}

TEST_F(Poe2CasterBasesTest, BaseColumns)
{
	// Grants: every sceptre's Spirit skill; Staff Bolt on the Ashen and Spriggan staves. The other staves and wands grant
	// nothing until their skills are built (steps S14 to S18).
	EXPECT_EQ(Meta("Fine Rattling Sceptre").grantedSkill, SkillSlot::ForPersistent(PersistentSkill::SkeletalWarriors));
	EXPECT_EQ(Meta("Fine Stoic Sceptre").grantedSkill, SkillSlot::ForPersistent(PersistentSkill::ManaShield));
	EXPECT_EQ(Meta("Fine Omen Sceptre").grantedSkill, SkillSlot::ForPersistent(PersistentSkill::OverwhelmingPresence));
	EXPECT_EQ(Meta("Shrine Sceptre").grantedSkill, SkillSlot::ForPersistent(PersistentSkill::PurityOfElements));
	EXPECT_EQ(Meta("Clasped Sceptre").grantedSkill, SkillSlot::ForPersistent(PersistentSkill::HeraldOfFrost));
	EXPECT_EQ(Meta("Wrath Sceptre").grantedSkill, SkillSlot::ForPersistent(PersistentSkill::HeraldOfThunder));
	EXPECT_EQ(Meta("Hallowed Sceptre").grantedSkill, SkillSlot::ForPersistent(PersistentSkill::SkeletalWarriors));
	const SkillSlot staffBolt = SkillSlot::ForDefault(static_cast<uint8_t>(DefaultAttack::StaffBolt));
	EXPECT_EQ(Meta("Ashen Staff").grantedSkill, staffBolt);
	EXPECT_EQ(Meta("Spriggan Staff").grantedSkill, staffBolt);
	for (size_t i = FirstNewRow; i < FirstNewRow + NewRows; i++) {
		const BaseMeta &base = *BaseMetaFor(static_cast<int>(i));
		const WeaponKind kind = WeaponKindOf(static_cast<int>(i));
		if (kind == WeaponKind::Sceptre)
			EXPECT_EQ(base.grantedSkill.kind, SkillKind::Persistent) << base.name;
		else if (kind == WeaponKind::Quarterstaff)
			EXPECT_EQ(base.grantedSkill.kind, SkillKind::None) << base.name;
		// Every caster weapon that grants is one GrantingBase finds.
		const Item item = Make(static_cast<_item_indexes>(i), 1, ITEM_QUALITY_NORMAL, 1);
		EXPECT_EQ(GrantingBase(item) != nullptr, base.grantedSkill.kind != SkillKind::None) << base.name;
		// Sceptres and quarterstaves lock no element.
		if (kind == WeaponKind::Sceptre || kind == WeaponKind::Quarterstaff)
			EXPECT_EQ(base.elementLock, 0) << base.name;
	}

	EXPECT_EQ(Meta("Ashen Staff").elementLock, ModBaseFireSpell);
	EXPECT_EQ(Meta("Gelid Staff").elementLock, ModBaseColdSpell);
	EXPECT_EQ(Meta("Voltaic Staff").elementLock, ModBaseLightningSpell);
	EXPECT_EQ(Meta("Rending Staff").elementLock, ModBasePhysicalSpell);
	EXPECT_EQ(Meta("Spriggan Staff").elementLock, 0);
	EXPECT_EQ(Meta("Volatile Wand").elementLock, ModBaseFireSpell);
	EXPECT_EQ(Meta("Frigid Wand").elementLock, ModBaseColdSpell);
	EXPECT_EQ(Meta("Attuned Wand").elementLock, 0);

	// Crit only on the new quarterstaves (POE2's, in tenths of a percent); the nine older ones keep the default.
	EXPECT_EQ(Meta("Fine Long Quarterstaff").crit, 100);
	EXPECT_EQ(Meta("Sinister Quarterstaff").crit, 120);
	for (const std::string_view name : { "Fine Wrapped Quarterstaff", "Fine Gothic Quarterstaff", "Crescent Quarterstaff", "Steelpoint Quarterstaff", "Slicing Quarterstaff",
	         "Barbarous Quarterstaff", "Bladed Quarterstaff", "Lunar Quarterstaff", "Dreaming Quarterstaff" })
		EXPECT_EQ(Meta(name).crit, -1) << name;
	EXPECT_EQ(Meta("Waxing Quarterstaff").aps, 150);
	EXPECT_EQ(Meta("Hefty Quarterstaff").aps, 130);

	// Implicits: block on the Barrier, Guardian and Aegis staves, Daze on the Hefty and Skullcrusher.
	ASSERT_EQ(Meta("Barrier Quarterstaff").implicits.size(), 1U);
	EXPECT_EQ(Meta("Barrier Quarterstaff").implicits[0].effect, ModEffect::BlockChance);
	EXPECT_EQ(Meta("Barrier Quarterstaff").implicits[0].min, 12);
	EXPECT_EQ(Meta("Barrier Quarterstaff").implicits[0].max, 18);
	ASSERT_EQ(Meta("Skullcrusher Quarterstaff").implicits.size(), 1U);
	EXPECT_EQ(Meta("Skullcrusher Quarterstaff").implicits[0].effect, ModEffect::DazeChance);
	EXPECT_TRUE(Meta("Ashen Staff").implicits.empty());

	// Fine Long follows the early-weapon rule: POE2's 9-18 doubled (plan 3.4).
	EXPECT_EQ(AllItemsList[Row("Fine Long Quarterstaff")].iMinDam, 18);
	EXPECT_EQ(AllItemsList[Row("Fine Long Quarterstaff")].iMaxDam, 36);

	// Unique types: three staves carry Diablo's (plan 3.2); everything else none.
	EXPECT_EQ(AllItemsList[Row("Ashen Staff")].iItemId, UITYPE_SHORTSTAFF);
	EXPECT_EQ(AllItemsList[Row("Spriggan Staff")].iItemId, UITYPE_QUARSTAFF);
	EXPECT_EQ(AllItemsList[Row("Pyrophyte Staff")].iItemId, UITYPE_LONGSTAFF);
	for (size_t i = FirstNewRow; i < FirstNewRow + NewRows; i++) {
		const std::string_view name = AllItemsList[i].iName;
		if (name != "Ashen Staff" && name != "Spriggan Staff" && name != "Pyrophyte Staff")
			EXPECT_EQ(AllItemsList[i].iItemId, UITYPE_NONE) << AllItemsList[i].iName;
	}
}

TEST_F(Poe2CasterBasesTest, NewItemsRollAndRebuild)
{
	int rolled = 0;
	for (size_t i = FirstNewRow; i < FirstNewRow + NewRows; i++) {
		const auto row = static_cast<_item_indexes>(i);
		const BaseMeta &base = *BaseMetaFor(row);
		for (const int itemLevel : { 1, 45, 82 }) {
			for (const item_quality rarity : { ITEM_QUALITY_NORMAL, ITEM_QUALITY_MAGIC, ITEM_QUALITY_RARE }) {
				for (uint32_t seed = 1; seed <= 20; seed++) {
					const Item item = Make(row, seed * 7919U, rarity, itemLevel);
					ASSERT_EQ(item.IDidx, row);
					EXPECT_GT(item._ivalue, 0) << base.name;
					EXPECT_EQ(item._iMaxDur, AllItemsList[i].iDurability) << base.name;
					Item rebuilt;
					RecreateItem(*MyPlayer, rebuilt, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff, item._iPoe2Craft);
					rebuilt._iIdentified = true;
					EXPECT_EQ(std::string_view(rebuilt._iIName), std::string_view(item._iIName)) << base.name;
					EXPECT_EQ(rebuilt._iPoe2Stats, item._iPoe2Stats) << base.name;
					for (size_t m = 0; m < item._iPoe2Mods.size(); m++) {
						EXPECT_EQ(rebuilt._iPoe2Mods[m].family, item._iPoe2Mods[m].family) << base.name;
						EXPECT_EQ(rebuilt._iPoe2Mods[m].values, item._iPoe2Mods[m].values) << base.name;
					}
					std::vector<Poe2ItemMod> mods;
					for (const Poe2ItemMod &mod : item._iPoe2Mods) {
						if (mod.family == 0)
							continue;
						mods.push_back(mod);
						rolled++;
						// Every modifier is of the base's own pool.
						const ModFamily *family = ItemModFamily(mod);
						ASSERT_NE(family, nullptr);
						EXPECT_NE(family->slots & (1U << static_cast<uint32_t>(*ItemModSlot(item))), 0U) << base.name << " " << family->id;
						// A locked base rolls only its own element.
						if (base.elementLock != 0)
							EXPECT_EQ(family->baseTags & ModBaseElementSpellBits & ~base.elementLock, 0) << base.name << " " << family->id;
					}
					uint16_t elements = 0;
					for (const Poe2ItemMod &mod : mods)
						elements |= ItemModFamily(mod)->baseTags & ModBaseElementSpellBits;
					EXPECT_LE(std::popcount(elements), 1) << base.name;
				}
			}
		}
	}
	EXPECT_GT(rolled, 5000);
	// The slots: the rework's own for its staves, wands and sceptres; the quarterstaves keep theirs.
	EXPECT_EQ(ItemModSlot(Make(Row("Gelid Staff"), 1, ITEM_QUALITY_NORMAL, 1)), ModSlot::Staff);
	EXPECT_EQ(ItemModSlot(Make(Row("Bone Wand"), 1, ITEM_QUALITY_NORMAL, 1)), ModSlot::Wand);
	EXPECT_EQ(ItemModSlot(Make(Row("Ochre Sceptre"), 1, ITEM_QUALITY_NORMAL, 1)), ModSlot::Sceptre);
	EXPECT_EQ(ItemModSlot(Make(Row("Wyrm Quarterstaff"), 1, ITEM_QUALITY_NORMAL, 1)), ItemModSlot(Make(Row("Lunar Quarterstaff"), 1, ITEM_QUALITY_NORMAL, 1)));
}

TEST_F(Poe2CasterBasesTest, CasterWeaponsLockTheirModifiersIn)
{
	// Plan 5.4: a new caster weapon saves its rolled modifiers written out (salt 0), so a family switched on in its pool
	// later can't re-roll it. Loading writes them in as well.
	int locked = 0;
	for (const std::string_view name : { "Gelid Staff", "Spriggan Staff", "Bone Wand", "Withered Wand", "Ochre Sceptre", "Lupine Sceptre" }) {
		for (const item_quality rarity : { ITEM_QUALITY_MAGIC, ITEM_QUALITY_RARE }) {
			for (uint32_t seed = 1; seed <= 15; seed++) {
				const Item item = Make(Row(name), seed * 7919U, rarity, 60);
				ASSERT_FALSE(item._iPoe2Craft.explicitMods) << name;
				if (item._iPoe2Mods[0].family == 0)
					continue;
				ASSERT_TRUE(LocksCasterWeaponMods(item)) << name;
				const Poe2Craft saved = SavedCraftState(item);
				EXPECT_TRUE(saved.explicitMods);
				EXPECT_EQ(saved.rarity, item._iMagical);
				Item loaded = item;
				LockCasterWeaponMods(loaded);
				EXPECT_EQ(loaded._iPoe2Craft, saved) << name;
				EXPECT_FALSE(LocksCasterWeaponMods(loaded)) << "Once is enough";
				EXPECT_EQ(SavedCraftState(loaded), saved);
				// The save bytes carry it (the wide layout), and the item rebuilds from them as it was.
				EXPECT_EQ(UnpackCraftState(PackCraftState(saved)), saved) << name;
				Item rebuilt;
				RecreateItem(*MyPlayer, rebuilt, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff, saved);
				rebuilt._iIdentified = true;
				EXPECT_EQ(std::string_view(rebuilt._iIName), std::string_view(item._iIName)) << name;
				EXPECT_EQ(rebuilt._iMagical, item._iMagical) << name;
				EXPECT_EQ(rebuilt._iPoe2Stats, item._iPoe2Stats) << name;
				EXPECT_EQ(rebuilt._iVAdd1, item._iVAdd1) << name;
				EXPECT_EQ(rebuilt._iMinMag, item._iMinMag) << name;
				for (size_t m = 0; m < item._iPoe2Mods.size(); m++) {
					EXPECT_EQ(rebuilt._iPoe2Mods[m].family, item._iPoe2Mods[m].family) << name;
					EXPECT_EQ(rebuilt._iPoe2Mods[m].tier, item._iPoe2Mods[m].tier) << name;
					EXPECT_EQ(rebuilt._iPoe2Mods[m].values, item._iPoe2Mods[m].values) << name;
				}
				// The families and tiers now come from the craft state, not the pool.
				const std::vector<Poe2ItemMod> written = CraftModList(saved);
				size_t count = 0;
				for (const Poe2ItemMod &mod : item._iPoe2Mods) {
					if (mod.family == 0)
						continue;
					ASSERT_LT(count, written.size());
					EXPECT_TRUE(std::any_of(written.begin(), written.end(), [&mod](const Poe2ItemMod &w) { return w.family == mod.family && w.tier == mod.tier; })) << name;
					count++;
				}
				EXPECT_EQ(count, written.size()) << name;
				locked++;
			}
		}
	}
	EXPECT_GT(locked, 100);

	// Nothing else locks: normal items, quarterstaves, the retired staves and sceptres, uniques, crafted items, and
	// nothing outside POE2.
	const Item normal = Make(Row("Gelid Staff"), 11, ITEM_QUALITY_NORMAL, 60);
	EXPECT_FALSE(LocksCasterWeaponMods(normal));
	EXPECT_EQ(SavedCraftState(normal), normal._iPoe2Craft);
	for (const std::string_view name : { "Wyrm Quarterstaff", "Focused Ashen Staff", "Quick Bronze Sceptre" }) {
		const Item other = Make(Row(name), 11, ITEM_QUALITY_RARE, 60);
		ASSERT_NE(other._iPoe2Mods[0].family, 0) << name;
		EXPECT_FALSE(LocksCasterWeaponMods(other)) << name;
		EXPECT_EQ(SavedCraftState(other), other._iPoe2Craft) << name;
	}
	Item unique = Make(Row("Bone Wand"), 11, ITEM_QUALITY_RARE, 60);
	unique._iMagical = ITEM_QUALITY_UNIQUE;
	EXPECT_FALSE(LocksCasterWeaponMods(unique));
	Item crafted = Make(Row("Bone Wand"), 11, ITEM_QUALITY_RARE, 60);
	crafted._iPoe2Craft = CurrentCraft(crafted);
	crafted._iPoe2Craft.mods[0].salt = 3;
	EXPECT_EQ(SavedCraftState(crafted), crafted._iPoe2Craft);
	Item outside = Make(Row("Bone Wand"), 11, ITEM_QUALITY_RARE, 60);
	gbIsPoe2 = false;
	EXPECT_FALSE(LocksCasterWeaponMods(outside));
	LockCasterWeaponMods(outside);
	EXPECT_FALSE(outside._iPoe2Craft.explicitMods);
	gbIsPoe2 = true;
}

TEST_F(Poe2CasterBasesTest, ChaosWandsAndStavesRollChaosModifiersAndNoOtherBaseDoes)
{
	// Chaos damage plan step 13: five new bases, locked to chaos, grant the chaos skills.
	struct Chaos {
		std::string_view name;
		WeaponSkill grant;
	};
	for (const Chaos &c : { Chaos { "Blighted Wand", WeaponSkill::WitheringBolt }, Chaos { "Rotting Wand", WeaponSkill::Decompose }, Chaos { "Gloom Wand", WeaponSkill::WitheringRay },
	         Chaos { "Hollow Staff", WeaponSkill::Soulrend }, Chaos { "Void Staff", WeaponSkill::DarkPact } }) {
		EXPECT_EQ(Meta(c.name).elementLock, ModBaseChaosSpell) << c.name;
		EXPECT_EQ(Meta(c.name).grantedSkill, SkillSlot::ForAttack(c.grant)) << c.name;
		EXPECT_GT(static_cast<size_t>(Row(c.name)), FirstNewRow + NewRows) << c.name;
		EXPECT_EQ(ItemModBaseTags(Make(Row(c.name), 1, ITEM_QUALITY_NORMAL, 1)), ModBaseChaosSpell) << c.name;
	}
	// Only they roll the chaos modifiers: a base without a lock rolls any element but chaos.
	EXPECT_EQ(ItemModBaseTags(Make(Row("Spriggan Staff"), 1, ITEM_QUALITY_NORMAL, 1)), ModBaseUnlockedElementSpellBits);
	EXPECT_EQ(ItemModBaseTags(Make(Row("Siphoning Wand"), 1, ITEM_QUALITY_NORMAL, 1)), ModBaseUnlockedElementSpellBits);
	const auto isChaos = [](const Poe2ItemMod &mod) {
		const ModFamily *family = ItemModFamily(mod);
		return family != nullptr && IsAnyOf(family->effect, ModEffect::ChaosSpellDamage, ModEffect::ChaosSkillLevels);
	};
	int chaosRolled = 0;
	for (uint32_t seed = 1; seed <= 200; seed++) {
		for (const std::string_view name : { "Void Staff", "Gloom Wand" }) {
			const Item item = Make(Row(name), seed * 7919U, ITEM_QUALITY_RARE, 82);
			for (const Poe2ItemMod &mod : item._iPoe2Mods) {
				if (mod.family == 0)
					continue;
				EXPECT_EQ(ItemModFamily(mod)->baseTags & ModBaseElementSpellBits & ~ModBaseChaosSpell, 0) << name;
				if (isChaos(mod))
					chaosRolled++;
			}
		}
		for (const std::string_view name : { "Spriggan Staff", "Siphoning Wand", "Dueling Wand", "Gelid Staff" }) {
			const Item item = Make(Row(name), seed * 7919U, ITEM_QUALITY_RARE, 82);
			for (const Poe2ItemMod &mod : item._iPoe2Mods) {
				if (mod.family != 0)
					EXPECT_FALSE(isChaos(mod)) << name << " " << seed;
			}
		}
	}
	EXPECT_GT(chaosRolled, 20);
}

TEST_F(Poe2CasterBasesTest, WeaponsWithoutDamage)
{
	Player &player = *MyPlayer;
	player._pBaseMag = player._pMagic = 200;
	player._pBaseStr = player._pStrength = 50;
	player._pBaseDex = player._pDexterity = 50;
	player._pBaseVit = player._pVitality = 50;
	player.setCharacterLevel(30);

	// A wand: nothing to hit with, so the hero's empty-hand damage, as Diablo gives it.
	Equip(player, INVLOC_HAND_LEFT, Row("Attuned Wand"));
	CalcPlrInv(player, false);
	EXPECT_EQ(player._pIMinDam, 1);
	EXPECT_EQ(player._pIMaxDam, 1);
	EXPECT_EQ(AttackWeaponDamage(player, SkillSlot {}), std::make_pair(1, 1));
	// It wears and is repaired like any weapon.
	Item &wand = player.InvBody[INVLOC_HAND_LEFT];
	EXPECT_EQ(wand._iMaxDur, 35);
	EXPECT_EQ(wand._iDurability, 35);
	EXPECT_EQ(GetRepairCost(wand), 0);
	wand._iDurability = 20;
	EXPECT_GT(GetRepairCost(wand), 0);

	// A staff with both hands, and a wand beside a sceptre.
	player.InvBody[INVLOC_HAND_LEFT] = {};
	Equip(player, INVLOC_HAND_LEFT, Row("Gelid Staff"));
	CalcPlrInv(player, false);
	EXPECT_EQ(player._pIMaxDam, 1);
	Equip(player, INVLOC_HAND_LEFT, Row("Bone Wand"));
	Equip(player, INVLOC_HAND_RIGHT, Row("Lupine Sceptre"));
	CalcPlrInv(player, false);
	EXPECT_EQ(player._pIMaxDam, 1);
	// The sceptre's Spirit.
	const int withSceptre = MaxSpirit(player);
	player.InvBody[INVLOC_HAND_RIGHT] = {};
	CalcPlrInv(player, false);
	EXPECT_EQ(withSceptre - MaxSpirit(player), SceptreSpirit);
}

TEST_F(Poe2CasterBasesTest, AMonkWithAStaffOrWandHasNoFists)
{
	Player &player = *MyPlayer;
	player._pClass = HeroClass::Monk;
	EXPECT_TRUE(FightsWithBareFists(player));
	// A new staff or wand casts; a sceptre leaves his fists free.
	for (const std::string_view name : { "Gelid Staff", "Attuned Wand" }) {
		Equip(player, INVLOC_HAND_LEFT, Row(name));
		EXPECT_FALSE(FightsWithBareFists(player)) << name;
		player.InvBody[INVLOC_HAND_LEFT]._iStatFlag = false;
		EXPECT_TRUE(FightsWithBareFists(player)) << name << " he can't use";
		player.InvBody[INVLOC_HAND_LEFT] = {};
	}
	Equip(player, INVLOC_HAND_LEFT, Row("Fine Rattling Sceptre"));
	EXPECT_TRUE(FightsWithBareFists(player));
	Equip(player, INVLOC_HAND_LEFT, Row("Fine Long Quarterstaff"));
	EXPECT_FALSE(FightsWithBareFists(player));
	// His fists still follow the bundle's quarterstaves: a level 1 gem hits like the Fine Wrapped Quarterstaff, not the
	// stronger Fine Long one.
	const ItemData &wrapped = AllItemsList[Row("Fine Wrapped Quarterstaff")];
	EXPECT_EQ(BareFistDamage(1).second, (wrapped.iMaxDam * BareFistStaffPercent + 50) / 100);
}

TEST_F(Poe2CasterBasesTest, EssencesAndProfaneModifiersOnRealNewWeapons)
{
	struct Case {
		std::string_view base;
		EssenceKind essence;
		std::string_view family;
	};
	const Case cases[] = {
		{ "Attuned Wand", EssenceKind::Clarity, "wand_maximum_mana" },
		{ "Attuned Wand", EssenceKind::Sorcery, "wand_cast_speed" },
		{ "Gelid Staff", EssenceKind::Clarity, "staff_maximum_mana" },
		{ "Gelid Staff", EssenceKind::Sorcery, "staff_cast_speed" },
		{ "Fine Rattling Sceptre", EssenceKind::Clarity, "sceptre_maximum_mana" },
		{ "Fine Rattling Sceptre", EssenceKind::Summoning, "sceptre_ally_damage" },
	};
	Player &player = *MyPlayer;
	for (const Case &c : cases) {
		const auto family = static_cast<uint8_t>(*FindModifierFamily(c.family) + 1);
		int applied = 0;
		for (uint32_t seed = 1; seed <= 20; seed++) {
			Item item = Make(Row(c.base), seed * 31U, ITEM_QUALITY_MAGIC, 60);
			// It fails only where the item already has that family, or its effect in another (a hybrid; currency never repeats one).
			bool had = false;
			for (const Poe2ItemMod &mod : item._iPoe2Mods)
				had = had || (mod.family != 0 && (ModEffectBits(ItemModFamily(mod)->effect) & ModEffectBits(ModifierFamilies()[family - 1].effect)).any());
			if (!ApplyCurrencyOrb(CurrencyOrb::Essence, player, item, EssenceVariant(c.essence, EssenceTier::Normal))) {
				EXPECT_TRUE(had) << c.base << " " << c.family << " seed " << seed;
				continue;
			}
			applied++;
			EXPECT_EQ(item._iMagical, ITEM_QUALITY_RARE) << c.base;
			bool found = false;
			for (const Poe2ItemMod &mod : item._iPoe2Mods)
				found = found || mod.family == family;
			EXPECT_TRUE(found) << c.base << " " << c.family << " seed " << seed;
		}
		EXPECT_GE(applied, 10) << c.base << " " << c.family;
	}

	// A Profane modifier offered on a desecrated new weapon is the rework's family of its slot.
	for (const std::string_view name : { "Attuned Wand", "Gelid Staff", "Fine Rattling Sceptre" }) {
		const ModSlot slot = *ItemModSlot(Make(Row(name), 1, ITEM_QUALITY_NORMAL, 1));
		std::set<std::string> offered;
		for (uint32_t seed = 1; seed <= 100; seed++) {
			Item item = Make(Row(name), seed * 6151U, ITEM_QUALITY_RARE, 80);
			if (!ApplyCurrencyOrb(CurrencyOrb::Desecrate, player, item))
				continue;
			for (const bool kinship : { false, true }) {
				for (const Poe2CraftMod &offer : DesecrationOffers(item, kinship)) {
					if (GetDesecratedMod(offer.family) != nullptr)
						continue;
					const ModFamily &family = ModifierFamilies()[offer.family - 1];
					EXPECT_NE(family.slots & (1U << static_cast<uint32_t>(slot)), 0U) << name << " " << family.id;
					offered.insert(family.id);
				}
			}
		}
		const std::string kind = slot == ModSlot::Wand ? "wand" : slot == ModSlot::Staff ? "staff" : "sceptre";
		EXPECT_TRUE(offered.contains(kind + "_maximum_mana")) << name;
		if (slot != ModSlot::Sceptre)
			EXPECT_TRUE(offered.contains(kind + "_cast_speed")) << name;
	}
}

TEST_F(Poe2CasterBasesTest, DropBucketsByKind)
{
	const auto bucket = [](std::string_view name) { return EquipmentBucketOf(Row(name)); };
	const auto kindBucket = [](KindBucket kind) { return FirstKindBucket + static_cast<size_t>(kind); };
	EXPECT_EQ(bucket("Attuned Wand"), kindBucket(KindBucket::Wand));
	EXPECT_EQ(bucket("Lupine Sceptre"), kindBucket(KindBucket::Sceptre));
	EXPECT_EQ(bucket("Gelid Staff"), kindBucket(KindBucket::CasterStaff));
	EXPECT_EQ(bucket("Fine Wrapped Quarterstaff"), kindBucket(KindBucket::Quarterstaff));
	EXPECT_EQ(bucket("Wyrm Quarterstaff"), kindBucket(KindBucket::Quarterstaff));
	EXPECT_EQ(bucket("Felled Greatclub"), static_cast<size_t>(ItemType::Mace));
	EXPECT_EQ(bucket("Quick Bone Sceptre"), static_cast<size_t>(ItemType::Mace));
	EXPECT_EQ(bucket("Focused Pilgrim Staff"), static_cast<size_t>(ItemType::Staff));
	EXPECT_EQ(bucket("Shortsword"), static_cast<size_t>(ItemType::Sword));
	EXPECT_EQ(EquipmentBucketWeight(static_cast<size_t>(ItemType::Mace)), 6);
	EXPECT_EQ(EquipmentBucketWeight(kindBucket(KindBucket::Quarterstaff)), 6);
	EXPECT_EQ(EquipmentBucketWeight(kindBucket(KindBucket::Wand)), 4);
	EXPECT_EQ(EquipmentBucketWeight(kindBucket(KindBucket::Sceptre)), 3);
	EXPECT_EQ(EquipmentBucketWeight(kindBucket(KindBucket::CasterStaff)), 4);

	// 10,000 drops at area level 40: each bucket's share is its weight's share, within 2 points.
	constexpr int Rolls = 10000;
	std::array<int, EquipmentBucketCount> counts {};
	SetRndSeed(12345);
	for (int i = 0; i < Rolls; i++) {
		const std::optional<_item_indexes> base = PickPoe2EquipmentBase(40, Poe2ItemLevel(40, 0));
		ASSERT_TRUE(base.has_value());
		counts[EquipmentBucketOf(*base)]++;
	}
	int total = 0;
	for (size_t b = 0; b < counts.size(); b++) {
		if (counts[b] > 0)
			total += EquipmentBucketWeight(b);
	}
	for (const KindBucket kind : { KindBucket::Wand, KindBucket::Sceptre, KindBucket::CasterStaff, KindBucket::Quarterstaff })
		EXPECT_GT(counts[kindBucket(kind)], 0) << static_cast<int>(kind);
	EXPECT_GT(counts[static_cast<size_t>(ItemType::Mace)], 0);
	// The retired Focused and Quick staves leave the Staff type nothing that drops.
	EXPECT_EQ(counts[static_cast<size_t>(ItemType::Staff)], 0);
	for (size_t b = 0; b < counts.size(); b++) {
		if (counts[b] == 0)
			continue;
		const double share = 100.0 * counts[b] / Rolls;
		const double want = 100.0 * EquipmentBucketWeight(b) / total;
		EXPECT_NEAR(share, want, 2.0) << "bucket " << b;
	}
}

TEST_F(Poe2CasterBasesTest, EquipMatrix)
{
	const auto item = [](std::string_view name) {
		Item made {};
		InitializeItem(made, Row(name));
		made._iStatFlag = true;
		return made;
	};
	const Item wand = item("Attuned Wand");
	const Item otherWand = item("Bone Wand");
	const Item sceptre = item("Lupine Sceptre");
	const Item oldSceptre = item("Quick Bone Sceptre");
	const Item shield = item("Hide Buckler");
	const Item focus = item("Wooden Focus");
	const Item sword = item("Shortsword");
	const Item mace = item("Construct Hammer");
	const Item dagger = item("Glass Shank");
	const Item staff = item("Gelid Staff");
	const Item quarterstaff = item("Fine Wrapped Quarterstaff");
	ASSERT_TRUE(IsWand(wand));
	ASSERT_EQ(mace._iLoc, ILOC_ONEHAND);

	Player &player = *MyPlayer;
	for (const HeroClass heroClass : { HeroClass::Warrior, HeroClass::Bard, HeroClass::Sorcerer }) {
		player._pClass = heroClass;
		const bool bard = heroClass == HeroClass::Bard;
		struct Pair {
			const Item *first;
			const Item *second;
			bool canHold;
		};
		const Pair pairs[] = {
			// A wand goes with a shield, a focus or a sceptre, never another wand or a martial weapon, even for the Bard.
			{ &wand, &shield, true },
			{ &wand, &focus, true },
			{ &wand, &sceptre, true },
			{ &wand, &oldSceptre, true },
			{ &wand, &otherWand, false },
			{ &wand, &sword, false },
			{ &wand, &mace, false },
			{ &wand, &dagger, false },
			{ &wand, &staff, false },
			// A sceptre goes with any one-hander.
			{ &sceptre, &sword, true },
			{ &sceptre, &oldSceptre, true },
			{ &sceptre, &sceptre, true },
			{ &sceptre, &focus, true },
			// As before: a weapon and a shield; two weapons only for the Bard; nothing beside a two-hander.
			{ &sword, &shield, true },
			{ &sword, &mace, bard },
			{ &sword, &sword, bard },
			{ &staff, &focus, false },
			{ &quarterstaff, &shield, false },
			{ &shield, &focus, false },
		};
		for (const Pair &pair : pairs) {
			EXPECT_EQ(HandsCanHold(player, *pair.first, *pair.second), pair.canHold) << static_cast<int>(heroClass) << " " << pair.first->_iIName << " + " << pair.second->_iIName;
			EXPECT_EQ(HandsCanHold(player, *pair.second, *pair.first), pair.canHold) << static_cast<int>(heroClass) << " " << pair.second->_iIName << " + " << pair.first->_iIName;
			// Equipping the second beside the first.
			player.InvBody[INVLOC_HAND_LEFT] = *pair.first;
			player.InvBody[INVLOC_HAND_RIGHT] = {};
			if (pair.second->_iLoc == ILOC_ONEHAND && pair.first->_iLoc == ILOC_ONEHAND)
				EXPECT_EQ(AutoEquip(player, *pair.second, /*persistItem=*/false), pair.canHold) << static_cast<int>(heroClass) << " " << pair.first->_iIName << " then " << pair.second->_iIName;
		}
	}
	player.InvBody[INVLOC_HAND_LEFT] = {};
}

/** @brief The item's tooltip lines, as PrintItemDetails (identified) or PrintItemDur (not) writes them. */
std::string Tooltip(const Item &item)
{
	HeadlessMode = false;
	FloatingInfoString = std::string_view {};
	FloatingInfoStyles.clear();
	if (item._iIdentified)
		PrintItemDetails(item);
	else
		PrintItemDur(item);
	std::string text(FloatingInfoString.str());
	FloatingInfoString = std::string_view {};
	FloatingInfoStyles.clear();
	return text;
}

bool Has(const std::string &text, std::string_view line)
{
	return text.find(line) != std::string::npos;
}

TEST_F(Poe2CasterBasesTest, TooltipsShowTheKindTheGrantAndNoDamage)
{
	const auto show = [](std::string_view name, bool identified = true) {
		Item item = Make(Row(name), 7, ITEM_QUALITY_NORMAL, 33);
		item._iIdentified = identified;
		return Tooltip(item);
	};
	const int level = GrantedGemLevel(33);
	struct Case {
		std::string_view base;
		std::string_view kind;
		SkillSlot grant;
	};
	for (const Case &c : {
	         Case { "Withered Wand", "One-Handed Wand", SkillSlot::ForAttack(WeaponSkill::WitheringBolt) },
	         Case { "Fine Rattling Sceptre", "One-Handed Sceptre", SkillSlot::ForPersistent(PersistentSkill::SkeletalWarriors) },
	         Case { "Gelid Staff", "Two-Handed Staff", SkillSlot::ForAttack(WeaponSkill::FreezingShards) },
	     }) {
		for (const bool identified : { true, false }) {
			const std::string text = show(c.base, identified);
			EXPECT_TRUE(Has(text, c.kind)) << c.base << "\n" << text;
			EXPECT_TRUE(Has(text, FormatRuntime("Grants Skill: Level {:d} {:s}", level, SkillName(c.grant)))) << c.base << "\n" << text;
			// No damage and no attack speed: it hits with its skill.
			EXPECT_FALSE(Has(text, "damage:")) << c.base << "\n" << text;
			EXPECT_FALSE(Has(text, "Attacks per Second")) << c.base << "\n" << text;
			EXPECT_TRUE(Has(text, "Dur: ")) << c.base << "\n" << text;
		}
	}
	// A sceptre's Spirit with its own increase: +100 on a normal one.
	EXPECT_TRUE(Has(show("Fine Rattling Sceptre"), "implicit: +100 to Spirit"));
	EXPECT_TRUE(Has(show("Ashen Staff"), FormatRuntime("Grants Skill: Level {:d} {:s}", level, SkillName(Meta("Ashen Staff").grantedSkill))));

	// A quarterstaff hits: its damage, its speed and its own crit.
	const std::string quarterstaff = show("Fine Long Quarterstaff");
	EXPECT_TRUE(Has(quarterstaff, "Two-Handed Quarterstaff")) << quarterstaff;
	EXPECT_TRUE(Has(quarterstaff, "damage:")) << quarterstaff;
	ASSERT_GE(Meta("Fine Long Quarterstaff").crit, 0);
	EXPECT_TRUE(Has(quarterstaff, FormatRuntime("Critical Hit Chance: {:d}%", BaseAttackCritPercent * Meta("Fine Long Quarterstaff").crit / 100))) << quarterstaff;
	EXPECT_FALSE(Has(quarterstaff, "Grants Skill")) << quarterstaff;

	// The older staves and sceptres, and maces, keep their text.
	const std::string staff = show("Long Staff");
	EXPECT_TRUE(Has(staff, "Two-Handed Staff")) << staff;
	EXPECT_TRUE(Has(staff, "damage:")) << staff;
	EXPECT_FALSE(Has(staff, "Critical Hit Chance")) << staff;
	const std::string club = show("Wooden Club");
	EXPECT_TRUE(Has(club, "One-Handed Mace")) << club;
	EXPECT_TRUE(Has(club, "damage:")) << club;
}

TEST_F(Poe2CasterBasesTest, OutsidePoe2AStaffAndAMacePrintAsBefore)
{
	gbIsPoe2 = false;
	UnloadModArchives();
	LoadModArchives({ { "hf" } });
	LoadItemData();
	LoadModifierData();
	GetOptions().Gameplay.floatingInfoBox.SetValue(true);
	for (const std::string_view name : { "Short Staff", "Mace" }) {
		Item item {};
		InitializeItem(item, Row(name));
		item._iIdentified = true;
		const std::string text = Tooltip(item);
		EXPECT_TRUE(Has(text, "damage: ")) << name << "\n" << text;
		EXPECT_FALSE(Has(text, "-Handed")) << name << "\n" << text;
		EXPECT_FALSE(Has(text, "Grants Skill")) << name << "\n" << text;
		EXPECT_FALSE(Has(text, "Critical Hit Chance")) << name << "\n" << text;
	}
	GetOptions().Gameplay.floatingInfoBox.SetValue(false);
}

TEST_F(Poe2CasterBasesTest, NewWandsAndSceptresHaveTheirOwnRareNames)
{
	const std::set<std::string_view> wandWords { "Barb", "Charm", "Goad", "Needle", "Quill", "Rod", "Song", "Spike", "Sting", "Thorn", "Twig", "Wand" };
	const std::set<std::string_view> sceptreWords { "Blow", "Breaker", "Call", "Chime", "Crack", "Crusher", "Gavel", "Hymn", "Knell", "Mace", "Ram", "Roar" };
	const auto secondWord = [](const Item &item) {
		const std::string name = GetRareItemName(item);
		return name.substr(name.find(' ') + 1);
	};
	for (uint32_t seed = 1; seed <= 40; seed++) {
		const Item wand = Make(Row("Withered Wand"), seed, ITEM_QUALITY_RARE, 40);
		const Item sceptre = Make(Row("Lupine Sceptre"), seed, ITEM_QUALITY_RARE, 40);
		EXPECT_TRUE(wandWords.contains(secondWord(wand))) << GetRareItemName(wand);
		EXPECT_TRUE(sceptreWords.contains(secondWord(sceptre))) << GetRareItemName(sceptre);
		EXPECT_EQ(std::string(wand._iIName), GetRareItemName(wand));
	}
	// The older sceptres keep the weapon words (the S0 golden holds their names).
	EXPECT_EQ(WeaponKindOf(Row("Quick Bone Sceptre")), WeaponKind::LegacySceptre);
}

TEST_F(Poe2CasterBasesTest, WeaponGemsAndTheGemcuttingTabs)
{
	const auto item = [](std::string_view name) {
		Item made {};
		InitializeItem(made, Row(name));
		made._iStatFlag = true;
		return made;
	};
	// Staff gems need a martial staff; caster weapons take no weapon gem.
	EXPECT_TRUE(ItemAllowsWeaponSkill(item("Fine Long Quarterstaff"), WeaponSkill::WhirlingStaff));
	EXPECT_TRUE(ItemAllowsWeaponSkill(item("Long Staff"), WeaponSkill::WhirlingStaff));
	EXPECT_FALSE(ItemAllowsWeaponSkill(item("Gelid Staff"), WeaponSkill::WhirlingStaff));
	EXPECT_FALSE(ItemAllowsWeaponSkill(item("Gelid Staff"), WeaponSkill::LeapSlam));
	EXPECT_FALSE(ItemAllowsWeaponSkill(item("Ashen Staff"), WeaponSkill::FistsOfThunder));
	// Mace gems need a martial mace.
	EXPECT_TRUE(ItemAllowsWeaponSkill(item("Wooden Club"), WeaponSkill::Smite));
	EXPECT_FALSE(ItemAllowsWeaponSkill(item("Withered Wand"), WeaponSkill::Smite));
	EXPECT_FALSE(ItemAllowsWeaponSkill(item("Lupine Sceptre"), WeaponSkill::Smite));

	// The Staff tab is the quarterstaves'.
	EXPECT_EQ(GemCategoryName(GemCategory::Staff), "Quarterstaff");
	// An uncut skill gem opens on Elemental for a caster staff or wand, on Spirit for a new sceptre.
	Player &player = *MyPlayer;
	player._pClass = HeroClass::Warrior;
	Equip(player, INVLOC_HAND_LEFT, Row("Wooden Club"));
	EXPECT_EQ(DefaultSkillCategory(player), GemCategory::Mace);
	Equip(player, INVLOC_HAND_LEFT, Row("Withered Wand"));
	EXPECT_EQ(DefaultSkillCategory(player), GemCategory::Elemental);
	Equip(player, INVLOC_HAND_LEFT, Row("Gelid Staff"));
	EXPECT_EQ(DefaultSkillCategory(player), GemCategory::Elemental);
	Equip(player, INVLOC_HAND_LEFT, Row("Lupine Sceptre"));
	EXPECT_EQ(DefaultSkillCategory(player), GemCategory::Spirit);
	Equip(player, INVLOC_HAND_LEFT, Row("Fine Long Quarterstaff"));
	EXPECT_EQ(DefaultSkillCategory(player), GemCategory::Staff);
	player._pClass = HeroClass::Rogue;
	EXPECT_EQ(DefaultSkillCategory(player), GemCategory::Bow);
	player.InvBody[INVLOC_HAND_LEFT] = {};
}

TEST_F(Poe2CasterBasesTest, OutsidePoe2HandsHoldAsBefore)
{
	gbIsPoe2 = false;
	UnloadModArchives();
	LoadModArchives({ { "hf" } });
	LoadItemData();
	LoadModifierData();
	const auto item = [](std::string_view name) {
		Item made {};
		InitializeItem(made, Row(name));
		made._iStatFlag = true;
		return made;
	};
	const Item sword = item("Short Sword");
	const Item mace = item("Mace");
	const Item shield = item("Buckler");
	const Item staff = item("Short Staff");
	Player &player = *MyPlayer;
	player._pClass = HeroClass::Warrior;
	EXPECT_TRUE(HandsCanHold(player, sword, shield));
	EXPECT_FALSE(HandsCanHold(player, sword, mace));
	EXPECT_FALSE(HandsCanHold(player, staff, shield));
	player._pClass = HeroClass::Bard;
	EXPECT_TRUE(HandsCanHold(player, sword, mace));
	EXPECT_TRUE(HandsCanHold(player, sword, shield));
	EXPECT_FALSE(HandsCanHold(player, staff, sword));
}

} // namespace
} // namespace devilution
