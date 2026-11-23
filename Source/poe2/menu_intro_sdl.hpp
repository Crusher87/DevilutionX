#pragma once

/**
 * @file poe2/menu_intro_sdl.hpp
 *
 * POE2 Mashup: the main menu's intro (poe2/menu_intro.hpp) on the screen, with an SDL 2 renderer, and what it and the
 * menus' logos (hd_ui.cpp) need from each other. The game's side is in poe2/hd.hpp (MenuIntroWanted and on).
 */

#if !defined(USE_SDL1) && !defined(USE_SDL3)

#include <SDL.h>

#include "poe2/live_fire_sdl.hpp"

namespace devilution {

namespace hd {

// ---- from hd_ui.cpp ----

/** @brief The menu logos' live fire (made from its maps the first time); null without them. */
fire::FireLayer *MenuLogoFire();
/** @brief The sharpest HD art of the logo (the title screen's, else the main menu's) as a texture; null without it. */
SDL_Texture *MenuLogoArt(SDL_Renderer *renderer);

namespace intro_sdl {

// ---- to hd_ui.cpp ----

/**
 * @brief The main menu's logo is about to be drawn with its fire, its picture (where the fire's maps go) at `picture`:
 * true while the intro draws it itself (where it lands, and so the intro's).
 */
bool HoldsMenuLogo(const SDL_FRect &picture);
/** @brief Once the intro has shown: the burning lines round the subtitle, behind the logo's letters. */
void DrawLines(SDL_Renderer *renderer, const SDL_FRect &picture, float seconds);
/** @brief Once the intro has shown: the subtitle, cooled, over the logo's letters. */
void DrawSubtitle(SDL_Renderer *renderer, const SDL_FRect &picture);
/** @brief Lets the lines' fire go (the logo's went with ForgetMenuFire). */
void ReleaseLines();
/** @brief The renderer went away, its textures with it. */
void ForgetTextures();

/**
 * @brief For tests: an intro of its own, played on `renderer` without the menus: `ui` (the menu as it would be drawn,
 * or null for black) fades in over it, and the logo lands with its picture at `picture`
 * (logical pixels). Seeks to `t` in steps of 1/60 s, as the reference page's own seek (from where the last call left
 * it, when that was earlier), then draws that frame onto the renderer's window. False if it can't play (its art missing).
 */
bool RenderForTest(SDL_Renderer *renderer, SDL_Texture *ui, const SDL_FRect &picture, float t);
/** @brief For tests: lets that intro go. */
void EndForTest();

} // namespace intro_sdl

} // namespace hd

} // namespace devilution

#endif
