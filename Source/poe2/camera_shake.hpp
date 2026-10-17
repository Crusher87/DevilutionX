/**
 * @file poe2/camera_shake.hpp
 *
 * POE2 Mashup camera shake: heavy hits (slams, explosions, landings) jolt the view for a moment so they feel weighty.
 * Only the picture moves; the game and the mouse are unaffected.
 */
#pragma once

#include "engine/displacement.hpp"

namespace devilution {

/** @brief How hard a skill shakes the view, in pixels at its strongest. */
enum class ShakeStrength : uint8_t {
	Light = 2,
	Medium = 4,
	Heavy = 6,
};

/** @brief Starts a shake; a stronger or longer one already running is kept. */
void ShakeCamera(ShakeStrength strength, int ticks);
/** @brief Fades the shake by one game tick. */
void TickCameraShake();
/** @brief How far to move the view this frame. */
Displacement CameraShakeOffset();

} // namespace devilution
