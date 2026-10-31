#include "poe2/floor_rooms.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <vector>

#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "objects.h"
#include "poe2/cathedral_floors.hpp"
#include "utils/is_of.hpp"

namespace devilution {

namespace {

/** A chamber's heart is floor at least this many steps from every wall: wider than the cave's passages. */
constexpr int CoreClearance = 3;
/** Wide-open patches smaller than this are a bulge in a passage, not a chamber. */
constexpr int MinCoreTiles = 8;

std::array<uint8_t, static_cast<size_t>(MAXDUNX) * MAXDUNY> Chambers {};

bool UsesChambers()
{
	return IsAnyOf(leveltype, DTYPE_CAVES, DTYPE_NEST);
}

/** Floor a monster can walk on; a closed door counts. */
bool Walkable(Point tile)
{
	if (IsTileNotSolid(tile))
		return true;
	const Object *object = FindObjectAtPosition(tile, false);
	return object != nullptr && object->isDoor();
}

} // namespace

void SplitIntoChambers(int width, int height, std::span<const uint8_t> walkable, std::span<const uint8_t> fixedRooms, std::span<uint8_t> rooms)
{
	const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
	const auto index = [height](Point tile) { return static_cast<size_t>(tile.x) * static_cast<size_t>(height) + static_cast<size_t>(tile.y); };
	const auto inside = [width, height](Point tile) { return tile.x >= 0 && tile.y >= 0 && tile.x < width && tile.y < height; };
	constexpr std::array<Displacement, 4> Sides { { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } } };
	constexpr std::array<Displacement, 8> Around { { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } } };

	int highest = 0;
	for (size_t i = 0; i < count; i++) {
		rooms[i] = walkable[i] != 0 ? fixedRooms[i] : 0;
		highest = std::max<int>(highest, fixedRooms[i]);
	}
	const auto open = [&](Point tile) { return inside(tile) && walkable[index(tile)] != 0 && fixedRooms[index(tile)] == 0; };

	// Steps from the nearest wall.
	std::vector<int> clearance(count, INT_MAX);
	std::vector<Point> queue;
	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			if (walkable[index({ x, y })] == 0) {
				clearance[index({ x, y })] = 0;
				queue.push_back({ x, y });
			}
		}
	}
	for (size_t head = 0; head < queue.size(); head++) {
		const Point tile = queue[head];
		for (const Displacement step : Around) {
			const Point next = tile + step;
			if (!inside(next) || clearance[index(next)] <= clearance[index(tile)] + 1)
				continue;
			clearance[index(next)] = clearance[index(tile)] + 1;
			queue.push_back(next);
		}
	}

	// Each wide-open patch big enough is a chamber's heart.
	int next = highest + 1;
	std::vector<uint8_t> seen(count, 0);
	std::vector<Point> grow;
	const auto isCore = [&](Point tile) { return open(tile) && clearance[index(tile)] >= CoreClearance; };
	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			if (!isCore({ x, y }) || seen[index({ x, y })] != 0)
				continue;
			std::vector<Point> patch { { x, y } };
			seen[index({ x, y })] = 1;
			for (size_t head = 0; head < patch.size(); head++) {
				for (const Displacement step : Around) {
					const Point tile = patch[head] + step;
					if (isCore(tile) && seen[index(tile)] == 0) {
						seen[index(tile)] = 1;
						patch.push_back(tile);
					}
				}
			}
			// Past the last room number a heart joins the chambers round it.
			if (static_cast<int>(patch.size()) < MinCoreTiles || next >= MaxFloorRooms)
				continue;
			for (const Point tile : patch) {
				rooms[index(tile)] = static_cast<uint8_t>(next);
				grow.push_back(tile);
			}
			next++;
		}
	}

	// The floor between hearts goes to the nearest on foot, so chambers meet half way along a passage.
	for (size_t head = 0; head < grow.size(); head++) {
		const Point tile = grow[head];
		for (const Displacement step : Sides) {
			const Point to = tile + step;
			if (!open(to) || rooms[index(to)] != 0)
				continue;
			rooms[index(to)] = rooms[index(tile)];
			grow.push_back(to);
		}
	}

	// Floor no heart reaches (a cave of narrow passages only, or a pocket off the rest) is a chamber of its own.
	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			if (!open({ x, y }) || rooms[index({ x, y })] != 0)
				continue;
			const auto id = static_cast<uint8_t>(std::min(next, MaxFloorRooms - 1));
			std::vector<Point> pocket { { x, y } };
			rooms[index({ x, y })] = id;
			for (size_t head = 0; head < pocket.size(); head++) {
				for (const Displacement step : Sides) {
					const Point tile = pocket[head] + step;
					if (open(tile) && rooms[index(tile)] == 0) {
						rooms[index(tile)] = id;
						pocket.push_back(tile);
					}
				}
			}
			next++;
		}
	}
}

void BuildFloorRooms()
{
	if (!UsesChambers())
		return;
	std::vector<uint8_t> walkable(Chambers.size());
	std::vector<uint8_t> fixedRooms(Chambers.size());
	for (int x = 0; x < MAXDUNX; x++) {
		for (int y = 0; y < MAXDUNY; y++) {
			const size_t i = static_cast<size_t>(x) * MAXDUNY + static_cast<size_t>(y);
			walkable[i] = Walkable({ x, y }) ? 1 : 0;
			fixedRooms[i] = static_cast<uint8_t>(dTransVal[x][y]);
		}
	}
	SplitIntoChambers(MAXDUNX, MAXDUNY, walkable, fixedRooms, Chambers);
}

int FloorRoomAt(Point tile)
{
	if (IsCathedralFunnelFloor())
		return CathedralRoomAt(tile);
	if (!UsesChambers())
		return static_cast<uint8_t>(dTransVal[tile.x][tile.y]);
	return Chambers[static_cast<size_t>(tile.x) * MAXDUNY + static_cast<size_t>(tile.y)];
}

} // namespace devilution
