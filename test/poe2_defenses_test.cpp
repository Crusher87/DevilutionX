#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/defenses.hpp"
#include "poe2/passive_tree.hpp"
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

} // namespace
} // namespace devilution
