#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "options.h"
#include "poe2/vfx.hpp"
#include "poe2/weather.hpp"

namespace devilution {
namespace {

class Poe2WeatherTest : public ::testing::Test {
public:
	void SetUp() override
	{
		gbIsPoe2 = true;
		HeadlessMode = false;
		leveltype = DTYPE_TOWN;
		weather::Reseed(7);
		vfx::Clear();
	}

	void TearDown() override
	{
		weather::Force(std::nullopt);
		weather::Reseed(1);
		vfx::Clear();
		HeadlessMode = true;
		leveltype = DTYPE_NONE;
	}

	/** @brief Runs the weather for the given seconds in frame-sized steps, checking its numbers stay sane. */
	static void Run(float seconds)
	{
		for (float t = 0; t < seconds * 1000; t += 40) {
			weather::Advance(40);
			const weather::State &now = weather::Current();
			ASSERT_GE(now.strength, 0.F);
			ASSERT_LE(now.strength, 1.F);
			ASSERT_GE(now.wind, -1.F);
			ASSERT_LE(now.wind, 1.F);
		}
	}
};

TEST_F(Poe2WeatherTest, TheGameOpensDry)
{
	weather::Advance(40);
	EXPECT_EQ(weather::Current().strength, 0.F);
}

TEST_F(Poe2WeatherTest, ShowersComeAndGoInTown)
{
	Run(60 * 60);
	const float wet = weather::WetShare();
	EXPECT_GT(wet, 0.1F) << "an hour in town with no rain";
	EXPECT_LT(wet, 0.7F) << "an hour in town mostly wet";
	EXPECT_EQ(weather::Current().kind, weather::Kind::Rain);
}

TEST_F(Poe2WeatherTest, DungeonsStayDry)
{
	leveltype = DTYPE_CATHEDRAL;
	Run(60 * 60);
	EXPECT_EQ(weather::WetShare(), 0.F);
}

TEST_F(Poe2WeatherTest, SwitchingWeatherOffStopsIt)
{
	weather::Force(weather::Kind::Rain, 1);
	Run(10);
	ASSERT_GT(weather::Current().strength, 0.9F);
	GetOptions().Graphics.weather.SetValue(false);
	Run(1);
	EXPECT_EQ(weather::Current().strength, 0.F);
	GetOptions().Graphics.weather.SetValue(true);
}

TEST_F(Poe2WeatherTest, ALevelWithNoWeatherStaysClear)
{
	weather::SetPreset(weather::Kind::Clear);
	Run(60 * 60);
	EXPECT_EQ(weather::WetShare(), 0.F);
	weather::SetPreset(weather::Kind::Rain);
}

TEST_F(Poe2WeatherTest, GoingUnderground_StopsTheRainAtOnce)
{
	weather::Force(weather::Kind::Rain, 1);
	Run(60);
	EXPECT_GT(weather::Current().strength, 0.9F);
	leveltype = DTYPE_CATHEDRAL;
	Run(1);
	EXPECT_EQ(weather::Current().strength, 0.F);
}

TEST_F(Poe2WeatherTest, ASkillCanCallAShowerThatPasses)
{
	weather::Request(weather::Kind::Rain, 0.8F, 60);
	Run(45);
	EXPECT_NEAR(weather::Current().strength, 0.8F, 0.01F);
	EXPECT_EQ(weather::Current().kind, weather::Kind::Rain);
	EXPECT_NE(weather::Text().find("Called by a skill"), std::string::npos) << weather::Text();
	Run(60);
	// After its minute, the schedule is back.
	EXPECT_EQ(weather::Text().find("Called by a skill"), std::string::npos) << weather::Text();
}

TEST_F(Poe2WeatherTest, RainDiesDownBeforeSnowStarts)
{
	weather::Request(weather::Kind::Rain, 1, 600);
	Run(40);
	ASSERT_GT(weather::Current().strength, 0.9F);
	weather::Request(weather::Kind::Snow, 1, 600);
	Run(5);
	EXPECT_EQ(weather::Current().kind, weather::Kind::Rain);
	EXPECT_LT(weather::Current().strength, 0.9F);
	Run(60);
	EXPECT_EQ(weather::Current().kind, weather::Kind::Snow);
	EXPECT_GT(weather::Current().strength, 0.9F);
}

TEST_F(Poe2WeatherTest, TheConsolesLookIsThereAtOnce)
{
	// Curtis 2026-10-21: weather("rain", 0.9) answered "Dry right now", as the rain was still fading in.
	weather::Force(weather::Kind::Rain, 0.9F);
	weather::Advance(1);
	EXPECT_EQ(weather::Current().kind, weather::Kind::Rain);
	EXPECT_NEAR(weather::Current().strength, 0.9F, 0.01F);
	EXPECT_NE(weather::Text().find("Rain at 90%"), std::string::npos) << weather::Text();
	weather::Force(weather::Kind::Snow, 0.8F);
	weather::Advance(1);
	EXPECT_EQ(weather::Current().kind, weather::Kind::Snow);
	EXPECT_NEAR(weather::Current().strength, 0.8F, 0.01F);
	weather::Force(weather::Kind::Clear);
	weather::Advance(1);
	EXPECT_EQ(weather::Current().strength, 0.F);
	EXPECT_NE(weather::Text().find("Dry right now"), std::string::npos) << weather::Text();
}

TEST_F(Poe2WeatherTest, HandingTheConsolesLookBackEasesOff)
{
	weather::Force(weather::Kind::Rain, 0.9F);
	weather::Advance(1);
	// The schedule opens on a dry spell, so the forced rain eases off rather than stopping or staying.
	weather::Force(std::nullopt);
	weather::Advance(1);
	EXPECT_NE(weather::Text().find("easing off"), std::string::npos) << weather::Text();
	EXPECT_NE(weather::Text().find("Dry spell"), std::string::npos) << weather::Text();
	Run(60);
	EXPECT_LT(weather::Current().strength, 0.9F);
}

TEST_F(Poe2WeatherTest, TheConsolesLookWaitsForTheSky)
{
	leveltype = DTYPE_CATHEDRAL;
	weather::Force(weather::Kind::Rain, 1);
	weather::Advance(1);
	EXPECT_EQ(weather::Current().strength, 0.F);
	EXPECT_NE(weather::Text().find("No sky here"), std::string::npos) << weather::Text();
	leveltype = DTYPE_TOWN;
	Run(5);
	EXPECT_GT(weather::Current().strength, 0.9F);
}

TEST_F(Poe2WeatherTest, TheSameSeedGivesTheSameWeather)
{
	weather::Reseed(99);
	Run(20 * 60);
	const float first = weather::WetShare();
	const float strength = weather::Current().strength;
	weather::Reseed(99);
	Run(20 * 60);
	EXPECT_EQ(weather::WetShare(), first);
	EXPECT_EQ(weather::Current().strength, strength);
}

TEST_F(Poe2WeatherTest, ADownpourFlashesWithLightning)
{
	weather::Force(weather::Kind::Rain, 1);
	bool flashed = false;
	for (int i = 0; i < 60 * 25 * 5 && !flashed; i++) {
		weather::Advance(40);
		flashed = weather::Current().flash > 0;
	}
	EXPECT_TRUE(flashed);
}

TEST_F(Poe2WeatherTest, LightRainNeverFlashes)
{
	weather::Force(weather::Kind::Rain, 0.4F);
	for (int i = 0; i < 25 * 60 * 10; i++) {
		weather::Advance(40);
		ASSERT_EQ(weather::Current().flash, 0.F);
	}
}

TEST_F(Poe2WeatherTest, TheConsoleNamesTheKinds)
{
	EXPECT_EQ(weather::KindNamed("rain"), weather::Kind::Rain);
	EXPECT_EQ(weather::KindNamed("snow"), weather::Kind::Snow);
	EXPECT_EQ(weather::KindNamed("clear"), weather::Kind::Clear);
	EXPECT_FALSE(weather::KindNamed("hail").has_value());
	weather::Force(weather::Kind::Rain, 0.5F);
	weather::Advance(31000);
	EXPECT_NE(weather::Text().find("Rain at 50%"), std::string::npos) << weather::Text();
}

TEST_F(Poe2WeatherTest, TheEffectsDropNothingWithoutAView)
{
	// Without a frame drawn there's no view to rain over, so no drops are made; the effects engine still runs.
	weather::Force(weather::Kind::Rain, 1);
	for (int i = 0; i < 100; i++)
		vfx::Advance(40);
	EXPECT_EQ(vfx::DropCount(), 0U);
	EXPECT_GT(weather::Current().strength, 0.F);
}

#if !defined(USE_SDL1) && !defined(USE_SDL3)
TEST_F(Poe2WeatherTest, RainLandsOnTheScreenAndDriesOff)
{
	GraphicsOptions &graphics = GetOptions().Graphics;
	const bool hd = *graphics.hdGraphics;
	const bool lit = *graphics.hdLighting;
	const bool rain = *graphics.weather;
	graphics.hdGraphics.SetValue(true);
	graphics.hdLighting.SetValue(true);
	graphics.weather.SetValue(true);
	// Drawing a frame (here without a renderer) tells the drops how big the screen is.
	vfx::RenderLens(nullptr, { 640, 480 }, { 1280, 720 });
	weather::Force(weather::Kind::Rain, 1);
	size_t most = 0;
	for (int i = 0; i < 400; i++) {
		vfx::Advance(40);
		most = std::max(most, vfx::LensDropCount());
		ASSERT_LE(vfx::LensDropCount(), 523U) << "more water on the screen than it holds";
	}
	EXPECT_GT(most, 100U) << "a downpour put hardly any water on the screen";
	// In the dungeon the screen dries off within seconds.
	leveltype = DTYPE_CATHEDRAL;
	for (int i = 0; i < 100; i++)
		vfx::Advance(40);
	EXPECT_EQ(vfx::LensDropCount(), 0U);
	graphics.hdGraphics.SetValue(hd);
	graphics.hdLighting.SetValue(lit);
	graphics.weather.SetValue(rain);
}

TEST_F(Poe2WeatherTest, AHarderStormPutsMoreWaterOnTheScreen)
{
	GraphicsOptions &graphics = GetOptions().Graphics;
	const bool hd = *graphics.hdGraphics;
	const bool lit = *graphics.hdLighting;
	const bool rain = *graphics.weather;
	graphics.hdGraphics.SetValue(true);
	graphics.hdLighting.SetValue(true);
	graphics.weather.SetValue(true);
	vfx::RenderLens(nullptr, { 640, 480 }, { 1280, 720 });
	// How much lies on the screen, on average, over half a minute of rain this strong.
	const auto waterIn = [](float strength) {
		vfx::Clear();
		weather::Force(weather::Kind::Rain, strength);
		size_t total = 0;
		for (int i = 0; i < 750; i++) {
			vfx::Advance(40);
			total += vfx::LensDropCount();
		}
		return total / 750;
	};
	const size_t drizzle = waterIn(0.15F);
	const size_t shower = waterIn(0.5F);
	const size_t downpour = waterIn(1);
	EXPECT_LT(drizzle * 2, shower) << "a drizzle puts nearly as much on the screen as a shower";
	EXPECT_LT(shower, downpour) << "a downpour puts no more on the screen than a shower";
	graphics.hdGraphics.SetValue(hd);
	graphics.hdLighting.SetValue(lit);
	graphics.weather.SetValue(rain);
}

TEST_F(Poe2WeatherTest, RainRunsOverThePanelsButNeverLandsOnThem)
{
	GraphicsOptions &graphics = GetOptions().Graphics;
	const bool hd = *graphics.hdGraphics;
	const bool lit = *graphics.hdLighting;
	const bool rain = *graphics.weather;
	graphics.hdGraphics.SetValue(true);
	graphics.hdLighting.SetValue(true);
	graphics.weather.SetValue(true);
	vfx::Clear();
	vfx::RenderLens(nullptr, { 640, 480 }, { 1280, 720 });
	// A bottom bar across the screen and an inventory on the right, as the UI overlay shows them (alpha in byte 3).
	constexpr int Width = 1280;
	constexpr int Height = 720;
	std::vector<uint8_t> ui(static_cast<size_t>(Width) * Height * 4, 0);
	for (int y = 0; y < Height; y++) {
		for (int x = 0; x < Width; x++) {
			if (y >= 600 || (x >= 960 && y >= 100))
				ui[(static_cast<size_t>(y) * Width + x) * 4 + 3] = 255;
		}
	}
	vfx::NoteUiPanels(ui.data(), nullptr, Width, Height);
	weather::Force(weather::Kind::Rain, 1);
	size_t most = 0;
	for (int i = 0; i < 1500; i++) {
		vfx::Advance(40);
		size_t onPanels = 0;
		size_t strays = 0;
		vfx::LensPanelCounts(onPanels, strays);
		ASSERT_EQ(strays, 0U) << "rain landed on a panel after " << i << " steps";
		most = std::max(most, onPanels);
	}
	EXPECT_GT(most, 0U) << "no water ever ran over a panel";
	vfx::NoteUiPanels(nullptr, nullptr, 0, 0);
	vfx::Clear();
	graphics.hdGraphics.SetValue(hd);
	graphics.hdLighting.SetValue(lit);
	graphics.weather.SetValue(rain);
}

#if SDL_VERSION_ATLEAST(2, 0, 18)
/** @brief What a window that is only a picture in memory shows now, without its alpha. */
std::vector<uint32_t> Shown(SDL_Renderer *renderer, const SDL_Surface *window)
{
	SDL_RenderFlush(renderer);
	std::vector<uint32_t> shown(static_cast<size_t>(window->w) * window->h);
	for (int y = 0; y < window->h; y++)
		std::memcpy(&shown[static_cast<size_t>(y) * window->w], static_cast<const uint8_t *>(window->pixels) + static_cast<size_t>(y) * window->pitch, static_cast<size_t>(window->w) * 4);
	for (uint32_t &pixel : shown)
		pixel &= 0xFFFFFFU;
	return shown;
}

/**
 * @brief Draws a world the way the game does: the window cleared, then a picture that reaches past every edge of the UI
 * (as the HD world's tiles do), and over it things at the UI's own pixels: bars across it and a line along its edge.
 */
void DrawWorld(SDL_Renderer *renderer, Size ui)
{
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	SDL_SetRenderDrawColor(renderer, 120, 100, 80, 255);
	const SDL_Rect past { -8, -8, ui.width + 16, ui.height + 16 };
	SDL_RenderFillRect(renderer, &past);
	SDL_SetRenderDrawColor(renderer, 30, 60, 110, 255);
	for (int x = 0; x < ui.width; x += 24) {
		const SDL_Rect bar { x, 0, 9, ui.height };
		SDL_RenderFillRect(renderer, &bar);
	}
	SDL_SetRenderDrawColor(renderer, 230, 220, 190, 255);
	const SDL_Rect edge { 0, 0, ui.width, ui.height };
	SDL_RenderDrawRect(renderer, &edge);
}

TEST_F(Poe2WeatherTest, TheWetScreenShowsTheWorldPixelForPixel)
{
	// A window that is only a picture in memory. It is never closed: the effects keep pictures of their own for a
	// renderer, and count on it living as long as the game does.
	constexpr Size WindowSize { 1478, 768 };
	static SDL_Surface *window = SDL_CreateRGBSurfaceWithFormat(0, WindowSize.width, WindowSize.height, 32, SDL_PIXELFORMAT_ARGB8888);
	static SDL_Renderer *renderer = window != nullptr ? SDL_CreateSoftwareRenderer(window) : nullptr;
	ASSERT_NE(renderer, nullptr) << SDL_GetError();
	GraphicsOptions &graphics = GetOptions().Graphics;
	const bool hd = *graphics.hdGraphics;
	const bool lit = *graphics.hdLighting;
	const bool rain = *graphics.weather;
	graphics.hdGraphics.SetValue(true);
	graphics.hdLighting.SetValue(true);
	graphics.weather.SetValue(true);
	weather::Force(weather::Kind::Rain, 1);
	// While there is water on the screen the world is drawn into a picture of its own, which then goes to the window:
	// it has to land there exactly as the world drawn straight to the window does, to the last line of pixels. Three UIs
	// that SDL fits into the window in its three ways. The game's own at a UI scale of 125, fitted by its width, which
	// times the scale comes out a hair under the window's. One with the window's shape as near as SDL cares, which fills
	// the window both ways though its height times the scale is short of the window's. And one that fits exactly twice.
	for (const Size ui : { Size { 1182, 614 }, Size { 1453, 755 }, Size { 739, 384 } }) {
		SCOPED_TRACE(::testing::Message() << "a UI of " << ui.width << " by " << ui.height);
		ASSERT_EQ(SDL_RenderSetLogicalSize(renderer, ui.width, ui.height), 0) << SDL_GetError();
		// Dry, the world goes straight to the window.
		vfx::Clear();
		ASSERT_FALSE(vfx::BeginScene(renderer, ui));
		DrawWorld(renderer, ui);
		vfx::EndScene(renderer);
		vfx::RenderLens(renderer, ui, ui);
		const std::vector<uint32_t> straight = Shown(renderer, window);
		for (int i = 0; i < 400 && vfx::LensDropCount() < 100; i++)
			vfx::Advance(40);
		ASSERT_GE(vfx::LensDropCount(), 100U) << "a downpour put hardly any water on the screen";
		float fitX = 0;
		float fitY = 0;
		SDL_RenderGetScale(renderer, &fitX, &fitY);
		ASSERT_TRUE(vfx::BeginScene(renderer, ui));
		DrawWorld(renderer, ui);
		vfx::EndScene(renderer);
		// (The UI is drawn next, at the window's own scale.)
		float scaleX = 0;
		float scaleY = 0;
		SDL_RenderGetScale(renderer, &scaleX, &scaleY);
		EXPECT_EQ(scaleX, fitX);
		EXPECT_EQ(scaleY, fitY);
		const std::vector<uint32_t> through = Shown(renderer, window);
		size_t different = 0;
		size_t first = 0;
		for (size_t i = 0; i < straight.size(); i++) {
			if (through[i] != straight[i] && different++ == 0)
				first = i;
		}
		EXPECT_EQ(different, 0U) << "drawn through its picture the world differs from itself drawn straight, first at " << first % WindowSize.width << ", " << first / WindowSize.width;
		// And then the water is drawn over it, which shows.
		vfx::RenderLens(renderer, ui, ui);
		const std::vector<uint32_t> wet = Shown(renderer, window);
		size_t watered = 0;
		for (size_t i = 0; i < straight.size(); i++)
			watered += wet[i] != through[i] ? 1 : 0;
		EXPECT_GT(watered, 0U) << "no water shows on the wet screen";
	}
	graphics.hdGraphics.SetValue(hd);
	graphics.hdLighting.SetValue(lit);
	graphics.weather.SetValue(rain);
}
#endif
#endif

} // namespace
} // namespace devilution
