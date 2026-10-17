#include <gtest/gtest.h>

#include "diablo.h"
#include "engine/ui_scale.hpp"
#include "utils/ui_fwd.h"

namespace devilution {
namespace {

class Poe2UiScaleTest : public ::testing::Test {
public:
	void TearDown() override
	{
		SetRenderSize({ 640, 480 }, 100, true);
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

} // namespace
} // namespace devilution
