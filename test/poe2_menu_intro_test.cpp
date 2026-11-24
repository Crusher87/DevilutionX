#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "options.h"
#include "poe2/hd.hpp"
#include "poe2/menu_intro.hpp"
#include "utils/display.h"
#include "utils/png.h"

#if !defined(USE_SDL1) && !defined(USE_SDL3)
#include "poe2/menu_intro_sdl.hpp"
#endif

// POE2 Mashup: the main menu's intro (poe2/menu_intro.hpp): its timeline as the approved reference has it, what it
// paints, and when it plays. Its art is stood in for by tiny pictures made here; the real art is never in the tests.

namespace devilution {
namespace {

using namespace intro;

TEST(MenuIntroTimelineTest, TheLogoFliesInAndLands)
{
	EXPECT_EQ(LogoScale(0.5F), 0.F);
	// From behind the viewer: far bigger than it ends.
	EXPECT_GT(LogoScale(FlightStart + 0.01F), 8.F);
	EXPECT_GT(LogoScale(2.F), LogoScale(3.F));
	// It lands at 0.848 of its place, springs back, and drifts out to it over five seconds.
	EXPECT_NEAR(LogoScale(Landing - 0.0001F), 0.848F, 0.01F);
	EXPECT_NEAR(LogoScale(End), 1.F, 0.03F);
	EXPECT_NEAR(LogoScale(Landing + 20.F), 1.F, 0.001F);
}

TEST(MenuIntroTimelineTest, ItFliesInInTheMiddleAndGlidesUpToTheMenu)
{
	// The first frames are all fire: the logo some twenty times its size, pulled back fast.
	EXPECT_GT(LogoScale(FlightStart), 20.F);
	EXPECT_LT(LogoScale(FlightStart + 1.3F), 2.F);
	// In the middle until after the claws and the words, at its place as the menu shows.
	EXPECT_EQ(Glide(Slash + 1.F), 0.F);
	EXPECT_GT(Glide((GlideStart + GlideEnd) / 2.F), 0.4F);
	EXPECT_LT(Glide((GlideStart + GlideEnd) / 2.F), 0.6F);
	EXPECT_FLOAT_EQ(Glide(MenuAt + 0.1F), 1.F);
	EXPECT_FLOAT_EQ(Glide(End), 1.F);
}

TEST(MenuIntroTimelineTest, HitStopsAreShort)
{
	EXPECT_FLOAT_EQ(TimeWarp(Landing - 0.01F), 1.F);
	EXPECT_FLOAT_EQ(TimeWarp(Landing + 0.005F), 0.075F);
	EXPECT_FLOAT_EQ(TimeWarp(Landing + 0.02F), 1.F);
	EXPECT_FLOAT_EQ(TimeWarp(Slash + 0.085F + 0.005F), 0.075F);
	EXPECT_FLOAT_EQ(TimeWarp(Slash + 0.085F + 0.02F), 1.F);
}

TEST(MenuIntroTimelineTest, TheLinesCatchFromTheMiddleOut)
{
	EXPECT_EQ(LineReach(Slash), 0.F);
	EXPECT_EQ(LineReach(Slash + 0.12F), 0.F);
	const float half = LineReach(Slash + 0.12F + 0.225F);
	EXPECT_NEAR(half, 0.75F, 0.001F);
	EXPECT_FLOAT_EQ(LineReach(Slash + 0.12F + 0.45F), 1.F);
	EXPECT_FLOAT_EQ(LineReach(End), 1.F);
}

TEST(MenuIntroTimelineTest, ItEndsOnTheMenuAsItAlwaysIs)
{
	EXPECT_EQ(MenuAlpha(MenuAt - 0.01F), 0.F);
	EXPECT_GT(MenuAlpha(MenuAt + 1.F), 0.F);
	EXPECT_FLOAT_EQ(MenuAlpha(End), 1.F);
	EXPECT_FLOAT_EQ(IntroLook(MenuAt - 0.01F), 1.F);
	EXPECT_FLOAT_EQ(IntroLook(End), 0.F);
	// The logo's fire, fed as the menu's own by the end.
	EXPECT_NEAR(FireAmount(End), 1.F, 0.05F);
	EXPECT_NEAR(BackLight(End), 0.F, 0.0001F);
	EXPECT_NEAR(Trail(End), 0.F, 0.0001F);
}

TEST(MenuIntroTimelineTest, TheLensShakesOnTheHitsAndIsStillAtTheEnd)
{
	const Lens landed = LensAt(Landing + 0.05F, 0, 0);
	EXPECT_GT(std::abs(landed.shakeX) + std::abs(landed.shakeY) + landed.punch, 0.F);
	const Lens raked = LensAt(Slash + 0.05F, 0, 0);
	EXPECT_GT(std::abs(raked.shakeX) + std::abs(raked.shakeY) + raked.punch, 0.F);
	EXPECT_GT(raked.flash, 0.F);
	// Dark just before the claws.
	EXPECT_LT(LensAt(Slash - 0.05F, 0, 0).dim, 0.9F);
	const Lens end = LensAt(End, 0, 0);
	EXPECT_NEAR(end.shakeX, 0.F, 0.01F);
	EXPECT_NEAR(end.shakeY, 0.F, 0.01F);
	EXPECT_NEAR(end.punch, 0.F, 0.001F);
	EXPECT_NEAR(end.zoom, 0.F, 0.001F);
	EXPECT_FLOAT_EQ(end.dim, 1.F);
}

TEST(MenuIntroTimelineTest, SoundsStartAsTheReferenceHasThem)
{
	const auto &cues = SoundCues();
	EXPECT_STREQ(cues[0].file, "apoc.wav");
	EXPECT_FLOAT_EQ(cues[0].at, 0.90F);
	EXPECT_STREQ(cues[1].file, "intro_sfx.wav");
	EXPECT_FLOAT_EQ(cues[1].at, 3.60F);
	EXPECT_FLOAT_EQ(cues[1].fadeIn, 0.35F);
	EXPECT_STREQ(cues[2].file, "diabloa1.wav");
	EXPECT_STREQ(cues[2].alternative, "diabloa2.wav");
	EXPECT_NEAR(cues[2].at, 5.34F, 0.0001F);
	EXPECT_STREQ(cues[3].file, "flamwave.wav");
	EXPECT_NEAR(cues[3].at, 5.55F, 0.0001F);
	for (size_t i = 1; i < cues.size(); i++)
		EXPECT_LT(cues[i - 1].at, cues[i].at);
	EXPECT_LT(cues[3].volume, cues[0].volume);
}

TEST(MenuIntroTimelineTest, RandomNumbersAreTheReferences)
{
	// mulberry32, as the page's: the same seed, the same numbers.
	Random a(77);
	Random b(77);
	for (int i = 0; i < 100; i++) {
		const float x = a.Next();
		EXPECT_EQ(x, b.Next());
		EXPECT_GE(x, 0.F);
		EXPECT_LT(x, 1.F);
	}
}

TEST(MenuIntroTimelineTest, NoiseIsSmoothAndBounded)
{
	float lowest = 0;
	float highest = 0;
	for (int i = 0; i < 400; i++) {
		const float x = static_cast<float>(i) * 0.137F;
		const float v = ValueNoise(x, x * 0.7F, 1.F);
		lowest = std::min(lowest, v);
		highest = std::max(highest, v);
		EXPECT_NEAR(v, ValueNoise(x + 0.001F, x * 0.7F, 1.F), 0.05F);
	}
	EXPECT_GE(lowest, -1.8F);
	EXPECT_LE(highest, 1.8F);
	EXPECT_LT(lowest, 0.F);
	EXPECT_GT(highest, 0.F);
}

/** @brief A stand-in for the subtitle's metal (opaque in a band across the middle) or its mask (G: a glow round it). */
std::vector<uint8_t> StandIn(int width, int height, bool mask)
{
	std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4, 0);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			uint8_t *p = &rgba[(static_cast<size_t>(y) * width + x) * 4];
			const bool letter = y >= height / 4 && y < height * 3 / 4 && x >= width / 8 && x < width * 7 / 8;
			if (mask) {
				p[1] = 200;
				p[3] = 255;
			} else if (letter) {
				p[0] = p[1] = p[2] = 160;
				p[3] = 255;
			}
		}
	}
	return rgba;
}

TEST(MenuIntroSubtitleTest, WordsCoolFromWhiteHotToMetal)
{
	const std::vector<uint8_t> steel = StandIn(36, 4, false);
	const std::vector<uint8_t> mask = StandIn(36, 4, true);
	Subtitle subtitle;
	EXPECT_FALSE(subtitle.Load(nullptr, 36, 4, 36 * 4, mask.data(), 36, 4, 36 * 4, 90, 10));
	ASSERT_TRUE(subtitle.Load(steel.data(), 36, 4, 36 * 4, mask.data(), 36, 4, 36 * 4, 90, 10));
	EXPECT_FALSE(Subtitle::Shows(-0.1F));
	EXPECT_TRUE(Subtitle::Shows(0.1F));
	std::vector<uint8_t> hot(90 * 10 * 4);
	std::vector<uint8_t> cold(90 * 10 * 4);
	subtitle.Paint(hot.data(), 90 * 4, Slash + 0.1F, 0.1F, 0.5F);
	subtitle.Paint(cold.data(), 90 * 4, 30.F, 30.F, 0.5F);
	// The middle of the words: white-hot first (blue nearly as high as red), dull metal later.
	const uint8_t *h = &hot[(5 * 90 + 45) * 4];
	const uint8_t *c = &cold[(5 * 90 + 45) * 4];
	EXPECT_GT(h[3], 200);
	EXPECT_GT(c[3], 200);
	EXPECT_GT(h[0] + h[1] + h[2], c[0] + c[1] + c[2]);
	EXPECT_GT(h[2], c[2]);
	EXPECT_LT(c[0], 250);
	// See-through off the metal (its glow, alpha premultiplied, only adds light there).
	EXPECT_EQ(cold[(0 * 90 + 45) * 4 + 3], 0);
	// The pop as it is stamped settles to its box.
	EXPECT_GT(Subtitle::Pop(0.02F), 1.F);
	EXPECT_NEAR(Subtitle::Pop(5.F), 1.F, 0.001F);
}

TEST(MenuIntroClawsTest, GougesShowOnlyAfterTheClawsAndFade)
{
	Claws claws;
	claws.Prepare(0.05F);
	ASSERT_TRUE(claws.Prepared());
	std::vector<uint8_t> rgba(static_cast<size_t>(claws.Width()) * claws.Height() * 4);
	EXPECT_FALSE(claws.Paint(rgba.data(), claws.Width() * 4, Slash - 0.1F, -0.1F, 0.5F));
	ASSERT_TRUE(claws.Paint(rgba.data(), claws.Width() * 4, Slash + 0.2F, 0.2F, 0.5F));
	int lit = 0;
	for (size_t i = 0; i < rgba.size(); i += 4)
		lit += rgba[i] > 0 ? 1 : 0;
	EXPECT_GT(lit, 0);
	EXPECT_FALSE(claws.Paint(rgba.data(), claws.Width() * 4, Slash + Claws::Lasts + 0.1F, Claws::Lasts + 0.1F, 0.5F));
}

TEST(MenuIntroLinesTest, LinesFeedFromTheMiddleOut)
{
	const fire::FireSource source = LineSource();
	ASSERT_EQ(source.width, LineGridWidth);
	ASSERT_EQ(source.height, LineGridHeight);
	ASSERT_EQ(source.cool.size(), static_cast<size_t>(LineGridWidth) * LineGridHeight);
	// Cold until they catch.
	std::vector<float> feed;
	LineFeed(feed, 0.F);
	EXPECT_EQ(*std::max_element(feed.begin(), feed.end()), 0.F);
	const auto at = [&](float x, float y) {
		const int i = static_cast<int>(x) - LineGridX;
		const int j = static_cast<int>(y) - LineGridY;
		return feed[static_cast<size_t>(j) * LineGridWidth + i];
	};
	LineFeed(feed, 0.3F);
	EXPECT_GT(at(LineMiddleX, TopLineY), 0.F);
	EXPECT_EQ(at(LineMiddleX + TopLineHalfWidth - 10.F, TopLineY), 0.F);
	LineFeed(feed, 1.F);
	EXPECT_GT(at(LineMiddleX + TopLineHalfWidth - 10.F, TopLineY), 0.F);
	EXPECT_GT(at(LineMiddleX - BottomLineHalfWidth + 10.F, BottomLineY), 0.F);
	// Nothing past the short line's ends, nor between the lines.
	EXPECT_EQ(at(LineMiddleX + BottomLineHalfWidth + 40.F, BottomLineY), 0.F);
	EXPECT_EQ(at(LineMiddleX, (TopLineY + BottomLineY) / 2.F), 0.F);
	// The short line's flames are cooled hard over it, so they stay under the words.
	const auto coolAt = [&](float y) {
		return source.cool[static_cast<size_t>(static_cast<int>(y) - LineGridY) * LineGridWidth + 550];
	};
	EXPECT_GT(coolAt(BottomLineY + 20.F), 2.F);
	EXPECT_EQ(coolAt(TopLineY + 20.F), 0.F);
}

TEST(MenuIntroLinesTest, TheirFireBurnsAlongTheLines)
{
	fire::FireSource source = LineSource();
	LineFeed(source.feed, 1.F);
	fire::LiveFire fire(std::move(source), fire::FireSettings {}, 5, -1, fire::LineRecipe());
	fire.WarmUp(1.F);
	const std::vector<float> &heat = fire.Heat();
	const int w = fire.Width();
	const int h = fire.Height();
	ASSERT_GT(w, 0);
	ASSERT_GT(h, 0);
	const auto row = [&](float boxY) {
		const int j = std::clamp(static_cast<int>((boxY - LineGridY) / LineGridHeight * h), 0, h - 1);
		float sum = 0;
		for (int i = 0; i < w; i++)
			sum += heat[static_cast<size_t>(j) * w + i];
		return sum;
	};
	EXPECT_GT(row(TopLineY + 4.F), 0.F);
	EXPECT_GT(row(BottomLineY + 4.F), 0.F);
	// Far over the top line it has burnt out.
	EXPECT_LT(row(LineGridY + LineGridHeight - 4.F), row(TopLineY + 4.F));
}

TEST(MenuIntroEmbersTest, TheLandingAndTheClawsThrowEmbers)
{
	Embers embers;
	embers.SetScreen(0, 0, 1600, 900);
	Random random;
	embers.Step(1.F / 60.F, 0.5F, random);
	const size_t before = embers.Count();
	embers.Landing(random);
	EXPECT_GT(embers.Count(), before + 100);
	const size_t landed = embers.Count();
	embers.Rake(random);
	EXPECT_GT(embers.Count(), landed);
	embers.Step(1.F / 60.F, Slash + 0.02F, random);
	std::vector<Streak> streaks;
	embers.Streaks(streaks, 1.F);
	EXPECT_FALSE(streaks.empty());
	// They burn out.
	for (int i = 0; i < 60 * 8; i++)
		embers.Step(1.F / 60.F, End + 5.F, random);
	EXPECT_LT(embers.Count(), landed / 4);
}

#if !defined(USE_SDL1) && !defined(USE_SDL3)

/** @brief Writes a small RGBA PNG: opaque in the middle, see-through round it. */
void WritePng(const std::filesystem::path &path, int width, int height, bool maps = false)
{
	std::filesystem::create_directories(path.parent_path());
	SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
	ASSERT_NE(surface, nullptr);
	for (int y = 0; y < height; y++) {
		auto *line = static_cast<uint8_t *>(surface->pixels) + static_cast<ptrdiff_t>(y) * surface->pitch;
		for (int x = 0; x < width; x++) {
			uint8_t *p = line + static_cast<ptrdiff_t>(x) * 4;
			const bool middle = x >= width / 4 && x < width * 3 / 4 && y >= height / 4 && y < height * 3 / 4;
			if (maps) {
				// Heat fed along the bottom, under the letters.
				p[0] = y >= height * 3 / 4 ? 255 : 0;
				p[1] = static_cast<uint8_t>((height - 1 - y) * 255 / height);
				p[2] = 0;
				p[3] = 255;
			} else {
				p[0] = p[1] = p[2] = middle ? 200 : 0;
				p[3] = middle ? 255 : 0;
			}
		}
	}
	ASSERT_EQ(IMG_SavePNG(surface, path.string().c_str()), 0);
	SDL_FreeSurface(surface);
}

class MenuIntroTest : public ::testing::Test {
public:
	static void SetUpTestSuite()
	{
		std::error_code error;
		Root = std::filesystem::temp_directory_path(error) / ("poe2_menu_intro_test_" + std::to_string(SDL_GetTicks()) + "_" + std::to_string(reinterpret_cast<uintptr_t>(&Root) & 0xFFFF));
		std::filesystem::create_directories(Root / "hd");
		std::ofstream(Root / "hd" / "pack.txt") << "test pack\n";
		WritePng(Root / "hd" / "ui_art" / "logo" / "d0_f00.png", 44, 18);
		WritePng(Root / "hd" / "ui_art" / "smlogo" / "d0_f00.png", 44, 18);
		WritePng(Root / "hd" / "ui_art" / "logo" / "fire_maps.png", 110, 44, true);
		WritePng(Root / "hd" / "ui_art" / "intro" / "steel.png", 36, 4);
		WritePng(Root / "hd" / "ui_art" / "intro" / "textmaps.png", 36, 4);
		OverridePaths.insert(OverridePaths.begin(), (Root / "").string());
		gbIsPoe2 = true;
		HeadlessMode = false;
		Screen = SDL_CreateRGBSurfaceWithFormat(0, Width, Height, 32, SDL_PIXELFORMAT_RGBA32);
		Renderer = Screen != nullptr ? SDL_CreateSoftwareRenderer(Screen) : nullptr;
	}

	static void TearDownTestSuite()
	{
		hd::intro_sdl::EndForTest();
		hd::FinishMenuIntro();
		hd::ForgetMenuFire();
		renderer = nullptr;
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
		renderer = Renderer;
	}

	void TearDown() override
	{
		renderer = nullptr;
	}

	/** @brief How bright the screen is, all told. */
	static uint64_t Light()
	{
		std::vector<uint32_t> pixels(static_cast<size_t>(Width) * Height);
		if (SDL_RenderReadPixels(Renderer, nullptr, SDL_PIXELFORMAT_RGBA32, pixels.data(), Width * 4) != 0)
			return 0;
		uint64_t sum = 0;
		for (const uint32_t pixel : pixels) {
			const auto *p = reinterpret_cast<const uint8_t *>(&pixel);
			sum += p[0] + p[1] + p[2];
		}
		return sum;
	}

	/** @brief Where the main menu's logo lands, as the reference has it on its 1600 x 900 stage. */
	static SDL_FRect Picture()
	{
		const float k = Width / 1600.F;
		return SDL_FRect { (800.F - 550.F * 0.9F) * k, (300.F - (560.F - 278.F) * 0.9F) * k, 1100.F * 0.9F * k, 432.F * 0.9F * k };
	}

	static constexpr int Width = 160;
	static constexpr int Height = 90;
	static std::filesystem::path Root;
	static SDL_Surface *Screen;
	static SDL_Renderer *Renderer;
};

std::filesystem::path MenuIntroTest::Root;
SDL_Surface *MenuIntroTest::Screen = nullptr;
SDL_Renderer *MenuIntroTest::Renderer = nullptr;

TEST_F(MenuIntroTest, WantedOnlyWithItsArtAndTheOption)
{
	ASSERT_NE(Renderer, nullptr);
	EXPECT_TRUE(hd::MenuIntroWanted());
	// Turned off in the ini.
	GetOptions().StartUp.menuIntro.SetValue(false);
	EXPECT_FALSE(hd::MenuIntroWanted());
	GetOptions().StartUp.menuIntro.SetValue(true);
	// HD Graphics off: no HD logo, no intro.
	gbIsPoe2 = false;
	EXPECT_FALSE(hd::MenuIntroWanted());
	gbIsPoe2 = true;
	// Without its art.
	const std::filesystem::path steel = Root / "hd" / "ui_art" / "intro" / "steel.png";
	const std::filesystem::path moved = Root / "steel.png";
	std::filesystem::rename(steel, moved);
	EXPECT_FALSE(hd::MenuIntroWanted());
	std::filesystem::rename(moved, steel);
	EXPECT_TRUE(hd::MenuIntroWanted());
	// Nor without a renderer.
	renderer = nullptr;
	EXPECT_FALSE(hd::MenuIntroWanted());
}

TEST_F(MenuIntroTest, ItPlaysDarkThenBurning)
{
	ASSERT_NE(Renderer, nullptr);
	ASSERT_TRUE(hd::intro_sdl::RenderForTest(Renderer, nullptr, Picture(), 0.3F));
	const uint64_t dark = Light();
	ASSERT_TRUE(hd::intro_sdl::RenderForTest(Renderer, nullptr, Picture(), 6.F));
	const uint64_t burning = Light();
	EXPECT_GT(burning, dark * 4 + 1000);
	hd::intro_sdl::EndForTest();
}

TEST_F(MenuIntroTest, SkippingEndsItAndItPlaysOnce)
{
	ASSERT_NE(Renderer, nullptr);
	ASSERT_TRUE(hd::MenuIntroWanted());
	hd::StartMenuIntro();
	ASSERT_TRUE(hd::MenuIntroPlaying());
	EXPECT_TRUE(hd::MenuIntroTakesInput());
	// A frame of it: drawn into its own picture, then onto the screen.
	ASSERT_TRUE(hd::BeginMenuIntroFrame(Renderer));
	SDL_SetRenderDrawColor(Renderer, 0, 0, 0, 255);
	SDL_RenderClear(Renderer);
	hd::EndMenuIntroFrame(Renderer);
	EXPECT_EQ(SDL_GetRenderTarget(Renderer), nullptr);
	hd::SkipMenuIntro();
	EXPECT_FALSE(hd::MenuIntroPlaying());
	EXPECT_FALSE(hd::MenuIntroTakesInput());
	EXPECT_FALSE(hd::BeginMenuIntroFrame(Renderer));
	// Once a launch.
	EXPECT_FALSE(hd::MenuIntroWanted());
	hd::StartMenuIntro();
	EXPECT_FALSE(hd::MenuIntroPlaying());
	hd::FinishMenuIntro();
}

/**
 * @brief Not a test: with POE2_INTRO_DUMP set to a folder holding the real art (hd\...), renders the reference's
 * moments to PNGs there with a GPU renderer, and times its frames at POE2_INTRO_DUMP_SIZE (e.g. 3440x1440).
 */
TEST_F(MenuIntroTest, DumpFrames)
{
	const char *dump = std::getenv("POE2_INTRO_DUMP");
	if (dump == nullptr)
		GTEST_SKIP() << "POE2_INTRO_DUMP not set";
	const std::filesystem::path root = dump;
	OverridePaths.insert(OverridePaths.begin(), (root / "").string());
	int w = 1600;
	int h = 900;
	if (const char *size = std::getenv("POE2_INTRO_DUMP_SIZE"); size != nullptr)
		std::sscanf(size, "%dx%d", &w, &h);
	ASSERT_EQ(SDL_InitSubSystem(SDL_INIT_VIDEO), 0) << SDL_GetError();
	SDL_Window *window = SDL_CreateWindow("intro", 0, 0, w, h, SDL_WINDOW_HIDDEN);
	ASSERT_NE(window, nullptr) << SDL_GetError();
	SDL_Renderer *gpu = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
	ASSERT_NE(gpu, nullptr) << SDL_GetError();
	SDL_RendererInfo info;
	SDL_GetRendererInfo(gpu, &info);
	std::printf("renderer: %s\n", info.name);
	// The game's logical size: the reference's stage's height, as wide as the window is (21:9 too).
	const int lw = 900 * w / h;
	SDL_RenderSetLogicalSize(gpu, lw, 900);
	hd::ForgetMenuFire();
	renderer = gpu;
	const SDL_FRect picture { static_cast<float>(lw) / 2.F - 550.F * 0.9F, 300.F - (560.F - 278.F) * 0.9F, 1100.F * 0.9F, 432.F * 0.9F };
	std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
	for (const float t : { 0.5F, 1.05F, 1.2F, 1.5F, 2.F, 3.F, 3.6F, 5.5F, 6.F, 8.F, 8.2F, 9.F, 11.5F }) {
		ASSERT_TRUE(hd::intro_sdl::RenderForTest(gpu, nullptr, picture, t));
		ASSERT_EQ(SDL_RenderReadPixels(gpu, nullptr, SDL_PIXELFORMAT_RGBA32, pixels.data(), w * 4), 0) << SDL_GetError();
		SDL_Surface *shot = SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
		char name[64];
		std::snprintf(name, sizeof(name), "game_t%.2f.png", static_cast<double>(t));
		IMG_SavePNG(shot, (root / name).string().c_str());
		SDL_FreeSurface(shot);
		SDL_RenderPresent(gpu);
	}
	hd::intro_sdl::EndForTest();
	// Its frames' cost through the flight, and through the busy part, landing to menu, as the game draws them (each
	// finished on the card).
	for (const auto &[from, to] : { std::pair { 1.F, 3.F }, std::pair { 3.F, 9.F } }) {
		ASSERT_TRUE(hd::intro_sdl::RenderForTest(gpu, nullptr, picture, from));
		double worst = 0;
		double total = 0;
		int frames = 0;
		for (float t = from + 1.F / 60.F; t < to; t += 1.F / 60.F) {
			const auto start = std::chrono::steady_clock::now();
			hd::intro_sdl::RenderForTest(gpu, nullptr, picture, t);
			const SDL_Rect one { 0, 0, 1, 1 };
			SDL_RenderReadPixels(gpu, &one, SDL_PIXELFORMAT_RGBA32, pixels.data(), 4);
			const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			worst = std::max(worst, ms);
			total += ms;
			frames++;
		}
		std::printf("%dx%d, %.1f to %.1f s: %.2f ms a frame on average, %.2f ms at worst (%d frames)\n", w, h, static_cast<double>(from), static_cast<double>(to), total / frames, worst, frames);
		hd::intro_sdl::EndForTest();
	}
	hd::ForgetMenuFire();
	renderer = Renderer;
	SDL_DestroyRenderer(gpu);
	SDL_DestroyWindow(window);
	std::erase(OverridePaths, (root / "").string());
}

#endif

} // namespace
} // namespace devilution
