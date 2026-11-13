#include "poe2/hd.hpp"

#include <algorithm>
#include <cmath>
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
#include "engine/palette.h"
#include "engine/render/clx_render.hpp"
#include "engine/surface.hpp"
#include "engine/trn.hpp"
#include "poe2/hd_outline.hpp"
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

/** @brief A smooth outline round an item picture's HD art, as shown at one size: its shape, and its texture as last made. */
struct UiOutlineArt {
	OutlineShape shape;
	SDL_Texture *texture = nullptr;
};

/** @brief An item picture's HD art: the picture, and its textures as last made. */
struct UiArt {
	bool tried = false;
	SDL_Surface *picture = nullptr;
	SDL_Texture *texture = nullptr;
	/** @brief The picture in the red of an item the hero can't use. */
	SDL_Texture *red = nullptr;
	/** @brief Its smooth outlines, made the first time each shows, by the size it is shown at and their cut. */
	std::unordered_map<uint64_t, UiOutlineArt> outlines;
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

/** @brief How far round an item picture its smooth outline (and glow) may reach, in UI pixels. */
constexpr int UiOutlineMargin = 7;

/**
 * @brief An outline the game drew round an item picture in the UI (ClxDrawOutline, just before the picture). Where
 * the picture shows its HD art, the outline shows smooth round that art, and the game's own is left out.
 */
struct UiOutline {
	/** @brief The picture it went round, and where it was drawn (UI pixels, as ClxDrawOutline takes it); null for none. */
	const uint8_t *pixels = nullptr;
	Point at;
	uint8_t colour = 0;
	OutlineLook look;
	/** @brief The pixels the game's outline lays down (RingOf). */
	const std::vector<Point> *ring = nullptr;
	/** @brief The UI round the picture, UiOutlineMargin past it and inside what it is drawn on, before the outline went on. */
	SDL_Rect area {};
	std::vector<uint8_t> under;
	/** @brief Where in the area the smooth outline may show (1): nothing but item pictures has been drawn there since. */
	std::vector<uint8_t> shows;
	bool whole = false;
	/** @brief The smooth outline shows this frame, in place of the game's. */
	bool smooth = false;
};

/** @brief The outline drawn last in the UI, until the picture it goes round is. */
UiOutline PendingUiOutline;

/** @brief The pixels the game's outline round each item picture lays down, from the picture's top left, by its pixels. */
std::unordered_map<const uint8_t *, std::vector<Point>> UiRings;

/** @brief How each outline in the UI was last drawn, so the one under the cursor fades in as the world's do. */
OutlineFader UiOutlines;

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
	/** @brief The outline the game drew round it, if it drew one. */
	UiOutline outline;
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

const std::vector<Point> &RingOf(ClxSprite clx)
{
	const auto [it, inserted] = UiRings.try_emplace(clx.pixelData());
	if (!inserted)
		return it->second;
	const int w = clx.width();
	const int h = clx.height();
	// Drawn with room round it: the pixels in the outline's colour are the ones it lays down.
	OwnedSurface surface { w + 4, h + 4 };
	SDL_FillSurfaceRect(surface.surface, nullptr, 0);
	ClxDrawOutline(surface, 1, { 2, h + 2 }, clx);
	for (int y = 0; y < surface.surface->h; y++) {
		const auto *line = static_cast<const uint8_t *>(surface.surface->pixels) + static_cast<ptrdiff_t>(y) * surface.surface->pitch;
		for (int x = 0; x < surface.surface->w; x++) {
			if (line[x] == 1)
				it->second.push_back({ x - 2, y - 2 });
		}
	}
	return it->second;
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
		for (auto &[key, outline] : art.outlines)
			outline.texture = nullptr;
	}
	MaskedTextures.clear();
}

void RecordUiOutlineImpl(const Surface &out, Point position, ClxSprite clx, uint8_t colour)
{
	if (out.surface == nullptr || out.surface != PalSurface)
		return;
	PendingUiOutline = {};
	const auto it = UiSpriteNames.find(clx.pixelData());
	if (it == UiSpriteNames.end() || it->second.width != clx.width() || it->second.height != clx.height())
		return;
	if (!Enabled())
		return;
	const SDL_Surface *ui = PalSurface;
	const int w = clx.width();
	const int h = clx.height();
	const Point at { out.region.x + position.x, out.region.y + position.y };
	const int left = std::max({ at.x - UiOutlineMargin, out.region.x, 0 });
	const int top = std::max({ at.y - h - UiOutlineMargin, out.region.y, 0 });
	const int right = std::min({ at.x + w + UiOutlineMargin, out.region.x + out.region.w, ui->w });
	const int bottom = std::min({ at.y + UiOutlineMargin, out.region.y + out.region.h, ui->h });
	if (right <= left || bottom <= top)
		return;
	const std::vector<Point> &ring = RingOf(clx);
	UiOutline &outline = PendingUiOutline;
	outline.pixels = clx.pixelData();
	outline.at = at;
	outline.colour = colour;
	outline.look = CurrentOutlineLook;
	outline.ring = &ring;
	outline.area = SDL_Rect { left, top, right - left, bottom - top };
	outline.under.resize(static_cast<size_t>(outline.area.w) * outline.area.h);
	for (int y = 0; y < outline.area.h; y++) {
		const auto *line = static_cast<const uint8_t *>(ui->pixels) + static_cast<ptrdiff_t>(top + y) * ui->pitch;
		std::copy(line + left, line + right, outline.under.begin() + static_cast<ptrdiff_t>(y) * outline.area.w);
	}
}

void RecordUiSpriteImpl(const Surface &out, Point position, ClxSprite clx, const uint8_t *trn)
{
	if (out.surface == nullptr || out.surface != PalSurface)
		return;
	// The outline the game drew round it just before, if it did; only the next picture can be the one it goes round.
	UiOutline outline = std::move(PendingUiOutline);
	PendingUiOutline = {};
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
	if (outline.pixels == clx.pixelData() && outline.at == Point { out.region.x + position.x, out.region.y + position.y })
		draw.outline = std::move(outline);
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
[[maybe_unused]] const bool RecordUiOutlineHookSet = (RecordUiOutlineHook = RecordUiOutlineImpl, true);

/**
 * @brief Before any item picture is taken out of the UI: where the outline round a draw may show. That is where the UI
 * is still as the outline and its picture left it, or shows a later picture whose HD art goes over it there; anything
 * else drawn over it since (a tooltip, the cursor) hides it.
 */
void FindWhereOutlineShows(size_t index)
{
	UiDraw &draw = UiDraws[index];
	UiOutline &outline = draw.outline;
	const SDL_Rect &area = outline.area;
	const SDL_Surface *ui = PalSurface;
	const auto inArea = [&](int ux, int uy) { return ux >= area.x && uy >= area.y && ux < area.x + area.w && uy < area.y + area.h; };
	// What the outline and its picture left there.
	std::vector<int16_t> left(outline.under.begin(), outline.under.end());
	for (const Point &p : *outline.ring) {
		const int ux = draw.rect.x + p.x;
		const int uy = draw.rect.y + p.y;
		if (inArea(ux, uy))
			left[static_cast<size_t>(uy - area.y) * area.w + (ux - area.x)] = outline.colour;
	}
	for (int y = 0; y < draw.rect.h; y++) {
		for (int x = 0; x < draw.rect.w; x++) {
			const int16_t own = draw.pattern->pixels[static_cast<size_t>(y) * draw.rect.w + x];
			if (own >= 0 && inArea(draw.rect.x + x, draw.rect.y + y))
				left[static_cast<size_t>(draw.rect.y + y - area.y) * area.w + (draw.rect.x + x - area.x)] = own;
		}
	}
	std::vector<const UiDraw *> later;
	for (size_t i = index + 1; i < UiDraws.size(); i++) {
		const UiDraw &other = UiDraws[i];
		if (other.art != nullptr && other.rect.x < area.x + area.w && other.rect.y < area.y + area.h && other.rect.x + other.rect.w > area.x && other.rect.y + other.rect.h > area.y)
			later.push_back(&other);
	}
	outline.shows.assign(left.size(), 0);
	size_t shown = 0;
	for (int y = 0; y < area.h; y++) {
		const int uy = area.y + y;
		const auto *line = static_cast<const uint8_t *>(ui->pixels) + static_cast<ptrdiff_t>(uy) * ui->pitch;
		for (int x = 0; x < area.w; x++) {
			const int ux = area.x + x;
			const size_t i = static_cast<size_t>(y) * area.w + x;
			bool shows = line[ux] == left[i];
			for (const UiDraw *other : later) {
				if (shows)
					break;
				const int ox = ux - other->rect.x;
				const int oy = uy - other->rect.y;
				if (ox < 0 || oy < 0 || ox >= other->rect.w || oy >= other->rect.h)
					continue;
				const size_t o = static_cast<size_t>(oy) * other->rect.w + ox;
				shows = other->shows[o] != 0 && other->pattern->pixels[o] == line[ux];
			}
			outline.shows[i] = shows ? 1 : 0;
			shown += shows ? 1 : 0;
		}
	}
	outline.whole = shown == left.size();
}

/** @brief Takes the game's own outline round a draw out of the UI, where it still shows: its smooth one goes there. */
void HideGameOutline(UiDraw &draw)
{
	UiOutline &outline = draw.outline;
	const SDL_Rect &area = outline.area;
	SDL_Surface *ui = PalSurface;
	auto *pixels = static_cast<uint8_t *>(ui->pixels);
	for (const Point &p : *outline.ring) {
		const int ux = draw.rect.x + p.x;
		const int uy = draw.rect.y + p.y;
		if (ux < area.x || uy < area.y || ux >= area.x + area.w || uy >= area.y + area.h)
			continue;
		const size_t at = static_cast<size_t>(uy) * ui->pitch + ux;
		if (pixels[at] != outline.colour)
			continue;
		Swapped.emplace_back(at, pixels[at]);
		pixels[at] = outline.under[static_cast<size_t>(uy - area.y) * area.w + (ux - area.x)];
	}
	outline.smooth = true;
}

Uint8 UiColourByte(float value)
{
	return static_cast<Uint8>(std::clamp(value, 0.F, 1.F) * 255.F + 0.5F);
}

/** @brief A texture of an outline's shape (white, its shape in the alpha); a glow adds light to what is under it. */
SDL_Texture *OutlineTexture(SDL_Renderer *renderer, const OutlineShape &shape, const std::vector<uint8_t> &alpha, OutlineCut cut)
{
	std::vector<uint8_t> rgba(alpha.size() * 4, 255);
	for (size_t i = 0; i < alpha.size(); i++)
		rgba[i * 4 + 3] = alpha[i];
	SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, shape.width, shape.height);
	if (texture == nullptr)
		return nullptr;
	SDL_SetTextureBlendMode(texture, cut == OutlineCut::Glow ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
	SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
#endif
	SDL_UpdateTexture(texture, nullptr, rgba.data(), shape.width * 4);
	return texture;
}

/** @brief The smooth outline round an item picture's HD art shown at `dest`'s size, made the first time it shows. */
UiOutlineArt *OutlineArtFor(SDL_Renderer *renderer, UiArt &art, const SDL_FRect &dest, OutlineCut cut)
{
	const auto w = static_cast<uint64_t>(dest.w);
	const auto h = static_cast<uint64_t>(dest.h);
	const auto [it, inserted] = art.outlines.try_emplace(w | (h << 16) | (static_cast<uint64_t>(cut) << 32));
	UiOutlineArt &made = it->second;
	if (inserted && dest.w > 0 && dest.h > 0) {
		// Round the picture's edge, a UI pixel at a time, as the world's go round their art a game pixel at a time.
		const SDL_Surface *picture = art.picture;
		std::vector<uint8_t> body(static_cast<size_t>(picture->w) * picture->h);
		std::vector<uint8_t> cover(body.size());
		for (int y = 0; y < picture->h; y++) {
			const auto *row = static_cast<const uint8_t *>(picture->pixels) + static_cast<ptrdiff_t>(y) * picture->pitch;
			for (int x = 0; x < picture->w; x++) {
				const uint8_t alpha = row[static_cast<ptrdiff_t>(x) * 4 + 3];
				cover[static_cast<size_t>(y) * picture->w + x] = alpha;
				body[static_cast<size_t>(y) * picture->w + x] = alpha >= 128 ? 1 : 0;
			}
		}
		made.shape = MakeOutlineShape(body, cover, picture->w, picture->h, static_cast<float>(picture->w) / dest.w, static_cast<float>(picture->h) / dest.h, cut);
	}
	if (made.shape.empty())
		return nullptr;
	if (made.texture == nullptr)
		made.texture = OutlineTexture(renderer, made.shape, made.shape.alpha, cut);
	return made.texture != nullptr ? &made : nullptr;
}

/** @brief A copy of a smooth outline with what doesn't show (UiOutline::shows) left out. */
SDL_Texture *MaskedOutline(SDL_Renderer *renderer, const UiDraw &draw, const SDL_FRect &dest, const UiOutlineArt &made, OutlineCut cut)
{
	const UiOutline &outline = draw.outline;
	const SDL_Rect &area = outline.area;
	uint64_t key = std::hash<std::string> {}(draw.name->name) ^ 0x5851F42D4C957F2DULL ^ (static_cast<uint64_t>(cut) << 56)
	    ^ (static_cast<uint64_t>(dest.w) << 44) ^ (static_cast<uint64_t>(dest.h) << 32)
	    ^ (static_cast<uint64_t>(static_cast<uint32_t>(area.x - draw.rect.x) & 0xFF) << 24) ^ (static_cast<uint64_t>(static_cast<uint32_t>(area.y - draw.rect.y) & 0xFF) << 16)
	    ^ (static_cast<uint64_t>(static_cast<uint32_t>(area.w) & 0xFF) << 8) ^ static_cast<uint64_t>(static_cast<uint32_t>(area.h) & 0xFF);
	for (const uint8_t bit : outline.shows)
		key = key * 1099511628211ULL ^ bit;
	auto [it, inserted] = MaskedTextures.try_emplace(key, nullptr);
	if (!inserted)
		return it->second;
	const OutlineShape &shape = made.shape;
	std::vector<uint8_t> alpha = shape.alpha;
	const float stepX = shape.w / static_cast<float>(shape.width);
	const float stepY = shape.h / static_cast<float>(shape.height);
	for (int y = 0; y < shape.height; y++) {
		const int uy = static_cast<int>(std::floor(dest.y + shape.y + (static_cast<float>(y) + 0.5F) * stepY)) - area.y;
		for (int x = 0; x < shape.width; x++) {
			const int ux = static_cast<int>(std::floor(dest.x + shape.x + (static_cast<float>(x) + 0.5F) * stepX)) - area.x;
			const bool shows = ux >= 0 && uy >= 0 && ux < area.w && uy < area.h && outline.shows[static_cast<size_t>(uy) * area.w + ux] != 0;
			if (!shows)
				alpha[static_cast<size_t>(y) * shape.width + x] = 0;
		}
	}
	it->second = OutlineTexture(renderer, shape, alpha, cut);
	return it->second;
}

/** @brief A number for an outline in the UI that only its picture at its place has, for how it fades in. */
uint32_t UiOutlineThing(const UiDraw &draw)
{
	uint64_t key = std::hash<std::string> {}(draw.name->name);
	key = key * 1099511628211ULL ^ static_cast<uint64_t>(static_cast<uint32_t>(draw.rect.x));
	key = key * 1099511628211ULL ^ static_cast<uint64_t>(static_cast<uint32_t>(draw.rect.y));
	const auto thing = static_cast<uint32_t>(key ^ (key >> 32));
	return thing != 0 ? thing : 1;
}

/**
 * @brief Draws the outline the game drew round an item picture smooth, round its HD art, in the outline's colour, as
 * the world's outlines are: fading in under the cursor, with a slow glow under it round uniques and quest items.
 */
void DrawUiOutline(SDL_Renderer *renderer, const UiDraw &draw, const SDL_FRect &dest)
{
	const UiOutline &outline = draw.outline;
	OutlineLook look = outline.look;
	if (look.thing == 0)
		look.thing = UiOutlineThing(draw);
	const SDL_Color &gameColour = logical_palette[outline.colour];
	OutlineColour colour { gameColour.r / 255.F, gameColour.g / 255.F, gameColour.b / 255.F };
	float alpha = 1.F;
	const uint32_t now = SDL_GetTicks();
	UiOutlines.Fade(look, now, colour, alpha);
	const auto drawCut = [&](OutlineCut cut, float strength) {
		if (strength <= 0)
			return;
		const UiOutlineArt *made = OutlineArtFor(renderer, *draw.art, dest, cut);
		if (made == nullptr)
			return;
		SDL_Texture *texture = outline.whole ? made->texture : MaskedOutline(renderer, draw, dest, *made, cut);
		if (texture == nullptr)
			return;
		const SDL_FRect at { dest.x + made->shape.x, dest.y + made->shape.y, made->shape.w, made->shape.h };
		SDL_SetTextureColorMod(texture, UiColourByte(colour[0]), UiColourByte(colour[1]), UiColourByte(colour[2]));
		SDL_SetTextureAlphaMod(texture, UiColourByte(strength));
		SDL_RenderCopyF(renderer, texture, nullptr, &at);
	};
	if (look.glow)
		drawCut(OutlineCut::Glow, alpha * GlowStrength(now));
	drawCut(OutlineCutFor(look), alpha);
}

} // namespace

void RegisterUiSprite(const uint8_t *pixels, const std::string &name, uint16_t width, uint16_t height)
{
	UiSpriteNames[pixels] = UiSpriteName { name, width, height };
	// A picture loaded where another was has its own pattern.
	for (auto &patterns : UiPatterns)
		patterns.erase(pixels);
	UiRings.erase(pixels);
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
	UiRings.erase(small.pixelData());
}

void PrepareUiSprites()
{
	Swapped.clear();
	SDL_Surface *ui = PalSurface;
	if (ui == nullptr || UiDraws.empty())
		return;
	for (UiDraw &draw : UiDraws) {
		draw.art = ArtFor(draw.name->name);
		draw.outline.smooth = false;
	}
	for (size_t i = 0; i < UiDraws.size(); i++) {
		if (UiDraws[i].art != nullptr && UiDraws[i].outline.pixels != nullptr)
			FindWhereOutlineShows(i);
	}
	for (UiDraw &draw : UiDraws) {
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
	// Last, so an outline a later picture was drawn over (and that picture taken out again) goes too.
	for (UiDraw &draw : UiDraws) {
		if (draw.shown != 0 && draw.art != nullptr && draw.outline.pixels != nullptr)
			HideGameOutline(draw);
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
		// Over the picture, as the world's outlines go over their sprites: it leaves out what the picture covers.
		if (draw.outline.smooth)
			DrawUiOutline(renderer, draw, dest);
	}
	UiDraws.clear();
	PendingUiOutline = {};
	UiOutlines.Prune(SDL_GetTicks());
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
