#pragma once

// An experiment (feel("batch", 1), off unless asked for): the HD world's tiles drawn from a few big pictures in
// few draws, where each tile was a picture and a draw of its own.
//
// The HD world is drawn back to front, a few thousand draws a frame, each with its own texture. Here a tile's art is
// also copied into a big shared picture (an atlas page) when it is made, and a run of draws that follow one another
// and show art from the same page goes to the graphics card as one SDL_RenderGeometry call. Nothing is drawn in
// another order than before, with one exception that cannot change the picture: light added on top of a tile (its
// shape light, or light above full) may be drawn after the tiles that follow it, as long as none of them touches the
// place it is added to.
//
// It needs SDL 2.0.18 (SDL_RenderGeometry). Without it, and with the switch off, every draw goes to SDL as before.

#include <cstddef>
#include <cstdint>

#ifdef USE_SDL3
#include <SDL3/SDL_render.h>
#else
#include <SDL.h>
#endif

#if !defined(USE_SDL1) && !defined(USE_SDL3)
#if SDL_VERSION_ATLEAST(2, 0, 18)
#define POE2_HD_BATCH 1
#endif
#endif

#ifdef POE2_HD_BATCH

namespace devilution {

namespace hd {

namespace batch {

/** @brief What a frame of the HD world cost in calls to SDL. */
struct FrameStats {
	/** @brief Draws sent to SDL (a batch counts as one). */
	int draws = 0;
	/** @brief Draws whose texture was not the one of the draw before. */
	int textureSwitches = 0;
	/** @brief Draws that went into a batch, and so cost no call of their own. */
	int merged = 0;
};

/**
 * @brief Starts a frame of the HD world. While `on`, draws of art that is in the atlas are gathered up to EndFrame.
 * @return True when the switch has just been turned on: the art made before it is in no atlas, so the caller throws
 * its pictures away (after EndFrame) and they are made again.
 */
bool BeginFrame(SDL_Renderer *renderer, bool on);
/** @brief Draws what is still gathered and ends the frame. */
void EndFrame(SDL_Renderer *renderer);
/** @brief The calls of the frame that ended last. */
[[nodiscard]] const FrameStats &LastFrame();

/**
 * @brief Copies a tile's art (RGBA32 pixels, as its own texture `texture` holds them) into the atlas, when the
 * experiment is on and there is room. The texture is kept and still drawn where the atlas can't be.
 */
void Add(SDL_Renderer *renderer, SDL_Texture *texture, const void *pixels, int width, int height, int pitch);
/** @brief A texture is gone or its picture has changed: it is no longer drawn from the atlas. */
void Forget(SDL_Texture *texture);
/**
 * @brief Empties the atlas and lets its pages go: the art's own textures are being thrown away (between frames).
 * @param destroy The renderer is still there, so the pages are destroyed; else it took them with it.
 */
void Release(bool destroy);
/** @brief Whether a texture's art is in the atlas. */
[[nodiscard]] bool Has(SDL_Texture *texture);
/** @brief How many pages the atlas has. */
[[nodiscard]] int PageCount();

/** @brief Draws what is gathered so far. Called before any drawing that does not go through this file. */
void FlushPending(SDL_Renderer *renderer);
inline bool Pending = false;
inline void Flush(SDL_Renderer *renderer)
{
	if (Pending)
		FlushPending(renderer);
}

// SDL's calls, as the HD world makes them: gathered where they can be, else passed on (and counted).
int Geometry(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Vertex *vertices, int count, const int *indices, int indexCount);
int CopyF(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *source, const SDL_FRect *dest);
int Copy(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *source, const SDL_Rect *dest);
int FillRect(SDL_Renderer *renderer, const SDL_Rect *rect);
int FillRectF(SDL_Renderer *renderer, const SDL_FRect *rect);
int ClearTarget(SDL_Renderer *renderer);
int SetTarget(SDL_Renderer *renderer, SDL_Texture *texture);
int SetClipRect(SDL_Renderer *renderer, const SDL_Rect *rect);
void DestroyTexture(SDL_Texture *texture);
int UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch);

/** @brief For the tests: the size of a page's side (0 for the renderer's own choice) and how many pages there may be. */
void SetPageLimits(int side, int pages);
/**
 * @brief For the tests: whether light added on top may wait behind tiles that only touch its edge (-1 for the
 * renderer's own choice: yes on a graphics card, no in SDL's software renderer, which rounds edges to whole pixels).
 */
void SetTightEdges(int tight);

} // namespace batch

} // namespace hd

} // namespace devilution

#endif
