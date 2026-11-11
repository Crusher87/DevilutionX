#include <cstdint>

#include <gtest/gtest.h>

#include "poe2/skill_details.hpp"

namespace devilution {
namespace {

TEST(Poe2SkillDetailsTest, MaceSmashKeepsTheFractionOfItsHit)
{
	// Curtis's sceptre: 2-5, through the free default attack's 85%. The hit keeps 1/64 points: 1.69 to 4.25.
	const DamageEstimate hit = MeleeHitEstimate(2, 5, 0, 0, 85);
	EXPECT_DOUBLE_EQ(hit.min, 108.0 / 64);
	EXPECT_DOUBLE_EQ(hit.max, 4.25);
	EXPECT_EQ(ShownDamage(hit.min), 2);
	EXPECT_EQ(ShownDamage(hit.max), 4);
	// The rolls 2, 3, 4 and 5 deal 1.69, 2.55, 3.39 and 4.25: about 2.97, shown as 3, not the 2 of truncated ends.
	EXPECT_NEAR(hit.average, (108.0 + 163.0 + 217.0 + 272.0) / 64 / 4, 1e-9);
	EXPECT_EQ(ShownDamage(hit.average), 3);
}

TEST(Poe2SkillDetailsTest, MeleePercentsApplyOneAfterTheOther)
{
	// Leap Slam: the gem's percent, then the slam's 80%, each cut to 1/64 points.
	const DamageEstimate hit = MeleeHitEstimate(10, 10, 0, 0, 115, 80);
	const int64_t raw = (int64_t { 10 } << 6) * 115 / 100 * 80 / 100;
	EXPECT_DOUBLE_EQ(hit.min, static_cast<double>(raw) / 64);
	EXPECT_DOUBLE_EQ(hit.max, hit.min);
	EXPECT_DOUBLE_EQ(hit.average, hit.min);
}

TEST(Poe2SkillDetailsTest, MeleeBonusesAreWholePointsBeforeThePercent)
{
	// 50% increased and 3 added on a roll of 5: 5 + 2 + 3 = 10 whole points, then 85% = 8.5.
	const DamageEstimate hit = MeleeHitEstimate(5, 5, 50, 3, 85);
	EXPECT_DOUBLE_EQ(hit.min, 8.5);
	EXPECT_EQ(ShownDamage(hit.min), 9);
}

TEST(Poe2SkillDetailsTest, ArrowsCutTheBowRangeToWholePoints)
{
	// The arrow's range is cut before the roll (ScaleSupportedMissileDamage): 85% of 2-5 is 1-4, whole points.
	const DamageEstimate hit = ArrowHitEstimate(2, 5, 0, 0, 85);
	EXPECT_DOUBLE_EQ(hit.min, 1);
	EXPECT_DOUBLE_EQ(hit.max, 4);
	EXPECT_DOUBLE_EQ(hit.average, 2.5);
	EXPECT_EQ(ShownDamage(hit.average), 3);
	// The hero's damage lands on each whole roll.
	const DamageEstimate bonus = ArrowHitEstimate(2, 5, 100, 1, 85);
	EXPECT_DOUBLE_EQ(bonus.min, 3);
	EXPECT_DOUBLE_EQ(bonus.max, 9);
}

TEST(Poe2SkillDetailsTest, ShownDamageRoundsToTheNearestPoint)
{
	EXPECT_EQ(ShownDamage(0), 0);
	EXPECT_EQ(ShownDamage(1.49), 1);
	EXPECT_EQ(ShownDamage(1.5), 2);
	EXPECT_EQ(ShownDamage(4.25), 4);
	EXPECT_EQ(ShownDamage(2.97), 3);
}

} // namespace
} // namespace devilution
