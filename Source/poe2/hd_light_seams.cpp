#include "poe2/hd_light_seams.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace devilution {

namespace hd {

void OpenColumnEnds(float tx, float ty, bool right, bool smooth, LightPoint &from, LightPoint &to)
{
	// The column's outer corner, or a quarter tile in from it as before; its inner edge is the tile's middle either way.
	const float out = smooth ? 0.5F : 0.25F;
	if (right) {
		from = { tx, ty };
		to = { tx + out, ty - out };
	} else {
		from = { tx - out, ty + out };
		to = { tx, ty };
	}
}

LightPoint FacePoint(float tx, float ty, bool right, bool smooth, float along)
{
	const float at = smooth ? std::clamp(along, -0.5F, 0.5F) : 0.F;
	// The face on the y-min edge runs from the tile's top corner (left on screen) to its right corner, along +x; the one
	// on the x-min edge from its left corner to its top corner, along -y.
	if (right)
		return { tx + at, ty - 0.5F };
	return { tx - 0.5F, ty - at };
}

LightPoint PieceFloorPoint(bool right, float fx, float fy)
{
	if (right)
		return { 0.5F - (1.F - fy - fx / 2), -0.5F + (fy - fx / 2) };
	return { -0.5F + (fx + 2 * fy - 1) / 2, 0.5F - (fx - 2 * fy + 1) / 2 };
}

void FoliageMesh(bool right, std::array<MeshVertex, FoliageVertexCount> &vertices, std::array<int, FoliageIndexCount> &indices)
{
	// A grid a quarter of the piece each way (8 by 4 game pixels). The piece is the top half of the tile's bottom piece:
	// its bottom is the floor diamond's middle row, and the tile's back edge runs corner to corner across it, from its
	// bottom left to its top right (left piece) or from its top left to its bottom right (right piece). Each square is cut
	// along that same way, so the back edge is made of the squares' cuts, with a vertex at each quarter.
	constexpr int Side = FoliageGridSide;
	for (int j = 0; j < Side; j++) {
		for (int i = 0; i < Side; i++) {
			const float fx = static_cast<float>(i) / (Side - 1);
			const float fy = static_cast<float>(j) / (Side - 1);
			const LightPoint floor = PieceFloorPoint(right, fx, fy / 2);
			vertices[static_cast<size_t>(j * Side + i)] = MeshVertex { fx, fy, floor.x, floor.y };
		}
	}
	int used = 0;
	for (int j = 0; j + 1 < Side; j++) {
		for (int i = 0; i + 1 < Side; i++) {
			const int a = j * Side + i;
			const int b = a + 1;
			const int c = a + Side;
			const int d = c + 1;
			const std::array<int, 6> cut = right ? std::array<int, 6> { a, b, d, a, d, c } : std::array<int, 6> { a, b, c, b, d, c };
			for (const int v : cut)
				indices[static_cast<size_t>(used++)] = v;
		}
	}
}

float AgainstOf(float shade, float flat, float strength)
{
	if (!(flat > 1e-6F))
		return 1.F;
	return std::clamp(1 + (shade / flat - 1) * strength, 0.15F, 2.5F);
}

float SharesFromAgainst(const float *against, const bool *has, size_t count, float &base, float *extra)
{
	float least = std::numeric_limits<float>::max();
	for (size_t k = 0; k < count; k++) {
		if (has[k])
			least = std::min(least, against[k]);
	}
	if (least == std::numeric_limits<float>::max())
		least = 1.F;
	base = least;
	float most = 0;
	for (size_t k = 0; k < count; k++) {
		extra[k] = has[k] ? against[k] - least : 0.F;
		most = std::max(most, extra[k]);
	}
	return most;
}

void CornerCell(float x, int &corner, float &t)
{
	const int k = static_cast<int>(std::floor(x - 0.5F));
	corner = 2 * k + 1;
	t = std::clamp(x - (static_cast<float>(k) + 0.5F), 0.F, 1.F);
}

float BlendCorners(float c00, float c10, float c01, float c11, float u, float v)
{
	return (c00 * (1 - u) + c10 * u) * (1 - v) + (c01 * (1 - u) + c11 * u) * v;
}

} // namespace hd

} // namespace devilution
