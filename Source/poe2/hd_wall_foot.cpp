#include "poe2/hd_wall_foot.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

#include "engine/light_tables.hpp"
#include "engine/render/dun_render.hpp"
#include "engine/render/light_render.hpp"
#include "engine/surface.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/reencode_dun_cels.hpp"
#include "utils/endian_swap.hpp"
#include "utils/is_of.hpp"
#include "utils/sdl_wrap.h"

namespace devilution {

namespace hd {

namespace {

/** @brief Draws a frame as the game does at a tile's foot, over a backdrop of `fill`. */
std::array<uint8_t, FootPixelCount> DrawFoot(const uint8_t *src, TileType tile, int height, uint8_t fill)
{
	std::array<uint8_t, FootPixelCount> out {};
	SDLSurfaceUniquePtr surface = SDLWrap::CreateRGBSurfaceWithFormat(0, FootSize, FootSize, 8, SDL_PIXELFORMAT_INDEX8);
	const Surface target(surface.get());
	std::memset(surface->pixels, fill, static_cast<size_t>(surface->pitch) * FootSize);
	// Fully lit: the frame's own colours.
	static std::vector<uint8_t> unlit;
	const size_t size = static_cast<size_t>(target.pitch()) * (FootSize + 1);
	if (unlit.size() < size)
		unlit.assign(size, 0);
	const Lightmap lightmap(target.at(0, 0), std::span<const uint8_t>(unlit.data(), size), static_cast<uint16_t>(target.pitch()), LightTables, FullyLitLightTable, FullyDarkLightTable);
	RenderTileFrame(target, lightmap, Point { 0, FootSize - 1 }, tile, src, static_cast<int_fast16_t>(height), MaskType::Solid, FullyLitLightTable);
	for (int y = 0; y < FootSize; y++)
		std::memcpy(&out[static_cast<size_t>(y) * FootSize], static_cast<const uint8_t *>(surface->pixels) + static_cast<ptrdiff_t>(y) * surface->pitch, FootSize);
	return out;
}

/** @brief The 4-connected bits of one part: each pixel's bit (-1 for other parts), and how many bits. */
int Bits(const FootParts &parts, FootPart part, std::array<int, FootPixelCount> &bitOf)
{
	bitOf.fill(-1);
	int count = 0;
	std::vector<int> stack;
	for (int start = 0; start < FootPixelCount; start++) {
		if (parts[static_cast<size_t>(start)] != part || bitOf[static_cast<size_t>(start)] >= 0)
			continue;
		stack.push_back(start);
		bitOf[static_cast<size_t>(start)] = count;
		while (!stack.empty()) {
			const int at = stack.back();
			stack.pop_back();
			const int x = at % FootSize;
			const int y = at / FootSize;
			const int next[4][2] { { x - 1, y }, { x + 1, y }, { x, y - 1 }, { x, y + 1 } };
			for (const auto &[nx, ny] : next) {
				if (nx < 0 || ny < 0 || nx >= FootSize || ny >= FootSize)
					continue;
				const auto n = static_cast<size_t>(ny * FootSize + nx);
				if (parts[n] != part || bitOf[n] >= 0)
					continue;
				bitOf[n] = count;
				stack.push_back(static_cast<int>(n));
			}
		}
		count++;
	}
	return count;
}

} // namespace

FootPixels ReadFootPiece(const uint8_t *src, TileType tile, int height)
{
	FootPixels piece;
	const auto dark = DrawFoot(src, tile, height, 0);
	const auto light = DrawFoot(src, tile, height, 255);
	for (size_t i = 0; i < FootPixelCount; i++) {
		// Drawn where it covers either backdrop the same way.
		const bool drawn = dark[i] == light[i];
		piece.drawn[i] = drawn ? 255 : 0;
		piece.index[i] = drawn ? dark[i] : 0;
	}
	return piece;
}

FootFloors LevelFootFloors()
{
	FootFloors floors;
	if (pDungeonCels == nullptr)
		return floors;
	const uint32_t frames = Swap32LE(reinterpret_cast<const uint32_t *>(pDungeonCels.get())[0]);
	std::unordered_set<uint16_t> seen;
	std::vector<FootPixels> standing;
	for (size_t piece = 0; piece < MAXTILES; piece++) {
		bool wallFoot = false;
		for (uint8_t block = 0; block < 2; block++) {
			const LevelCelBlock cel = DPieceMicros[piece].mt[block];
			// (Pieces past the level's own may still hold another level's.)
			if (!cel.hasValue() || cel.frame() > frames)
				continue;
			const TileType type = cel.type();
			const DunFrameInfo info { block, type, SOLData[piece] };
			// A floor piece: drawn as a triangle (a floor with foliage over it holds its floor as one, first).
			const bool floor = IsAnyOf(type, TileType::LeftTriangle, TileType::RightTriangle) || (type == TileType::TransparentSquare && info.isFloor());
			wallFoot = wallFoot || !floor;
			if (!floor || !seen.insert(cel.frame()).second)
				continue;
			const bool right = type == TileType::TransparentSquare ? !info.isFloorLeft() : type == TileType::RightTriangle;
			const uint8_t *data = GetDunFrame(pDungeonCels.get(), cel.frame());
			(right ? floors.right : floors.left).push_back(ReadFootPiece(data, right ? TileType::RightTriangle : TileType::LeftTriangle, DunFrameTriangleHeight));
		}
		// The two pieces above a wall's foot: the wall itself, or a door, a post, a barrel standing there.
		for (uint8_t block = 2; wallFoot && block < 4; block++) {
			const LevelCelBlock cel = DPieceMicros[piece].mt[block];
			if (!cel.hasValue() || cel.frame() > frames || !seen.insert(cel.frame()).second)
				continue;
			standing.push_back(ReadFootPiece(GetDunFrame(pDungeonCels.get(), cel.frame()), cel.type(), DunFrameHeight));
		}
	}
	CountFootColours(floors, standing);
	return floors;
}

std::vector<FootPixels> FootAbove(size_t piece, bool right)
{
	std::vector<FootPixels> above;
	if (pDungeonCels == nullptr || piece >= MAXTILES)
		return above;
	const uint32_t frames = Swap32LE(reinterpret_cast<const uint32_t *>(pDungeonCels.get())[0]);
	for (size_t block = right ? 3 : 2; block < 6; block += 2) {
		const LevelCelBlock cel = DPieceMicros[piece].mt[block];
		if (cel.hasValue() && cel.frame() <= frames)
			above.push_back(ReadFootPiece(GetDunFrame(pDungeonCels.get(), cel.frame()), cel.type(), DunFrameHeight));
	}
	return above;
}

void CountFootColours(FootFloors &floors, const std::vector<FootPixels> &standing)
{
	std::array<double, 256> floorCount {};
	std::array<double, 256> wallCount {};
	double floorAll = 0;
	double wallAll = 0;
	for (const auto *list : { &floors.left, &floors.right }) {
		for (const FootPixels &floor : *list) {
			for (size_t i = 0; i < FootPixelCount; i++) {
				if (floor.drawn[i] == 0)
					continue;
				floorCount[floor.index[i]]++;
				floorAll++;
			}
		}
	}
	for (const FootPixels &piece : standing) {
		for (size_t i = 0; i < FootPixelCount; i++) {
			if (piece.drawn[i] == 0)
				continue;
			wallCount[piece.index[i]]++;
			wallAll++;
		}
	}
	floors.coloured = floorAll > 0 && wallAll > 0;
	for (size_t c = 0; c < 256; c++) {
		floors.floorShare[c] = floorAll > 0 ? static_cast<float>(floorCount[c] / floorAll) : 0.F;
		floors.wallShare[c] = wallAll > 0 ? static_cast<float>(wallCount[c] / wallAll) : 0.F;
	}
}

bool InFootDiamond(bool right, int x, int y)
{
	// As the game draws a floor piece at a tile's foot: 31 rows from the second, 2 pixels wide at the top and the
	// bottom and the whole column at the middle row, against the column's inner edge.
	if (y < 1 || y >= FootSize || x < 0 || x >= FootSize)
		return false;
	const int width = 2 * (FootSize / 2 - std::abs(y - FootSize / 2));
	return right ? x < width : x >= FootSize - width;
}

bool SplitWallFoot(const FootPixels &piece, const FootFloors &level, bool right, FootParts &parts, const std::vector<FootPixels> &above)
{
	const std::vector<FootPixels> &floors = right ? level.right : level.left;
	const auto sameAt = [&](const FootPixels &floor, size_t step) {
		int same = 0;
		for (size_t i = 0; i < FootPixelCount; i += step)
			same += piece.drawn[i] != 0 && floor.drawn[i] != 0 && piece.index[i] == floor.index[i] ? 1 : 0;
		return same;
	};
	// The floor piece most like it: each looked at a few pixels apart first, then the likeliest pixel by pixel (a level
	// has a thousand floor pieces or so).
	constexpr size_t Sparse = 7;
	constexpr size_t Likeliest = 8;
	std::vector<std::pair<int, size_t>> likely;
	likely.reserve(floors.size());
	for (size_t f = 0; f < floors.size(); f++)
		likely.emplace_back(sameAt(floors[f], Sparse), f);
	const size_t keep = std::min(Likeliest, likely.size());
	std::partial_sort(likely.begin(), likely.begin() + static_cast<ptrdiff_t>(keep), likely.end(), [](const auto &a, const auto &b) { return a.first > b.first || (a.first == b.first && a.second < b.second); });
	const FootPixels *best = nullptr;
	int bestSame = 0;
	for (size_t k = 0; k < keep; k++) {
		const FootPixels &floor = floors[likely[k].second];
		const int same = sameAt(floor, 1);
		if (same > bestSame) {
			bestSame = same;
			best = &floor;
		}
	}
	// Under the floor: in its half diamond, as the game's floor pieces draw it.
	const auto underFloor = [&](size_t i) {
		return InFootDiamond(right, static_cast<int>(i) % FootSize, static_cast<int>(i) / FootSize);
	};
	if (best == nullptr && !level.coloured) {
		for (size_t i = 0; i < FootPixelCount; i++)
			parts[i] = piece.drawn[i] == 0 ? FootPart::Empty : underFloor(i) ? FootPart::Floor : FootPart::Wall;
		return std::any_of(parts.begin(), parts.end(), [](FootPart part) { return part == FootPart::Floor; });
	}
	// Just as in the floor piece most like it, a pixel is the floor's. Else how floor-like it is (more than nought for
	// the floor): how much more its colour is used in the floors than in what stands on them, taken over the pixels
	// round it that aren't sure either, so a colour both use goes with what is round it.
	constexpr float Most = 4.F;
	constexpr float Least = 1e-4F;
	// What stands here: the colours of this wall's own, above the floor's back edge and in the pieces above, most of
	// all; what stands anywhere on the level a little.
	std::array<float, 256> wallShare = level.wallShare;
	{
		std::array<float, 256> own {};
		float all = 0;
		const auto count = [&](const FootPixels &pixels, bool outsideOnly) {
			for (size_t i = 0; i < FootPixelCount; i++) {
				if (pixels.drawn[i] == 0 || (outsideOnly && underFloor(i)))
					continue;
				own[pixels.index[i]]++;
				all++;
			}
		};
		count(piece, true);
		for (const FootPixels &pixels : above)
			count(pixels, false);
		if (all > 0) {
			constexpr float OwnShare = 0.75F;
			for (size_t c = 0; c < 256; c++)
				wallShare[c] = (level.coloured ? (1 - OwnShare) * level.wallShare[c] : 0.F) + OwnShare * own[c] / all;
		}
	}
	const bool coloured = level.coloured;
	std::array<bool, FootPixelCount> sure {};
	std::array<float, FootPixelCount> floorLike {};
	for (size_t i = 0; i < FootPixelCount; i++) {
		if (piece.drawn[i] == 0 || !underFloor(i))
			continue;
		sure[i] = best != nullptr && best->drawn[i] != 0 && best->index[i] == piece.index[i];
		if (sure[i] || !coloured)
			continue;
		const uint8_t c = piece.index[i];
		floorLike[i] = std::clamp(std::log((level.floorShare[c] + Least) / (wallShare[c] + Least)), -Most, Most);
	}
	constexpr int Reach = 2;
	for (size_t i = 0; i < FootPixelCount; i++) {
		if (piece.drawn[i] == 0) {
			parts[i] = FootPart::Empty;
			continue;
		}
		if (!underFloor(i)) {
			parts[i] = FootPart::Wall;
			continue;
		}
		if (sure[i]) {
			parts[i] = FootPart::Floor;
			continue;
		}
		const int x = static_cast<int>(i) % FootSize;
		const int y = static_cast<int>(i) / FootSize;
		float sum = 0;
		for (int dy = -Reach; dy <= Reach; dy++) {
			for (int dx = -Reach; dx <= Reach; dx++) {
				const int nx = x + dx;
				const int ny = y + dy;
				if (nx < 0 || ny < 0 || nx >= FootSize || ny >= FootSize)
					continue;
				const auto n = static_cast<size_t>(ny * FootSize + nx);
				if (piece.drawn[n] != 0 && underFloor(n) && !sure[n])
					sum += floorLike[n];
			}
		}
		parts[i] = sum > 0 ? FootPart::Floor : FootPart::Wall;
	}

	std::array<int, FootPixelCount> bitOf;
	// Floor's colours by chance among the wall's own: a small bit of floor, or one shut in by the wall that doesn't
	// reach the floor's front edges (where it goes on into the floor in front) and isn't big.
	{
		const int count = Bits(parts, FootPart::Floor, bitOf);
		std::vector<int> size(static_cast<size_t>(count), 0);
		std::vector<bool> front(static_cast<size_t>(count), false);
		for (size_t i = 0; i < FootPixelCount; i++) {
			const int bit = bitOf[i];
			if (bit < 0)
				continue;
			size[static_cast<size_t>(bit)]++;
			const int x = static_cast<int>(i) % FootSize;
			const int y = static_cast<int>(i) / FootSize;
			if (!InFootDiamond(right, x, y + 1) || x == (right ? 0 : FootSize - 1))
				front[static_cast<size_t>(bit)] = true;
		}
		for (size_t i = 0; i < FootPixelCount; i++) {
			const auto bit = static_cast<size_t>(bitOf[i]);
			if (bitOf[i] >= 0 && (size[bit] < FootLeastFloorBits || (!front[bit] && size[bit] < FootShutInFloorBits)))
				parts[i] = FootPart::Wall;
		}
	}
	// What stands on the tile reaches out of the floor's half diamond, or on up past the piece's top: a bit of "wall"
	// that does neither is the floor's own, drawn a little otherwise here.
	{
		const int count = Bits(parts, FootPart::Wall, bitOf);
		std::vector<bool> stands(static_cast<size_t>(count), false);
		for (size_t i = 0; i < FootPixelCount; i++) {
			if (bitOf[i] >= 0 && (i < static_cast<size_t>(FootSize) || !underFloor(i)))
				stands[static_cast<size_t>(bitOf[i])] = true;
		}
		for (size_t i = 0; i < FootPixelCount; i++) {
			if (bitOf[i] >= 0 && !stands[static_cast<size_t>(bitOf[i])])
				parts[i] = FootPart::Floor;
		}
	}
	return std::any_of(parts.begin(), parts.end(), [](FootPart part) { return part == FootPart::Floor; });
}

void SplitFootArt(const uint8_t *rgba, int width, int height, int pitch, const FootParts &parts, int scale, uint8_t *floorOut, uint8_t *wallOut, int &floorCount, int &wallCount)
{
	// The game's empty pixels go with a part beside them: the wall's first, which is drawn over the floor.
	FootParts settled = parts;
	for (int y = 0; y < FootSize; y++) {
		for (int x = 0; x < FootSize; x++) {
			const auto i = static_cast<size_t>(y * FootSize + x);
			if (parts[i] != FootPart::Empty)
				continue;
			bool wall = false;
			bool floor = false;
			for (int dy = -1; dy <= 1; dy++) {
				for (int dx = -1; dx <= 1; dx++) {
					const int nx = x + dx;
					const int ny = y + dy;
					if (nx < 0 || ny < 0 || nx >= FootSize || ny >= FootSize)
						continue;
					const FootPart near = parts[static_cast<size_t>(ny * FootSize + nx)];
					wall = wall || near == FootPart::Wall;
					floor = floor || near == FootPart::Floor;
				}
			}
			settled[i] = !wall && floor ? FootPart::Floor : FootPart::Wall;
		}
	}
	floorCount = 0;
	wallCount = 0;
	for (int y = 0; y < height; y++) {
		const uint8_t *row = rgba + static_cast<ptrdiff_t>(y) * pitch;
		const int gy = std::clamp(y / std::max(scale, 1), 0, FootSize - 1);
		for (int x = 0; x < width; x++) {
			const int gx = std::clamp(x / std::max(scale, 1), 0, FootSize - 1);
			const bool isFloor = settled[static_cast<size_t>(gy * FootSize + gx)] == FootPart::Floor;
			const size_t at = (static_cast<size_t>(y) * width + x) * 4;
			// The colour stays in both, so the picture's edge keeps it.
			std::memcpy(floorOut + at, row + static_cast<ptrdiff_t>(x) * 4, 4);
			std::memcpy(wallOut + at, row + static_cast<ptrdiff_t>(x) * 4, 4);
			const uint8_t alpha = row[x * 4 + 3];
			(isFloor ? wallOut : floorOut)[at + 3] = 0;
			if (alpha != 0)
				(isFloor ? floorCount : wallCount)++;
		}
	}
}

} // namespace hd

} // namespace devilution
