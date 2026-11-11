/**
 * @file poe2/mix.hpp
 *
 * POE2 Mashup: a small integer hash for rolls that come from an item or map seed instead of the game's random numbers.
 */
#pragma once

#include <cstdint>

namespace devilution {

/** @brief Scrambles the bits of a seed (a 32-bit integer hash). */
inline uint32_t Mix(uint32_t value)
{
	value ^= value >> 16;
	value *= 0x7FEB352DU;
	value ^= value >> 15;
	value *= 0x846CA68BU;
	value ^= value >> 16;
	return value;
}

} // namespace devilution
