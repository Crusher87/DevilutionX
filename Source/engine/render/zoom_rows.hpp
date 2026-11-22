/**
 * @file engine/render/zoom_rows.hpp
 *
 * EXPERIMENT (claude/overnight-fps-experiment): the work of scrollrt.cpp's Zoom, apart from the panels, so
 * that it can be tested and timed by itself. Doubles the top left part of an 8-bit buffer in place, sixteen
 * pixels at a time where the processor has SSE2. Writes exactly the bytes the loop it replaces wrote.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define DEVILUTIONX_ZOOM_ROWS_SSE2 1
#include <emmintrin.h>
#endif

namespace devilution {

/** Writes each of `count` pixels at `src` twice, to `dst`. The two must not overlap. */
inline void DoublePixels(const uint8_t *src, uint8_t *dst, int count)
{
	int i = 0;
#ifdef DEVILUTIONX_ZOOM_ROWS_SSE2
	for (; i + 16 <= count; i += 16) {
		const __m128i pixels = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + i));
		_mm_storeu_si128(reinterpret_cast<__m128i *>(dst + 2 * i), _mm_unpacklo_epi8(pixels, pixels));
		_mm_storeu_si128(reinterpret_cast<__m128i *>(dst + 2 * i + 16), _mm_unpackhi_epi8(pixels, pixels));
	}
#endif
	for (; i < count; i++) {
		dst[2 * i] = src[i];
		dst[2 * i + 1] = src[i];
	}
}

/**
 * @brief Scales the top left part of a buffer up 2x, in place.
 * @param base The buffer's top left pixel.
 * @param pitch Bytes from one row to the next.
 * @param height Rows of the result.
 * @param offsetX Where the result starts in each row (the width of an open left panel).
 * @param width Pixels of the result in each row.
 *
 * Works from the bottom row up. Row `k` of the source goes to rows `2k` and `2k + 1`, so a source row is
 * only written over after it was read, and the doubled row is never the row being read.
 */
inline void ZoomTopLeft(uint8_t *base, int pitch, int height, int offsetX, int width)
{
	const int pairs = width / 2;
	const int odd = width % 2;
	const int srcHeight = (height + 1) / 2;
	const int doubleableHeight = height / 2;
	for (int i = 0; i < doubleableHeight; i++) {
		const uint8_t *src = base + static_cast<std::ptrdiff_t>(srcHeight - 1 - i) * pitch;
		uint8_t *dst = base + static_cast<std::ptrdiff_t>(height - 1 - 2 * i) * pitch + offsetX;
		// With an odd width the leftmost pixel is written once.
		if (odd != 0)
			dst[0] = src[0];
		DoublePixels(src + odd, dst + odd, pairs);
		std::memcpy(dst - pitch, dst, width);
	}
	if ((height % 2) == 1) {
		// Kept byte for byte as the loop this replaces had it. With an odd height it leaves row 0 undoubled
		// and copies `width` bytes that start just right of the picture in row 0 to the same place one row
		// above the buffer (its padding). That looks like a slip in the original; it is not this
		// experiment's to change.
		uint8_t *row = base + offsetX + width;
		std::memcpy(row - pitch, row, width);
	}
}

} // namespace devilution
