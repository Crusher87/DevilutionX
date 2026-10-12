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
	Wear(player, INVLOC_CHEST, "Robe", 10);
	Recalculate(player);
	ASSERT_EQ(player.poe2.maxEnergyShield, 40);

	const Fixed26_6 leftover = AbsorbWithEnergyShield(player, Fixed26_6::fromInt(50));
	EXPECT_EQ(leftover, Fixed26_6::fromInt(10));
	EXPECT_EQ(player.poe2.energyShield, 0);

	// Nothing for the 4 second delay, then 12.5% per second.
	for (int i = 0; i < EnergyShieldRechargeDelayTicks; i++)
		RechargeEnergyShield(player);
	EXPECT_EQ(player.poe2.energyShield, 0);
	for (int i = 0; i < 20; i++)
		RechargeEnergyShield(player);
	EXPECT_EQ(player.poe2.energyShield, 5 * 64);
	for (int i = 0; i < 20 * 10; i++)
		RechargeEnergyShield(player);
	EXPECT_EQ(player.poe2.energyShield, 40 * 64);
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

} // namespace
} // namespace devilution
