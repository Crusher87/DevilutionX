#include "poe2/hd.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <map>
#include <string>
#include <string_view>
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
#include "poe2/hd_spell_icons.hpp"
#include "utils/file_util.h"
#include "utils/log.hpp"
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
	/** @brief A UI panel's picture (RegisterUiPicture): its HD art goes under the UI, which leaves out its pixels there. */
	bool panel = false;
	/** @brief A spell icon: its HD art is put together from a plate and a layer, and shrunk to its size on screen. */
	bool spellIcon = false;
};

/** @brief Each item picture frame, by the address of its pixels. */
std::unordered_map<const uint8_t *, UiSpriteName> UiSpriteNames;

// A font's letters (RegisterUiLetters) show their HD art the same way, over the UI, but only in the colours they were
// named for: the same letter drawn with another colour table keeps the game's own.

/** @brief A letter frame drawn in one colour: the address of its pixels and of its colour table (0 for none). */
using UiLetterKey = std::pair<uintptr_t, uintptr_t>;

UiLetterKey LetterKey(const uint8_t *pixels, const uint8_t *trn)
{
	return { reinterpret_cast<uintptr_t>(pixels), reinterpret_cast<uintptr_t>(trn) };
}

/** @brief Each letter's HD art name, by its frame and colour (std::map, so a draw's pointer to one stays good). */
std::map<UiLetterKey, UiSpriteName> UiLetterNames;

/** @brief A smooth outline round an item picture's HD art, as shown at one size: its shape, and its texture as last made. */
struct UiOutlineArt {
	OutlineShape shape;
	SDL_Texture *texture = nullptr;
};

/** @brief A spell icon's HD art shrunk to one size on screen, and its texture as last made. */
struct UiFittedArt {
	SDL_Surface *picture = nullptr;
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
	/** @brief A spell icon's picture at each size it has been shown at on screen (FittedArtOf), by width and height. */
	std::unordered_map<uint32_t, UiFittedArt> fitted;
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
	/** @brief The pattern of a picture drawn in code (a rune), which only this draw has; `pattern` points to it. */
	std::shared_ptr<const UiPattern> ownPattern;
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
	/** @brief When in the frame it was drawn, among the draws and the copies (UiBlit), so panels go under in that order. */
	uint32_t order = 0;
};
std::vector<UiDraw> UiDraws;
uint32_t UiOrder = 0;

/** @brief The UI pixels set to what was under an item picture while the UI overlay is made: where, and what they were. */
std::vector<std::pair<size_t, uint8_t>> Swapped;

/** @brief Textures of pictures partly covered, by their art, colour and what of them shows. */
std::unordered_map<uint64_t, SDL_Texture *> MaskedTextures;

// UI panels (the inventory, the spell book, the bottom panel...) show their HD art the same way, but under the UI: the UI
// leaves out their pixels that still show (UiHiddenPixels), so whatever was drawn over them (text, items, the cursor)
// stays over their art.

/** @brief Each UI panel picture's frames, by the address of their pixels, by its name (RegisterUiPicture). */
std::unordered_map<std::string, std::vector<const uint8_t *>> UiPictures;

/** @brief One copy of part of a surface a panel was put together on (the bottom panel) into this frame's UI. */
struct UiBlit {
	/** @brief The HD art of the whole surface. */
	std::string name;
	/** @brief The part copied, in the surface, and the surface's width (its art is that many times smaller). */
	SDL_Rect src {};
	int sourceWidth = 0;
	/** @brief Where it went in the UI. */
	SDL_Rect dest {};
	/** @brief The pixels it copied. */
	std::vector<uint8_t> pixels;
	int shown = 0;
	UiArt *art = nullptr;
	uint32_t order = 0;
};
std::vector<UiBlit> UiBlits;

/** @brief The UI pixels the overlay leaves out this frame (1), since a panel's HD art goes under them; and its size. */
std::vector<uint8_t> UiHidden;
int UiHiddenWidth = 0;
int UiHiddenHeight = 0;
bool UiHiddenAny = false;

/** @brief Where `--hd-export` writes the UI pictures (empty when it isn't), and what it wrote: frame sizes by name. */
std::string UiExportDir;
std::map<std::string, std::vector<std::pair<int, int>>> UiExported;

/** @brief Draws a picture on two colours to find the pixels it lays down: those that come out the same. */
void MakePattern(UiPattern &pattern, ClxSprite clx, const uint8_t *trn)
{
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
}

const UiPattern &PatternOf(ClxSprite clx, const uint8_t *trn)
{
	UiPattern &pattern = UiPatterns[trn != nullptr ? 1 : 0][clx.pixelData()];
	if (pattern.width == clx.width() && pattern.height == clx.height() && !pattern.pixels.empty())
		return pattern;
	MakePattern(pattern, clx, trn);
	return pattern;
}

/** @brief The pixels a letter lays down in its colour, by its frame and colour. */
std::map<UiLetterKey, UiPattern> UiLetterPatterns;

const UiPattern &LetterPatternOf(ClxSprite clx, const uint8_t *trn)
{
	UiPattern &pattern = UiLetterPatterns[LetterKey(clx.pixelData(), trn)];
	if (pattern.width == clx.width() && pattern.height == clx.height() && !pattern.pixels.empty())
		return pattern;
	MakePattern(pattern, clx, trn);
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
		for (auto &[size, fitted] : art.fitted)
			fitted.texture = nullptr;
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
	const UiSpriteName *name = nullptr;
	bool letter = false;
	if (const auto it = UiSpriteNames.find(clx.pixelData()); it != UiSpriteNames.end()) {
		// Other colourings (a Book of Specialisation's gold binding) keep the game's own picture.
		if (trn != nullptr && trn != GetInfravisionTRN())
			return;
		name = &it->second;
	} else if (const auto found = UiLetterNames.find(LetterKey(clx.pixelData(), trn)); found != UiLetterNames.end()) {
		// A letter in a colour it has art for; it has no outline of its own to smooth.
		name = &found->second;
		letter = true;
		outline = {};
	}
	if (name == nullptr || name->width != clx.width() || name->height != clx.height())
		return;
	if (!Enabled())
		return;
	if (UiDraws.size() >= 512)
		UiDraws.clear();
	UiDraw &draw = UiDraws.emplace_back();
	draw.order = UiOrder++;
	draw.name = name;
	draw.pattern = letter ? &LetterPatternOf(clx, trn) : &PatternOf(clx, trn);
	draw.red = !letter && trn != nullptr;
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

/** @brief A UI picture's HD art name from its asset's path: lower case, with / and no extension (data/inv/inv). */
std::string UiPictureName(std::string_view path)
{
	std::string name(path);
	std::replace(name.begin(), name.end(), '\\', '/');
	std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	for (const std::string_view ext : { ".cl2", ".clx", ".cel" }) {
		if (name.size() > ext.size() && name.ends_with(ext))
			name.resize(name.size() - ext.size());
	}
	return name;
}

/** @brief Writes one frame of a UI picture for `--hd-export`: `colourAt` gives each pixel's palette colour, or -1 for none. */
template <typename ColourAt>
void WriteUiFrame(const std::string &name, size_t frame, int width, int height, const ColourAt &colourAt)
{
	const std::string dir = UiExportDir + "/ui/" + name;
	RecursivelyCreateDir(dir.c_str());
	SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
	if (surface == nullptr)
		return;
	for (int y = 0; y < height; y++) {
		auto *row = static_cast<uint8_t *>(surface->pixels) + static_cast<ptrdiff_t>(y) * surface->pitch;
		for (int x = 0; x < width; x++) {
			uint8_t *p = row + static_cast<ptrdiff_t>(x) * 4;
			const int colour = colourAt(x, y);
			if (colour < 0) {
				p[0] = p[1] = p[2] = p[3] = 0;
				continue;
			}
			const SDL_Color &c = logical_palette[colour];
			p[0] = c.r;
			p[1] = c.g;
			p[2] = c.b;
			p[3] = 255;
		}
	}
	char file[32];
	std::snprintf(file, sizeof(file), "/d0_f%02zu.png", frame);
	if (IMG_SavePNG(surface, (dir + file).c_str()) != 0)
		LogError("HD export: couldn't write {}{}", dir, file);
	SDL_FreeSurface(surface);
	std::vector<std::pair<int, int>> &sizes = UiExported[name];
	if (sizes.size() <= frame)
		sizes.resize(frame + 1);
	sizes[frame] = { width, height };
}

/** @brief Forgets the frames at these pixels, and this frame's draws (which may point at their names). */
void ForgetUiFrames(const std::vector<const uint8_t *> &frames)
{
	UiDraws.clear();
	PendingUiOutline = {};
	for (const uint8_t *pixels : frames) {
		const auto it = UiSpriteNames.find(pixels);
		if (it != UiSpriteNames.end() && it->second.panel)
			UiSpriteNames.erase(it);
		for (auto &patterns : UiPatterns)
			patterns.erase(pixels);
		UiRings.erase(pixels);
	}
}

void RegisterUiPictureImpl(std::string_view path, ClxSpriteList sprites)
{
	const std::string name = UiPictureName(path);
	std::vector<const uint8_t *> &frames = UiPictures[name];
	// A picture loaded again under its name: the old one's pixels are freed, or soon will be.
	if (!frames.empty())
		ForgetUiFrames(frames);
	frames.clear();
	for (size_t frame = 0; frame < sprites.numSprites(); frame++) {
		const ClxSprite sprite = sprites[frame];
		char suffix[32];
		std::snprintf(suffix, sizeof(suffix), "/d0_f%02zu", frame);
		RegisterUiSprite(sprite.pixelData(), name + suffix, sprite.width(), sprite.height());
		UiSpriteNames[sprite.pixelData()].panel = true;
		frames.push_back(sprite.pixelData());
		if (!UiExportDir.empty()) {
			UiPattern pattern;
			MakePattern(pattern, sprite, nullptr);
			WriteUiFrame(name, frame, pattern.width, pattern.height, [&](int x, int y) {
				return static_cast<int>(pattern.pixels[static_cast<size_t>(y) * pattern.width + x]);
			});
		}
	}
}

void RegisterUiLettersImpl(ClxSpriteList sprites, const uint8_t *trn, std::string_view name)
{
	for (size_t frame = 0; frame < sprites.numSprites(); frame++) {
		const ClxSprite sprite = sprites[frame];
		if (sprite.width() == 0 || sprite.height() == 0)
			continue;
		char suffix[32];
		std::snprintf(suffix, sizeof(suffix), "/d0_f%02zu", frame);
		const UiLetterKey key = LetterKey(sprite.pixelData(), trn);
		UiLetterNames[key] = UiSpriteName { std::string(name) + suffix, sprite.width(), sprite.height() };
		UiLetterPatterns.erase(key);
	}
}

/** @brief Forgets the letters of a font whose pixels are about to be freed, in every colour. */
void ForgetUiLetters(ClxSpriteList sprites)
{
	bool any = false;
	for (size_t frame = 0; frame < sprites.numSprites(); frame++) {
		const uintptr_t pixels = reinterpret_cast<uintptr_t>(sprites[frame].pixelData());
		const auto from = UiLetterKey { pixels, 0 };
		for (auto it = UiLetterNames.lower_bound(from); it != UiLetterNames.end() && it->first.first == pixels;) {
			// This frame's draws may point at its name.
			if (!any) {
				UiDraws.clear();
				any = true;
			}
			it = UiLetterNames.erase(it);
		}
		for (auto it = UiLetterPatterns.lower_bound(from); it != UiLetterPatterns.end() && it->first.first == pixels;)
			it = UiLetterPatterns.erase(it);
	}
}

void ForgetUiPictureImpl(ClxSpriteList sprites)
{
	if (sprites.numSprites() == 0)
		return;
	ForgetUiLetters(sprites);
	const uint8_t *first = sprites[0].pixelData();
	for (auto it = UiPictures.begin(); it != UiPictures.end(); ++it) {
		if (!it->second.empty() && it->second.front() == first) {
			ForgetUiFrames(it->second);
			UiPictures.erase(it);
			return;
		}
	}
}

void RecordUiBlitImpl(const Surface &out, const Surface &src, Rectangle srcRect, Point targetPosition, std::string_view name)
{
	if (out.surface == nullptr || out.surface != PalSurface || src.surface == nullptr)
		return;
	if (!Enabled())
		return;
	// Clipped as the copy is (Surface::BlitFrom).
	SDL_Rect rect { srcRect.position.x, srcRect.position.y, srcRect.size.width, srcRect.size.height };
	Point at = targetPosition;
	out.Clip(&rect, &at);
	if (rect.w <= 0 || rect.h <= 0 || rect.x < 0 || rect.y < 0 || rect.x + rect.w > src.w() || rect.y + rect.h > src.h())
		return;
	if (UiBlits.size() >= 256)
		UiBlits.clear();
	UiBlit &blit = UiBlits.emplace_back();
	blit.order = UiOrder++;
	blit.name = std::string(name) + "/d0_f00";
	blit.src = rect;
	blit.sourceWidth = src.w();
	blit.dest = SDL_Rect { out.region.x + at.x, out.region.y + at.y, rect.w, rect.h };
	// A copy: the surface can change, or go, before the frame is shown.
	blit.pixels.resize(static_cast<size_t>(rect.w) * rect.h);
	for (int y = 0; y < rect.h; y++) {
		const uint8_t *line = src.at(rect.x, rect.y + y);
		std::copy(line, line + rect.w, blit.pixels.begin() + static_cast<ptrdiff_t>(y) * rect.w);
	}
}

[[maybe_unused]] const bool RegisterUiPictureHookSet = (RegisterUiPictureHook = RegisterUiPictureImpl, true);
[[maybe_unused]] const bool ForgetUiPictureHookSet = (ForgetUiPictureHook = ForgetUiPictureImpl, true);
[[maybe_unused]] const bool RecordUiBlitHookSet = (RecordUiBlitHook = RecordUiBlitImpl, true);
[[maybe_unused]] const bool RegisterUiLettersHookSet = (RegisterUiLettersHook = RegisterUiLettersImpl, true);

/** @brief Leaves a UI pixel out of the overlay this frame. */
void HideUiPixel(int ux, int uy)
{
	UiHidden[static_cast<size_t>(uy) * UiHiddenWidth + ux] = 1;
	UiHiddenAny = true;
}

/**
 * @brief Where a UI panel's picture still shows (its own pixels still there), leaves the UI out, as its HD art goes
 * under it there. Item pictures with HD art over it have already been taken out, so the panel shows under them.
 */
void PrepareUiPanel(UiDraw &draw, const SDL_Surface *ui)
{
	draw.shown = 0;
	draw.whole = false;
	const int w = draw.rect.w;
	int laid = 0;
	for (int y = 0; y < draw.rect.h; y++) {
		const int uy = draw.rect.y + y;
		const auto *line = uy >= 0 && uy < ui->h ? static_cast<const uint8_t *>(ui->pixels) + static_cast<ptrdiff_t>(uy) * ui->pitch : nullptr;
		for (int x = 0; x < w; x++) {
			const size_t i = static_cast<size_t>(y) * w + x;
			const int16_t own = draw.pattern->pixels[i];
			if (own < 0) {
				draw.shows[i] = 0;
				continue;
			}
			laid++;
			draw.shows[i] = line != nullptr && draw.shows[i] != 0 && line[draw.rect.x + x] == own ? 1 : 0;
			draw.shown += draw.shows[i];
		}
	}
	// A panel all but covered keeps the game's own few pixels.
	if (draw.shown * 10 < laid) {
		draw.shown = 0;
		return;
	}
	for (int y = 0; y < draw.rect.h; y++) {
		for (int x = 0; x < w; x++) {
			if (draw.shows[static_cast<size_t>(y) * w + x] != 0)
				HideUiPixel(draw.rect.x + x, draw.rect.y + y);
		}
	}
}

/** @brief As PrepareUiPanel, for a copy of part of a panel's surface. Its colour 0 stays: the art has none there. */
void PrepareUiBlit(UiBlit &blit, const SDL_Surface *ui)
{
	blit.shown = 0;
	blit.art = ArtFor(blit.name);
	if (blit.art == nullptr || blit.sourceWidth <= 0)
		return;
	int laid = 0;
	for (int y = 0; y < blit.dest.h; y++) {
		const int uy = blit.dest.y + y;
		if (uy < 0 || uy >= ui->h)
			continue;
		const auto *line = static_cast<const uint8_t *>(ui->pixels) + static_cast<ptrdiff_t>(uy) * ui->pitch;
		for (int x = 0; x < blit.dest.w; x++) {
			const int ux = blit.dest.x + x;
			const uint8_t own = blit.pixels[static_cast<size_t>(y) * blit.dest.w + x];
			if (ux < 0 || ux >= ui->w || own == 0)
				continue;
			laid++;
			if (line[ux] != own)
				continue;
			HideUiPixel(ux, uy);
			blit.shown++;
		}
	}
	if (laid == 0)
		blit.shown = 0;
}

/** @brief The renderer the textures are made for: a new one took the old one's textures with it. */
void UseRenderer(SDL_Renderer *renderer)
{
	if (renderer != UiArtRenderer) {
		ForgetTextures();
		UiArtRenderer = renderer;
	}
}

/** @brief An HD art's whole picture as a texture, made the first time it shows. */
SDL_Texture *TextureOf(SDL_Renderer *renderer, UiArt &art)
{
	if (art.texture == nullptr)
		art.texture = MakeTexture(renderer, art.picture);
	return art.texture;
}

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
		if (other.art != nullptr && !other.name->panel && other.rect.x < area.x + area.w && other.rect.y < area.y + area.h && other.rect.x + other.rect.w > area.x && other.rect.y + other.rect.h > area.y)
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

/** @brief The names of pictures drawn in code (runes), by their HD art's name and the size of the box they are drawn in. */
std::unordered_map<std::string, UiSpriteName> UiPictureNames;

void RecordUiPictureImpl(const Surface &out, const Rectangle &box, const std::string &name, const uint8_t *before)
{
	if (out.surface == nullptr || out.surface != PalSurface || before == nullptr)
		return;
	const int w = box.size.width;
	const int h = box.size.height;
	if (w <= 0 || h <= 0 || w > UINT16_MAX || h > UINT16_MAX)
		return;
	if (!Enabled())
		return;
	const auto [named, inserted] = UiPictureNames.try_emplace(name + '@' + std::to_string(w) + 'x' + std::to_string(h));
	if (inserted)
		named->second = UiSpriteName { name, static_cast<uint16_t>(w), static_cast<uint16_t>(h) };
	if (UiDraws.size() >= 512)
		UiDraws.clear();
	UiDraw &draw = UiDraws.emplace_back();
	draw.name = &named->second;
	auto pattern = std::make_shared<UiPattern>();
	pattern->width = static_cast<uint16_t>(w);
	pattern->height = static_cast<uint16_t>(h);
	pattern->pixels.assign(static_cast<size_t>(w) * h, -1);
	draw.rect = SDL_Rect { out.region.x + box.position.x, out.region.y + box.position.y, w, h };
	// What it was drawn over, and the pixels it changed: those are the ones it lays down. Pixels outside the surface it
	// is drawn on (clipped) lay nothing down.
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
			const size_t i = static_cast<size_t>(y) * w + x;
			draw.under[i] = before[i];
			draw.shows[i] = 1; // inside the surface: may show
			if (line[ux] != before[i])
				pattern->pixels[i] = line[ux];
		}
	}
	draw.pattern = pattern.get();
	draw.ownPattern = std::move(pattern);
}

[[maybe_unused]] const bool RecordUiPictureHookSet = (RecordUiPictureHook = RecordUiPictureImpl, true);

// Spell icons (the skill bar, the spell book, the spell list...) show painted pictures the same way, over the UI where
// the icon's own pixels still show. An icon's picture is its stone plate, in the colours the icon is drawn in, with the
// icon's symbol laid over it. The art is far bigger than an icon is on screen, so the picture is shrunk once to the
// exact screen size (an area filter) and then drawn pixel for pixel, rather than scaled down by the graphics card every
// frame, which would skip most of its pixels.

/** @brief The number of colourings SetSpellTrans has (SpellType). */
constexpr size_t NumSpellIconTints = 5;

/** @brief Each icon's name, by its colours, its size and its frame: null until asked for, then its name or NoSpellIcon. */
const UiSpriteName *SpellIconNames[NumSpellIconTints][2][SpellIconLastFrame + 1];
const UiSpriteName NoSpellIcon;

/** @brief The icons' names, by their HD art's name (the nodes stay where they are, so draws can point at them). */
std::unordered_map<std::string, UiSpriteName> SpellIconNamesByArt;

/** @brief The name a spell icon's draw goes by; null when it has no HD art name (a label frame, colours with no plate). */
const UiSpriteName *SpellIconNameOf(uint8_t frame, bool large, uint8_t tint, int width, int height)
{
	if (!SpellIconHasHdArt(frame) || tint >= NumSpellIconTints)
		return nullptr;
	const UiSpriteName *&name = SpellIconNames[tint][large ? 1 : 0][frame];
	if (name == nullptr) {
		const std::string art = SpellIconArtName(frame, large, static_cast<SpellType>(tint));
		if (art.empty()) {
			name = &NoSpellIcon;
		} else {
			const auto [it, inserted] = SpellIconNamesByArt.try_emplace(art + (large ? "@big" : "@small"));
			if (inserted) {
				it->second = UiSpriteName { art, static_cast<uint16_t>(width), static_cast<uint16_t>(height) };
				it->second.spellIcon = true;
			}
			name = &it->second;
		}
	}
	return name != &NoSpellIcon && name->width == width && name->height == height ? name : nullptr;
}

/** @brief Loads one picture of the HD art (hd\<name>.png), 4 bytes a pixel; null when the art hasn't got it. */
SDL_Surface *LoadUiPicture(const std::string &name)
{
	std::string path = "hd\\" + name + ".png";
	std::replace(path.begin(), path.end(), '/', '\\');
	if (!FindAsset(path).ok())
		return nullptr;
	SDL_Surface *loaded = LoadPNG(path.c_str());
	if (loaded == nullptr)
		return nullptr;
	SDL_Surface *picture = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
	SDL_FreeSurface(loaded);
	return picture;
}

/**
 * @brief A spell icon's HD art by its name (SpellIconArtName): the plate, with the symbol's layer laid over it. Null
 * when either file is missing (or they differ in size): the icon then keeps the game's own drawing.
 */
UiArt *SpellIconArtFor(const std::string &name)
{
	UiArt &art = UiArts[name];
	if (!art.tried) {
		art.tried = true;
		const size_t plus = name.find('+');
		if (plus == std::string::npos) {
			art.picture = LoadUiPicture(name);
			// A small icon's own plate may be missing from older art: the large plate then.
			if (const std::string_view fallback = SpellIconPlateFallback(name); art.picture == nullptr && !fallback.empty())
				art.picture = LoadUiPicture(std::string(fallback));
		} else if (const UiArt *plate = SpellIconArtFor(name.substr(0, plus)); plate != nullptr) {
			// (UiArts' entries stay where they are when another is added, so `art` is still good.)
			SDL_Surface *layer = LoadUiPicture(name.substr(plus + 1));
			if (layer != nullptr && layer->w == plate->picture->w && layer->h == plate->picture->h) {
				art.picture = SDL_DuplicateSurface(plate->picture);
				if (art.picture != nullptr) {
					LayPictureOver(static_cast<uint8_t *>(art.picture->pixels), art.picture->pitch,
					    static_cast<const uint8_t *>(layer->pixels), layer->pitch, layer->w, layer->h);
				}
			}
			if (layer != nullptr)
				SDL_FreeSurface(layer);
		}
	}
	return art.picture != nullptr ? &art : nullptr;
}

/** @brief A spell icon about to be drawn: where, and the UI's pixels there before it. */
struct PendingSpellIcon {
	const UiSpriteName *name = nullptr;
	SDL_Rect rect {};
	std::vector<uint8_t> under;
};
PendingSpellIcon PendingUiSpellIcon;

void RecordUiSpellIconImpl(const Surface &out, Rectangle box, uint8_t frame, bool large, uint8_t tint, bool drawn)
{
	PendingSpellIcon &pending = PendingUiSpellIcon;
	const UiSpriteName *before = pending.name;
	pending.name = nullptr;
	if (out.surface == nullptr || out.surface != PalSurface)
		return;
	const int w = box.size.width;
	const int h = box.size.height;
	if (w <= 0 || h <= 0 || w > UINT16_MAX || h > UINT16_MAX)
		return;
	const SDL_Rect rect { out.region.x + box.position.x, out.region.y + box.position.y, w, h };
	const SDL_Surface *ui = PalSurface;
	// The part of the box inside the surface it is drawn on: the rest is clipped, and lays nothing down.
	const int left = std::max({ rect.x, static_cast<int>(out.region.x), 0 });
	const int top = std::max({ rect.y, static_cast<int>(out.region.y), 0 });
	const int right = std::min({ rect.x + w, static_cast<int>(out.region.x + out.region.w), ui->w });
	const int bottom = std::min({ rect.y + h, static_cast<int>(out.region.y + out.region.h), ui->h });
	if (right <= left || bottom <= top)
		return;
	const auto *pixels = static_cast<const uint8_t *>(ui->pixels);
	if (!drawn) {
		if (!Enabled())
			return;
		const UiSpriteName *name = SpellIconNameOf(frame, large, tint, w, h);
		if (name == nullptr || SpellIconArtFor(name->name) == nullptr)
			return;
		pending.name = name;
		pending.rect = rect;
		pending.under.assign(static_cast<size_t>(w) * h, 0);
		for (int uy = top; uy < bottom; uy++) {
			const uint8_t *line = pixels + static_cast<ptrdiff_t>(uy) * ui->pitch;
			std::copy(line + left, line + right, pending.under.begin() + static_cast<ptrdiff_t>(uy - rect.y) * w + (left - rect.x));
		}
		return;
	}
	if (before == nullptr || pending.rect.x != rect.x || pending.rect.y != rect.y || pending.rect.w != w || pending.rect.h != h)
		return;
	if (UiDraws.size() >= 512)
		UiDraws.clear();
	UiDraw &draw = UiDraws.emplace_back();
	draw.order = UiOrder++;
	draw.name = before;
	draw.rect = rect;
	draw.under = std::move(pending.under);
	pending.under.clear();
	// An icon fills its box: every pixel of it inside the surface is the icon's, as it is now.
	auto pattern = std::make_shared<UiPattern>();
	pattern->width = static_cast<uint16_t>(w);
	pattern->height = static_cast<uint16_t>(h);
	pattern->pixels.assign(static_cast<size_t>(w) * h, -1);
	draw.shows.assign(static_cast<size_t>(w) * h, 0);
	for (int uy = top; uy < bottom; uy++) {
		const uint8_t *line = pixels + static_cast<ptrdiff_t>(uy) * ui->pitch;
		for (int ux = left; ux < right; ux++) {
			const size_t i = static_cast<size_t>(uy - rect.y) * w + (ux - rect.x);
			pattern->pixels[i] = line[ux];
			draw.shows[i] = 1; // inside the surface: may show
		}
	}
	draw.pattern = pattern.get();
	draw.ownPattern = std::move(pattern);
}

/**
 * @brief The border the game drew just inside a box: the spell icons drawn there this frame don't show under it, even
 * where it happens to have the colour the icon had.
 */
void CoverUiSpellIconImpl(const Surface &out, Rectangle box)
{
	if (out.surface == nullptr || out.surface != PalSurface)
		return;
	const SDL_Rect rect { out.region.x + box.position.x, out.region.y + box.position.y, box.size.width, box.size.height };
	for (auto it = UiDraws.rbegin(); it != UiDraws.rend(); ++it) {
		UiDraw &draw = *it;
		if (!draw.name->spellIcon)
			continue;
		const int left = std::max(rect.x, draw.rect.x);
		const int top = std::max(rect.y, draw.rect.y);
		const int right = std::min(rect.x + rect.w, draw.rect.x + draw.rect.w);
		const int bottom = std::min(rect.y + rect.h, draw.rect.y + draw.rect.h);
		for (int uy = top; uy < bottom; uy++) {
			const bool edgeRow = uy < rect.y + 2 || uy >= rect.y + rect.h - 2;
			for (int ux = left; ux < right; ux++) {
				if (edgeRow || ux < rect.x + 2 || ux >= rect.x + rect.w - 2)
					draw.shows[static_cast<size_t>(uy - draw.rect.y) * draw.rect.w + (ux - draw.rect.x)] = 0;
			}
		}
	}
}

[[maybe_unused]] const bool RecordUiSpellIconHookSet = (RecordUiSpellIconHook = RecordUiSpellIconImpl, true);
[[maybe_unused]] const bool CoverUiSpellIconHookSet = (CoverUiSpellIconHook = CoverUiSpellIconImpl, true);

/** @brief A spell icon's picture shrunk to a size on screen, made the first time it is shown at that size. */
UiFittedArt *FittedArtOf(UiArt &art, int width, int height)
{
	if (width <= 0 || height <= 0 || width > UINT16_MAX || height > UINT16_MAX)
		return nullptr;
	const uint32_t size = static_cast<uint32_t>(width) | (static_cast<uint32_t>(height) << 16);
	auto it = art.fitted.find(size);
	if (it == art.fitted.end()) {
		// A new size: the UI's scale or the window changed. The sizes before it are no longer shown (an icon takes one
		// of two widths and heights at a scale, by where it is).
		if (art.fitted.size() >= 8) {
			for (auto &[key, old] : art.fitted) {
				if (old.texture != nullptr)
					SDL_DestroyTexture(old.texture);
				if (old.picture != nullptr)
					SDL_FreeSurface(old.picture);
			}
			art.fitted.clear();
			// Copies of them with parts left out may be among these.
			for (auto &[key, texture] : MaskedTextures)
				SDL_DestroyTexture(texture);
			MaskedTextures.clear();
		}
		it = art.fitted.try_emplace(size).first;
		const SDL_Surface *picture = art.picture;
		SDL_Surface *fitted = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
		if (fitted != nullptr) {
			ShrinkPicture(static_cast<const uint8_t *>(picture->pixels), picture->w, picture->h, picture->pitch,
			    static_cast<uint8_t *>(fitted->pixels), width, height, fitted->pitch);
		}
		it->second.picture = fitted;
	}
	return it->second.picture != nullptr ? &it->second : nullptr;
}

/**
 * @brief Draws a spell icon's HD art over the UI: its picture at the very size the icon's box has on screen, on the
 * screen pixels the game's own icon covers, where the icon's own pixels still show.
 */
void DrawFittedUiArt(SDL_Renderer *renderer, const UiDraw &draw)
{
	// How much bigger the UI is on screen (it is drawn in UI pixels).
	float scaleX = 1.F;
	float scaleY = 1.F;
	SDL_RenderGetScale(renderer, &scaleX, &scaleY);
	if (!(scaleX > 0.F) || !(scaleY > 0.F))
		return;
	const int x0 = FirstScreenPixelOf(draw.rect.x, scaleX);
	const int y0 = FirstScreenPixelOf(draw.rect.y, scaleY);
	const int width = FirstScreenPixelOf(draw.rect.x + draw.rect.w, scaleX) - x0;
	const int height = FirstScreenPixelOf(draw.rect.y + draw.rect.h, scaleY) - y0;
	UiFittedArt *fitted = FittedArtOf(*draw.art, width, height);
	if (fitted == nullptr)
		return;
	SDL_Texture *texture = nullptr;
	// (An icon partly outside the surface it was drawn on has all the pixels it laid down showing, but not all of itself.)
	if (draw.whole && draw.shown == draw.rect.w * draw.rect.h) {
		if (fitted->texture == nullptr)
			fitted->texture = MakeTexture(renderer, fitted->picture);
		texture = fitted->texture;
	} else {
		// Partly covered (a border, a key's letter, a tooltip): a copy with the screen pixels of covered UI pixels left out.
		uint64_t key = std::hash<std::string> {}(draw.name->name) ^ 0xD6E8FEB86659FD93ULL;
		for (const int part : { width, height, x0 - static_cast<int>(std::floor(static_cast<float>(draw.rect.x) * scaleX)), y0 - static_cast<int>(std::floor(static_cast<float>(draw.rect.y) * scaleY)), draw.rect.w, draw.rect.h })
			key = key * 1099511628211ULL ^ static_cast<uint64_t>(static_cast<uint32_t>(part));
		for (const uint8_t bit : draw.shows)
			key = key * 1099511628211ULL ^ bit;
		auto [it, inserted] = MaskedTextures.try_emplace(key, nullptr);
		if (inserted) {
			SDL_Surface *copy = SDL_DuplicateSurface(fitted->picture);
			if (copy != nullptr) {
				for (int y = 0; y < copy->h; y++) {
					auto *row = static_cast<uint8_t *>(copy->pixels) + static_cast<ptrdiff_t>(y) * copy->pitch;
					const int uy = static_cast<int>(std::floor((static_cast<float>(y0 + y) + 0.5F) / scaleY)) - draw.rect.y;
					for (int x = 0; x < copy->w; x++) {
						const int ux = static_cast<int>(std::floor((static_cast<float>(x0 + x) + 0.5F) / scaleX)) - draw.rect.x;
						const bool shows = ux >= 0 && uy >= 0 && ux < draw.rect.w && uy < draw.rect.h && draw.shows[static_cast<size_t>(uy) * draw.rect.w + ux] != 0;
						if (!shows)
							row[static_cast<ptrdiff_t>(x) * 4 + 3] = 0;
					}
				}
				it->second = MakeTexture(renderer, copy);
				SDL_FreeSurface(copy);
			}
		}
		texture = it->second;
	}
	if (texture == nullptr)
		return;
	const SDL_FRect dest { static_cast<float>(x0) / scaleX, static_cast<float>(y0) / scaleY, static_cast<float>(width) / scaleX, static_cast<float>(height) / scaleY };
	SDL_RenderCopyF(renderer, texture, nullptr, &dest);
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
	if (UiHiddenAny)
		std::fill(UiHidden.begin(), UiHidden.end(), 0);
	UiHiddenAny = false;
	if (ui == nullptr || (UiDraws.empty() && UiBlits.empty()))
		return;
	if (UiHiddenWidth != ui->w || UiHiddenHeight != ui->h) {
		UiHiddenWidth = ui->w;
		UiHiddenHeight = ui->h;
		UiHidden.assign(static_cast<size_t>(ui->w) * ui->h, 0);
	}
	for (UiDraw &draw : UiDraws) {
		draw.art = draw.name->spellIcon ? SpellIconArtFor(draw.name->name) : ArtFor(draw.name->name);
		draw.outline.smooth = false;
	}
	for (size_t i = 0; i < UiDraws.size(); i++) {
		if (UiDraws[i].art != nullptr && UiDraws[i].outline.pixels != nullptr)
			FindWhereOutlineShows(i);
	}
	for (UiDraw &draw : UiDraws) {
		draw.shown = 0;
		draw.whole = false;
		if (draw.art == nullptr || draw.name->panel)
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
	// After the item pictures are taken out, so a panel shows its art under theirs.
	for (UiDraw &draw : UiDraws) {
		if (draw.art != nullptr && draw.name->panel)
			PrepareUiPanel(draw, ui);
	}
	for (UiBlit &blit : UiBlits)
		PrepareUiBlit(blit, ui);
}

const uint8_t *UiHiddenPixels(int width, int height)
{
	if (!UiHiddenAny || width != UiHiddenWidth || height != UiHiddenHeight)
		return nullptr;
	return UiHidden.data();
}

void RenderUiPanels(SDL_Renderer *renderer)
{
	if (renderer == nullptr)
		return;
	UseRenderer(renderer);
	const auto drawPanel = [&](const UiDraw &draw) {
		if (draw.shown == 0 || draw.art == nullptr || !draw.name->panel)
			return;
		const SDL_FRect dest = DestOf(draw);
		if (SDL_Texture *texture = TextureOf(renderer, *draw.art); texture != nullptr)
			SDL_RenderCopyF(renderer, texture, nullptr, &dest);
	};
	// In the order the game drew them: a button drawn over the bottom panel goes over its art.
	size_t nextDraw = 0;
	for (const UiBlit &blit : UiBlits) {
		while (nextDraw < UiDraws.size() && UiDraws[nextDraw].order < blit.order)
			drawPanel(UiDraws[nextDraw++]);
		if (blit.shown == 0 || blit.art == nullptr)
			continue;
		SDL_Texture *texture = TextureOf(renderer, *blit.art);
		if (texture == nullptr)
			continue;
		// The art is of the whole surface, at its own size (twice the game's).
		const float scale = static_cast<float>(blit.art->picture->w) / static_cast<float>(blit.sourceWidth);
		const SDL_Rect src {
			static_cast<int>(std::lround(blit.src.x * scale)),
			static_cast<int>(std::lround(blit.src.y * scale)),
			static_cast<int>(std::lround(blit.src.w * scale)),
			static_cast<int>(std::lround(blit.src.h * scale)),
		};
		if (src.x + src.w > blit.art->picture->w || src.y + src.h > blit.art->picture->h)
			continue;
		const SDL_FRect dest { static_cast<float>(blit.dest.x), static_cast<float>(blit.dest.y), static_cast<float>(blit.dest.w), static_cast<float>(blit.dest.h) };
		SDL_RenderCopyF(renderer, texture, &src, &dest);
	}
	while (nextDraw < UiDraws.size())
		drawPanel(UiDraws[nextDraw++]);
	UiBlits.clear();
}

void BeginUiExport(const std::string &dir)
{
	UiExportDir = dir;
	UiExported.clear();
}

void ExportUiSurface(std::string_view name, const Surface &surface)
{
	if (UiExportDir.empty() || surface.surface == nullptr)
		return;
	WriteUiFrame(UiPictureName(name), 0, surface.w(), surface.h(), [&](int x, int y) {
		const uint8_t colour = *surface.at(x, y);
		return colour == 0 ? -1 : static_cast<int>(colour);
	});
}

void EndUiExport()
{
	if (UiExportDir.empty())
		return;
	std::string json = "{\"pictures\":[";
	bool first = true;
	for (const auto &[name, sizes] : UiExported) {
		json += first ? "\n" : ",\n";
		first = false;
		json += "{\"name\":\"" + name + "\",\"frames\":" + std::to_string(sizes.size()) + ",\"sizes\":[";
		for (size_t i = 0; i < sizes.size(); i++) {
			if (i != 0)
				json += ",";
			json += "[" + std::to_string(sizes[i].first) + "," + std::to_string(sizes[i].second) + "]";
		}
		json += "]}";
	}
	json += "\n]}\n";
	const std::string dir = UiExportDir + "/ui";
	RecursivelyCreateDir(dir.c_str());
	if (FILE *f = std::fopen((dir + "/list.json").c_str(), "wb"); f != nullptr) {
		std::fwrite(json.data(), 1, json.size(), f);
		std::fclose(f);
	}
	LogInfo("HD export: wrote {} UI pictures", UiExported.size());
	UiExportDir.clear();
	UiExported.clear();
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
	UseRenderer(renderer);
	if (MaskedTextures.size() > 256) {
		for (auto &[key, texture] : MaskedTextures)
			SDL_DestroyTexture(texture);
		MaskedTextures.clear();
	}
	for (UiDraw &draw : UiDraws) {
		// Panels went under the UI (RenderUiPanels).
		if (draw.shown == 0 || draw.art == nullptr || renderer == nullptr || draw.name->panel)
			continue;
		if (draw.name->spellIcon) {
			DrawFittedUiArt(renderer, draw);
			continue;
		}
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
	UiBlits.clear();
	UiOrder = 0;
	PendingUiOutline = {};
	UiOutlines.Prune(SDL_GetTicks());
}

#else

void RegisterUiSprite(const uint8_t *, const std::string &, uint16_t, uint16_t) { }
void RegisterFittedUiSprite(ClxSprite, ClxSprite) { }
void PrepareUiSprites() { }
void RestoreUiSprites() { }
void RenderUiSprites(SDL_Renderer *) { }
const uint8_t *UiHiddenPixels(int, int) { return nullptr; }
void RenderUiPanels(SDL_Renderer *) { }
void BeginUiExport(const std::string &) { }
void ExportUiSurface(std::string_view, const Surface &) { }
void EndUiExport() { }

#endif

} // namespace hd

} // namespace devilution
