#include <gtest/gtest.h>

#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "poe2/vfx.hpp"

namespace devilution {
namespace {

class Poe2VfxTest : public ::testing::Test {
public:
	void SetUp() override
	{
		gbIsPoe2 = true;
		HeadlessMode = false;
		vfx::Clear();
	}

	void TearDown() override
	{
		vfx::Clear();
		HeadlessMode = true;
	}
};

TEST_F(Poe2VfxTest, ElementsFollowTheDamageType)
{
	EXPECT_EQ(vfx::ElementOf(DamageType::Fire, false), vfx::Element::Fire);
	EXPECT_EQ(vfx::ElementOf(DamageType::Magic, true), vfx::Element::Cold);
	EXPECT_EQ(vfx::ElementOf(DamageType::Acid, true), vfx::Element::Chaos);
	EXPECT_EQ(vfx::ElementOf(DamageType::Physical, false), vfx::Element::Physical);
	EXPECT_EQ(vfx::ElementOf(DamageType::Physical, true), vfx::Element::Holy);
}

TEST_F(Poe2VfxTest, AnImpactSpraysParticlesThatDieOut)
{
	vfx::Impact({ 40, 40 }, vfx::Element::Fire, 1.F);
	EXPECT_GT(vfx::ParticleCount(), 0U);
	EXPECT_GT(vfx::ShapeCount(), 0U);
	for (int i = 0; i < 200; i++)
		vfx::Advance(50);
	EXPECT_EQ(vfx::ParticleCount(), 0U);
	EXPECT_EQ(vfx::ShapeCount(), 0U);
}

TEST_F(Poe2VfxTest, ASigilLastsAsLongAsItCharges)
{
	vfx::Sigil({ 40, 40 }, 2, 2000, vfx::Element::Arcane);
	ASSERT_EQ(vfx::ShapeCount(), 1U);
	vfx::Advance(1900);
	EXPECT_EQ(vfx::ShapeCount(), 1U);
	vfx::Advance(1000);
	EXPECT_EQ(vfx::ShapeCount(), 0U);
}

TEST_F(Poe2VfxTest, NothingIsMadeOutsidePoe2OrWithoutAScreen)
{
	gbIsPoe2 = false;
	vfx::Showcase({ 40, 40 });
	EXPECT_EQ(vfx::ShapeCount(), 0U);
	gbIsPoe2 = true;
	HeadlessMode = true;
	vfx::Slam({ 40, 40 }, 2, vfx::Element::Physical);
	EXPECT_EQ(vfx::ShapeCount(), 0U);
	EXPECT_EQ(vfx::ParticleCount(), 0U);
}

} // namespace
} // namespace devilution
