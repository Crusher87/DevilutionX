#include "poe2/hd_batch.hpp"

#ifdef POE2_HD_BATCH

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace devilution {

namespace hd {

namespace batch {

namespace {

/** @brief Round each picture in a page, its own edge pixels again: what is read a hair past its edge is what its own texture would give. */
constexpr int Gutter = 1;
/** @brief A page's side unless the graphics card's textures are smaller: 4096 holds some 3,800 pieces of 64 by 64. */
constexpr int WantedPageSide = 4096;
constexpr int SmallestPageSide = 1024;
/** @brief Pages at most (64 MB each at 4096): art that finds no room is drawn from its own texture as before. */
constexpr int MostPages = 6;
/** @brief A run is sent on when it holds this many corners, so no call grows without end. */
constexpr size_t MostIndices = 49152;

/** @brief Where a texture's art is in the atlas: its page and its place there, without the gutter. */
struct Slot {
	int page;
	int x;
	int y;
	int width;
	int height;
};

/** @brief A row of pictures of about one height across a page. */
struct Shelf {
	int y;
	int height;
	int used;
};

struct Page {
	SDL_Texture *texture = nullptr;
	std::vector<Shelf> shelves;
	int nextY = 0;
};

/** @brief Draws gathered for one page and one way of blending, in the order they came. */
struct Run {
	int page = -1;
	std::vector<SDL_Vertex> vertices;
	std::vector<int> indices;
};

std::unordered_map<SDL_Texture *, Slot> Slots;
std::vector<Page> Pages;
SDL_Renderer *PageRenderer = nullptr;
int PageSide = 0;
int PageSideLimit = 0;
int PageLimit = MostPages;
int TightEdges = -1;

/** @brief The switch, as of the last BeginFrame. */
bool On = false;
/** @brief Between BeginFrame and EndFrame with the switch on. */
bool Gathering = false;
/** @brief How near a tile may come to light waiting to be added before the light is drawn first, in the renderer's pixels. */
float EdgeRoom = 0;

/** @brief The tiles gathered, and the light to add on top of them once they are drawn. */
Run Blended;
Run Added;
/** @brief Where the light in Added goes: a tile drawn over any of these has to come after it. */
std::vector<SDL_FRect> AddedRects;
std::vector<uint8_t> Scratch;

FrameStats Current;
FrameStats Last;
/** @brief The texture of the frame's last draw, and whether there was one. */
SDL_Texture *LastTexture = nullptr;
bool Drawn = false;

/** @brief Forgets what is gathered and where every picture is. */
void Empty()
{
	Blended.vertices.clear();
	Blended.indices.clear();
	Added.vertices.clear();
	Added.indices.clear();
	AddedRects.clear();
	Pending = false;
	Slots.clear();
	for (Page &page : Pages) {
		page.shelves.clear();
		page.nextY = 0;
	}
}

void Note(SDL_Texture *texture)
{
	Current.draws++;
	if (!Drawn || texture != LastTexture)
		Current.textureSwitches++;
	Drawn = true;
	LastTexture = texture;
}

/** @brief The side of a page for this renderer, or 0 when its textures are too small for an atlas to be worth it. */
int ChoosePageSide(SDL_Renderer *renderer)
{
	// Built for SDL_RenderGeometry, but the SDL beside the game may be an older one.
	SDL_version linked;
	SDL_GetVersion(&linked);
	if (SDL_VERSIONNUM(linked.major, linked.minor, linked.patch) < SDL_VERSIONNUM(2, 0, 18))
		return 0;
	int side = WantedPageSide;
	if (PageSideLimit > 0) {
		side = PageSideLimit;
	} else {
		SDL_RendererInfo info;
		if (SDL_GetRendererInfo(renderer, &info) != 0)
			return 0;
		// (SDL's software renderer names no largest size.)
		while (side >= SmallestPageSide && ((info.max_texture_width > 0 && side > info.max_texture_width) || (info.max_texture_height > 0 && side > info.max_texture_height)))
			side /= 2;
		if (side < SmallestPageSide)
			return 0;
	}
	return side;
}

/** @brief Finds room for a picture with its gutter. */
bool FindRoom(SDL_Renderer *renderer, int width, int height, Slot &slot)
{
	const int wide = width + Gutter * 2;
	const int tall = height + Gutter * 2;
	if (PageSide <= 0 || wide > PageSide || tall > PageSide)
		return false;
	for (size_t p = 0;; p++) {
		if (p == Pages.size()) {
			if (static_cast<int>(Pages.size()) >= PageLimit)
				return false;
			Pages.emplace_back();
		}
		Page &page = Pages[p];
		if (page.texture == nullptr) {
			page.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, PageSide, PageSide);
			if (page.texture == nullptr) {
				// No room on the card for a page: the art stays in its own textures.
				Pages.resize(p);
				PageLimit = static_cast<int>(p);
				return false;
			}
			SDL_SetTextureBlendMode(page.texture, SDL_BLENDMODE_BLEND);
			SDL_SetTextureScaleMode(page.texture, SDL_ScaleModeNearest);
		}
		Shelf *found = nullptr;
		for (Shelf &shelf : page.shelves) {
			if (shelf.height >= tall && shelf.height <= tall + tall / 8 && shelf.used + wide <= PageSide) {
				found = &shelf;
				break;
			}
		}
		if (found == nullptr && page.nextY + tall <= PageSide) {
			page.shelves.push_back(Shelf { page.nextY, tall, 0 });
			page.nextY += tall;
			found = &page.shelves.back();
		}
		if (found == nullptr)
			continue;
		slot = Slot { static_cast<int>(p), found->used + Gutter, found->y + Gutter, width, height };
		found->used += wide;
		return true;
	}
}

/**
 * @brief A place across a picture (0 to 1) as a place across its page.
 *
 * SDL's software renderer cuts a texture place down to a whole pixel, so a place a hair under a whole pixel must
 * stay under it once the picture's place in the page is added (a page's side is a power of two, so nothing else is
 * rounded). A graphics card doesn't see the difference.
 */
float PagePlace(float along, int size, int origin)
{
	const float inside = along * static_cast<float>(size);
	float at = static_cast<float>(origin) + inside;
	const float whole = std::floor(inside);
	if (inside != whole && at >= static_cast<float>(origin) + whole + 1 && at > 0) {
		uint32_t bits;
		std::memcpy(&bits, &at, sizeof(bits));
		bits--;
		std::memcpy(&at, &bits, sizeof(bits));
	}
	return at / static_cast<float>(PageSide);
}

void Send(SDL_Renderer *renderer, Run &run, SDL_BlendMode mode)
{
	if (run.indices.empty()) {
		run.vertices.clear();
		return;
	}
	SDL_Texture *texture = Pages[static_cast<size_t>(run.page)].texture;
	if (mode != SDL_BLENDMODE_BLEND)
		SDL_SetTextureBlendMode(texture, mode);
	Note(texture);
	SDL_RenderGeometry(renderer, texture, run.vertices.data(), static_cast<int>(run.vertices.size()), run.indices.data(), static_cast<int>(run.indices.size()));
	if (mode != SDL_BLENDMODE_BLEND)
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	run.vertices.clear();
	run.indices.clear();
}

/** @brief Whether two draws may share a pixel (true unless they are surely apart, so also for a place that is no number). */
bool Near(const SDL_FRect &a, const SDL_FRect &b)
{
	const bool apart = a.x - EdgeRoom >= b.x + b.w || b.x - EdgeRoom >= a.x + a.w || a.y - EdgeRoom >= b.y + b.h || b.y - EdgeRoom >= a.y + a.h;
	return !apart;
}

/** @brief Takes a draw of art in the atlas into its run. False when it can't be gathered (it is then drawn as before). */
bool Gather(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Vertex *vertices, int count, const int *indices, int indexCount)
{
	if (!Gathering || texture == nullptr || vertices == nullptr || count <= 0 || renderer != PageRenderer)
		return false;
	const auto it = Slots.find(texture);
	if (it == Slots.end())
		return false;
	SDL_BlendMode mode = SDL_BLENDMODE_NONE;
	if (SDL_GetTextureBlendMode(texture, &mode) != 0 || (mode != SDL_BLENDMODE_BLEND && mode != SDL_BLENDMODE_ADD))
		return false;
	const int corners = indices != nullptr ? indexCount : count;
	if (corners <= 0 || corners % 3 != 0)
		return false;
	if (indices != nullptr) {
		for (int i = 0; i < indexCount; i++) {
			if (indices[i] < 0 || indices[i] >= count)
				return false;
		}
	}
	const Slot &slot = it->second;
	// Where it is drawn: round the corners its shapes use.
	const SDL_FPoint &first = vertices[indices != nullptr ? indices[0] : 0].position;
	float x0 = first.x;
	float y0 = first.y;
	float x1 = x0;
	float y1 = y0;
	for (int i = 1; i < corners; i++) {
		const SDL_FPoint &at = vertices[indices != nullptr ? indices[i] : i].position;
		x0 = std::min(x0, at.x);
		y0 = std::min(y0, at.y);
		x1 = std::max(x1, at.x);
		y1 = std::max(y1, at.y);
	}
	const SDL_FRect bounds { x0, y0, x1 - x0, y1 - y0 };
	const bool added = mode == SDL_BLENDMODE_ADD;
	Run &run = added ? Added : Blended;
	if (!run.indices.empty() && (run.page != slot.page || run.indices.size() + static_cast<size_t>(corners) > MostIndices))
		FlushPending(renderer);
	if (!added && !AddedRects.empty()) {
		// Light waiting to be added where this tile goes was drawn before the tile, and has to stay before it.
		for (const SDL_FRect &rect : AddedRects) {
			if (Near(rect, bounds)) {
				FlushPending(renderer);
				break;
			}
		}
	}
	run.page = slot.page;
	const int base = static_cast<int>(run.vertices.size());
	for (int i = 0; i < count; i++) {
		SDL_Vertex vertex = vertices[i];
		vertex.tex_coord.x = PagePlace(vertex.tex_coord.x, slot.width, slot.x);
		vertex.tex_coord.y = PagePlace(vertex.tex_coord.y, slot.height, slot.y);
		run.vertices.push_back(vertex);
	}
	if (indices != nullptr) {
		for (int i = 0; i < indexCount; i++)
			run.indices.push_back(base + indices[i]);
	} else {
		for (int i = 0; i < count; i++)
			run.indices.push_back(base + i);
	}
	if (added)
		AddedRects.push_back(bounds);
	Pending = true;
	Current.merged++;
	return true;
}

} // namespace

bool BeginFrame(SDL_Renderer *renderer, bool on)
{
	const bool turnedOn = on && !On;
	// Turned off: the pages go, and every draw is its own again.
	if (On && !on)
		Release(true);
	On = on;
	if (On && PageRenderer != renderer) {
		Release(false);
		PageRenderer = renderer;
		PageSide = ChoosePageSide(renderer);
	}
	Current = {};
	Drawn = false;
	LastTexture = nullptr;
	Gathering = On && PageSide > 0;
	if (Gathering) {
		bool tight = TightEdges > 0;
		if (TightEdges < 0) {
			SDL_RendererInfo info;
			tight = SDL_GetRendererInfo(renderer, &info) == 0 && (info.flags & SDL_RENDERER_SOFTWARE) == 0;
		}
		float scaleX = 1;
		float scaleY = 1;
		SDL_RenderGetScale(renderer, &scaleX, &scaleY);
		const float scale = std::max(std::min(scaleX, scaleY), 0.01F);
		// On a graphics card two shapes share no pixel when one ends exactly where the other begins, or before it (a
		// pixel on the shared edge goes to one of them, and edges are rounded the same way). SDL's software renderer
		// rounds a shape's edges to whole pixels, each on its own, so there the light is given room.
		EdgeRoom = tight ? 0.F : 1.5F / std::min(scale, 1.F);
	}
	return turnedOn;
}

void EndFrame(SDL_Renderer *renderer)
{
	Flush(renderer);
	Gathering = false;
	Last = Current;
}

const FrameStats &LastFrame()
{
	return Last;
}

void Add(SDL_Renderer *renderer, SDL_Texture *texture, const void *pixels, int width, int height, int pitch)
{
	if (!On || PageSide <= 0 || renderer != PageRenderer || texture == nullptr || pixels == nullptr || width <= 0 || height <= 0)
		return;
	// Only art drawn pixel for pixel, as the tiles are: smoothed art would take in its neighbours' colours at the edge.
	SDL_ScaleMode scaleMode = SDL_ScaleModeLinear;
	if (SDL_GetTextureScaleMode(texture, &scaleMode) != 0 || scaleMode != SDL_ScaleModeNearest)
		return;
	Slots.erase(texture);
	Slot slot;
	if (!FindRoom(renderer, width, height, slot))
		return;
	const int wide = width + Gutter * 2;
	const int tall = height + Gutter * 2;
	Scratch.resize(static_cast<size_t>(wide) * tall * 4);
	for (int y = 0; y < tall; y++) {
		const auto *from = static_cast<const uint8_t *>(pixels) + static_cast<ptrdiff_t>(std::clamp(y - Gutter, 0, height - 1)) * pitch;
		uint8_t *to = Scratch.data() + static_cast<size_t>(y) * wide * 4;
		for (int x = 0; x < wide; x++)
			std::memcpy(to + static_cast<size_t>(x) * 4, from + static_cast<size_t>(std::clamp(x - Gutter, 0, width - 1)) * 4, 4);
	}
	const SDL_Rect place { slot.x - Gutter, slot.y - Gutter, wide, tall };
	if (SDL_UpdateTexture(Pages[static_cast<size_t>(slot.page)].texture, &place, Scratch.data(), wide * 4) != 0)
		return;
	Slots.emplace(texture, slot);
}

void Forget(SDL_Texture *texture)
{
	if (!Slots.empty())
		Slots.erase(texture);
}

void Release(bool destroy)
{
	Empty();
	if (destroy && PageRenderer != nullptr) {
		for (Page &page : Pages) {
			if (page.texture != nullptr)
				SDL_DestroyTexture(page.texture);
		}
	}
	Pages.clear();
	PageRenderer = nullptr;
	PageSide = 0;
}

bool Has(SDL_Texture *texture)
{
	return Slots.find(texture) != Slots.end();
}

int PageCount()
{
	int count = 0;
	for (const Page &page : Pages) {
		if (page.texture != nullptr && !page.shelves.empty())
			count++;
	}
	return count;
}

void FlushPending(SDL_Renderer *renderer)
{
	Pending = false;
	Send(renderer, Blended, SDL_BLENDMODE_BLEND);
	Send(renderer, Added, SDL_BLENDMODE_ADD);
	AddedRects.clear();
}

int Geometry(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Vertex *vertices, int count, const int *indices, int indexCount)
{
	if (Gather(renderer, texture, vertices, count, indices, indexCount))
		return 0;
	Flush(renderer);
	Note(texture);
	return SDL_RenderGeometry(renderer, texture, vertices, count, indices, indexCount);
}

int CopyF(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *source, const SDL_FRect *dest)
{
	if (Gathering && source == nullptr && dest != nullptr && texture != nullptr && Slots.find(texture) != Slots.end()) {
		// The whole picture in its texture's colour: the same four corners SDL makes of a copy.
		SDL_Color colour { 255, 255, 255, 255 };
		if (SDL_GetTextureColorMod(texture, &colour.r, &colour.g, &colour.b) == 0 && SDL_GetTextureAlphaMod(texture, &colour.a) == 0) {
			const float x1 = dest->x + dest->w;
			const float y1 = dest->y + dest->h;
			const SDL_Vertex corners[4] {
				{ { dest->x, dest->y }, colour, { 0, 0 } },
				{ { x1, dest->y }, colour, { 1, 0 } },
				{ { x1, y1 }, colour, { 1, 1 } },
				{ { dest->x, y1 }, colour, { 0, 1 } },
			};
			static constexpr int Order[6] { 0, 1, 2, 0, 2, 3 };
			if (Gather(renderer, texture, corners, 4, Order, 6))
				return 0;
		}
	}
	Flush(renderer);
	Note(texture);
	return SDL_RenderCopyF(renderer, texture, source, dest);
}

int Copy(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *source, const SDL_Rect *dest)
{
	Flush(renderer);
	Note(texture);
	return SDL_RenderCopy(renderer, texture, source, dest);
}

int FillRect(SDL_Renderer *renderer, const SDL_Rect *rect)
{
	Flush(renderer);
	Note(nullptr);
	return SDL_RenderFillRect(renderer, rect);
}

int FillRectF(SDL_Renderer *renderer, const SDL_FRect *rect)
{
	Flush(renderer);
	Note(nullptr);
	return SDL_RenderFillRectF(renderer, rect);
}

int ClearTarget(SDL_Renderer *renderer)
{
	Flush(renderer);
	Note(nullptr);
	return SDL_RenderClear(renderer);
}

int SetTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
	Flush(renderer);
	return SDL_SetRenderTarget(renderer, texture);
}

int SetClipRect(SDL_Renderer *renderer, const SDL_Rect *rect)
{
	Flush(renderer);
	return SDL_RenderSetClipRect(renderer, rect);
}

void DestroyTexture(SDL_Texture *texture)
{
	Forget(texture);
	SDL_DestroyTexture(texture);
}

int UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch)
{
	Forget(texture);
	return SDL_UpdateTexture(texture, rect, pixels, pitch);
}

void SetPageLimits(int side, int pages)
{
	PageSideLimit = side;
	PageLimit = pages > 0 ? pages : MostPages;
}

void SetTightEdges(int tight)
{
	TightEdges = tight;
}

} // namespace batch

} // namespace hd

} // namespace devilution

#endif
