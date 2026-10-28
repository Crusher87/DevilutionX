#include <optional>

#include <gtest/gtest.h>

#include "control/control.hpp"
#include "cursor.h"
#include "diablo.h"
#include "engine/ui_scale.hpp"
#include "inv.h"
#include "minitext.h"
#include "player.h"
#include "panels/skill_bar.hpp"
#include "poe2/hud_globes.hpp"
#include "utils/ui_fwd.h"

namespace devilution {
namespace {

class Poe2UiScaleTest : public ::testing::Test {
public:
	void TearDown() override
	{
		SetRenderSize({ 640, 480 }, 100, true);
		invflag = false;
		CharFlag = false;
	}

	static void Apply(Size render, int scale)
	{
		const Size ui = SetRenderSize(render, scale, true);
		gnScreenWidth = ui.width;
		gnScreenHeight = ui.height;
		gnViewportHeight = ui.height;
	}
};

TEST_F(Poe2UiScaleTest, UiShrinksAndTheWorldKeepsTheFullSize)
{
	Apply({ 3440, 1440 }, 150);
	EXPECT_TRUE(IsUiScaled());
	EXPECT_EQ(gnScreenWidth, 2293);
	EXPECT_EQ(gnScreenHeight, 960);
	EXPECT_EQ(gnRenderWidth, 3440);
	EXPECT_EQ(gnRenderHeight, 1440);
}

TEST_F(Poe2UiScaleTest, TheUiStaysAtLeast640x480)
{
	Apply({ 800, 600 }, 200);
	EXPECT_EQ(ActiveUiScale(), 125);
	EXPECT_EQ(gnScreenWidth, 640);
	EXPECT_EQ(gnScreenHeight, 480);

	Apply({ 640, 480 }, 150);
	EXPECT_FALSE(IsUiScaled());
}

TEST_F(Poe2UiScaleTest, NoScalingWithoutTheRenderer)
{
	EXPECT_EQ(SetRenderSize({ 1920, 1080 }, 200, false).width, 1920);
	EXPECT_FALSE(IsUiScaled());
}

TEST_F(Poe2UiScaleTest, WorldSpaceSwapsSizesAndTheMouse)
{
	Apply({ 1920, 1080 }, 200);
	MousePosition = { 480, 270 };
	{
		const WorldSpaceScope world;
		EXPECT_EQ(gnScreenWidth, 1920);
		EXPECT_EQ(gnViewportHeight, 1080);
		EXPECT_EQ(MousePosition, Point(960, 540));
		EXPECT_EQ(WorldToUi({ 960, 540 }), Point(480, 270));
		{
			const WorldSpaceScope nested;
			EXPECT_EQ(MousePosition, Point(960, 540));
		}
		EXPECT_EQ(gnScreenWidth, 1920);
	}
	EXPECT_EQ(gnScreenWidth, 960);
	EXPECT_EQ(MousePosition, Point(480, 270));
	EXPECT_EQ(UiToWorld({ 100, 50 }), Point(200, 100));
	EXPECT_EQ(UiToWorldHeight(60), 120);
}

TEST_F(Poe2UiScaleTest, SkillTraySitsBesideTheManaGlobe)
{
	const auto overlaps = [](const Rectangle &a, const Rectangle &b) {
		return a.position.x < b.position.x + b.size.width && b.position.x < a.position.x + a.size.width
		    && a.position.y < b.position.y + b.size.height && b.position.y < a.position.y + a.size.height;
	};
	for (const Size render : { Size { 640, 480 }, Size { 800, 600 }, Size { 1280, 720 }, Size { 1920, 1080 }, Size { 2560, 1440 }, Size { 3440, 1440 } }) {
		for (const int scale : { 100, 125, 150, 175, 200 }) {
			Apply(render, scale);
			CalculatePanelAreas();
			const Rectangle &panel = GetMainPanel();
			SCOPED_TRACE(testing::Message() << render.width << "x" << render.height << " at " << scale << "%");

			// Curtis 2026-10-22: like POE2's skill bar, on the main panel just left of the mana globe, clear of its dome.
			invflag = false;
			CharFlag = false;
			std::optional<Rectangle> tray = SkillTrayRect();
			ASSERT_TRUE(tray.has_value());
			EXPECT_GE(tray->position.x, panel.position.x);
			EXPECT_LE(tray->position.x + tray->size.width, panel.position.x + ManaGlobeX);
			EXPECT_LE(tray->position.y + tray->size.height, panel.position.y);
			EXPECT_GE(tray->position.y, 0);

			// The inventory never hides it: it moves out from under it where the screen is small.
			invflag = true;
			tray = SkillTrayRect();
			ASSERT_TRUE(tray.has_value());
			EXPECT_FALSE(overlaps(*tray, GetRightPanel()));
			EXPECT_GE(tray->position.y, 0);

			// With the character sheet open too, it shows only where neither covers it.
			CharFlag = true;
			if (tray = SkillTrayRect(); tray.has_value()) {
				EXPECT_FALSE(overlaps(*tray, GetRightPanel()));
				EXPECT_FALSE(overlaps(*tray, GetLeftPanel()));
			}
		}
	}
}

TEST_F(Poe2UiScaleTest, NothingInTheWorldIsHoveredThroughTheUi)
{
	// Curtis 2026-10-28: a ground potion's tooltip showed through the open skill tray popup.
	Apply({ 640, 480 }, 100);
	CalculatePanelAreas();
	Players.resize(1);
	MyPlayerId = 0;
	MyPlayer = &Players[0];
	*MyPlayer = {};
	gbIsPoe2 = true;
	invflag = false;
	CharFlag = false;
	const std::optional<Rectangle> tray = SkillTrayRect();
	ASSERT_TRUE(tray.has_value());
	const Point world { 320, 60 };
	EXPECT_FALSE(IsCursorOverUi(world));
	EXPECT_TRUE(IsCursorOverUi(tray->Center()));
	EXPECT_TRUE(IsCursorOverUi(GetMainPanel().Center()));

	// The tray popup covers the world above the tray.
	for (int y = tray->position.y; y < tray->position.y + tray->size.height && !IsSkillTrayPopupOpen(); y += 4) {
		for (int x = tray->position.x; x < tray->position.x + tray->size.width && !IsSkillTrayPopupOpen(); x += 4)
			HandleSkillBarClick({ x, y });
	}
	ASSERT_TRUE(IsSkillTrayPopupOpen());
	const Point abovePopupSlot { tray->Center().x, tray->position.y - 12 };
	EXPECT_TRUE(IsCursorOverUi(abovePopupSlot));
	CloseSkillTrayPopup();
	EXPECT_FALSE(IsCursorOverUi(abovePopupSlot));

	// Side panels, and talk text over the whole screen.
	invflag = true;
	EXPECT_TRUE(IsCursorOverUi(GetRightPanel().Center()));
	invflag = false;
	qtextflag = true;
	EXPECT_TRUE(IsCursorOverUi(world));
	qtextflag = false;

	// Diablo's own check is as it was: the tray isn't there.
	gbIsPoe2 = false;
	EXPECT_FALSE(IsCursorOverUi(tray->Center()));
	EXPECT_TRUE(IsCursorOverUi(GetMainPanel().Center()));
}

} // namespace
} // namespace devilution
