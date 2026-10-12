/**
 * @file controls/wasd_movement.hpp
 *
 * POE2-style keyboard movement: hold W, A, S or D to walk.
 */
#pragma once

#ifdef USE_SDL3
#include <SDL3/SDL_keycode.h>
#else
#include <SDL.h>

#ifdef USE_SDL1
#include "utils/sdl2_to_1_2_backports.h"
#endif
#endif

#include "controls/axis_direction.h"

namespace devilution {

/** @brief Whether the player chose WASD movement over click-to-move only. */
bool IsWasdMovementEnabled();

/** @brief Turns WASD movement on or off and saves the choice. */
void SetWasdMovementEnabled(bool enabled);

/**
 * @brief Tracks a movement key being pressed.
 * @return true if the key is a movement key and should not trigger anything else.
 */
bool WasdMovementKeyPressed(SDL_Keycode key);

/**
 * @brief Tracks a movement key being released.
 * @return true if the key is a movement key and should not trigger anything else.
 */
bool WasdMovementKeyReleased(SDL_Keycode key);

/** @brief Forgets all held movement keys, e.g. when the window loses focus. */
void ClearWasdMovementKeys();

/** @brief Direction on screen the held movement keys point to. */
AxisDirection GetWasdMovementDirection();

} // namespace devilution
