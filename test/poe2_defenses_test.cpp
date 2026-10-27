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
#include "poe2/currency.hpp"
#include "poe2/defenses.hpp"
#include "poe2/loot.hpp"
#include "poe2/monster_levels.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/unique_effects.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2DefensesTest : public ::testing::Test {
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
		LoadPassiveTreeData();

		Player &player = *MyPlayer;
		player._pClass = HeroClass::Sorcerer;
		player.setCharacterLevel(10);
		player._pBaseStr = player._pStrength = 100;
		player._pBaseMag = player._pMagic = 100;
		player._pBaseDex = player._pDexterity = 50;
		player._pBaseVit = player._pVitality = 25;
		player._pMaxHPBase = player._pHPBase = Fixed26_6::fromInt(100);
		player._pMaxManaBase = player._pManaBase = Fixed26_6::fromInt(20);
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		LoadPassiveTreeData();
		UnloadModArchives();
	}

	static void Wear(Player &player, inv_body_loc slot, std::string_view baseName, int ac)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == baseName) {
				Item &item = player.InvBody[slot];
				InitializeItem(item, static_cast<_item_indexes>(i));
				item._iAC = ac;
				item._iStatFlag = true;
				return;
			}
		}
		FAIL() << baseName;
	}

	static void Recalculate(Player &player)
	{
		// Keeps CalcPlrInv from loading art for the new armor graphic.
		CalcPlrItemVals(player, false);
	}

	/** Wears a body armour with quality, which ApplyBaseProperties adds as a drop or a rebuild does, and local % increases. */
	static void WearWithQuality(Player &player, std::string_view baseName, int ac, int quality, int increased = 0)
	{
		Wear(player, INVLOC_CHEST, baseName, ac);
		Item &item = player.InvBody[INVLOC_CHEST];
		SetItemQuality(item, quality);
		ApplyBaseProperties(item);
		item._iPLAC = static_cast<int16_t>(item._iPLAC + increased);
		Recalculate(player);
	}
};

TEST_F(Poe2DefensesTest, LightStunFollowsPoe2)
{
	// POE2: twice the hit's share of the target's life, up to 100%, and nothing at 20% or less.
	EXPECT_EQ(LightStunChance(5, 100), 0);
	EXPECT_EQ(LightStunChance(10, 100), 0);
	EXPECT_EQ(LightStunChance(11, 100), 22);
	EXPECT_EQ(LightStunChance(25, 100), 50);
	EXPECT_EQ(LightStunChance(50, 100), 100);
	EXPECT_EQ(LightStunChance(500, 100), 100);
	EXPECT_EQ(LightStunChance(0, 100), 0);
	EXPECT_EQ(LightStunChance(10, 0), 0);
	// Works in raw fixed-point units as well: 30 damage against 200 life is a 30% chance.
	EXPECT_EQ(LightStunChance(int64_t { 30 } << 6, int64_t { 200 } << 6), 30);
}

TEST_F(Poe2DefensesTest, AccuracyFollowsPoe2)
{
	// POE2: 125% x accuracy / (accuracy + 0.3 x evasion), from 5% to 100%.
	EXPECT_EQ(Poe2HitChance(1000, 1000), 96);
	EXPECT_EQ(Poe2HitChance(1, 100000), 5);
	EXPECT_EQ(Poe2HitChance(100000, 0), 100);
	// Arrows lose accuracy past 2 tiles, down to a tenth at 9.
	EXPECT_EQ(AccuracyAtDistancePercent(1), 100);
	EXPECT_EQ(AccuracyAtDistancePercent(2), 100);
	EXPECT_EQ(AccuracyAtDistancePercent(5), 62);
	EXPECT_EQ(AccuracyAtDistancePercent(9), 10);
	EXPECT_EQ(AccuracyAtDistancePercent(20), 10);

	// 6 a level and 6 a point of Dexterity; an ordinary hero hits an ordinary monster of their level about 9 in 10.
	Player &player = *MyPlayer;
	player.setCharacterLevel(1);
	player._pDexterity = 20;
	player._pIBonusToHit = 0;
	EXPECT_EQ(AccuracyRating(player), 126);
	EXPECT_EQ(TypicalHitChance(player), 92);
	player.setCharacterLevel(10);
	player._pDexterity = 25;
	EXPECT_EQ(TypicalHitChance(player), 92);
}

TEST_F(Poe2DefensesTest, BasesGrantTheirOwnDefense)
{
	Item item;
	for (const auto &[name, type] : std::initializer_list<std::pair<std::string_view, DefenseType>> {
	         { "Robe", DefenseType::EnergyShield },
	         { "Cap", DefenseType::EnergyShield },
	         { "Leather Armor", DefenseType::Evasion },
	         { "Chain Mail", DefenseType::ArmourEvasion },
	         { "Full Plate Mail", DefenseType::Armour },
	     }) {
		bool found = false;
		for (size_t i = 0; i < AllItemsList.size() && !found; i++) {
			if (AllItemsList[i].iName != name)
				continue;
			InitializeItem(item, static_cast<_item_indexes>(i));
			EXPECT_EQ(ItemDefenseType(item), type) << name;
			found = true;
		}
		EXPECT_TRUE(found) << name;
	}
}

TEST_F(Poe2DefensesTest, GearSplitsIntoArmourEvasionAndEnergyShield)
{
	Player &player = *MyPlayer;
	Wear(player, INVLOC_CHEST, "Robe", 6);
	Wear(player, INVLOC_HEAD, "Skull Cap", 4);
	Wear(player, INVLOC_HAND_RIGHT, "Small Shield", 8);
	Recalculate(player);

	EXPECT_EQ(player.poe2.maxEnergyShield, 6 * EnergyShieldPerDefense);
	EXPECT_EQ(player.poe2.energyShield, player.poe2.maxEnergyShield * 64) << "new energy shield starts full";
	EXPECT_EQ(player.poe2.evasion, 4 + 4);
	EXPECT_EQ(EvasionRating(player), 8 + 50 / 5);
	EXPECT_EQ(ArmourRating(player), 4) << "half the shield is armour";
	EXPECT_EQ(HitDefense(player), EvasionRating(player));
}

TEST_F(Poe2DefensesTest, ArmourReducesHitsLikePoe2)
{
	EXPECT_EQ(ArmourReductionPercent(0, Fixed26_6::fromInt(10)), 0);
	// 100 armour against a 10 damage hit: 100 / (100 + 100) = 50%.
	EXPECT_EQ(ArmourReductionPercent(100, Fixed26_6::fromInt(10)), 50);
	// Big hits get through armour more easily.
	EXPECT_EQ(ArmourReductionPercent(100, Fixed26_6::fromInt(90)), 10);
	EXPECT_EQ(ArmourReductionPercent(100000, Fixed26_6::fromInt(1)), MaxArmourReductionPercent);
}

TEST_F(Poe2DefensesTest, EnergyShieldSoaksDamageThenRecharges)
{
	Player &player = *MyPlayer;
	Wear(player, INVLOC_CHEST, "Robe", 20);
	Recalculate(player);
	ASSERT_EQ(player.poe2.maxEnergyShield, 20 * EnergyShieldPerDefense);
	ASSERT_EQ(player.poe2.maxEnergyShield, 60);

	const Fixed26_6 leftover = AbsorbWithEnergyShield(player, Fixed26_6::fromInt(70));
	EXPECT_EQ(leftover, Fixed26_6::fromInt(10));
	EXPECT_EQ(player.poe2.energyShield, 0);

	// Nothing for the 4 second delay, then 12.5% per second.
	for (int i = 0; i < EnergyShieldRechargeDelayTicks; i++)
		RechargeEnergyShield(player);
	EXPECT_EQ(player.poe2.energyShield, 0);
	for (int i = 0; i < 20; i++)
		RechargeEnergyShield(player);
	EXPECT_EQ(player.poe2.energyShield, 60 * 64 / 8);
	for (int i = 0; i < 20 * 10; i++)
		RechargeEnergyShield(player);
	EXPECT_EQ(player.poe2.energyShield, 60 * 64);
}

TEST_F(Poe2DefensesTest, ResistancesDropInDeeperAreas)
{
	Player &player = *MyPlayer;
	sgGameInitInfo.nDifficulty = DIFF_NORMAL;
	EXPECT_EQ(ResistancePenalty(player), 0);
	player._pLvlVisited[4] = true;
	EXPECT_EQ(ResistancePenalty(player), 0);
	player._pLvlVisited[5] = true;
	EXPECT_EQ(ResistancePenalty(player), ResistancePenaltyPerArea);
	player._pLvlVisited[13] = true;
	EXPECT_EQ(ResistancePenalty(player), 3 * ResistancePenaltyPerArea);
	player._pLvlVisited[24] = true;
	EXPECT_EQ(ResistancePenalty(player), 3 * ResistancePenaltyPerArea);

	sgGameInitInfo.nDifficulty = DIFF_HELL;
	EXPECT_EQ(ResistancePenalty(player), (3 * ResistancePenaltyPerArea) + (2 * ResistancePenaltyPerDifficulty));
	sgGameInitInfo.nDifficulty = DIFF_NORMAL;
}

TEST_F(Poe2DefensesTest, ResistancesCanGoNegative)
{
	Player &player = *MyPlayer;
	sgGameInitInfo.nDifficulty = DIFF_NORMAL;
	player._pLvlVisited[13] = true;
	Recalculate(player);
	EXPECT_EQ(player._pFireResist, -30);
	EXPECT_EQ(player._pLghtResist, -30);
	EXPECT_EQ(player._pMagResist, -30);
	EXPECT_EQ(Poe2Resistance(200, 30, MaxResistance), MaxResistance);
	EXPECT_EQ(Poe2Resistance(-100, 30, MaxResistance), MinPoe2Resistance);

	gbIsPoe2 = false;
	Recalculate(player);
	EXPECT_EQ(player._pFireResist, 0);
}

TEST_F(Poe2DefensesTest, VanillaModeKeepsArmorClass)
{
	gbIsPoe2 = false;
	Player &player = *MyPlayer;
	Wear(player, INVLOC_CHEST, "Robe", 6);
	Recalculate(player);
	EXPECT_EQ(player.poe2.maxEnergyShield, 0);
	EXPECT_EQ(HitDefense(player), player.GetArmor());
	EXPECT_EQ(AbsorbWithEnergyShield(player, Fixed26_6::fromInt(5)), Fixed26_6::fromInt(5));
}

TEST_F(Poe2DefensesTest, HybridBodyArmoursSplitTheirDefense)
{
	Player &player = *MyPlayer;
	Wear(player, INVLOC_CHEST, "Cleric Vestments", 50);
	Recalculate(player);
	EXPECT_EQ(ArmourRating(player), 25);
	EXPECT_EQ(player.poe2.maxEnergyShield, 25 * EnergyShieldPerDefense);

	Wear(player, INVLOC_CHEST, "Monastic Garb", 61);
	Recalculate(player);
	EXPECT_EQ(ArmourRating(player), 0);
	EXPECT_EQ(player.poe2.evasion, 31) << "evasion takes the odd point";
	EXPECT_EQ(player.poe2.maxEnergyShield, 30 * EnergyShieldPerDefense);

	Wear(player, INVLOC_CHEST, "Grand Regalia", 76);
	Recalculate(player);
	EXPECT_EQ(ArmourRating(player), 26);
	EXPECT_EQ(player.poe2.evasion, 25);
	EXPECT_EQ(player.poe2.maxEnergyShield, 25 * EnergyShieldPerDefense);
}

TEST_F(Poe2DefensesTest, IdentifyingNeverChangesDefence)
{
	// Curtis 2026-10-22: Fate Pelt (Pilgrim Vestments) gave 15 energy shield unidentified, then armour 7 and
	// energy shield 12 once identified. Its 2 flat armour and 20% increased defence count either way.
	Player &player = *MyPlayer;
	Wear(player, INVLOC_CHEST, "Pilgrim Vestments", 10);
	Item &vestments = player.InvBody[INVLOC_CHEST];
	vestments._iMagical = ITEM_QUALITY_MAGIC;
	vestments._iPoe2Stats[static_cast<size_t>(Poe2ItemStat::FlatArmour)] = 2;
	vestments._iPLAC = 20;
	vestments._iIdentified = false;
	Recalculate(player);
	const int armour = ArmourRating(player);
	const int energyShield = player.poe2.maxEnergyShield;
	EXPECT_EQ(armour, 7);
	// Its 12 energy shield is raised by the 20% too (Curtis 2026-10-23).
	EXPECT_EQ(energyShield, 14);

	vestments._iIdentified = true;
	Recalculate(player);
	EXPECT_EQ(ArmourRating(player), armour);
	EXPECT_EQ(player.poe2.maxEnergyShield, energyShield);
}

TEST_F(Poe2DefensesTest, QualityRaisesEnergyShield)
{
	// Curtis 2026-10-23, picture 10: an Armourer's Scrap took a robe to 10% quality and its 45 energy shield stayed 45.
	// Its 14 points of defence gave 45 energy shield from 2% quality to 14%, a whole point (3 energy shield) at a time.
	Player &player = *MyPlayer;
	WearWithQuality(player, "Hexer's Robe", 14, 0);
	EXPECT_EQ(player.poe2.maxEnergyShield, 42);
	WearWithQuality(player, "Hexer's Robe", 14, 8);
	EXPECT_EQ(player.poe2.maxEnergyShield, 45);
	WearWithQuality(player, "Hexer's Robe", 14, 10);
	EXPECT_EQ(player.poe2.maxEnergyShield, 46);
	WearWithQuality(player, "Hexer's Robe", 14, 20);
	EXPECT_EQ(player.poe2.maxEnergyShield, 50);
}

TEST_F(Poe2DefensesTest, QualityMultipliesLocalDefence)
{
	// POE2: quality and the item's local % increases multiply. 50 with 50% increased and 20% quality is 90, not 85.
	Player &player = *MyPlayer;
	WearWithQuality(player, "Full Plate Mail", 50, 20, 50);
	EXPECT_EQ(ArmourRating(player), 90);
	WearWithQuality(player, "Leather Armor", 50, 20, 50);
	EXPECT_EQ(player.poe2.evasion, 90);
	WearWithQuality(player, "Robe", 10, 20, 50);
	EXPECT_EQ(player.poe2.maxEnergyShield, 54);
}

TEST_F(Poe2DefensesTest, QualityRaisesEachDefenceOfAHybrid)
{
	// A small increase on a hybrid went all to one kind: 10% quality on 10 points gave the vestments' energy shield nothing.
	Player &player = *MyPlayer;
	WearWithQuality(player, "Pilgrim Vestments", 10, 10);
	EXPECT_EQ(ArmourRating(player), 6);
	EXPECT_EQ(player.poe2.maxEnergyShield, 17);

	WearWithQuality(player, "Hermit Garb", 10, 10);
	EXPECT_EQ(player.poe2.evasion, 6);
	EXPECT_EQ(player.poe2.maxEnergyShield, 17);

	WearWithQuality(player, "Chain Mail Vest", 10, 10);
	EXPECT_EQ(ArmourRating(player), 6);
	EXPECT_EQ(player.poe2.evasion, 6);

	WearWithQuality(player, "Grand Regalia", 12, 20);
	EXPECT_EQ(ArmourRating(player), 5);
	EXPECT_EQ(player.poe2.evasion, 5);
	EXPECT_EQ(player.poe2.maxEnergyShield, 14);
}

TEST_F(Poe2DefensesTest, ArmourersScrapRaisesADropsEnergyShield)
{
	// Picture 10 again, the way it happened: a dropped robe of item level 7 with 8% quality, then a scrap.
	Player &player = *MyPlayer;
	_item_indexes base = IDI_GOLD;
	for (size_t i = 0; i < AllItemsList.size() && base == IDI_GOLD; i++) {
		if (AllItemsList[i].iName == "Hexer's Robe")
			base = static_cast<_item_indexes>(i);
	}
	Item &robe = player.InvBody[INVLOC_CHEST];
	robe = {};
	SetLootRarity(robe, ITEM_QUALITY_NORMAL);
	SetPoe2ItemLevel(robe, 7);
	SetItemQuality(robe, 8);
	SetRndSeed(7);
	SetupAllItems(player, robe, base, AdvanceRndSeed(), 14, 1, /*onlygood=*/true, /*pregen=*/false, 0, /*forceNotUnique=*/true);
	robe._iStatFlag = true;
	Recalculate(player);
	const int before = player.poe2.maxEnergyShield;

	ASSERT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Scrap, player, robe));
	EXPECT_EQ(ItemQuality(robe), 10);
	robe._iStatFlag = true;
	Recalculate(player);
	EXPECT_EQ(player.poe2.maxEnergyShield, (robe._iAC * EnergyShieldPerDefense * 110 + 50) / 100);
	EXPECT_GT(player.poe2.maxEnergyShield, before);
}

TEST_F(Poe2DefensesTest, ArmourersScrapOnStartingGear)
{
	// Starting gear is rebuilt from its base alone, which dropped the quality an Armourer's Scrap gave it.
	Player &player = *MyPlayer;
	Wear(player, INVLOC_CHEST, "Robe", 10);
	Item &robe = player.InvBody[INVLOC_CHEST];
	for (int i = 0; i < 5; i++)
		ASSERT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Scrap, player, robe));
	EXPECT_EQ(ItemQuality(robe), 10);
	EXPECT_EQ(robe._iAC, 10);
	robe._iStatFlag = true;
	Recalculate(player);
	EXPECT_EQ(player.poe2.maxEnergyShield, 33);
}

TEST_F(Poe2DefensesTest, WhetstoneQualityRaisesWeaponDamage)
{
	// The same for a Blacksmith's Whetstone on a starting weapon.
	Player &player = *MyPlayer;
	Wear(player, INVLOC_HAND_LEFT, "Short Sword", 0);
	Recalculate(player);
	const int plain = player._pIBonusDam;
	Item &sword = player.InvBody[INVLOC_HAND_LEFT];
	for (int i = 0; i < 5; i++)
		ASSERT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Whetstone, player, sword));
	EXPECT_EQ(ItemQuality(sword), 10);
	sword._iStatFlag = true;
	Recalculate(player);
	EXPECT_EQ(player._pIBonusDam, plain + 10);

	// POE2: quality and local % increases multiply, so 50% increased with 10% quality is 65%, not 60%.
	sword._iPLDam = static_cast<int16_t>(sword._iPLDam + 50);
	Recalculate(player);
	EXPECT_EQ(player._pIBonusDam, plain + 65);

	// Quality isn't a hidden modifier: an unidentified sword has it, though not its hidden 50%.
	sword._iMagical = ITEM_QUALITY_MAGIC;
	sword._iIdentified = false;
	Recalculate(player);
	EXPECT_EQ(player._pIBonusDam, plain + 10);
}

TEST_F(Poe2DefensesTest, TopBodyArmourBasesCarryImplicits)
{
	Player &player = *MyPlayer;
	Wear(player, INVLOC_CHEST, "Rusted Cuirass", 6);
	Recalculate(player);
	const int baseLife = player._pMaxHP.raw() / 64;
	const int baseMagicRes = player._pMagResist;

	Wear(player, INVLOC_CHEST, "Heroic Armour", 70);
	Recalculate(player);
	EXPECT_EQ(player._pMaxHP.raw() / 64, baseLife + 20);

	Wear(player, INVLOC_CHEST, "Devout Garb", 70);
	Recalculate(player);
	EXPECT_EQ(player._pMagResist, baseMagicRes + 20);

	Wear(player, INVLOC_CHEST, "Armoured Vest", 60);
	Recalculate(player);
	EXPECT_EQ(player.poe2.evasion, 60 + 60 * 15 / 100);
}

/** Monsters' accuracy against the hero (plan v2 step 4): needs the monster and area level tables. */
class Poe2MonsterAccuracyTest : public Poe2DefensesTest {
public:
	void SetUp() override
	{
		Poe2DefensesTest::SetUp();
		sgGameInitInfo.nDifficulty = DIFF_NORMAL;
		LoadMonsterData();
		// Also loads the monster level tables.
		LoadAreaLevels();
		GoTo(19, DTYPE_NEST);
	}

	void TearDown() override
	{
		sgGameInitInfo.nDifficulty = DIFF_NORMAL;
		currlevel = 0;
		leveltype = DTYPE_TOWN;
		gbIsPoe2 = false;
		LoadAreaLevels();
		Poe2DefensesTest::TearDown();
	}

	static void GoTo(int floor, dungeon_type type)
	{
		setlevel = false;
		setlvlnum = SL_NONE;
		currlevel = static_cast<uint8_t>(floor);
		leveltype = type;
	}

	/** A monster on the POE2 tables, as InitMonster leaves it. */
	static Monster MakeMonster(_monster_id type, UniqueMonsterType unique = UniqueMonsterType::None)
	{
		LevelMonsterTypes[0].type = type;
		Monster monster {};
		monster.levelType = 0;
		monster.uniqueType = unique;
		monster.flags = MFLAG_POE2_TABLES;
		return monster;
	}

	/** The to-hit whose ratio to the type's ref is 100%. */
	static int RefToHit(_monster_id type)
	{
		return (MonsterTypeRow(type).toHitRef10 + 5) / 10;
	}
};

TEST_F(Poe2MonsterAccuracyTest, EvasionMakesMonstersMiss)
{
	// Hive 3 on Nightmare: area level 38, accuracy 274 for a type at its ref.
	sgGameInitInfo.nDifficulty = DIFF_NIGHTMARE;
	ASSERT_EQ(CurrentAreaLevel(), 38);
	const Monster monster = MakeMonster(MT_NZOMBIE);
	ASSERT_TRUE(UsesPoe2MonsterAccuracy(monster));
	EXPECT_EQ(MonsterLevelAccuracy(38), 274);
	// RefToHit rounds the type's tenths-of-a-point ref to a whole to-hit, which can move the ratio by 2%.
	EXPECT_NEAR(MonsterAccuracyRating(monster, RefToHit(MT_NZOMBIE)), 274, 6);
	// 274 against an evasion rating of 840: POE2's 125 x acc / (acc + 0.3 x eva) = 65%.
	EXPECT_NEAR(MonsterHitChance(monster, RefToHit(MT_NZOMBIE), 840), 65, 2);
	EXPECT_EQ(MonsterHitChance(monster, RefToHit(MT_NZOMBIE), 0), 100);
	// No floor but POE2's 5%.
	EXPECT_EQ(MonsterHitChance(monster, RefToHit(MT_NZOMBIE), 1000000), 5);
}

TEST_F(Poe2MonsterAccuracyTest, HellDoesNotRaiseAccuracy)
{
	EXPECT_EQ(MonsterDifficultyToHitBonus(DIFF_NORMAL), 0);
	EXPECT_EQ(MonsterDifficultyToHitBonus(DIFF_NIGHTMARE), 85);
	EXPECT_EQ(MonsterDifficultyToHitBonus(DIFF_HELL), 120);
	sgGameInitInfo.nDifficulty = DIFF_HELL;
	const Monster monster = MakeMonster(MT_NZOMBIE);
	// What MonsterAttackPlayer and PlayerMHit pass on: Hell's +120 comes off, the area level carries the difficulty.
	const int stripped = static_cast<int>(monster.toHit(DIFF_HELL)) - MonsterDifficultyToHitBonus(DIFF_HELL);
	EXPECT_EQ(stripped, static_cast<int>(monster.toHit(DIFF_NORMAL)));
	EXPECT_EQ(MonsterAccuracyRating(monster, stripped), MonsterAccuracyRating(monster, static_cast<int>(monster.toHit(DIFF_NORMAL))));
	// Hell's accuracy is the table's at Hell's area level (23 + 30), times the zombie's own ratio.
	ASSERT_EQ(CurrentAreaLevel(), 53);
	const int ref = MonsterTypeRow(MT_NZOMBIE).toHitRef10;
	const int64_t permille = std::clamp<int64_t>(int64_t { stripped } * 10000 / ref, 300, 2500);
	EXPECT_EQ(MonsterAccuracyRating(monster, stripped), static_cast<int>(MonsterLevelAccuracy(53) * permille / 1000));
}

TEST_F(Poe2MonsterAccuracyTest, UniqueToHitRatioClamped)
{
	// Bonehead Keenaxe: a custom to-hit of 100 against his type's ref of 25 is 400%, held to 250%.
	const Monster bonehead = MakeMonster(MT_TSKELAX, static_cast<UniqueMonsterType>(13));
	ASSERT_EQ(bonehead.toHit(DIFF_NORMAL), 100U);
	const int level = Poe2MonsterLevel(bonehead, DIFF_NORMAL);
	EXPECT_EQ(level, CurrentAreaLevel() + 2);
	EXPECT_EQ(MonsterAccuracyRating(bonehead, 100), MonsterLevelAccuracy(level) * 2500 / 1000);
	// And no lower than 30%.
	EXPECT_EQ(MonsterAccuracyRating(bonehead, 0), MonsterLevelAccuracy(level) * 300 / 1000);
}

TEST_F(Poe2MonsterAccuracyTest, ArrowFalloffUsesTiles)
{
	const Monster archer = MakeMonster(MT_NZOMBIE);
	const int toHit = RefToHit(MT_NZOMBIE);
	const int point = MonsterHitChance(archer, toHit, 840, AccuracyAtDistancePercent(2));
	EXPECT_EQ(point, MonsterHitChance(archer, toHit, 840));
	EXPECT_LT(MonsterHitChance(archer, toHit, 840, AccuracyAtDistancePercent(5)), point);
	// At 9 tiles the arrow keeps a tenth of its accuracy.
	const int far = MonsterHitChance(archer, toHit, 840, AccuracyAtDistancePercent(9));
	EXPECT_EQ(far, Poe2HitChance(MonsterAccuracyRating(archer, toHit) / 10, 840));
	EXPECT_LT(far, point / 3);
}

TEST_F(Poe2MonsterAccuracyTest, NoLevelTermInBlock)
{
	Player &player = *MyPlayer;
	player._pBlockFlag = true;
	player.setCharacterLevel(1);
	GoTo(1, DTYPE_CATHEDRAL);
	const Monster weak = MakeMonster(MT_NZOMBIE);
	const int lowLevel = BlockChanceAgainstMonster(player, &weak, true);
	EXPECT_EQ(lowLevel, player.GetBlockChance(false));
	EXPECT_EQ(BlockChanceAgainstMonster(player, &weak, false), lowLevel);
	EXPECT_EQ(BlockChanceAgainstMonster(player, nullptr, false), lowLevel);

	// Character level 40 against a level 60+ monster: the same block.
	player.setCharacterLevel(40);
	sgGameInitInfo.nDifficulty = DIFF_HELL;
	GoTo(16, DTYPE_HELL);
	const Monster strong = MakeMonster(MT_NZOMBIE);
	ASSERT_GE(static_cast<int>(strong.level(DIFF_HELL)), 60);
	EXPECT_EQ(BlockChanceAgainstMonster(player, &strong, true), lowLevel);
	EXPECT_EQ(BlockChanceAgainstMonster(player, &strong, false), lowLevel);
}

TEST_F(Poe2MonsterAccuracyTest, SpellsIgnoreEvasionUnlessUnique)
{
	// A monster's spell always hits in POE2 (PlayerMHit: 100 - SpellEvasion); evasion alone takes nothing off it.
	Player &player = *MyPlayer;
	player.poe2.evasion = 5000;
	EXPECT_EQ(SpellEvasion(player), 0);
}

TEST_F(Poe2MonsterAccuracyTest, MinionsAndOldMonstersKeepDiabloToHit)
{
	Monster monster = MakeMonster(MT_NZOMBIE);
	EXPECT_TRUE(UsesPoe2MonsterAccuracy(monster));
	monster.flags = 0;
	EXPECT_FALSE(UsesPoe2MonsterAccuracy(monster));
	monster.flags = MFLAG_POE2_TABLES | MFLAG_GOLEM;
	EXPECT_FALSE(UsesPoe2MonsterAccuracy(monster));
}

TEST_F(Poe2MonsterAccuracyTest, DiabloModeUnchanged)
{
	gbIsPoe2 = false;
	Player &player = *MyPlayer;
	player.setCharacterLevel(10);
	const Monster monster = MakeMonster(MT_NZOMBIE);
	EXPECT_FALSE(UsesPoe2MonsterAccuracy(monster));
	const int level = static_cast<int>(monster.level(DIFF_NORMAL));
	EXPECT_EQ(BlockChanceAgainstMonster(player, &monster, true), player.GetBlockChance() - level * 2);
	EXPECT_EQ(BlockChanceAgainstMonster(player, &monster, false), player.GetBlockChance(false) - (level - 10) * 2);
	EXPECT_EQ(BlockChanceAgainstMonster(player, nullptr, false), player.GetBlockChance(false));
	EXPECT_EQ(monster.toHit(DIFF_HELL), monster.toHit(DIFF_NORMAL) + 120);
}

} // namespace
} // namespace devilution
