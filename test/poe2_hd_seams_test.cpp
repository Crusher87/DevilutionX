#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "poe2/hd_seams.hpp"

// POE2 Mashup: the HD world's tiles stay flush. A tile's HD picture is cut to the game's own shape for it, and made
// solid wherever the game's tile is solid, so the backdrop never shows as a thin line between two tiles
// (hd::CutTileArt). And a floor piece lit by its shape takes the same light on flat floor from tile to tile
// (hd::ShapeLightShares).

namespace devilution {
namespace {

constexpr int Scale = 2;

struct Picture {
	int width;
	int height;
	std::vector<uint8_t> pixels;
	Picture(int w, int h, std::array<uint8_t, 4> fill)
	    : width(w)
	    , height(h)
	    , pixels(static_cast<size_t>(w) * h * 4)
	{
		for (int i = 0; i < w * h; i++)
			Set(i % w, i / w, fill);
	}
	void Set(int x, int y, std::array<uint8_t, 4> colour)
	{
		for (int c = 0; c < 4; c++)
			pixels[(static_cast<size_t>(y) * width + x) * 4 + c] = colour[c];
	}
	std::array<uint8_t, 4> At(int x, int y) const
	{
		const uint8_t *p = &pixels[(static_cast<size_t>(y) * width + x) * 4];
		return { p[0], p[1], p[2], p[3] };
	}
	int Cut(const std::vector<uint8_t> &cover, int reach)
	{
		return hd::CutTileArt(pixels.data(), width, height, width * 4, cover.data(), width / Scale, Scale, reach);
	}
};

using Rgba = std::array<uint8_t, 4>;

TEST(HdSeams, SettingGivesTheReach)
{
	EXPECT_EQ(hd::SeamReachOf(0.F), 0);
	EXPECT_EQ(hd::SeamReachOf(-1.F), 0);
	EXPECT_EQ(hd::SeamReachOf(1.F), hd::SeamReach);
	EXPECT_EQ(hd::SeamReachOf(0.5F), hd::SeamReach);
	EXPECT_EQ(hd::SeamReachOf(12.F), 12);
	EXPECT_EQ(hd::SeamReachOf(1000.F), hd::MostSeamReach);
}

TEST(HdSeams, SolidPixelsAreNeverChanged)
{
	Picture picture(8, 8, { 10, 20, 30, 255 });
	for (int i = 0; i < 8; i++)
		picture.Set(i, (i * 3) % 8, { static_cast<uint8_t>(40 + i), static_cast<uint8_t>(90 - i), 200, 255 });
	picture.Set(3, 3, { 0, 0, 0, 0 });
	picture.Set(4, 3, { 250, 250, 250, 100 });
	const Picture before = picture;
	const std::vector<uint8_t> cover(16, 255);
	EXPECT_EQ(picture.Cut(cover, hd::SeamReach), 2);
	for (int y = 0; y < 8; y++) {
		for (int x = 0; x < 8; x++) {
			if (before.At(x, y)[3] == 255) {
				EXPECT_EQ(picture.At(x, y), before.At(x, y)) << x << "," << y;
			}
		}
	}
}

TEST(HdSeams, AHoleInsideTheGamesShapeTakesItsNeighboursColour)
{
	Picture picture(8, 8, { 100, 60, 20, 255 });
	// A see-through pixel whose own colour is rubbish, as a picture's cut-away pixels often are.
	picture.Set(4, 4, { 255, 0, 255, 0 });
	const std::vector<uint8_t> cover(16, 255);
	EXPECT_EQ(picture.Cut(cover, hd::SeamReach), 1);
	EXPECT_EQ(picture.At(4, 4), (Rgba { 100, 60, 20, 255 }));
}

TEST(HdSeams, AHalfSeeThroughPixelKeepsItsShareOfItsOwnColour)
{
	Picture picture(8, 8, { 100, 100, 100, 255 });
	picture.Set(4, 4, { 200, 0, 100, 128 });
	const std::vector<uint8_t> cover(16, 255);
	picture.Cut(cover, hd::SeamReach);
	const Rgba got = picture.At(4, 4);
	EXPECT_EQ(got[3], 255);
	// Its own colour over the neighbours', as it showed over a neighbour's colour and not over black.
	EXPECT_NEAR(got[0], 150, 1);
	EXPECT_NEAR(got[1], 50, 1);
	EXPECT_EQ(got[2], 100);
}

TEST(HdSeams, NearlySolidPixelsKeepTheirColour)
{
	Picture picture(8, 8, { 100, 100, 100, 255 });
	picture.Set(4, 4, { 7, 8, 9, hd::SeamSolidAlpha });
	const std::vector<uint8_t> cover(16, 255);
	EXPECT_EQ(picture.Cut(cover, hd::SeamReach), 1);
	// Solid in its own colour, not mixed with its neighbours'.
	EXPECT_EQ(picture.At(4, 4), (Rgba { 7, 8, 9, 255 }));
}

TEST(HdSeams, OutsideTheGamesShapeIsCutAsBefore)
{
	Picture picture(8, 4, { 100, 100, 100, 255 });
	picture.Set(5, 0, { 100, 100, 100, 200 });
	// Game pixels: solid, solid, half see-through, nothing.
	const std::vector<uint8_t> cover { 255, 255, 128, 0, 255, 255, 128, 0 };
	picture.Cut(cover, hd::SeamReach);
	for (int y = 0; y < 4; y++) {
		EXPECT_EQ(picture.At(0, y)[3], 255);
		EXPECT_EQ(picture.At(3, y)[3], 255);
		EXPECT_EQ(picture.At(6, y)[3], 0);
		EXPECT_EQ(picture.At(7, y)[3], 0);
	}
	EXPECT_EQ(picture.At(4, 0)[3], 128);
	EXPECT_EQ(picture.At(5, 0)[3], 200 * 128 / 255);
	EXPECT_EQ(picture.At(4, 1)[3], 128);
}

TEST(HdSeams, OffIsTheOldCutExactly)
{
	Picture picture(8, 4, { 100, 100, 100, 255 });
	picture.Set(1, 1, { 9, 9, 9, 77 });
	picture.Set(5, 2, { 9, 9, 9, 200 });
	const std::vector<uint8_t> cover { 255, 255, 128, 0, 255, 255, 128, 0 };
	Picture expected = picture;
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 8; x++) {
			Rgba colour = expected.At(x, y);
			colour[3] = static_cast<uint8_t>(colour[3] * cover[static_cast<size_t>(y / Scale) * 4 + x / Scale] / 255);
			expected.Set(x, y, colour);
		}
	}
	EXPECT_EQ(picture.Cut(cover, 0), 0);
	EXPECT_EQ(picture.pixels, expected.pixels);
}

TEST(HdSeams, ASoftEdgeIsFilledToTheEdgeOfTheGamesShape)
{
	// A picture whose right edge fades out over three pixels inside the game's shape: the hairline between two tiles.
	Picture picture(8, 4, { 80, 120, 160, 255 });
	for (int y = 0; y < 4; y++) {
		picture.Set(5, y, { 80, 120, 160, 170 });
		picture.Set(6, y, { 80, 120, 160, 85 });
		picture.Set(7, y, { 0, 0, 0, 0 });
	}
	const std::vector<uint8_t> cover(8, 255);
	EXPECT_EQ(picture.Cut(cover, hd::SeamReach), 12);
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 8; x++)
			EXPECT_EQ(picture.At(x, y), (Rgba { 80, 120, 160, 255 })) << x << "," << y;
	}
}

TEST(HdSeams, AWideHoleKeepsItsMiddle)
{
	Picture picture(32, 2, { 50, 50, 50, 255 });
	for (int x = 4; x < 28; x++) {
		picture.Set(x, 0, { 0, 0, 0, 0 });
		picture.Set(x, 1, { 0, 0, 0, 0 });
	}
	const std::vector<uint8_t> cover(16, 255);
	picture.Cut(cover, 4);
	EXPECT_EQ(picture.At(4, 0), (Rgba { 50, 50, 50, 255 }));
	EXPECT_EQ(picture.At(7, 1), (Rgba { 50, 50, 50, 255 }));
	EXPECT_EQ(picture.At(8, 0)[3], 0);
	EXPECT_EQ(picture.At(16, 1)[3], 0);
	EXPECT_EQ(picture.At(23, 0)[3], 0);
	EXPECT_EQ(picture.At(24, 0), (Rgba { 50, 50, 50, 255 }));
	// Asked to reach further, it closes.
	picture.Cut(cover, hd::MostSeamReach);
	EXPECT_EQ(picture.At(16, 1), (Rgba { 50, 50, 50, 255 }));
}

TEST(HdSeams, ColourSpreadsFromEachSideNotAlongTheScan)
{
	// Red art on the left, blue on the right, a see-through gap between: each half of the gap takes its own side.
	Picture picture(8, 2, { 200, 0, 0, 255 });
	for (int y = 0; y < 2; y++) {
		for (int x = 2; x < 6; x++)
			picture.Set(x, y, { 0, 0, 0, 0 });
		picture.Set(6, y, { 0, 0, 200, 255 });
		picture.Set(7, y, { 0, 0, 200, 255 });
	}
	const std::vector<uint8_t> cover(4, 255);
	picture.Cut(cover, hd::SeamReach);
	EXPECT_EQ(picture.At(2, 0), (Rgba { 200, 0, 0, 255 }));
	EXPECT_EQ(picture.At(3, 1), (Rgba { 200, 0, 0, 255 }));
	EXPECT_EQ(picture.At(4, 0), (Rgba { 0, 0, 200, 255 }));
	EXPECT_EQ(picture.At(5, 1), (Rgba { 0, 0, 200, 255 }));
}

TEST(HdSeams, NothingSolidNothingFilled)
{
	Picture picture(4, 4, { 9, 9, 9, 100 });
	const Picture before = picture;
	const std::vector<uint8_t> cover(4, 255);
	EXPECT_EQ(picture.Cut(cover, hd::MostSeamReach), 0);
	EXPECT_EQ(picture.pixels, before.pixels);
}

// The ways a shape map sorts a surface: +x, -x, +y, -y, up (hd.cpp's ShapeAxes).
constexpr size_t Ways = 5;
constexpr size_t Up = 4;

/** @brief The light a flat (up-facing) part of a piece is drawn in, against the light the floor has there. */
float FlatFloorLight(const std::array<float, Ways> &shade, float reference)
{
	const std::array<bool, Ways> has { true, false, true, false, true };
	float base = 1;
	std::array<float, Ways> extra {};
	if (!hd::ShapeLightShares(shade.data(), has.data(), Ways, reference, 1.F, base, extra.data()))
		return 1.F;
	return base + extra[Up];
}

TEST(HdSeams, FlatFloorTakesTheSameLightOnEveryTile)
{
	// Two tiles side by side, a lamp nearer one: the ways of facing take different light on each.
	const std::array<float, Ways> near { 1.6F, 0.4F, 1.1F, 0.7F, 1.F };
	const std::array<float, Ways> far { 1.2F, 0.8F, 1.05F, 0.9F, 1.F };
	// Measured against a floor facing up, flat floor is drawn in exactly the floor's own light on both: no step, and
	// the light level is the one the floor has without shape maps.
	EXPECT_FLOAT_EQ(FlatFloorLight(near, near[Up]), 1.F);
	EXPECT_FLOAT_EQ(FlatFloorLight(far, far[Up]), 1.F);
	// Measured against a thing standing on the tile seen from the front (the old way), it differed tile by tile.
	const float nearFront = 1.35F;
	const float farFront = 1.1F;
	EXPECT_GT(std::abs(FlatFloorLight(near, nearFront) - FlatFloorLight(far, farFront)), 0.05F);
	EXPECT_LT(FlatFloorLight(near, nearFront), 0.8F);
}

TEST(HdSeams, ShapeSharesAddUpToEachWaysLight)
{
	const std::array<float, Ways> shade { 1.6F, 0.4F, 1.1F, 0.7F, 1.F };
	const std::array<bool, Ways> has { true, true, false, true, true };
	float base = 0;
	std::array<float, Ways> extra {};
	ASSERT_TRUE(hd::ShapeLightShares(shade.data(), has.data(), Ways, 1.F, 1.F, base, extra.data()));
	EXPECT_FLOAT_EQ(base, 0.4F);
	EXPECT_FLOAT_EQ(base + extra[0], 1.6F);
	EXPECT_FLOAT_EQ(extra[1], 0.F);
	EXPECT_FLOAT_EQ(extra[2], 0.F);
	EXPECT_FLOAT_EQ(base + extra[3], 0.7F);
	EXPECT_FLOAT_EQ(base + extra[Up], 1.F);
	// Half as strong, half as far from the flat light.
	ASSERT_TRUE(hd::ShapeLightShares(shade.data(), has.data(), Ways, 1.F, 0.5F, base, extra.data()));
	EXPECT_FLOAT_EQ(base + extra[0], 1.3F);
}

TEST(HdSeams, DrawnFlatWhereShapeMakesNoDifference)
{
	const std::array<float, Ways> even { 1.F, 1.01F, 0.99F, 1.F, 1.F };
	const std::array<bool, Ways> all { true, true, true, true, true };
	const std::array<bool, Ways> none {};
	float base = 0;
	std::array<float, Ways> extra {};
	EXPECT_FALSE(hd::ShapeLightShares(even.data(), all.data(), Ways, 1.F, 1.F, base, extra.data()));
	const std::array<float, Ways> shade { 1.6F, 0.4F, 1.1F, 0.7F, 1.F };
	EXPECT_FALSE(hd::ShapeLightShares(shade.data(), none.data(), Ways, 1.F, 1.F, base, extra.data()));
	// In the dark there is no light to measure against.
	EXPECT_FALSE(hd::ShapeLightShares(shade.data(), all.data(), Ways, 0.01F, 1.F, base, extra.data()));
}

} // namespace
} // namespace devilution
