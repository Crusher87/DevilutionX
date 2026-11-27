/**
 * @file poe2/props.hpp
 *
 * POE2 Mashup: the Atmosphere option's small things that stand in the HD world (step 6 of the old Diablo feel): cobwebs
 * in the wall corners (idea 64) and rats that run from the hero's light (62). This file knows where they are, how they move and what they look like; hd.cpp draws them in their place among
 * the walls and sprites, in the light that falls there.
 *
 * Looks only: nothing here touches the game state, saves or the game's seeded random numbers, and nothing can be hit,
 * picked up or stood on.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "engine/point.hpp"

namespace devilution::props {

enum class Kind : uint8_t {
	Web,
	Rat,
};
inline constexpr size_t NumKinds = 2;

/** @brief A kind's picture: RGBA bytes, row by row. Made the first time it is asked for and kept. */
struct Picture {
	int width = 0;
	int height = 0;
	const uint8_t *rgba = nullptr;
};
Picture PictureOf(Kind kind);

/** @brief A corner of a shape: where it is, in game pixels from the middle of its tile's floor, and its place in the picture. */
struct Corner {
	float x;
	float y;
	float u;
	float v;
};

/** @brief The most pairs of corners a shape has. */
inline constexpr size_t MaxPairs = 8;

/**
 * @brief One thing to draw: a strip of its picture running down through pairs of corners (left, right), as it stands
 * this frame.
 */
struct Shape {
	Kind kind;
	/** @brief How much of it shows, 0 to 1. */
	float alpha;
	/** @brief The world point (in tiles) whose light it is seen in, and whether that is the light on a wall's face. */
	float lightX;
	float lightY;
	bool onWall;
	size_t pairs;
	std::array<Corner, MaxPairs * 2> corners;
	/**
	 * @brief For a rat, what its run's pictures need (RatRunFrames frames a way, beside the picture painted here):
	 * which way it runs on screen (0 down, then round as the screen is seen: 1 down-left, 2 left, 3 up-left, 4 up,
	 * 5 up-right, 6 right, 7 down-right), the frame of its run, and where its feet are, in game pixels from the middle
	 * of its tile's floor. -1 for anything else.
	 */
	int direction = -1;
	int frame = 0;
	float footX = 0;
	float footY = 0;
};

/** @brief The frames of a rat's run, a loop, in its run's pictures. */
inline constexpr int RatRunFrames = 14;

/** @brief The most things one tile holds. */
inline constexpr size_t MaxAtTile = 3;

/** @brief Whether anything stands anywhere on this level now (so the tiles need not be asked one by one). */
bool Any();
/** @brief The things on a tile this frame; returns how many. */
size_t At(Point tile, std::array<Shape, MaxAtTile> &shapes);

/** @brief Once a drawn frame: the webs shiver, the rats run. */
void Advance(float ms);
/** @brief Forgets the level (a new game, a level change): the next frame looks at the level afresh. */
void Reset();

} // namespace devilution::props
