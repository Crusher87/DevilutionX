#pragma once

namespace devilution {

/**
 * @file poe2/autosave.hpp
 *
 * POE2 Mashup autosave: there is no Save Game in the menu. Like POE2, the game saves on its own: each time a level
 * finishes loading (stairs, town portal, waypoint), on level up, now and then when no monster is close, and when
 * leaving to the main menu or quitting. A hero who is dead or holds an item on the cursor waits for the next chance.
 */

/** @brief Whether the game saves on its own (POE2 Mashup single player). */
bool AutosaveEnabled();

/** @brief Asks for a save at the next safe moment. */
void RequestAutosave();

/** @brief Runs once per game tick: saves when one was asked for, or when it's been a while and it's quiet. */
void ProcessAutosave();

/** @brief Saves right away if it's safe, for leaving to the main menu or quitting. */
void AutosaveNow();

} // namespace devilution
