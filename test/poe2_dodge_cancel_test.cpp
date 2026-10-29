/**
 * @file poe2_dodge_cancel_test.cpp
 *
 * POE2 Mashup: the dodge roll breaks off an attack or cast (Curtis 2026-10-29: "when I'm mid cast/attack I should be able
 * to dodge roll out of the animation"). Before the action frame the cast is cancelled and costs nothing; after it, the
 * roll only cuts the follow-through short and the hit stands.
 */
#include <gtest/gtest.h>

#include "cursor.h"
#include "engine/assets.hpp"
#include "engine/lighting_defs.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/attack_speed.hpp"
#include "poe2/dodge_roll.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {

extern void TestPlayerProcessAction(Player &player);

namespace {

constexpr WeaponSkill Skill = WeaponSkill::Spark;

class Poe2DodgeCancelTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayerId = 0;
		MyPlayer = &Players[0];
		*MyPlayer = {};
		MyPlayer->poe2.clear();
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		gbIsPoe2 = true;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadPlayerDataFiles();
		LoadMissileData();
		LoadItemData();
		LoadSpellData();
		LoadModifierData();
		LoadSkillSpeedData();
		Missiles.clear();
		leveltype = DTYPE_CATHEDRAL;
		ActiveMonsterCount = 0;
		pcursmonst = -1;

		Player &player = *MyPlayer;
		player._pClass = HeroClass::Sorcerer;
		player.setCharacterLevel(30);
		player._pBaseStr = player._pStrength = 100;
		player._pBaseMag = player._pMagic = 100;
		player._pBaseDex = player._pDexterity = 100;
		player.lightId = NO_LIGHT;
		player.position.tile = { 20, 20 };
		player.position.future = player.position.tile;
		player.position.old = player.position.tile;
		player._pdir = Direction::South;
		cursPosition = { 20, 25 };
		// Spark in the first socket, so it costs its gem's mana.
		player.poe2.skillSlots[0] = SkillSlot::ForAttack(Skill);
		player.poe2.slotLevels[0] = 5;
		CalcPlrInv(player, false);
		SetPlrAnims(player);
		player._pmode = PM_STAND;
		player.destAction = ACTION_NONE;
		player._pHitPoints = player._pHPBase = player._pMaxHP = player._pMaxHPBase = Fixed26_6::fromInt(200);
		player._pMana = player._pManaBase = player._pMaxMana = player._pMaxManaBase = Fixed26_6::fromInt(100);
	}

	void TearDown() override
	{
		Missiles.clear();
		leveltype = DTYPE_TOWN;
		pcursmonst = -1;
		gbIsPoe2 = false;
		LoadModifierData();
		LoadSkillSpeedData();
		UnloadModArchives();
		LoadMissileData();
	}

	/** @brief Missiles the cast fired, not its sparkles. */
	static int Shots()
	{
		int shots = 0;
		for (const Missile &missile : Missiles) {
			if (missile._mitype != MissileID::VisualEffect)
				shots++;
		}
		return shots;
	}
};

TEST_F(Poe2DodgeCancelTest, ARollBeforeTheActionFrameCancelsTheCastForFree)
{
	Player &player = *MyPlayer;
	ASSERT_GT(WeaponSkillManaCost(player, Skill), 0);
	ASSERT_TRUE(TryUseWeaponSkill(player, Skill));
	ASSERT_EQ(player._pmode, PM_SPELL);
	EXPECT_EQ(player.poe2.castSkill, Skill);
	// Paid as the cast started.
	EXPECT_LT(player._pMana, Fixed26_6::fromInt(100));
	TestPlayerProcessAction(player);
	ASSERT_EQ(player._pmode, PM_SPELL);
	ASSERT_FALSE(ActionHasGoneOff(player));
	Missiles.clear();

	BreakOffActionForDodge(player);
	EXPECT_EQ(player._pmode, PM_STAND);
	EXPECT_EQ(player.poe2.castSkill, WeaponSkill::None);
	EXPECT_EQ(player.destAction, ACTION_NONE);
	EXPECT_EQ(player._pMana, Fixed26_6::fromInt(100)) << "the cancelled cast costs nothing";
	EXPECT_EQ(player._pManaBase, Fixed26_6::fromInt(100));

	// Nothing goes off afterwards.
	for (int tick = 0; tick < 60; tick++) {
		TestPlayerProcessAction(player);
		ProcessSkillVolley(player);
		player.poe2.tick();
	}
	EXPECT_EQ(Shots(), 0);
	EXPECT_EQ(player._pMana, Fixed26_6::fromInt(100));
}

TEST_F(Poe2DodgeCancelTest, ARollKeepsDeepWellsFreeSkillForTheNextOne)
{
	Player &player = *MyPlayer;
	player.poe2.uniqueState.freeSkill = true;
	ASSERT_TRUE(TryUseWeaponSkill(player, Skill));
	EXPECT_FALSE(player.poe2.uniqueState.freeSkill);
	EXPECT_EQ(player._pMana, Fixed26_6::fromInt(100));
	BreakOffActionForDodge(player);
	EXPECT_TRUE(player.poe2.uniqueState.freeSkill);
	EXPECT_EQ(player._pMana, Fixed26_6::fromInt(100));
}

TEST_F(Poe2DodgeCancelTest, ARollAfterTheActionFrameKeepsTheHit)
{
	Player &player = *MyPlayer;
	ASSERT_TRUE(TryUseWeaponSkill(player, Skill));
	const Fixed26_6 paid = player._pMana;
	ASSERT_LT(paid, Fixed26_6::fromInt(100));
	for (int tick = 0; tick < 60 && player._pmode == PM_SPELL && !ActionHasGoneOff(player); tick++)
		TestPlayerProcessAction(player);
	ASSERT_EQ(player._pmode, PM_SPELL) << "the follow-through is still playing";
	ASSERT_TRUE(ActionHasGoneOff(player));
	const int shots = Shots();
	ASSERT_GT(shots, 0);

	BreakOffActionForDodge(player);
	EXPECT_EQ(player._pmode, PM_STAND);
	EXPECT_EQ(player.poe2.castSkill, WeaponSkill::None);
	EXPECT_EQ(Shots(), shots) << "the sparks fly on";
	EXPECT_EQ(player._pMana, paid) << "a cast that went off stays paid for";
}

TEST_F(Poe2DodgeCancelTest, TheActionFrameIsWhereTheCastGoesOff)
{
	Player &player = *MyPlayer;
	EXPECT_TRUE(ActionHasGoneOff(player)) << "standing, nothing is under way";
	ASSERT_TRUE(TryUseWeaponSkill(player, Skill));
	int ticks = 0;
	while (player._pmode == PM_SPELL && !ActionHasGoneOff(player) && ticks < 60) {
		EXPECT_EQ(Shots(), 0) << "nothing before the action frame, tick " << ticks;
		TestPlayerProcessAction(player);
		ticks++;
	}
	EXPECT_GT(Shots(), 0) << "it went off on the tick ActionHasGoneOff turned";
}

} // namespace
} // namespace devilution
