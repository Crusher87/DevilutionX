#include <gtest/gtest.h>

#include "player.h"
#include "poe2/skill_resources.hpp"
#include "poe2/spirit.hpp"

namespace devilution {
namespace {

TEST(Poe2SkillResources, RageAddsAttackDamageAndDrains)
{
	Player player {};
	EXPECT_EQ(ResourceAttackDamagePercent(player), 100);
	AddRage(player, 50);
	EXPECT_EQ(player.poe2.rage, MaxRage);
	EXPECT_EQ(ResourceAttackDamagePercent(player), 100 + MaxRage);

	// Rage holds for a while after the last hit, then drains a point at a time.
	for (int i = 0; i < RageHoldTicks; i++)
		player.poe2.tick();
	EXPECT_EQ(player.poe2.rage, MaxRage);
	for (int i = 0; i < 40; i++)
		player.poe2.tick();
	EXPECT_LT(player.poe2.rage, MaxRage);
	EXPECT_GT(player.poe2.rage, 0);
}

TEST(Poe2SkillResources, ComboBuildsAndResets)
{
	Player player {};
	for (int i = 0; i < 15; i++)
		AddCombo(player);
	EXPECT_EQ(player.poe2.combo, MaxCombo);
	for (int i = 0; i < ComboTicks; i++)
		player.poe2.tick();
	EXPECT_EQ(player.poe2.combo, 0);
}

TEST(Poe2SkillResources, FrenzyChargesAddDamage)
{
	Player player {};
	player.poe2.frenzyCharges = MaxFrenzyCharges;
	player.poe2.frenzyTicks = 1;
	EXPECT_EQ(ResourceAttackDamagePercent(player), 100 + (MaxFrenzyCharges * FrenzyChargeDamagePercent));
	player.poe2.tick();
	EXPECT_EQ(player.poe2.frenzyCharges, 0);
}

TEST(Poe2SkillResources, SkeletalMagesGrowWithLevel)
{
	EXPECT_EQ(SkeletalMageCount(1), 1);
	EXPECT_EQ(SkeletalMageCount(20), 2);
	EXPECT_EQ(SkeletalMageCount(50), 3);
	EXPECT_EQ(SpiritReservation(PersistentSkill::SkeletalMages), 40);
	EXPECT_EQ(SpiritReservation(PersistentSkill::HeraldOfFrost), 30);
	EXPECT_FALSE(PersistentSkillName(PersistentSkill::HeraldOfFrost).empty());
}

} // namespace
} // namespace devilution
