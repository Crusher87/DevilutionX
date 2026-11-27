#include "poe2/props.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "diablo.h"
#include "levels/dun_tile.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
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
	// Grey with the dust of the stone it hangs on, not white: it is part of the room, seen only where the light catches it.
	const Rgba thread { 150, 144, 132, 0.5F };
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
	// A little paler than a real rat, as it is seen in dim light on a dark floor; but dark enough to belong to the
	// place (the first ones were paler and bigger, and stood out from it: Curtis 2026-11-14).
	const Rgba fur { 100, 86, 74, 1 };
	const Rgba belly { 144, 128, 108, 1 };
	const Rgba skin { 168, 126, 114, 1 };
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

/** @brief A web in a corner, on the corner's own tile. */
struct Fixed {
	Point tile;
	/** @brief How high it hangs from, in game pixels. */
	float height;
	/** @brief Its own beat, so no two move together. */
	float phase;
};

std::vector<Fixed> Things;
/** @brief Which thing a tile holds: its place in Things plus one, or 0. */
std::array<uint16_t, MAXDUNX * MAXDUNY> Held {};
/** @brief The level the things were placed for, and whether they are placed. */
uint32_t PlacedFor = 0;
bool Placed = false;
float Time = 0;

/**
 * @brief A rat on the run from one tile's middle to the next: out of the dark as far as the hero's light, then back along
 * a way worked out for it into the dark. It comes and goes only where it can't be seen: one that vanished in plain sight
 * read as a fault (Curtis 2026-11-15).
 */
struct Rat {
	Point from;
	Point to;
	float along = 0;
	float age = 0;
	/** @brief The tiles still ahead of it after `to`, the last of them out of sight; and how many it has taken. */
	std::vector<Point> way;
	size_t taken = 0;
	/** @brief How long it has stood with no way into the dark, in milliseconds. */
	float stuck = 0;
	/** @brief Its age when it began to fade (the last resort, when no way opens); negative while it doesn't. */
	float fadeFrom = -1;
	/** @brief Whether it has yet been where it can be seen: it starts in the dark. */
	bool seen = false;
	bool gone = false;
};

std::vector<Rat> Rats;
/** @brief How long a rat that found no way out of sight takes to fade, in milliseconds. */
constexpr float RatFade = 600;
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
	return level ^ (part(atmosphere::Strength(atmosphere::Knob::Cobwebs)) * 0x85EBCA6BU);
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
	const float webs = std::min(atmosphere::Strength(atmosphere::Knob::Cobwebs), 3.F);
	// One corner in this many holds a web; 0 none.
	float webGap = atmosphere::Value("web_gap", 0);
	const uint32_t seed = DungeonSeeds[currlevel] ^ (static_cast<uint32_t>(currlevel) << 8);
	for (int x = 2; x < MAXDUNX - 2 && Things.size() < 600; x++) {
		for (int y = 2; y < MAXDUNY - 2; y++) {
			const Point tile { x, y };
			bool left;
			bool right;
			BackWalls(tile, left, right);
			// A room's far corner: both walls meet here and the room's floor lies in front of them.
			if (!left || !right || !Plain(tile))
				continue;
			const float pick = Hash(static_cast<uint32_t>(x), static_cast<uint32_t>(y), seed);
			const float other = Hash(static_cast<uint32_t>(y), static_cast<uint32_t>(x), seed ^ 0xA5A5U);
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
			Things.push_back({ tile, 50 + 6 * other, other * 40 });
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

bool RatCanStand(Point tile)
{
	return Floor(tile) && dObject[tile.x][tile.y] == 0;
}

float Brightness(Point tile)
{
	const hd::light::Rgb light = hd::light::At(static_cast<float>(tile.x), static_cast<float>(tile.y));
	return (light.r + light.g + light.b) / 3;
}

/** @brief Whether a rat on this tile is out of sight: in the dark, or too far from the hero to be in view. */
bool OutOfSight(Point tile)
{
	const Point hero = MyPlayer->position.tile;
	return Brightness(tile) < 0.03F || hero.WalkingDistance(tile) > 15;
}

/** @brief Whether a rat may run over this tile: floor with nothing on it, and not right by the hero. */
bool RatCanPass(Point tile)
{
	const Point hero = MyPlayer->position.tile;
	return RatCanStand(tile) && dMonster[tile.x][tile.y] == 0 && dPlayer[tile.x][tile.y] == 0 && hero.WalkingDistance(tile) >= 2;
}

/**
 * @brief The shortest way for a rat from a tile to one that is out of sight, a step at a time along the floor and never
 * past the hero: the tiles after the first, in order. False when there is none within reach.
 */
bool WayIntoTheDark(Point start, std::vector<Point> &way)
{
	constexpr int Reach = 26;
	constexpr int Wide = 2 * Reach + 1;
	// Each tile round the start: 0 not reached yet, else 1 + the step that reached it (0 to 3).
	static std::array<uint8_t, Wide * Wide> reached;
	static std::vector<Point> open;
	reached.fill(0);
	open.clear();
	const auto slot = [&](Point tile) { return static_cast<size_t>(tile.y - start.y + Reach) * Wide + static_cast<size_t>(tile.x - start.x + Reach); };
	// The steps tried first lead away from the hero, so of two ways as short it takes the one that flees.
	const Point hero = MyPlayer->position.tile;
	const Displacement away = start - hero;
	const Displacement alongX { away.deltaX >= 0 ? 1 : -1, 0 };
	const Displacement alongY { 0, away.deltaY >= 0 ? 1 : -1 };
	const bool xFirst = std::abs(away.deltaX) >= std::abs(away.deltaY);
	const std::array<Displacement, 4> steps { xFirst ? alongX : alongY, xFirst ? alongY : alongX, xFirst ? -alongY : -alongX, xFirst ? -alongX : -alongY };
	reached[slot(start)] = 5;
	open.push_back(start);
	for (size_t i = 0; i < open.size(); i++) {
		const Point tile = open[i];
		if (tile != start && OutOfSight(tile)) {
			way.clear();
			for (Point back = tile; back != start; back -= steps[reached[slot(back)] - 1U])
				way.push_back(back);
			std::reverse(way.begin(), way.end());
			return true;
		}
		for (size_t k = 0; k < steps.size(); k++) {
			const Point next = tile + steps[k];
			if (std::abs(next.x - start.x) > Reach || std::abs(next.y - start.y) > Reach || reached[slot(next)] != 0 || !RatCanPass(next))
				continue;
			reached[slot(next)] = static_cast<uint8_t>(k + 1);
			open.push_back(next);
		}
	}
	return false;
}

/** @brief A rat comes out of the dark to where the hero's light is just reaching, ahead of the hero, and runs back from it. */
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
		// Only one that has somewhere to go: a way along the floor to where it can't be seen.
		Rat rat;
		if (!WayIntoTheDark(tile, rat.way))
			continue;
		// It comes out of the dark the same way, as far as this tile, and turns back when it gets here.
		std::reverse(rat.way.begin(), rat.way.end());
		rat.way.push_back(tile);
		rat.from = rat.way[0];
		rat.to = rat.way[1];
		rat.taken = 2;
		// A second one down the same way hangs back in the dark a moment, so the two don't run as one.
		for (const Rat &other : Rats) {
			if (other.from == rat.from)
				rat.along = -1 - 2 * Roll();
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
	// A rat with no way into the dark waits this long for one to open before it fades where it is, over this long; one
	// that has run this long fades too (all in milliseconds). Neither should happen: it starts only with a way to go.
	constexpr float StuckFor = 2500;
	constexpr float RunsFor = 15000;
	for (Rat &rat : Rats) {
		rat.age += ms;
		if (rat.fadeFrom >= 0) {
			rat.gone = rat.age - rat.fadeFrom >= RatFade;
			continue;
		}
		rat.along += speed * ms / 1000;
		while (rat.along >= 1) {
			// At the middle of a tile: once it has been out in the light, it is gone where nobody can see it.
			if (!OutOfSight(rat.to)) {
				rat.seen = true;
			} else if (rat.seen) {
				rat.gone = true;
				break;
			}
			// On along its way, or by a new one if the hero's light or something in the way has closed it.
			const bool onward = rat.taken < rat.way.size() && RatCanPass(rat.way[rat.taken]);
			if (!onward) {
				// One that never reached the light has nothing to run from.
				if (!rat.seen) {
					rat.gone = true;
					break;
				}
				rat.taken = 0;
				if (!WayIntoTheDark(rat.to, rat.way)) {
					rat.way.clear();
					rat.along = 1;
					rat.stuck += ms;
					break;
				}
			}
			rat.stuck = 0;
			rat.along -= 1;
			rat.from = rat.to;
			rat.to = rat.way[rat.taken++];
		}
		if (rat.stuck >= StuckFor || rat.age >= RunsFor) {
			rat.fadeFrom = rat.age;
		}
	}
	Rats.erase(std::remove_if(Rats.begin(), Rats.end(), [](const Rat &rat) { return rat.gone; }), Rats.end());
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

Shape WebShape(const Fixed &thing)
{
	Shape shape {};
	shape.kind = Kind::Web;
	shape.alpha = std::clamp(atmosphere::Value("web_alpha", 0.4F), 0.F, 1.F);
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
	const float along = std::max(rat.along, 0.F);
	const float x = static_cast<float>(rat.from.x) + static_cast<float>(rat.to.x - rat.from.x) * along;
	const float y = static_cast<float>(rat.from.y) + static_cast<float>(rat.to.y - rat.from.y) * along;
	shape.alpha = std::clamp(std::min(rat.age / 120, rat.fadeFrom < 0 ? 1.F : 1 - (rat.age - rat.fadeFrom) / RatFade), 0.F, 1.F);
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
	const float size = std::clamp(atmosphere::Value("rat_size", 0.4F), 0.2F, 1.5F);
	const float c = std::cos(slope);
	const float s = std::sin(slope);
	// It holds still while it waits for a way to open.
	const int frame = rat.stuck > 0 ? 1 : static_cast<int>(rat.age / 55) % RatFrames;
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
	// For its run's pictures (hd.cpp draws those when they are there): the eighth of the screen its way points in,
	// counted round from straight down, and the frame of the loop it has run to (60 a second as the run was made). It
	// stands on its first frame while it waits.
	const float angle = std::atan2(sy * 16, sx * 32) * 180 / Pi;
	shape.direction = (static_cast<int>(std::lround((angle - 90) / 45)) % 8 + 8) % 8;
	const float fps = std::clamp(atmosphere::Value("rat_run_fps", 60.F), 10.F, 120.F);
	shape.frame = rat.stuck > 0 ? 0 : static_cast<int>(rat.age * fps / 1000) % RatRunFrames;
	shape.footX = px;
	shape.footY = py;
	return shape;
}

} // namespace

Picture PictureOf(Kind kind)
{
	Canvas &canvas = Pictures[static_cast<size_t>(kind)];
	if (canvas.rgba.empty()) {
		switch (kind) {
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
		shapes[count++] = WebShape(thing);
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
