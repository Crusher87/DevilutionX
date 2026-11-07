/**
 * @file panels/monster_spawner.hpp
 *
 * POE2 Mashup monster spawner: the item spawner's Monsters tab. Pick a monster, watch it in a preview (in HD when the
 * HD pack has art for it), and spawn it next to the hero.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#else
#include <SDL.h>
#endif

#include "engine/direction.hpp"
#include "engine/rectangle.hpp"
#include "engine/surface.hpp"
#include "monster.h"
#include "tables/monstdat.h"

namespace devilution {

enum class MonsterSpawnerFilter : uint8_t {
	All,
	Normal,
	Unique,
	Count,
};

struct MonsterSpawnerEntry {
	std::string name;
	/** @brief The monster's picture folder, e.g. "zombie": tells apart monsters of the same name. */
	std::string detail;
	_monster_id type;
	/** @brief Index into UniqueMonstersData, or -1. */
	int unique = -1;
	/** @brief The HD pack has art for its standing frames. */
	bool hdArt = false;
};

std::string_view MonsterSpawnerFilterName(MonsterSpawnerFilter filter);

/**
 * @brief Every monster the spawner offers: the normal ones in the data's order, then the uniques.
 * @param hasSprites Whether the game's own pictures for a monster are there to load.
 */
std::vector<MonsterSpawnerEntry> BuildMonsterSpawnerList(const std::function<bool(const MonsterData &)> &hasSprites);

/** @brief Entries of the filter that contain every word of the query (case-insensitive). */
std::vector<size_t> FilterMonsterSpawnerList(const std::vector<MonsterSpawnerEntry> &list, std::string_view query, MonsterSpawnerFilter filter);

/** @brief Whether every animation file of the monster is in the game's data. */
bool MonsterSpritesExist(const MonsterData &data);

/** @brief Whether a monster CL2 file's frame tables stay inside the file (some files in the game's data are broken stubs). */
bool MonsterCl2LooksValid(const uint8_t *data, size_t size);

/** @brief Whether every animation file of the monster is there and readable without crashing; checked once per monster. */
bool MonsterSpritesValid(const MonsterData &data);

/**
 * @brief The name the HD pack gives a frame of a monster's animation, e.g. monsters/zombie/zombien/d0_f00
 * (as hd::RegisterSprites names the frames loaded by InitMonsterGFX).
 */
std::string MonsterArtName(const MonsterData &data, MonsterGraphic graphic, Direction direction, int frame);

/** @brief The facing @p steps eighths of a turn clockwise on screen from @p direction (negative: anticlockwise). */
Direction TurnDirection(Direction direction, int steps);

struct MonsterPreviewFrame {
	int frame;
	/** @brief How far towards the next frame (0 to 1), for the HD in-between drawings. */
	float fraction;
};

/** @brief How long a death holds its last frame before it plays again. */
constexpr uint32_t MonsterPreviewDeathHoldMs = 1500;

/**
 * @brief The frame an animation shows @p elapsedMs after it started, at the game's 20 ticks a second.
 * @param holdLast Play once, hold the last frame a while (MonsterPreviewDeathHoldMs), then again: for deaths.
 */
MonsterPreviewFrame MonsterPreviewFrameAt(int frames, int ticksPerFrame, uint32_t elapsedMs, bool holdLast);

void OpenMonsterSpawner();
void CloseMonsterSpawner();
size_t MonsterSpawnerResultCount();

/**
 * @brief Handles an event for the Monsters tab, drawn in @p area.
 * @param requestClose Set when the spawner should close (a monster was spawned).
 * @return true if the event was handled.
 */
bool MonsterSpawnerHandleEvent(const SDL_Event &event, const Rectangle &area, bool &requestClose);
void DrawMonsterSpawner(const Surface &out, const Rectangle &area);

} // namespace devilution
