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
	weather::Force(weather::Kind::Rain, 1);
	Run(40);
	ASSERT_GT(weather::Current().strength, 0.9F);
	weather::Force(weather::Kind::Snow, 1);
	Run(1.5F);
	EXPECT_EQ(weather::Current().kind, weather::Kind::Rain);
	EXPECT_LT(weather::Current().strength, 0.9F);
	Run(10);
	EXPECT_EQ(weather::Current().kind, weather::Kind::Snow);
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

} // namespace
} // namespace devilution
