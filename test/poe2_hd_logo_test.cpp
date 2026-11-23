#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/clx_sprite.hpp"
#include "engine/dx.h"
#include "engine/render/clx_render.hpp"
#include "engine/surface.hpp"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "poe2/hd.hpp"
#include "utils/png.h"
#include "utils/sdl_wrap.h"
#include "utils/surface_to_clx.hpp"

// POE2 Mashup: the menus' logos (ui_art\logo on the title screen, ui_art\smlogo on the main menu) show the HD pack's
// sharp logo over the UI in place of the old one. The new art is see-through where the old one's fire was, so none of
// the old picture may be left under it; without the art the old one shows as it was.

namespace devilution {
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
	}

	static void TearDownTestSuite()
	{
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

	static std::filesystem::path Root;
	SDLSurfaceUniquePtr ui;
	OptionalOwnedClxSpriteList logo;
};

std::filesystem::path HdLogoTest::Root;

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

#endif

} // namespace
} // namespace devilution
