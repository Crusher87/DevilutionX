#pragma once

/**
 * @file poe2/live_fire_sdl.hpp
 *
 * POE2 Mashup: a live fire (poe2/live_fire.hpp) drawn with an SDL 2 renderer: worked out on the CPU each frame, sent
 * up as one streaming texture and added over what is under it, in any rect. What goes in front of it (the logo's
 * letters) can be lit by it. The menus' logo uses it (hd_ui.cpp); a torch or a brazier can, with its own maps.
 */

#if !defined(USE_SDL1) && !defined(USE_SDL3)

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <SDL.h>

#include "poe2/live_fire.hpp"

namespace devilution {

namespace fire {

class FireLayer {
public:
	/**
	 * @param threads As LiveFire's: -1 picks from the computer's cores.
	 * @param warm Whether it opens already burning (LiveFire::WarmUp).
	 */
	FireLayer(FireSource source, FireSettings settings, uint32_t seed = 77, int threads = -1, const FireRecipe &recipe = {}, bool warm = true);
	~FireLayer();
	FireLayer(const FireLayer &) = delete;
	FireLayer &operator=(const FireLayer &) = delete;

	LiveFire &Fire() { return *fire_; }

	/** @brief Something painted over the fire's picture (LiveFire::Paint's, top row first) before it is sent up. */
	void SetAfterPaint(std::function<void(uint8_t *, int)> afterPaint) { afterPaint_ = std::move(afterPaint); }

	/**
	 * @brief Where the whole grid goes when its maps' picture goes at `picture`: wider by its padding (FireSource::pad)
	 * on each side. Pointer, Draw and their cover take this rect; DrawLit's `fire` is the picture's.
	 */
	SDL_FRect GridRect(const SDL_FRect &picture) const;

	/**
	 * @brief The pointer is at (x, y), in the same units as `rect` (where the fire is drawn): moving over it blows it.
	 * `inside` false (or a pointer off the fire) forgets where it was, so coming back doesn't blow it from afar.
	 */
	void Pointer(float x, float y, const SDL_FRect &rect, float seconds);

	/**
	 * @brief Moves the fire on by `seconds` and draws it into `rect`, added over what is there. `cover` (grid size,
	 * top row first, 0..255) hides it where something is over it. False if it could not be drawn.
	 */
	bool Draw(SDL_Renderer *renderer, const SDL_FRect &rect, float seconds, const uint8_t *cover = nullptr);

	/**
	 * @brief Draws `picture` at `dest`, lit by the fire as it was last drawn, the fire's maps' picture (the grid without
	 * its padding) being at `fire` (fractions of the picture: x, y, w, h). False when the renderer can't (no render
	 * targets): the caller draws it unlit.
	 */
	bool DrawLit(SDL_Renderer *renderer, SDL_Texture *picture, const SDL_FRect &dest, const SDL_FRect &fire);

	/** @brief The renderer went away, its textures with it. */
	void ForgetTextures();
	/** @brief Lets its textures go, the renderer they were made with still there. */
	void ReleaseTextures();

private:
	bool MakeTextures(SDL_Renderer *renderer);

	std::unique_ptr<LiveFire> fire_;
	std::function<void(uint8_t *, int)> afterPaint_;
	SDL_Renderer *renderer_ = nullptr;
	SDL_Texture *colour_ = nullptr;
	SDL_Texture *multiply_ = nullptr;
	SDL_Texture *add_ = nullptr;
	SDL_Texture *lit_ = nullptr;
	int litWidth_ = 0;
	int litHeight_ = 0;
	bool doubled_ = false;
	bool addDark_ = false;
	bool texturesFailed_ = false;
	bool litFailed_ = false;
	bool lightPainted_ = false;
	bool pointerKnown_ = false;
	float pointerX_ = 0;
	float pointerY_ = 0;
};

} // namespace fire

} // namespace devilution

#endif
