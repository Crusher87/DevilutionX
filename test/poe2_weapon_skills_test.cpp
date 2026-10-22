#include <string>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/default_attack.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"

namespace devilution {
namespace {

class Poe2WeaponSkillsTest : public ::testing::Test {
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
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		UnloadModArchives();
	}

	static Item MakeBase(std::string_view name)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == name && AllItemsList[i].dropRate > 0) {
				Item item;
				InitializeItem(item, static_cast<_item_indexes>(i));
				return item;
			}
		}
		return {};
	}
};

TEST_F(Poe2WeaponSkillsTest, WeaponTypesGrantSkills)
{
	EXPECT_EQ(WeaponSkillForItem(MakeBase("Shiv")), WeaponSkill::Flurry);
	EXPECT_EQ(WeaponSkillForItem(MakeBase("Shortsword")), WeaponSkill::Cleave);
	EXPECT_EQ(WeaponSkillForItem(MakeBase("Woodsman's Axe")), WeaponSkill::Cleave);
	EXPECT_EQ(WeaponSkillForItem(MakeBase("Sledge")), WeaponSkill::GroundSlam);
	EXPECT_EQ(WeaponSkillForItem(MakeBase("Yew Bow")), WeaponSkill::Barrage);
	EXPECT_EQ(WeaponSkillForItem(MakeBase("Quick Pilgrim Staff")), WeaponSkill::WhirlingStaff);
	EXPECT_EQ(WeaponSkillForItem(MakeBase("Hide Buckler")), WeaponSkill::None);
	EXPECT_EQ(WeaponSkillForItem(Item {}), WeaponSkill::None);

	Player &player = *MyPlayer;
	player = {};
	player.InvBody[INVLOC_HAND_RIGHT] = MakeBase("Yew Bow");
	EXPECT_EQ(WeaponSkillFor(player), WeaponSkill::Barrage);
	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Sledge");
	EXPECT_EQ(WeaponSkillFor(player), WeaponSkill::GroundSlam);
}

TEST_F(Poe2WeaponSkillsTest, EveryWeaponHasAFreeDefaultAttack)
{
	Player &player = *MyPlayer;
	player = {};
	player._pClass = HeroClass::Rogue;
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::Punch);
	player.InvBody[INVLOC_HAND_RIGHT] = MakeBase("Hide Buckler");
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::ShieldBash);
	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Shortsword");
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::SwordStrike);
	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Shiv");
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::DaggerStab);
	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Woodsman's Axe");
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::AxeChop);
	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Sledge");
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::MaceSmash);
	player.InvBody[INVLOC_HAND_RIGHT] = {};
	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Yew Bow");
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::BowShot);
	EXPECT_TRUE(IsRangedDefaultAttack(DefaultAttack::BowShot));
	EXPECT_FALSE(FiresStaffBolt(player));

	// A staff channels a bolt, except for a Monk, who fights with it.
	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Quick Pilgrim Staff");
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::StaffBolt);
	EXPECT_TRUE(FiresStaffBolt(player));
	player._pClass = HeroClass::Monk;
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::StaffStrike);
	EXPECT_FALSE(FiresStaffBolt(player));
}

TEST_F(Poe2WeaponSkillsTest, StaffBoltScalesWithTheStaff)
{
	Player &player = *MyPlayer;
	player = {};
	player._pIMinDam = 4;
	player._pIMaxDam = 12;
	player._pIBonusDam = 50;
	player._pMagic = 50;
	SetRndSeed(3);
	for (int i = 0; i < 200; i++) {
		const int damage = RollStaffBoltDamage(player);
		// The staff's damage with its bonus, cut to StaffBoltDamagePercent, plus 10% of Magic.
		EXPECT_GE(damage, ((4 + 2) * StaffBoltDamagePercent / 100) + 5);
		EXPECT_LE(damage, ((12 + 6) * StaffBoltDamagePercent / 100) + 5);
	}
}

TEST_F(Poe2WeaponSkillsTest, QueuedSkillPaysManaWhenTheAttackStarts)
{
	Player &player = *MyPlayer;
	player = {};
	player._pMana = Fixed26_6::fromInt(20);
	player._pManaBase = Fixed26_6::fromInt(20);

	// A melee skill isn't spent on a ranged attack.
	player.poe2.pendingSkill = WeaponSkill::Cleave;
	ActivateQueuedWeaponSkill(player, /*isRangedAttack=*/true);
	EXPECT_EQ(player.poe2.activeSkill, WeaponSkill::None);
	EXPECT_EQ(player.poe2.pendingSkill, WeaponSkill::Cleave);

	ActivateQueuedWeaponSkill(player, /*isRangedAttack=*/false);
	EXPECT_EQ(player.poe2.activeSkill, WeaponSkill::Cleave);
	EXPECT_EQ(player.poe2.pendingSkill, WeaponSkill::None);
	EXPECT_EQ(player._pMana.whole(), 20 - WeaponSkillManaCost(WeaponSkill::Cleave));

	// The next plain attack is normal again.
	ActivateQueuedWeaponSkill(player, /*isRangedAttack=*/false);
	EXPECT_EQ(player.poe2.activeSkill, WeaponSkill::None);

	// Without enough mana the attack happens without the skill.
	player._pMana = Fixed26_6::fromInt(1);
	player.poe2.pendingSkill = WeaponSkill::GroundSlam;
	ActivateQueuedWeaponSkill(player, /*isRangedAttack=*/false);
	EXPECT_EQ(player.poe2.activeSkill, WeaponSkill::None);
	EXPECT_EQ(player._pMana.whole(), 1);
}

TEST_F(Poe2WeaponSkillsTest, QueuedSkillExpires)
{
	Poe2PlayerData data;
	data.pendingSkill = WeaponSkill::Flurry;
	data.pendingSkillTicks = 2;
	data.tick();
	EXPECT_EQ(data.pendingSkill, WeaponSkill::Flurry);
	data.tick();
	EXPECT_EQ(data.pendingSkill, WeaponSkill::None);
}

TEST_F(Poe2WeaponSkillsTest, BattleCryWearsOff)
{
	Poe2PlayerData data;
	data.warcryTicks = 2;
	data.tick();
	EXPECT_EQ(data.warcryTicks, 1);
	data.tick();
	data.tick();
	EXPECT_EQ(data.warcryTicks, 0);
	data.warcryTicks = 5;
	data.clear();
	EXPECT_EQ(data.warcryTicks, 0);
}

TEST_F(Poe2WeaponSkillsTest, DashSkillStrikesAsItLands)
{
	// Curtis 2026-10-22: Dashing Strike stood about a second after the dash before it struck. A Monk's staff swing:
	// 13 frames, hitting on the eighth.
	Player &player = *MyPlayer;
	player._pAFrames = 13;
	player._pAFNum = 8;
	player._pdir = Direction::South;
	player.position.tile = { 20, 20 };
	player.position.future = player.position.tile;

	const auto ticksToHit = [&player](bool quickStrike) {
		BeginSkillAnimation(player, WeaponSkill::DashingStrike, player_graphic::Attack, player.position.tile + Direction::South, quickStrike);
		EXPECT_EQ(player._pmode, PM_SPELL);
		int ticks = 0;
		while (player.AnimInfo.currentFrame != player.poe2.castFrame && ticks < 30) {
			player.AnimInfo.processAnimation();
			ticks++;
		}
		player.poe2.castSkill = WeaponSkill::None;
		player._pmode = PM_STAND;
		return ticks;
	};
	EXPECT_EQ(ticksToHit(/*quickStrike=*/false), 7);
	EXPECT_EQ(ticksToHit(/*quickStrike=*/true), DashStrikeWindupTicks);
}

TEST(Poe2WeaponSkills, GroundSlamHitsAForwardCone)
{
	// Facing south (down the screen), the cone covers the tiles ahead and not the ones behind or beside.
	EXPECT_TRUE(InGroundSlamCone(Direction::South, Displacement { 1, 1 }));
	EXPECT_TRUE(InGroundSlamCone(Direction::South, Displacement { 3, 3 }));
	EXPECT_TRUE(InGroundSlamCone(Direction::South, Displacement { 3, 2 }));
	EXPECT_FALSE(InGroundSlamCone(Direction::South, Displacement { -1, -1 }));
	EXPECT_FALSE(InGroundSlamCone(Direction::South, Displacement { 1, -1 }));
	EXPECT_FALSE(InGroundSlamCone(Direction::South, Displacement { 3, 0 }));
}

TEST(Poe2WeaponSkills, FireballIsSlowAndFireboltIsFast)
{
	EXPECT_LT(FireballSpeedPercent, 100);
	EXPECT_GT(FireboltSpeedPercent, 100);
	EXPECT_GE(BarrageArrows, 5);
	EXPECT_GE(EmberFusilladeEmbers, 5);
}

} // namespace
} // namespace devilution
