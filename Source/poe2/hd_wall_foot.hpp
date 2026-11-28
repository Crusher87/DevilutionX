#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "levels/dun_tile.hpp"

namespace devilution {

namespace hd {

// POE2 Mashup: the foot of a wall and the floor in front of it, told apart pixel by pixel (feel("wallfoot")). A tile's
// bottom piece holds both: the floor's half diamond and, above its back edge, the foot of the tile's wall. The HD world
// drew the half diamond in the floor's light and the rest in the wall's, cut along the floor's back edge, so where the
// wall reaches over that edge (a door leaf standing at the tile's front edge) its pixels took the floor's light, and the
// light stepped across it along a straight line. Here each pixel of the piece goes with the wall or with the floor, found
// once from the game's own pixels (SplitWallFoot): by the level's floor pieces, and by the colours its floors use and
// its walls do. Without the renderer, so it can be tested.

/** @brief A bottom piece's side, in game pixels. */
constexpr int FootSize = 32;
constexpr int FootPixelCount = FootSize * FootSize;

/** @brief A piece's own 8-bit pixels as the game draws them at a tile's foot (bottom row 31): colour, and drawn or not. */
struct FootPixels {
	std::array<uint8_t, FootPixelCount> index {};
	std::array<uint8_t, FootPixelCount> drawn {};
};

/**
 * @brief The level's floor pieces, by the column they are drawn in, as the floors a wall's foot may stand on, and how
 * much each colour is used in its floors and in what stands on them (CountFootColours).
 */
struct FootFloors {
	std::vector<FootPixels> left;
	std::vector<FootPixels> right;
	/** @brief Each colour's share of the floors' pixels and of the standing pieces' (0 to 1, each adding up to 1). */
	std::array<float, 256> floorShare {};
	std::array<float, 256> wallShare {};
	bool coloured = false;
};

/** @brief Counts the colours of `floors`' floor pieces and of `standing`: pieces of walls and what else stands on the tiles. */
void CountFootColours(FootFloors &floors, const std::vector<FootPixels> &standing);

enum class FootPart : uint8_t {
	Empty,
	Floor,
	Wall,
};
using FootParts = std::array<FootPart, FootPixelCount>;

/** @brief Smaller bits of floor inside a wall are the wall's own pixels that happen to have the floor's colour. */
constexpr int FootLeastFloorBits = 6;
/** @brief ...and so are bits of floor shut in by the wall, away from the floor's front edges, smaller than this. */
constexpr int FootShutInFloorBits = 48;

/** @brief A frame of the level's tiles drawn as the game draws it at a tile's foot, `height` rows above its bottom. */
FootPixels ReadFootPiece(const uint8_t *src, TileType tile, int height);

/** @brief The two pieces above a tile's bottom piece in its column (of `piece`, the level's MICROS): what stands there. */
std::vector<FootPixels> FootAbove(size_t piece, bool right);

/** @brief The floor pieces of the level that is loaded (pDungeonCels, DPieceMicros), each frame once. */
FootFloors LevelFootFloors();

/**
 * @brief Each pixel of a wall's bottom piece: the wall's or the floor's (or nothing).
 *
 * Above the floor's back edge (out of its half diamond) it is all wall. In the half diamond, a pixel is the floor's
 * where it is just as in the floor piece most like this one, or where the pixels round it are of colours the level's
 * floors use more than what stands on them (CountFootColours), and above all more than this wall does: above the
 * floor's back edge and in `above`, the pieces above this one. Then a bit of wall that stands nowhere outside the half
 * diamond, nor reaches the piece's top (where what stands on the tile goes on up), is the floor's own (a crack or a
 * stain drawn where the wall stands), and a bit of floor of fewer than FootLeastFloorBits pixels, or shut in by the wall
 * (FootShutInFloorBits), is the wall's. With
 * nothing to go by, the piece is cut along the floor's back edge, as before.
 * @return Whether some of it is floor.
 */
bool SplitWallFoot(const FootPixels &piece, const FootFloors &floors, bool right, FootParts &parts, const std::vector<FootPixels> &above = {});

/** @brief Whether a game pixel of a bottom piece is in the floor's half diamond (the cut along its back edge). */
bool InFootDiamond(bool right, int x, int y);

/**
 * @brief Cuts a bottom piece's HD picture (RGBA32, `scale` times the game's pixels) in two by `parts`: its floor and
 * its wall, each the picture with the other's pixels see-through. Drawn one over the other they are the picture again,
 * each pixel in one of them only. A pixel the game leaves empty (an HD picture's soft edge) goes with the wall beside
 * it, else the floor beside it, else the wall.
 * @param floorOut, wallOut width * height * 4 bytes each, rows packed.
 * @param floorCount, wallCount How many of the picture's drawn pixels went to each.
 */
void SplitFootArt(const uint8_t *rgba, int width, int height, int pitch, const FootParts &parts, int scale, uint8_t *floorOut, uint8_t *wallOut, int &floorCount, int &wallCount);

} // namespace hd

} // namespace devilution
