#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "panels/spell_icons.hpp"
#include "poe2/hd_spell_icons.hpp"

namespace devilution {
namespace {

/** @brief A picture, 4 bytes a pixel, all one colour. */
std::vector<uint8_t> Flat(int width, int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
	for (size_t i = 0; i < pixels.size(); i += 4) {
		pixels[i] = r;
		pixels[i + 1] = g;
		pixels[i + 2] = b;
		pixels[i + 3] = a;
	}
	return pixels;
}

std::vector<uint8_t> Shrunk(const std::vector<uint8_t> &src, int srcWidth, int srcHeight, int dstWidth, int dstHeight)
{
	std::vector<uint8_t> dst(static_cast<size_t>(dstWidth) * dstHeight * 4, 0x55);
	hd::ShrinkPicture(src.data(), srcWidth, srcHeight, srcWidth * 4, dst.data(), dstWidth, dstHeight, dstWidth * 4);
	return dst;
}

TEST(HdSpellIcons, EachTintHasItsPlate)
{
	EXPECT_EQ(hd::SpellIconPlateName(SpellType::Skill), "spellicons/plate_skill");
	EXPECT_EQ(hd::SpellIconPlateName(SpellType::Spell), "spellicons/plate_spell");
	EXPECT_EQ(hd::SpellIconPlateName(SpellType::Scroll), "spellicons/plate_scroll");
	EXPECT_EQ(hd::SpellIconPlateName(SpellType::Charges), "spellicons/plate_charges");
	EXPECT_EQ(hd::SpellIconPlateName(SpellType::Invalid), "spellicons/plate_invalid");
	EXPECT_TRUE(hd::SpellIconPlateName(static_cast<SpellType>(200)).empty());
}

TEST(HdSpellIcons, AFrameHasItsTwoDigitLayer)
{
	EXPECT_EQ(hd::SpellIconLayerName(0, true), "spellicons/big/00");
	EXPECT_EQ(hd::SpellIconLayerName(7, true), "spellicons/big/07");
	EXPECT_EQ(hd::SpellIconLayerName(7, false), "spellicons/small/07");
	EXPECT_EQ(hd::SpellIconLayerName(25, false), "spellicons/small/25");
	EXPECT_EQ(hd::SpellIconLayerName(27, true), "spellicons/big/27");
	EXPECT_EQ(hd::SpellIconLayerName(50, false), "spellicons/small/50");
	EXPECT_EQ(hd::SpellIconLayerName(static_cast<uint8_t>(SpellIcon::Firebolt), true), "spellicons/big/00");
	EXPECT_EQ(hd::SpellIconLayerName(static_cast<uint8_t>(SpellIcon::Mana), true), "spellicons/big/50");
}

TEST(HdSpellIcons, TheBlankIconIsThePlateAlone)
{
	ASSERT_EQ(static_cast<uint8_t>(SpellIcon::Empty), hd::SpellIconEmptyFrame);
	EXPECT_TRUE(hd::SpellIconHasHdArt(hd::SpellIconEmptyFrame));
	EXPECT_TRUE(hd::SpellIconLayerName(hd::SpellIconEmptyFrame, true).empty());
	EXPECT_TRUE(hd::SpellIconLayerName(hd::SpellIconEmptyFrame, false).empty());
	EXPECT_EQ(hd::SpellIconArtName(hd::SpellIconEmptyFrame, true, SpellType::Spell), "spellicons/plate_spell");
	EXPECT_EQ(hd::SpellIconArtName(hd::SpellIconEmptyFrame, false, SpellType::Invalid), "spellicons/plate_invalid");
}

TEST(HdSpellIcons, LabelFramesKeepTheGamesOwnDrawing)
{
	ASSERT_EQ(static_cast<uint8_t>(SpellIcon::Mana), hd::SpellIconLastFrame);
	for (int frame = 51; frame < 256; frame++) {
		EXPECT_FALSE(hd::SpellIconHasHdArt(static_cast<uint8_t>(frame))) << frame;
		EXPECT_TRUE(hd::SpellIconLayerName(static_cast<uint8_t>(frame), true).empty()) << frame;
		EXPECT_TRUE(hd::SpellIconLayerName(static_cast<uint8_t>(frame), false).empty()) << frame;
		EXPECT_TRUE(hd::SpellIconArtName(static_cast<uint8_t>(frame), false, SpellType::Skill).empty()) << frame;
	}
}

TEST(HdSpellIcons, AnIconsArtIsItsPlateAndItsLayer)
{
	EXPECT_EQ(hd::SpellIconArtName(11, false, SpellType::Spell), "spellicons/plate_spell+spellicons/small/11");
	EXPECT_EQ(hd::SpellIconArtName(11, true, SpellType::Skill), "spellicons/plate_skill+spellicons/big/11");
	EXPECT_EQ(hd::SpellIconArtName(11, true, SpellType::Charges), "spellicons/plate_charges+spellicons/big/11");
	// Colours with no plate: no HD art.
	EXPECT_TRUE(hd::SpellIconArtName(11, true, static_cast<SpellType>(9)).empty());
}

TEST(HdSpellIcons, EveryIconOfTheGameHasAName)
{
	// The frames a spell's icon can be, and both sizes: each has a plate, and all but the blank one a layer.
	for (int frame = 0; frame <= hd::SpellIconLastFrame; frame++) {
		for (const bool large : { false, true }) {
			const std::string name = hd::SpellIconArtName(static_cast<uint8_t>(frame), large, SpellType::Skill);
			ASSERT_FALSE(name.empty()) << frame;
			EXPECT_EQ(name.find('+') == std::string::npos, frame == hd::SpellIconEmptyFrame) << frame;
		}
	}
}

TEST(HdSpellIcons, ShrinkingAFlatPictureKeepsItsColour)
{
	const std::vector<uint8_t> src = Flat(224, 224, 201, 97, 13, 255);
	// The sizes an icon has on screen at UI scale 175, and odd ones.
	for (const auto &[w, h] : std::vector<std::pair<int, int>> { { 65, 67 }, { 64, 66 }, { 98, 98 }, { 37, 38 }, { 56, 56 }, { 1, 1 }, { 223, 3 }, { 224, 224 } }) {
		const std::vector<uint8_t> dst = Shrunk(src, 224, 224, w, h);
		for (size_t i = 0; i < dst.size(); i += 4) {
			ASSERT_EQ(dst[i], 201) << w << "x" << h << " at " << i / 4;
			ASSERT_EQ(dst[i + 1], 97);
			ASSERT_EQ(dst[i + 2], 13);
			ASSERT_EQ(dst[i + 3], 255);
		}
	}
}

TEST(HdSpellIcons, ShrinkingAFlatSeeThroughPictureKeepsItsColourAndAlpha)
{
	const std::vector<uint8_t> src = Flat(224, 224, 10, 220, 130, 77);
	const std::vector<uint8_t> dst = Shrunk(src, 224, 224, 65, 67);
	for (size_t i = 0; i < dst.size(); i += 4) {
		ASSERT_EQ(dst[i], 10);
		ASSERT_EQ(dst[i + 1], 220);
		ASSERT_EQ(dst[i + 2], 130);
		ASSERT_EQ(dst[i + 3], 77);
	}
}

TEST(HdSpellIcons, ShrinkingAveragesWhatEachPixelCovers)
{
	// Left half black, right half white, 8 wide: at 2 wide each new pixel is one half; at 1 wide, the middle grey.
	std::vector<uint8_t> src = Flat(8, 2, 0, 0, 0, 255);
	for (int y = 0; y < 2; y++) {
		for (int x = 4; x < 8; x++) {
			for (int c = 0; c < 3; c++)
				src[(static_cast<size_t>(y) * 8 + x) * 4 + c] = 255;
		}
	}
	const std::vector<uint8_t> two = Shrunk(src, 8, 2, 2, 1);
	EXPECT_EQ(two[0], 0);
	EXPECT_EQ(two[4], 255);
	const std::vector<uint8_t> one = Shrunk(src, 8, 2, 1, 1);
	EXPECT_NEAR(one[0], 128, 1);
	EXPECT_EQ(one[3], 255);
	// 8 to 3: the middle pixel covers 2 2/3 source pixels, 1 1/3 of each half.
	const std::vector<uint8_t> three = Shrunk(src, 8, 2, 3, 1);
	EXPECT_EQ(three[0], 0);
	EXPECT_NEAR(three[4], 128, 1);
	EXPECT_EQ(three[8], 255);
}

TEST(HdSpellIcons, SeeThroughPixelsDontDarkenTheEdges)
{
	// A red shape on see-through black (as a layer's empty pixels are): shrunk, its edge is still pure red, only
	// more see-through. Averaging the colours as they are would give a dark red fringe.
	std::vector<uint8_t> src = Flat(8, 8, 0, 0, 0, 0);
	for (int y = 0; y < 8; y++) {
		for (int x = 0; x < 3; x++) {
			uint8_t *p = &src[(static_cast<size_t>(y) * 8 + x) * 4];
			p[0] = 255;
			p[3] = 255;
		}
	}
	const std::vector<uint8_t> dst = Shrunk(src, 8, 8, 2, 2);
	for (int i = 0; i < 4; i += 2) {
		const uint8_t *edge = &dst[static_cast<size_t>(i) * 4];
		EXPECT_EQ(edge[0], 255);
		EXPECT_EQ(edge[1], 0);
		EXPECT_EQ(edge[2], 0);
		EXPECT_NEAR(edge[3], 191, 1); // 3 of its 4 columns
		const uint8_t *clear = edge + 4;
		EXPECT_EQ(clear[3], 0);
	}
}

TEST(HdSpellIcons, ShrinkingReadsRowsByTheirPitch)
{
	// Rows with padding after them, in both pictures: the padding is neither read nor written.
	const int srcPitch = 4 * 4 + 8;
	std::vector<uint8_t> src(static_cast<size_t>(srcPitch) * 4, 0xEE);
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 4; x++) {
			uint8_t *p = &src[static_cast<size_t>(y) * srcPitch + static_cast<size_t>(x) * 4];
			p[0] = 40;
			p[1] = 80;
			p[2] = 120;
			p[3] = 255;
		}
	}
	const int dstPitch = 2 * 4 + 4;
	std::vector<uint8_t> dst(static_cast<size_t>(dstPitch) * 2, 0x11);
	hd::ShrinkPicture(src.data(), 4, 4, srcPitch, dst.data(), 2, 2, dstPitch);
	for (int y = 0; y < 2; y++) {
		for (int x = 0; x < 2; x++) {
			const uint8_t *p = &dst[static_cast<size_t>(y) * dstPitch + static_cast<size_t>(x) * 4];
			EXPECT_EQ(p[0], 40);
			EXPECT_EQ(p[1], 80);
			EXPECT_EQ(p[2], 120);
			EXPECT_EQ(p[3], 255);
		}
		for (int i = 8; i < dstPitch; i++)
			EXPECT_EQ(dst[static_cast<size_t>(y) * dstPitch + i], 0x11);
	}
}

TEST(HdSpellIcons, ALayerGoesOverThePlateByItsAlpha)
{
	// The plate is opaque: where the layer is clear the plate stays, where it is solid the layer shows, and a
	// half see-through pixel is halfway between; the result stays opaque.
	std::vector<uint8_t> plate = Flat(3, 1, 100, 50, 200, 255);
	std::vector<uint8_t> layer = Flat(3, 1, 0, 0, 0, 0);
	layer[0] = 9; // a clear pixel's colour means nothing
	layer[4] = 255;
	layer[5] = 255;
	layer[6] = 255;
	layer[7] = 255;
	layer[8] = 0;
	layer[9] = 150;
	layer[10] = 0;
	layer[11] = 128;
	hd::LayPictureOver(plate.data(), 12, layer.data(), 12, 3, 1);
	EXPECT_EQ(plate[0], 100);
	EXPECT_EQ(plate[1], 50);
	EXPECT_EQ(plate[2], 200);
	EXPECT_EQ(plate[3], 255);
	EXPECT_EQ(plate[4], 255);
	EXPECT_EQ(plate[5], 255);
	EXPECT_EQ(plate[6], 255);
	EXPECT_EQ(plate[7], 255);
	EXPECT_NEAR(plate[8], 50, 1);
	EXPECT_NEAR(plate[9], 100, 1);
	EXPECT_NEAR(plate[10], 100, 1);
	EXPECT_EQ(plate[11], 255);
}

TEST(HdSpellIcons, ALayerOverAClearPictureKeepsItsOwnColour)
{
	// Straight alpha: over nothing, a half see-through red stays pure red at half alpha (no darkening).
	std::vector<uint8_t> under = Flat(1, 1, 0, 0, 0, 0);
	const std::vector<uint8_t> over = Flat(1, 1, 255, 0, 0, 128);
	hd::LayPictureOver(under.data(), 4, over.data(), 4, 1, 1);
	EXPECT_EQ(under[0], 255);
	EXPECT_EQ(under[1], 0);
	EXPECT_EQ(under[2], 0);
	EXPECT_EQ(under[3], 128);
}

TEST(HdSpellIcons, AnIconCoversTheScreenPixelsTheGamesOwnDoes)
{
	// UI scale 175: UI pixel 0 starts at screen pixel 0; each UI pixel takes the screen pixels whose middle is in it.
	EXPECT_EQ(hd::FirstScreenPixelOf(0, 1.75F), 0);
	EXPECT_EQ(hd::FirstScreenPixelOf(1, 1.75F), 2); // 1.75: the pixel at 1 (middle 1.5) is still UI pixel 0
	EXPECT_EQ(hd::FirstScreenPixelOf(2, 1.75F), 3);
	EXPECT_EQ(hd::FirstScreenPixelOf(4, 1.75F), 7);
	EXPECT_EQ(hd::FirstScreenPixelOf(10, 2.F), 20);
	EXPECT_EQ(hd::FirstScreenPixelOf(10, 1.F), 10);
	// A 37 wide icon is 64 or 65 screen pixels wide by where it is, and its neighbours meet with no gap or overlap.
	for (int x = 0; x < 200; x++) {
		const int width = hd::FirstScreenPixelOf(x + 37, 1.75F) - hd::FirstScreenPixelOf(x, 1.75F);
		EXPECT_TRUE(width == 64 || width == 65) << x;
		// Every screen pixel from its first on belongs to it: the pixel's middle is inside the UI pixel.
		const int first = hd::FirstScreenPixelOf(x, 1.75F);
		EXPECT_EQ(static_cast<int>((static_cast<float>(first) + 0.5F) / 1.75F), x);
		if (first > 0) {
			EXPECT_EQ(static_cast<int>((static_cast<float>(first - 1) + 0.5F) / 1.75F), x - 1);
		}
	}
}

} // namespace
} // namespace devilution
