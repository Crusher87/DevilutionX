/**
 * @file poe2/floor_rooms.hpp
 *
 * POE2 Mashup: a floor's rooms, for the pack planner (poe2/floor_packs.hpp) and the floor census.
 *
 * Diablo's Cathedral, Catacombs and Hell floors already have rooms: the regions it uses to see through walls
 * (dTransVal). The Caves and Hellfire's Nest have none outside their theme rooms, so there the open floor is split into
 * chambers where the passages narrow: each wide-open part of the cave is a chamber, and the floor between two of them
 * is shared out by which one is nearer on foot.
 */
#pragma once

#include <cstdint>
#include <span>

#include "engine/point.hpp"

namespace devilution {

/** @brief Room numbers fit in a byte. */
constexpr int MaxFloorRooms = 256;

/** @brief Works out this floor's rooms; call once the floor is built, before FloorRoomAt. Building them again gives the same rooms. */
void BuildFloorRooms();

/** @brief The room a tile is in (0 for walls and floor outside every room). */
int FloorRoomAt(Point tile);

/**
 * @brief Splits a cave into chambers (by tile, x * height + y).
 * Tiles with a fixed room (a theme room) keep it; every other walkable tile gets a chamber numbered after the highest
 * fixed room; walls get 0.
 */
void SplitIntoChambers(int width, int height, std::span<const uint8_t> walkable, std::span<const uint8_t> fixedRooms, std::span<uint8_t> rooms);

} // namespace devilution
