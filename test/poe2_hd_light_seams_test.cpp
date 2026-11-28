#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif

#include "poe2/hd_light_seams.hpp"

// POE2 Mashup: no step in the light where two of the HD world's tiles meet (feel("lightseams"), hd_light_seams.hpp).
// Every piece takes its light from points it shares with the pieces beside it, so both sides of a shared edge take the
// same light there, and a tile's light on average stays what it was.

namespace devilution {
namespace {

using hd::LightPoint;

/** @brief A light over the floor as light::At gives it: samples half a tile apart, a quarter tile either side of each tile's middle, blended. */
struct Field {
	std::function<float(float, float)> source;

	static float SampleAt(int i) { return static_cast<float>(i) / 2 - 0.25F; }

	float operator()(float x, float y) const
	{
		const float fi = (x + 0.25F) * 2;
		const float fj = (y + 0.25F) * 2;
		const int i = static_cast<int>(std::floor(fi));
		const int j = static_cast<int>(std::floor(fj));
		const float u = fi - static_cast<float>(i);
		const float v = fj - static_cast<float>(j);
		const auto at = [&](int a, int b) { return source(SampleAt(a), SampleAt(b)); };
		return (at(i, j) * (1 - u) + at(i + 1, j) * u) * (1 - v) + (at(i, j + 1) * (1 - u) + at(i + 1, j + 1) * u) * v;
	}
	float operator()(LightPoint p) const { return (*this)(p.x, p.y); }
};

/** @brief A lamp's light: bright by it, gone four tiles off. */
Field Lamp(float lx, float ly)
{
	return Field { [lx, ly](float x, float y) {
		const float d = std::sqrt((x - lx) * (x - lx) + (y - ly) * (y - ly));
		return std::clamp(1.F - d / 4.F, 0.F, 1.F);
	} };
}

/** @brief Light at random at each sample: the worst case for a step. */
Field Noise(uint32_t seed)
{
	return Field { [seed](float x, float y) {
		uint32_t h = seed ^ (static_cast<uint32_t>(std::lround(x * 4) + 1000) * 73856093U) ^ (static_cast<uint32_t>(std::lround(y * 4) + 1000) * 19349663U);
		h ^= h >> 13;
		h *= 0x5bd1e995U;
		h ^= h >> 15;
		return static_cast<float>(h & 0xFFFF) / 65535.F;
	} };
}

Field Linear()
{
	return Field { [](float x, float y) { return 0.3F + 0.05F * x - 0.03F * y; } };
}

float ColumnMean(const Field &light, float tx, float ty, bool right, bool smooth)
{
	LightPoint from;
	LightPoint to;
	hd::OpenColumnEnds(tx, ty, right, smooth, from, to);
	return (light(from) + light(to)) / 2;
}

TEST(HdLightSeams, OpenColumnsMeetWithTheSameLight)
{
	const Field light = Noise(7);
	float oldStep = 0;
	for (int ty = 5; ty < 12; ty++) {
		for (int tx = 5; tx < 12; tx++) {
			const auto fx = static_cast<float>(tx);
			const auto fy = static_cast<float>(ty);
			for (const bool smooth : { false, true }) {
				LightPoint leftFrom;
				LightPoint leftTo;
				LightPoint rightFrom;
				LightPoint rightTo;
				LightPoint besideFrom;
				LightPoint besideTo;
				hd::OpenColumnEnds(fx, fy, false, smooth, leftFrom, leftTo);
				hd::OpenColumnEnds(fx, fy, true, smooth, rightFrom, rightTo);
				// On screen, left of this tile's left column is the right column of the tile at (x - 1, y + 1).
				hd::OpenColumnEnds(fx - 1, fy + 1, true, smooth, besideFrom, besideTo);
				// A tile's own two columns meet at its middle either way.
				EXPECT_FLOAT_EQ(light(leftTo), light(rightFrom));
				if (smooth)
					EXPECT_FLOAT_EQ(light(leftFrom), light(besideTo)) << tx << "," << ty;
				else
					oldStep = std::max(oldStep, std::abs(light(leftFrom) - light(besideTo)));
			}
		}
	}
	// As it was, the columns stepped at their edges.
	EXPECT_GT(oldStep, 0.05F);
}

TEST(HdLightSeams, OpenTileKeepsItsAverageLight)
{
	// Light changing evenly: the tile's average is exactly what it was.
	const Field even = Linear();
	for (const float tx : { 3.F, 10.F, 40.F }) {
		const float was = (ColumnMean(even, tx, 7, false, false) + ColumnMean(even, tx, 7, true, false)) / 2;
		const float now = (ColumnMean(even, tx, 7, false, true) + ColumnMean(even, tx, 7, true, true)) / 2;
		EXPECT_NEAR(now, was, 1e-5F);
	}
	// By a lamp, within a little: most where the lamp's light peaks inside the tile itself (the corners lie further from
	// the peak than the points a quarter tile in did) and where it gives out; a few hundredths of full light at most.
	constexpr float Lx = 10.3F;
	constexpr float Ly = 8.6F;
	const Field lamp = Lamp(Lx, Ly);
	for (int ty = 5; ty < 13; ty++) {
		for (int tx = 6; tx < 15; tx++) {
			const auto fx = static_cast<float>(tx);
			const auto fy = static_cast<float>(ty);
			const float was = (ColumnMean(lamp, fx, fy, false, false) + ColumnMean(lamp, fx, fy, true, false)) / 2;
			const float now = (ColumnMean(lamp, fx, fy, false, true) + ColumnMean(lamp, fx, fy, true, true)) / 2;
			const bool byLamp = std::abs(fx - Lx) <= 1 && std::abs(fy - Ly) <= 1;
			EXPECT_NEAR(now, was, byLamp ? 0.04F : 0.025F) << tx << "," << ty;
		}
	}
}

TEST(HdLightSeams, WallFacesInARowShareTheirEnds)
{
	for (const float tx : { 4.F, 9.F }) {
		for (const float ty : { 6.F, 13.F }) {
			// The face on the x-min edge runs along y: the next one in its row is at y + 1, its screen-left end ours.
			LightPoint a = hd::FacePoint(tx, ty, false, true, -0.5F);
			LightPoint b = hd::FacePoint(tx, ty + 1, false, true, 0.5F);
			EXPECT_FLOAT_EQ(a.x, b.x);
			EXPECT_FLOAT_EQ(a.y, b.y);
			// The face on the y-min edge runs along x.
			a = hd::FacePoint(tx, ty, true, true, 0.5F);
			b = hd::FacePoint(tx + 1, ty, true, true, -0.5F);
			EXPECT_FLOAT_EQ(a.x, b.x);
			EXPECT_FLOAT_EQ(a.y, b.y);
			// Off, every point of a face is its middle, as before.
			const LightPoint middle = hd::FacePoint(tx, ty, true, false, 0.4F);
			EXPECT_FLOAT_EQ(middle.x, tx);
			EXPECT_FLOAT_EQ(middle.y, ty - 0.5F);
			const LightPoint left = hd::FacePoint(tx, ty, false, false, -0.3F);
			EXPECT_FLOAT_EQ(left.x, tx - 0.5F);
			EXPECT_FLOAT_EQ(left.y, ty);
		}
	}
}

TEST(HdLightSeams, PieceCornersAreTheTileCorners)
{
	const auto expect = [](bool right, float fx, float fy, float x, float y) {
		const LightPoint p = hd::PieceFloorPoint(right, fx, fy);
		EXPECT_FLOAT_EQ(p.x, x) << right << " " << fx << "," << fy;
		EXPECT_FLOAT_EQ(p.y, y) << right << " " << fx << "," << fy;
	};
	// Left half: its outer corner is the tile's left corner (-x, +y), then its top and its bottom.
	expect(false, 0, 0.5F, -0.5F, 0.5F);
	expect(false, 1, 0, -0.5F, -0.5F);
	expect(false, 1, 1, 0.5F, 0.5F);
	// Right half: the right corner (+x, -y), the top and the bottom.
	expect(true, 1, 0.5F, 0.5F, -0.5F);
	expect(true, 0, 0, -0.5F, -0.5F);
	expect(true, 0, 1, 0.5F, 0.5F);
	// Above a half's back edge lies the tile behind: the left half's top left is the middle of the tile at x - 1.
	expect(false, 0, 0, -1, 0);
	expect(true, 1, 0, 0, -1);
}

/** @brief A mesh's light at a point of its rectangle, blended in the triangle the point lies in (as the GPU does), or NaN outside it. */
float MeshLight(const std::array<hd::MeshVertex, hd::FoliageVertexCount> &mesh, const std::array<int, hd::FoliageIndexCount> &indices, const std::array<float, hd::FoliageVertexCount> &values, float px, float py)
{
	for (int t = 0; t < hd::FoliageIndexCount; t += 3) {
		const hd::MeshVertex &a = mesh[static_cast<size_t>(indices[t])];
		const hd::MeshVertex &b = mesh[static_cast<size_t>(indices[t + 1])];
		const hd::MeshVertex &c = mesh[static_cast<size_t>(indices[t + 2])];
		const float area = (b.fx - a.fx) * (c.fy - a.fy) - (c.fx - a.fx) * (b.fy - a.fy);
		if (std::abs(area) < 1e-7F)
			continue;
		const float wb = ((px - a.fx) * (c.fy - a.fy) - (c.fx - a.fx) * (py - a.fy)) / area;
		const float wc = ((b.fx - a.fx) * (py - a.fy) - (px - a.fx) * (b.fy - a.fy)) / area;
		const float wa = 1 - wb - wc;
		constexpr float Slack = -1e-5F;
		if (wa >= Slack && wb >= Slack && wc >= Slack)
			return wa * values[static_cast<size_t>(indices[t])] + wb * values[static_cast<size_t>(indices[t + 1])] + wc * values[static_cast<size_t>(indices[t + 2])];
	}
	return std::nanf("");
}

TEST(HdLightSeams, FoliageMeetsTheFloorAlongTheBackEdge)
{
	for (const bool right : { false, true }) {
		std::array<hd::MeshVertex, hd::FoliageVertexCount> mesh;
		std::array<int, hd::FoliageIndexCount> indices;
		hd::FoliageMesh(right, mesh, indices);
		for (const Field &light : { Noise(3), Lamp(9.4F, 11.2F), Linear() }) {
			constexpr float Tx = 10;
			constexpr float Ty = 10;
			std::array<float, hd::FoliageVertexCount> values;
			for (size_t v = 0; v < mesh.size(); v++)
				values[v] = light(Tx + mesh[v].wx, Ty + mesh[v].wy);
			float oldStep = 0;
			for (int step = 0; step <= 64; step++) {
				const float fx = static_cast<float>(step) / 64;
				const float fy = right ? fx : 1 - fx;
				// The back edge: the floor there is the floor of the tile behind and of this one alike.
				const LightPoint floor = hd::PieceFloorPoint(right, fx, fy / 2);
				const float want = light(Tx + floor.x, Ty + floor.y);
				EXPECT_NEAR(MeshLight(mesh, indices, values, fx, fy), want, 1e-5F) << right << " " << fx;
				LightPoint from;
				LightPoint to;
				hd::OpenColumnEnds(Tx, Ty, right, false, from, to);
				oldStep = std::max(oldStep, std::abs(light(from) * (1 - fx) + light(to) * fx - want));
			}
			// Lit as a thing standing on the tile, it took other light than the floor right there.
			EXPECT_GT(oldStep, 0.01F);
		}
	}
}

TEST(HdLightSeams, FoliageTakesTheFloorsLightAllOver)
{
	// Everywhere in it, close to the floor's own light under it: the blend in triangles a quarter tile wide is close
	// to light::At's own, and on average the same.
	const Field lamp = Lamp(9.4F, 11.2F);
	for (const bool right : { false, true }) {
		std::array<hd::MeshVertex, hd::FoliageVertexCount> mesh;
		std::array<int, hd::FoliageIndexCount> indices;
		hd::FoliageMesh(right, mesh, indices);
		constexpr float Tx = 10;
		constexpr float Ty = 10;
		std::array<float, hd::FoliageVertexCount> values;
		for (size_t v = 0; v < mesh.size(); v++)
			values[v] = lamp(Tx + mesh[v].wx, Ty + mesh[v].wy);
		double meshSum = 0;
		double floorSum = 0;
		int count = 0;
		for (int j = 0; j < 32; j++) {
			for (int i = 0; i < 64; i++) {
				const float fx = (static_cast<float>(i) + 0.5F) / 64;
				const float fy = (static_cast<float>(j) + 0.5F) / 32;
				const float got = MeshLight(mesh, indices, values, fx, fy);
				ASSERT_FALSE(std::isnan(got)) << "the mesh leaves a hole at " << fx << "," << fy;
				const LightPoint floor = hd::PieceFloorPoint(right, fx, fy / 2);
				const float want = lamp(Tx + floor.x, Ty + floor.y);
				EXPECT_NEAR(got, want, 0.01F);
				meshSum += got;
				floorSum += want;
				count++;
			}
		}
		EXPECT_NEAR(meshSum / count, floorSum / count, 0.002);
	}
}

TEST(HdLightSeams, CornerBlendIsContinuousAcrossTileEdges)
{
	// Values kept at the tiles' corners, blended in each tile: the same on both sides of every edge.
	const auto corner = [](int cx, int cy) { return static_cast<float>(((cx * 31 + cy * 17) & 15)) / 15.F; };
	const auto at = [&](float x, float y) {
		int cx;
		int cy;
		float u;
		float v;
		hd::CornerCell(x, cx, u);
		hd::CornerCell(y, cy, v);
		return hd::BlendCorners(corner(cx, cy), corner(cx + 2, cy), corner(cx, cy + 2), corner(cx + 2, cy + 2), u, v);
	};
	for (int k = 3; k < 9; k++) {
		const float edge = static_cast<float>(k) + 0.5F;
		for (int s = 0; s <= 8; s++) {
			const float along = 4.F + static_cast<float>(s) / 8;
			EXPECT_NEAR(at(edge - 1e-4F, along), at(edge + 1e-4F, along), 1e-3F);
			EXPECT_NEAR(at(along, edge - 1e-4F), at(along, edge + 1e-4F), 1e-3F);
		}
		// At a corner, its own value.
		EXPECT_FLOAT_EQ(at(edge, edge), corner(2 * k + 1, 2 * k + 1));
	}
	// A tile's average is the average of its corners.
	double sum = 0;
	for (int j = 0; j < 40; j++) {
		for (int i = 0; i < 40; i++)
			sum += at(4.5F + (static_cast<float>(i) + 0.5F) / 40, 6.5F + (static_cast<float>(j) + 0.5F) / 40);
	}
	EXPECT_NEAR(sum / 1600, (corner(9, 13) + corner(11, 13) + corner(9, 15) + corner(11, 15)) / 4, 1e-4);
}

TEST(HdLightSeams, ShapeSharesLightAPixelTheSameWhicheverWaysAPieceHas)
{
	// A pixel facing up and a little to +x, lit at a corner two tiles share: one has a picture for every way of facing,
	// the other only up and +x. The pixel comes out the same in both (the art times base, plus each way's share).
	const std::array<float, 5> against { 1.4F, 0.6F, 0.9F, 1.1F, 1.F };
	const std::array<float, 5> pixel { 0.2F, 0.F, 0.F, 0.F, 0.8F };
	const auto lit = [&](const std::array<bool, 5> &has) {
		float base;
		std::array<float, 5> extra;
		hd::SharesFromAgainst(against.data(), has.data(), 5, base, extra.data());
		float sum = base;
		for (size_t k = 0; k < 5; k++)
			sum += pixel[k] * extra[k];
		return sum;
	};
	const float all = lit({ true, true, true, true, true });
	const float some = lit({ true, false, false, false, true });
	EXPECT_NEAR(all, some, 1e-5F);
	EXPECT_NEAR(all, 0.2F * 1.4F + 0.8F * 1.F, 1e-5F);
	// Flat floor facing up takes exactly the floor's light.
	EXPECT_FLOAT_EQ(hd::AgainstOf(0.7F, 0.7F, 1.F), 1.F);
	EXPECT_FLOAT_EQ(hd::AgainstOf(0.7F, 0.F, 1.F), 1.F);
	float base;
	std::array<float, 5> extra;
	const std::array<bool, 5> none {};
	EXPECT_FLOAT_EQ(hd::SharesFromAgainst(against.data(), none.data(), 5, base, extra.data()), 0.F);
	EXPECT_FLOAT_EQ(base, 1.F);
}

/**
 * @brief Renders a little of the floor with SDL's software renderer, in flat grey: two tiles of floor, lit at each point
 * by the light there (as the floor's light layer does), and the foliage of the front one reaching a few pixels over its
 * back edge onto the floor behind. Returns the largest jump in brightness found where the foliage's top meets that floor,
 * over and above the floor's own change from one row to the next.
 */
int FoliageEdgeJump(const Field &light, bool smooth)
{
	constexpr int Zoom = 4;
	constexpr int Width = 160 * Zoom;
	constexpr int Height = 96 * Zoom;
	constexpr float Tx = 10;
	constexpr float Ty = 10;
	// A tile's middle at (x - y) * 32, (x + y) * 16 game pixels, tile (10, 10) in the middle of the picture.
	const auto screen = [&](float x, float y) {
		return SDL_FPoint { (static_cast<float>(Width) / Zoom / 2 + (x - y - (Tx - Ty)) * 32) * Zoom, (static_cast<float>(Height) / Zoom / 2 + (x + y - (Tx + Ty)) * 16) * Zoom };
	};
	SDL_Surface *target = SDL_CreateRGBSurfaceWithFormat(0, Width, Height, 32, SDL_PIXELFORMAT_ARGB8888);
	SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(target);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	constexpr uint8_t Grey = 200;
	const auto colour = [](float value) {
		const auto byte = static_cast<uint8_t>(std::clamp(value * Grey, 0.F, 255.F) + 0.5F);
		return SDL_Color { byte, byte, byte, 255 };
	};
	// The floor: each tile in triangles an eighth of a tile wide, lit at each corner.
	constexpr int Cuts = 8;
	for (const auto &[x, y] : { std::pair { Tx - 1, Ty }, std::pair { Tx, Ty } }) {
		std::vector<SDL_Vertex> vertices;
		std::vector<int> indices;
		for (int j = 0; j <= Cuts; j++) {
			for (int i = 0; i <= Cuts; i++) {
				const float wx = x - 0.5F + static_cast<float>(i) / Cuts;
				const float wy = y - 0.5F + static_cast<float>(j) / Cuts;
				vertices.push_back(SDL_Vertex { screen(wx, wy), colour(light(wx, wy)), { 0, 0 } });
			}
		}
		for (int j = 0; j < Cuts; j++) {
			for (int i = 0; i < Cuts; i++) {
				const int c = j * (Cuts + 1) + i;
				for (const int v : { c, c + 1, c + Cuts + 2, c, c + Cuts + 2, c + Cuts + 1 })
					indices.push_back(v);
			}
		}
		SDL_RenderGeometry(renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()), indices.data(), static_cast<int>(indices.size()));
	}
	// The front tile's left foliage: white where it is, up to 5 game pixels over the back edge, see-through elsewhere.
	constexpr int Reach = 5;
	const int pieceWidth = 32 * Zoom;
	const int pieceHeight = 16 * Zoom;
	SDL_Surface *art = SDL_CreateRGBSurfaceWithFormat(0, pieceWidth, pieceHeight, 32, SDL_PIXELFORMAT_ARGB8888);
	for (int py = 0; py < pieceHeight; py++) {
		auto *row = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(art->pixels) + static_cast<ptrdiff_t>(py) * art->pitch);
		for (int px = 0; px < pieceWidth; px++) {
			// The back edge runs from the bottom left to the top right: how far above it, in game pixels.
			const float above = (static_cast<float>(pieceHeight) - static_cast<float>(px) / 2 - static_cast<float>(py) - 0.5F) / Zoom;
			row[px] = above >= -2 && above <= Reach ? 0xFFFFFFFFU : 0x00FFFFFFU;
		}
	}
	SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, art);
	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	std::array<hd::MeshVertex, hd::FoliageVertexCount> mesh;
	std::array<int, hd::FoliageIndexCount> indices;
	hd::FoliageMesh(false, mesh, indices);
	const SDL_FPoint middle = screen(Tx, Ty);
	const SDL_FRect dest { middle.x - static_cast<float>(pieceWidth), middle.y - static_cast<float>(pieceHeight), static_cast<float>(pieceWidth), static_cast<float>(pieceHeight) };
	LightPoint from;
	LightPoint to;
	hd::OpenColumnEnds(Tx, Ty, false, false, from, to);
	std::array<SDL_Vertex, hd::FoliageVertexCount> vertices;
	for (size_t v = 0; v < mesh.size(); v++) {
		// As before: lit across the column from a quarter tile in to the middle, the same all the way up it.
		const float value = smooth ? light(Tx + mesh[v].wx, Ty + mesh[v].wy) : light(from) * (1 - mesh[v].fx) + light(to) * mesh[v].fx;
		vertices[v] = SDL_Vertex { { dest.x + mesh[v].fx * dest.w, dest.y + mesh[v].fy * dest.h }, colour(value), { mesh[v].fx, mesh[v].fy } };
	}
	SDL_RenderGeometry(renderer, texture, vertices.data(), hd::FoliageVertexCount, indices.data(), hd::FoliageIndexCount);
	SDL_RenderPresent(renderer);
	const auto pixel = [&](int x, int y) {
		return static_cast<int>((reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(target->pixels) + static_cast<ptrdiff_t>(y) * target->pitch)[x]) & 0xFF);
	};
	// Down each column of the picture: the foliage's top row and the floor's row just above it, against the floor's own
	// change between the two rows above that.
	int most = 0;
	for (int px = 2 * Zoom; px < pieceWidth - 2 * Zoom; px++) {
		const int x = static_cast<int>(dest.x) + px;
		for (int py = 0; py < pieceHeight; py++) {
			const auto *row = reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(art->pixels) + static_cast<ptrdiff_t>(py) * art->pitch);
			if ((row[px] >> 24) == 0)
				continue;
			const int y = static_cast<int>(dest.y) + py;
			if (py < 3)
				break;
			const int jump = std::abs(pixel(x, y) - pixel(x, y - 1));
			const int own = std::abs(pixel(x, y - 1) - pixel(x, y - 2));
			most = std::max(most, jump - own);
			break;
		}
	}
	SDL_DestroyTexture(texture);
	SDL_FreeSurface(art);
	SDL_DestroyRenderer(renderer);
	SDL_FreeSurface(target);
	return most;
}

TEST(HdLightSeams, RenderedFoliageShowsNoLineOverTheFloor)
{
	const Field lamp = Lamp(9.2F, 10.9F);
	const int before = FoliageEdgeJump(lamp, false);
	const int now = FoliageEdgeJump(lamp, true);
	// Within the rounding of a colour byte or two.
	EXPECT_LE(now, 2);
	// The line as it was: plain to see.
	EXPECT_GE(before, 6);
}

} // namespace
} // namespace devilution
