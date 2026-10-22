#include <gtest/gtest.h>

#include "cursor.h"
#include "engine/surface.hpp"
#include "utils/sdl_bilinear_scale.hpp"

using namespace devilution;

TEST(Cursor, NewCursor)
{
	NewCursor(CURSOR_HOURGLASS);
	EXPECT_EQ(pcurs, CURSOR_HOURGLASS);
}

namespace {

uint8_t CyclingBlendTable[256][256];

} // namespace

TEST(Cursor, HalvedItemPicturesStaySeeThroughWhileThePaletteCycles)
{
	// Curtis 2026-10-22: an Emerald shrunk to one cell while the cave palette cycled had a solid coloured square.
	// A cycling palette's table no longer maps the transparent index 1 to itself; here it maps to 9.
	for (int a = 0; a < 256; a++) {
		for (int b = 0; b < 256; b++)
			CyclingBlendTable[a][b] = static_cast<uint8_t>((a + b) / 2);
	}
	CyclingBlendTable[1][1] = 9;
	CyclingBlendTable[40][60] = 1;
	OwnedSurface picture { 4, 2 };
	// Left quad: all see-through. Right quad: two opaque pixels whose blend comes out as 1.
	const uint8_t pixels[2][4] = { { 1, 1, 1, 40 }, { 1, 1, 1, 60 } };
	for (int y = 0; y < 2; y++) {
		for (int x = 0; x < 4; x++)
			picture[{ x, y }] = pixels[y][x];
	}
	OwnedSurface half { 2, 1 };
	BilinearDownscaleByHalf8(picture.surface, CyclingBlendTable, half.surface, 1);
	EXPECT_EQ((half[{ 0, 0 }]), 1);
	EXPECT_NE((half[{ 1, 0 }]), 1);
}
