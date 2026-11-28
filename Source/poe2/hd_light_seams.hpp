#pragma once

#include <array>
#include <cstddef>

namespace devilution {

namespace hd {

// POE2 Mashup: no step in the light where two of the HD world's tiles meet (feel("lightseams")). Each piece of a tile
// used to take its light from points of its own (its middle, a quarter tile in), so two pieces side by side met with
// different light along their shared edge: a faint line along every tile's edge. Here the light of a piece comes from
// points it shares with the pieces round it (its tile's corners and edges), so both sides of every edge take the same
// light there. Without the renderer, so it can be tested.

/** @brief A point of the world (in tiles: a tile's middle is a whole number, its corners are halves). */
struct LightPoint {
	float x = 0;
	float y = 0;
};

/**
 * @brief The two points the light across one column of something standing on an open tile is taken from: its left edge
 * and its right edge on screen.
 *
 * With `smooth` the column's outer edge is the tile's corner (shared with the column beside it, of the tile next to it
 * on screen) and its inner edge the tile's middle (shared with the tile's other column). Else a quarter tile in from the
 * corner, as before, which stepped at every column's edge. For light that changes evenly the tile's average is the same.
 * @param right The tile's right column (its right corner is +x, -y of its middle), else its left (-x, +y).
 */
void OpenColumnEnds(float tx, float ty, bool right, bool smooth, LightPoint &from, LightPoint &to);

/**
 * @brief The point along a wall's face the light from the room behind it is weighed at (FromBehind): the face's middle,
 * or with `smooth` the point `along` the face (-0.5 its end at the left of the screen, 0.5 at the right), so that faces
 * meeting end to end take the same share where they meet.
 * @param right The face on the tile's y-min edge, else the one on its x-min edge.
 */
LightPoint FacePoint(float tx, float ty, bool right, bool smooth, float along);

/**
 * @brief A grid of vertices over a piece of foliage: the 16 game pixel high part of a floor tile's piece that the game
 * draws over the floor after it (grass, earth and stones that reach over the tile's back edge). Lit by the floor's light
 * at each vertex, as the floor under it is, it meets that floor with no step; lit as a thing standing on the tile, it
 * showed as a thin line along every tile's back edges.
 *
 * Vertices sit a quarter of the piece apart each way, among them where the tile's back edge crosses each quarter of
 * its width, so the light along that edge is exactly the floor's there (light::At blends samples a half tile apart, a quarter tile either side of each middle).
 */
constexpr int FoliageGridSide = 5;
constexpr int FoliageVertexCount = FoliageGridSide * FoliageGridSide;
constexpr int FoliageIndexCount = (FoliageGridSide - 1) * (FoliageGridSide - 1) * 6;
struct MeshVertex {
	/** @brief Where in the piece's rectangle, 0 to 1 each way. */
	float fx = 0;
	float fy = 0;
	/** @brief The floor's point under it, from the tile's middle (in tiles). */
	float wx = 0;
	float wy = 0;
};
void FoliageMesh(bool right, std::array<MeshVertex, FoliageVertexCount> &vertices, std::array<int, FoliageIndexCount> &indices);

/**
 * @brief The floor's point (from the tile's middle, in tiles) drawn at a point of a floor tile's bottom piece, fx and fy
 * 0 to 1 across its 32 by 32 game pixels: its half of the floor's diamond has its outer corner at (0 or 1, 0.5).
 */
LightPoint PieceFloorPoint(bool right, float fx, float fy);

/**
 * @brief A wall's or floor's shape lit by the light at each of its vertices (TileShapeLightFor), where it was lit by one
 * light for the whole piece: each way of facing's light against the floor's (1 is the same), for the axes the piece has.
 * @param against Each way of facing's light against the flat floor's, `count` of them.
 * @param base The piece's art in this times its light...
 * @param extra ...and each way's picture added in this times it (0 for ways it doesn't have).
 * @return The most of `extra`: drawn flat where that is nothing.
 */
float SharesFromAgainst(const float *against, const bool *has, size_t count, float &base, float *extra);

/** @brief A way of facing's light against the flat floor's (TileShapeLightFor): 1 + (shade / flat - 1) * strength, kept within reason. */
float AgainstOf(float shade, float flat, float strength);

/**
 * @brief Where a point lies among the tiles' corners: the corner at or before it each way (in half tiles, so a whole
 * number: corner (2k+1)/2) and how far on to the next (0 to 1). Values kept at the corners blended by these meet along
 * every tile's edge, since a tile's edge lies between two corners.
 */
void CornerCell(float x, int &corner, float &t);

/** @brief Blends four corners' values: c00 at (x-, y-), c10 (x+, y-), c01 (x-, y+), c11 (x+, y+). */
float BlendCorners(float c00, float c10, float c01, float c11, float u, float v);

} // namespace hd

} // namespace devilution
