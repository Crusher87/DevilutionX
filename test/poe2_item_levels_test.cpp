/**
 * POE2 Mashup bases and item levels (harder monsters plan, step 10b): the bases take POE2's own levels, damage and
 * defence, equipment drops by the area level, and currency keeps the item level scale it had.
 */
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "monster.h"
#include "player.h"
#include "poe2/area_levels.hpp"
#include "poe2/bases.hpp"
#include "poe2/default_attack.hpp"
#include "poe2/defenses.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "spells.h"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2ItemLevelsTest : public ::testing::Test {
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
		sgGameInitInfo.nDifficulty = DIFF_NORMAL;
		setlevel = false;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadSpellData();
		LoadPlayerDataFiles();
		LoadMonsterData();
		// Also loads the bases and the area levels.
		LoadModifierData();
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		sgGameInitInfo.nDifficulty = DIFF_NORMAL;
		MyPlayer->poe2.mapTier = 0;
		leveltype = DTYPE_TOWN;
		currlevel = 0;
		LoadModifierData();
		UnloadModArchives();
	}

	static _item_indexes FindBase(std::string_view name)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == name && AllItemsList[i].dropRate > 0)
				return static_cast<_item_indexes>(i);
		}
		ADD_FAILURE() << name;
		return IDI_GOLD;
	}

	/** A normal item of the base, built the way a drop is. */
	static Item MakeItem(std::string_view name, uint32_t seed = 1)
	{
		Item item {};
		SetLootRarity(item, ITEM_QUALITY_NORMAL);
		SetRndSeed(seed);
		SetupAllItems(*MyPlayer, item, FindBase(name), AdvanceRndSeed(), 30, 1, /*onlygood=*/false, /*pregen=*/false, 0, /*forceNotUnique=*/true);
		return item;
	}

	/** Breaks `count` barrels in a map of the tier and keeps what they drop. */
	static std::vector<Item> MapContainerDrops(int tier, int count)
	{
		currlevel = 13;
		leveltype = DTYPE_HELL;
		MyPlayer->poe2.mapTier = static_cast<uint8_t>(tier);
		MyPlayer->poe2.mapLevel = 13;
		std::vector<Item> drops;
		for (uint32_t seed = 1; seed <= static_cast<uint32_t>(count); seed++) {
			SetRndSeed(seed);
			ActiveItemCount = 0;
			for (int i = 0; i < MAXITEMS; i++)
				ActiveItems[i] = i;
			CreateRndItem({ 40, 40 }, false, false, false);
			for (int i = 0; i < ActiveItemCount; i++)
				drops.push_back(Items[ActiveItems[i]]);
			for (int x = 0; x < MAXDUNX; x++)
				for (int y = 0; y < MAXDUNY; y++)
					dItem[x][y] = 0;
		}
		MyPlayer->poe2.mapTier = 0;
		return drops;
	}

	static Monster MakeMonster(_monster_id type, UniqueMonsterType unique = UniqueMonsterType::None)
	{
		LevelMonsterTypes[0].type = type;
		Monster monster {};
		monster.levelType = 0;
		monster.uniqueType = unique;
		monster.flags = 0;
		return monster;
	}
};

TEST_F(Poe2ItemLevelsTest, TopStaffIsPoe2sDreamingQuarterstaff)
{
	// Weapons are POE2's own bases (Curtis 2026-10-27): the last quarterstaff is the Dreaming Quarterstaff, 99-133 at 78.
	const ItemData &staff = AllItemsList[FindBase("Dreaming Quarterstaff")];
	EXPECT_EQ(staff.iMinDam, 99);
	EXPECT_EQ(staff.iMaxDam, 133);
	EXPECT_EQ(staff.iMinMLvl, 78) << "drop levels are POE2's area levels";
	const Item built = MakeItem("Dreaming Quarterstaff");
	EXPECT_EQ(built._iMinDam, 99);
	EXPECT_EQ(built._iMaxDam, 133);

	// A base's defence is its armour plus its evasion plus a third of its energy shield, and every one rolls exactly that.
	EXPECT_EQ(AllItemsList[FindBase("Conqueror Plate")].iMinAC, 496);
	EXPECT_EQ(AllItemsList[FindBase("Enlightened Robe")].iMinAC, 138 / EnergyShieldPerDefense);
	const ItemData &regalia = AllItemsList[FindBase("Grand Regalia")];
	EXPECT_EQ(regalia.iMinAC, 182 + 165 + 17);
	EXPECT_EQ(regalia.iMaxAC, regalia.iMinAC);
	EXPECT_EQ(regalia.iMinMLvl, 65);
}

TEST_F(Poe2ItemLevelsTest, TopBasesDropAt80)
{
	// The top two-handed mace is POE2's Massive Greathammer; the Titan Crown is among the bases that drop at 80.
	const ItemData &hammer = AllItemsList[FindBase("Massive Greathammer")];
	EXPECT_EQ(hammer.iMinMLvl, 77);
	EXPECT_EQ(hammer.iMinDam, 119);
	EXPECT_EQ(hammer.iMaxDam, 161);
	EXPECT_EQ(AllItemsList[FindBase("Titan Crown")].iMinMLvl, 80);

	// Uniques keep their own drop levels and item levels.
	const auto isTopBase = [](const Item &item) {
		return item._iMagical != ITEM_QUALITY_UNIQUE && AllItemsList[static_cast<size_t>(item.IDidx)].iMinMLvl == 80;
	};
	// A tier 15 map is area level 77: no base of drop level 80 yet.
	ASSERT_EQ(MapMonsterAreaLevel(15), 77);
	for (const Item &item : MapContainerDrops(15, 3000)) {
		if (EquipmentClassWeight(item._itype) != 0 && item._iMagical != ITEM_QUALITY_UNIQUE)
			EXPECT_LE(AllItemsList[static_cast<size_t>(item.IDidx)].iMinMLvl, 77) << item._iIName;
	}

	// Tier 16 is area level 80: equipment of item level 80, on bases of drop level 80 at most, those of 80 among them.
	ASSERT_EQ(MapMonsterAreaLevel(16), 80);
	int topBases = 0;
	for (const Item &item : MapContainerDrops(16, 3000)) {
		if (EquipmentClassWeight(item._itype) == 0 || item._iMagical == ITEM_QUALITY_UNIQUE)
			continue;
		EXPECT_EQ(GetPoe2ItemLevel(item), 80) << item._iIName;
		EXPECT_LE(AllItemsList[static_cast<size_t>(item.IDidx)].iMinMLvl, 80) << item._iIName;
		if (isTopBase(item))
			topBases++;
	}
	EXPECT_GT(topBases, 0);
}

TEST_F(Poe2ItemLevelsTest, CurrencyGatesUnchanged)
{
	// The orbs' drop levels stay on the item level scale they had (Chaos 12 ... Perfect Jeweller's 46).
	EXPECT_EQ(CurrencyDropLevel(IMISC_ORB_TRANSMUTE), 1);
	EXPECT_EQ(CurrencyDropLevel(IMISC_ORB_CHAOS), 12);
	EXPECT_EQ(CurrencyDropLevel(IMISC_ORB_DIVINE), 35);
	EXPECT_EQ(CurrencyDropLevel(IMISC_ORB_PERFECT_JEWELLER), 46);
	EXPECT_EQ(CurrencyDropLevel(IMISC_ORB_GREATER_CHAOS), 72);

	// Hell's Hell 4 (area level 67): equipment drops at the area level plus the boss's two, currency at 1.3 times
	// Diablo's monster level as before.
	sgGameInitInfo.nDifficulty = DIFF_HELL;
	currlevel = 16;
	leveltype = DTYPE_HELL;
	ASSERT_EQ(CurrentAreaLevel(DIFF_HELL), 37 + (2 * AreaLevelsPerDifficulty));
	const Monster butcher = MakeMonster(MT_CLEAVER, UniqueMonsterType::Butcher);
	ASSERT_EQ(GetLootSource(butcher), LootSource::Boss);
	const int bonus = LootItemLevelBonus(LootSource::Boss);
	EXPECT_EQ(Poe2ItemLevel(butcher, DIFF_HELL), 67 + bonus);
	EXPECT_EQ(Poe2CurrencyLevel(butcher, DIFF_HELL), Poe2ItemLevel(butcher.data().level + (2 * AreaLevelsPerDifficulty), bonus));
	EXPECT_LT(Poe2CurrencyLevel(butcher, DIFF_HELL), Poe2ItemLevel(butcher, DIFF_HELL));

	// Without the area level table everything keeps the old scale.
	gbIsPoe2 = false;
	LoadAreaLevels();
	gbIsPoe2 = true;
	EXPECT_EQ(Poe2ItemLevel(butcher, DIFF_HELL), Poe2CurrencyLevel(butcher, DIFF_HELL));
}

TEST_F(Poe2ItemLevelsTest, FistsReachTheTopStaff)
{
	const ItemData &top = AllItemsList[FindBase("Dreaming Quarterstaff")];
	ASSERT_EQ(top.iMinMLvl, 78);
	const auto share = [](int damage) { return (damage * BareFistStaffPercent + 50) / 100; };
	const std::pair<int, int> topDamage { share(top.iMinDam), share(top.iMaxDam) };
	// POE2's character level for gem level 18 is 78, the staff's; 19's is 84 and 20's 90, past it; 17's is 72, short of it.
	EXPECT_EQ(BareFistDamage(18), topDamage);
	EXPECT_EQ(BareFistDamage(19), topDamage);
	EXPECT_EQ(BareFistDamage(MaxGemLevel), topDamage);
	EXPECT_LT(BareFistDamage(17).second, topDamage.second);
	EXPECT_GT(BareFistDamage(18).second, BareFistDamage(10).second);
}

TEST_F(Poe2ItemLevelsTest, EveryVendorLevelHasACandidatePerCategory)
{
	// Every class of POE2 equipment the vendors sell has, at every area level they stock for, a base Griswold can
	// stock (drop level up to the area level) and one for his premium stock (from PremiumAreaLevelDivisor of it up).
	std::set<ItemType> types;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		const ItemData &data = AllItemsList[i];
		if (BaseMetaFor(static_cast<int>(i)) != nullptr && data.dropRate > 0 && EquipmentClassWeight(data.itype) > 0)
			types.insert(data.itype);
	}
	EXPECT_GE(types.size(), 10U);
	const int lowest = AreaLevelForFloor(1) + VendorAreaLevelBonus;
	ASSERT_GT(lowest, VendorAreaLevelBonus);
	for (int area = lowest; area <= MaxPoe2ItemLevel; area++) {
		for (const ItemType type : types) {
			bool stock = false;
			bool premium = false;
			for (size_t i = 0; i < AllItemsList.size(); i++) {
				const ItemData &data = AllItemsList[i];
				if (data.itype != type || data.dropRate <= 0 || !IsItemAvailable(static_cast<int>(i)) || BaseMetaFor(static_cast<int>(i)) == nullptr)
					continue;
				stock = stock || data.iMinMLvl <= area;
				premium = premium || (data.iMinMLvl >= area / PremiumAreaLevelDivisor && data.iMinMLvl <= area);
			}
			EXPECT_TRUE(stock) << "area " << area << ", item type " << static_cast<int>(type);
			EXPECT_TRUE(premium) << "area " << area << ", item type " << static_cast<int>(type);
		}
	}
}

TEST_F(Poe2ItemLevelsTest, HybridsSplitTheirDefenceLikePoe2)
{
	int armour = 0;
	int evasion = 0;
	int energyShield = 0;
	// Rogue Armour: POE2's 63 armour and 53 evasion, 54% armour.
	SplitDefense(DefenseType::ArmourEvasion, 116, armour, evasion, energyShield, DefenseSplit { 1, { 54, 0 } });
	EXPECT_EQ(armour, 63);
	EXPECT_EQ(evasion, 53);
	EXPECT_EQ(energyShield, 0);
	// Grand Regalia: 182 armour, 165 evasion and 50 energy shield (17 points), 50/45; the rounded percents give 54 shown.
	SplitDefense(DefenseType::All, 364, armour, evasion, energyShield, DefenseSplit { 2, { 50, 45 } });
	EXPECT_EQ(armour, 182);
	EXPECT_EQ(evasion, 164);
	EXPECT_EQ(energyShield, 18);
	// No split: evenly, as before.
	SplitDefense(DefenseType::ArmourEvasion, 116, armour, evasion, energyShield);
	EXPECT_EQ(armour, 58);
	EXPECT_EQ(evasion, 58);

	// A dropped Cleric Vestments shows POE2's 195 armour and 57 energy shield.
	DefensePoints base;
	DefensePoints bonus;
	SplitItemDefense(MakeItem("Cleric Vestments"), base, bonus);
	EXPECT_EQ(base.armour, 195);
	EXPECT_EQ(base.energyShield, 57);
	SplitItemDefense(MakeItem("Rogue Armour"), base, bonus);
	EXPECT_EQ(base.armour, 63);
	EXPECT_EQ(base.evasion, 53);

	// An item from before, whose defence is below what its base rolls now, splits evenly as it did.
	Item old = MakeItem("Rogue Armour");
	old._iAC = 17;
	SplitItemDefense(old, base, bonus);
	EXPECT_EQ(base.armour, 9);
	EXPECT_EQ(base.evasion, 8);
}

} // namespace
} // namespace devilution
