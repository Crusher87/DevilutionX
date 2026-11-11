#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <vector>

#include "engine/clx_sprite.hpp"
#include "engine/palette.h"
#include "engine/render/primitive_render.hpp"
#include "engine/surface.hpp"
#include "game_mode.hpp"
#include "monster.h"
#include "poe2/monster_mods.hpp"
#include "poe2/overhead_bars.hpp"
#include "tables/monstdat.h"

namespace devilution {
namespace {

void Fill(const Surface &surface, uint8_t color)
{
	for (int y = 0; y < surface.h(); y++)
		std::fill_n(surface.at(0, y), surface.w(), color);
}

/** @brief A palette with the bars' colours at 40 onwards and black elsewhere, so each shade is found exactly. */
void UseBarPalette()
{
	for (SDL_Color &colour : logical_palette)
		colour = { 0, 0, 0, 255 };
	int index = 40;
	for (const std::array<uint8_t, 3> &colour : OverheadBarTargetColours())
		logical_palette[index++] = { colour[0], colour[1], colour[2], 255 };
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

TEST(Poe2OverheadBarsTest, BarShadesComeFromTheLevelPalette)
{
	UseBarPalette();
	const OverheadBarShades &shades = GetOverheadBarShades();
	EXPECT_EQ(shades.frame, 40);
	EXPECT_EQ(shades.empty, 41);
	EXPECT_EQ(shades.life[0], 42);
	EXPECT_EQ(shades.life[1], 43) << "the life bar's middle is a bright green";
	EXPECT_GT(logical_palette[shades.life[1]].g, 2 * logical_palette[shades.life[1]].r);
	EXPECT_EQ(shades.mana[HeroManaRows - 1], 40 + static_cast<int>(OverheadBarTargetColours().size()) - 1);

	logical_palette[43] = { 0, 0, 0, 255 };
	EXPECT_NE(GetOverheadBarShades().life[1], 43) << "a new palette is matched again";
}

TEST(Poe2OverheadBarsTest, HeroBarShowsLifeWithManaUnder)
{
	UseBarPalette();
	const OverheadBarShades &shades = GetOverheadBarShades();
	EXPECT_EQ(HeroBarWidth, 44) << "about a tenth bigger than the old 40";
	OwnedSurface out(HeroBarWidth + 4, HeroBarHeight + 4);
	Fill(out, 0);
	OverheadBarValues values;
	values.life = 50;
	values.maxLife = 100;
	values.mana = 100;
	values.maxMana = 100;
	DrawHeroBar(out, { 0, 0 }, values, false);

	EXPECT_EQ(out[Point(0, 0)], shades.frame);
	for (int row = 0; row < HeroLifeRows; row++)
		EXPECT_EQ(out[Point(1, 1 + row)], shades.life[row]) << "each row its own shade";
	EXPECT_EQ(out[Point(HeroBarWidth - 2, 1)], shades.empty) << "half the life, half the bar";
	EXPECT_EQ(out[Point(1, HeroLifeRows + 1)], shades.empty) << "a dark line between life and mana";
	for (int row = 0; row < HeroManaRows; row++)
		EXPECT_EQ(out[Point(HeroBarWidth - 2, HeroLifeRows + 2 + row)], shades.mana[row]);
	EXPECT_EQ(out[Point(1, HeroBarHeight - 1)], shades.frame);
	EXPECT_EQ(out[Point(HeroBarWidth, 0)], 0) << "nothing outside the bar";
}

TEST(Poe2OverheadBarsTest, EnergyShieldWashesOverTheLifeBar)
{
	UseBarPalette();
	const OverheadBarShades &shades = GetOverheadBarShades();
	OwnedSurface out(HeroBarWidth, HeroBarHeight);
	OverheadBarValues values;
	values.life = 100;
	values.maxLife = 100;
	values.energyShield = 25;
	values.maxEnergyShield = 100;

	Fill(out, 0);
	DrawHeroBar(out, { 0, 0 }, values, false);
	EXPECT_EQ(out[Point(1, 1)], shades.life[0]) << "Energy Shield only shows with its option";

	Fill(out, 0);
	DrawHeroBar(out, { 0, 0 }, values, true);
	for (int row = 0; row < HeroLifeRows; row++)
		EXPECT_EQ(out[Point(1, 1 + row)], shades.energyShield[row]) << "the whole height of the life bar";
	EXPECT_EQ(out[Point(HeroBarWidth - 2, 1)], shades.life[0]) << "a quarter of the shield covers a quarter of the bar";

	values.life = 10;
	values.energyShield = 100;
	Fill(out, 0);
	DrawHeroBar(out, { 0, 0 }, values, true);
	EXPECT_EQ(out[Point(1, 2)], shades.energyShield[1]) << "over the life left";
	EXPECT_EQ(out[Point(HeroBarWidth - 2, 2)], shades.energyShieldOverEmpty[1]) << "and over the empty bar";

	values.maxEnergyShield = 0;
	Fill(out, 0);
	DrawHeroBar(out, { 0, 0 }, values, true);
	EXPECT_EQ(out[Point(1, 1)], shades.life[0]) << "no Energy Shield, nothing over the life";
}

TEST(Poe2OverheadBarsTest, HeroBarIsDrawnInFullColourOverTheWorld)
{
	UseBarPalette();
	OwnedSurface out(HeroBarWidth, HeroBarHeight);
	Fill(out, 0);
	std::vector<UiCover> cells(static_cast<size_t>(HeroBarWidth) * HeroBarHeight);
	std::vector<UiTint> tints(cells.size());
	SetUiCoverTarget(out.surface, cells.data(), tints.data());
	OverheadBarValues values;
	values.life = 100;
	values.maxLife = 100;
	DrawHeroBar(out, { 0, 0 }, values, false);
	SetUiCoverTarget(nullptr, nullptr);

	const UiTint &bright = tints[(2 * static_cast<size_t>(HeroBarWidth)) + 1];
	EXPECT_TRUE(bright.trueColour) << "the palette has no true green, so the layer over the world draws it";
	EXPECT_EQ(bright.alpha, 255);
	EXPECT_GT(bright.g, 2 * bright.r);
	EXPECT_GT(bright.g, 2 * bright.b);
	EXPECT_EQ(bright.color, out[Point(1, 2)]) << "the palette's nearest still drawn under it";
}

void MakeMonster(Monster &monster, uint32_t seed)
{
	LevelMonsterTypes[0].type = MT_NZOMBIE;
	monster.levelType = 0;
	monster.uniqueType = UniqueMonsterType::None;
	monster.flags = 0;
	monster.isInvalid = false;
	monster.mode = MonsterMode::Stand;
	monster.rndItemSeed = seed;
	monster.maxHitPoints = Fixed26_6::fromInt(100);
	monster.hitPoints = monster.maxHitPoints;
}

MonsterRarity RarityOf(uint32_t seed)
{
	Monster monster {};
	MakeMonster(monster, seed);
	return GetMonsterAffixes(monster).rarity;
}

uint8_t FrameColorOf(uint32_t seed, UniqueMonsterType unique = UniqueMonsterType::None)
{
	Monster monster {};
	MakeMonster(monster, seed);
	monster.uniqueType = unique;
	return MonsterBarFrameColor(monster);
}

TEST(Poe2OverheadBarsTest, OnlyHurtEnemiesGetABar)
{
	Monster monster {};
	MakeMonster(monster, 1);
	EXPECT_FALSE(IsHurtEnemyForBar(monster)) << "not hurt yet";
	monster.hitPoints = Fixed26_6::fromInt(60);
	EXPECT_TRUE(IsHurtEnemyForBar(monster));

	monster.mode = MonsterMode::Death;
	EXPECT_FALSE(IsHurtEnemyForBar(monster));
	monster.mode = MonsterMode::Stand;
	monster.hitPoints = Fixed26_6::fromInt(0);
	EXPECT_FALSE(IsHurtEnemyForBar(monster));
	monster.hitPoints = Fixed26_6::fromInt(60);

	monster.flags = MFLAG_GOLEM;
	EXPECT_FALSE(IsHurtEnemyForBar(monster)) << "the hero's own minions are allies";
	monster.flags = MFLAG_GOLEM | MFLAG_BERSERK;
	EXPECT_TRUE(IsHurtEnemyForBar(monster)) << "a berserk golem is an enemy";

	monster.flags = MFLAG_HIDDEN;
	EXPECT_FALSE(IsHurtEnemyForBar(monster));
}

TEST(Poe2OverheadBarsTest, BarFrameShowsTheRarity)
{
	gbIsPoe2 = true;
	uint32_t rareSeed = 0;
	uint32_t magicSeed = 0;
	uint32_t normalSeed = 0;
	for (uint32_t seed = 1; seed < 100000 && (rareSeed == 0 || magicSeed == 0 || normalSeed == 0); seed++) {
		switch (RarityOf(seed)) {
		case MonsterRarity::Rare:
			rareSeed = rareSeed == 0 ? seed : rareSeed;
			break;
		case MonsterRarity::Magic:
			magicSeed = magicSeed == 0 ? seed : magicSeed;
			break;
		default:
			normalSeed = normalSeed == 0 ? seed : normalSeed;
			break;
		}
	}
	EXPECT_EQ(FrameColorOf(rareSeed), RareMonsterBarFrameColor);
	EXPECT_EQ(FrameColorOf(magicSeed), MagicMonsterBarFrameColor);
	EXPECT_EQ(FrameColorOf(normalSeed), NormalMonsterBarFrameColor);
	EXPECT_EQ(FrameColorOf(normalSeed, UniqueMonsterType::Garbud), UniqueMonsterBarFrameColor);
	gbIsPoe2 = false;
}

TEST(Poe2OverheadBarsTest, MonsterBarShowsItsLifeInsideTheFrame)
{
	OwnedSurface out(MonsterBarWidth, MonsterBarHeight);
	Fill(out, 0);
	OverheadBarValues values;
	values.life = 1;
	values.maxLife = 4;
	DrawMonsterBar(out, { 0, 0 }, MonsterBarWidth, values, RareMonsterBarFrameColor);
	EXPECT_EQ(out[Point(0, 0)], RareMonsterBarFrameColor);
	EXPECT_EQ(out[Point(MonsterBarWidth - 1, MonsterBarHeight - 1)], RareMonsterBarFrameColor);
	EXPECT_EQ(out[Point(1, 1)], OverheadBarLifeColor);
	EXPECT_EQ(out[Point(1, MonsterLifeRows)], OverheadBarLifeColor);
	EXPECT_EQ(out[Point(MonsterBarWidth / 2, 1)], GetOverheadBarShades().empty) << "a quarter of the life, a quarter of the bar";
}

} // namespace
} // namespace devilution
