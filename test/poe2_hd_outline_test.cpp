#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "poe2/hd_outline.hpp"

namespace devilution {
namespace {

using hd::OutlineCut;

/** @brief A width x height picture with a filled square body from (left, top), `side` pixels across. */
struct Picture {
	int width;
	int height;
	std::vector<uint8_t> body;
	std::vector<uint8_t> cover;

	Picture(int w, int h, int left, int top, int side)
	    : width(w)
	    , height(h)
	    , body(static_cast<size_t>(w) * h, 0)
	    , cover(body.size(), 0)
	{
		for (int y = top; y < top + side; y++) {
			for (int x = left; x < left + side; x++) {
				body[static_cast<size_t>(y) * w + x] = 1;
				cover[static_cast<size_t>(y) * w + x] = 255;
			}
		}
	}

	/** @brief The outline's alpha at a picture pixel, or 0 outside the outline. */
	static uint8_t At(const hd::OutlineShape &shape, int x, int y, float scale = 1.F)
	{
		const int sx = x - static_cast<int>(shape.x * scale);
		const int sy = y - static_cast<int>(shape.y * scale);
		if (sx < 0 || sy < 0 || sx >= shape.width || sy >= shape.height)
			return 0;
		return shape.alpha[static_cast<size_t>(sy) * shape.width + sx];
	}
};

TEST(Poe2HdOutlineTest, CutsAndReach)
{
	EXPECT_EQ(hd::OutlineCutFor({}), OutlineCut::Line);
	EXPECT_EQ(hd::OutlineCutFor({ .thick = true }), OutlineCut::Thick);
	// A glow is drawn under the line, never instead of it.
	EXPECT_EQ(hd::OutlineCutFor({ .glow = true }), OutlineCut::Line);
	EXPECT_FLOAT_EQ(hd::OutlineReach(OutlineCut::Line), 1.F);
	EXPECT_LT(hd::OutlineReach(OutlineCut::Line), hd::OutlineReach(OutlineCut::Thick));
	EXPECT_LT(hd::OutlineReach(OutlineCut::Thick), hd::OutlineReach(OutlineCut::Glow));
}

TEST(Poe2HdOutlineTest, LineGoesRoundTheBodyAndLeavesItOut)
{
	const Picture picture(11, 11, 4, 4, 3);
	const hd::OutlineShape shape = hd::MakeOutlineShape(picture.body, picture.cover, picture.width, picture.height, 1.F, 1.F, OutlineCut::Line);
	ASSERT_FALSE(shape.empty());
	// Right beside the body: full; the body itself: none (the picture goes there).
	EXPECT_EQ(Picture::At(shape, 3, 5), 255);
	EXPECT_EQ(Picture::At(shape, 7, 5), 255);
	EXPECT_EQ(Picture::At(shape, 5, 3), 255);
	EXPECT_EQ(Picture::At(shape, 5, 5), 0);
	// Two pixels away: nothing, so the band is one pixel wide.
	EXPECT_EQ(Picture::At(shape, 2, 5), 0);
	// The corner is rounded: the diagonal pixel is only partly covered.
	const uint8_t corner = Picture::At(shape, 3, 3);
	EXPECT_GT(corner, 0);
	EXPECT_LT(corner, 255);
	// It sits round the body, in the picture's pixels.
	EXPECT_LE(shape.x, 3.F);
	EXPECT_LE(shape.y, 3.F);
	EXPECT_GE(shape.x + shape.w, 8.F);
	EXPECT_GE(shape.y + shape.h, 8.F);
}

TEST(Poe2HdOutlineTest, PlaceIsInGamePixels)
{
	// The same body at four pixels to the game pixel: the band is four pixels wide, its place the same in game pixels.
	const Picture small(11, 11, 4, 4, 3);
	const Picture big(44, 44, 16, 16, 12);
	const hd::OutlineShape a = hd::MakeOutlineShape(small.body, small.cover, small.width, small.height, 1.F, 1.F, OutlineCut::Line);
	const hd::OutlineShape b = hd::MakeOutlineShape(big.body, big.cover, big.width, big.height, 4.F, 4.F, OutlineCut::Line);
	ASSERT_FALSE(b.empty());
	EXPECT_NEAR(a.x, b.x, 0.5F);
	EXPECT_NEAR(a.w, b.w, 1.F);
	EXPECT_EQ(Picture::At(b, 13, 20, 4.F), 255);
	EXPECT_EQ(Picture::At(b, 10, 20, 4.F), 0);
}

TEST(Poe2HdOutlineTest, GlowReachesFurtherAndFades)
{
	const Picture picture(21, 21, 9, 9, 3);
	const hd::OutlineShape line = hd::MakeOutlineShape(picture.body, picture.cover, picture.width, picture.height, 1.F, 1.F, OutlineCut::Line);
	const hd::OutlineShape glow = hd::MakeOutlineShape(picture.body, picture.cover, picture.width, picture.height, 1.F, 1.F, OutlineCut::Glow);
	ASSERT_FALSE(glow.empty());
	EXPECT_GT(glow.w, line.w + 4.F);
	// Strongest by the body, fading away from it.
	const uint8_t near = Picture::At(glow, 8, 10);
	const uint8_t mid = Picture::At(glow, 6, 10);
	const uint8_t far = Picture::At(glow, 4, 10);
	EXPECT_GT(near, mid);
	EXPECT_GT(mid, far);
	EXPECT_EQ(Picture::At(glow, 10, 10), 0);
}

TEST(Poe2HdOutlineTest, NoBodyNoOutline)
{
	const Picture picture(8, 8, 0, 0, 0);
	EXPECT_TRUE(hd::MakeOutlineShape(picture.body, picture.cover, picture.width, picture.height, 1.F, 1.F, OutlineCut::Line).empty());
	EXPECT_TRUE(hd::MakeOutlineShape({}, {}, 8, 8, 1.F, 1.F, OutlineCut::Line).empty());
}

TEST(Poe2HdOutlineTest, HoveredOutlineFadesIn)
{
	hd::OutlineFader fader;
	const hd::OutlineLook look { .hovered = true, .thing = 7 };
	const hd::OutlineColour gold { 1.F, 0.8F, 0.2F };
	const auto at = [&](uint32_t now) {
		hd::OutlineColour colour = gold;
		float alpha = 1.F;
		fader.Fade(look, now, colour, alpha);
		return alpha;
	};
	EXPECT_FLOAT_EQ(at(1000), 0.F);
	EXPECT_NEAR(at(1000 + static_cast<uint32_t>(hd::HoverFadeMs / 2)), 0.5F, 0.01F);
	EXPECT_FLOAT_EQ(at(1000 + static_cast<uint32_t>(hd::HoverFadeMs)), 1.F);
	EXPECT_FLOAT_EQ(at(1250), 1.F);
	// Gone for a while (not drawn), then hovered again: it fades in again.
	EXPECT_FLOAT_EQ(at(1250 + hd::OutlineGoneMs + 1), 0.F);
}

TEST(Poe2HdOutlineTest, OutlineNotHoveredShowsAtOnce)
{
	hd::OutlineFader fader;
	hd::OutlineColour colour { 1.F, 0.5F, 0.F };
	float alpha = 1.F;
	fader.Fade({ .thing = 3 }, 500, colour, alpha);
	EXPECT_FLOAT_EQ(alpha, 1.F);
	EXPECT_FLOAT_EQ(colour[1], 0.5F);
}

TEST(Poe2HdOutlineTest, HoverFadesFromTheOutlineItHad)
{
	// An item with the orange "currency fits" outline, then the cursor on it: its outline turns from orange to its own.
	hd::OutlineFader fader;
	const hd::OutlineColour orange { 1.F, 0.5F, 0.F };
	const hd::OutlineColour blue { 0.F, 0.F, 1.F };
	hd::OutlineColour colour = orange;
	float alpha = 1.F;
	fader.Fade({ .thing = 9 }, 100, colour, alpha);
	colour = blue;
	alpha = 1.F;
	fader.Fade({ .hovered = true, .thing = 9 }, 116, colour, alpha);
	EXPECT_FLOAT_EQ(alpha, 1.F);
	EXPECT_FLOAT_EQ(colour[0], 1.F);
	EXPECT_FLOAT_EQ(colour[2], 0.F);
	colour = blue;
	fader.Fade({ .hovered = true, .thing = 9 }, 116 + static_cast<uint32_t>(hd::HoverFadeMs), colour, alpha);
	EXPECT_FLOAT_EQ(colour[0], 0.F);
	EXPECT_FLOAT_EQ(colour[2], 1.F);
}

TEST(Poe2HdOutlineTest, ThingsFadeApart)
{
	hd::OutlineFader fader;
	hd::OutlineColour colour { 1.F, 1.F, 1.F };
	float alpha = 1.F;
	for (const uint32_t now : { 0U, 100U, 200U }) {
		alpha = 1.F;
		fader.Fade({ .hovered = true, .thing = 1 }, now, colour, alpha);
	}
	EXPECT_FLOAT_EQ(alpha, 1.F);
	// The cursor moves onto another item: that one starts from nothing.
	alpha = 1.F;
	fader.Fade({ .hovered = true, .thing = 2 }, 216, colour, alpha);
	EXPECT_FLOAT_EQ(alpha, 0.F);
	EXPECT_EQ(fader.size(), 2U);
}

TEST(Poe2HdOutlineTest, PruneForgetsOldOutlinesOnceThereAreMany)
{
	hd::OutlineFader fader;
	hd::OutlineColour colour { 1.F, 1.F, 1.F };
	float alpha = 1.F;
	for (uint32_t thing = 1; thing <= 300; thing++)
		fader.Fade({ .thing = thing }, 0, colour, alpha);
	fader.Fade({ .thing = 1000 }, 5000, colour, alpha);
	fader.Prune(5000);
	EXPECT_EQ(fader.size(), 1U);
}

TEST(Poe2HdOutlineTest, GlowBreathes)
{
	EXPECT_NEAR(hd::GlowStrength(0), hd::GlowLow, 1e-4F);
	EXPECT_NEAR(hd::GlowStrength(hd::GlowBreathMs / 2), hd::GlowHigh, 1e-4F);
	EXPECT_NEAR(hd::GlowStrength(hd::GlowBreathMs), hd::GlowLow, 1e-4F);
}

} // namespace
} // namespace devilution
