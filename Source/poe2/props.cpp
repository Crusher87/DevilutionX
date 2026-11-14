#include "poe2/props.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include "diablo.h"
#include "levels/dun_tile.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "monster.h"
#include "player.h"
#include "poe2/atmosphere.hpp"
#include "poe2/hd_light.hpp"
#include "poe2/moments.hpp"

namespace devilution::props {

namespace {

constexpr float Pi = 3.14159265F;

// The pictures. Each is painted here by rule, four picture pixels to a game pixel, so nothing is loaded from disk.

/** @brief Picture pixels to a game pixel. */
constexpr int Fine = 4;

struct Rgba {
	float r;
	float g;
	float b;
	float a;
};

struct Canvas {
	int width = 0;
	int height = 0;
	std::vector<uint8_t> rgba;

	void resize(int gameWidth, int gameHeight)
	{
		width = gameWidth * Fine;
		height = gameHeight * Fine;
		rgba.assign(static_cast<size_t>(width) * height * 4, 0);
	}

	/** @brief Lays a colour over a pixel. */
	void over(int x, int y, const Rgba &colour, float cover)
	{
		const float a = std::clamp(colour.a * cover, 0.F, 1.F);
		if (a <= 0 || x < 0 || y < 0 || x >= width || y >= height)
			return;
		uint8_t *at = &rgba[(static_cast<size_t>(y) * width + x) * 4];
		const float under = static_cast<float>(at[3]) / 255 * (1 - a);
		const float all = a + under;
		const std::array<float, 3> top { colour.r, colour.g, colour.b };
		for (int c = 0; c < 3; c++)
			at[c] = static_cast<uint8_t>(std::clamp((top[c] * a + static_cast<float>(at[c]) * under) / all, 0.F, 255.F));
		at[3] = static_cast<uint8_t>(std::clamp(all * 255 + 0.5F, 0.F, 255.F));
	}
};

/**
 * @brief Paints a shape: `distance` gives how far a point (in game pixels) is outside it (below 0 inside), `colour` its
 * colour there. Only the box it is given is looked at.
 */
template <typename Distance, typename Colour>
void Fill(Canvas &canvas, float x0, float y0, float x1, float y1, Distance distance, Colour colour)
{
	const int left = std::max(static_cast<int>(std::floor(x0 * Fine)) - 1, 0);
	const int top = std::max(static_cast<int>(std::floor(y0 * Fine)) - 1, 0);
	const int right = std::min(static_cast<int>(std::ceil(x1 * Fine)) + 1, canvas.width);
	const int bottom = std::min(static_cast<int>(std::ceil(y1 * Fine)) + 1, canvas.height);
	for (int y = top; y < bottom; y++) {
		for (int x = left; x < right; x++) {
			const float px = (static_cast<float>(x) + 0.5F) / Fine;
			const float py = (static_cast<float>(y) + 0.5F) / Fine;
			const float d = distance(px, py);
			const float cover = std::clamp(0.5F - d * Fine, 0.F, 1.F);
			if (cover > 0)
				canvas.over(x, y, colour(px, py, d), cover);
		}
	}
}

float ToEllipse(float px, float py, float cx, float cy, float rx, float ry)
{
	const float nx = (px - cx) / rx;
	const float ny = (py - cy) / ry;
	return (std::sqrt(nx * nx + ny * ny) - 1) * std::min(rx, ry);
}

/** @brief How far a point is from a line from a to b that is ra thick at a and rb at b (half its width). */
float ToLine(float px, float py, float ax, float ay, float ra, float bx, float by, float rb)
{
	const float dx = bx - ax;
	const float dy = by - ay;
	const float length = dx * dx + dy * dy;
	const float t = length > 0 ? std::clamp(((px - ax) * dx + (py - ay) * dy) / length, 0.F, 1.F) : 0.F;
	const float qx = px - (ax + dx * t);
	const float qy = py - (ay + dy * t);
	return std::sqrt(qx * qx + qy * qy) - (ra + (rb - ra) * t);
}

void Line(Canvas &canvas, float ax, float ay, float ra, float bx, float by, float rb, const Rgba &colour)
{
	const float r = std::max(ra, rb) + 1;
	Fill(
	    canvas, std::min(ax, bx) - r, std::min(ay, by) - r, std::max(ax, bx) + r, std::max(ay, by) + r,
	    [&](float px, float py) { return ToLine(px, py, ax, ay, ra, bx, by, rb); },
	    [&](float, float, float) { return colour; });
}

Rgba Scaled(const Rgba &colour, float by)
{
	return { colour.r * by, colour.g * by, colour.b * by, colour.a };
}

Rgba Mixed(const Rgba &a, const Rgba &b, float t)
{
	return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t };
}

/** @brief A number from 0 to 1 out of whole numbers, the same each time (this file's own, never the game's dice). */
float Hash(uint32_t a, uint32_t b, uint32_t c = 0)
{
	uint32_t h = a * 0x9E3779B1U ^ (b + 0x7F4A7C15U) * 0x85EBCA6BU ^ (c + 0x165667B1U) * 0xC2B2AE35U;
	h ^= h >> 15;
	h *= 0x2C1B3C6DU;
	h ^= h >> 12;
	h *= 0x297A2D39U;
	h ^= h >> 15;
	return static_cast<float>(h & 0xFFFFFF) / static_cast<float>(0x1000000);
}

// The chain: links of dark iron hanging straight down, a hook on the last. The picture is the longest chain; a shorter
// one shows its lower part.

constexpr int ChainWide = 12;
constexpr int ChainLong = 76;
/** @brief The hook's share of the picture's length, at its foot. */
constexpr float HookLong = 14;
constexpr float LinkEvery = 4.6F;

Canvas PaintChain()
{
	Canvas canvas;
	canvas.resize(ChainWide, ChainLong);
	const float axis = ChainWide / 2.F;
	const Rgba iron { 82, 77, 72, 1 };
	const Rgba rust { 104, 62, 42, 1 };
	const int links = static_cast<int>((ChainLong - HookLong) / LinkEvery);
	// The links seen edge on first, so the ones seen flat lie over their ends.
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i <= links; i++) {
			if ((i % 2 == 0) != (pass == 1))
				continue;
			const float cy = 2.6F + static_cast<float>(i) * LinkEvery;
			const Rgba metal = Mixed(iron, rust, 0.45F * Hash(static_cast<uint32_t>(i), 77));
			if (i % 2 == 1) {
				Fill(
				    canvas, axis - 2, cy - 4.2F, axis + 2, cy + 4.2F,
				    [&](float px, float py) { return ToLine(px, py, axis, cy - 3.1F, 0.7F, axis, cy + 3.1F, 0.7F); },
				    [&](float px, float, float d) { return Scaled(metal, (d > -0.3F ? 0.5F : 1.F) * (1.25F - 0.6F * std::abs(px - axis + 0.25F))); });
				continue;
			}
			Fill(
			    canvas, axis - 3.6F, cy - 4.4F, axis + 3.6F, cy + 4.4F,
			    [&](float px, float py) { return std::abs(ToEllipse(px, py, axis, cy, 2.25F, 3.25F)) - 0.62F; },
			    [&](float px, float py, float d) {
				    // Lit from the upper left, as the old game's own iron is.
				    const float lit = 0.92F - 0.16F * (px - axis) - 0.07F * (py - cy);
				    return Scaled(metal, (d > -0.28F ? 0.5F : 1.F) * std::clamp(lit, 0.55F, 1.5F));
			    });
		}
	}
	// The hook: a shank from the last link, round under to a point that turns back up.
	const float top = ChainLong - HookLong;
	const float cx = axis - 2.4F;
	const float cy = top + 7.2F;
	constexpr float Radius = 3.3F;
	Line(canvas, axis + 0.9F, top - 1.5F, 0.9F, axis + 0.9F, cy, 1.15F, Scaled(iron, 1.05F));
	constexpr int Steps = 22;
	for (int s = 0; s < Steps; s++) {
		const auto at = [&](int step, float &x, float &y, float &r) {
			const float t = static_cast<float>(step) / Steps;
			const float angle = t * 1.2F * Pi;
			x = cx + Radius * std::cos(angle) + (t > 0.8F ? (t - 0.8F) * 4 : 0);
			y = cy + Radius * std::sin(angle) - (t > 0.8F ? (t - 0.8F) * 9 : 0);
			r = 1.15F - 0.7F * t * t;
		};
		float ax;
		float ay;
		float ra;
		float bx;
		float by;
		float rb;
		at(s, ax, ay, ra);
		at(s + 1, bx, by, rb);
		Fill(
		    canvas, std::min(ax, bx) - 2, std::min(ay, by) - 2, std::max(ax, bx) + 2, std::max(ay, by) + 2,
		    [&](float px, float py) { return ToLine(px, py, ax, ay, ra, bx, by, rb); },
		    [&](float px, float py, float d) {
			    const float lit = 1.15F - 0.1F * (px - cx) - 0.08F * (py - cy);
			    return Scaled(iron, (d > -0.3F ? 0.6F : 1.F) * std::clamp(lit, 0.7F, 1.6F));
		    });
	}
	return canvas;
}

// The cobweb: a sheet across a corner, hung from the corner's own line and from the two walls either side.

constexpr int WebWide = 40;
constexpr int WebTall = 30;
constexpr int WebStrands = 7;

Canvas PaintWeb()
{
	Canvas canvas;
	canvas.resize(WebWide, WebTall);
	const float apexX = WebWide / 2.F;
	const float apexY = 1.5F;
	// Where a strand ends: along the web's sagging lower edge, from the left wall to the right one.
	const auto foot = [&](int strand, float &x, float &y) {
		const float t = static_cast<float>(strand) / (WebStrands - 1);
		x = 2 + (WebWide - 4) * t;
		y = 14.5F + 13 * 4 * t * (1 - t);
	};
	const auto on = [&](int strand, float along, float &x, float &y) {
		float fx;
		float fy;
		foot(strand, fx, fy);
		x = apexX + (fx - apexX) * along;
		y = apexY + (fy - apexY) * along;
	};
	const Rgba thread { 222, 220, 210, 0.6F };
	// The dust it has gathered, between the strands.
	Fill(
	    canvas, 0, 0, static_cast<float>(WebWide), static_cast<float>(WebTall),
	    [&](float px, float py) {
		    const float t = std::clamp((px - 2) / (WebWide - 4), 0.F, 1.F);
		    const float lower = 14.5F + 13 * 4 * t * (1 - t);
		    const float side = apexY + std::abs(px - apexX) / (apexX - 2) * (14.5F - apexY);
		    return std::max(py - lower, side - py) + 0.6F;
	    },
	    [&](float, float, float d) { return Rgba { thread.r, thread.g, thread.b, 0.1F * std::clamp(-d / 3, 0.F, 1.F) }; });
	for (int strand = 0; strand < WebStrands; strand++) {
		float fx;
		float fy;
		foot(strand, fx, fy);
		Line(canvas, apexX, apexY, 0.3F, fx, fy, 0.22F, thread);
	}
	// The cross threads, each sagging a little towards the corner between two strands; closer together further out.
	const std::array<float, 5> rings { 0.28F, 0.5F, 0.68F, 0.84F, 1.F };
	for (size_t ring = 0; ring < rings.size(); ring++) {
		for (int strand = 0; strand + 1 < WebStrands; strand++) {
			// A thread or two has gone: an old web is never whole.
			if (ring + 1 < rings.size() && Hash(static_cast<uint32_t>(ring), static_cast<uint32_t>(strand), 5) < 0.14F)
				continue;
			float ax;
			float ay;
			float bx;
			float by;
			on(strand, rings[ring], ax, ay);
			on(strand + 1, rings[ring], bx, by);
			const float mx = (ax + bx) / 2 + (apexX - (ax + bx) / 2) * 0.07F;
			const float my = (ay + by) / 2 + (apexY - (ay + by) / 2) * 0.07F;
			const float r = ring + 1 == rings.size() ? 0.27F : 0.2F;
			Line(canvas, ax, ay, r, mx, my, r, thread);
			Line(canvas, mx, my, r, bx, by, r, thread);
		}
	}
	return canvas;
}

// The rat: four pictures of it at a run, one under the other, nose to the right.

constexpr int RatWide = 32;
constexpr int RatTall = 14;
constexpr int RatFrames = 4;
/** @brief Where its feet touch the floor, and the middle of its body, in its picture. */
constexpr float RatFloor = 12.6F;
constexpr float RatMiddle = 17;

Canvas PaintRat()
{
	Canvas canvas;
	canvas.resize(RatWide, RatTall * RatFrames);
	// Paler than a real rat: it is seen in dim light on a dark floor, and has to stand out from it as the old game's
	// monsters do.
	const Rgba fur { 128, 110, 94, 1 };
	const Rgba belly { 188, 168, 142, 1 };
	const Rgba skin { 214, 160, 146, 1 };
	// Front and hind feet, how far forward of where the leg starts; and the body's rise in the stride.
	const std::array<float, RatFrames> front { 2.6F, 0.9F, -1.6F, 0.2F };
	const std::array<float, RatFrames> hind { -2.6F, -0.4F, 2.F, 0.6F };
	const std::array<float, RatFrames> rise { 0.5F, 0.F, -0.3F, 0.2F };
	for (int frame = 0; frame < RatFrames; frame++) {
		const float oy = static_cast<float>(frame * RatTall) - rise[frame];
		const float base = static_cast<float>(frame * RatTall);
		// Its shadow on the floor, close under it.
		Fill(
		    canvas, 6, base + 10, 29, base + 14,
		    [&](float px, float py) { return ToEllipse(px, py, 17.5F, base + RatFloor, 9.5F, 1.3F); },
		    [&](float, float, float d) { return Rgba { 0, 0, 0, 0.5F * std::clamp(-d / 1.2F, 0.F, 1.F) }; });
		// The tail first, under the body: it streams out behind and whips with the stride.
		const float whip = static_cast<float>(frame) * Pi / 2;
		for (int s = 0; s < 12; s++) {
			const auto at = [&](int step, float &x, float &y, float &r) {
				const float t = static_cast<float>(step) / 12;
				x = 11.5F - 10 * t;
				y = oy + 8.6F - 2.2F * t + 1.3F * t * std::sin(whip + t * 4.2F);
				r = 0.78F - 0.52F * t;
			};
			float ax;
			float ay;
			float ra;
			float bx;
			float by;
			float rb;
			at(s, ax, ay, ra);
			at(s + 1, bx, by, rb);
			Line(canvas, ax, ay, ra, bx, by, rb, Scaled(skin, 0.74F));
		}
		// The far legs, darker; then the body in one piece, so its dark rim runs round the whole of it; then the near legs.
		Line(canvas, 20.2F, oy + 10, 0.7F, 20.2F - front[frame] * 0.8F, base + RatFloor, 0.5F, Scaled(fur, 0.45F));
		Line(canvas, 13.2F, oy + 10, 0.9F, 13.2F - hind[frame] * 0.8F, base + RatFloor, 0.55F, Scaled(fur, 0.45F));
		Fill(
		    canvas, 7, oy + 2, 31, oy + 13,
		    [&](float px, float py) {
			    // Hunched: high over the haunch, low at the nose.
			    const float haunch = ToEllipse(px, py, 13.2F, oy + 7.3F, 4.7F, 4.1F);
			    const float trunk = ToEllipse(px, py, 17.6F, oy + 8.F, 6.1F, 3.3F);
			    const float head = ToEllipse(px, py, 23.7F, oy + 8.9F, 3.1F, 2.3F);
			    const float snout = ToLine(px, py, 25.4F, oy + 9.3F, 1.4F, 28.7F, oy + 10.1F, 0.5F);
			    return std::min({ haunch, trunk, head, snout });
		    },
		    [&](float px, float py, float d) {
			    // Dark along the back, paler under, with a dark rim and a rough coat: the old game's small sprites are
			    // drawn so.
			    const float down = std::clamp((py - (oy + 3.5F)) / 8, 0.F, 1.F);
			    const float rough = 0.88F + 0.24F * Hash(static_cast<uint32_t>(px * 2), static_cast<uint32_t>((py - oy) * 2), 31);
			    return Scaled(Mixed(Scaled(fur, 0.78F), belly, down * down), (d > -0.45F ? 0.45F : 1.F) * rough);
		    });
		// Ear, eye, nose.
		Fill(
		    canvas, 20, oy + 3, 25, oy + 9,
		    [&](float px, float py) { return ToEllipse(px, py, 22.4F, oy + 6.5F, 1.25F, 1.45F); },
		    [&](float, float, float d) { return Scaled(skin, d > -0.45F ? 0.3F : 0.55F); });
		Fill(
		    canvas, 24, oy + 7, 27, oy + 10,
		    [&](float px, float py) { return ToEllipse(px, py, 25.3F, oy + 8.5F, 0.45F, 0.45F); },
		    [&](float, float, float) { return Rgba { 6, 5, 5, 1 }; });
		Fill(
		    canvas, 27, oy + 8, 31, oy + 12,
		    [&](float px, float py) { return ToEllipse(px, py, 29.F, oy + 10.1F, 0.5F, 0.45F); },
		    [&](float, float, float) { return Scaled(skin, 0.7F); });
		Line(canvas, 20.8F, oy + 10, 0.75F, 20.8F + front[frame], base + RatFloor, 0.5F, Scaled(fur, 0.7F));
		Line(canvas, 20.8F + front[frame], base + RatFloor, 0.5F, 21.9F + front[frame], base + RatFloor, 0.4F, Scaled(skin, 0.8F));
		Line(canvas, 13.6F, oy + 10, 1.F, 13.6F + hind[frame], base + RatFloor, 0.55F, Scaled(fur, 0.7F));
		Line(canvas, 13.6F + hind[frame], base + RatFloor, 0.55F, 15 + hind[frame], base + RatFloor, 0.4F, Scaled(skin, 0.8F));
	}
	return canvas;
}

/**
 * @brief Gives the clear pixels beside a picture's edge the colour of the edge, so the picture drawn smoothly doesn't
 * pick up a dark rim from them.
 */
void Bleed(Canvas &canvas)
{
	for (int pass = 0; pass < 2; pass++) {
		std::vector<uint8_t> from = canvas.rgba;
		// Pixels a pass has coloured count as edge for the next: marked by a little alpha in the copy only.
		for (int y = 0; y < canvas.height; y++) {
			for (int x = 0; x < canvas.width; x++) {
				uint8_t *at = &canvas.rgba[(static_cast<size_t>(y) * canvas.width + x) * 4];
				if (at[3] != 0 || (at[0] | at[1] | at[2]) != 0)
					continue;
				int sum[3] = { 0, 0, 0 };
				int count = 0;
				for (int dy = -1; dy <= 1; dy++) {
					for (int dx = -1; dx <= 1; dx++) {
						const int nx = x + dx;
						const int ny = y + dy;
						if (nx < 0 || ny < 0 || nx >= canvas.width || ny >= canvas.height)
							continue;
						const uint8_t *near = &from[(static_cast<size_t>(ny) * canvas.width + nx) * 4];
						if (near[3] == 0 && (near[0] | near[1] | near[2]) == 0)
							continue;
						for (int c = 0; c < 3; c++)
							sum[c] += near[c];
						count++;
					}
				}
				if (count == 0)
					continue;
				for (int c = 0; c < 3; c++)
					at[c] = static_cast<uint8_t>(std::max(sum[c] / count, 1));
			}
		}
	}
}

std::array<Canvas, NumKinds> Pictures;

// Where the things stand.

/** @brief A chain by a wall or a web in a corner, on the wall's own tile. */
struct Fixed {
	Kind kind;
	Point tile;
	/** @brief A chain's wall: the one on the tile's y-min edge (running down to the right on screen), else the x-min one. */
	bool rightWall;
	/** @brief How high it hangs from, and a chain's length, in game pixels. */
	float height;
	float length;
	/** @brief Its own beat, so no two move together. */
	float phase;
	/** @brief A chain's swing: its angle from straight down and how fast that turns. */
	float angle = 0;
	float turning = 0;
	float kickedAgo = 10000;
};

std::vector<Fixed> Things;
/** @brief Which thing a tile holds: its place in Things plus one, or 0. */
std::array<uint16_t, MAXDUNX * MAXDUNY> Held {};
/** @brief The level the things were placed for, and whether they are placed. */
uint32_t PlacedFor = 0;
bool Placed = false;
float Time = 0;
float SinceKicks = 0;

/** @brief A rat on the run from one tile's middle to the next. */
struct Rat {
	Point from;
	Point to;
	float along = 0;
	float age = 0;
	float life = 0;
	/** @brief Running out of floor: it fades where it is. */
	bool stopped = false;
};

std::vector<Rat> Rats;
float RatIn = 6000;
uint32_t Dice = 0x51ED270BU;

float Roll()
{
	Dice ^= Dice << 13;
	Dice ^= Dice >> 17;
	Dice ^= Dice << 5;
	return static_cast<float>(Dice & 0xFFFFFF) / static_cast<float>(0x1000000);
}

/** @brief What the things were placed for: the level, and how many of them the switches ask for. */
uint32_t LevelNow()
{
	const uint32_t level = DungeonSeeds[currlevel] ^ (static_cast<uint32_t>(currlevel) << 8) ^ (static_cast<uint32_t>(leveltype) << 16) ^ (setlevel ? static_cast<uint32_t>(setlvlnum) << 24 : 0U);
	const auto part = [](float value) { return static_cast<uint32_t>(std::clamp(value, 0.F, 3.F) * 100); };
	return level ^ (part(atmosphere::Strength(atmosphere::Knob::Chains)) * 0x9E3779B1U) ^ (part(atmosphere::Strength(atmosphere::Knob::Cobwebs)) * 0x85EBCA6BU);
}

bool Wall(Point tile)
{
	return InDungeonBounds(tile) && TileHasAny(tile, TileProperties::BlockLight);
}

/** @brief The walls on a wall tile's two back edges (where Diablo stands its walls), as hd.cpp reads them. */
void BackWalls(Point tile, bool &left, bool &right)
{
	left = false;
	right = false;
	if (!Wall(tile))
		return;
	const MICROS &micros = DPieceMicros[dPiece[tile.x][tile.y]];
	left = !micros.mt[0].hasValue() || micros.mt[0].type() != TileType::LeftTriangle;
	right = !micros.mt[1].hasValue() || micros.mt[1].type() != TileType::RightTriangle;
}

/** @brief A plain stretch of wall: nothing set in it (a door, a lever, an arch). */
bool Plain(Point tile)
{
	return dObject[tile.x][tile.y] == 0 && dSpecial[tile.x][tile.y] == 0;
}

bool Floor(Point tile)
{
	return InDungeonBounds(tile) && !Wall(tile) && IsTileNotSolid(tile);
}

void Place()
{
	Things.clear();
	Held.fill(0);
	Placed = true;
	PlacedFor = LevelNow();
	if (leveltype == DTYPE_TOWN || leveltype == DTYPE_NONE)
		return;
	const float chains = std::min(atmosphere::Strength(atmosphere::Knob::Chains), 3.F);
	const float webs = std::min(atmosphere::Strength(atmosphere::Knob::Cobwebs), 3.F);
	// One wall in this many holds a chain, one corner in this many a web; 0 none.
	float chainGap = atmosphere::Value("chain_gap", 0);
	float webGap = atmosphere::Value("web_gap", 0);
	const float butcherGap = atmosphere::RoomValue("butcher", "chain_gap", 2);
	const std::optional<WorldTileRectangle> butcher = atmosphere::ButcherRoom();
	const uint32_t seed = DungeonSeeds[currlevel] ^ (static_cast<uint32_t>(currlevel) << 8);
	for (int x = 2; x < MAXDUNX - 2 && Things.size() < 600; x++) {
		for (int y = 2; y < MAXDUNY - 2; y++) {
			const Point tile { x, y };
			bool left;
			bool right;
			BackWalls(tile, left, right);
			if ((!left && !right) || !Plain(tile))
				continue;
			const float pick = Hash(static_cast<uint32_t>(x), static_cast<uint32_t>(y), seed);
			const float other = Hash(static_cast<uint32_t>(y), static_cast<uint32_t>(x), seed ^ 0xA5A5U);
			if (left && right) {
				// A room's far corner: both walls meet here and the room's floor lies in front of them.
				if (webs <= 0 || webGap <= 0 || pick * webGap >= webs)
					continue;
				// The two walls run on from it, one a side, with the room's floor between them.
				bool l;
				bool r;
				BackWalls(tile + Displacement { 1, 0 }, l, r);
				const bool runsRight = r && !l && Plain(tile + Displacement { 1, 0 });
				BackWalls(tile + Displacement { 0, 1 }, l, r);
				const bool runsLeft = l && !r && Plain(tile + Displacement { 0, 1 });
				if (!runsRight || !runsLeft || !Floor(tile + Displacement { 1, 1 }) || !Floor(tile + Displacement { 2, 1 }) || !Floor(tile + Displacement { 1, 2 }))
					continue;
				Things.push_back({ Kind::Web, tile, false, 50 + 6 * other, 0, other * 40 });
				Held[static_cast<size_t>(x) * MAXDUNY + y] = static_cast<uint16_t>(Things.size());
				continue;
			}
			if (chains <= 0)
				continue;
			// A flat run of wall with a room's floor in front of it.
			const Displacement along = right ? Displacement { 1, 0 } : Displacement { 0, 1 };
			const Displacement front = right ? Displacement { 0, 1 } : Displacement { 1, 0 };
			const auto sameWall = [&](Point at) {
				bool l;
				bool r;
				BackWalls(at, l, r);
				return (right ? r && !l : l && !r) && Plain(at);
			};
			if (!sameWall(tile + along) || !sameWall(tile - along) || !Floor(tile + front) || !Floor(tile + front + along) || !Floor(tile + front - along))
				continue;
			const Point before = tile + front;
			const bool his = butcher && butcher->contains(WorldTilePosition(static_cast<WorldTileCoord>(before.x), static_cast<WorldTileCoord>(before.y)));
			const float gap = his ? butcherGap : chainGap;
			if (gap <= 0 || pick * gap >= chains)
				continue;
			// Never two side by side.
			if (Held[static_cast<size_t>(x - along.deltaX) * MAXDUNY + (y - along.deltaY)] != 0)
				continue;
			Things.push_back({ Kind::Chain, tile, right, 50 + 6 * other, 12 + 14 * Hash(static_cast<uint32_t>(x), static_cast<uint32_t>(y), seed ^ 0x77U), other * 40 });
			Held[static_cast<size_t>(x) * MAXDUNY + y] = static_cast<uint16_t>(Things.size());
		}
	}
}

/** @brief Where a point in tiles is on screen, in game pixels from the middle of a tile's floor. */
void FromTile(float x, float y, Point tile, float &px, float &py)
{
	const float dx = x - static_cast<float>(tile.x);
	const float dy = y - static_cast<float>(tile.y);
	px = (dx - dy) * 32;
	py = (dx + dy) * 16;
}

/** @brief Something moving close by sets a chain swinging, away from it. */
void KickChains()
{
	struct Mover {
		Point at;
	};
	std::array<Mover, 24> movers;
	size_t count = 0;
	if (MyPlayer != nullptr && MyPlayer->isWalking())
		movers[count++] = { MyPlayer->position.tile };
	Point hero { 0, 0 };
	if (MyPlayer != nullptr)
		hero = MyPlayer->position.tile;
	for (size_t i = 0; i < ActiveMonsterCount && count < movers.size(); i++) {
		const Monster &monster = Monsters[ActiveMonsters[i]];
		if (!monster.isWalking() || std::abs(monster.position.tile.x - hero.x) > 14 || std::abs(monster.position.tile.y - hero.y) > 14)
			continue;
		movers[count++] = { monster.position.tile };
	}
	if (count == 0)
		return;
	for (Fixed &thing : Things) {
		if (thing.kind != Kind::Chain || thing.kickedAgo < 700)
			continue;
		const Point front = thing.tile + (thing.rightWall ? Displacement { 0, 1 } : Displacement { 1, 0 });
		for (size_t m = 0; m < count; m++) {
			const Point at = movers[m].at;
			if (std::abs(at.x - front.x) > 1 || std::abs(at.y - front.y) > 1)
				continue;
			// Which side of it on screen the mover is: it swings the other way first.
			const int side = (at.x - at.y) - (thing.tile.x - thing.tile.y);
			thing.turning += (side <= 0 ? 1.F : -1.F) * (1.1F + 0.5F * Hash(static_cast<uint32_t>(at.x), static_cast<uint32_t>(at.y), static_cast<uint32_t>(Time)));
			thing.kickedAgo = 0;
			break;
		}
	}
}

void SwingChains(float ms)
{
	const float dt = std::min(ms, 50.F) / 1000;
	for (Fixed &thing : Things) {
		if (thing.kind != Kind::Chain)
			continue;
		thing.kickedAgo += ms;
		// A pendulum: the longer the chain the slower it swings; it rings down over a few swings.
		const float rate = 5.4F * std::sqrt(40 / (thing.length + HookLong));
		thing.turning += (-rate * rate * thing.angle - 0.9F * thing.turning) * dt;
		thing.turning = std::clamp(thing.turning, -3.F, 3.F);
		thing.angle = std::clamp(thing.angle + thing.turning * dt, -0.6F, 0.6F);
	}
}

bool RatCanStand(Point tile)
{
	return Floor(tile) && dObject[tile.x][tile.y] == 0;
}

/** @brief The next tile for a rat: on the way it was going if it can, else whichever way leads from the hero. */
bool NextTile(const Rat &rat, Point &next)
{
	const Point hero = MyPlayer->position.tile;
	const Displacement going = rat.to - rat.from;
	const Displacement away = rat.to - hero;
	std::array<Displacement, 3> tries {
		going,
		std::abs(away.deltaX) >= std::abs(away.deltaY) ? Displacement { 0, away.deltaY >= 0 ? 1 : -1 } : Displacement { away.deltaX >= 0 ? 1 : -1, 0 },
		std::abs(away.deltaX) >= std::abs(away.deltaY) ? Displacement { 0, away.deltaY >= 0 ? -1 : 1 } : Displacement { away.deltaX >= 0 ? -1 : 1, 0 },
	};
	for (const Displacement step : tries) {
		const Point tile = rat.to + step;
		if ((step.deltaX == 0 && step.deltaY == 0) || !RatCanStand(tile))
			continue;
		// Never back towards the hero.
		if (hero.WalkingDistance(tile) < hero.WalkingDistance(rat.to))
			continue;
		next = tile;
		return true;
	}
	return false;
}

float Brightness(Point tile)
{
	const hd::light::Rgb light = hd::light::At(static_cast<float>(tile.x), static_cast<float>(tile.y));
	return (light.r + light.g + light.b) / 3;
}

/** @brief A rat starts where the hero's light is just reaching, ahead of the hero, and runs from it. */
void StartRat()
{
	const Point hero = MyPlayer->position.tile;
	const Displacement ahead = Displacement(MyPlayer->_pdir);
	for (int attempt = 0; attempt < 6; attempt++) {
		const int far = 1 + static_cast<int>(Roll() * 3);
		const Point tile = hero + Displacement { ahead.deltaX * far + static_cast<int>(Roll() * 5) - 2, ahead.deltaY * far + static_cast<int>(Roll() * 5) - 2 };
		if (!RatCanStand(tile) || !IsTileLit(tile) || !IsTileVisible(tile) || dMonster[tile.x][tile.y] != 0 || dPlayer[tile.x][tile.y] != 0)
			continue;
		const float bright = Brightness(tile);
		// Lit enough to be seen as it starts, and not right at the hero's feet.
		if (bright < 0.3F || hero.WalkingDistance(tile) < 2)
			continue;
		const Displacement away = tile - hero;
		Rat rat;
		rat.from = tile;
		rat.to = tile;
		rat.life = 1500 + 900 * Roll();
		// Its first step: along whichever way leads most from the hero.
		const Displacement first = std::abs(away.deltaX) >= std::abs(away.deltaY) ? Displacement { away.deltaX >= 0 ? 1 : -1, 0 } : Displacement { 0, away.deltaY >= 0 ? 1 : -1 };
		if (RatCanStand(tile + first)) {
			rat.to = tile + first;
		} else {
			Point next;
			rat.from = tile - first;
			if (!NextTile(rat, next))
				continue;
			rat.from = tile;
			rat.to = next;
		}
		Rats.push_back(rat);
		return;
	}
}

void RunRats(float ms)
{
	const float strength = std::min(atmosphere::Strength(atmosphere::Knob::Rats), 3.F);
	float every = atmosphere::Value("rat_every", 0);
	if (strength <= 0 || every <= 0 || MyPlayer == nullptr) {
		Rats.clear();
		return;
	}
	const float speed = std::clamp(atmosphere::Value("rat_speed", 5.F), 2.F, 14.F);
	for (Rat &rat : Rats) {
		rat.age += ms;
		if (rat.stopped)
			continue;
		rat.along += speed * ms / 1000;
		while (rat.along >= 1 && !rat.stopped) {
			Point next;
			if (NextTile(rat, next)) {
				rat.along -= 1;
				rat.from = rat.to;
				rat.to = next;
			} else {
				rat.along = 1;
				rat.stopped = true;
				rat.life = std::min(rat.life, rat.age + 160);
			}
		}
	}
	Rats.erase(std::remove_if(Rats.begin(), Rats.end(), [](const Rat &rat) { return rat.age >= rat.life; }), Rats.end());
	// The next one waits while the hero stands, fights or lies dead: the light has to come to it.
	bool walking = MyPlayer->isWalking();
	if (!walking || moments::FightOn() || MyPlayerIsDead || Rats.size() >= 2)
		return;
	RatIn -= ms * strength;
	if (RatIn > 0)
		return;
	RatIn = every * 1000 * (0.6F + 0.8F * Roll());
	StartRat();
	// Now and then two.
	if (!Rats.empty() && Roll() < 0.3F)
		StartRat();
}

Shape ChainShape(const Fixed &thing)
{
	Shape shape {};
	shape.kind = Kind::Chain;
	shape.alpha = 1;
	// Its wall stands on the tile's back edge; it hangs against the middle of that wall's face, lit as the face is.
	const float side = thing.rightWall ? 1.F : -1.F;
	shape.lightX = static_cast<float>(thing.tile.x) - (thing.rightWall ? 0.F : 0.45F);
	shape.lightY = static_cast<float>(thing.tile.y) - (thing.rightWall ? 0.45F : 0.F);
	shape.onWall = true;
	const float total = std::min(thing.length + HookLong, static_cast<float>(ChainLong));
	// The draft keeps it just alive; what swings it is what goes past.
	const float angle = thing.angle + 0.012F * std::sin(Time * 0.0011F + thing.phase);
	constexpr size_t Pairs = 7;
	shape.pairs = Pairs;
	const float ax = 16 * side;
	float px = ax;
	float py = -8 - thing.height;
	const float step = total / (Pairs - 1);
	for (size_t k = 0; k < Pairs; k++) {
		// The foot of a chain swings wider than its head.
		const float a = angle * (0.5F + 0.6F * static_cast<float>(k) / (Pairs - 1));
		const float dx = std::sin(a);
		const float dy = std::cos(a);
		// It swings along its wall, which runs down the screen as it runs to the side.
		const float lean = (px - ax) * 0.5F * side;
		const float v = 1 - (total - step * static_cast<float>(k)) / ChainLong;
		shape.corners[k * 2] = { px - dy * (ChainWide / 2.F), py + lean + dx * (ChainWide / 2.F), 0, v };
		shape.corners[k * 2 + 1] = { px + dy * (ChainWide / 2.F), py + lean - dx * (ChainWide / 2.F), 1, v };
		px += dx * step;
		py += dy * step;
	}
	return shape;
}

Shape WebShape(const Fixed &thing)
{
	Shape shape {};
	shape.kind = Kind::Web;
	shape.alpha = std::clamp(atmosphere::Value("web_alpha", 0.6F), 0.F, 1.F);
	shape.lightX = static_cast<float>(thing.tile.x) - 0.3F;
	shape.lightY = static_cast<float>(thing.tile.y) - 0.3F;
	shape.onWall = true;
	shape.pairs = 3;
	// The corner's own line stands on the tile's far point; the web hangs from it and from the walls either side.
	const float top = -16 - thing.height;
	const float size = std::clamp(atmosphere::Value("web_size", 0.75F), 0.3F, 1.5F);
	const float shiver = std::clamp(atmosphere::Value("web_shiver", 0.6F), 0.F, 3.F);
	for (size_t row = 0; row < 3; row++) {
		const float t = static_cast<float>(row) / 2;
		// Held fast at the top; its free edge trembles.
		const float beat = Time * 0.0043F + thing.phase;
		const float dx = shiver * t * t * (0.6F * std::sin(beat) + 0.4F * std::sin(beat * 2.7F + 1));
		const float dy = shiver * t * t * 0.5F * std::sin(beat * 1.9F + 2);
		const float y = top + (WebTall * t - 1.5F) * size;
		shape.corners[row * 2] = { -WebWide / 2.F * size + dx, y + dy, 0, t };
		shape.corners[row * 2 + 1] = { WebWide / 2.F * size + dx, y + dy, 1, t };
	}
	return shape;
}

Shape RatShape(const Rat &rat, Point tile)
{
	Shape shape {};
	shape.kind = Kind::Rat;
	const float x = static_cast<float>(rat.from.x) + static_cast<float>(rat.to.x - rat.from.x) * rat.along;
	const float y = static_cast<float>(rat.from.y) + static_cast<float>(rat.to.y - rat.from.y) * rat.along;
	shape.alpha = std::clamp(std::min(rat.age / 120, (rat.life - rat.age) / 220), 0.F, 1.F);
	shape.lightX = x;
	shape.lightY = y;
	shape.onWall = false;
	shape.pairs = 2;
	float px;
	float py;
	FromTile(x, y, tile, px, py);
	// Which way it runs on screen: a step along x goes right and down, a step along y left and down.
	const Displacement going = rat.to - rat.from;
	const float sx = static_cast<float>(going.deltaX - going.deltaY);
	const float sy = static_cast<float>(going.deltaX + going.deltaY);
	const bool right = sx >= 0;
	// Its picture runs level; turned to the slope the floor's lines have on screen.
	const float slope = std::atan2(sy * 0.5F, std::abs(sx) > 0 ? std::abs(sx) : 1.F) * (right ? 1.F : -1.F);
	const float size = std::clamp(atmosphere::Value("rat_size", 0.8F), 0.3F, 1.5F);
	const float c = std::cos(slope);
	const float s = std::sin(slope);
	const int frame = static_cast<int>(rat.age / 55) % RatFrames;
	const float v0 = static_cast<float>(frame) / RatFrames;
	const float v1 = static_cast<float>(frame + 1) / RatFrames;
	const auto corner = [&](float cx, float cy, float u, float v) {
		// From its feet, in its own picture's pixels; nose to the left when it runs left.
		const float lx = (right ? cx - RatMiddle : RatMiddle - cx) * size;
		const float ly = (cy - RatFloor) * size;
		return Corner { px + lx * c - ly * s, py + lx * s + ly * c, u, v };
	};
	shape.corners[0] = corner(0, 0, 0, v0);
	shape.corners[1] = corner(RatWide, 0, 1, v0);
	shape.corners[2] = corner(0, RatTall, 0, v1);
	shape.corners[3] = corner(RatWide, RatTall, 1, v1);
	return shape;
}

} // namespace

Picture PictureOf(Kind kind)
{
	Canvas &canvas = Pictures[static_cast<size_t>(kind)];
	if (canvas.rgba.empty()) {
		switch (kind) {
		case Kind::Chain:
			canvas = PaintChain();
			break;
		case Kind::Web:
			canvas = PaintWeb();
			break;
		case Kind::Rat:
			canvas = PaintRat();
			break;
		}
		Bleed(canvas);
	}
	return { canvas.width, canvas.height, canvas.rgba.data() };
}

bool Any()
{
	return Placed && (!Things.empty() || !Rats.empty());
}

size_t At(Point tile, std::array<Shape, MaxAtTile> &shapes)
{
	size_t count = 0;
	if (!Placed || !InDungeonBounds(tile))
		return 0;
	if (const uint16_t held = Held[static_cast<size_t>(tile.x) * MAXDUNY + tile.y]; held != 0) {
		const Fixed &thing = Things[held - 1];
		shapes[count++] = thing.kind == Kind::Chain ? ChainShape(thing) : WebShape(thing);
	}
	for (const Rat &rat : Rats) {
		if (count >= MaxAtTile)
			break;
		// It belongs to whichever tile it is nearer the middle of.
		if ((rat.along < 0.5F ? rat.from : rat.to) == tile)
			shapes[count++] = RatShape(rat, tile);
	}
	return count;
}

void Advance(float ms)
{
	if (MyPlayer == nullptr || MyPlayer->pLvlLoad != 0)
		return;
	if (!Placed || PlacedFor != LevelNow()) {
		Rats.clear();
		Place();
	}
	if (PauseMode != 0)
		return;
	Time += ms;
	if (!Things.empty()) {
		SinceKicks += ms;
		if (SinceKicks >= 100) {
			SinceKicks = 0;
			KickChains();
		}
		SwingChains(ms);
	}
	RunRats(ms);
}

void Reset()
{
	Placed = false;
	Things.clear();
	Held.fill(0);
	Rats.clear();
	RatIn = 6000;
}

} // namespace devilution::props
