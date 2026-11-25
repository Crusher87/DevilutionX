#include <gtest/gtest.h>

#include "control/control.hpp"
#include "diablo.h"
#include "engine/render/scrollrt.h"
#include "options.h"
#include "utils/ui_fwd.h"

using namespace devilution;

// TilesInView

TEST(Scroll_rt, calc_tiles_in_view_original)
{
	gnScreenWidth = 640;
	gnScreenHeight = 480;
	gnViewportHeight = gnScreenHeight - 128;
	GetOptions().Graphics.zoom.SetValue(false);
	int columns = 0;
	int rows = 0;
	TilesInView(&columns, &rows);
	EXPECT_EQ(columns, 10);
	EXPECT_EQ(rows, 11);
}

TEST(Scroll_rt, calc_tiles_in_view_original_zoom)
{
	gnScreenWidth = 640;
	gnScreenHeight = 480;
	gnViewportHeight = gnScreenHeight - 128;
	GetOptions().Graphics.zoom.SetValue(true);
	int columns = 0;
	int rows = 0;
	TilesInView(&columns, &rows);
	EXPECT_EQ(columns, 5);
	EXPECT_EQ(rows, 6);
}

TEST(Scroll_rt, calc_tiles_in_view_960_540)
{
	gnScreenWidth = 960;
	gnScreenHeight = 540;
	gnViewportHeight = gnScreenHeight;
	GetOptions().Graphics.zoom.SetValue(false);
	int columns = 0;
	int rows = 0;
	TilesInView(&columns, &rows);
	EXPECT_EQ(columns, 15);
	EXPECT_EQ(rows, 17);
}

TEST(Scroll_rt, calc_tiles_in_view_640_512)
{
	gnScreenWidth = 640;
	gnScreenHeight = 512;
	gnViewportHeight = gnScreenHeight - 128;
	GetOptions().Graphics.zoom.SetValue(false);
	int columns = 0;
	int rows = 0;
	TilesInView(&columns, &rows);
	EXPECT_EQ(columns, 10);
	EXPECT_EQ(rows, 12);
}

TEST(Scroll_rt, calc_tiles_in_view_768_480_zoom)
{
	gnScreenWidth = 768;
	gnScreenHeight = 480;
	gnViewportHeight = gnScreenHeight;
	GetOptions().Graphics.zoom.SetValue(true);
	int columns = 0;
	int rows = 0;
	TilesInView(&columns, &rows);
	EXPECT_EQ(columns, 6);
	EXPECT_EQ(rows, 8);
}

// CalcTileOffset

TEST(Scroll_rt, calc_tile_offset_original)
{
	gnScreenWidth = 640;
	gnScreenHeight = 480;
	gnViewportHeight = gnScreenHeight - 128;
	GetOptions().Graphics.zoom.SetValue(false);
	int x = 0;
	int y = 0;
	CalcTileOffset(&x, &y);
	EXPECT_EQ(x, 0);
	EXPECT_EQ(y, 0);
}

TEST(Scroll_rt, calc_tile_offset_original_zoom)
{
	gnScreenWidth = 640;
	gnScreenHeight = 480;
	gnViewportHeight = gnScreenHeight - 128;
	GetOptions().Graphics.zoom.SetValue(true);
	int x = 0;
	int y = 0;
	CalcTileOffset(&x, &y);
	EXPECT_EQ(x, 0);
	EXPECT_EQ(y, 8);
}

TEST(Scroll_rt, calc_tile_offset_960_540)
{
	gnScreenWidth = 960;
	gnScreenHeight = 540;
	gnViewportHeight = gnScreenHeight;
	GetOptions().Graphics.zoom.SetValue(false);
	int x = 0;
	int y = 0;
	CalcTileOffset(&x, &y);
	EXPECT_EQ(x, 0);
	EXPECT_EQ(y, 2);
}

TEST(Scroll_rt, calc_tile_offset_853_480)
{
	gnScreenWidth = 853;
	gnScreenHeight = 480;
	gnViewportHeight = gnScreenHeight;
	GetOptions().Graphics.zoom.SetValue(false);
	int x = 0;
	int y = 0;
	CalcTileOffset(&x, &y);
	EXPECT_EQ(x, 21);
	EXPECT_EQ(y, 0);
}

TEST(Scroll_rt, calc_tile_offset_768_480_zoom)
{
	gnScreenWidth = 768;
	gnScreenHeight = 480;
	gnViewportHeight = gnScreenHeight;
	GetOptions().Graphics.zoom.SetValue(true);
	int x = 0;
	int y = 0;
	CalcTileOffset(&x, &y);
	EXPECT_EQ(x, 0);
	EXPECT_EQ(y, 8);
}

// RowsCoveredByPanel

TEST(Scroll_rt, calc_tiles_covered_by_panel_original)
{
	gnScreenWidth = 640;
	GetOptions().Graphics.zoom.SetValue(false);
	CalculatePanelAreas();
	EXPECT_EQ(RowsCoveredByPanel(), 0);
}

TEST(Scroll_rt, calc_tiles_covered_by_panel_960)
{
	gnScreenWidth = 960;
	GetOptions().Graphics.zoom.SetValue(false);
	CalculatePanelAreas();
	EXPECT_EQ(RowsCoveredByPanel(), 4);
}

TEST(Scroll_rt, calc_tiles_covered_by_panel_960_zoom)
{
	gnScreenWidth = 960;
	GetOptions().Graphics.zoom.SetValue(true);
	CalculatePanelAreas();
	EXPECT_EQ(RowsCoveredByPanel(), 2);
}

// POE2 Mashup: the world kept under the game menu (KeepWorldPicture).

namespace {

WorldStillness StillUnderMenu()
{
	return WorldStillness {
		.gameMenuOpen = true,
		.multiplayer = false,
		.heroDead = false,
		.redrawAll = false,
		.layerKeepsWorld = true,
	};
}

WorldPictureKey SomeView()
{
	WorldPictureKey key;
	key.view = { 40, 50 };
	key.offset = { 16, -8 };
	key.screenWidth = 1966;
	key.screenHeight = 823;
	key.renderWidth = 3440;
	key.renderHeight = 1440;
	key.viewportHeight = 823;
	key.worldLayer = 3;
	key.options = 7;
	key.menu = &key;
	key.renderer = &key.view;
	key.level = 2;
	key.levelType = 1;
	key.zoom = true;
	key.hd = true;
	key.effects = true;
	return key;
}

} // namespace

TEST(Scroll_rt, kept_world_shown_again_while_menu_holds_the_game)
{
	const WorldPictureKey view = SomeView();
	EXPECT_TRUE(WorldStandsStill(StillUnderMenu()));
	EXPECT_TRUE(MayShowKeptWorld(StillUnderMenu(), view, view));
}

TEST(Scroll_rt, first_frame_under_menu_draws_the_world)
{
	EXPECT_FALSE(MayShowKeptWorld(StillUnderMenu(), std::nullopt, SomeView()));
}

TEST(Scroll_rt, world_drawn_when_it_does_not_stand_still)
{
	const WorldPictureKey view = SomeView();
	const auto with = [](void (*change)(WorldStillness &)) {
		WorldStillness world = StillUnderMenu();
		change(world);
		return world;
	};
	// A multiplayer game goes on under the menu.
	EXPECT_FALSE(WorldStandsStill(with([](WorldStillness &w) { w.multiplayer = true; })));
	EXPECT_FALSE(MayShowKeptWorld(with([](WorldStillness &w) { w.multiplayer = true; }), view, view));
	// The menu closed: the game goes on.
	EXPECT_FALSE(MayShowKeptWorld(with([](WorldStillness &w) { w.gameMenuOpen = false; }), view, view));
	EXPECT_FALSE(MayShowKeptWorld(with([](WorldStillness &w) { w.heroDead = true; }), view, view));
	// The window shown again, a display option changed.
	EXPECT_FALSE(MayShowKeptWorld(with([](WorldStillness &w) { w.redrawAll = true; }), view, view));
	// The UI drawn into the world's frame (no layers): the world isn't there to show again.
	EXPECT_FALSE(MayShowKeptWorld(with([](WorldStillness &w) { w.layerKeepsWorld = false; }), view, view));
}

TEST(Scroll_rt, world_drawn_again_when_the_view_changes)
{
	const WorldPictureKey kept = SomeView();
	const auto changed = [&](void (*change)(WorldPictureKey &)) {
		WorldPictureKey now = kept;
		change(now);
		return MayShowKeptWorld(StillUnderMenu(), kept, now);
	};
	static int other;
	EXPECT_FALSE(changed([](WorldPictureKey &k) { k.view.x++; }));
	EXPECT_FALSE(changed([](WorldPictureKey &k) { k.offset.deltaY++; }));
	EXPECT_FALSE(changed([](WorldPictureKey &k) { k.screenWidth = 1720; }));
	EXPECT_FALSE(changed([](WorldPictureKey &k) { k.screenHeight = 720; }));
	EXPECT_FALSE(changed([](WorldPictureKey &k) { k.renderWidth = 2560; }));
	EXPECT_FALSE(changed([](WorldPictureKey &k) { k.renderHeight = 1080; }));
	EXPECT_FALSE(changed([](WorldPictureKey &k) { k.viewportHeight = 700; }));
	// The world's frame made anew (UI scale, HD or effects switched at UI Scale 100).
	EXPECT_FALSE(changed([](WorldPictureKey &k) { k.worldLayer++; }));
	// An option changed in the menu (brightness, volume, HD Graphics...).
	EXPECT_FALSE(changed([](WorldPictureKey &k) { k.options++; }));
	// A sub-menu opened or closed.
	EXPECT_FALSE(changed([](WorldPictureKey &k) { k.menu = &other; }));
	// The renderer made anew.
	EXPECT_FALSE(changed([](WorldPictureKey &k) { k.renderer = &other; }));
	EXPECT_FALSE(changed([](WorldPictureKey &k) { k.level++; }));
	EXPECT_FALSE(changed([](WorldPictureKey &k) { k.levelType++; }));
	EXPECT_FALSE(changed([](WorldPictureKey &k) { k.setLevel = 4; }));
	EXPECT_FALSE(changed([](WorldPictureKey &k) { k.zoom = false; }));
	EXPECT_FALSE(changed([](WorldPictureKey &k) { k.hd = false; }));
	EXPECT_FALSE(changed([](WorldPictureKey &k) { k.effects = false; }));
	EXPECT_FALSE(changed([](WorldPictureKey &k) { k.leftPanel = true; }));
	EXPECT_FALSE(changed([](WorldPictureKey &k) { k.rightPanel = true; }));
	EXPECT_FALSE(changed([](WorldPictureKey &k) { k.floorCopy = true; }));
}

TEST(Scroll_rt, option_change_is_counted)
{
	const uint32_t before = OptionChangeCount();
	GetOptions().Graphics.zoom.SetValue(!*GetOptions().Graphics.zoom);
	EXPECT_NE(OptionChangeCount(), before);
}
