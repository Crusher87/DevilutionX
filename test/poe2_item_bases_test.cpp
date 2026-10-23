#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>

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
#include "poe2/bases.hpp"
#include "poe2/defenses.hpp"
#include "poe2/item_stats.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/stash_tabs.hpp"
#include "qol/stash.h"
#include "spells.h"
#include "tables/playerdat.hpp"
#include "utils/file_util.h"
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
	for (const std::string_view name : { "Shortsword", "Heavy Imperial Edge", "Ash Staff", "Light Crossbow", "Quick Bone Sceptre", "Wooden Focus", "Iron Cap", "Hide Buckler", "Ruby Ring", "Azure Amulet" }) {
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
		// POE2 has no Coral Ring, so it only carries its unique.
		if (data.iName != "Coral Ring")
			EXPECT_GT(data.dropRate, 0) << data.iName;
		if (data.iClass == ICLASS_WEAPON)
			EXPECT_GT(data.iMaxDam, data.iMinDam) << data.iName;
		if (data.iClass == ICLASS_ARMOR) {
			EXPECT_GT(data.iMinAC, 0) << data.iName;
			EXPECT_TRUE(meta->defense.has_value()) << data.iName;
		}
	}
	// The bundle's 384, plus POE2's Crimson, Lunar and Gold Amulets.
	EXPECT_EQ(bases, 387);
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

TEST_F(Poe2ItemBasesTest, HeavyBasesAttackSlower)
{
	// Curtis 2026-10-23: a Heavy base's reduced attack speed only cancelled speed from elsewhere, so on its own it was
	// never slower. Now it adds an animation frame for every 10%.
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
	const auto attackFrames = [&player](std::string_view base) {
		player.InvBody[INVLOC_HAND_LEFT] = MakeItem(base, 1);
		player.InvBody[INVLOC_HAND_LEFT]._iStatFlag = true;
		CalcPlrInv(player, false);
		return player.poe2.passiveAttackFrames;
	};
	EXPECT_EQ(attackFrames("Heavy Shortsword"), -1);
	EXPECT_EQ(attackFrames("Shortsword"), 0);
	EXPECT_EQ(attackFrames("Swift Shortsword"), 1);

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

} // namespace
} // namespace devilution
