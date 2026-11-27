#include "poe2/hd_seams.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace devilution {

namespace hd {

int SeamReachOf(float setting)
{
	if (!(setting > 0))
		return 0;
	if (setting <= 1)
		return SeamReach;
	return std::min(static_cast<int>(std::lround(setting)), MostSeamReach);
}

int CutTileArt(uint8_t *pixels, int width, int height, int pitch, const uint8_t *cover, int coverWidth, int scale, int reach)
{
	if (pixels == nullptr || cover == nullptr || width <= 0 || height <= 0 || scale <= 0)
		return 0;
	const size_t total = static_cast<size_t>(width) * height;
	constexpr uint16_t Unreached = std::numeric_limits<uint16_t>::max();
	// How far each pixel is from solid art: 0 is solid art itself; Unreached, a pixel still to fill or left alone.
	std::vector<uint16_t> ring;
	std::vector<size_t> open;
	int filled = 0;
	if (reach > 0)
		ring.assign(total, Unreached);
	for (int y = 0; y < height; y++) {
		uint8_t *row = pixels + static_cast<ptrdiff_t>(y) * pitch;
		const uint8_t *coverRow = cover + static_cast<size_t>(y / scale) * coverWidth;
		for (int x = 0; x < width; x++) {
			uint8_t &alpha = row[x * 4 + 3];
			const uint8_t covered = coverRow[x / scale];
			if (reach <= 0 || covered != 255) {
				alpha = static_cast<uint8_t>(alpha * covered / 255);
				continue;
			}
			if (alpha >= SeamSolidAlpha) {
				if (alpha != 255) {
					alpha = 255;
					filled++;
				}
				ring[static_cast<size_t>(y) * width + x] = 0;
				continue;
			}
			open.push_back(static_cast<size_t>(y) * width + x);
		}
	}
	// Ring by ring outwards from the solid art, each pixel taking the colour of the ones filled before it.
	for (int step = 1; step <= reach && !open.empty(); step++) {
		size_t kept = 0;
		const int before = filled;
		for (const size_t at : open) {
			const int x = static_cast<int>(at % static_cast<size_t>(width));
			const int y = static_cast<int>(at / static_cast<size_t>(width));
			int sum[3] = { 0, 0, 0 };
			int weight = 0;
			for (int dy = -1; dy <= 1; dy++) {
				for (int dx = -1; dx <= 1; dx++) {
					const int nx = x + dx;
					const int ny = y + dy;
					if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= width || ny >= height)
						continue;
					if (ring[static_cast<size_t>(ny) * width + nx] >= step)
						continue;
					// The pixels straight beside it count twice the ones across a corner.
					const int share = dx == 0 || dy == 0 ? 2 : 1;
					const uint8_t *from = pixels + static_cast<ptrdiff_t>(ny) * pitch + nx * 4;
					for (int c = 0; c < 3; c++)
						sum[c] += from[c] * share;
					weight += share;
				}
			}
			if (weight == 0) {
				open[kept++] = at;
				continue;
			}
			uint8_t *pixel = pixels + static_cast<ptrdiff_t>(y) * pitch + x * 4;
			const int own = pixel[3];
			for (int c = 0; c < 3; c++) {
				const int spread = (sum[c] + weight / 2) / weight;
				pixel[c] = static_cast<uint8_t>((pixel[c] * own + spread * (255 - own) + 127) / 255);
			}
			pixel[3] = 255;
			// Marked after the ring is done (below), so a ring never reads its own pixels: no smear along the scan.
			ring[at] = static_cast<uint16_t>(step + 1);
			filled++;
		}
		open.resize(kept);
		// Nothing solid anywhere near what is left.
		if (filled == before)
			break;
		for (uint16_t &mark : ring) {
			if (mark == step + 1)
				mark = static_cast<uint16_t>(step);
		}
	}
	return filled;
}

bool ShapeLightShares(const float *shade, const bool *has, size_t count, float flat, float strength, float &base, float *extra)
{
	// Too little to see: all its ways of facing within this of each other.
	constexpr float LeastSpread = 0.03F;
	constexpr size_t MostWays = 8;
	if (flat < 0.05F || count > MostWays)
		return false;
	float least = std::numeric_limits<float>::max();
	float against[MostWays];
	for (size_t k = 0; k < count; k++) {
		against[k] = std::clamp(1 + (shade[k] / flat - 1) * strength, 0.15F, 2.5F);
		if (has[k])
			least = std::min(least, against[k]);
	}
	if (least == std::numeric_limits<float>::max())
		return false;
	base = least;
	float most = 0;
	for (size_t k = 0; k < count; k++) {
		extra[k] = has[k] ? against[k] - least : 0.F;
		most = std::max(most, extra[k]);
	}
	return most >= LeastSpread;
}

} // namespace hd

} // namespace devilution
