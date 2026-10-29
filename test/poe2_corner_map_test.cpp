#include <gtest/gtest.h>

#include <algorithm>

#include "automap.h"
#include "engine/assets.hpp"
#include "engine/render/automap_render.hpp"
#include "engine/surface.hpp"
#include "game_mode.hpp"
#include "levels/gendung.h"
#include "levels/trigs.h"
#include "missiles.h"
#include "monster.h"
#include "objects.h"
#include "options.h"
#include "poe2/corner_map.hpp"
#include "utils/palette_blending.hpp"

namespace devilution {
namespace {

void Fill(const Surface &surface, uint8_t color)
{
	for (int y = 0; y < surface.h(); y++)
		std::fill_n(surface.at(0, y), surface.w(), color);
}

class Poe2CornerMapTest : public ::testing::Test {
public:
	void SetUp() override
	{
		gbIsPoe2 = true;
		AutomapActive = false;
		GameplayOptions &gameplay = GetOptions().Gameplay;
		gameplay.showCornerMap.SetValue(true);
		gameplay.mapTransparency.SetValue(25);
		gameplay.landscapeTransparency.SetValue(50);
		gameplay.mapZoom.SetValue(1);
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		AutomapActive = false;
		SetMapLineOpacity(100);
		numtrigs = 0;
		leveltype = DTYPE_TOWN;
		currlevel = 0;
		std::fill_n(&AutomapView[0][0], DMAXX * DMAXY, static_cast<uint8_t>(MAP_EXP_NONE));
	}
};

TEST_F(Poe2CornerMapTest, SitsInTheTopRightCornerAFifthOfTheScreenWide)
{
	const Rectangle medium = CornerMapRect(640, CornerMapSize::Medium);
	EXPECT_EQ(medium.size.width, 128);
	EXPECT_EQ(medium.size.height, 85) << "two thirds as high as it is wide";
	EXPECT_EQ(medium.position.x + medium.size.width, 640 - CornerMapMargin);
	EXPECT_EQ(medium.position.y, CornerMapMargin);

	EXPECT_EQ(CornerMapRect(640, CornerMapSize::Small).size.width, 96);
	EXPECT_EQ(CornerMapRect(640, CornerMapSize::Large).size.width, 160);
	// UI Scale 150 on a 1920 by 1080 window leaves a 1280 wide UI.
	const Rectangle scaled = CornerMapRect(1280, CornerMapSize::Medium);
	EXPECT_EQ(scaled.size.width, 256);
	EXPECT_EQ(scaled.position.x + scaled.size.width, 1280 - CornerMapMargin);
}

TEST_F(Poe2CornerMapTest, ZoomScalesWithTheScreenLikeDiablosMinimap)
{
	EXPECT_EQ(CornerMapScale(1, 480), 25);
	EXPECT_EQ(CornerMapScale(2, 480), 50);
	EXPECT_EQ(CornerMapScale(4, 720), 100) << "720 lines is still one step of 480";
	EXPECT_EQ(CornerMapScale(1, 1080), 50);
	EXPECT_EQ(CornerMapScale(0, 480), 25) << "out of range levels stay in range";
	EXPECT_EQ(CornerMapScale(9, 480), 100);
	EXPECT_EQ(CornerMapScale(1, 100000), 200) << "as large as the minimap gets";
	for (int level = 1; level <= 4; level++)
		EXPECT_EQ(CornerMapScale(level, 1440) % 25, 0) << "the map's lines are drawn in steps of 25";
}

TEST_F(Poe2CornerMapTest, RecentresOnlyWhenTheHeroNearsTheEdge)
{
	const Size map { 128, 85 };
	// At scale 25 a tile is 8 by 4 pixels; a third of the map is 42 pixels across and 28 down.
	EXPECT_FALSE(CornerMapNeedsRecentre({ 0, 0 }, 25, map));
	EXPECT_FALSE(CornerMapNeedsRecentre({ 5, -5 }, 25, map)) << "40 pixels across is under a third";
	EXPECT_TRUE(CornerMapNeedsRecentre({ 6, -6 }, 25, map));
	EXPECT_FALSE(CornerMapNeedsRecentre({ 7, 7 }, 25, map)) << "28 pixels down is under a third of 85";
	EXPECT_TRUE(CornerMapNeedsRecentre({ 8, 8 }, 25, map));
	EXPECT_TRUE(CornerMapNeedsRecentre({ 3, -3 }, 50, map)) << "closer zoom, sooner";
}

TEST_F(Poe2CornerMapTest, ZoomStepsStayInTheList)
{
	EXPECT_EQ(StepListIndex(0, 4, 1), 1u);
	EXPECT_EQ(StepListIndex(3, 4, 1), 3u);
	EXPECT_EQ(StepListIndex(2, 4, -1), 1u);
	EXPECT_EQ(StepListIndex(0, 4, -1), 0u);
	EXPECT_EQ(StepListIndex(0, 0, 1), 0u);
}

TEST_F(Poe2CornerMapTest, ZoomKeysStepTheOptionWhileTheTabMapIsClosed)
{
	auto &zoom = GetOptions().Gameplay.mapZoom;
	EXPECT_TRUE(CornerMapZoomKey(1));
	EXPECT_EQ(*zoom, 2);
	EXPECT_TRUE(CornerMapZoomKey(1));
	EXPECT_TRUE(CornerMapZoomKey(1));
	EXPECT_TRUE(CornerMapZoomKey(1));
	EXPECT_EQ(*zoom, 4) << "stops at the closest zoom";
	EXPECT_TRUE(CornerMapZoomKey(-1));
	EXPECT_EQ(*zoom, 3);

	AutomapActive = true;
	EXPECT_FALSE(CornerMapZoomKey(-1)) << "the Tab map zooms itself";
	EXPECT_EQ(*zoom, 3);
	AutomapActive = false;
	gbIsPoe2 = false;
	EXPECT_FALSE(CornerMapZoomKey(-1));
	EXPECT_EQ(*zoom, 3);
}

TEST_F(Poe2CornerMapTest, ShowsOnlyInPoe2WithTheOptionOnAndTheTabMapClosed)
{
	EXPECT_TRUE(CornerMapShown());
	AutomapActive = true;
	EXPECT_FALSE(CornerMapShown());
	AutomapActive = false;
	GetOptions().Gameplay.showCornerMap.SetValue(false);
	EXPECT_FALSE(CornerMapShown());
	GetOptions().Gameplay.showCornerMap.SetValue(true);
	gbIsPoe2 = false;
	EXPECT_FALSE(CornerMapShown());
}

TEST_F(Poe2CornerMapTest, TransparencyOptionsSetTheOpacities)
{
	EXPECT_EQ(MapOpacityPercent(), 75);
	EXPECT_EQ(LandscapeOpacityPercent(), 37) << "the floor fades with the lines too";
	GetOptions().Gameplay.landscapeTransparency.SetValue(100);
	EXPECT_EQ(LandscapeOpacityPercent(), 0);
	GetOptions().Gameplay.mapTransparency.SetValue(0);
	GetOptions().Gameplay.landscapeTransparency.SetValue(0);
	EXPECT_EQ(MapOpacityPercent(), 100);
	EXPECT_EQ(LandscapeOpacityPercent(), 100);

	gbIsPoe2 = false;
	EXPECT_EQ(MapOpacityPercent(), 100) << "Diablo's map is unchanged";
	EXPECT_EQ(LandscapeOpacityPercent(), 0);
}

TEST_F(Poe2CornerMapTest, BlendingGoesInQuarterSteps)
{
	// A stand-in palette where blending two colours gives the one halfway between them.
	for (int a = 0; a < 256; a++) {
		for (int b = 0; b < 256; b++)
			paletteTransparencyLookup[a][b] = static_cast<uint8_t>((a + b) / 2);
	}
	EXPECT_EQ(BlendMapColor(200, 100, 100), 200);
	EXPECT_EQ(BlendMapColor(200, 100, 75), 175);
	EXPECT_EQ(BlendMapColor(200, 100, 50), 150);
	EXPECT_EQ(BlendMapColor(200, 100, 25), 125);
	EXPECT_EQ(BlendMapColor(200, 100, 0), 100);

	OwnedSurface out(4, 4);
	Fill(out, 100);
	SetAutomapType(AutomapType::Opaque);
	SetMapLineOpacity(50);
	SetMapPixel(out, { 1, 1 }, 200);
	EXPECT_EQ(*out.at(1, 1), 150);
	SetMapLineOpacity(100);
	SetMapPixel(out, { 2, 2 }, 200);
	EXPECT_EQ(*out.at(2, 2), 200) << "at full opacity the line is drawn as Diablo draws it";
	SetMapLineOpacity(150);
	EXPECT_EQ(GetMapLineOpacity(), 100);
}

TEST_F(Poe2CornerMapTest, MarkerShapes)
{
	EXPECT_TRUE(IsInMapIconShape(MapIconShape::Diamond, 3, 0, 3));
	EXPECT_FALSE(IsInMapIconShape(MapIconShape::Diamond, 2, 2, 3));
	EXPECT_TRUE(IsInMapIconShape(MapIconShape::Square, 2, -2, 3));
	EXPECT_FALSE(IsInMapIconShape(MapIconShape::Square, 3, 0, 3));
	EXPECT_TRUE(IsInMapIconShape(MapIconShape::Circle, 2, 2, 3));
	EXPECT_FALSE(IsInMapIconShape(MapIconShape::Circle, 3, 3, 3));
	EXPECT_TRUE(IsInMapIconShape(MapIconShape::TriangleUp, 0, -3, 3)) << "the point at the top";
	EXPECT_FALSE(IsInMapIconShape(MapIconShape::TriangleUp, 2, -3, 3));
	EXPECT_TRUE(IsInMapIconShape(MapIconShape::TriangleUp, 3, 3, 3)) << "the flat side at the bottom";
	EXPECT_TRUE(IsInMapIconShape(MapIconShape::TriangleDown, 0, 3, 3));
	EXPECT_FALSE(IsInMapIconShape(MapIconShape::TriangleDown, 2, 3, 3));
	EXPECT_TRUE(IsInMapIconShape(MapIconShape::Cross, 0, 3, 3));
	EXPECT_FALSE(IsInMapIconShape(MapIconShape::Cross, 2, 2, 3));
	EXPECT_FALSE(IsInMapIconShape(MapIconShape::Diamond, 4, 0, 3));

	EXPECT_EQ(MapIconRadius(25), 2);
	EXPECT_EQ(MapIconRadius(50), 3);
	EXPECT_EQ(MapIconRadius(200), 5);
}

TEST_F(Poe2CornerMapTest, MarkerIsDrawnWithAnOutlineInsideTheMap)
{
	OwnedSurface out(20, 20);
	Fill(out, 5);
	SetMapLineOpacity(100);
	const MapIconStyle style { MapIconShape::Square, 200, "" };
	DrawMapIcon(out, { 10, 10 }, style, 3, Rectangle { { 0, 0 }, { 11, 20 } });
	EXPECT_EQ(*out.at(10, 10), 200);
	EXPECT_EQ(*out.at(8, 12), 200);
	EXPECT_EQ(*out.at(10, 13), 0) << "a dark outline";
	EXPECT_EQ(*out.at(10, 15), 5);
	EXPECT_EQ(*out.at(11, 10), 5) << "nothing outside the map";
}

TEST_F(Poe2CornerMapTest, LabelsFillInTheName)
{
	EXPECT_EQ(MapIconLabel({ MapIconShape::Circle, 0, "{name}" }, "The Butcher"), "The Butcher");
	EXPECT_EQ(MapIconLabel({ MapIconShape::Circle, 0, "Portal" }, "ignored"), "Portal");
	EXPECT_EQ(MapIconLabel({ MapIconShape::Circle, 0, "" }, "Bonehead"), "");
	EXPECT_EQ(MapIconFromKey("strongbox"), MapIcon::Strongbox);
	EXPECT_EQ(MapIconFromKey("unique_monster"), MapIcon::UniqueMonster);
	EXPECT_EQ(MapIconFromKey("dragon"), std::nullopt);
}

TEST_F(Poe2CornerMapTest, IconTableLoadsFromThePoe2Mod)
{
	UnloadModArchives();
	LoadModArchives({ { "hf", "poe2" } });
	LoadMapIcons();
	EXPECT_EQ(GetMapIconStyle(MapIcon::StairsDown).shape, MapIconShape::TriangleDown);
	EXPECT_EQ(GetMapIconStyle(MapIcon::StairsDown).label, "Down");
	EXPECT_EQ(GetMapIconStyle(MapIcon::Strongbox).shape, MapIconShape::Square);
	EXPECT_EQ(GetMapIconStyle(MapIcon::Strongbox).color, PAL8_ORANGE + 1);
	EXPECT_EQ(GetMapIconStyle(MapIcon::Shrine).shape, MapIconShape::Cross);
	EXPECT_EQ(GetMapIconStyle(MapIcon::UniqueMonster).label, "{name}");
	EXPECT_EQ(GetMapIconStyle(MapIcon::RareMonster).label, "") << "\"-\" in the table shows no name";
	EXPECT_EQ(GetMapIconStyle(MapIcon::Portal).color, PAL8_BLUE + 1);
	EXPECT_EQ(GetMapIconStyle(MapIcon::RedPortal).color, PAL8_RED + 1);
	UnloadModArchives();
}

TEST_F(Poe2CornerMapTest, MarkersShowOnlyAboutAScreenFromTheHero)
{
	EXPECT_EQ(MapMarkerRange, 15);
	EXPECT_TRUE(IsInMapMarkerRange({ 50, 50 }, { 50, 50 }));
	EXPECT_TRUE(IsInMapMarkerRange({ 50, 50 }, { 65, 50 }));
	EXPECT_FALSE(IsInMapMarkerRange({ 50, 50 }, { 66, 50 }));
	EXPECT_TRUE(IsInMapMarkerRange({ 50, 50 }, { 40, 39 })) << "10 and 11 steps is under 15 in a straight line";
	EXPECT_FALSE(IsInMapMarkerRange({ 50, 50 }, { 61, 61 })) << "11 steps each way is over 15 in a straight line";
}

TEST_F(Poe2CornerMapTest, ExitsShowOnceSeenAndOnlyNearTheHero)
{
	Missiles.clear();
	ActiveObjectCount = 0;
	ActiveMonsterCount = 0;
	numtrigs = 2;
	trigs[0] = { { 40, 50 }, WM_DIABNEXTLVL, 0 };
	trigs[1] = { { 60, 30 }, WM_DIABPREVLVL, 0 };

	leveltype = DTYPE_TOWN;
	EXPECT_TRUE(CollectMapMarkers({ 80, 80 }).empty()) << "town's exits are far away too";
	std::vector<MapMarker> markers = CollectMapMarkers({ 50, 40 });
	ASSERT_EQ(markers.size(), 2u) << "town needs no exploring";
	EXPECT_EQ(markers[0].icon, MapIcon::StairsDown);
	EXPECT_EQ(markers[0].tile, Point(40, 50));
	EXPECT_EQ(markers[1].icon, MapIcon::Town) << "stairs up to level 0 lead to town";
	markers = CollectMapMarkers({ 40, 60 });
	ASSERT_EQ(markers.size(), 1u);
	EXPECT_EQ(markers[0].icon, MapIcon::StairsDown);

	leveltype = DTYPE_CATHEDRAL;
	currlevel = 1;
	EXPECT_TRUE(CollectMapMarkers({ 50, 40 }).empty()) << "not seen yet";
	AutomapView[(40 - 16) / 2][(50 - 16) / 2] = MAP_EXP_SELF;
	trigs[1]._tlvl = 1;
	AutomapView[(60 - 16) / 2][(30 - 16) / 2] = MAP_EXP_SHRINE;
	markers = CollectMapMarkers({ 50, 40 });
	ASSERT_EQ(markers.size(), 2u);
	EXPECT_EQ(markers[1].icon, MapIcon::StairsUp);
	markers = CollectMapMarkers({ 40, 64 });
	ASSERT_EQ(markers.size(), 1u) << "seen, but the hero has walked away";
	EXPECT_EQ(markers[0].icon, MapIcon::StairsDown);
	EXPECT_TRUE(CollectMapMarkers({ 20, 90 }).empty());
}

TEST_F(Poe2CornerMapTest, YourOwnTownPortalShowsFromAnywhere)
{
	numtrigs = 0;
	ActiveObjectCount = 0;
	ActiveMonsterCount = 0;
	Missiles.clear();
	Missile &mine = Missiles.emplace_back();
	mine._mitype = MissileID::TownPortal;
	mine._misource = MyPlayerId;
	mine.position.tile = { 30, 30 };
	Missile &theirs = Missiles.emplace_back();
	theirs._mitype = MissileID::TownPortal;
	theirs._misource = MyPlayerId + 1;
	theirs.position.tile = { 70, 70 };

	std::vector<MapMarker> markers = CollectMapMarkers({ 70, 75 });
	ASSERT_EQ(markers.size(), 2u);
	EXPECT_EQ(markers[0].tile, Point(30, 30)) << "your own, far away";
	EXPECT_EQ(markers[1].tile, Point(70, 70));
	markers = CollectMapMarkers({ 30, 35 });
	ASSERT_EQ(markers.size(), 1u) << "another hero's portal only up close";
	EXPECT_EQ(markers[0].icon, MapIcon::Portal);
	EXPECT_EQ(markers[0].tile, Point(30, 30));
	Missiles.clear();
}

} // namespace
} // namespace devilution
