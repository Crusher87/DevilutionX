#include <gtest/gtest.h>

#include "engine/palette.h"
#include "engine/surface.hpp"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "levels/gendung.h"
#include "missiles.h"
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

TEST_F(Poe2VfxTest, ATownPortalIsDrawnByTheEffectsWhileItsMissileLives)
{
	Missiles.clear();
	Missile &portal = Missiles.emplace_back();
	portal._mitype = MissileID::TownPortal;
	portal._miAnimType = MissileGraphicID::None;
	portal.position.tile = { 40, 40 };
	portal._miDrawFlag = true;
	portal._miDelFlag = false;
	portal.setFrameGroup<PortalFrame>(PortalFrame::Start);
	vfx::ScanMissiles();
	EXPECT_EQ(vfx::PortalCount(), 1U);
	// Opening throws its sparks and its light.
	for (int i = 0; i < 20; i++)
		vfx::Advance(50);
	EXPECT_GT(vfx::ParticleCount(), 0U);
	// Gone from the level: it folds shut, then is forgotten.
	Missiles.clear();
	vfx::ScanMissiles();
	EXPECT_EQ(vfx::PortalCount(), 1U);
	for (int i = 0; i < 20; i++)
		vfx::Advance(50);
	EXPECT_EQ(vfx::PortalCount(), 0U);
}

/** @brief A screen of the dungeon showing nothing but one palette colour. */
OwnedSurface ScreenOf(uint8_t colour)
{
	OwnedSurface screen(256, 128);
	for (int y = 0; y < screen.h(); y++) {
		uint8_t *line = screen.at(0, y);
		for (int x = 0; x < screen.w(); x++)
			line[x] = colour;
	}
	return screen;
}

TEST_F(Poe2VfxTest, LavaOnScreenGlowsAndThrowsEmbers)
{
	const dungeon_type oldLevelType = leveltype;
	leveltype = DTYPE_CAVES;
	logical_palette[5] = { 240, 90, 20, 255 };
	vfx::SetCamera({ 40, 40 }, {}, false);
	const OwnedSurface screen = ScreenOf(5);
	vfx::ScanWorld(screen);
	EXPECT_GT(vfx::AmbientCount(), 0U);
	for (int i = 0; i < 20; i++)
		vfx::Advance(100);
	EXPECT_GT(vfx::ParticleCount(), 0U);
	leveltype = oldLevelType;
}

TEST_F(Poe2VfxTest, LowColoursAreOnlyFluidWhereThePaletteMoves)
{
	const dungeon_type oldLevelType = leveltype;
	leveltype = DTYPE_CATHEDRAL;
	logical_palette[5] = { 240, 90, 20, 255 };
	vfx::SetCamera({ 40, 40 }, {}, false);
	const OwnedSurface screen = ScreenOf(5);
	vfx::ScanWorld(screen);
	EXPECT_EQ(vfx::AmbientCount(), 0U);
	leveltype = oldLevelType;
}

} // namespace
} // namespace devilution
