#include <algorithm>
#include <array>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "engine/animationinfo.h"
#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "loadsave.h"
#include "pfile.h"
#include "player.h"
#include "poe2/attack_speed.hpp"
#include "poe2/bases.hpp"
#include "poe2/craft_state.hpp"
#include "poe2/default_attack.hpp"
#include "poe2/defenses.hpp"
#include "poe2/item_stats.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/stash_tabs.hpp"
#include "poe2/weapon_kinds.hpp"
#include "poe2/weapon_skills.hpp"
#include "qol/stash.h"
#include "spells.h"
#include "tables/playerdat.hpp"
#include "utils/file_util.h"
#include "utils/is_of.hpp"
#include "utils/paths.h"
#include "utils/str_cat.hpp"

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

	/**
	 * Saves the item in a single-player stash and loads it back. The stash writes and reads each item with the game
	 * save's own item record (SaveItem / LoadAndValidateItemData).
	 */
	static Item SaveAndLoad(Item item)
	{
		const std::string previousPrefPath = paths::PrefPath();
		const std::string folder = ::testing::TempDir() + "poe2_item_bases_" + ::testing::UnitTest::GetInstance()->current_test_info()->name() + DIRECTORY_SEPARATOR_STR;
		CreateDir(folder.c_str());
		for (const std::string &file : ListFiles(folder.c_str()))
			RemoveFile((folder + file).c_str());
		paths::SetPrefPath(folder);

		Stash = {};
		StashTabs.reset();
		// Fixed cells: GetInventorySize needs the cursor art from the game files, which tests may not have.
		const Size size { 2, 3 };
		item.position = { 0, size.height - 1 };
		Stash.stashList.push_back(item);
		for (int x = 0; x < size.width; x++) {
			for (int y = 0; y < size.height; y++)
				Stash.stashGrids[0][x][y] = 1;
		}
		Stash.dirty = true;
		sfile_write_stash();

		Stash = {};
		StashTabs.reset();
		LoadStash();
		Item loaded {};
		if (Stash.stashList.size() == 1)
			loaded = Stash.stashList[0];
		else
			ADD_FAILURE() << Stash.stashList.size() << " items came back";
		Stash = {};
		StashTabs.reset();
		paths::SetPrefPath(previousPrefPath);
		return loaded;
	}
};

TEST_F(Poe2ItemBasesTest, JewelsAndWaystonesTakeOneCell)
{
	for (const std::string_view name : { "Emerald", "Waystone", "Tablet", "Fragment" }) {
		Item item {};
		item._iCurs = AllItemsList[FindBase(name)].iCurs;
		EXPECT_TRUE(IsOneCellCopy(item)) << name;
		const Size size = GetInventorySize(item);
		EXPECT_EQ(size.width, 1) << name;
		EXPECT_EQ(size.height, 1) << name;
	}
	// A Book of Specialisation is a full four-cell book, like Diablo's spell books (Curtis 2026-10-20).
	Item book {};
	book._iCurs = AllItemsList[FindBase("Book of the Caves")].iCurs;
	EXPECT_EQ(book._iCurs, ICURS_BOOK_RED);
	EXPECT_FALSE(IsOneCellCopy(book));
	// The quest items with the same pictures keep their size.
	EXPECT_EQ(AllItemsList[IDI_ROCK].iCurs, ICURS_MAGIC_ROCK);
	EXPECT_EQ(AllItemsList[IDI_MAPOFDOOM].iCurs, ICURS_MAP_OF_THE_STARS);
}

TEST_F(Poe2ItemBasesTest, OneCellItemsLetGoOfTheirOldCells)
{
	Player &player = *MyPlayer;
	player._pNumInv = 1;
	player.InvList[0] = {};
	player.InvList[0]._iCurs = ICURS_MAGIC_ROCK_SMALL;
	std::fill(std::begin(player.InvGrid), std::end(player.InvGrid), 0);
	// Saved as a 2x2 jewel whose bottom-left cell is column 0 of row 1.
	player.InvGrid[0] = -1;
	player.InvGrid[1] = -1;
	player.InvGrid[10] = 1;
	player.InvGrid[11] = -1;

	FitOneCellItems(player);

	EXPECT_EQ(player.InvGrid[10], 1);
	EXPECT_EQ(player.InvGrid[0], 0);
	EXPECT_EQ(player.InvGrid[1], 0);
	EXPECT_EQ(player.InvGrid[11], 0);

	// A bigger item keeps all its cells.
	player.InvList[0]._iCurs = ICURS_MAGIC_ROCK;
	player.InvGrid[0] = -1;
	FitOneCellItems(player);
	EXPECT_EQ(player.InvGrid[0], -1);

	// A Stone Charm saved when it still had the 2x2 Magic Rock picture takes one cell.
	InitializeItem(player.InvList[0], FindBase("Stone Charm"));
	EXPECT_EQ(player.InvList[0]._iCurs, ICURS_RUNE_OF_STONE);
	player.InvList[0]._iCurs = ICURS_MAGIC_ROCK;
	EXPECT_TRUE(TakesOneCell(player.InvList[0]));
	FitOneCellItems(player);
	EXPECT_EQ(player.InvGrid[0], 0);
	EXPECT_EQ(player.InvGrid[10], 1);
	player._pNumInv = 0;
	player.InvList[0] = {};
	std::fill(std::begin(player.InvGrid), std::end(player.InvGrid), 0);
}

TEST_F(Poe2ItemBasesTest, OneCellItemsOnTheFloorFitTheirNewPicture)
{
	// An Emerald dropped before it took one cell was saved with the 20-frame Magic Rock picture, part way through it.
	Item item {};
	item._iCurs = ICURS_MAGIC_ROCK_SMALL;
	item._iAnimFlag = true;
	item.AnimInfo.numberOfFrames = 20;
	item.AnimInfo.currentFrame = 15;

	GetItemFrm(item);

	EXPECT_EQ(item.AnimInfo.numberOfFrames, 13);
	EXPECT_EQ(item.AnimInfo.currentFrame, 12);
	EXPECT_FALSE(item._iAnimFlag);

	// An item that already matches its picture keeps its frame.
	item._iCurs = ICURS_MAGIC_ROCK;
	item._iAnimFlag = true;
	item.AnimInfo.numberOfFrames = 20;
	item.AnimInfo.currentFrame = 15;
	GetItemFrm(item);
	EXPECT_EQ(item.AnimInfo.currentFrame, 15);
	EXPECT_TRUE(item._iAnimFlag);
}

TEST_F(Poe2ItemBasesTest, BundleBasesReplaceTheOldOnes)
{
	for (const std::string_view name : { "Shortsword", "Dark Blade", "Fine Wrapped Quarterstaff", "Crude Bow", "Fine Rattling Sceptre", "Ashen Staff", "Withered Wand", "Wooden Focus", "Iron Cap", "Hide Buckler", "Ruby Ring", "Azure Amulet" }) {
		const _item_indexes base = FindBase(name);
		EXPECT_GT(AllItemsList[base].dropRate, 0) << name;
		EXPECT_TRUE(IsItemAvailable(base)) << name;
		EXPECT_NE(BaseMetaFor(base), nullptr) << name;
	}
	// The old bases stay for items that already exist, but no longer drop.
	for (const std::string_view name : { "Long Sword", "Wrapped Quarterstaff", "Siege Crossbow", "Omen Sceptre", "Crystal Focus", "Great Helm", "Ring" })
		EXPECT_EQ(AllItemsList[FindBase(name)].dropRate, 0) << name;
	// So do the bundle's heavy and swift weapons and its crossbows (Curtis 2026-10-27: weapons are POE2's own bases).
	for (const std::string_view name : { "Heavy Imperial Edge", "Swift Shortsword", "Light Crossbow", "Dragonspine Crossbow" }) {
		EXPECT_EQ(AllItemsList[FindBase(name)].dropRate, 0) << name;
		EXPECT_NE(BaseMetaFor(FindBase(name)), nullptr) << name;
	}
	// The bundle's Focused and Quick staves and sceptres too, since the caster weapons rework's bases came in.
	for (const std::string_view name : { "Quick Bone Sceptre", "Focused Pilgrim Staff", "Quick Worldroot Staff", "Focused Sovereign Sceptre" }) {
		EXPECT_EQ(AllItemsList[FindBase(name)].dropRate, 0) << name;
		EXPECT_NE(BaseMetaFor(FindBase(name)), nullptr) << name;
	}
	EXPECT_EQ(AllItemsList[FindBase("Light Crossbow")].itype, ItemType::Bow);
	EXPECT_EQ(AllItemsList[FindBase("Quick Bone Sceptre")].itype, ItemType::Mace);
	EXPECT_EQ(AllItemsList[FindBase("Wooden Focus")].itype, ItemType::Shield);
	// Diablo's uniques keep dropping on the new bases.
	EXPECT_EQ(AllItemsList[FindBase("Crude Bow")].iItemId, UITYPE_SHORTBOW);
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
		// POE2 has no Coral Ring, so it only carries its unique; the heavy and swift weapons, the crossbows and the
		// Focused and Quick staves and sceptres only stay for saved items.
		if (data.iName != "Coral Ring" && !IsAnyOf(meta->identity, "heavy", "swift") && !IsAnyOf(meta->itemClass, "crossbow", "legacy_staff", "legacy_sceptre"))
			EXPECT_GT(data.dropRate, 0) << data.iName;
		// The caster weapons rework's staves, wands and sceptres hit for nothing: they cast.
		if (data.iClass == ICLASS_WEAPON && IsCasterWeapon(static_cast<int>(i)))
			EXPECT_EQ(data.iMaxDam, 0) << data.iName;
		else if (data.iClass == ICLASS_WEAPON)
			EXPECT_GT(data.iMaxDam, data.iMinDam) << data.iName;
		if (data.iClass == ICLASS_ARMOR) {
			EXPECT_GT(data.iMinAC, 0) << data.iName;
			EXPECT_TRUE(meta->defense.has_value()) << data.iName;
		}
	}
	// The bundle's 384, POE2's Crimson, Lunar and Gold Amulets, the 43 body armours of plan step 10, the 10 POE2
	// weapons that carry Diablo unique types no other base does and the caster weapons rework's 54.
	EXPECT_EQ(bases, 494);
}

TEST_F(Poe2ItemBasesTest, EveryDiabloWeaponUniqueHasABaseThatDrops)
{
	// Diablo's weapon bases stopped dropping when the bundle's came in, and each new base carries one Diablo unique type,
	// so some types were left on no base that drops. A POE2 base of their kind carries each of those.
	int checked = 0;
	for (const UniqueItem &unique : UniqueItems) {
		// The bundle's uniques have no Diablo type; The Butcher's Cleaver and Griswold's Edge have bases of their own.
		if (IsAnyOf(unique.UIItemId, UITYPE_INVALID, UITYPE_CLEAVER, UITYPE_GRISWOLD))
			continue;
		bool weapon = false;
		bool drops = false;
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			const ItemData &data = AllItemsList[i];
			if (data.iItemId != unique.UIItemId || data.iMiscId == IMISC_UNIQUE)
				continue;
			weapon = weapon || data.iClass == ICLASS_WEAPON;
			drops = drops || (data.dropRate > 0 && BaseMetaFor(static_cast<int>(i)) != nullptr);
		}
		if (!weapon)
			continue;
		checked++;
		EXPECT_TRUE(drops) << unique.UIName;
	}
	EXPECT_GE(checked, 60);
	EXPECT_EQ(AllItemsList[FindBase("Savage Greataxe")].iItemId, UITYPE_BATTLEAXE);
	EXPECT_EQ(AllItemsList[FindBase("Construct Hammer")].iItemId, UITYPE_WARHAMMER);
	EXPECT_EQ(AllItemsList[FindBase("Shortbow")].iItemId, UITYPE_LONGBOW);
	EXPECT_EQ(AllItemsList[FindBase("Shortbow")].iMinMLvl, 5);
}

TEST_F(Poe2ItemBasesTest, RingsFollowPoe2)
{
	// POE2's lineup and drop levels, which for jewellery are POE2 item levels.
	const auto level = [](std::string_view name) { return AllItemsList[FindBase(name)].iMinMLvl; };
	EXPECT_EQ(level("Iron Ring"), 1);
	EXPECT_EQ(level("Lazuli Ring"), 1);
	EXPECT_EQ(level("Ruby Ring"), 8);
	EXPECT_EQ(level("Sapphire Ring"), 12);
	EXPECT_EQ(level("Golden Hoop"), 12);
	EXPECT_EQ(level("Topaz Ring"), 16);
	EXPECT_EQ(level("Pearl Ring"), 32);
	EXPECT_GE(Stat(MakeItem("Pearl Ring", 32), Poe2ItemStat::CastSpeed), 7);
}

TEST_F(Poe2ItemBasesTest, AmuletsFollowPoe2)
{
	const auto level = [](std::string_view name) { return AllItemsList[FindBase(name)].iMinMLvl; };
	EXPECT_EQ(level("Crimson Amulet"), 1);
	EXPECT_EQ(level("Azure Amulet"), 1);
	EXPECT_EQ(level("Amber Amulet"), 8);
	EXPECT_EQ(level("Jade Amulet"), 8);
	EXPECT_EQ(level("Lapis Amulet"), 8);
	EXPECT_EQ(level("Lunar Amulet"), 14);
	EXPECT_EQ(level("Bloodstone Amulet"), 18);
	EXPECT_EQ(level("Stellar Amulet"), 24);
	EXPECT_EQ(level("Solar Amulet"), 30);
	EXPECT_EQ(level("Pearlescent Amulet"), 30);
	EXPECT_EQ(level("Gold Amulet"), 35);

	const Item crimson = MakeItem("Crimson Amulet", 1);
	EXPECT_GE(crimson._iPLLifeRegen, 2);
	const Item solar = MakeItem("Solar Amulet", 1);
	EXPECT_GE(Stat(solar, Poe2ItemStat::Spirit), 10);
	EXPECT_LE(Stat(solar, Poe2ItemStat::Spirit), 15);
	EXPECT_GE(Stat(MakeItem("Gold Amulet", 1), Poe2ItemStat::ItemRarity), 12);

	// The Lunar Amulet's energy shield rolls in defence points and shows as energy shield.
	const Item lunar = MakeItem("Lunar Amulet", 1);
	const int points = Stat(lunar, Poe2ItemStat::FlatEnergyShield);
	EXPECT_GE(points, 2);
	ASSERT_EQ(ItemImplicitText(lunar).size(), 1U);
	EXPECT_EQ(ItemImplicitText(lunar)[0], StrCat("+", points * EnergyShieldPerDefense, " to maximum Energy Shield"));
}

TEST_F(Poe2ItemBasesTest, HeavyAndSwiftBasesAreTheirSiblingsPoe2Base)
{
	// Curtis 2026-10-27: weapons are POE2's own bases, so a saved heavy or swift weapon is its balanced sibling's base.
	for (const auto &[old, real] : { std::pair { "Heavy Shortsword", "Shortsword" }, std::pair { "Swift Shortsword", "Shortsword" },
	         std::pair { "Heavy Imperial Edge", "Dark Blade" }, std::pair { "Swift Starbow", "Warmonger Bow" } }) {
		const ItemData &data = AllItemsList[FindBase(old)];
		const ItemData &base = AllItemsList[FindBase(real)];
		EXPECT_EQ(data.dropRate, 0) << old;
		EXPECT_EQ(data.iMinMLvl, base.iMinMLvl) << old;
		EXPECT_EQ(data.iMinDam, base.iMinDam) << old;
		EXPECT_EQ(data.iMaxDam, base.iMaxDam) << old;
		EXPECT_EQ(data.iMinStr, base.iMinStr) << old;
		EXPECT_EQ(data.iMinDex, base.iMinDex) << old;
		EXPECT_EQ(BaseMetaFor(FindBase(old))->aps, BaseMetaFor(FindBase(real))->aps) << old;
		EXPECT_EQ(BaseMetaFor(FindBase(old))->level, BaseMetaFor(FindBase(real))->level) << old;
		const Item item = MakeItem(old, 1);
		EXPECT_EQ(Stat(item, Poe2ItemStat::AttackSpeed), 0) << old;
		EXPECT_TRUE(ItemImplicitText(item).empty()) << old;
	}

	const Item quick = MakeItem("Quick Pilgrim Staff", 1);
	const Item focused = MakeItem("Focused Pilgrim Staff", 1);
	EXPECT_GT(Stat(quick, Poe2ItemStat::CastSpeed), Stat(focused, Poe2ItemStat::CastSpeed));
	// One implicit each (Curtis 2026-10-16): the quick staff casts faster, the focused one gives Intelligence.
	EXPECT_GT(focused._iPLMag, quick._iPLMag);
}

TEST_F(Poe2ItemBasesTest, WeaponBasesHaveTheirAttacksPerSecond)
{
	const auto aps = [](std::string_view name) {
		const BaseMeta *base = BaseMetaFor(FindBase(name));
		return base != nullptr ? static_cast<int>(base->aps) : -1;
	};
	// POE2's own (Path of Building 2's AttackRateBase).
	EXPECT_EQ(aps("Shortsword"), 155);
	EXPECT_EQ(aps("Heavy Shortsword"), 155);
	EXPECT_EQ(aps("Swift Shortsword"), 155);
	EXPECT_EQ(aps("Corroded Longsword"), 130);
	EXPECT_EQ(aps("Felled Greatclub"), 110);
	EXPECT_EQ(aps("Glass Shank"), 155);
	EXPECT_EQ(aps("Fine Wrapped Quarterstaff"), 140);
	EXPECT_EQ(aps("Crude Bow"), 120);
	EXPECT_EQ(aps("Messer"), 160);
	// Crossbows no longer drop and keep the bundle's.
	EXPECT_EQ(aps("Light Crossbow"), 90);
	// Caster weapons, armour and jewellery have none.
	EXPECT_EQ(aps("Quick Pilgrim Staff"), 0);
	EXPECT_EQ(aps("Iron Cap"), 0);
	EXPECT_EQ(aps("Ruby Ring"), 0);
}

/**
 * Plan step 14 (Curtis 2026-10-27): the first quarterstaff and bow, the only ones a hero uses up to about level 11, hit
 * 100% and 115% harder than their base (60% and 40% before attacks took POE2's times; research/attack-speed/rebalance):
 * POE2's Wrapped Quarterstaff 7-12 and Crude Bow 6-9 (the heavy and swift rows are the same bases). The next ones keep
 * POE2's numbers.
 */
TEST_F(Poe2ItemBasesTest, ActOneStavesAndBowsHitHarder)
{
	const auto damage = [](std::string_view name) {
		const ItemData &data = AllItemsList[FindBase(name)];
		return std::make_pair(static_cast<int>(data.iMinDam), static_cast<int>(data.iMaxDam));
	};
	EXPECT_EQ(damage("Fine Wrapped Quarterstaff"), std::make_pair(14, 24));
	EXPECT_EQ(damage("Heavy Ash Staff"), std::make_pair(14, 24));
	EXPECT_EQ(damage("Crude Bow"), std::make_pair(13, 19));
	EXPECT_EQ(damage("Swift Short Bow"), std::make_pair(13, 19));
	EXPECT_EQ(damage("Fine Gothic Quarterstaff"), std::make_pair(16, 26));
	EXPECT_EQ(damage("Recurve Bow"), std::make_pair(15, 31));
	// A white staff hits for its base.
	const Item staff = MakeItem("Fine Wrapped Quarterstaff", 1);
	EXPECT_EQ(staff._iMinDam, 14);
	EXPECT_EQ(staff._iMaxDam, 24);
}

TEST_F(Poe2ItemBasesTest, WeaponsAttackAtTheirBasesSpeed)
{
	// Curtis 2026-10-23: a base's own attacks per second set its attack time. Since 2026-10-27 they are POE2's: the
	// Shortsword 1.55 a second, the Dull Hatchet 1.5, and a saved Heavy Shortsword is a Shortsword.
	LoadPlayerDataFiles();
	Player &player = *MyPlayer;
	player = {};
	player.poe2.clear();
	player._pClass = HeroClass::Warrior;
	player.setCharacterLevel(10);
	player._pBaseStr = player._pStrength = 200;
	player._pBaseMag = player._pMagic = 200;
	player._pBaseDex = player._pDexterity = 200;
	player._pBaseVit = player._pVitality = 50;
	const auto attackTime = [&player](std::string_view base) {
		player.InvBody[INVLOC_HAND_LEFT] = MakeItem(base, 1);
		player.InvBody[INVLOC_HAND_LEFT]._iStatFlag = true;
		CalcPlrInv(player, false);
		return ActionSeconds(AttackTime(player, DefaultSkillSlot(player)));
	};
	EXPECT_NEAR(attackTime("Shortsword"), 1 / 1.55, 0.0001);
	EXPECT_NEAR(attackTime("Heavy Shortsword"), 1 / 1.55, 0.0001);
	EXPECT_NEAR(attackTime("Dull Hatchet"), 1 / 1.5, 0.0001);

	// A negative skip starts the animation before its first frame, so it reaches each frame a game tick later.
	const auto ticksToFrame = [](int8_t skipped, int8_t frame) {
		AnimationInfo animation;
		animation.setNewAnimation(std::nullopt, 16, 1, AnimationDistributionFlags::ProcessAnimationPending, skipped, 9);
		int ticks = 0;
		while (animation.currentFrame != frame && ticks < 40) {
			animation.processAnimation();
			ticks++;
		}
		return ticks;
	};
	EXPECT_EQ(ticksToFrame(-1, 8), ticksToFrame(0, 8) + 1);
	EXPECT_EQ(ticksToFrame(1, 8), ticksToFrame(0, 8) - 1);
	player.InvBody[INVLOC_HAND_LEFT] = {};
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
	EXPECT_EQ(ItemModSlot(MakeItem("Glass Shank", 1)), ModSlot::Dagger);
	EXPECT_EQ(ItemModSlot(MakeItem("Felled Greatclub", 1)), ModSlot::TwoHandMelee);
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
	const _item_indexes base = FindBase("Fine Wrapped Quarterstaff");
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

/** Plan step 9: the bundle's top weapon damage (199-281 on the base, +82 added) passes the old 8-bit fields. */
TEST_F(Poe2ItemBasesTest, WeaponDamageAbove255Survives)
{
	const _item_indexes base = FindBase("Heavy Shortsword");
	AllItemsList[base].iMinDam = 199;
	AllItemsList[base].iMaxDam = 281;
	Item sword = MakeItem("Heavy Shortsword", 1);
	EXPECT_GE(sword._iMinDam, 199);
	EXPECT_GE(sword._iMaxDam, 281);

	const int minDamage = sword._iMinDam + 82;
	const int maxDamage = sword._iMaxDam + 82;
	ApplyModEffect(sword, ModEffect::AddedPhysical, 82, 82);
	EXPECT_EQ(sword._iMinDam, minDamage);
	EXPECT_EQ(sword._iMaxDam, maxDamage);

	const Item loaded = SaveAndLoad(sword);
	EXPECT_EQ(loaded.IDidx, base);
	EXPECT_EQ(loaded._iMinDam, minDamage) << "A game save keeps damage past 255";
	EXPECT_EQ(loaded._iMaxDam, maxDamage);

	// Oil of Sharpness from another player (network and multiplayer hero items) keeps its 16 bits.
	Item oiled = sword;
	oiled._iMinDam = 340;
	oiled._iMaxDam = 363;
	EXPECT_EQ(ClampMaxDam(oiled, 364), 364);
	EXPECT_EQ(ClampMaxDam(oiled, 371), 363) << "Still no more than 30 over the minimum";

	// Added damage clamps at the field's top instead of wrapping.
	ApplyModEffect(sword, ModEffect::AddedPhysical, 70000, 70000);
	EXPECT_EQ(sword._iMaxDam, UINT16_MAX);
}

/** Plan step 9: body armour past 255 (the bundle's top is about 496) is built and saved whole. */
TEST_F(Poe2ItemBasesTest, ArmourAbove255Survives)
{
	const _item_indexes base = FindBase("Iron Cuirass");
	AllItemsList[base].iMinAC = 496;
	AllItemsList[base].iMaxAC = 496;
	const Item cuirass = MakeItem("Iron Cuirass", 1);
	EXPECT_GE(cuirass._iAC, 496);

	const Item loaded = SaveAndLoad(cuirass);
	EXPECT_EQ(loaded.IDidx, base);
	EXPECT_EQ(loaded._iAC, cuirass._iAC) << "A game save keeps armour past 255";
}


/** A rare of the base at POE2 item level 60, as a drop builds it. */
Item MakeRare(_item_indexes base, uint32_t seed)
{
	Item item {};
	SetLootRarity(item, ITEM_QUALITY_RARE);
	SetPoe2ItemLevel(item, 60);
	SetRndSeed(seed);
	SetupAllItems(*MyPlayer, item, base, AdvanceRndSeed(), 30, 1, /*onlygood=*/true, /*pregen=*/false, 0, /*forceNotUnique=*/true);
	item._iIdentified = true;
	return item;
}

bool HasMod(const Item &item, uint8_t family)
{
	return std::any_of(item._iPoe2Mods.begin(), item._iPoe2Mods.end(), [family](const Poe2ItemMod &mod) { return mod.family == family; });
}

void ExpectSameMods(const Item &actual, const Item &expected, std::string_view what)
{
	for (size_t i = 0; i < expected._iPoe2Mods.size(); i++) {
		EXPECT_EQ(actual._iPoe2Mods[i].family, expected._iPoe2Mods[i].family) << what << " mod " << i;
		EXPECT_EQ(actual._iPoe2Mods[i].tier, expected._iPoe2Mods[i].tier) << what << " mod " << i;
		EXPECT_EQ(actual._iPoe2Mods[i].values, expected._iPoe2Mods[i].values) << what << " mod " << i;
	}
	EXPECT_EQ(actual._iPoe2Stats, expected._iPoe2Stats) << what;
}

/** The points of an item's defence that come from its base: what its flat modifiers didn't add. */
int BaseDefence(const Item &item)
{
	const auto stat = [&item](Poe2ItemStat s) { return static_cast<int>(item._iPoe2Stats[static_cast<size_t>(s)]); };
	return item._iAC - stat(Poe2ItemStat::FlatArmour) - stat(Poe2ItemStat::FlatEvasion) - stat(Poe2ItemStat::FlatEnergyShield);
}

/** The low end of an item's damage that comes from its base: what its added physical damage didn't add. */
int BaseMinDamage(const Item &item, uint8_t addedPhysical)
{
	int damage = item._iMinDam;
	for (const Poe2ItemMod &mod : item._iPoe2Mods) {
		if (mod.family == addedPhysical)
			damage -= mod.values[0];
	}
	return damage;
}

/**
 * Harder monsters plan v3: an item saved before the bases and flat modifiers took POE2's numbers loads with exactly
 * the armour and damage it had, its modifiers keep the values they rolled with, and no rebuild takes its base's
 * share below what it had.
 */
TEST_F(Poe2ItemBasesTest, OldItemsKeepTheirArmourAndDamage)
{
	const _item_indexes armourBase = FindBase("Rogue Armour");
	const _item_indexes weaponBase = FindBase("Shortsword");
	const std::optional<uint8_t> flatArmourFamily = FindModifierFamily("flat_armour");
	const std::optional<uint8_t> flatEvasionFamily = FindModifierFamily("flat_evasion");
	const std::optional<uint8_t> addedPhysicalFamily = FindModifierFamily("local_added_physical");
	ASSERT_TRUE(flatArmourFamily && flatEvasionFamily && addedPhysicalFamily);
	const auto flatArmour = static_cast<uint8_t>(*flatArmourFamily + 1);
	const auto flatEvasion = static_cast<uint8_t>(*flatEvasionFamily + 1);
	const auto addedPhysical = static_cast<uint8_t>(*addedPhysicalFamily + 1);

	// The game before: the item table's old numbers for the two bases, and the modifiers' old scales.
	AllItemsList[armourBase].iMinAC = 16;
	AllItemsList[armourBase].iMaxAC = 18;
	AllItemsList[weaponBase].iMinDam = 2;
	AllItemsList[weaponBase].iMaxDam = 6;
	Item armour;
	Item weapon;
	{
		const LegacyModScales legacy;
		for (uint32_t seed = 1; seed < 2000 && armour.isEmpty(); seed++) {
			const Item item = MakeRare(armourBase, seed);
			if (HasMod(item, flatArmour) || HasMod(item, flatEvasion))
				armour = item;
		}
		for (uint32_t seed = 1; seed < 2000 && weapon.isEmpty(); seed++) {
			const Item item = MakeRare(weaponBase, seed);
			if (HasMod(item, addedPhysical))
				weapon = item;
		}
	}
	ASSERT_FALSE(armour.isEmpty()) << "an old Rogue Armour with flat armour or evasion";
	ASSERT_FALSE(weapon.isEmpty()) << "an old Shortsword with added physical damage";
	ASSERT_NE(armour.dwBuff & CF_POE2, 0U);
	const int oldArmourBase = BaseDefence(armour);
	EXPECT_GE(oldArmourBase, 16);
	EXPECT_LE(oldArmourBase, 18);
	EXPECT_EQ(BaseMinDamage(weapon, addedPhysical), 2);
	// Before, a hybrid's defence split evenly.
	DefensePoints oldDefence;
	SplitDefense(ItemDefenseType(armour), oldArmourBase, oldDefence.armour, oldDefence.evasion, oldDefence.energyShield);
	oldDefence.armour += Stat(armour, Poe2ItemStat::FlatArmour);
	oldDefence.evasion += Stat(armour, Poe2ItemStat::FlatEvasion);

	// This build's tables.
	LoadItemData();
	LoadModifierData();
	ASSERT_GT(AllItemsList[armourBase].iMinAC, armour._iAC) << "today's base alone has more than the old item";
	ASSERT_GT(AllItemsList[weaponBase].iMinDam, BaseMinDamage(weapon, addedPhysical));

	// A fresh rebuild has today's numbers, which is how an old item is told apart.
	Item fresh;
	RecreateItem(*MyPlayer, fresh, armour.IDidx, armour._iCreateInfo, armour._iSeed, armour._ivalue, armour.dwBuff, armour._iPoe2Craft);
	EXPECT_GT(fresh._iAC, armour._iAC);
	EXPECT_TRUE(IsLegacyScaleItem(armour, fresh));
	EXPECT_FALSE(IsLegacyScaleItem(fresh, fresh)) << "an item of this build never is";

	for (const Item *old : { &armour, &weapon }) {
		const std::string name = old->_iIName;
		const Item loaded = SaveAndLoad(*old);
		EXPECT_EQ(loaded.IDidx, old->IDidx) << name;
		EXPECT_EQ(loaded._iAC, old->_iAC) << name;
		EXPECT_EQ(loaded._iMinDam, old->_iMinDam) << name;
		EXPECT_EQ(loaded._iMaxDam, old->_iMaxDam) << name;
		ExpectSameMods(loaded, *old, name);

		// Crafting rebuilds it from its seed and keeps all of that.
		const Item crafted = RebuildWithCraft(*MyPlayer, loaded, loaded.dwBuff, loaded._iPoe2Craft);
		EXPECT_EQ(crafted._iAC, old->_iAC) << name;
		EXPECT_EQ(crafted._iMinDam, old->_iMinDam) << name;
		EXPECT_EQ(crafted._iMaxDam, old->_iMaxDam) << name;
		ExpectSameMods(crafted, *old, name);
		EXPECT_FALSE(RollingLegacyModScales());

		// A Divine Orb rerolls the modifiers' values, with their old scales, and leaves the base's share alone.
		const Item divined = RebuildWithCraft(*MyPlayer, loaded, (loaded.dwBuff & ~CF_POE2_DIVINE) | (1U << 9), loaded._iPoe2Craft);
		EXPECT_EQ(BaseDefence(divined), BaseDefence(*old)) << name;
		EXPECT_EQ(BaseMinDamage(divined, addedPhysical), BaseMinDamage(*old, addedPhysical)) << name;
		for (const Poe2ItemMod &mod : divined._iPoe2Mods) {
			if (mod.family != flatArmour && mod.family != addedPhysical)
				continue;
			const ModFamily &family = *ItemModFamily(mod);
			const ModTier &tier = family.tiers[mod.tier - 1];
			EXPECT_LE(mod.values[0], ((tier.max[0] * family.legacyScale[0]) + 99) / 100) << name << ": " << family.id;
		}
	}

	// The armour shows what it showed: its defence is below what the base rolls now, so it splits evenly as before.
	DefensePoints base;
	DefensePoints bonus;
	SplitItemDefense(SaveAndLoad(armour), base, bonus);
	EXPECT_EQ(base.armour, oldDefence.armour);
	EXPECT_EQ(base.evasion, oldDefence.evasion);
	EXPECT_EQ(base.energyShield, 0);
}

TEST_F(Poe2ItemBasesTest, GearNeedsPoe2sFullAttributes)
{
	// Curtis 2026-10-27, after the Warrior playtest: the bases need POE2's own attributes, no longer 55% of them.
	const auto needs = [](std::string_view name) {
		const ItemData &data = AllItemsList[FindBase(name)];
		return std::array<int, 3> { data.iMinStr, data.iMinMag, data.iMinDex };
	};
	EXPECT_EQ(needs("Iron Cap"), (std::array<int, 3> { 5, 0, 0 }));
	EXPECT_EQ(needs("Legion Helm"), (std::array<int, 3> { 78, 0, 0 }));
	EXPECT_EQ(needs("Titan Crown"), (std::array<int, 3> { 150, 0, 0 }));
	EXPECT_EQ(needs("Shortsword"), (std::array<int, 3> { 0, 0, 0 }));
	EXPECT_EQ(needs("Vampiric Blade"), (std::array<int, 3> { 14, 0, 14 }));
	EXPECT_EQ(needs("Dark Blade"), (std::array<int, 3> { 63, 0, 63 }));
	EXPECT_EQ(needs("Fine Gothic Quarterstaff"), (std::array<int, 3> { 0, 9, 18 }));
	EXPECT_EQ(needs("Ruby Ring"), (std::array<int, 3> { 0, 0, 0 }));
	EXPECT_EQ(needs("Crimson Amulet"), (std::array<int, 3> { 0, 0, 0 }));
	// Diablo's bases keep theirs.
	EXPECT_EQ(needs("Full Plate Mail"), (std::array<int, 3> { 90, 0, 0 }));

	// A requirement past 127 stays whole on the item and through a save, and is checked.
	LoadPlayerDataFiles();
	Player &player = *MyPlayer;
	player = {};
	player.poe2.clear();
	player._pClass = HeroClass::Warrior;
	player.setCharacterLevel(player.getMaxCharacterLevel());
	player._pBaseMag = player._pMagic = 50;
	player._pBaseDex = player._pDexterity = 50;
	const Item crown = MakeItem("Titan Crown", 7);
	EXPECT_EQ(crown._iMinStr, 150);
	EXPECT_EQ(SaveAndLoad(crown)._iMinStr, 150);
	player._pBaseStr = player._pStrength = 149;
	EXPECT_FALSE(player.CanUseItem(crown));
	player._pBaseStr = player._pStrength = 150;
	EXPECT_TRUE(player.CanUseItem(crown));
}

TEST_F(Poe2ItemBasesTest, GearNeedsItsCharacterLevel)
{
	// Curtis 2026-10-27, after the Warrior playtest: like POE2's, a base needs the character level of its required level.
	LoadPlayerDataFiles();
	Player &player = *MyPlayer;
	player = {};
	player.poe2.clear();
	player._pClass = HeroClass::Warrior;
	player._pBaseStr = player._pStrength = 200;
	player._pBaseMag = player._pMagic = 200;
	player._pBaseDex = player._pDexterity = 200;
	player._pBaseVit = player._pVitality = 50;
	const Item helm = MakeItem("Legion Helm", 3);
	ASSERT_EQ(ItemRequiredLevel(helm), 48);
	EXPECT_EQ(ItemRequiredLevel(MakeItem("Iron Cap", 3)), 1);
	EXPECT_EQ(ItemRequiredLevel(MakeItem("Titan Crown", 3)), 80);
	EXPECT_EQ(ItemRequiredLevel(MakeItem("Ruby Ring", 3)), 8);
	EXPECT_EQ(ItemRequiredLevel(MakeItem("Gold Amulet", 3)), 35);
	EXPECT_EQ(ItemRequiredLevel(MakeItem("Conqueror Plate", 3)), 68);
	// Diablo's bases need none.
	EXPECT_EQ(ItemRequiredLevel(MakeItem("Full Plate Mail", 3)), 0);

	player.setCharacterLevel(47);
	EXPECT_FALSE(player.CanUseItem(helm));
	player.setCharacterLevel(48);
	EXPECT_TRUE(player.CanUseItem(helm));

	// Worn below its level it gives nothing, like gear that needs more Strength.
	player.InvBody[INVLOC_HEAD] = helm;
	player.setCharacterLevel(47);
	CalcPlrInv(player, false);
	EXPECT_FALSE(player.InvBody[INVLOC_HEAD]._iStatFlag);
	const int armourBelow = player._pIAC;
	player.setCharacterLevel(48);
	CalcPlrInv(player, false);
	EXPECT_TRUE(player.InvBody[INVLOC_HEAD]._iStatFlag);
	EXPECT_GT(player._pIAC, armourBelow);
	player.InvBody[INVLOC_HEAD] = {};

	// Outside POE2 mode nothing changes.
	gbIsPoe2 = false;
	EXPECT_EQ(ItemRequiredLevel(helm), 0);
	player.setCharacterLevel(1);
	EXPECT_TRUE(player.CanUseItem(helm));
	gbIsPoe2 = true;
}

TEST_F(Poe2ItemBasesTest, RequirementLineShowsTheLevel)
{
	EXPECT_EQ(ItemRequirementText(48, 78, 0, 0), "Required: Level 48, 78 Str");
	EXPECT_EQ(ItemRequirementText(48, 0, 0, 0), "Required: Level 48");
	EXPECT_EQ(ItemRequirementText(33, 37, 0, 37), "Required: Level 33, 37 Str 37 Dex");
	// Level 1 isn't a requirement, and Diablo's line is as it was.
	EXPECT_EQ(ItemRequirementText(1, 5, 0, 0), "Required: 5 Str");
	EXPECT_EQ(ItemRequirementText(0, 30, 0, 30), "Required: 30 Str 30 Dex");
	EXPECT_EQ(ItemRequirementText(0, 0, 25, 0), "Required: 25 Mag");
	EXPECT_EQ(ItemRequirementText(1, 0, 0, 0), "");
	EXPECT_EQ(ItemRequirementText(0, 0, 0, 0), "");
}

TEST_F(Poe2ItemBasesTest, NewHeroesStartWithPoe2Bases)
{
	LoadPlayerDataFiles();
	struct Start {
		HeroClass heroClass;
		std::string_view weapon;
		std::string_view offHand;
		WeaponSkill starterSkill;
		DefaultAttack attack;
	};
	const Start starts[] = {
		{ HeroClass::Warrior, "Shortsword", "Wooden Tower Shield", WeaponSkill::Cleave, DefaultAttack::SwordStrike },
		{ HeroClass::Rogue, "Crude Bow", "", WeaponSkill::Barrage, DefaultAttack::BowShot },
		{ HeroClass::Monk, "Fine Wrapped Quarterstaff", "", WeaponSkill::WhirlingStaff, DefaultAttack::StaffStrike },
		{ HeroClass::Sorcerer, "Focused Pilgrim Staff", "", WeaponSkill::None, DefaultAttack::StaffBolt },
		{ HeroClass::Bard, "Shortsword", "Glass Shank", WeaponSkill::Flurry, DefaultAttack::SwordStrike },
	};
	for (const Start &start : starts) {
		const PlayerStartingLoadoutData &loadout = GetPlayerStartingLoadoutForClass(start.heroClass);
		const _item_indexes weapon = GetStartingLoadoutItem(loadout, 0);
		ASSERT_NE(weapon, IDI_NONE) << start.weapon;
		EXPECT_EQ(AllItemsList[weapon].iName, start.weapon);
		ASSERT_NE(BaseMetaFor(weapon), nullptr) << start.weapon << " is a POE2 base";

		Player &player = *MyPlayer;
		player = {};
		player._pClass = start.heroClass;
		InitializeItem(player.InvBody[INVLOC_HAND_LEFT], weapon);
		if (!start.offHand.empty()) {
			const _item_indexes offHand = GetStartingLoadoutItem(loadout, 1);
			ASSERT_NE(offHand, IDI_NONE) << start.offHand;
			EXPECT_EQ(AllItemsList[offHand].iName, start.offHand);
			InitializeItem(player.InvBody[INVLOC_HAND_RIGHT], offHand);
		}
		// The starting gem still fits the new weapon, and the free attack is the weapon's own.
		if (start.starterSkill != WeaponSkill::None)
			EXPECT_TRUE(WeaponAllowsSkill(player, start.starterSkill)) << start.weapon;
		EXPECT_EQ(DefaultAttackFor(player), start.attack) << start.weapon;
		// One healing flask and one mana flask go in the belt's two flask slots (Curtis 2026-10-28).
		EXPECT_EQ(std::count(loadout.items.begin(), loadout.items.end(), IDI_HEAL), 1) << start.weapon;
		EXPECT_EQ(std::count(loadout.items.begin(), loadout.items.end(), IDI_MANA), 1) << start.weapon;
	}
	EXPECT_EQ(std::count(GetPlayerStartingLoadoutForClass(HeroClass::Barbarian).items.begin(), GetPlayerStartingLoadoutForClass(HeroClass::Barbarian).items.end(), IDI_MANA), 1);
	EXPECT_EQ(AllItemsList[GetStartingLoadoutItem(GetPlayerStartingLoadoutForClass(HeroClass::Warrior), 1)].itype, ItemType::Shield);
	// The Barbarian keeps Diablo's start.
	EXPECT_EQ(GetStartingLoadoutItem(GetPlayerStartingLoadoutForClass(HeroClass::Barbarian), 0), IDI_BARBARIAN);
	// Outside POE2 mode a 'poe2:' id names nothing.
	gbIsPoe2 = false;
	EXPECT_EQ(GetStartingLoadoutItem(GetPlayerStartingLoadoutForClass(HeroClass::Warrior), 0), IDI_NONE);
	gbIsPoe2 = true;

	// Diablo's heroes start as before.
	UnloadModArchives();
	LoadPlayerDataFiles();
	LoadItemData();
	const PlayerStartingLoadoutData &warrior = GetPlayerStartingLoadoutForClass(HeroClass::Warrior);
	EXPECT_EQ(GetStartingLoadoutItem(warrior, 0), IDI_WARRIOR);
	EXPECT_EQ(GetStartingLoadoutItem(warrior, 1), IDI_WARRSHLD);
	EXPECT_EQ(std::count(warrior.items.begin(), warrior.items.end(), IDI_HEAL), 2);
	EXPECT_EQ(GetStartingLoadoutItem(GetPlayerStartingLoadoutForClass(HeroClass::Rogue), 0), IDI_ROGUE);
	EXPECT_FALSE(FindPoe2ItemId("poe2:one_hand_sword_balanced_shortsword").has_value());
}

TEST_F(Poe2ItemBasesTest, ShieldsBlockWithTheirBasesOwnChance)
{
	LoadPlayerDataFiles();
	LoadPassiveTreeData();
	Player &player = *MyPlayer;
	player = {};
	player.poe2.clear();
	player._pClass = HeroClass::Warrior;
	player.setCharacterLevel(1);
	player._pBaseStr = player._pStrength = 30;
	player._pBaseMag = player._pMagic = 10;
	player._pBaseDex = player._pDexterity = 20;
	player._pBaseVit = player._pVitality = 25;
	player._pMaxHPBase = player._pHPBase = Fixed26_6::fromInt(100);
	player._pMaxManaBase = player._pManaBase = Fixed26_6::fromInt(20);

	// No shield, no block.
	CalcPlrItemVals(player, false);
	EXPECT_EQ(player.GetBlockChance(), 0);
	EXPECT_EQ(BlockChanceAgainstMonster(player, nullptr, true), 0);

	// A Warrior with a Wooden Tower Shield and 20 Dexterity blocks 28%, the shield's own chance.
	Item &shield = player.InvBody[INVLOC_HAND_RIGHT];
	InitializeItem(shield, FindBase("Wooden Tower Shield"));
	shield._iStatFlag = true;
	EXPECT_EQ(ShieldBlockChance(shield), 28);
	CalcPlrItemVals(player, false);
	ASSERT_TRUE(player._pBlockFlag);
	EXPECT_EQ(player.GetBlockChance(), 28);
	EXPECT_EQ(player.GetBlockChance(false), 28);
	EXPECT_EQ(BlockChanceAgainstMonster(player, nullptr, true), 28);
	EXPECT_EQ(BlockChanceAgainstMonster(player, nullptr, false), 28);

	// Dexterity doesn't add to it.
	player._pBaseDex = player._pDexterity = 60;
	CalcPlrItemVals(player, false);
	EXPECT_EQ(player.GetBlockChance(), 28);

	// The tree's block adds on top, and Magma Barrier raises the sum.
	PassiveNodeId rimAndEdge = 0;
	for (const PassiveNode &node : PassiveTree()) {
		if (node.name == "Rim and Edge")
			rimAndEdge = node.id;
	}
	ASSERT_NE(rimAndEdge, 0);
	player.poe2.passives.set(rimAndEdge);
	CalcPlrItemVals(player, false);
	EXPECT_EQ(player.poe2.passiveBlockChance, 3);
	EXPECT_EQ(player.GetBlockChance(), 31);
	player.poe2.spiritState.blockPercent = 25;
	EXPECT_EQ(player.GetBlockChance(), 31 * 125 / 100);
	player.poe2.spiritState.blockPercent = 0;

	// A shield the hero can't use doesn't block.
	shield._iStatFlag = false;
	CalcPlrItemVals(player, false);
	EXPECT_EQ(player.GetBlockChance(), 0);

	// Each line of POE2 shields, from its first base to its last, and Diablo's shields, which block like POE2's: 25%
	// for a buckler or small shield up to 30% for a large or tower shield. Foci don't block.
	const std::pair<std::string_view, int> shields[] = {
		{ "Wooden Tower Shield", 28 }, { "Titan Tower Shield", 35 },
		{ "Hide Buckler", 25 }, { "Phantom Buckler", 32 },
		{ "Round Shield", 26 }, { "Conqueror Shield", 33 },
		{ "Reliquary Shield", 25 }, { "Seraph Shield", 32 },
		{ "Wooden Focus", 0 },
		{ "Small Shield", 25 }, { "Kite Shield", 27 }, { "Large Shield", 30 }, { "Tower Shield", 30 }, { "Gothic Shield", 30 },
		{ "Twig Focus", 0 },
	};
	for (const auto &[name, block] : shields) {
		Item item;
		InitializeItem(item, FindBase(name));
		EXPECT_EQ(ShieldBlockChance(item), block) << name;
	}
	// The Warrior's old starting Buckler.
	Item buckler;
	InitializeItem(buckler, IDI_WARRSHLD);
	EXPECT_EQ(ShieldBlockChance(buckler), 25);

	// Diablo keeps Dexterity, the class's base and the level.
	player.poe2.passives.reset(rimAndEdge);
	shield._iStatFlag = true;
	CalcPlrItemVals(player, false);
	gbIsPoe2 = false;
	EXPECT_EQ(player.GetBlockChance(), player._pDexterity + player.getBaseToBlock() + 2);
	gbIsPoe2 = true;
}

} // namespace
} // namespace devilution
