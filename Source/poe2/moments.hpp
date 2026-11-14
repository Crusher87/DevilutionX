/**
 * @file poe2/moments.hpp
 *
 * POE2 Mashup: the Atmosphere option's moments (step 5 of the old Diablo feel). Short changes to the HD world's light and
 * air at a point in play: a door opening on a dark room (idea 47), taking the stairs down (49), the quiet after a fight
 * (50) and the hero's death (53). This file keeps what each moment is doing now; hd.cpp, hd_light.cpp and vfx.cpp ask it
 * and draw.
 *
 * Looks only: nothing here touches the game state, saves or the game's seeded random numbers, and nothing waits: the
 * game plays on under every one of them.
 */
#pragma once

#include <array>

namespace devilution::moments {

/** @brief The level is about to fade out for a level change; down when the hero takes stairs down. */
void LeavingLevel(bool down);
/** @brief The new level is about to fade in. */
void ArrivingOnLevel();

/** @brief Once a drawn frame: follows the fight round the hero and the hero's death. */
void Advance(float ms);

/**
 * @brief Idea 49. The HD world's share of the screen's fade: while the hero goes down, the world dims more slowly than
 * the fade and the dark closes in on the hero instead (see Iris). Any other time it is the fade as given.
 */
std::array<float, 3> WorldTint(const std::array<float, 3> &fade);

/**
 * @brief Ideas 49 and 53. How far the dark has closed in round the hero: 1 the whole view shows, 0 nothing but the
 * hero's own spot. Sets how dark it is outside the pool (1 black).
 */
float Iris(float &darkness);
/** @brief The smallest the pool gets, in tiles across the floor (the hero's death leaves the body in a small pool). */
float IrisFloor();

/** @brief Idea 53. How far the colour has drained to red at the hero's death: 0 none, 1 the original's red. */
float RedDrain();
/** @brief Idea 53. How much of the dying hero's own light is left (1 all of it). */
float HeroLightKept();
/** @brief Idea 53. How much of its light a flame at a floor point keeps as the hero dies beside it (1 all of it). */
float FlameKept(float x, float y);
/** @brief Whether FlameKept is below 1 anywhere. */
bool FlamesDimmed();

/** @brief Idea 50. How much of their flicker the flames keep in the quiet after a fight (1 all of it). */
float FlickerKept();
/** @brief Idea 50. How much of the mist shows: a fight drives it off and it rolls back in the quiet after (1 all of it). */
float MistKept();
/** @brief Idea 50. True once when the quiet after a fight starts (the dust settles then). */
bool TakeQuietStarted();
/** @brief Idea 50. Whether the quiet is on now. */
bool Quiet();

/** @brief Idea 47. How long the room behind a door takes to come up from the dark, in milliseconds; 0 when off. */
float DoorRevealMs();
/** @brief Idea 47. The HD light saw a door open at this tile. */
void DoorOpened(int x, int y);
/** @brief Idea 47. Takes the next door that opened, for its puff of dust; false when there is none. */
bool TakeDoorOpened(int &x, int &y);

/** @brief Forgets everything (a new game, a level change). */
void Reset();

} // namespace devilution::moments
