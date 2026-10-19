#include <cstdint>

#include <gtest/gtest.h>

#include "engine/render/primitive_render.hpp"
#include "engine/surface.hpp"

namespace devilution {
namespace {

TEST(PrimitiveRenderTest, VerticalLineIsClippedAtTheBottomOfASubregion)
{
	OwnedSurface out { 8, 16 };
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 8; x++)
			out[{ x, y }] = 0;
	// A 4-row window in the middle of the surface, like a scrolled list inside a panel.
	const Surface window = out.subregion(0, 4, 8, 4);
	DrawVerticalLine(window, { 2, 2 }, 10, 7);
	for (int y = 0; y < 16; y++)
		EXPECT_EQ(out[Point(2, y)], (y == 6 || y == 7) ? 7 : 0) << "row " << y;
}

} // namespace
} // namespace devilution
