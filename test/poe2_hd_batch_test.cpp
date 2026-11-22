// The HD batching experiment (Source/poe2/hd_batch.cpp) draws the same picture as drawing every tile on its own.
//
// The pictures are made here, at test time; SDL's software renderer draws them into memory, once with the batching
// off and once with it on, and every pixel of the two results is compared. The scenes make the calls the HD world
// makes for its tiles (hd.cpp): flat copies in a colour, shapes with a colour at each corner, light added on top,
// and sprites and fills in between that can't be batched.

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

#include <gtest/gtest.h>

#include "poe2/hd_batch.hpp"

#ifdef POE2_HD_BATCH

namespace devilution {
namespace {

namespace batch = hd::batch;

constexpr int TileSide = 64;
/** @brief A shape sheet as hd.cpp lays it out: five pictures side by side, a clear pixel either side of each. */
constexpr int SheetAxes = 5;
constexpr int SheetWidth = (TileSide + 2) * SheetAxes;

struct Random {
	uint32_t state;
	uint32_t Next()
	{
		state = state * 1664525U + 1013904223U;
		return state >> 8;
	}
	int Below(int limit) { return static_cast<int>(Next() % static_cast<uint32_t>(limit)); }
	float Unit() { return static_cast<float>(Next() & 0xFFFF) / 65535.F; }
};

struct Pictures {
	std::vector<SDL_Texture *> tiles;
	std::vector<SDL_Texture *> sheets;
	std::vector<SDL_Texture *> sprites;
};

/** @brief A tile's picture: noisy colours, cut to one of the shapes the game cuts tiles to, some of it see-through. */
SDL_Surface *TilePicture(int index, int width, int height, bool sheet)
{
	SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
	Random random { static_cast<uint32_t>(index) * 7919U + (sheet ? 17U : 3U) };
	for (int y = 0; y < height; y++) {
		auto *row = static_cast<uint8_t *>(surface->pixels) + static_cast<ptrdiff_t>(y) * surface->pitch;
		for (int x = 0; x < width; x++) {
			uint8_t alpha = 255;
			const int tx = sheet ? (x % (TileSide + 2)) - 1 : x;
			switch (index % 5) {
			case 0: break;
			case 1: alpha = tx + y * 2 >= TileSide ? 255 : 0; break; // a floor's left half
			case 2: alpha = (TileSide - 1 - tx) + y * 2 >= TileSide ? 255 : 0; break; // its right half
			case 3: alpha = ((tx + y) & 1) != 0 ? 255 : 0; break; // a wall cut away, every other pixel
			default: alpha = static_cast<uint8_t>(random.Below(256)); break;
			}
			if (sheet && (tx < 0 || tx >= TileSide))
				alpha = 0;
			if (sheet && alpha != 0)
				alpha = static_cast<uint8_t>(random.Below(256));
			row[x * 4] = static_cast<uint8_t>(random.Below(256));
			row[x * 4 + 1] = static_cast<uint8_t>(random.Below(256));
			row[x * 4 + 2] = static_cast<uint8_t>(random.Below(256));
			row[x * 4 + 3] = alpha;
		}
	}
	return surface;
}

Pictures MakePictures(SDL_Renderer *renderer, int tiles, int sheets, int sprites)
{
	Pictures pictures;
	for (int i = 0; i < tiles + sheets; i++) {
		const bool sheet = i >= tiles;
		SDL_Surface *surface = TilePicture(i, sheet ? SheetWidth : TileSide, TileSide, sheet);
		SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
		SDL_SetTextureBlendMode(texture, sheet ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
		SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
		batch::Add(renderer, texture, surface->pixels, surface->w, surface->h, surface->pitch);
		SDL_FreeSurface(surface);
		(sheet ? pictures.sheets : pictures.tiles).push_back(texture);
	}
	for (int i = 0; i < sprites; i++) {
		SDL_Surface *surface = TilePicture(1000 + i * 5 + 4, 96, 128, false);
		SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
		SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
		// (Smoothed art is not taken into the atlas.)
		batch::Add(renderer, texture, surface->pixels, surface->w, surface->h, surface->pitch);
		SDL_FreeSurface(surface);
		pictures.sprites.push_back(texture);
	}
	return pictures;
}

// The calls hd.cpp makes, through the same doors (there, SDL's names lead to them).

void Flat(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_FRect &dest, SDL_Color colour)
{
	SDL_SetTextureColorMod(texture, colour.r, colour.g, colour.b);
	SDL_SetTextureAlphaMod(texture, colour.a);
	batch::CopyF(renderer, texture, nullptr, &dest);
}

void FlatAdded(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_FRect &dest, SDL_Color colour)
{
	SDL_SetTextureColorMod(texture, colour.r, colour.g, colour.b);
	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_ADD);
	batch::CopyF(renderer, texture, nullptr, &dest);
	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
}

SDL_Color RandomColour(Random &random, bool alpha = false)
{
	return SDL_Color { static_cast<uint8_t>(random.Below(256)), static_cast<uint8_t>(random.Below(256)), static_cast<uint8_t>(random.Below(256)), static_cast<uint8_t>(alpha ? 128 : 255) };
}

/** @brief A draw lit left to right at `stops` places (hd.cpp's DrawShaded), and again added on top when `above`. */
void Gradient(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_FRect &dest, Random &random, int stops, bool above, std::array<SDL_Vertex, 8> *made = nullptr, std::array<int, 18> *order = nullptr, int *orderCount = nullptr)
{
	SDL_SetTextureColorMod(texture, 255, 255, 255);
	SDL_SetTextureAlphaMod(texture, 255);
	std::array<SDL_Vertex, 8> vertices;
	static constexpr std::array<float, 4> Places3 { 0.F, 0.37F, 1.F, 1.F };
	for (int s = 0; s < stops; s++) {
		const float fx = stops == 2 ? static_cast<float>(s) : (stops == 3 ? Places3[static_cast<size_t>(s)] : static_cast<float>(s) / 3.F);
		for (int edge = 0; edge < 2; edge++)
			vertices[static_cast<size_t>(s * 2 + edge)] = SDL_Vertex { { dest.x + fx * dest.w, dest.y + static_cast<float>(edge) * dest.h }, RandomColour(random), { fx, static_cast<float>(edge) } };
	}
	std::array<int, 18> indices;
	int count = 0;
	for (int s = 0; s + 1 < stops; s++) {
		for (const int corner : { 0, 2, 3, 0, 3, 1 })
			indices[static_cast<size_t>(count++)] = s * 2 + corner;
	}
	batch::Geometry(renderer, texture, vertices.data(), stops * 2, indices.data(), count);
	if (above) {
		for (int v = 0; v < stops * 2; v++)
			vertices[static_cast<size_t>(v)].color = RandomColour(random);
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_ADD);
		batch::Geometry(renderer, texture, vertices.data(), stops * 2, indices.data(), count);
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	}
	if (made != nullptr) {
		*made = vertices;
		*order = indices;
		*orderCount = count;
	}
}

/** @brief The foot of a wall (hd.cpp's DrawTileWall): half the piece, three corners and no order given. */
void WallFoot(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_FRect &dest, Random &random, bool right)
{
	SDL_SetTextureColorMod(texture, 255, 255, 255);
	SDL_SetTextureAlphaMod(texture, 255);
	const std::array<std::array<float, 2>, 3> corners { { { 0, 0 }, { 1, 0 }, { right ? 1.F : 0.F, 0.5F } } };
	std::array<SDL_Vertex, 3> vertices;
	for (size_t i = 0; i < 3; i++)
		vertices[i] = SDL_Vertex { { dest.x + corners[i][0] * dest.w, dest.y + corners[i][1] * dest.h }, RandomColour(random), { corners[i][0], corners[i][1] } };
	batch::Geometry(renderer, texture, vertices.data(), 3, nullptr, 0);
}

/** @brief A piece of ground lit at a grid of points (hd.cpp's DrawGroundLit). */
void Ground(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_FRect &dest, Random &random)
{
	SDL_SetTextureColorMod(texture, 255, 255, 255);
	SDL_SetTextureAlphaMod(texture, 255);
	std::array<SDL_Vertex, 9> vertices;
	for (int j = 0; j < 3; j++) {
		for (int i = 0; i < 3; i++) {
			const float fx = static_cast<float>(i) / 2;
			const float fy = static_cast<float>(j) / 2;
			vertices[static_cast<size_t>(j * 3 + i)] = SDL_Vertex { { dest.x + fx * dest.w, dest.y + fy * dest.h }, RandomColour(random), { fx, fy } };
		}
	}
	std::array<int, 24> indices;
	int used = 0;
	for (int j = 0; j < 2; j++) {
		for (int i = 0; i < 2; i++) {
			const int corner = j * 3 + i;
			for (const int v : { corner, corner + 1, corner + 4, corner, corner + 4, corner + 3 })
				indices[static_cast<size_t>(used++)] = v;
		}
	}
	batch::Geometry(renderer, texture, vertices.data(), 9, indices.data(), used);
}

/** @brief A tile's shape light (hd.cpp's AddShapeLight): its sheet's pictures added over the same shape, one draw. */
void ShapeLight(SDL_Renderer *renderer, SDL_Texture *sheet, const SDL_Vertex *vertices, int count, const int *indices, int indexCount, Random &random)
{
	const float cell = TileSide;
	const float across = (cell + 2) * SheetAxes;
	std::array<SDL_Vertex, 16 * SheetAxes> lit;
	std::array<int, 16 * 3 * SheetAxes> order;
	int litCount = 0;
	int orderCount = 0;
	for (int k = 0; k < SheetAxes; k++) {
		if (random.Below(3) == 0)
			continue;
		const float left = static_cast<float>(k) * (cell + 2) + 1;
		const SDL_Color colour = RandomColour(random);
		for (int v = 0; v < count; v++) {
			SDL_Vertex vertex = vertices[v];
			vertex.color = colour;
			vertex.tex_coord.x = (left + vertices[v].tex_coord.x * cell) / across;
			lit[static_cast<size_t>(litCount + v)] = vertex;
		}
		for (int i = 0; i < indexCount; i++)
			order[static_cast<size_t>(orderCount++)] = litCount + indices[i];
		litCount += count;
	}
	if (litCount > 0)
		batch::Geometry(renderer, sheet, lit.data(), litCount, order.data(), orderCount);
}

void FlatWithShape(SDL_Renderer *renderer, SDL_Texture *texture, SDL_Texture *sheet, const SDL_FRect &dest, Random &random)
{
	Flat(renderer, texture, dest, RandomColour(random));
	std::array<SDL_Vertex, 4> quad;
	for (int v = 0; v < 4; v++) {
		const float fx = static_cast<float>(v & 1);
		const float fy = static_cast<float>(v >> 1);
		quad[static_cast<size_t>(v)] = SDL_Vertex { { dest.x + fx * dest.w, dest.y + fy * dest.h }, { 255, 255, 255, 255 }, { fx, fy } };
	}
	static constexpr std::array<int, 6> QuadIndices { 0, 1, 2, 1, 3, 2 };
	ShapeLight(renderer, sheet, quad.data(), 4, QuadIndices.data(), 6, random);
}

struct Result {
	std::vector<uint8_t> pixels;
	batch::FrameStats stats;
	int pages = 0;
	int inAtlas = 0;
	double milliseconds = 0;
};

struct View {
	int width = 640;
	int height = 400;
	int tiles = 40;
	int sheets = 40;
	int sprites = 4;
	int pageSide = 1024;
	int pages = 6;
	/** @brief As hd::batch::SetTightEdges. */
	int tight = -1;
	float scale = 1;
};

using Scene = std::function<void(SDL_Renderer *, const Pictures &)>;

Result Draw(const View &setup, bool on, const Scene &scene)
{
	Result result;
	SDL_Surface *screen = SDL_CreateRGBSurfaceWithFormat(0, setup.width, setup.height, 32, SDL_PIXELFORMAT_ARGB8888);
	SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(screen);
	EXPECT_NE(renderer, nullptr);
	if (renderer == nullptr)
		return result;
	batch::SetPageLimits(setup.pageSide, setup.pages);
	batch::SetTightEdges(setup.tight);
	// The switch is thrown before the pictures are made, as in the game.
	batch::BeginFrame(renderer, on);
	batch::EndFrame(renderer);
	const Pictures pictures = MakePictures(renderer, setup.tiles, setup.sheets, setup.sprites);
	for (SDL_Texture *texture : pictures.tiles)
		result.inAtlas += batch::Has(texture) ? 1 : 0;
	for (SDL_Texture *texture : pictures.sheets)
		result.inAtlas += batch::Has(texture) ? 1 : 0;
	for (SDL_Texture *texture : pictures.sprites)
		EXPECT_FALSE(batch::Has(texture));
	result.pages = batch::PageCount();
	SDL_SetRenderDrawColor(renderer, 23, 31, 47, 255);
	SDL_RenderClear(renderer);
	SDL_RenderSetScale(renderer, setup.scale, setup.scale);
	const auto start = std::chrono::steady_clock::now();
	batch::BeginFrame(renderer, on);
	scene(renderer, pictures);
	batch::EndFrame(renderer);
	SDL_RenderFlush(renderer);
	result.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	result.stats = batch::LastFrame();
	result.pixels.assign(static_cast<const uint8_t *>(screen->pixels), static_cast<const uint8_t *>(screen->pixels) + static_cast<size_t>(screen->pitch) * screen->h);
	for (const std::vector<SDL_Texture *> *list : { &pictures.tiles, &pictures.sheets, &pictures.sprites }) {
		for (SDL_Texture *texture : *list)
			batch::DestroyTexture(texture);
	}
	batch::Release(true);
	SDL_DestroyRenderer(renderer);
	SDL_FreeSurface(screen);
	return result;
}

/** @brief How many pixels differ between two results, and where the first is. */
int Differing(const Result &a, const Result &b, int width, std::string &first)
{
	int count = 0;
	const size_t pixels = std::min(a.pixels.size(), b.pixels.size()) / 4;
	for (size_t i = 0; i < pixels; i++) {
		if (std::memcmp(a.pixels.data() + i * 4, b.pixels.data() + i * 4, 4) == 0)
			continue;
		if (count++ == 0) {
			char text[128];
			std::snprintf(text, sizeof(text), "first at (%d, %d): %02x%02x%02x%02x against %02x%02x%02x%02x", static_cast<int>(i % static_cast<size_t>(width)), static_cast<int>(i / static_cast<size_t>(width)),
			    a.pixels[i * 4 + 3], a.pixels[i * 4 + 2], a.pixels[i * 4 + 1], a.pixels[i * 4], b.pixels[i * 4 + 3], b.pixels[i * 4 + 2], b.pixels[i * 4 + 1], b.pixels[i * 4]);
			first = text;
		}
	}
	return count;
}

/** @brief Draws a scene with the batching off and on and expects the same picture. Returns both results. */
std::array<Result, 2> ExpectSame(const View &setup, const Scene &scene, const char *what)
{
	std::array<Result, 2> results { Draw(setup, false, scene), Draw(setup, true, scene) };
	EXPECT_EQ(results[0].pixels.size(), results[1].pixels.size());
	EXPECT_FALSE(results[0].pixels.empty());
	std::string first;
	const int differing = Differing(results[0], results[1], setup.width, first);
	EXPECT_EQ(differing, 0) << what << ": " << differing << " pixels differ, " << first;
	// Something was drawn at all.
	bool any = false;
	for (size_t i = 0; i + 4 <= results[0].pixels.size() && !any; i += 4)
		any = std::memcmp(results[0].pixels.data() + i, results[0].pixels.data(), 4) != 0;
	EXPECT_TRUE(any) << what;
	EXPECT_EQ(results[0].stats.merged, 0) << what;
	EXPECT_EQ(results[0].inAtlas, 0) << what;
	std::printf("[ batch    ] %s: off %d draws, %d texture switches; on %d draws, %d texture switches (%d merged, %d pictures on %d pages); software renderer %.1f ms off, %.1f ms on\n",
	    what, results[0].stats.draws, results[0].stats.textureSwitches, results[1].stats.draws, results[1].stats.textureSwitches, results[1].stats.merged, results[1].inAtlas, results[1].pages,
	    results[0].milliseconds, results[1].milliseconds);
	return results;
}

SDL_FRect Dest(float x, float y, float toLogical, float width = 32, float height = 32)
{
	// As hd.cpp's DestOf: the world's pixels doubled, times the screen's share of the world.
	return SDL_FRect { x * 2 * toLogical, y * 2 * toLogical, width * 2 * toLogical, height * 2 * toLogical };
}

TEST(HdBatch, FlatTilesInColoursAtWholePixels)
{
	View setup;
	const auto results = ExpectSame(setup, [](SDL_Renderer *renderer, const Pictures &pictures) {
		Random random { 1 };
		for (int y = -1; y < 8; y++) {
			for (int x = -1; x < 12; x++) {
				const SDL_FRect dest = Dest(static_cast<float>(x * 32 - 7), static_cast<float>(y * 32 - 5), 1.F);
				Flat(renderer, pictures.tiles[static_cast<size_t>(random.Below(static_cast<int>(pictures.tiles.size())))], dest, RandomColour(random, random.Below(4) == 0));
			}
		}
	}, "flat tiles, whole pixels");
	EXPECT_EQ(results[0].stats.draws, 9 * 13);
	EXPECT_EQ(results[1].stats.draws, 1);
	EXPECT_EQ(results[1].inAtlas, 80);
}

TEST(HdBatch, FlatTilesBetweenPixels)
{
	for (const float toLogical : { 0.5F, 0.9F, 1.25F, 1.7916666F, 2.F / 3.F, 3.F }) {
		View setup;
		ExpectSame(setup, [toLogical](SDL_Renderer *renderer, const Pictures &pictures) {
			Random random { 2 };
			// Rows half a tile apart and overlapping, as the game's tiles lie; off every edge of the screen.
			for (int row = -2; row < 40; row++) {
				for (int column = -2; column < 14; column++) {
					const float x = static_cast<float>(column * 64 + (row & 1) * 32) - 40.5F;
					const float y = static_cast<float>(row * 16) - 21.25F;
					Flat(renderer, pictures.tiles[static_cast<size_t>(random.Below(static_cast<int>(pictures.tiles.size())))], Dest(x, y, toLogical), RandomColour(random));
					Flat(renderer, pictures.tiles[static_cast<size_t>(random.Below(static_cast<int>(pictures.tiles.size())))], Dest(x + 32, y, toLogical), RandomColour(random, random.Below(5) == 0));
				}
			}
		}, "flat tiles between pixels");
	}
}

/** @brief Every kind of tile draw the lit HD world makes, with sprites and fills between that end a batch. */
void MixedScene(SDL_Renderer *renderer, const Pictures &pictures, float toLogical, uint32_t seed, bool sprites)
{
	Random random { seed };
	const auto tile = [&] { return pictures.tiles[static_cast<size_t>(random.Below(static_cast<int>(pictures.tiles.size())))]; };
	const auto sheet = [&] { return pictures.sheets[static_cast<size_t>(random.Below(static_cast<int>(pictures.sheets.size())))]; };
	for (int row = -2; row < 30; row++) {
		for (int column = -1; column < 8; column++) {
			const float x = static_cast<float>(column * 64 + (row & 1) * 32) - 13.F;
			const float y = static_cast<float>(row * 16) - 9.F;
			for (int half = 0; half < 2; half++) {
				const SDL_FRect dest = Dest(x + static_cast<float>(half * 32), y, toLogical);
				std::array<SDL_Vertex, 8> made;
				std::array<int, 18> order;
				int orderCount = 0;
				switch (random.Below(9)) {
				case 0: Flat(renderer, tile(), dest, RandomColour(random)); break;
				case 1: FlatWithShape(renderer, tile(), sheet(), dest, random); break;
				case 2: {
					SDL_Texture *texture = tile();
					Flat(renderer, texture, dest, RandomColour(random));
					FlatAdded(renderer, texture, dest, RandomColour(random));
				} break;
				case 3: Gradient(renderer, tile(), dest, random, 2 + random.Below(3), false); break;
				case 4: Gradient(renderer, tile(), dest, random, 2 + random.Below(3), true); break;
				case 5: {
					const int stops = 2 + random.Below(3);
					Gradient(renderer, tile(), dest, random, stops, random.Below(2) == 0, &made, &order, &orderCount);
					ShapeLight(renderer, sheet(), made.data(), stops * 2, order.data(), orderCount, random);
				} break;
				case 6: WallFoot(renderer, tile(), dest, random, half == 1); break;
				case 7: Ground(renderer, tile(), dest, random); break;
				default: {
					// A wall standing up from here, three pieces high.
					for (int up = 0; up < 3; up++)
						FlatWithShape(renderer, tile(), sheet(), Dest(x + static_cast<float>(half * 32), y - static_cast<float>(up * 32), toLogical), random);
				} break;
				}
			}
			if (sprites && random.Below(6) == 0) {
				SDL_Texture *sprite = pictures.sprites[static_cast<size_t>(random.Below(static_cast<int>(pictures.sprites.size())))];
				SDL_SetTextureColorMod(sprite, 200, 180, 160);
				const SDL_FRect dest = Dest(x - 8, y - 40, toLogical, 48, 64);
				batch::CopyF(renderer, sprite, nullptr, &dest);
			}
			if (sprites && random.Below(40) == 0) {
				SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
				SDL_SetRenderDrawColor(renderer, 0, 0, 0, 90);
				const SDL_FRect dest = Dest(x, y, toLogical, 64, 16);
				batch::FillRectF(renderer, &dest);
			}
		}
	}
}

TEST(HdBatch, EveryKindOfTileDrawWithSpritesBetween)
{
	for (const float toLogical : { 1.F, 0.9F, 1.25F, 1.7916666F }) {
		for (const uint32_t seed : { 11U, 12U, 13U }) {
			View setup;
			ExpectSame(setup, [=](SDL_Renderer *renderer, const Pictures &pictures) { MixedScene(renderer, pictures, toLogical, seed, true); }, "every kind of tile draw");
		}
	}
}

TEST(HdBatch, AddedLightWaitsBehindTilesThatOnlyTouchIt)
{
	// At whole pixels two tiles side by side share no pixel, so the light added on one may be drawn after the next:
	// the way a graphics card is treated. (SDL's software renderer is otherwise given room for its rounding.)
	View setup;
	setup.tight = 1;
	const auto results = ExpectSame(setup, [](SDL_Renderer *renderer, const Pictures &pictures) { MixedScene(renderer, pictures, 1.F, 21, false); }, "added light, tight edges");
	View loose = setup;
	loose.tight = 0;
	const auto looser = ExpectSame(loose, [](SDL_Renderer *renderer, const Pictures &pictures) { MixedScene(renderer, pictures, 1.F, 21, false); }, "added light, room at the edges");
	EXPECT_LT(results[1].stats.draws, looser[1].stats.draws);
	EXPECT_LT(looser[1].stats.draws, looser[0].stats.draws);
}

TEST(HdBatch, ClippedAndScaled)
{
	View setup;
	setup.scale = 1.5F;
	ExpectSame(setup, [](SDL_Renderer *renderer, const Pictures &pictures) {
		const SDL_Rect clip { 37, 21, 301, 199 };
		batch::SetClipRect(renderer, &clip);
		MixedScene(renderer, pictures, 0.8F, 31, true);
		batch::SetClipRect(renderer, nullptr);
		MixedScene(renderer, pictures, 0.3F, 32, true);
	}, "clipped and scaled");
}

TEST(HdBatch, ManyPagesAndNoRoomLeft)
{
	// Small pages: the pictures spread over several, and with only two pages allowed some find no room and are drawn
	// from their own textures.
	View setup;
	setup.pageSide = 512;
	setup.pages = 16;
	auto results = ExpectSame(setup, [](SDL_Renderer *renderer, const Pictures &pictures) { MixedScene(renderer, pictures, 1.25F, 41, true); }, "many pages");
	EXPECT_GT(results[1].pages, 2);
	EXPECT_EQ(results[1].inAtlas, 80);
	setup.pages = 2;
	results = ExpectSame(setup, [](SDL_Renderer *renderer, const Pictures &pictures) { MixedScene(renderer, pictures, 1.25F, 41, true); }, "no room left");
	EXPECT_EQ(results[1].pages, 2);
	EXPECT_LT(results[1].inAtlas, 80);
	EXPECT_GT(results[1].inAtlas, 0);
}

TEST(HdBatch, ForgottenAndChangedPicturesAreDrawnFromTheirOwnTexture)
{
	View setup;
	ExpectSame(setup, [](SDL_Renderer *renderer, const Pictures &pictures) {
		Random random { 51 };
		// A picture painted again (as flowing lava is) leaves the atlas, which holds its old pixels.
		SDL_Texture *changed = pictures.tiles[3];
		std::vector<uint8_t> pixels(static_cast<size_t>(TileSide) * TileSide * 4, 200);
		batch::UpdateTexture(changed, nullptr, pixels.data(), TileSide * 4);
		for (int i = 0; i < 60; i++) {
			const SDL_FRect dest = Dest(static_cast<float>(i % 10 * 30), static_cast<float>(i / 10 * 30), 1.F);
			Flat(renderer, i % 3 == 0 ? changed : pictures.tiles[static_cast<size_t>(i % 40)], dest, RandomColour(random));
		}
	}, "changed picture");
}

TEST(HdBatch, AWideViewsWorthOfTiles)
{
	// A view of 3440 by 1440 at the game's zoom: 1720 by 720 of the game's pixels, drawn at twice their size. Tiles
	// are 64 by 32 game pixels in rows 16 apart: about 29 by 47 of them. Each is a floor of two pieces, lit by its
	// shape as often as not; one in five has a wall of two columns four pieces high, and there are 60 sprites. The
	// real count depends on the level; this is to see how the calls compare, not how fast a graphics card draws.
	View setup;
	setup.width = 3440;
	setup.height = 1440;
	setup.tiles = 600;
	setup.sheets = 600;
	setup.sprites = 12;
	setup.pageSide = 4096;
	const Scene scene = [](SDL_Renderer *renderer, const Pictures &pictures) {
		Random random { 61 };
		const auto tile = [&] { return pictures.tiles[static_cast<size_t>(random.Below(static_cast<int>(pictures.tiles.size())))]; };
		const auto sheet = [&] { return pictures.sheets[static_cast<size_t>(random.Below(static_cast<int>(pictures.sheets.size())))]; };
		const int rows = 720 / 16 + 3;
		const int columns = 1720 / 64 + 2;
		// The floor, then what stands on it, back to front.
		for (int row = -1; row < rows; row++) {
			for (int column = -1; column < columns; column++) {
				const float x = static_cast<float>(column * 64 + (row & 1) * 32);
				const float y = static_cast<float>(row * 16);
				for (int half = 0; half < 2; half++) {
					if (random.Below(2) == 0)
						FlatWithShape(renderer, tile(), sheet(), Dest(x + static_cast<float>(half * 32), y, 1.F), random);
					else
						Flat(renderer, tile(), Dest(x + static_cast<float>(half * 32), y, 1.F), RandomColour(random));
				}
			}
		}
		int sprites = 0;
		for (int row = -1; row < rows; row++) {
			for (int column = -1; column < columns; column++) {
				const float x = static_cast<float>(column * 64 + (row & 1) * 32);
				const float y = static_cast<float>(row * 16);
				if (random.Below(5) == 0) {
					for (int half = 0; half < 2; half++) {
						WallFoot(renderer, tile(), Dest(x + static_cast<float>(half * 32), y, 1.F), random, half == 1);
						for (int up = 1; up < 4; up++) {
							std::array<SDL_Vertex, 8> made;
							std::array<int, 18> order;
							int orderCount = 0;
							Gradient(renderer, tile(), Dest(x + static_cast<float>(half * 32), y - static_cast<float>(up * 32), 1.F), random, 2, false, &made, &order, &orderCount);
							if (random.Below(2) == 0)
								ShapeLight(renderer, sheet(), made.data(), 4, order.data(), orderCount, random);
						}
					}
				}
				if (random.Below(23) == 0 && sprites < 60) {
					sprites++;
					SDL_Texture *sprite = pictures.sprites[static_cast<size_t>(random.Below(static_cast<int>(pictures.sprites.size())))];
					const SDL_FRect dest = Dest(x - 8, y - 40, 1.F, 48, 64);
					batch::CopyF(renderer, sprite, nullptr, &dest);
				}
			}
		}
	};
	const auto results = ExpectSame(setup, scene, "a 3440x1440 view's worth, software edges");
	EXPECT_LT(results[1].stats.draws, results[0].stats.draws);
	// As a graphics card is treated: light added on a tile waits behind the tiles that only touch it. (All at whole
	// pixels here, where SDL's software renderer agrees that they share no pixel.)
	setup.tight = 1;
	const auto tight = ExpectSame(setup, scene, "a 3440x1440 view's worth, graphics card edges");
	EXPECT_LT(tight[1].stats.draws, results[1].stats.draws / 2);
}

} // namespace
} // namespace devilution

#else

TEST(HdBatch, NeedsSdl2Geometry)
{
	// SDL_RenderGeometry came with SDL 2.0.18; without it the experiment is not built.
	SUCCEED();
}

#endif
