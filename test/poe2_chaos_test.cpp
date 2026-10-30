#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <sstream>
#include <string>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/defenses.hpp"
#include "poe2/item_stats.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/telemetry.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"
#include "utils/file_util.h"
#include "utils/paths.h"

namespace devilution {
namespace {

/** @brief Chaos damage (research/chaos-damage/plan-v2.md): the rules that cut across files, end to end. */
class Poe2ChaosTest : public ::testing::Test {
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
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadSpellData();
		LoadPlayerDataFiles();
		LoadPassiveTreeData();
		LoadModifierData();

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

	/** @brief Puts a ring with these chaos stats on the hero; slot picks the ring finger. */
	static Item &WearChaosRing(Player &player, inv_body_loc slot, int chaos, int maxChaos = 0)
	{
		Item &ring = player.InvBody[slot];
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iMiscId == IMISC_RING) {
				InitializeItem(ring, static_cast<_item_indexes>(i));
				break;
			}
		}
		ring._iStatFlag = true;
		ring._iIdentified = true;
		ring._iMagical = ITEM_QUALITY_MAGIC;
		if (chaos != 0)
			ApplyModEffect(ring, ModEffect::ChaosResistance, chaos, 0);
		if (maxChaos != 0)
			ApplyModEffect(ring, ModEffect::MaxChaosResistance, maxChaos, 0);
		return ring;
	}

	static void Recalculate(Player &player)
	{
		CalcPlrItemVals(player, false);
	}
};

TEST_F(Poe2ChaosTest, ChaosResistanceFamilyComesOnlyFromWhatNamesIt)
{
	const std::optional<uint8_t> family = FindModifierFamily("chaos_resistance");
	ASSERT_TRUE(family.has_value());
	const ModFamily &chaos = ModifierFamilies()[*family];
	EXPECT_EQ(chaos.effect, ModEffect::ChaosResistance);
	EXPECT_TRUE(chaos.enabled);
	EXPECT_FALSE(chaos.natural);
}

TEST_F(Poe2ChaosTest, ChaosResistanceStartsAtZero)
{
	Player &player = *MyPlayer;
	Recalculate(player);
	EXPECT_EQ(player.poe2.chaosResistance, 0);
	EXPECT_EQ(player.poe2.maxChaosResistance, MaxResistance);
}

TEST_F(Poe2ChaosTest, ChaosResistanceAddsUpFromGear)
{
	Player &player = *MyPlayer;
	WearChaosRing(player, INVLOC_RING_LEFT, 13);
	WearChaosRing(player, INVLOC_RING_RIGHT, 9);
	Recalculate(player);
	EXPECT_EQ(player.poe2.chaosResistance, 22);
	EXPECT_EQ(player.poe2.uncappedChaosResistance, 22);
	// The elements don't get it, and it doesn't get theirs.
	EXPECT_EQ(player._pFireResist, 0);
	EXPECT_EQ(player._pMagResist, 0);
	EXPECT_EQ(player._pLghtResist, 0);

	// Unidentified gear doesn't count, as for every other modifier.
	player.InvBody[INVLOC_RING_RIGHT]._iIdentified = false;
	Recalculate(player);
	EXPECT_EQ(player.poe2.chaosResistance, 13);
}

TEST_F(Poe2ChaosTest, AllResistancesLeaveChaosOut)
{
	Player &player = *MyPlayer;
	Item &ring = WearChaosRing(player, INVLOC_RING_LEFT, 0);
	ApplyModEffect(ring, ModEffect::AllResistances, 20, 0);
	Recalculate(player);
	EXPECT_EQ(player._pFireResist, 20);
	EXPECT_EQ(player.poe2.chaosResistance, 0);
}

TEST_F(Poe2ChaosTest, ChaosResistanceHasItsOwnCap)
{
	Player &player = *MyPlayer;
	WearChaosRing(player, INVLOC_RING_LEFT, 60);
	WearChaosRing(player, INVLOC_RING_RIGHT, 40);
	Recalculate(player);
	EXPECT_EQ(player.poe2.uncappedChaosResistance, 100);
	EXPECT_EQ(player.poe2.chaosResistance, MaxResistance);

	// Maximum chaos resistance raises the cap, up to the hard cap.
	WearChaosRing(player, INVLOC_RING_RIGHT, 40, 5);
	Recalculate(player);
	EXPECT_EQ(player.poe2.maxChaosResistance, MaxResistance + 5);
	EXPECT_EQ(player.poe2.chaosResistance, MaxResistance + 5);
	WearChaosRing(player, INVLOC_RING_RIGHT, 40, 50);
	Recalculate(player);
	EXPECT_EQ(player.poe2.maxChaosResistance, HardMaxResistance);
	EXPECT_EQ(player.poe2.chaosResistance, HardMaxResistance);
}

TEST_F(Poe2ChaosTest, ChaosResistanceHasAFloorButNoDepthPenalty)
{
	Player &player = *MyPlayer;
	player._pLvlVisited[13] = true;
	sgGameInitInfo.nDifficulty = DIFF_HELL;
	WearChaosRing(player, INVLOC_RING_LEFT, 10);
	Recalculate(player);
	EXPECT_LT(player._pFireResist, 0);
	EXPECT_EQ(player.poe2.chaosResistance, 10);

	WearChaosRing(player, INVLOC_RING_LEFT, -200);
	Recalculate(player);
	EXPECT_EQ(player.poe2.uncappedChaosResistance, -200);
	EXPECT_EQ(player.poe2.chaosResistance, MinPoe2Resistance);
	sgGameInitInfo.nDifficulty = DIFF_NORMAL;
}

TEST_F(Poe2ChaosTest, TelemetryLogsChaosResistance)
{
	paths::SetPrefPath(::testing::TempDir() + "poe2_chaos_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + DIRECTORY_SEPARATOR_STR);
	Player &player = *MyPlayer;
	player._pMaxHP = player._pMaxHPBase;
	player._pHitPoints = player._pHPBase;
	WearChaosRing(player, INVLOC_RING_LEFT, 17);
	Recalculate(player);
	EXPECT_NE(TelemetryBuildText(player).find("chaos 17% (max 75%)"), std::string::npos) << TelemetryBuildText(player);

	TelemetryInit();
	TelemetryGameStarted(player, false);
	TelemetryFlush();
	const std::string folder = paths::PrefPath() + "poe2_logs" + DIRECTORY_SEPARATOR_STR;
	std::string latest;
	for (const std::string &file : ListFiles(folder.c_str())) {
		if (file.ends_with(".events") && file > latest)
			latest = file;
	}
	std::ifstream stream(folder + latest);
	std::stringstream events;
	events << stream.rdbuf();
	EXPECT_NE(events.str().find("\"chaosRes\":17,\"maxChaosRes\":75"), std::string::npos) << events.str();
}

} // namespace
} // namespace devilution
