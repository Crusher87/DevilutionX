#include <gtest/gtest.h>

#include <algorithm>

#include "engine/palette.h"
#include "engine/surface.hpp"
#include "poe2/hud_globes.hpp"
#include "poe2/skill_resources.hpp"
#include "utils/str_cat.hpp"

namespace devilution {
namespace {

constexpr uint8_t EmptyColor = 7;
constexpr uint8_t FrameColor = 9;
constexpr int Radius = 30;
constexpr Point Center { 44, 50 };

void Fill(const Surface &surface, uint8_t color)
{
	for (int y = 0; y < surface.h(); y++)
		std::fill_n(surface.at(0, y), surface.w(), color);
}

/** @brief Empty art is plain; the full art has a round globe of liquid in it. */
void MakeArt(const Surface &empty, const Surface &full)
{
	Fill(empty, EmptyColor);
	Fill(full, EmptyColor);
	for (int y = 0; y < full.h(); y++) {
		for (int x = 0; x < full.w(); x++) {
			const int dx = x - Center.x;
			const int dy = y - Center.y;
			if ((dx * dx) + (dy * dy) < Radius * Radius)
				*full.at(x, y) = FrameColor;
		}
	}
}

bool IsRamp(uint8_t color, uint8_t rampBase)
{
	return color >= rampBase && color < rampBase + 16;
}

TEST(Poe2HudGlobesTest, ArcSegmentsFollowTheValue)
{
	EXPECT_EQ(LitArcSegments(0, 100, 12), 0);
	EXPECT_EQ(LitArcSegments(10, 0, 12), 0);
	EXPECT_EQ(LitArcSegments(1, 100, 12), 1) << "a sliver still shows";
	EXPECT_EQ(LitArcSegments(50, 100, 12), 6);
	EXPECT_EQ(LitArcSegments(100, 100, 12), 12);
	EXPECT_EQ(LitArcSegments(150, 100, 12), 12);
}

TEST(Poe2HudGlobesTest, LiquidShapeComesFromTheArt)
{
	OwnedSurface empty(GlobeArtSize, GlobeArtSize);
	OwnedSurface full(GlobeArtSize, GlobeArtSize);
	MakeArt(empty, full);
	const GlobeShape shape = MeasureGlobe(empty, full);
	ASSERT_TRUE(shape.valid);
	EXPECT_NEAR(shape.centerX, Center.x, 1.F);
	EXPECT_NEAR(shape.centerY, Center.y, 1.F);
	EXPECT_NEAR(shape.radiusX, Radius, 1.F);
	EXPECT_EQ(shape.bottom, Center.y + Radius - 1);
	EXPECT_FLOAT_EQ(GlobeLiquidLevel(shape, 0), static_cast<float>(shape.bottom + 1));
	EXPECT_FLOAT_EQ(GlobeLiquidLevel(shape, GlobeFull), static_cast<float>(shape.top));

	Fill(full, EmptyColor);
	EXPECT_FALSE(MeasureGlobe(empty, full).valid);
}

TEST(Poe2HudGlobesTest, LiquidFillsFromTheBottom)
{
	OwnedSurface empty(GlobeArtSize, GlobeArtSize);
	OwnedSurface full(GlobeArtSize, GlobeArtSize);
	MakeArt(empty, full);
	const GlobeShape shape = MeasureGlobe(empty, full);
	OwnedSurface out(200, 200);
	const Point origin { 20, 30 };
	const Point nearBottom = origin + Displacement { Center.x, Center.y + Radius - 4 };
	const Point nearTop = origin + Displacement { Center.x, Center.y - Radius + 4 };
	const Point outside = origin + Displacement { 2, Center.y };

	Fill(out, 0);
	DrawGlobeLiquid(out, origin, empty, full, shape, GlobeLiquid::Life, GlobeFull / 2, false, 0, false);
	EXPECT_TRUE(IsRamp(out[nearBottom], PAL16_RED));
	EXPECT_EQ(out[nearTop], EmptyColor) << "the top half is empty";
	EXPECT_EQ(out[outside], EmptyColor);

	Fill(out, 0);
	DrawGlobeLiquid(out, origin, empty, full, shape, GlobeLiquid::Mana, GlobeFull, false, 1234, false);
	EXPECT_TRUE(IsRamp(out[nearBottom], PAL16_BLUE));
	EXPECT_TRUE(IsRamp(out[nearTop], PAL16_BLUE));

	Fill(out, 0);
	DrawGlobeLiquid(out, origin, empty, full, shape, GlobeLiquid::Life, 0, false, 0, false);
	EXPECT_EQ(out[nearBottom], EmptyColor);
}

TEST(Poe2HudGlobesTest, DomeOnlyDrawsAbovePanel)
{
	OwnedSurface empty(GlobeArtSize, GlobeArtSize);
	OwnedSurface full(GlobeArtSize, GlobeArtSize);
	MakeArt(empty, full);
	const GlobeShape shape = MeasureGlobe(empty, full);
	OwnedSurface out(200, 200);
	Fill(out, 0);
	const Point origin { 20, 30 };
	DrawGlobeLiquid(out, origin, empty, full, shape, GlobeLiquid::Life, GlobeFull, true, 0, false);
	const Point inPanel = origin + Displacement { Center.x, GlobePanelTopRow + 2 };
	const Point abovePanel = origin + Displacement { Center.x, GlobePanelTopRow - 2 };
	EXPECT_EQ(out[inPanel], 0);
	EXPECT_EQ(out[abovePanel], EmptyColor) << "outside the liquid in this test art";
}

TEST(Poe2HudGlobesTest, ArcLightsSegmentsFromItsEnd)
{
	OwnedSurface out(200, 200);
	const Point center { 100, 100 };
	constexpr int ArcRadius = 40;
	// A pixel in the middle of the 7th of 12 segments, just below the right of the ring.
	const Point middle { 142, 105 };
	// Near the bottom end of the arc, and near its top end.
	const Point bottomEnd { 112, 140 };
	const Point topEnd { 112, 60 };
	const Point left { 58, 100 };

	Fill(out, 0);
	DrawGlobeArc(out, center, ArcRadius, 4, EnergyShieldArc, EnergyShieldArc.segments, 0, 200, 0);
	EXPECT_TRUE(IsRamp(out[middle], PAL16_BLUE));
	EXPECT_EQ(out[left], 0) << "the energy shield ring is only on the right";

	Fill(out, 0);
	DrawGlobeArc(out, center, ArcRadius, 4, EnergyShieldArc, 0, 0, 200, 0);
	EXPECT_TRUE(IsRamp(out[middle], PAL16_GRAY));

	Fill(out, 0);
	DrawGlobeArc(out, center, ArcRadius, 4, EnergyShieldArc, 1, 0, 200, 0);
	EXPECT_TRUE(IsRamp(out[bottomEnd], PAL16_BLUE)) << "energy shield fills from the bottom";
	EXPECT_TRUE(IsRamp(out[topEnd], PAL16_GRAY));

	Fill(out, 0);
	DrawGlobeArc(out, center, ArcRadius, 4, EnergyShieldArc, EnergyShieldArc.segments, 0, 100, 0);
	EXPECT_EQ(out[bottomEnd], 0) << "rows below the clip are left alone";
	EXPECT_TRUE(IsRamp(out[topEnd], PAL16_BLUE));
}

TEST(Poe2HudGlobesTest, LifeLevelsShowEnergyShieldOnlyWhenTheHeroHasSome)
{
	GlobeLevels levels;
	levels.life = 120;
	levels.maxLife = 150;
	std::vector<GlobeLevelLine> lines = LifeGlobeLevelLines(levels);
	ASSERT_EQ(lines.size(), 1U);
	EXPECT_EQ(lines[0].label, "Life");
	EXPECT_EQ(lines[0].value, "120/150");

	levels.energyShield = 30;
	levels.maxEnergyShield = 40;
	lines = LifeGlobeLevelLines(levels);
	ASSERT_EQ(lines.size(), 2U);
	EXPECT_EQ(lines[0].label, "Life") << "life stays on top, like POE2";
	EXPECT_EQ(lines[1].label, "Energy");
	EXPECT_EQ(lines[1].value, "30/40");
	EXPECT_EQ(lines[1].color, UiFlags::ColorBlue);

	levels.life = -3;
	EXPECT_EQ(LifeGlobeLevelLines(levels)[0].value, "0/150") << "a dying hero never shows less than nothing";
}

TEST(Poe2HudGlobesTest, LifeLevelsShowFrenzyChargesOnlyWhileHeld)
{
	GlobeLevels levels;
	levels.life = 10;
	levels.maxLife = 10;
	levels.maxEnergyShield = 5;
	EXPECT_EQ(LifeGlobeLevelLines(levels).size(), 2U);

	levels.frenzyCharges = 2;
	const std::vector<GlobeLevelLine> lines = LifeGlobeLevelLines(levels);
	ASSERT_EQ(lines.size(), 3U);
	EXPECT_EQ(lines[2].label, "Frenzy") << "frenzy charges come last, under energy shield";
	EXPECT_EQ(lines[2].value, StrCat(2, "/", MaxFrenzyCharges));
}

TEST(Poe2HudGlobesTest, ManaLevelsShowReservedSpiritOutOfTheTotal)
{
	GlobeLevels levels;
	levels.mana = 45;
	levels.maxMana = 80;
	std::vector<GlobeLevelLine> lines = ManaGlobeLevelLines(levels);
	ASSERT_EQ(lines.size(), 1U);
	EXPECT_EQ(lines[0].label, "Mana");
	EXPECT_EQ(lines[0].value, "45/80");

	levels.reservedSpirit = 30;
	levels.maxSpirit = 100;
	lines = ManaGlobeLevelLines(levels);
	ASSERT_EQ(lines.size(), 2U);
	EXPECT_EQ(lines[1].label, "Spirit");
	EXPECT_EQ(lines[1].value, "30/100");
	EXPECT_EQ(lines[1].color, UiFlags::ColorGold);
}

TEST(Poe2HudGlobesTest, ManaLevelsShowRageAndComboOnlyWhileBuiltUp)
{
	GlobeLevels levels;
	levels.mana = 45;
	levels.maxMana = 80;
	levels.maxSpirit = 100;
	EXPECT_EQ(ManaGlobeLevelLines(levels).size(), 2U) << "no Rage line without Rage";

	levels.rage = 12;
	std::vector<GlobeLevelLine> lines = ManaGlobeLevelLines(levels);
	ASSERT_EQ(lines.size(), 3U);
	EXPECT_EQ(lines[0].label, "Mana");
	EXPECT_EQ(lines[1].label, "Spirit");
	EXPECT_EQ(lines[2].label, "Rage") << "Rage sits under Spirit, like POE2";
	EXPECT_EQ(lines[2].value, StrCat(12, "/", MaxRage));
	EXPECT_EQ(lines[2].color, UiFlags::ColorRed);

	levels.combo = 4;
	lines = ManaGlobeLevelLines(levels);
	ASSERT_EQ(lines.size(), 4U);
	EXPECT_EQ(lines[3].label, "Combo");
	EXPECT_EQ(lines[3].value, StrCat(4, "/", MaxCombo));

	levels.rage = 0;
	lines = ManaGlobeLevelLines(levels);
	ASSERT_EQ(lines.size(), 3U);
	EXPECT_EQ(lines[2].label, "Combo") << "Combo shows without Rage";
}

TEST(Poe2HudGlobesTest, LevelLinesFitTheirSlots)
{
	GlobeLevels levels;
	levels.maxLife = 1;
	levels.maxEnergyShield = 1;
	levels.frenzyCharges = 1;
	levels.maxMana = 1;
	levels.maxSpirit = 1;
	levels.rage = 1;
	levels.combo = 1;
	EXPECT_LE(LifeGlobeLevelLines(levels).size(), static_cast<size_t>(GlobeLevelLineSlots));
	EXPECT_EQ(ManaGlobeLevelLines(levels).size(), static_cast<size_t>(GlobeLevelLineSlots));
}

TEST(Poe2HudGlobesTest, RageBarFillsLeftToRight)
{
	EXPECT_EQ(RageBarFill(0, MaxRage, 100), 0);
	EXPECT_EQ(RageBarFill(1, 1000, 100), 1) << "any Rage shows";
	EXPECT_EQ(RageBarFill(15, 30, 100), 50);
	EXPECT_EQ(RageBarFill(30, 30, 100), 100);
	EXPECT_EQ(RageBarFill(40, 30, 100), 100);
	EXPECT_EQ(RageBarFill(5, 0, 100), 0);
}

TEST(Poe2HudGlobesTest, RageBarIsAFramedRedFillOnADarkTrack)
{
	OwnedSurface out(20, 8);
	const Rectangle bar { { 2, 1 }, { 12, 5 } };

	Fill(out, 1);
	DrawRageBar(out, bar, 0, 10);
	EXPECT_EQ(*out.at(5, 3), 1) << "nothing while the hero has no Rage";

	DrawRageBar(out, bar, 5, 10);
	EXPECT_EQ(*out.at(1, 3), 1) << "nothing outside the bar";
	EXPECT_EQ(*out.at(2, 3), *out.at(13, 3)) << "a frame down both ends";
	EXPECT_EQ(*out.at(5, 1), *out.at(5, 5)) << "and along the top and bottom";
	EXPECT_FALSE(IsRamp(*out.at(2, 3), PAL16_RED));
	// The track is 10 pixels wide from x 3: half of it is lit.
	for (int x = 3; x < 8; x++)
		EXPECT_TRUE(IsRamp(*out.at(x, 3), PAL16_RED)) << x;
	EXPECT_LT(*out.at(3, 3), *out.at(8, 3)) << "the fill is brighter than the empty track";
	EXPECT_EQ(*out.at(8, 3), *out.at(12, 3)) << "the rest is empty track";
	EXPECT_LT(*out.at(4, 2), *out.at(4, 4)) << "the fill is lit along its top";
}

} // namespace
} // namespace devilution
