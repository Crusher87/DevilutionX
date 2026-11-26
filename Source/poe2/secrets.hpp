#pragma once

/**
 * @file poe2/secrets.hpp
 *
 * Sanctuary Reforged: the hidden trail to the secret level. What this PC has found on it is kept in secrets.ini next
 * to the settings, so every hero (and a new one after a hardcore death) shares it. Nothing here goes into a save or
 * over the network: each player's own computer remembers what that player found.
 */

#include <cstdint>

namespace devilution {

namespace secrets {

/** @brief The steps of the trail found so far, in the order they are found. */
enum class Found : uint8_t {
	/** @brief A town cow was clicked until the hero grumbled about it. */
	PesteredCow,
	/** @brief Gillian told the rumour about the Dark One's name. */
	HeardRumour,
	/** @brief The menu logo's letters were lit from the outside in, then blown out. */
	LitName,
};

/** @brief Whether this PC has found that step. */
bool Has(Found step);

/** @brief Remembers that this PC found that step (and writes it down at once). */
void Set(Found step);

/** @brief Forgets the whole trail (the console's secrets("reset")). */
void Reset();

} // namespace secrets

} // namespace devilution
