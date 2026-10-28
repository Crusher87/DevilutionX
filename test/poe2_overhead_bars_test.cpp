#include <gtest/gtest.h>

#include <algorithm>
#include <array>

#include "engine/clx_sprite.hpp"
#include "engine/surface.hpp"
#include "poe2/overhead_bars.hpp"

namespace devilution {
namespace {

void Fill(const Surface &surface, uint8_t color)
{
	for (int y = 0; y < surface.h(); y++)
		std::fill_n(surface.at(0, y), surface.w(), color);
}

TEST(Poe2OverheadBarsTest, FillFollowsTheValue)
{
	EXPECT_EQ(OverheadBarFill(0, 100, 38), 0);
	EXPECT_EQ(OverheadBarFill(10, 0, 38), 0) << "nothing to show without a maximum";
	EXPECT_EQ(OverheadBarFill(1, 1000, 38), 1) << "a sliver still shows";
	EXPECT_EQ(OverheadBarFill(50, 100, 38), 19);
	EXPECT_EQ(OverheadBarFill(100, 100, 38), 38);
	EXPECT_EQ(OverheadBarFill(150, 100, 38), 38) << "life stolen over the maximum fills the bar, no more";
}

TEST(Poe2OverheadBarsTest, SolidTopRowSkipsTheEmptyRowsAboveTheSprite)
{
	// A 4 by 5 sprite with its 3 bottom rows solid: CLX rows run from the bottom up.
	constexpr std::array<uint8_t, 9> Data { 6, 0, 4, 0, 5, 0, 0xBF - 12, 7, 8 };
	const ClxSprite sprite { Data.data(), static_cast<uint32_t>(Data.size()) };
	EXPECT_EQ(ClxSolidTopRow(sprite), 2);

	constexpr std::array<uint8_t, 7> Empty { 6, 0, 4, 0, 5, 0, 20 };
	EXPECT_EQ(ClxSolidTopRow(ClxSprite { Empty.data(), static_cast<uint32_t>(Empty.size()) }), 5);
}

TEST(Poe2OverheadBarsTest, HeroBarShowsLifeWithManaUnder)
{
	OwnedSurface out(HeroBarWidth + 4, HeroBarHeight + 4);
	Fill(out, 0);
	OverheadBarValues values;
	values.life = 50;
	values.maxLife = 100;
	values.mana = 100;
	values.maxMana = 100;
	DrawHeroBar(out, { 0, 0 }, values, false);

	EXPECT_EQ(out[Point(0, 0)], HeroBarFrameColor);
	EXPECT_EQ(out[Point(1, 1)], OverheadBarLifeColor);
	EXPECT_EQ(out[Point(1, HeroLifeRows)], OverheadBarLifeColor);
	EXPECT_EQ(out[Point(HeroBarWidth - 2, 1)], OverheadBarEmptyColor) << "half the life, half the bar";
	EXPECT_EQ(out[Point(1, HeroLifeRows + 1)], OverheadBarEmptyColor) << "a dark line between life and mana";
	EXPECT_EQ(out[Point(HeroBarWidth - 2, HeroLifeRows + 2)], OverheadBarManaColor);
	EXPECT_EQ(out[Point(1, HeroBarHeight - 1)], HeroBarFrameColor);
	EXPECT_EQ(out[Point(HeroBarWidth, 0)], 0) << "nothing outside the bar";
}

TEST(Poe2OverheadBarsTest, EnergyShieldGoesOverTheTopOfTheLifeBar)
{
	OwnedSurface out(HeroBarWidth, HeroBarHeight);
	OverheadBarValues values;
	values.life = 100;
	values.maxLife = 100;
	values.energyShield = 25;
	values.maxEnergyShield = 100;

	Fill(out, 0);
	DrawHeroBar(out, { 0, 0 }, values, false);
	EXPECT_EQ(out[Point(1, 1)], OverheadBarLifeColor) << "Energy Shield only shows with its option";

	Fill(out, 0);
	DrawHeroBar(out, { 0, 0 }, values, true);
	EXPECT_EQ(out[Point(1, 1)], OverheadBarEnergyShieldColor);
	EXPECT_EQ(out[Point(1, HeroEnergyShieldRows)], OverheadBarEnergyShieldColor);
	EXPECT_EQ(out[Point(1, HeroEnergyShieldRows + 1)], OverheadBarLifeColor) << "the life still shows under it";
	EXPECT_EQ(out[Point(HeroBarWidth - 2, 1)], OverheadBarLifeColor) << "a quarter of the shield covers a quarter of the bar";

	values.maxEnergyShield = 0;
	Fill(out, 0);
	DrawHeroBar(out, { 0, 0 }, values, true);
	EXPECT_EQ(out[Point(1, 1)], OverheadBarLifeColor) << "no Energy Shield, nothing over the life";
}

} // namespace
} // namespace devilution
