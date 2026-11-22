/**
 * @file poe2/camera_shake.hpp
 *
 * POE2 Mashup camera shake: heavy hits (slams, explosions, landings) jolt the view for a moment so they feel weighty.
 * Only the picture moves; the game and the mouse are unaffected. Sanctuary Reforged shakes it for a few of Diablo's own
 * big moments (ShakeCameraFor).
 */
#pragma once

#include <cstdint>

#include "engine/displacement.hpp"

namespace devilution {

struct Monster;

/** @brief How hard a skill shakes the view, in pixels at its strongest. */
enum class ShakeStrength : uint8_t {
	Light = 2,
	Medium = 4,
	Heavy = 6,
};

/**
 * @brief Sanctuary Reforged: Diablo's own big moments that shake the view, gently. Only these, so the shake stays
 * special: the hero's own Nova, Immolation and Apocalypse, Diablo's Apocalypse blast reaching the hero, and the
 * Butcher's or Diablo's own melee blows landing on the hero.
 */
enum class ReforgedShake : uint8_t {
	Nova,
	Immolation,
	Apocalypse,
	DiabloApocalypse,
	BossBlow,
};

/** @brief Starts a shake; a stronger or longer one already running is kept. */
void ShakeCamera(ShakeStrength strength, int ticks);
/** @brief Sanctuary Reforged: shakes the view for one of Diablo's own big moments; nothing in any other mode. */
void ShakeCameraFor(ReforgedShake moment);
/** @brief Sanctuary Reforged: whether this monster's melee blows shake the view (the Butcher and Diablo). */
bool BlowsShakeCamera(const Monster &monster);
/** @brief Fades the shake by one game tick. */
void TickCameraShake();
/** @brief How far to move the view this frame. */
Displacement CameraShakeOffset();
/** @brief Stops any shake at once. */
void StopCameraShake();

} // namespace devilution
