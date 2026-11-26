#pragma once

/**
 * @file poe2/menu_secret.hpp
 *
 * Sanctuary Reforged: the main menu's step of the hidden trail. Once Gillian has told the rumour (secrets.hpp), the
 * burning logo's letters are lit "from the outside in": D, O, I, L, A, B. Each right one stays burning higher, a
 * wrong one lets them all settle back. After the B they all roar up, and a fast sweep of the mouse across them
 * blows them out one by one; when all six are out a cow moos somewhere far off and the fire creeps back.
 * Before the rumour (and after) a click on a letter only makes it flare up.
 *
 * Cosmetic and local: nothing in it touches the game's state, its random numbers or the network.
 */

#if !defined(USE_SDL1) && !defined(USE_SDL3)

#include <SDL.h>

namespace devilution {

namespace fire {
class FireLayer;
}

namespace menu_secret {

/** @brief The left mouse button went down on the main menu (it is looked at with the next frame). */
void NoteClick();

/**
 * @brief Each frame the main menu's logo burns: `grid` is where the fire's whole grid is drawn and (x, y) the pointer,
 * in the same units; `seconds` since the last frame.
 */
void Update(fire::FireLayer &layer, const SDL_FRect &grid, float x, float y, float seconds);

/** @brief The main menu's logo isn't burning (another screen, or its fire was let go): back to the start. */
void Leave();

/** @brief The logo's fire is about to be let go: forget it without touching it. */
void Forget();

} // namespace menu_secret

} // namespace devilution

#endif
