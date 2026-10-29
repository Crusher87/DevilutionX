/**
 * @file floors.hpp
 *
 * POE2 Mashup: floors that are rebuilt fresh on each visit.
 *
 * Diablo saves every floor exactly as you left it. Path of Exile 2 instead keeps an empty area alive for a
 * while and builds a new one once that time has passed. This module decides, for each floor, whether the
 * saved copy is still "kept" (and so loaded as it was) or whether the next visit rolls a new seed and builds
 * the floor again.
 *
 * The rules (agreed with Curtis on 2026-10-21):
 *  - Come back within 15 minutes of game time and it's the same floor.
 *  - Stay away longer and it's a new floor: new layout, new monsters, new chests, blank map.
 *  - An open town portal keeps its floor alive. The timer starts once the portal closes.
 *  - Loading a saved game keeps only the floor you stand on and the floor your portal leads to.
 *  - Hold Ctrl while clicking stairs to force a fresh floor on the other side.
 *  - Quest progress never resets. Finished quest bosses come back as plain fights.
 *  - Diablo's and Na-Krul's floors stay as they were once their quest is done, so the ending can't replay.
 *
 * Multiplayer: the timer is off (every player would have a different clock); only the Ctrl+click reroll
 * is used, and it is sent to everyone as a command so all players build the same floor.
 */
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "levels/gendung_defs.hpp"

namespace devilution {

/** @brief How long an empty floor is kept, in game ticks (20 per second): 15 minutes, POE2's timer. */
constexpr uint32_t FloorKeepTicks = 15 * 60 * 20;

/** @brief Game time in ticks since the hero was made. Only runs while playing, so a night away doesn't count. */
extern uint32_t Poe2GameTicks;
/** @brief True while the floor being loaded was just rolled fresh (set by Poe2BeginFloorVisit). */
extern bool Poe2FloorFresh;
/** @brief Ctrl was held when the stairs were clicked: the floor on the other side is rolled fresh. */
extern bool Poe2ForceFreshFloor;

/** @brief Forgets every floor record (new hero / new game). */
void Poe2FloorsReset();
/** @brief Advances the game clock by one tick. */
void Poe2FloorsTick();

/** @brief Whether the saved copy of this floor should be loaded instead of building it again. */
bool Poe2FloorIsKept(int level, bool isSetLevel);
/** @brief Notes that the floor was just written to the save, so it counts as kept from now. */
void Poe2FloorSaved(int level, bool isSetLevel);
/** @brief The next visit to this floor builds it again. */
void Poe2ForgetFloor(int level, bool isSetLevel);
/** @brief Rolls a new seed for the floor, counts the visit and returns the seed. */
uint32_t Poe2RollFreshFloor(int level, bool isSetLevel);
/** @brief Applies a floor roll that another player sent. */
void Poe2ApplyFreshFloor(int level, bool isSetLevel, uint32_t seed, uint16_t visits);
/** @brief How many times this floor has been built for this hero (1 on the first visit). */
uint16_t Poe2FloorVisits(int level, bool isSetLevel);

/**
 * @brief Called at the start of loading a level. Decides whether the floor is kept and rolls it fresh if not.
 * Sets Poe2FloorFresh.
 */
void Poe2BeginFloorVisit(bool firstflag, lvl_entry lvldir);
/** @brief After a saved game is loaded: only the floor you stand on and your portal's floor stay kept. */
void Poe2FloorsAfterLoadGame();
/** @brief The hero's town portal closed; the timer on its floor starts now. */
void Poe2PortalClosed(int level, bool isSetLevel);

/** @brief How a floor's current build is made. */
struct Poe2FloorBuild {
	/** The new Cathedral builder (poe2/cathedral_floors.hpp) rather than Diablo's. */
	bool cathedralFunnel = false;
	/** Designed rooms this build leaves out (bit per room). */
	uint64_t avoidRooms = 0;
};

/**
 * @brief The builder and rooms for a floor's current build. The first call for a build settles them from the
 * New Cathedral Floors setting and the room history; later calls (the same floor built again from its seed) get the
 * same answer, so a kept floor comes back as it was.
 */
Poe2FloorBuild Poe2FloorBuildFor(int level);
/** @brief The designed rooms a new Cathedral floor used, for the room history (once per build). */
void Poe2NoteFloorRooms(int level, uint64_t rooms);

/** @brief Packs the floor records for the save file. */
std::vector<uint8_t> Poe2PackFloors();
/** @brief Unpacks the floor records from the save file. Returns false if the data is unreadable. */
bool Poe2UnpackFloors(std::span<const uint8_t> data);

} // namespace devilution
