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
	EXPECT_EQ(WeaponSkillForItem(MakeBase("Dagger")), WeaponSkill::Flurry);
	EXPECT_EQ(WeaponSkillForItem(MakeBase("Long Sword")), WeaponSkill::Cleave);
	EXPECT_EQ(WeaponSkillForItem(MakeBase("Battle Axe")), WeaponSkill::Cleave);
	EXPECT_EQ(WeaponSkillForItem(MakeBase("Maul")), WeaponSkill::GroundSlam);
	EXPECT_EQ(WeaponSkillForItem(MakeBase("Long Bow")), WeaponSkill::Barrage);
	EXPECT_EQ(WeaponSkillForItem(MakeBase("Long Staff")), WeaponSkill::WhirlingStaff);
	EXPECT_EQ(WeaponSkillForItem(MakeBase("Buckler")), WeaponSkill::None);
	EXPECT_EQ(WeaponSkillForItem(Item {}), WeaponSkill::None);

	Player &player = *MyPlayer;
	player = {};
	player.InvBody[INVLOC_HAND_RIGHT] = MakeBase("Long Bow");
	EXPECT_EQ(WeaponSkillFor(player), WeaponSkill::Barrage);
	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Maul");
	EXPECT_EQ(WeaponSkillFor(player), WeaponSkill::GroundSlam);
}

TEST_F(Poe2WeaponSkillsTest, EveryWeaponHasAFreeDefaultAttack)
{
	Player &player = *MyPlayer;
	player = {};
	player._pClass = HeroClass::Rogue;
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::Punch);
	player.InvBody[INVLOC_HAND_RIGHT] = MakeBase("Buckler");
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::ShieldBash);
	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Long Sword");
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::SwordStrike);
	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Dagger");
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::DaggerStab);
	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Battle Axe");
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::AxeChop);
	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Maul");
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::MaceSmash);
	player.InvBody[INVLOC_HAND_RIGHT] = {};
	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Long Bow");
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::BowShot);
	EXPECT_TRUE(IsRangedDefaultAttack(DefaultAttack::BowShot));
	EXPECT_FALSE(FiresStaffBolt(player));

	// A staff channels a bolt, except for a Monk, who fights with it.
	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Long Staff");
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
		EXPECT_GE(damage, 4 + 2 + 10);
		EXPECT_LE(damage, 12 + 6 + 10);
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
