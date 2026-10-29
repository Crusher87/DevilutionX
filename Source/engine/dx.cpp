/**
 * @file dx.cpp
 *
 * Implementation of functions setting up the graphics pipeline.
 */
#include "engine/dx.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#else
#include <SDL.h>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "controls/control_mode.hpp"
#include "controls/plrctrls.h"
#include "engine/render/automap_render.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/ui_scale.hpp"
#include "headless_mode.hpp"
#include "init.hpp"
#include "options.h"
#include "poe2/hd.hpp"
#include "poe2/skill_lab.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/vfx.hpp"
#include "utils/display.h"
#include "utils/log.hpp"
#include "utils/palette_blending.hpp"
#include "utils/sdl_wrap.h"
#include "utils/static_vector.hpp"

#ifndef USE_SDL1
#include "controls/touch/renderers.h"
#endif

#ifdef __3DS__
#include <3ds.h>
#endif

namespace devilution {

int refreshDelay;
SDL_Renderer *renderer;
#ifndef USE_SDL1
SDLTextureUniquePtr texture;
#endif

/** Currently active palette */
SDLPaletteUniquePtr Palette;

/** 24-bit renderer texture surface */
SDLSurfaceUniquePtr RendererTextureSurface;

/** 8-bit surface that we render to */
SDL_Surface *PalSurface;

namespace {

/** POE2 Mashup: with the UI scaled, the full-size frame: the world, then the UI scaled up over it. */
SDLSurfaceUniquePtr WorldSurface;
/** The UI frame as it was before the UI was drawn on it: what's left unchanged shows the full-size world. */
std::vector<uint8_t> UiBase;
/** Where the UI drew its panels, boxes and lines over the world, even in the world's own colours. */
std::vector<UiCover> UiCovered;
/** POE2 Mashup: UI pixels laid over the world with real alpha, such as the Tab map's floor (Curtis 2026-10-29). */
std::vector<UiTint> UiTinted;
/** For each full-size column and row, the UI column and row it shows. */
std::vector<uint16_t> UiColumnOf;
std::vector<uint16_t> UiRowOf;
bool HaveScaledWorld = false;
#if !defined(USE_SDL1) && !defined(USE_SDL3)
/** POE2 Mashup: a frame waiting to be presented in layers: the world, the effects, then the UI over them. */
bool LayeredFramePending = false;
/** The UI with the world left out (transparent), at the UI's size. */
SDL_Texture *UiOverlay = nullptr;
SDL_Renderer *UiOverlayRenderer = nullptr;
int UiOverlayWidth = 0;
int UiOverlayHeight = 0;

bool WantsLayeredFrame()
{
	return WorldSurface != nullptr && HaveScaledWorld && renderer != nullptr && (vfx::Enabled() || hd::Active());
}

/**
 * @brief Fills the UI overlay: the UI's own pixels opaque, the ones still showing the world transparent, the ones a
 * see-through box darkens half black, and tinted ones (TintUiPixel) in their colour at their own alpha.
 */
bool UpdateUiOverlay()
{
	if (UiOverlay == nullptr || UiOverlayRenderer != renderer || UiOverlayWidth != gnScreenWidth || UiOverlayHeight != gnScreenHeight) {
		if (UiOverlay != nullptr && UiOverlayRenderer == renderer)
			SDL_DestroyTexture(UiOverlay);
		UiOverlay = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, gnScreenWidth, gnScreenHeight);
		UiOverlayRenderer = renderer;
		UiOverlayWidth = gnScreenWidth;
		UiOverlayHeight = gnScreenHeight;
		if (UiOverlay == nullptr)
			return false;
		SDL_SetTextureBlendMode(UiOverlay, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
		SDL_SetTextureScaleMode(UiOverlay, SDL_ScaleModeNearest);
#endif
	}
	void *pixels;
	int pitch;
	if (SDL_LockTexture(UiOverlay, nullptr, &pixels, &pitch) != 0)
		return false;
	const SDL_Color *colors = Palette->colors;
	const auto *ui = static_cast<const uint8_t *>(PalSurface->pixels);
	const bool covers = UiCovered.size() == UiBase.size();
	const bool tints = UiTinted.size() == UiBase.size();
	for (int y = 0; y < gnScreenHeight; y++) {
		const uint8_t *uiLine = ui + static_cast<ptrdiff_t>(y) * PalSurface->pitch;
		const uint8_t *baseLine = UiBase.data() + static_cast<size_t>(y) * gnScreenWidth;
		// A panel's pixel in the same colour as the world under it is still the panel's.
		const UiCover *coverLine = covers ? UiCovered.data() + static_cast<size_t>(y) * gnScreenWidth : nullptr;
		const bool covered = coverLine != nullptr && std::any_of(coverLine, coverLine + gnScreenWidth, [](UiCover cover) { return cover != UiCover::None; });
		const UiTint *tintLine = tints ? UiTinted.data() + static_cast<size_t>(y) * gnScreenWidth : nullptr;
		const bool tinted = tintLine != nullptr && std::any_of(tintLine, tintLine + gnScreenWidth, [](UiTint tint) { return tint.alpha != 0; });
		auto *out = static_cast<uint8_t *>(pixels) + static_cast<ptrdiff_t>(y) * pitch;
		if (!covered && !tinted && std::memcmp(uiLine, baseLine, gnScreenWidth) == 0) {
			std::memset(out, 0, static_cast<size_t>(gnScreenWidth) * 4);
			continue;
		}
		for (int x = 0; x < gnScreenWidth; x++, out += 4) {
			// POE2 Mashup: a tint nothing has been drawn over since lies over the HD world at its own alpha, the same
			// whatever the 8-bit world under it, so it holds steady while the world scrolls (Curtis 2026-10-29).
			if (tinted && tintLine[x].alpha != 0 && tintLine[x].color == uiLine[x] && (!covered || coverLine[x] != UiCover::Solid)) {
				const SDL_Color &color = colors[uiLine[x]];
				out[0] = color.r;
				out[1] = color.g;
				out[2] = color.b;
				out[3] = tintLine[x].alpha;
				continue;
			}
			if (uiLine[x] == baseLine[x] && (!covered || coverLine[x] == UiCover::None)) {
				out[3] = 0;
				continue;
			}
			// A see-through box's darkened world is black laid half over the world and the effects alike, once or twice.
			if (covered && coverLine[x] == UiCover::Shaded) {
				const uint8_t once = paletteTransparencyLookup[0][baseLine[x]];
				if (uiLine[x] == baseLine[x] || uiLine[x] == once || uiLine[x] == paletteTransparencyLookup[0][once]) {
					out[0] = 0;
					out[1] = 0;
					out[2] = 0;
					out[3] = (uiLine[x] == once || uiLine[x] == baseLine[x]) ? 128 : 192;
					continue;
				}
			}
			const SDL_Color &color = colors[uiLine[x]];
			out[0] = color.r;
			out[1] = color.g;
			out[2] = color.b;
			out[3] = 255;
		}
	}
	SDL_UnlockTexture(UiOverlay);
	return true;
}

/** @brief Presents the world, the effects over it, and the UI over both. */
void PresentLayeredFrame(SDL_Surface *surface)
{
	LayeredFramePending = false;
	Blit(WorldSurface.get(), nullptr, nullptr);
	if (SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) <= -1) ErrSdl();
	if (SDL_RenderClear(renderer) <= -1) ErrSdl();
	if (SDL_UpdateTexture(texture.get(), nullptr, surface->pixels, surface->pitch) <= -1) ErrSdl();
	if (SDL_RenderCopy(renderer, texture.get(), nullptr, nullptr) <= -1) ErrSdl();
	// POE2 Mashup: the HD world goes over the game's own, which still shows outside the view.
	TelemetryDrawStep("drawing effects");
	if (hd::Active())
		hd::Render(renderer, { gnRenderWidth, gnRenderHeight }, { gnScreenWidth, gnScreenHeight });
	vfx::Render(renderer, { gnRenderWidth, gnRenderHeight }, { gnScreenWidth, gnScreenHeight });
	TelemetryDrawStep(nullptr);
	if (UpdateUiOverlay())
		SDL_RenderCopy(renderer, UiOverlay, nullptr, nullptr);
	SkillLabCapturePresented(renderer);
}
#endif

void CreateWorldSurface()
{
	WorldSurface = nullptr;
	SetUiCoverTarget(nullptr, nullptr);
	UiBase.clear();
	UiCovered.clear();
	UiTinted.clear();
	HaveScaledWorld = false;
	if (!IsUiScaled())
		return;
	WorldSurface = SDLWrap::CreateRGBSurfaceWithFormat(0, gnRenderWidth, gnRenderHeight, 8, SDL_PIXELFORMAT_INDEX8);
#ifdef USE_SDL3
	if (!SDL_SetSurfacePalette(WorldSurface.get(), Palette.get())) ErrSdl();
#elif !defined(USE_SDL1)
	if (SDL_SetSurfacePalette(WorldSurface.get(), Palette.get()) < 0)
		ErrSdl();
#endif
	UiBase.assign(static_cast<size_t>(gnScreenWidth) * gnScreenHeight, 0);
	UiCovered.assign(UiBase.size(), UiCover::None);
	UiTinted.assign(UiBase.size(), UiTint {});
	UiColumnOf.resize(gnRenderWidth);
	for (int x = 0; x < gnRenderWidth; x++)
		UiColumnOf[x] = static_cast<uint16_t>(std::min(x * gnScreenWidth / gnRenderWidth, gnScreenWidth - 1));
	UiRowOf.resize(gnRenderHeight);
	for (int y = 0; y < gnRenderHeight; y++)
		UiRowOf[y] = static_cast<uint16_t>(std::min(y * gnScreenHeight / gnRenderHeight, gnScreenHeight - 1));
}

/** @brief Builds the full-size frame: the UI scaled up, over the world where the UI drew. */
void ComposeScaledFrame()
{
	const auto *ui = static_cast<const uint8_t *>(PalSurface->pixels);
	const int uiPitch = PalSurface->pitch;
	auto *frame = static_cast<uint8_t *>(WorldSurface->pixels);
	const int framePitch = WorldSurface->pitch;
	for (int y = 0; y < gnRenderHeight; y++) {
		const int uiRow = UiRowOf[y];
		const uint8_t *uiLine = ui + static_cast<ptrdiff_t>(uiRow) * uiPitch;
		uint8_t *frameLine = frame + static_cast<ptrdiff_t>(y) * framePitch;
		if (!HaveScaledWorld) {
			for (int x = 0; x < gnRenderWidth; x++)
				frameLine[x] = uiLine[UiColumnOf[x]];
			continue;
		}
		const uint8_t *baseLine = UiBase.data() + static_cast<size_t>(uiRow) * gnScreenWidth;
		const UiTint *tintLine = UiTinted.size() == UiBase.size() ? UiTinted.data() + static_cast<size_t>(uiRow) * gnScreenWidth : nullptr;
		const bool tinted = tintLine != nullptr && std::any_of(tintLine, tintLine + gnScreenWidth, [](UiTint tint) { return tint.alpha != 0; });
		if (!tinted && std::memcmp(uiLine, baseLine, gnScreenWidth) == 0)
			continue;
		for (int x = 0; x < gnRenderWidth; x++) {
			const uint16_t column = UiColumnOf[x];
			const uint8_t color = uiLine[column];
			// POE2 Mashup: a tint is blended into the full-size world, not the shrunk copy of it under the UI.
			if (tinted && tintLine[column].alpha != 0 && tintLine[column].color == color) {
				frameLine[x] = BlendMapColor(color, frameLine[x], tintLine[column].alpha * 100 / 255);
				continue;
			}
			if (color != baseLine[column])
				frameLine[x] = color;
		}
	}
}

} // namespace
namespace {
SDLSurfaceUniquePtr PinnedPalSurface;

#ifndef USE_SDL1
constexpr size_t MaxDirtyRects = 16;
StaticVector<SDL_Rect, MaxDirtyRects> DirtyRects;
bool ForceFullUpdate;
const void *LastPresentedPixels;
#endif
} // namespace

/** Whether we render directly to the screen surface, i.e. `PalSurface == GetOutputSurface()` */
bool RenderDirectlyToOutputSurface;

namespace {

bool CanRenderDirectlyToOutputSurface()
{
#ifdef USE_SDL1
#ifdef SDL1_FORCE_DIRECT_RENDER
	return true;
#else
	auto *outputSurface = GetOutputSurface();
	return ((outputSurface->flags & SDL_DOUBLEBUF) == SDL_DOUBLEBUF
	    && outputSurface->w == gnScreenWidth && outputSurface->h == gnScreenHeight
	    && outputSurface->format->BitsPerPixel == 8);
#endif
#else // !USE_SDL1
	if (renderer != nullptr) return false;
	SDL_Surface *outputSurface = GetOutputSurface();
	// Assumes double-buffering is available.
	return outputSurface->w == static_cast<int>(gnScreenWidth)
	    && outputSurface->h == static_cast<int>(gnScreenHeight)
	    && SDLC_SURFACE_BITSPERPIXEL(outputSurface) == 8;
#endif
}

/**
 * @brief Limit FPS to avoid high CPU load, use when v-sync isn't available
 */
void LimitFrameRate()
{
	if (*GetOptions().Graphics.frameRateControl != FrameRateControl::CPUSleep)
		return;
	static uint32_t frameDeadline;
	const uint32_t tc = SDL_GetTicks() * 1000;
	uint32_t v = 0;
	if (frameDeadline > tc) {
		v = tc % refreshDelay;
		SDL_Delay((v / 1000) + 1); // ceil
	}
	frameDeadline = tc + v + refreshDelay;
}

} // namespace

void dx_init()
{
#ifndef USE_SDL1
	SDL_RaiseWindow(ghMainWnd);
	SDL_ShowWindow(ghMainWnd);
#endif

	Palette = SDLWrap::AllocPalette();
	palette_init();
	CreateBackBuffer();
}

Surface GlobalBackBuffer()
{
	return Surface(PalSurface, SDL_Rect { 0, 0, gnScreenWidth, gnScreenHeight });
}

Surface WorldBackBuffer()
{
	if (WorldSurface == nullptr)
		return GlobalBackBuffer();
	return Surface(WorldSurface.get(), SDL_Rect { 0, 0, gnRenderWidth, gnRenderHeight });
}

void PrepareScaledUi()
{
	if (WorldSurface == nullptr)
		return;
	const auto *world = static_cast<const uint8_t *>(WorldSurface->pixels);
	const int worldPitch = WorldSurface->pitch;
	auto *ui = static_cast<uint8_t *>(PalSurface->pixels);
	const int uiPitch = PalSurface->pitch;
	for (int y = 0; y < gnScreenHeight; y++) {
		// Sample the middle of the patch of the world each UI pixel covers.
		const int worldY = std::min((2 * y + 1) * gnRenderHeight / (2 * gnScreenHeight), gnRenderHeight - 1);
		const uint8_t *worldLine = world + static_cast<ptrdiff_t>(worldY) * worldPitch;
		uint8_t *uiLine = ui + static_cast<ptrdiff_t>(y) * uiPitch;
		for (int x = 0; x < gnScreenWidth; x++)
			uiLine[x] = worldLine[std::min((2 * x + 1) * gnRenderWidth / (2 * gnScreenWidth), gnRenderWidth - 1)];
		std::memcpy(UiBase.data() + static_cast<size_t>(y) * gnScreenWidth, uiLine, gnScreenWidth);
	}
	HaveScaledWorld = true;
	// The UI drawn from here on notes where it covers the world.
	std::fill(UiCovered.begin(), UiCovered.end(), UiCover::None);
	std::fill(UiTinted.begin(), UiTinted.end(), UiTint {});
	const bool fits = PalSurface->w == gnScreenWidth && PalSurface->h == gnScreenHeight && UiCovered.size() == UiBase.size();
	const bool tintFits = fits && UiTinted.size() == UiBase.size();
	SetUiCoverTarget(PalSurface, fits ? UiCovered.data() : nullptr, tintFits ? UiTinted.data() : nullptr);
}

void ClearScaledWorld()
{
	HaveScaledWorld = false;
	SetUiCoverTarget(nullptr, nullptr);
}

void dx_cleanup()
{
#ifndef USE_SDL1
	if (ghMainWnd != nullptr)
		SDL_HideWindow(ghMainWnd);
#endif

	SetUiCoverTarget(nullptr, nullptr);
	PalSurface = nullptr;
	PinnedPalSurface = nullptr;
	WorldSurface = nullptr;
	Palette = nullptr;
	RendererTextureSurface = nullptr;
#ifndef USE_SDL1
	texture = nullptr;
	FreeVirtualGamepadTextures();
	if (*GetOptions().Graphics.upscale)
		SDL_DestroyRenderer(renderer);
#endif
	SDL_DestroyWindow(ghMainWnd);
}

void CreateBackBuffer()
{
	if (CanRenderDirectlyToOutputSurface()) {
		Log("{}", "Will render directly to the SDL output surface");
		PalSurface = GetOutputSurface();
		RenderDirectlyToOutputSurface = true;
	} else {
		PinnedPalSurface = SDLWrap::CreateRGBSurfaceWithFormat(
		    /*flags=*/0,
		    /*width=*/gnScreenWidth,
		    /*height=*/gnScreenHeight,
		    /*depth=*/8,
		    SDL_PIXELFORMAT_INDEX8);
		PalSurface = PinnedPalSurface.get();
	}

#if defined(USE_SDL3)
	if (!SDL_SetSurfacePalette(PalSurface, Palette.get())) ErrSdl();
#elif !defined(USE_SDL1)
	// In SDL2, `PalSurface` points to the global `palette`.
	if (SDL_SetSurfacePalette(PalSurface, Palette.get()) < 0)
		ErrSdl();
#else
	// In SDL1, `PalSurface` owns its palette and we must update it every
	// time the global `palette` is changed. No need to do anything here as
	// the global `palette` doesn't have any colors set yet.
#endif
	CreateWorldSurface();
}

void BltFast(SDL_Rect *srcRect, SDL_Rect *dstRect)
{
	if (WorldSurface != nullptr && !HeadlessMode) {
#if !defined(USE_SDL1) && !defined(USE_SDL3)
		// With the effects layer, the world and the UI go up separately when the frame is presented.
		LayeredFramePending = WantsLayeredFrame();
		if (LayeredFramePending)
			return;
#endif
		// The UI is scaled up over the full-size world, always the whole frame.
		ComposeScaledFrame();
		Blit(WorldSurface.get(), nullptr, nullptr);
		return;
	}
	if (RenderDirectlyToOutputSurface) {
#ifndef USE_SDL1
		// A null rect means the caller changed the entire surface.
		if (dstRect == nullptr || DirtyRects.size() == MaxDirtyRects)
			ForceFullUpdate = true;
		else
			DirtyRects.push_back(*dstRect);
#endif
		return;
	}
	Blit(PalSurface, srcRect, dstRect);
}

void Blit(SDL_Surface *src, SDL_Rect *srcRect, SDL_Rect *dstRect)
{
	if (HeadlessMode)
		return;

	SDL_Surface *dst = GetOutputSurface();
#if defined(USE_SDL3)
	if (!SDL_BlitSurface(src, srcRect, dst, dstRect)) ErrSdl();
#elif !defined(USE_SDL1)
	if (SDL_BlitSurface(src, srcRect, dst, dstRect) < 0)
		ErrSdl();
#else
	if (!OutputRequiresScaling()) {
		if (SDL_BlitSurface(src, srcRect, dst, dstRect) < 0)
			ErrSdl();
		return;
	}

	SDL_Rect scaledDstRect;
	if (dstRect != NULL) {
		scaledDstRect = *dstRect;
		ScaleOutputRect(&scaledDstRect);
		dstRect = &scaledDstRect;
	}

	// Same pixel format: We can call BlitScaled directly.
	if (SDLBackport_PixelFormatFormatEq(src->format, dst->format)) {
		if (SDL_BlitScaled(src, srcRect, dst, dstRect) < 0)
			ErrSdl();
		return;
	}

	// If the surface has a color key, we must stretch first and can then call BlitSurface.
	if (SDL_HasColorKey(src)) {
		SDLSurfaceUniquePtr stretched = SDLWrap::CreateRGBSurface(SDL_SWSURFACE, dstRect->w, dstRect->h, src->format->BitsPerPixel,
		    src->format->Rmask, src->format->Gmask, src->format->BitsPerPixel, src->format->Amask);
		SDL_SetColorKey(stretched.get(), SDL_SRCCOLORKEY, src->format->colorkey);
		if (src->format->palette != NULL)
			SDL_SetPalette(stretched.get(), SDL_LOGPAL, src->format->palette->colors, 0, src->format->palette->ncolors);
		SDL_Rect stretched_rect = { 0, 0, dstRect->w, dstRect->h };
		if (SDL_SoftStretch(src, srcRect, stretched.get(), &stretched_rect) < 0
		    || SDL_BlitSurface(stretched.get(), &stretched_rect, dst, dstRect) < 0) {
			ErrSdl();
		}
		return;
	}

	// A surface with a non-output pixel format but without a color key needs scaling.
	// We can convert the format and then call BlitScaled.
	SDLSurfaceUniquePtr converted = SDLWrap::ConvertSurface(src, dst->format, 0);
	if (SDL_BlitScaled(converted.get(), srcRect, dst, dstRect) < 0)
		ErrSdl();
#endif
}

#ifndef USE_SDL1
namespace {

void UpdateOutputSurface()
{

	// Only do partial blitting if the output buffer is the same as last frame.
	const SDL_Surface *outputSurface = GetOutputSurface();
	const void *pixels = outputSurface != nullptr ? outputSurface->pixels : nullptr;
	const void *lastPixels = LastPresentedPixels;
	LastPresentedPixels = pixels;

	if (!ForceFullUpdate && !DirtyRects.empty() && pixels == lastPixels) {
		const int numRects = static_cast<int>(DirtyRects.size());
#ifdef USE_SDL3
		const bool updated = SDL_UpdateWindowSurfaceRects(ghMainWnd, DirtyRects.data(), numRects);
#else
		const bool updated = SDL_UpdateWindowSurfaceRects(ghMainWnd, DirtyRects.data(), numRects) >= 0;
#endif
		DirtyRects.clear();
		if (!updated) ErrSdl();
		return;
	}
	DirtyRects.clear();
	ForceFullUpdate = false;

#ifdef USE_SDL3
	if (!SDL_UpdateWindowSurface(ghMainWnd)) ErrSdl();
#else
	if (SDL_UpdateWindowSurface(ghMainWnd) <= -1) ErrSdl();
#endif
}

} // namespace
#endif

void RenderPresent()
{
	if (HeadlessMode)
		return;

	SDL_Surface *surface = GetOutputSurface();

	if (!gbActive) {
#ifdef __EMSCRIPTEN__
		// Just yield to browser when inactive instead of blocking
		emscripten_sleep(1);
#else
		LimitFrameRate();
#endif
		return;
	}

#ifndef USE_SDL1
	if (renderer != nullptr) {
#ifdef USE_SDL3
		if (!SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255)) ErrSdl();
		if (!SDL_RenderClear(renderer)) ErrSdl();
		if (!SDL_UpdateTexture(texture.get(), nullptr, surface->pixels, surface->pitch)) ErrSdl();
		if (!SDL_RenderTexture(renderer, texture.get(), nullptr, nullptr)) ErrSdl();
#else
		if (LayeredFramePending && WantsLayeredFrame()) {
			PresentLayeredFrame(surface);
		} else {
			if (LayeredFramePending) {
				// The effects were turned off since the frame was drawn: compose it the usual way.
				LayeredFramePending = false;
				ComposeScaledFrame();
				Blit(WorldSurface.get(), nullptr, nullptr);
			}
			if (SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) <= -1) ErrSdl();
			if (SDL_RenderClear(renderer) <= -1) ErrSdl();
			if (SDL_UpdateTexture(texture.get(), nullptr, surface->pixels, surface->pitch) <= -1) ErrSdl();
			if (SDL_RenderCopy(renderer, texture.get(), nullptr, nullptr) <= -1) ErrSdl();
		}
#endif

		if (ControlMode == ControlTypes::VirtualGamepad) {
			RenderVirtualGamepad(renderer);
		}
		SDL_RenderPresent(renderer);

#ifdef __EMSCRIPTEN__
		// TODO: Refactor to use emscripten_set_main_loop or requestAnimationFrame instead.
		// For now, yield to browser to allow rendering via ASYNCIFY sleep.
		emscripten_sleep(1);
#endif

		if (*GetOptions().Graphics.frameRateControl != FrameRateControl::VerticalSync) {
			LimitFrameRate();
		}
	} else {
		if (ControlMode == ControlTypes::VirtualGamepad) {
			RenderVirtualGamepad(surface);
		}

		UpdateOutputSurface();

		if (RenderDirectlyToOutputSurface)
			PalSurface = GetOutputSurface();
		LimitFrameRate();
	}
#else
	if (SDL_Flip(surface) <= -1) {
		ErrSdl();
	}
	if (RenderDirectlyToOutputSurface)
		PalSurface = GetOutputSurface();
	LimitFrameRate();
#endif
}

} // namespace devilution
