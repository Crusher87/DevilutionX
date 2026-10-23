/**
 * @file poe2/floor_packs.hpp
 *
 * POE2 Mashup: a floor's monsters placed room by room, in packs, instead of one small group at a time on random tiles.
 *
 * Step 2 of the dungeon plan (research/dungeon-generation/06-plan-v2.md). Curtis found rooms with a few barrels and one
 * monster, and on his Monk run no monster was near him over half the time he spent in the dungeon. The floor keeps
 * the same number of monsters; only where they stand changes:
 *  - One or two big fights (12 to 14 monsters led by a rare) in an open room with two or more ways in, about two
 *    thirds and a third of the way from the stairs up to the stairs down.
 *  - Every dead end holds something: a chest, shrine or sarcophagus it already had, a new chest at its far end, or a
 *    pack there led by a rare.
 *  - The rest go out as packs of 4 to 6, each where the floor is furthest from every monster already placed, rooms
 *    with no monsters yet first, so the floor fills evenly and the walk keeps meeting fights.
 *  - Packs mix the floor's monster types and hold at most 3 archers or casters.
 *  - Rares only lead the big fights and guard dead ends, about as many as the old random roll gave a floor, so the
 *    loot stays where it was (Curtis, 10-23: level 1 drops felt like a bit much). Magic monsters still roll at random.
 *  - A theme room's monsters (the barrel room's, the library's) stand together as one pack.
 * Cathedral floors only for now. The placement has its own random stream, seeded from the floor's, so the same seed
 * still gives the same floor.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "tables/monstdat.h"

namespace devilution {

/** @brief Monsters that mostly fight from a distance: archers, casters and spitters. */
bool IsRangedMonsterAi(MonsterAIID ai);

/** @brief Whether this floor's monsters are placed in packs: POE2, a Cathedral floor, not a quest floor. */
bool UseFloorPacks();

/**
 * @brief Places up to count monsters of the scatter types room by room (call while the stairs' view is marked, as
 * InitMonsters does). Fewer only when the floor has no room left.
 * @return How many were placed.
 */
size_t PlaceFloorPacks(size_t count, std::span<const size_t> scatterTypes);

/** @brief A theme room's monsters as one pack around the room's middle, about one per frequency free tiles. */
void PlaceThemePack(int8_t room, size_t typeIndex, int frequency);

} // namespace devilution
