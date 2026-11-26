#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace devilution {

namespace carved {

// Carved bodies: each hero's and monster's drawing, seen from its 8 facings, carved into one solid body made of round
// balls (tools: the shadow-shapes carving scripts, run on the HD art). The body is what the shadow of a light beside it
// is cast from: the light sees the body from its own side, not the drawing that faces the viewer.
//
// A sprite folder's bodies are in <folder>/carved.bin beside its HD pictures:
//   "CRV1", u32 poses, f32 el, ax, ay, zmin, f32 shifts[16] (x, y per facing),
//   per pose: char name[12] ("f00", "f00_m1", ...), u32 first ball, u32 balls,
//   per ball: i16 x*8, y*8, z*8 (up from the floor), r*64.
// Units: game pixels of the original (1x) drawing. A body's x runs right and y away from the viewer when it faces
// south (facing 0); facing d turns it by -45*d degrees. Its floor point is drawn at sprite pixel
// (ax + shift_x, ay + shift_y - zmin * cos(el)).

struct Ball {
	float x;
	float y;
	float z;
	float r;
};

struct Pose {
	std::string name;
	uint32_t first = 0;
	uint32_t count = 0;
};

struct Sprite {
	float el = 30;
	float ax = 0;
	float ay = 0;
	float zmin = 0;
	std::array<float, 16> shifts {};
	std::vector<Pose> poses;
	std::vector<Ball> balls;

	/** @brief A pose by name ("f05_m1"), or null. */
	[[nodiscard]] const Pose *Find(std::string_view name) const;
	[[nodiscard]] std::span<const Ball> BallsOf(const Pose &pose) const;
	/** @brief Where the body's floor point is drawn in the sprite's picture, in its game pixels, for a facing. */
	void FeetAt(int facing, float &x, float &y) const;
};

/** @brief Reads a carved.bin. False when it isn't one (nothing changes then). */
bool Parse(std::span<const uint8_t> data, Sprite &out);

/** @brief A body as one light sees it from the side, for its shadow: how much of each pixel it covers (0 to 1). */
struct Card {
	int width = 0;
	int height = 0;
	/** @brief The row of the floor; the body's floor point is at column width / 2 of it. */
	int floorRow = 0;
	/** @brief The highest row it covers. */
	int top = 0;
	std::vector<float> cover;
};

/**
 * @brief The body (its balls) as a light sees it, standing on the floor: across the light (dx, dy is the way the light
 * shines along the floor, in tiles, of length 1) and up. Across is laid out as the HD shadow lays out its picture: a
 * point `a` tiles to the right of the light's way, looking along it, is (-dy * a, dx * a) from the floor point.
 * @param lean Picture rows per unit of height (the view's lean: heights are drawn shorter than they are).
 * @param tileAcross Game pixels across one tile.
 * @param below Rows kept below the floor row.
 */
void MakeCard(std::span<const Ball> balls, int facing, float lean, float dx, float dy, float tileAcross, int below, Card &out);

/**
 * @brief Which way each pixel of a body's picture faces, as the viewer sees it: for each pixel of a width x height
 * picture (`k` picture pixels per unit), right, up and towards the eye (a unit vector) and how sure (0 to 1: 0 where
 * no ball covers it, the edges of the body fading). The nearest ball surface wins, and the facings are smoothed over
 * `smooth` pixels so the joins between balls don't show.
 */
void ViewFacings(const Sprite &sprite, std::span<const Ball> balls, int facing, float k, int width, int height, float smooth, std::vector<float> &out);

/** @brief A body's facing from its HD picture's name (".../d3_f05" is 3), or -1. */
int FacingOf(std::string_view name);

/** @brief The pose an HD picture shows ("f05", with "_m1" after in-between 1, "_b1" after 3...). */
std::string PoseOf(std::string_view name, int inBetween);

} // namespace carved

} // namespace devilution
