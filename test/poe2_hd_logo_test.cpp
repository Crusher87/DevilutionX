#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/clx_sprite.hpp"
#include "engine/dx.h"
#include "engine/render/clx_render.hpp"
#include "engine/surface.hpp"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "poe2/hd.hpp"
#include "utils/display.h"
#include "utils/png.h"
#include "utils/sdl_wrap.h"
#include "utils/surface_to_clx.hpp"

// POE2 Mashup: the menus' logos (ui_art\logo on the title screen, ui_art\smlogo on the main menu) show the HD pack's
// sharp logo over the UI in place of the old one. The new art is see-through where the old one's fire was, so none of
// the old picture may be left under it; without the art the old one shows as it was.

namespace devilution {

extern SDLSurfaceUniquePtr RendererTextureSurface; // dx.cpp

namespace {

#if !defined(USE_SDL1) && !defined(USE_SDL3)

constexpr int FrameWidth = 8;
constexpr int FrameHeight = 6;
constexpr uint8_t Background = 5;
constexpr uint8_t Letters = 10;
constexpr uint8_t Fire = 20;
constexpr uint8_t Transparent = 250;

/** @brief Writes a small see-through PNG, standing in for a frame of the HD logo. */
void WritePng(const std::filesystem::path &path)
{
	std::filesystem::create_directories(path.parent_path());
	SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, FrameWidth * 4, FrameHeight * 4, 32, SDL_PIXELFORMAT_RGBA32);
	ASSERT_NE(surface, nullptr);
	SDL_FillRect(surface, nullptr, 0);
	ASSERT_EQ(IMG_SavePNG(surface, path.string().c_str()), 0);
	SDL_FreeSurface(surface);
}

/** @brief Writes a live fire's maps (hd\\ui_art\\logo\\fire_maps.png): heat fed along the bottom, under the letters. */
void WriteFireMaps(const std::filesystem::path &path)
{
	constexpr int Width = 128;
	constexpr int Height = 64;
	SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, Width, Height, 32, SDL_PIXELFORMAT_RGBA32);
	ASSERT_NE(surface, nullptr);
	for (int row = 0; row < Height; row++) {
		auto *line = static_cast<uint8_t *>(surface->pixels) + static_cast<ptrdiff_t>(row) * surface->pitch;
		const int y = Height - 1 - row;
		for (int x = 0; x < Width; x++) {
			uint8_t *p = line + static_cast<ptrdiff_t>(x) * 4;
			p[0] = y >= 2 && y < 14 && x >= 16 && x < 112 ? 255 : 0;
			p[1] = static_cast<uint8_t>(std::max(0, (y - 14) * 5));
			p[2] = 0;
			p[3] = 255;
		}
	}
	ASSERT_EQ(IMG_SavePNG(surface, path.string().c_str()), 0);
	SDL_FreeSurface(surface);
}

class HdLogoTest : public ::testing::Test {
public:
	static void SetUpTestSuite()
	{
		std::error_code error;
		Root = std::filesystem::temp_directory_path(error) / ("poe2_hd_logo_test_" + std::to_string(SDL_GetTicks()) + "_" + std::to_string(reinterpret_cast<uintptr_t>(&Root) & 0xFFFF));
		std::filesystem::create_directories(Root / "hd");
		std::ofstream(Root / "hd" / "pack.txt") << "test pack\n";
		// Art for both frames of the title logo, only the first frame of the main menu's, none for Hellfire's.
		WritePng(Root / "hd" / "ui_art" / "logo" / "d0_f00.png");
		WritePng(Root / "hd" / "ui_art" / "logo" / "d0_f01.png");
		WritePng(Root / "hd" / "ui_art" / "smlogo" / "d0_f00.png");
		OverridePaths.insert(OverridePaths.begin(), (Root / "").string());
		gbIsPoe2 = true;
		HeadlessMode = false;
		// One renderer for them all: the HD art's textures are kept by renderer.
		Screen = SDL_CreateRGBSurfaceWithFormat(0, 40, 30, 32, SDL_PIXELFORMAT_RGBA32);
		Renderer = Screen != nullptr ? SDL_CreateSoftwareRenderer(Screen) : nullptr;
	}

	static void TearDownTestSuite()
	{
		hd::ForgetMenuFire();
		if (Renderer != nullptr)
			SDL_DestroyRenderer(Renderer);
		if (Screen != nullptr)
			SDL_FreeSurface(Screen);
		Renderer = nullptr;
		Screen = nullptr;
		HeadlessMode = true;
		gbIsPoe2 = false;
		std::erase(OverridePaths, (Root / "").string());
		std::error_code error;
		std::filesystem::remove_all(Root, error);
	}

	void SetUp() override
	{
		ui = SDLWrap::CreateRGBSurfaceWithFormat(0, 40, 30, 8, SDL_PIXELFORMAT_INDEX8);
		PalSurface = ui.get();
		SDL_FillRect(PalSurface, nullptr, Background);
		// Two frames, each letters over fire, see-through in the corners.
		OwnedSurface frames { FrameWidth, FrameHeight * 2 };
		for (int y = 0; y < FrameHeight * 2; y++) {
			for (int x = 0; x < FrameWidth; x++) {
				const int row = y % FrameHeight;
				uint8_t colour = row < FrameHeight / 2 ? Fire : Letters;
				if ((x == 0 || x == FrameWidth - 1) && (row == 0 || row == FrameHeight - 1))
					colour = Transparent;
				*frames.at(x, y) = colour;
			}
		}
		logo = SurfaceToClx(frames, 2, Transparent);
	}

	void TearDown() override
	{
		hd::DropUiDraws();
		hd::ForgetUiPicture(ClxSpriteList { *logo });
		logo = std::nullopt;
		PalSurface = nullptr;
		ui = nullptr;
	}

	/** @brief Draws a frame at (10, 12) as DiabloUI does (RenderClxSprite: its top left). */
	void Draw(size_t frame)
	{
		RenderClxSprite(Surface(PalSurface), (*logo)[frame], { 10, 12 });
	}

	/** @brief How many UI pixels are not the background. */
	[[nodiscard]] int Drawn() const
	{
		int count = 0;
		for (int y = 0; y < PalSurface->h; y++) {
			const auto *line = static_cast<const uint8_t *>(PalSurface->pixels) + static_cast<ptrdiff_t>(y) * PalSurface->pitch;
			for (int x = 0; x < PalSurface->w; x++)
				count += line[x] != Background ? 1 : 0;
		}
		return count;
	}

	static constexpr int Laid = FrameWidth * FrameHeight - 4;

	/** @brief Shows the frame's HD art (and fire) as the game does, on black; how many pixels of it are lit, in and out of the logo. */
	static void Present(int &inside, int &outside)
	{
		hd::PrepareUiSprites();
		hd::RestoreUiSprites();
		SDL_SetRenderDrawColor(Renderer, 0, 0, 0, 255);
		SDL_RenderClear(Renderer);
		hd::RenderUiSprites(Renderer);
		std::vector<uint32_t> pixels(40 * 30);
		ASSERT_EQ(SDL_RenderReadPixels(Renderer, nullptr, SDL_PIXELFORMAT_RGBA32, pixels.data(), 40 * 4), 0);
		inside = 0;
		outside = 0;
		for (int y = 0; y < 30; y++) {
			for (int x = 0; x < 40; x++) {
				const auto *p = reinterpret_cast<const uint8_t *>(&pixels[static_cast<size_t>(y) * 40 + x]);
				if (p[0] + p[1] + p[2] == 0)
					continue;
				const bool inLogo = x >= 10 && x < 10 + FrameWidth && y >= 12 && y < 12 + FrameHeight;
				(inLogo ? inside : outside)++;
			}
		}
	}

	static std::filesystem::path Root;
	static SDL_Surface *Screen;
	static SDL_Renderer *Renderer;
	SDLSurfaceUniquePtr ui;
	OptionalOwnedClxSpriteList logo;
};

std::filesystem::path HdLogoTest::Root;
SDL_Surface *HdLogoTest::Screen = nullptr;
SDL_Renderer *HdLogoTest::Renderer = nullptr;

TEST_F(HdLogoTest, NamesEachFrameAfterItsPictureAndLayer)
{
	hd::RegisterUiPicture("ui_art\\logo", *logo, hd::UiPictureLayer::Over);
	std::string name;
	hd::UiPictureLayer layer = hd::UiPictureLayer::Under;
	ASSERT_TRUE(hd::UiPictureFrameOf((*logo)[0].pixelData(), name, layer));
	EXPECT_EQ(name, "ui_art/logo/d0_f00");
	EXPECT_EQ(layer, hd::UiPictureLayer::Over);
	ASSERT_TRUE(hd::UiPictureFrameOf((*logo)[1].pixelData(), name, layer));
	EXPECT_EQ(name, "ui_art/logo/d0_f01");
}

TEST_F(HdLogoTest, PicturesGoUnderTheUiUnlessToldOtherwise)
{
	// Panels keep going under the UI, as before.
	hd::RegisterUiPicture("UI_ART\\SMLOGO.CEL", *logo);
	std::string name;
	hd::UiPictureLayer layer = hd::UiPictureLayer::Over;
	ASSERT_TRUE(hd::UiPictureFrameOf((*logo)[0].pixelData(), name, layer));
	EXPECT_EQ(name, "ui_art/smlogo/d0_f00");
	EXPECT_EQ(layer, hd::UiPictureLayer::Under);
}

TEST_F(HdLogoTest, ForgottenPictureIsNoLongerNamed)
{
	hd::RegisterUiPicture("ui_art\\logo", *logo, hd::UiPictureLayer::Over);
	hd::ForgetUiPicture(ClxSpriteList { *logo });
	std::string name;
	hd::UiPictureLayer layer;
	EXPECT_FALSE(hd::UiPictureFrameOf((*logo)[0].pixelData(), name, layer));
	EXPECT_FALSE(hd::UiPictureFrameOf((*logo)[1].pixelData(), name, layer));
}

TEST_F(HdLogoTest, NoneOfTheOldLogoShowsUnderItsArt)
{
	hd::RegisterUiPicture("ui_art\\logo", *logo, hd::UiPictureLayer::Over);
	for (size_t frame = 0; frame < 2; frame++) {
		SDL_FillRect(PalSurface, nullptr, Background);
		Draw(frame);
		ASSERT_EQ(Drawn(), Laid);
		hd::PrepareUiSprites();
		// Letters and fire alike: the frame shows what was under them, and the art goes over it.
		EXPECT_EQ(Drawn(), 0) << "frame " << frame;
		hd::RestoreUiSprites();
		EXPECT_EQ(Drawn(), Laid) << "frame " << frame;
		hd::DropUiDraws();
	}
}

TEST_F(HdLogoTest, FrameWithoutArtShowsTheFirstFramesArt)
{
	// The HD logo is still: a pack with only d0_f00 covers every frame of the game's fire cycle.
	hd::RegisterUiPicture("ui_art\\smlogo", *logo, hd::UiPictureLayer::Over);
	for (size_t frame = 0; frame < 2; frame++) {
		SDL_FillRect(PalSurface, nullptr, Background);
		Draw(frame);
		hd::PrepareUiSprites();
		EXPECT_EQ(Drawn(), 0) << "frame " << frame;
		hd::RestoreUiSprites();
		hd::DropUiDraws();
	}
}

TEST_F(HdLogoTest, PictureWithoutArtKeepsTheOldLogo)
{
	hd::RegisterUiPicture("ui_art\\hf_logo2", *logo, hd::UiPictureLayer::Over);
	Draw(0);
	hd::PrepareUiSprites();
	EXPECT_EQ(Drawn(), Laid);
	hd::RestoreUiSprites();
}

TEST_F(HdLogoTest, UnnamedPictureKeepsTheOldLogo)
{
	std::string name;
	hd::UiPictureLayer layer;
	ASSERT_FALSE(hd::UiPictureFrameOf((*logo)[0].pixelData(), name, layer));
	Draw(0);
	hd::PrepareUiSprites();
	EXPECT_EQ(Drawn(), Laid);
	hd::RestoreUiSprites();
}

TEST_F(HdLogoTest, WithoutHdGraphicsTheOldLogoShows)
{
	hd::RegisterUiPicture("ui_art\\logo", *logo, hd::UiPictureLayer::Over);
	gbIsPoe2 = false;
	Draw(0);
	hd::PrepareUiSprites();
	gbIsPoe2 = true;
	EXPECT_EQ(Drawn(), Laid);
	hd::RestoreUiSprites();
}

TEST_F(HdLogoTest, DroppedDrawsAreNotShownLater)
{
	hd::RegisterUiPicture("ui_art\\logo", *logo, hd::UiPictureLayer::Over);
	Draw(0);
	hd::DropUiDraws();
	hd::PrepareUiSprites();
	EXPECT_EQ(Drawn(), Laid);
	hd::RestoreUiSprites();
}

TEST_F(HdLogoTest, WithoutFireMapsTheHdLogoShowsAlone)
{
	ASSERT_NE(Renderer, nullptr);
	hd::ForgetMenuFire();
	hd::RegisterUiPicture("ui_art\\logo", *logo, hd::UiPictureLayer::Over);
	Draw(0);
	int inside = -1;
	int outside = -1;
	Present(inside, outside);
	// The (see-through) HD art and nothing behind it.
	EXPECT_EQ(inside, 0);
	EXPECT_EQ(outside, 0);
}

TEST_F(HdLogoTest, WithFireMapsTheLogoBurnsBehindItsArt)
{
	ASSERT_NE(Renderer, nullptr);
	const std::filesystem::path maps = Root / "hd" / "ui_art" / "logo" / "fire_maps.png";
	WriteFireMaps(maps);
	hd::ForgetMenuFire();
	hd::RegisterUiPicture("ui_art\\logo", *logo, hd::UiPictureLayer::Over);
	Draw(0);
	int inside = 0;
	int outside = 0;
	Present(inside, outside);
	EXPECT_GT(inside, 0);
	EXPECT_EQ(outside, 0);
	// With the HD Graphics option off there is no HD logo, so no fire either.
	gbIsPoe2 = false;
	Draw(0);
	gbIsPoe2 = true;
	Present(inside, outside);
	EXPECT_EQ(inside, 0);
	hd::ForgetMenuFire();
	std::error_code error;
	std::filesystem::remove(maps, error);
}

TEST_F(HdLogoTest, MenusShowTheHdLogoWhereItHasArt)
{
	// Hellfire's menus load the Diablo logo in place of their own where the pack has its art.
	EXPECT_TRUE(hd::ShowsHdLogo("ui_art/logo"));
	EXPECT_TRUE(hd::ShowsHdLogo("ui_art/smlogo"));
	EXPECT_FALSE(hd::ShowsHdLogo("data/diabsmal"));
	EXPECT_FALSE(hd::ShowsHdLogo("ui_art/hf_logo2"));
	// HD Graphics off: Hellfire keeps its own logos.
	gbIsPoe2 = false;
	EXPECT_FALSE(hd::ShowsHdLogo("ui_art/smlogo"));
	gbIsPoe2 = true;
}

/** @brief How many pixels of the frame sent to the graphics card (RendererTextureSurface) show the old logo. */
int OldLogoSent(const SDL_Surface *sent)
{
	int count = 0;
	for (int y = 0; y < sent->h; y++) {
		const auto *line = static_cast<const uint8_t *>(sent->pixels) + static_cast<ptrdiff_t>(y) * sent->pitch;
		for (int x = 0; x < sent->w; x++) {
			const uint8_t red = line[static_cast<ptrdiff_t>(x) * 4];
			count += red == Letters || red == Fire ? 1 : 0;
		}
	}
	return count;
}

TEST_F(HdLogoTest, UnscaledMenusShowTheHdLogoToo)
{
	// UI Scale 100 (no world surface): in the menus the frame goes up without the old logo, and its HD art is kept for
	// RenderPresent to draw over it; in a game the frame goes up as it is, without HD art.
	ASSERT_NE(Renderer, nullptr);
	SDL_Color colours[256];
	for (int i = 0; i < 256; i++)
		colours[i] = SDL_Color { static_cast<Uint8>(i), static_cast<Uint8>(i), static_cast<Uint8>(i), 255 };
	SDL_SetPaletteColors(PalSurface->format->palette, colours, 0, 256);
	RendererTextureSurface = SDLWrap::CreateRGBSurfaceWithFormat(0, 40, 30, 32, SDL_PIXELFORMAT_RGBA32);
	SDL_Renderer *const oldRenderer = renderer;
	const bool oldDirect = RenderDirectlyToOutputSurface;
	const bool oldRunGame = gbRunGame;
	renderer = Renderer;
	RenderDirectlyToOutputSurface = false;
	hd::RegisterUiPicture("ui_art\\logo", *logo, hd::UiPictureLayer::Over);

	gbRunGame = false;
	Draw(0);
	BltFast(nullptr, nullptr);
	EXPECT_EQ(Drawn(), Laid); // the UI itself is as it was drawn
	EXPECT_EQ(OldLogoSent(RendererTextureSurface.get()), 0);
	hd::PrepareUiSprites(); // the draw is still there for the frame's HD art
	EXPECT_EQ(Drawn(), 0);
	hd::RestoreUiSprites();
	hd::DropUiDraws();

	gbRunGame = true;
	Draw(0);
	BltFast(nullptr, nullptr);
	EXPECT_EQ(OldLogoSent(RendererTextureSurface.get()), Laid);
	hd::PrepareUiSprites(); // dropped
	EXPECT_EQ(Drawn(), Laid);
	hd::RestoreUiSprites();

	gbRunGame = oldRunGame;
	renderer = oldRenderer;
	RenderDirectlyToOutputSurface = oldDirect;
	RendererTextureSurface = nullptr;
}

#endif

} // namespace
} // namespace devilution
