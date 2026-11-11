/**
 * @file poe2/level_census.hpp
 *
 * POE2 Mashup play log: what each dungeon floor holds, room by room, and how the time on it was spent.
 *
 * Curtis found rooms with a few barrels and one monster (Warrior run 1, picture 9). Before monsters are placed
 * any differently, this counts what the floors hold today, so a change can be checked against numbers:
 *  - On every floor entered, a census: each room (the game's own room numbers, dTransVal), its doors, how far
 *    it is from the up stairs, whether the way from stairs to stairs goes through it, and what's in it
 *    (monsters by group and rarity, chests, barrels, shrines, items, theme). Plus a small map of the floor.
 *  - While on the floor: seconds and kills per room, time to the first fight, quiet stretches between fights
 *    (counting only time spent moving or fighting), and time walking back through rooms already emptied.
 *  - The floor checker (censusfloors(n) in the console) walks the hero between two floors n times, rolling
 *    each one fresh, so many floors can be counted in a minute.
 *
 * Read-only: nothing here changes the game, the save or the random numbers, apart from the floor checker's
 * fresh floors (the same as Ctrl+clicking the stairs).
 */
#pragma once

#include <string>
#include <string_view>

namespace devilution {

struct Monster;
struct Player;

/** @brief The floor being loaded was just built (rather than loaded as it was left). */
void LevelCensusNoteBuilt();
/** @brief A level was entered: counts the floor (dungeon floors only) and starts tracking the visit. */
void LevelCensusEntered();
/** @brief The floor is being left: writes how the time on it was spent. */
void LevelCensusLeft(std::string_view how);
/** @brief Once per game tick on a dungeon floor. */
void LevelCensusTick(const Player &player, bool inFight);
/** @brief A hostile monster was killed. */
void LevelCensusKill(const Monster &monster);

/** @brief The console's rooms() command: this floor's census and the visit so far. */
std::string LevelCensusText();
/** @brief The console's censusfloors(n) command: starts the floor checker. */
std::string LevelCensusStartBatch(int floors);

} // namespace devilution
