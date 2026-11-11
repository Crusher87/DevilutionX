#include "poe2/hd.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_render.h>
#else
#include <SDL.h>
#endif

#include "engine/assets.hpp"
#include "engine/dx.h"
#include "engine/render/clx_render.hpp"
#include "engine/surface.hpp"
#include "engine/trn.hpp"
#include "utils/png.h"
#include "utils/sdl_compat.h"

namespace devilution {

namespace hd {

#if !defined(USE_SDL1) && !defined(USE_SDL3)

namespace {

// The pictures of items in the inventory, the stash, the belt, the shops and on the cursor are drawn with the UI,
// which the HD world doesn't redraw. Each draw of one is noted here, with the pixels it lays down and the UI pixels it
// lays them over. When the frame is shown, the item's HD picture goes over the UI where the item's own pixels still
// show (a tooltip or the item on the cursor can be over it), and the UI under it shows what was there before it.

/** @brief An item picture's HD art name, and the size of the picture it was named from. */
struct UiSpriteName {
	std::string name;
	uint16_t width = 0;
	uint16_t height = 0;
	/** @brief For a shrunken copy of a picture (one inventory cell): the full picture's size; 0 for a picture of its own. */
	uint16_t fullWidth = 0;
	uint16_t fullHeight = 0;
};

/** @brief Each item picture frame, by the address of its pixels. */
std::unordered_map<const uint8_t *, UiSpriteName> UiSpriteNames;

/** @brief An item picture's HD art: the picture, and its textures as last made. */
struct UiArt {
	bool tried = false;
	SDL_Surface *picture = nullptr;
	SDL_Texture *texture = nullptr;
	/** @brief The picture in the red of an item the hero can't use. */
	SDL_Texture *red = nullptr;
};
std::unordered_map<std::string, UiArt> UiArts;
SDL_Renderer *UiArtRenderer = nullptr;

/** @brief The pixels a picture lays down: its palette index, or -1 where it lays none. */
struct UiPattern {
	uint16_t width = 0;
	uint16_t height = 0;
	std::vector<int16_t> pixels;
};
/** @brief Each picture's pattern, plain and red, by the address of its pixels (and 1 for red). */
/** @brief The patterns of item pictures drawn as they are [0] and in red [1], by their pixels. */
std::unordered_map<const uint8_t *, UiPattern> UiPatterns[2];

/** @brief One draw of an item picture in this frame's UI. */
struct UiDraw {
	const UiSpriteName *name = nullptr;
	const UiPattern *pattern = nullptr;
	bool red = false;
	/** @brief Where it is drawn in the UI. */
	SDL_Rect rect {};
	/** @brief The UI's pixels under it before it was drawn. */
	std::vector<uint8_t> under;
	/** @brief Where its own pixels still show when the frame is shown (1), and how many. */
	std::vector<uint8_t> shows;
	int shown = 0;
	bool whole = false;
	UiArt *art = nullptr;
};
std::vector<UiDraw> UiDraws;

/** @brief The UI pixels set to what was under an item picture while the UI overlay is made: where, and what they were. */
std::vector<std::pair<size_t, uint8_t>> Swapped;

/** @brief Textures of pictures partly covered, by their art, colour and what of them shows. */
std::unordered_map<uint64_t, SDL_Texture *> MaskedTextures;

const UiPattern &PatternOf(ClxSprite clx, const uint8_t *trn)
{
	UiPattern &pattern = UiPatterns[trn != nullptr ? 1 : 0][clx.pixelData()];
	if (pattern.width == clx.width() && pattern.height == clx.height() && !pattern.pixels.empty())
		return pattern;
	pattern.width = clx.width();
	pattern.height = clx.height();
	const int w = clx.width();
	const int h = clx.height();
	// Drawn once on 0 and once on 255: the pixels that come out the same are the ones it lays down.
	OwnedSurface low { w, h };
	OwnedSurface high { w, h };
	SDL_FillSurfaceRect(low.surface, nullptr, 0);
	SDL_FillSurfaceRect(high.surface, nullptr, 255);
	for (OwnedSurface *target : { &low, &high }) {
		if (trn != nullptr)
			ClxDrawTRN(*target, { 0, h }, clx, trn);
		else
			ClxDraw(*target, { 0, h }, clx);
	}
	pattern.pixels.assign(static_cast<size_t>(w) * h, -1);
	for (int y = 0; y < h; y++) {
		const auto *a = static_cast<const uint8_t *>(low.surface->pixels) + static_cast<ptrdiff_t>(y) * low.surface->pitch;
		const auto *b = static_cast<const uint8_t *>(high.surface->pixels) + static_cast<ptrdiff_t>(y) * high.surface->pitch;
		for (int x = 0; x < w; x++) {
			if (a[x] == b[x])
				pattern.pixels[static_cast<size_t>(y) * w + x] = a[x];
		}
	}
	return pattern;
}

UiArt *ArtFor(const std::string &name)
{
	UiArt &art = UiArts[name];
	if (!art.tried) {
		art.tried = true;
		std::string path = "hd\\" + name + ".png";
		std::replace(path.begin(), path.end(), '/', '\\');
		if (FindAsset(path).ok()) {
			if (SDL_Surface *loaded = LoadPNG(path.c_str()); loaded != nullptr) {
				art.picture = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
				SDL_FreeSurface(loaded);
			}
		}
	}
	return art.picture != nullptr ? &art : nullptr;
}

SDL_Texture *MakeTexture(SDL_Renderer *renderer, SDL_Surface *picture)
{
	SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, picture);
	if (texture != nullptr) {
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
		SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
#endif
	}
	return texture;
}

/** @brief Turns a picture (RGBA) into the red of an item the hero can't use, as the game's own red does: by brightness. */
void MakeRed(SDL_Surface *picture)
{
	for (int y = 0; y < picture->h; y++) {
		auto *row = static_cast<uint8_t *>(picture->pixels) + static_cast<ptrdiff_t>(y) * picture->pitch;
		for (int x = 0; x < picture->w; x++) {
			uint8_t *p = row + static_cast<ptrdiff_t>(x) * 4;
			const float bright = 0.299F * p[0] + 0.587F * p[1] + 0.114F * p[2];
			p[0] = static_cast<uint8_t>(std::min(255.F, bright * 1.35F));
			p[1] = static_cast<uint8_t>(bright * 0.18F);
			p[2] = static_cast<uint8_t>(bright * 0.18F);
		}
	}
}

/** @brief Where an item picture's HD art goes, in UI pixels: over the draw, or for a shrunken copy where its picture sits. */
SDL_FRect DestOf(const UiDraw &draw)
{
	SDL_FRect dest { static_cast<float>(draw.rect.x), static_cast<float>(draw.rect.y), static_cast<float>(draw.rect.w), static_cast<float>(draw.rect.h) };
	if (draw.name->fullWidth != 0) {
		// Halved until it fits, then centred in the cell, as the game makes the copy (cursor.cpp, SmallItemSprite).
		int w = draw.name->fullWidth;
		int h = draw.name->fullHeight;
		while (w > draw.rect.w || h > draw.rect.h) {
			w = std::max(w / 2, 1);
			h = std::max(h / 2, 1);
		}
		dest = SDL_FRect { dest.x + static_cast<float>((draw.rect.w - w) / 2), dest.y + static_cast<float>((draw.rect.h - h) / 2), static_cast<float>(w), static_cast<float>(h) };
	}
	return dest;
}

void ForgetTextures()
{
	for (auto &[name, art] : UiArts) {
		art.texture = nullptr;
		art.red = nullptr;
	}
	MaskedTextures.clear();
}

void RecordUiSpriteImpl(const Surface &out, Point position, ClxSprite clx, const uint8_t *trn)
{
	if (out.surface == nullptr || out.surface != PalSurface)
		return;
	const auto it = UiSpriteNames.find(clx.pixelData());
	if (it == UiSpriteNames.end() || it->second.width != clx.width() || it->second.height != clx.height())
		return;
	// Other colourings (a Book of Specialisation's gold binding) keep the game's own picture.
	if (trn != nullptr && trn != GetInfravisionTRN())
		return;
	if (!Enabled())
		return;
	if (UiDraws.size() >= 512)
		UiDraws.clear();
	UiDraw &draw = UiDraws.emplace_back();
	draw.name = &it->second;
	draw.pattern = &PatternOf(clx, trn);
	draw.red = trn != nullptr;
	const int w = clx.width();
	const int h = clx.height();
	draw.rect = SDL_Rect { out.region.x + position.x, out.region.y + position.y - h, w, h };
	// What it is drawn over; pixels outside the surface it is drawn on (clipped) lay nothing down.
	draw.under.assign(static_cast<size_t>(w) * h, 0);
	draw.shows.assign(static_cast<size_t>(w) * h, 0);
	const SDL_Surface *ui = PalSurface;
	for (int y = 0; y < h; y++) {
		const int uy = draw.rect.y + y;
		if (uy < 0 || uy >= ui->h || uy < out.region.y || uy >= out.region.y + out.region.h)
			continue;
		const auto *line = static_cast<const uint8_t *>(ui->pixels) + static_cast<ptrdiff_t>(uy) * ui->pitch;
		for (int x = 0; x < w; x++) {
			const int ux = draw.rect.x + x;
			if (ux < 0 || ux >= ui->w || ux < out.region.x || ux >= out.region.x + out.region.w)
				continue;
			draw.under[static_cast<size_t>(y) * w + x] = line[ux];
			draw.shows[static_cast<size_t>(y) * w + x] = 1; // inside the surface: may show
		}
	}
}

[[maybe_unused]] const bool RecordUiSpriteHookSet = (RecordUiSpriteHook = RecordUiSpriteImpl, true);

} // namespace

void RegisterUiSprite(const uint8_t *pixels, const std::string &name, uint16_t width, uint16_t height)
{
	UiSpriteNames[pixels] = UiSpriteName { name, width, height };
	// A picture loaded where another was has its own pattern.
	for (auto &patterns : UiPatterns)
		patterns.erase(pixels);
}

void RegisterFittedUiSprite(ClxSprite small, ClxSprite full)
{
	const auto it = UiSpriteNames.find(full.pixelData());
	if (it == UiSpriteNames.end() || it->second.fullWidth != 0)
		return;
	const UiSpriteName fitted { it->second.name, small.width(), small.height(), full.width(), full.height() };
	UiSpriteNames[small.pixelData()] = fitted;
	for (auto &patterns : UiPatterns)
		patterns.erase(small.pixelData());
}

void PrepareUiSprites()
{
	Swapped.clear();
	SDL_Surface *ui = PalSurface;
	if (ui == nullptr || UiDraws.empty())
		return;
	for (UiDraw &draw : UiDraws) {
		draw.art = ArtFor(draw.name->name);
		draw.shown = 0;
		draw.whole = false;
		if (draw.art == nullptr)
			continue;
		const int w = draw.rect.w;
		int laid = 0;
		for (int y = 0; y < draw.rect.h; y++) {
			const int uy = draw.rect.y + y;
			const bool rowInside = uy >= 0 && uy < ui->h;
			const auto *line = rowInside ? static_cast<const uint8_t *>(ui->pixels) + static_cast<ptrdiff_t>(uy) * ui->pitch : nullptr;
			for (int x = 0; x < w; x++) {
				const size_t i = static_cast<size_t>(y) * w + x;
				const int16_t own = draw.pattern->pixels[i];
				if (own < 0) {
					draw.shows[i] = 0;
					continue;
				}
				laid++;
				// Its own pixel still there: nothing has been drawn over it since.
				const bool inside = line != nullptr && draw.shows[i] != 0;
				draw.shows[i] = inside && line[draw.rect.x + x] == own ? 1 : 0;
				draw.shown += draw.shows[i];
			}
		}
		// A picture all but covered keeps the game's own few pixels.
		if (draw.shown * 10 < laid) {
			draw.shown = 0;
			continue;
		}
		draw.whole = draw.shown == laid;
		for (int y = 0; y < draw.rect.h; y++) {
			auto *line = static_cast<uint8_t *>(ui->pixels) + static_cast<ptrdiff_t>(draw.rect.y + y) * ui->pitch;
			for (int x = 0; x < w; x++) {
				const size_t i = static_cast<size_t>(y) * w + x;
				if (draw.shows[i] == 0)
					continue;
				const size_t at = static_cast<size_t>(draw.rect.y + y) * ui->pitch + draw.rect.x + x;
				Swapped.emplace_back(at, line[draw.rect.x + x]);
				line[draw.rect.x + x] = draw.under[i];
			}
		}
	}
}

void RestoreUiSprites()
{
	SDL_Surface *ui = PalSurface;
	if (ui != nullptr) {
		auto *pixels = static_cast<uint8_t *>(ui->pixels);
		for (auto it = Swapped.rbegin(); it != Swapped.rend(); ++it)
			pixels[it->first] = it->second;
	}
	Swapped.clear();
}

void RenderUiSprites(SDL_Renderer *renderer)
{
	if (renderer != UiArtRenderer) {
		// The old renderer took its textures with it.
		ForgetTextures();
		UiArtRenderer = renderer;
	}
	if (MaskedTextures.size() > 256) {
		for (auto &[key, texture] : MaskedTextures)
			SDL_DestroyTexture(texture);
		MaskedTextures.clear();
	}
	for (UiDraw &draw : UiDraws) {
		if (draw.shown == 0 || draw.art == nullptr || renderer == nullptr)
			continue;
		UiArt &art = *draw.art;
		const SDL_FRect dest = DestOf(draw);
		SDL_Texture *texture = nullptr;
		if (draw.whole) {
			SDL_Texture *&made = draw.red ? art.red : art.texture;
			if (made == nullptr) {
				if (draw.red) {
					SDL_Surface *red = SDL_DuplicateSurface(art.picture);
					MakeRed(red);
					made = MakeTexture(renderer, red);
					SDL_FreeSurface(red);
				} else {
					made = MakeTexture(renderer, art.picture);
				}
			}
			texture = made;
		} else {
			// Partly covered: a copy of the picture with what doesn't show left out.
			uint64_t key = std::hash<std::string> {}(draw.name->name) ^ (draw.red ? 0x9E3779B97F4A7C15ULL : 0) ^ (static_cast<uint64_t>(draw.rect.w) << 48) ^ (static_cast<uint64_t>(draw.rect.h) << 40);
			for (const uint8_t bit : draw.shows)
				key = key * 1099511628211ULL ^ bit;
			auto [it, inserted] = MaskedTextures.try_emplace(key, nullptr);
			if (inserted) {
				SDL_Surface *copy = SDL_DuplicateSurface(art.picture);
				if (draw.red)
					MakeRed(copy);
				const float stepX = dest.w / static_cast<float>(copy->w);
				const float stepY = dest.h / static_cast<float>(copy->h);
				for (int y = 0; y < copy->h; y++) {
					auto *row = static_cast<uint8_t *>(copy->pixels) + static_cast<ptrdiff_t>(y) * copy->pitch;
					const int uy = static_cast<int>(dest.y + (static_cast<float>(y) + 0.5F) * stepY) - draw.rect.y;
					for (int x = 0; x < copy->w; x++) {
						const int ux = static_cast<int>(dest.x + (static_cast<float>(x) + 0.5F) * stepX) - draw.rect.x;
						const bool shows = ux >= 0 && uy >= 0 && ux < draw.rect.w && uy < draw.rect.h && draw.shows[static_cast<size_t>(uy) * draw.rect.w + ux] != 0;
						if (!shows)
							row[static_cast<ptrdiff_t>(x) * 4 + 3] = 0;
					}
				}
				it->second = MakeTexture(renderer, copy);
				SDL_FreeSurface(copy);
			}
			texture = it->second;
		}
		if (texture != nullptr)
			SDL_RenderCopyF(renderer, texture, nullptr, &dest);
	}
	UiDraws.clear();
}

#else

void RegisterUiSprite(const uint8_t *, const std::string &, uint16_t, uint16_t) { }
void RegisterFittedUiSprite(ClxSprite, ClxSprite) { }
void PrepareUiSprites() { }
void RestoreUiSprites() { }
void RenderUiSprites(SDL_Renderer *) { }

#endif

} // namespace hd

} // namespace devilution
