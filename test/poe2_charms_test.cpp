#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "missiles.h"
#include "player.h"
#include "poe2/charms.hpp"
#include "poe2/defenses.hpp"
#include "poe2/flasks.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2CharmsTest : public ::testing::Test {
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
		LoadMissileData();
		Missiles.clear();

		Player &player = *MyPlayer;
		player._pMaxHP = player._pMaxHPBase = Fixed26_6::fromInt(100);
		player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(100);
		player._pMaxMana = player._pMaxManaBase = Fixed26_6::fromInt(100);
		player._pMana = player._pManaBase = Fixed26_6::fromInt(100);
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		Missiles.clear();
		UnloadModArchives();
		LoadMissileData();
	}

	static Item &PutInBelt(Player &player, size_t slot, std::string_view name)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == name) {
				Item &item = player.SpdList[slot];
				InitializeItem(item, static_cast<_item_indexes>(i));
				return item;
			}
		}
		ADD_FAILURE() << name;
		return player.SpdList[slot];
	}
};

TEST_F(Poe2CharmsTest, CharmsStartFullAndAreBeltItems)
{
	Player &player = *MyPlayer;
	for (const std::string_view name : { "Ruby Charm", "Sapphire Charm", "Stone Charm" }) {
		Item &charm = PutInBelt(player, 4, name);
		EXPECT_TRUE(IsCharm(charm)) << name;
		EXPECT_EQ(charm._iCharges, MaxCharmCharges) << name;
		EXPECT_TRUE(charm.isUsable()) << name;
	}
}

TEST_F(Poe2CharmsTest, RubyCharmCatchesLowLife)
{
	Player &player = *MyPlayer;
	Item &charm = PutInBelt(player, 4, "Ruby Charm");
	player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(50);
	ProcessCharms(player);
	EXPECT_EQ(player._pHitPoints.whole(), 50);
	EXPECT_EQ(charm._iCharges, MaxCharmCharges);

	player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(20);
	ProcessCharms(player);
	EXPECT_EQ(player._pHitPoints.whole(), 50);
	EXPECT_EQ(charm._iCharges, MaxCharmCharges - CharmChargesPerUse);

	// Waits out the cooldown before going off again.
	player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(20);
	ProcessCharms(player);
	EXPECT_EQ(player._pHitPoints.whole(), 20);
}

TEST_F(Poe2CharmsTest, SapphireCharmCatchesLowMana)
{
	Player &player = *MyPlayer;
	Item &charm = PutInBelt(player, 5, "Sapphire Charm");
	player._pMana = player._pManaBase = Fixed26_6::fromInt(10);
	ProcessCharms(player);
	EXPECT_EQ(player._pMana.whole(), 50);
	EXPECT_EQ(charm._iCharges, MaxCharmCharges - CharmChargesPerUse);
}

TEST_F(Poe2CharmsTest, StoneCharmGrantsGuardAfterAHeavyHit)
{
	Player &player = *MyPlayer;
	Item &charm = PutInBelt(player, 6, "Stone Charm");
	EXPECT_EQ(ApplyGuard(player, Fixed26_6::fromInt(5)).whole(), 5);
	EXPECT_EQ(player.poe2.guardTicks, 0);

	EXPECT_EQ(ApplyGuard(player, Fixed26_6::fromInt(20)).whole(), 20);
	EXPECT_EQ(player.poe2.guardTicks, GuardTicks);
	EXPECT_EQ(charm._iCharges, MaxCharmCharges - CharmChargesPerUse);
	EXPECT_EQ(ApplyGuard(player, Fixed26_6::fromInt(10)).whole(), 6);
}

TEST_F(Poe2CharmsTest, KillsAndTownRefillCharms)
{
	Player &player = *MyPlayer;
	Item &charm = PutInBelt(player, 4, "Ruby Charm");
	charm._iCharges = 0;
	GainFlaskCharges(player, 2);
	EXPECT_EQ(charm._iCharges, 2);
	RefillFlasks(player);
	EXPECT_EQ(charm._iCharges, MaxCharmCharges);
}

} // namespace
} // namespace devilution
