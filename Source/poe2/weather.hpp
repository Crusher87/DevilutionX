/**
 * @file poe2/weather.hpp
 *
 * POE2 Mashup dynamic weather: rain (and snow) that comes and goes outdoors, with its own sound. The weather's state
 * lives here in one place and the effects engine (vfx) draws it. Today it is cosmetic and local: it never touches the
 * game state, its seeded random numbers or saves. A skill or map that wants weather to matter later drives this same
 * state through Request(), from the game's own seeded dice and the command system, with no change to the drawing.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "utils/attributes.h"

namespace devilution::weather {

enum class Kind : uint8_t {
	Clear,
	Rain,
	Snow,
};

/** @brief What the weather is doing right now. */
struct State {
	Kind kind = Kind::Clear;
	/** @brief How hard it rains or snows, 0 (nothing) to 1 (a downpour). */
	float strength = 0;
	/** @brief The wind, -1 (hard from the right) to 1 (hard from the left); drops lean with it. */
	float wind = 0;
	/** @brief A lightning flash lighting the screen, 1 at the strike fading to 0. */
	float flash = 0;
};

/** @brief Whether this level has a sky: today, the town. Dungeons stay dry. */
bool Outdoors();
/** @brief Whether the weather runs at all: POE2 Mashup, the Weather option and the Modern Effects it draws with. */
bool Enabled();
/** @brief What the weather is doing; strength is 0 wherever there is no sky. */
const State &Current();
/** @brief The weather a level gets when its schedule turns wet: rain in town, snow where a cold zone asks for it. */
Kind Preset();
void SetPreset(Kind kind);
/**
 * @brief A skill, boss or map calling the weather: the given kind at the given strength for the given time, fading in
 * and out, after which the schedule takes over again.
 */
void Request(Kind kind, float strength, float seconds);
/** @brief The console forcing a look (weather("rain", 0.8)); nullopt hands it back to the schedule. */
void Force(std::optional<Kind> kind, float strength = 1);
/** @brief Moves the weather on by the given real time: its schedule, wind, lightning and sound. */
void Advance(float ms);
/** @brief Starts the schedule over from the given seed (tests, and a future seeded multiplayer weather). */
void Reseed(uint32_t seed);
/** @brief Stops the weather sound before the audio shuts down. */
void StopSound();
/** @brief What the console's weather() shows. */
std::string Text();
/** @brief The kind named "rain", "snow" or "clear", if any. */
std::optional<Kind> KindNamed(std::string_view name);
/** @brief The share of the last simulated time that was wet (tests). */
float WetShare();

} // namespace devilution::weather
