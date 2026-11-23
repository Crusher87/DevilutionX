/**
 * @file dx.h
 *
 * Interface of functions setting up the graphics pipeline.
 */
#pragma once

#ifdef USE_SDL3
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#else
#include <SDL.h>
#endif

#include "engine/surface.hpp"
#include "utils/attributes.h"

namespace devilution {

/** Whether we render directly to the screen surface, i.e. `PalSurface == GetOutputSurface()` */
extern bool RenderDirectlyToOutputSurface;

extern DVL_API_FOR_TEST SDL_Surface *PalSurface;

Surface GlobalBackBuffer();

/**
 * @brief POE2 Mashup: with the UI scaled, the world is drawn into this full-size frame; otherwise it's the back buffer.
 */
Surface WorldBackBuffer();
/**
 * @brief POE2 Mashup: the world is drawn into a frame of its own (WorldBackBuffer), under the UI: with the UI scaled,
 * and at UI Scale 100 with HD Graphics or Modern Effects on (WantsWorldLayer), where that frame is the UI's size.
 */
bool HasWorldLayer();
/** @brief Makes or drops the world's frame at UI Scale 100 as HD Graphics and Modern Effects are switched; before a frame is drawn. */
void SyncWorldLayer();
/** @brief The world is drawn: put a shrunk copy of it under the UI, which is drawn next. */
void PrepareScaledUi();
/** @brief What is on screen now has no world under it (menus, loading screens). */
void ClearScaledWorld();

void dx_init();
void dx_cleanup();
void CreateBackBuffer();
void BltFast(SDL_Rect *srcRect, SDL_Rect *dstRect);
void Blit(SDL_Surface *src, SDL_Rect *srcRect, SDL_Rect *dstRect);
void RenderPresent();

} // namespace devilution
