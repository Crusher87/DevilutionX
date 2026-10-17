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
#include "tables/misdat.h"
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
				item._iCharges = item._iMaxCharges; // as if filled from kills
				return item;
			}
		}
		ADD_FAILURE() << name;
		return player.SpdList[slot];
	}
};

TEST_F(Poe2CharmsTest, CharmsStartEmptyAndAreBeltItems)
{
	Player &player = *MyPlayer;
	for (const std::string_view name : { "Bloodstone Charm", "Sapphire Charm", "Iron Charm", "Ruby Charm", "Topaz Charm", "Amethyst Charm", "Stone Charm", "Golden Charm" }) {
		Item &charm = PutInBelt(player, 4, name);
		InitializeItem(charm, charm.IDidx);
		EXPECT_TRUE(IsCharm(charm)) << name;
		EXPECT_EQ(charm._iCharges, 0) << name;
		EXPECT_EQ(charm._iMaxCharges, MaxCharmCharges) << name;
		charm._iCharges = MaxCharmCharges;
		EXPECT_TRUE(charm.isUsable()) << name;
	}
}

TEST_F(Poe2CharmsTest, BloodstoneCharmCatchesLowLife)
{
	Player &player = *MyPlayer;
	Item &charm = PutInBelt(player, 4, "Bloodstone Charm");
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

TEST_F(Poe2CharmsTest, IronCharmGrantsGuardAfterAHeavyHit)
{
	Player &player = *MyPlayer;
	Item &charm = PutInBelt(player, 6, "Iron Charm");
	EXPECT_EQ(ApplyGuard(player, Fixed26_6::fromInt(5)).whole(), 5);
	EXPECT_EQ(player.poe2.guardTicks, 0);

	EXPECT_EQ(ApplyGuard(player, Fixed26_6::fromInt(20)).whole(), 20);
	EXPECT_EQ(player.poe2.guardTicks, GuardTicks);
	EXPECT_EQ(charm._iCharges, MaxCharmCharges - CharmChargesPerUse);
	EXPECT_EQ(ApplyGuard(player, Fixed26_6::fromInt(10)).whole(), 6);
}

TEST_F(Poe2CharmsTest, KillsRefillCharmsButTownDoesNot)
{
	Player &player = *MyPlayer;
	Item &charm = PutInBelt(player, 4, "Bloodstone Charm");
	charm._iCharges = 0;
	GainFlaskCharges(player, 2);
	EXPECT_EQ(charm._iCharges, 2);
	RefillFlasks(player);
	EXPECT_EQ(charm._iCharges, 2);
	RefillCharms(player);
	EXPECT_EQ(charm._iCharges, MaxCharmCharges);
}

TEST_F(Poe2CharmsTest, TheTownWellRefillsCharms)
{
	Player &player = *MyPlayer;
	Item &charm = PutInBelt(player, 4, "Bloodstone Charm");
	charm._iCharges = 1;
	player.plrlevel = 0;
	player.plrIsOnSetLevel = false;
	player.position.tile = { 40, 40 };
	RefillCharmsAtWell(player);
	EXPECT_EQ(charm._iCharges, 1) << "Away from the well";
	player.position.tile = { 59, 70 };
	RefillCharmsAtWell(player);
	EXPECT_EQ(charm._iCharges, MaxCharmCharges);
}

TEST_F(Poe2CharmsTest, RubyCharmRaisesFireResistanceWhenAFireHitLands)
{
	Player &player = *MyPlayer;
	Item &charm = PutInBelt(player, 4, "Ruby Charm");
	EXPECT_EQ(ResistanceAgainstHit(player, DamageType::Physical, 0), 0);
	EXPECT_EQ(ResistanceAgainstHit(player, DamageType::Lightning, 10), 10);
	EXPECT_EQ(charm._iCharges, MaxCharmCharges);

	EXPECT_EQ(ResistanceAgainstHit(player, DamageType::Fire, 10), 10 + ResistCharmBonus);
	EXPECT_EQ(charm._iCharges, MaxCharmCharges - CharmChargesPerUse);
	// The bonus lasts without spending more charges, and stops at the cap.
	EXPECT_EQ(ResistanceAgainstHit(player, DamageType::Fire, 70), MaxResistance);
	EXPECT_EQ(charm._iCharges, MaxCharmCharges - CharmChargesPerUse);
	// It doesn't hold up the life charm.
	EXPECT_EQ(player.poe2.charmCooldown, 0);

	for (int i = 0; i < ResistCharmTicks; i++)
		ProcessCharms(player);
	EXPECT_EQ(player.poe2.resistCharmTicks[0], 0);
}

TEST_F(Poe2CharmsTest, TopazAndAmethystCharmsAnswerTheirElements)
{
	Player &player = *MyPlayer;
	PutInBelt(player, 4, "Topaz Charm");
	PutInBelt(player, 5, "Amethyst Charm");
	EXPECT_EQ(ResistanceAgainstHit(player, DamageType::Fire, 0), 0);
	EXPECT_EQ(ResistanceAgainstHit(player, DamageType::Lightning, 0), ResistCharmBonus);
	EXPECT_EQ(ResistanceAgainstHit(player, DamageType::Magic, -20), -20 + ResistCharmBonus);
}

TEST_F(Poe2CharmsTest, StoneCharmBreaksAStun)
{
	Player &player = *MyPlayer;
	EXPECT_FALSE(CharmPreventsStun(player));
	Item &charm = PutInBelt(player, 4, "Stone Charm");
	EXPECT_TRUE(CharmPreventsStun(player));
	EXPECT_EQ(charm._iCharges, MaxCharmCharges - CharmChargesPerUse);
	EXPECT_EQ(player.poe2.stunImmuneTicks, StunImmuneTicks);
	// Immune for a while without spending more charges.
	EXPECT_TRUE(CharmPreventsStun(player));
	EXPECT_EQ(charm._iCharges, MaxCharmCharges - CharmChargesPerUse);
	for (int i = 0; i < StunImmuneTicks; i++)
		ProcessCharms(player);
	EXPECT_TRUE(CharmPreventsStun(player));
	EXPECT_EQ(charm._iCharges, 0);
	player.poe2.stunImmuneTicks = 0;
	EXPECT_FALSE(CharmPreventsStun(player));
}

TEST_F(Poe2CharmsTest, GoldenCharmSpendsAllItsCharges)
{
	EXPECT_EQ(CharmChargesPerUseFor(CharmEffect::Golden), MaxCharmCharges);
	EXPECT_EQ(CharmChargesPerUseFor(CharmEffect::Fire), CharmChargesPerUse);
}

} // namespace
} // namespace devilution
