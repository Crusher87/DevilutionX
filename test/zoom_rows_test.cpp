// EXPERIMENT: ZoomTopLeft must write exactly what the loop it replaced in scrollrt.cpp's Zoom wrote.

#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "engine/render/zoom_rows.hpp"

namespace devilution {
namespace {

// The body of Zoom as it was before the experiment, on a bare buffer.
void ZoomAsItWas(uint8_t *base, int pitch, int height, int viewportOffsetX, int viewportWidth)
{
	const int srcWidth = (viewportWidth + 1) / 2;
	const int doubleableWidth = viewportWidth / 2;
	const int srcHeight = (height + 1) / 2;
	const int doubleableHeight = height / 2;

	uint8_t *src = base + (srcHeight - 1) * pitch + srcWidth - 1;
	uint8_t *dst = base + (height - 1) * pitch + viewportOffsetX + viewportWidth - 1;
	const bool oddViewportWidth = (viewportWidth % 2) == 1;

	for (int hgt = 0; hgt < doubleableHeight; hgt++) {
		for (int i = 0; i < doubleableWidth; i++) {
			*dst-- = *src;
			*dst-- = *src;
			--src;
		}
		if (oddViewportWidth) {
			*dst-- = *src;
			--src;
		}
		src -= (pitch - srcWidth);
		memcpy(dst - pitch + 1, dst + 1, viewportWidth);
		dst -= 2 * pitch - viewportWidth;
	}
	if ((height % 2) == 1) {
		memcpy(dst - pitch + 1, dst + 1, viewportWidth);
	}
}

TEST(ZoomRowsTest, WritesTheSameBytesAsTheLoopItReplaced)
{
	uint32_t seed = 12345;
	for (const int bufferWidth : { 1, 2, 15, 16, 17, 31, 33, 64, 127, 320, 643 }) {
		for (const int height : { 1, 2, 3, 8, 9, 37, 240 }) {
			for (const int panel : { 0, 1, 7, 160 }) {
				if (panel >= bufferWidth)
					continue;
				const int width = bufferWidth - panel;
				for (const int padding : { 0, 5, 64 }) {
					const int pitch = bufferWidth + padding;
					// One spare row above (an odd height writes there) and one below.
					std::vector<uint8_t> original(static_cast<size_t>(pitch) * (height + 2));
					for (uint8_t &pixel : original) {
						seed = seed * 1664525U + 1013904223U;
						pixel = static_cast<uint8_t>(seed >> 24);
					}
					std::vector<uint8_t> expected = original;
					std::vector<uint8_t> actual = original;
					ZoomAsItWas(expected.data() + pitch, pitch, height, panel, width);
					ZoomTopLeft(actual.data() + pitch, pitch, height, panel, width);
					ASSERT_EQ(expected, actual)
					    << "width " << width << " height " << height << " panel " << panel << " pitch " << pitch;
				}
			}
		}
	}
}

TEST(ZoomRowsTest, DoublesEveryPixelOfAnEvenSizedPicture)
{
	const int width = 40;
	const int height = 6;
	std::vector<uint8_t> buffer(static_cast<size_t>(width) * height);
	for (int y = 0; y < height / 2; y++) {
		for (int x = 0; x < width / 2; x++)
			buffer[y * width + x] = static_cast<uint8_t>(1 + y * 20 + x);
	}
	ZoomTopLeft(buffer.data(), width, height, 0, width);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++)
			ASSERT_EQ(buffer[y * width + x], static_cast<uint8_t>(1 + (y / 2) * 20 + x / 2)) << x << "," << y;
	}
}

} // namespace
} // namespace devilution
