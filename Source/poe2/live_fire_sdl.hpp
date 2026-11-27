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

// Worked out off the game's thread where it has threads for sure (as live_fire.cpp's workers).
#if defined(_WIN32) || defined(__APPLE__) || (defined(__linux__) && !defined(__ANDROID__))
#define POE2_FIRE_LAYER_THREAD 1
#include <condition_variable>
#include <mutex>
#include <thread>
#endif

#include <SDL.h>

#include "poe2/live_fire.hpp"

namespace devilution {

namespace fire {

/** @brief How FireLayer::DrawAsync paces the fire's frames and how much of the computer it gives them. */
struct FirePace {
	/**
	 * @brief The least time between two frames of the fire, in seconds (0: one each frame of the game). The game's
	 * frames are counted whole: a frame of the fire starts on the game's frame that brings the time since the last to
	 * within a third of a frame of this, so at a steady frame rate it starts every so many frames, always the same
	 * number (not now every second, now every third, as the frames' lengths happen to add up).
	 */
	float interval = 1.F / 40.F;
	/**
	 * @brief How many of the fire's workers help its thread (-1: all it has). A few by default, so it keeps off most
	 * of the computer's cores (the game's frame needs them).
	 */
	int workers = 2;
};

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

	/** @brief The fire itself, once any frame being worked out off the game's thread (DrawAsync) is done. */
	LiveFire &Fire()
	{
		WaitForFrame();
		return *fire_;
	}

	/** @brief The fire's grid size (fixed: safe while a frame is worked out). */
	int GridWidth() const { return gridWidth_; }
	int GridHeight() const { return gridHeight_; }

	/**
	 * @brief Something painted over the fire's picture (LiveFire::Paint's, top row first) before it is sent up. With
	 * DrawAsync it runs on the fire's thread: what it reads is set in `beforeFrame`, which runs on the game's thread
	 * each time a frame of the fire is started (Draw's too).
	 */
	void SetAfterPaint(std::function<void(uint8_t *, int)> afterPaint, std::function<void()> beforeFrame = {})
	{
		WaitForFrame();
		afterPaint_ = std::move(afterPaint);
		beforeFrame_ = std::move(beforeFrame);
	}

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
	 * @brief As Draw, but the fire is worked out off the game's thread, up to 40 times a second (`pace`): each call
	 * sends up the last frame finished (if a new one is) and starts the next, then draws what it has. The game's frame
	 * only pays for sending it up. (Without threads: Draw.)
	 */
	bool DrawAsync(SDL_Renderer *renderer, const SDL_FRect &rect, float seconds, const uint8_t *cover = nullptr, const FirePace &pace = {});

	/**
	 * @brief As DrawAsync without drawing it: the fire moves on unseen, off the game's thread. (Without threads it
	 * moves on at once.)
	 */
	void AdvanceAsync(float seconds, const FirePace &pace = {});

	/** @brief Whether a frame of the fire should start now: `owed` seconds since the last, the game's frame `seconds` long. */
	static bool FrameIsDue(float owed, float seconds, float interval);

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
	/** @brief Waits for the frame being worked out off the game's thread, if one is. */
	void WaitForFrame();
	/** @brief Sends the finished frame's pictures up to the textures. */
	void UploadFrame();
	/** @brief The thread's loop: works out each frame it is given. */
	void Loop();
	/** @brief DrawAsync's and AdvanceAsync's: takes in a finished frame (sent up if `upload`) and starts the next when due. */
	void Pump(float seconds, const uint8_t *cover, const FirePace &pace, bool upload);

	std::unique_ptr<LiveFire> fire_;
	int gridWidth_ = 0;
	int gridHeight_ = 0;
	std::function<void(uint8_t *, int)> afterPaint_;
	std::function<void()> beforeFrame_;
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
	/** @brief The light sent up (counted), and which of it, picture and place the lit picture (lit_) holds. */
	uint64_t lightVersion_ = 0;
	uint64_t litVersion_ = 0;
	bool litValid_ = false;
	SDL_Texture *litPicture_ = nullptr;
	SDL_FRect litFire_ {};
	float pointerX_ = 0;
	float pointerY_ = 0;

	// ---- DrawAsync's: what the thread works out, and into what ----
	struct PendingPush {
		float x0, y0, x1, y1, seconds;
	};
	std::vector<PendingPush> pushes_;
	std::vector<uint8_t> frameColour_;
	std::vector<uint8_t> frameMultiply_;
	std::vector<uint8_t> frameAdd_;
	std::vector<uint8_t> frameCover_;
	bool frameHasCover_ = false;
	/** @brief Whether the frame being worked out is painted (DrawAsync's) or only moves the fire on (AdvanceAsync's). */
	bool framePainted_ = true;
	float frameSeconds_ = 0;
	/** @brief Seconds gone by since the last frame was started. */
	float owedSeconds_ = 0;
	bool frameReady_ = false;
	bool frameShown_ = false;
	/**
	 * @brief Keeping up with a pace quicker than the steady one (Pump): late frames in the window of the game's frames
	 * being counted, the game's frames left of a spell at the steady pace, and how many such spells there have been.
	 */
	static constexpr float SteadyInterval = 1.F / 40.F;
	static constexpr int LateWindow = 120;
	static constexpr int LateLimit = 4;
	static constexpr int SteadyFrames = 600;
	int late_ = 0;
	int lateWindow_ = 0;
	int steadyFrames_ = 0;
	int steadySpells_ = 0;
#ifdef POE2_FIRE_LAYER_THREAD
	std::thread thread_;
	std::mutex lock_;
	std::condition_variable wake_;
	std::condition_variable done_;
	bool working_ = false;
	bool quit_ = false;
#endif
};

} // namespace fire

} // namespace devilution

#endif
