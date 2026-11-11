#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/flasks.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2FlasksTest : public ::testing::Test {
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
	}

	static _item_indexes RejuvenationPotion()
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iMiscId == IMISC_REJUV)
				return static_cast<_item_indexes>(i);
		}
		return IDI_NONE;
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		UnloadModArchives();
	}
};

TEST_F(Poe2FlasksTest, PotionsInTheFirstTwoSlotsAreFlasks)
{
	Player &player = *MyPlayer;
	InitializeItem(player.SpdList[0], IDI_HEAL);
	InitializeItem(player.SpdList[1], IDI_BOOK1);
	InitializeItem(player.SpdList[4], IDI_HEAL);
	EXPECT_TRUE(IsFlaskSlot(player, 0));
	EXPECT_FALSE(IsFlaskSlot(player, 1)) << "only potions are flasks";
	EXPECT_FALSE(IsFlaskSlot(player, 2)) << "empty slot";
	EXPECT_FALSE(IsFlaskSlot(player, 4)) << "slots 3-8 are for charms";
	EXPECT_TRUE(BeltSlotTakes(player.SpdList[0], 1));
	EXPECT_FALSE(BeltSlotTakes(player.SpdList[0], 2));

	gbIsPoe2 = false;
	EXPECT_FALSE(IsFlaskSlot(player, 0));
}

TEST_F(Poe2FlasksTest, DrinkingUsesChargesNotThePotion)
{
	Player &player = *MyPlayer;
	InitializeItem(player.SpdList[0], IDI_MANA);
	player._pMaxMana = Fixed26_6::fromInt(100);
	ASSERT_EQ(player.poe2.flaskCharges[0], MaxFlaskCharges);

	for (int use = 0; use < MaxFlaskCharges / FlaskChargesPerUse; use++) {
		player._pMana = {};
		ASSERT_TRUE(UseFlask(player, 0)) << "use " << use;
	}
	EXPECT_EQ(player.poe2.flaskCharges[0], 0);
	EXPECT_FALSE(player.SpdList[0].isEmpty());
	EXPECT_FALSE(CanUseFlask(player, 0));
	EXPECT_FALSE(UseFlask(player, 0));
}

TEST_F(Poe2FlasksTest, FlasksDontDrinkWhenFull)
{
	Player &player = *MyPlayer;
	InitializeItem(player.SpdList[0], IDI_HEAL);
	player._pMaxHP = Fixed26_6::fromInt(100);
	player._pHitPoints = player._pMaxHP;
	EXPECT_FALSE(UseFlask(player, 0));
	EXPECT_EQ(player.poe2.flaskCharges[0], MaxFlaskCharges) << "no charges spent at full life";
	player._pHitPoints = Fixed26_6::fromInt(1);
	EXPECT_TRUE(UseFlask(player, 0));
}

TEST_F(Poe2FlasksTest, KillsRefillCharges)
{
	Player &player = *MyPlayer;
	player.poe2.flaskCharges.fill(0);
	GainFlaskCharges(player, 4);
	EXPECT_EQ(player.poe2.flaskCharges[3], 4);
	GainFlaskCharges(player, 200);
	EXPECT_EQ(player.poe2.flaskCharges[2], MaxFlaskCharges);
	EXPECT_EQ(player.poe2.flaskCharges[4], MaxFlaskCharges);

	player.poe2.flaskCharges.fill(0);
	RefillFlasks(player);
	EXPECT_EQ(player.poe2.flaskCharges[1], MaxFlaskCharges);
}

TEST_F(Poe2FlasksTest, AFullFlaskHasSixUsesAndRejuvenationThree)
{
	Player &player = *MyPlayer;
	InitializeItem(player.SpdList[0], IDI_HEAL);
	InitializeItem(player.SpdList[1], RejuvenationPotion());
	EXPECT_EQ(MaxFlaskCharges / FlaskChargesPerUseFor(player.SpdList[0]), 6);
	EXPECT_EQ(MaxFlaskCharges / FlaskChargesPerUseFor(player.SpdList[1]), 3);
}

TEST_F(Poe2FlasksTest, TierComesFromItemLevelAndFullPotionsAreHigher)
{
	Item flask {};
	InitializeItem(flask, IDI_HEAL);
	flask._iCreateInfo = 1;
	EXPECT_EQ(GetFlaskTier(flask), 0U);
	flask._iCreateInfo = 18;
	EXPECT_EQ(GetFlaskTier(flask), 3U);
	flask._iCreateInfo = 63;
	EXPECT_EQ(GetFlaskTier(flask), NumFlaskTiers - 1);

	Item full {};
	InitializeItem(full, IDI_FULLHEAL);
	full._iCreateInfo = 18;
	EXPECT_EQ(GetFlaskTier(full), 5U);
	EXPECT_EQ(FlaskDescription(flask), "Recovers 920 life over 3.0 seconds");
}

TEST_F(Poe2FlasksTest, PoE2sLadderReachesTheEndgame)
{
	// POE2's nine bases, Lesser at level 1 to Ultimate at 60 (Curtis 2026-10-30: "More, like POE2").
	EXPECT_EQ(NumFlaskTiers, 9U);
	EXPECT_EQ(FlaskTiers[0].level, 1);
	EXPECT_EQ(FlaskTiers[NumFlaskTiers - 1].level, 60);
	EXPECT_EQ(FlaskTiers[NumFlaskTiers - 1].life, 920);
	EXPECT_EQ(FlaskTiers[NumFlaskTiers - 1].mana, 310);
	for (size_t i = 1; i < NumFlaskTiers; i++) {
		EXPECT_GT(FlaskTiers[i].level, FlaskTiers[i - 1].level) << i;
		EXPECT_GT(FlaskTiers[i].life, FlaskTiers[i - 1].life) << i;
		EXPECT_GE(FlaskTiers[i].mana, FlaskTiers[i - 1].mana) << i;
	}
	// A hero gains 12 life a level as in POE2, so a flask of the hero's level recovers about a hero's base life:
	// Lesser 50 at level 1, Colossal 450 at 30, Ultimate 920 at 60.
	EXPECT_EQ(FlaskTiers[FlaskTierForLevel(30)].life, 450);

	Item flask {};
	InitializeItem(flask, IDI_MANA);
	const std::pair<int, std::string_view> names[] = {
		{ 1, "Lesser Mana Flask" }, { 4, "Medium Mana Flask" }, { 10, "Greater Mana Flask" }, { 16, "Grand Mana Flask" }, { 23, "Giant Mana Flask" },
		{ 30, "Colossal Mana Flask" }, { 40, "Gargantuan Mana Flask" }, { 50, "Transcendent Mana Flask" }, { 63, "Ultimate Mana Flask" },
	};
	for (const auto &[level, name] : names) {
		flask._iCreateInfo = static_cast<uint16_t>(level);
		NameFlask(flask);
		EXPECT_EQ(std::string_view(flask._iIName), name) << level;
	}
}

TEST_F(Poe2FlasksTest, FlasksSavedBeforeTheLadderKeepAboutTheirAmounts)
{
	// Saved flasks have Diablo's monster level, 1 to 32 (the old ladder's Minor at 1 to Full at 32: 60 to 450 life).
	constexpr std::pair<int, int> OldLifeAtLevel[] = { { 1, 60 }, { 6, 110 }, { 12, 170 }, { 18, 240 }, { 25, 330 }, { 32, 450 } };
	Item flask {};
	InitializeItem(flask, IDI_HEAL);
	for (const auto &[level, oldLife] : OldLifeAtLevel) {
		flask._iCreateInfo = static_cast<uint16_t>(level);
		const int life = FlaskTiers[GetFlaskTier(flask)].life;
		EXPECT_GE(life * 100, oldLife * 80) << level;
		EXPECT_LE(life * 100, oldLife * 140) << level;
	}
}

TEST_F(Poe2FlasksTest, DroppedFlasksTakeTheAreaLevel)
{
	EXPECT_TRUE(IsFlaskBase(IDI_HEAL));
	EXPECT_TRUE(IsFlaskBase(IDI_FULLMANA));
	EXPECT_FALSE(IsFlaskBase(IDI_GOLD));
	EXPECT_FALSE(IsFlaskBase(-1));
	// The area level, within CF_LEVEL's 6 bits; multiplayer keeps Diablo's level, which item checks go by there.
	EXPECT_EQ(FlaskDropLevel(55, 30), 55);
	EXPECT_EQ(FlaskDropLevel(80, 30), 63);
	EXPECT_EQ(FlaskDropLevel(0, 30), 1);
	gbIsMultiplayer = true;
	EXPECT_EQ(FlaskDropLevel(55, 30), 30);
	gbIsMultiplayer = false;

	Item flask {};
	SetupAllItems(*MyPlayer, flask, IDI_HEAL, 1234, FlaskDropLevel(62, 30), 1, false, false);
	EXPECT_EQ(GetFlaskTier(flask), NumFlaskTiers - 1);
	Item rebuilt;
	RecreateItem(*MyPlayer, rebuilt, flask.IDidx, flask._iCreateInfo, flask._iSeed, flask._ivalue, flask.dwBuff);
	EXPECT_EQ(GetFlaskTier(rebuilt), NumFlaskTiers - 1);
}

TEST_F(Poe2FlasksTest, LifeComesBackOverTime)
{
	Player &player = *MyPlayer;
	player._pMaxHP = Fixed26_6::fromInt(500);
	player._pMaxHPBase = Fixed26_6::fromInt(500);
	player._pHitPoints = Fixed26_6::fromInt(100);
	player._pHPBase = Fixed26_6::fromInt(100);
	InitializeItem(player.SpdList[0], IDI_HEAL);
	player.SpdList[0]._iCreateInfo = 1;
	const FlaskTier &tier = FlaskTiers[0];

	ASSERT_TRUE(UseFlask(player, 0));
	EXPECT_EQ(player._pHitPoints.whole(), 100) << "nothing comes back instantly";
	for (int tick = 0; tick < tier.lifeTicks / 2; tick++)
		ProcessFlaskRecovery(player);
	EXPECT_NEAR(player._pHitPoints.whole(), 100 + (tier.life / 2), 1);
	for (int tick = 0; tick < tier.lifeTicks; tick++)
		ProcessFlaskRecovery(player);
	EXPECT_EQ(player._pHitPoints.whole(), 100 + tier.life);
	EXPECT_EQ(player._pHPBase.whole(), 100 + tier.life);
	EXPECT_EQ(player.poe2.lifeFlaskTicks, 0);
}

TEST_F(Poe2FlasksTest, RecoveryStopsAtFullLife)
{
	Player &player = *MyPlayer;
	player._pMaxHP = Fixed26_6::fromInt(100);
	player._pMaxHPBase = Fixed26_6::fromInt(100);
	player._pHitPoints = Fixed26_6::fromInt(90);
	player._pHPBase = Fixed26_6::fromInt(90);
	InitializeItem(player.SpdList[0], IDI_HEAL);
	ASSERT_TRUE(UseFlask(player, 0));
	for (int tick = 0; tick < 200; tick++)
		ProcessFlaskRecovery(player);
	EXPECT_EQ(player._pHitPoints, player._pMaxHP);
	EXPECT_EQ(player.poe2.lifeFlaskTicks, 0);
}

TEST_F(Poe2FlasksTest, RejuvenationIsInstant)
{
	Player &player = *MyPlayer;
	player._pMaxHP = Fixed26_6::fromInt(500);
	player._pMaxHPBase = Fixed26_6::fromInt(500);
	player._pHitPoints = Fixed26_6::fromInt(100);
	player._pHPBase = Fixed26_6::fromInt(100);
	player._pMaxMana = Fixed26_6::fromInt(500);
	player._pMaxManaBase = Fixed26_6::fromInt(500);
	InitializeItem(player.SpdList[0], RejuvenationPotion());
	player.SpdList[0]._iCreateInfo = 1;
	ASSERT_TRUE(UseFlask(player, 0));
	EXPECT_EQ(player._pHitPoints.whole(), 100 + (FlaskTiers[0].life / 2));
	EXPECT_EQ(player._pMana.whole(), FlaskTiers[0].mana / 2);
	EXPECT_EQ(player.poe2.flaskCharges[0], MaxFlaskCharges - RejuvenationChargesPerUse);
}

TEST_F(Poe2FlasksTest, FlasksAreNamedByTierAndMods)
{
	Item flask;
	InitializeItem(flask, IDI_HEAL);
	flask._iCreateInfo = 18;
	NameFlask(flask);
	EXPECT_STREQ(flask._iIName, "Grand Healing Flask");
	EXPECT_EQ(flask._iMagical, ITEM_QUALITY_NORMAL);

	SetFlaskMods(flask, FlaskPrefix::Concentrated, FlaskSuffix::Ox, 0);
	EXPECT_STREQ(flask._iIName, "Concentrated Grand Healing Flask of the Ox");
	EXPECT_EQ(flask._iMagical, ITEM_QUALITY_MAGIC);
	EXPECT_EQ(GetFlaskPrefix(flask), FlaskPrefix::Concentrated);
	EXPECT_EQ(GetFlaskSuffix(flask), FlaskSuffix::Ox);
}

TEST_F(Poe2FlasksTest, RebuildingAFlaskKeepsItsMods)
{
	Item flask;
	SetupAllItems(*MyPlayer, flask, IDI_HEAL, 1234, 10, 1, false, false);
	SetFlaskMods(flask, FlaskPrefix::Seething, FlaskSuffix::Plenty, 7);

	Item rebuilt;
	RecreateItem(*MyPlayer, rebuilt, flask.IDidx, flask._iCreateInfo, flask._iSeed, flask._ivalue, flask.dwBuff);
	EXPECT_EQ(GetFlaskPrefix(rebuilt), FlaskPrefix::Seething);
	EXPECT_EQ(GetFlaskSuffix(rebuilt), FlaskSuffix::Plenty);
	EXPECT_EQ(GetFlaskQuality(rebuilt), 7);
	EXPECT_STREQ(rebuilt._iIName, flask._iIName);

	// A scoured flask stays normal when it's rebuilt.
	SetFlaskMods(flask, FlaskPrefix::None, FlaskSuffix::None, 0);
	RecreateItem(*MyPlayer, rebuilt, flask.IDidx, flask._iCreateInfo, flask._iSeed, flask._ivalue, flask.dwBuff);
	EXPECT_EQ(rebuilt._iMagical, ITEM_QUALITY_NORMAL);
}

TEST_F(Poe2FlasksTest, SomeDroppedFlasksAreMagic)
{
	int magic = 0;
	for (uint32_t seed = 1; seed <= 400; seed++) {
		Item flask;
		SetupAllItems(*MyPlayer, flask, IDI_HEAL, seed, 10, 1, false, false);
		if (flask._iMagical == ITEM_QUALITY_MAGIC)
			magic++;
	}
	EXPECT_GT(magic, 100);
	EXPECT_LT(magic, 180);
}

TEST_F(Poe2FlasksTest, SeethingRecoversAtOnceAndConcentratedMore)
{
	Player &player = *MyPlayer;
	player._pMaxHP = player._pMaxHPBase = Fixed26_6::fromInt(500);
	player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(100);
	InitializeItem(player.SpdList[0], IDI_HEAL);
	player.SpdList[0]._iCreateInfo = 1;
	SetFlaskMods(player.SpdList[0], FlaskPrefix::Seething, FlaskSuffix::None, 0);
	ASSERT_TRUE(UseFlask(player, 0));
	EXPECT_EQ(player._pHitPoints.whole(), 100 + (FlaskTiers[0].life * 60 / 100));
	EXPECT_EQ(player.poe2.lifeFlaskTicks, 0);

	player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(100);
	SetFlaskMods(player.SpdList[0], FlaskPrefix::Concentrated, FlaskSuffix::None, 10);
	ASSERT_TRUE(UseFlask(player, 0));
	for (int tick = 0; tick < 200; tick++)
		ProcessFlaskRecovery(player);
	EXPECT_EQ(player._pHitPoints.whole(), 100 + (FlaskTiers[0].life * 150 / 100));
}

TEST_F(Poe2FlasksTest, AmpleHoldsMoreAndPlentyUsesLess)
{
	Player &player = *MyPlayer;
	InitializeItem(player.SpdList[0], IDI_HEAL);
	SetFlaskMods(player.SpdList[0], FlaskPrefix::Ample, FlaskSuffix::Plenty, 0);
	EXPECT_EQ(MaxFlaskChargesFor(player.SpdList[0]), MaxFlaskCharges + AmpleFlaskCharges);
	EXPECT_EQ(FlaskChargesPerUseFor(player.SpdList[0]), 8);
	RefillFlasks(player);
	EXPECT_EQ(player.poe2.flaskCharges[0], MaxFlaskCharges + AmpleFlaskCharges);
}

TEST_F(Poe2FlasksTest, OxGivesArmourAndSpringsRefills)
{
	Player &player = *MyPlayer;
	player._pMaxHP = player._pMaxHPBase = Fixed26_6::fromInt(500);
	player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(100);
	InitializeItem(player.SpdList[0], IDI_HEAL);
	SetFlaskMods(player.SpdList[0], FlaskPrefix::None, FlaskSuffix::Ox, 0);
	EXPECT_EQ(FlaskArmourPercent(player), 0);
	ASSERT_TRUE(UseFlask(player, 0));
	EXPECT_EQ(FlaskArmourPercent(player), OxArmourPercent);

	InitializeItem(player.SpdList[1], IDI_HEAL);
	SetFlaskMods(player.SpdList[1], FlaskPrefix::None, FlaskSuffix::Springs, 0);
	player.poe2.flaskCharges[1] = 0;
	player.poe2.springsFlaskTicks = 0;
	for (int tick = 0; tick < SpringsChargeTicks * 2; tick++)
		ProcessFlaskRecovery(player);
	EXPECT_EQ(player.poe2.flaskCharges[1], 2);
}

TEST_F(Poe2FlasksTest, FlasksPastTheTwoFlaskSlotsMoveForward)
{
	Player &player = *MyPlayer;
	InitializeItem(player.SpdList[0], IDI_HEAL);
	InitializeItem(player.SpdList[3], IDI_MANA);
	player.poe2.flaskCharges[3] = 40;
	ProcessFlaskRecovery(player);
	EXPECT_TRUE(player.SpdList[3].isEmpty());
	ASSERT_TRUE(IsFlaskSlot(player, 1)) << "an older save's flask in slot 4 moves to the free flask slot";
	EXPECT_EQ(player.poe2.flaskCharges[1], 40) << "with its charges";
}

} // namespace
} // namespace devilution
