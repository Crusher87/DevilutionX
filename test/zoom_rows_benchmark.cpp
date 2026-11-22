// EXPERIMENT: how long doubling a 3440x1440 picture takes, the old loop against ZoomTopLeft.

#include <cstdint>
#include <cstring>
#include <vector>

#include <benchmark/benchmark.h>

#include "engine/render/zoom_rows.hpp"

namespace devilution {
namespace {

constexpr int Width = 3440;
constexpr int Height = 1440;
constexpr int Pitch = Width + 2 * 64;

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
}

std::vector<uint8_t> Picture()
{
	std::vector<uint8_t> buffer(static_cast<size_t>(Pitch) * (Height + 2));
	uint32_t seed = 1;
	for (uint8_t &pixel : buffer) {
		seed = seed * 1664525U + 1013904223U;
		pixel = static_cast<uint8_t>(seed >> 24);
	}
	return buffer;
}

void BM_ZoomAsItWas(benchmark::State &state)
{
	std::vector<uint8_t> buffer = Picture();
	for (auto _ : state) {
		ZoomAsItWas(buffer.data() + Pitch, Pitch, Height, 0, Width);
		benchmark::DoNotOptimize(buffer.data());
	}
}

void BM_ZoomTopLeft(benchmark::State &state)
{
	std::vector<uint8_t> buffer = Picture();
	for (auto _ : state) {
		ZoomTopLeft(buffer.data() + Pitch, Pitch, Height, 0, Width);
		benchmark::DoNotOptimize(buffer.data());
	}
}

BENCHMARK(BM_ZoomAsItWas)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_ZoomTopLeft)->Unit(benchmark::kMicrosecond);

} // namespace
} // namespace devilution
