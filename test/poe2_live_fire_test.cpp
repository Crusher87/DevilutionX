#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <gtest/gtest.h>

#include "poe2/live_fire.hpp"

// POE2 Mashup: the live fire behind the menus' logo (and, later, torches and braziers), worked out on the CPU.

namespace devilution {
namespace fire {
namespace {

/** @brief Maps for a small fire: heat fed along the bottom rows between `left` and `right`, sparks at its top. */
FireSource SmallSource(int width, int height, int left, int right)
{
	std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4, 0);
	for (int row = 0; row < height; row++) {
		const int y = height - 1 - row;
		for (int x = 0; x < width; x++) {
			uint8_t *p = &rgba[(static_cast<size_t>(row) * width + x) * 4];
			const bool source = x >= left && x < right && y >= 4 && y < 14;
			p[0] = source ? 255 : 0;
			p[1] = static_cast<uint8_t>(std::clamp((y - 14) * 255 / 120, 0, 255));
			p[2] = source && (y == 11 || y == 12) ? 200 : 0;
			p[3] = 255;
		}
	}
	FireSource source;
	EXPECT_TRUE(SourceFromMaps(rgba.data(), width, height, width * 4, source));
	return source;
}

float Sum(const std::vector<float> &values)
{
	float sum = 0;
	for (const float v : values)
		sum += v;
	return sum;
}

TEST(LiveFireTest, MapsAreReadBottomRowFirst)
{
	const uint8_t rgba[2 * 2 * 4] = {
		// Top row: feed on the left, a spark spot on the right.
		255, 0, 0, 255, 0, 0, 255, 255,
		// Bottom row: half way up on the left.
		0, 51, 0, 255, 0, 0, 0, 255
	};
	FireSource source;
	ASSERT_TRUE(SourceFromMaps(rgba, 2, 2, 8, source));
	EXPECT_FLOAT_EQ(source.feed[2], 1.6F);
	EXPECT_FLOAT_EQ(source.feed[0], 0.F);
	EXPECT_FLOAT_EQ(source.rise[0], 0.2F);
	// Every other pixel is looked at for sparks, from the top left: (1, top) is skipped.
	EXPECT_TRUE(source.sparkX.empty());
	EXPECT_FALSE(SourceFromMaps(nullptr, 2, 2, 8, source));
}

TEST(LiveFireTest, LogoZonesFollowTheLetters)
{
	const std::vector<FireZone> zones = LogoZones();
	ASSERT_EQ(zones.size(), 6U);
	// D: logo pixels 23..101, the grid twice that, its feed reaching 14 logo pixels past each side.
	EXPECT_FLOAT_EQ(zones[0].centre, 124.F);
	EXPECT_FLOAT_EQ(zones[0].halfWidth, 106.F);
	EXPECT_FLOAT_EQ(zones[0].phase, 0.4F);
	// Big letters breathe slower: the D (78 wide) slowest, the I (31 wide) quickest.
	EXPECT_NEAR(zones[0].beat, 2.6F, 1e-4F);
	EXPECT_NEAR(zones[1].beat, 4.8F, 1e-4F);
}

TEST(LiveFireTest, RampMatchesTheSpec)
{
	const RampColour cold = RampAt(0.F, 0.5F);
	EXPECT_FLOAT_EQ(cold.a, 0.F);
	const RampColour yellow = RampAt(0.36F, 0.F);
	EXPECT_FLOAT_EQ(yellow.r, 205.F);
	EXPECT_FLOAT_EQ(yellow.g, 62.F);
	EXPECT_FLOAT_EQ(yellow.b, 8.F);
	EXPECT_FLOAT_EQ(yellow.a, 1.F);
	const RampColour red = RampAt(0.36F, 1.F);
	EXPECT_FLOAT_EQ(red.r, 160.F);
	EXPECT_FLOAT_EQ(red.g, 22.F);
	// Curtis's half and half.
	const RampColour mixed = RampAt(0.36F, 0.5F);
	EXPECT_FLOAT_EQ(mixed.r, 182.5F);
	EXPECT_FLOAT_EQ(mixed.g, 42.F);
	// Half way between two stops, and past the top.
	const RampColour between = RampAt(0.035F, 0.F);
	EXPECT_NEAR(between.r, 33.F, 1e-3F);
	EXPECT_NEAR(between.a, 0.225F, 1e-5F);
	const RampColour top = RampAt(5.F, 0.F);
	EXPECT_FLOAT_EQ(top.r, 255.F);
	EXPECT_FLOAT_EQ(top.b, 196.F);
	// Hotter is brighter.
	float last = -1;
	for (int i = 0; i <= 34; i++) {
		const RampColour c = RampAt(static_cast<float>(i) * 0.05F, 0.5F);
		const float bright = (c.r + c.g + c.b) * c.a;
		EXPECT_GE(bright, last) << "heat " << static_cast<float>(i) * 0.05F;
		last = bright;
	}
}

TEST(LiveFireTest, SameSeedSameFireWhateverTheThreads)
{
	LiveFire alone(SmallSource(96, 64, 30, 66), FireSettings {}, 5, 0);
	LiveFire shared(SmallSource(96, 64, 30, 66), FireSettings {}, 5, 3);
	for (int i = 0; i < 40; i++) {
		alone.Advance(1.F / 60.F);
		shared.Advance(1.F / 60.F);
	}
	EXPECT_EQ(alone.Heat(), shared.Heat());
	EXPECT_EQ(alone.SparkCount(), shared.SparkCount());
	std::vector<uint8_t> a(96 * 64 * 4), b(96 * 64 * 4);
	alone.Paint(a.data(), 96 * 4);
	shared.Paint(b.data(), 96 * 4);
	EXPECT_EQ(a, b);
}

TEST(LiveFireTest, HeatRisesFromItsFeedAndStaysInRange)
{
	LiveFire fire(SmallSource(96, 96, 30, 66), FireSettings {}, 1, 0);
	EXPECT_EQ(Sum(fire.Heat()), 0.F);
	for (int i = 0; i < 60; i++)
		fire.Step(1.F / 60.F);
	float below = 0;
	float above = 0;
	for (int y = 0; y < 96; y++) {
		for (int x = 0; x < 96; x++) {
			const float t = fire.Heat()[static_cast<size_t>(y) * 96 + x];
			ASSERT_GE(t, 0.F);
			ASSERT_LE(t, 1.7F);
			(y < 14 ? below : above) += t;
		}
	}
	EXPECT_GT(below, 0.F);
	// Carried up above where it was fed.
	EXPECT_GT(above, 0.F);
	// Nothing shows at the top of the picture (it fades out there).
	std::vector<uint8_t> rgba(96 * 96 * 4);
	fire.Paint(rgba.data(), 96 * 4);
	for (int x = 0; x < 96 * 4; x += 4)
		EXPECT_EQ(rgba[x] + rgba[x + 1] + rgba[x + 2], 0) << x / 4;
}

TEST(LiveFireTest, NoFeedNoFire)
{
	LiveFire fire(SmallSource(64, 48, 0, 0), FireSettings {}, 1, 0);
	for (int i = 0; i < 30; i++)
		fire.Advance(1.F / 30.F);
	EXPECT_EQ(Sum(fire.Heat()), 0.F);
	std::vector<uint8_t> rgba(64 * 48 * 4, 7);
	fire.Paint(rgba.data(), 64 * 4);
	for (size_t i = 0; i < rgba.size(); i += 4) {
		ASSERT_EQ(rgba[i], 0);
		ASSERT_EQ(rgba[i + 1], 0);
		ASSERT_EQ(rgba[i + 2], 0);
		ASSERT_EQ(rgba[i + 3], 255);
	}
	// No heat, no light on the letters: times 1 (halved for the doubling blend), nothing added.
	std::vector<uint8_t> multiply(fire.LightWidth() * fire.LightHeight() * 4), add(multiply.size());
	fire.PaintLight(multiply.data(), fire.LightWidth() * 4, add.data(), fire.LightWidth() * 4, true);
	EXPECT_EQ(multiply[0], 128);
	EXPECT_EQ(add[0], 0);
}

TEST(LiveFireTest, NoiseChangesWithTime)
{
	// Curtis turned down frozen noise: the same fire a moment later is not the same picture moved up.
	LiveFire fire(SmallSource(96, 96, 10, 86), FireSettings {}, 2, 0);
	fire.WarmUp(1.F);
	std::vector<uint8_t> a(96 * 96 * 4), b(96 * 96 * 4);
	fire.Paint(a.data(), 96 * 4);
	fire.Step(1.F / 60.F);
	fire.Paint(b.data(), 96 * 4);
	EXPECT_NE(a, b);
}

TEST(LiveFireTest, PaintKeepsAwayFromTheEdgesAndUnderCover)
{
	LiveFire fire(SmallSource(96, 96, 0, 96), FireSettings {}, 3, 0);
	fire.WarmUp(1.F);
	std::vector<uint8_t> rgba(96 * 96 * 4);
	fire.Paint(rgba.data(), 96 * 4);
	int lit = 0;
	for (int row = 0; row < 96; row++) {
		for (int x = 0; x < 96; x++) {
			const uint8_t *p = &rgba[(static_cast<size_t>(row) * 96 + x) * 4];
			const int sum = p[0] + p[1] + p[2];
			lit += sum > 0 ? 1 : 0;
			// The picture's left and right columns are all but black (the edge mask is next to 0 there).
			if (x == 0 || x == 95) {
				EXPECT_LE(sum, 3) << x << "," << row;
			}
		}
	}
	EXPECT_GT(lit, 0);
	const std::vector<uint8_t> hidden(96 * 96, 0);
	fire.Paint(rgba.data(), 96 * 4, hidden.data());
	for (size_t i = 0; i < rgba.size(); i += 4)
		ASSERT_EQ(rgba[i] + rgba[i + 1] + rgba[i + 2], 0);
}

TEST(LiveFireTest, PointerBlowsTheFireAndTheWindDiesDown)
{
	FireSettings settings;
	LiveFire still(SmallSource(160, 96, 40, 120), settings, 4, 0);
	LiveFire blown(SmallSource(160, 96, 40, 120), settings, 4, 0);
	still.WarmUp(1.F);
	blown.WarmUp(1.F);
	// A sweep to the right across the middle, at 600 grid pixels a second.
	blown.Push(40, 40, 50, 40, 1.F / 60.F);
	float most = 0;
	for (size_t i = 0; i < blown.Wind().size(); i += 2) {
		EXPECT_GE(blown.Wind()[i], 0.F);
		most = std::max(most, blown.Wind()[i]);
	}
	// 600 * 0.55 * 25%, blended in at 0.7 at most.
	EXPECT_GT(most, 600 * 0.55F * 0.25F * 0.3F);
	EXPECT_LE(most, 600 * 0.55F * 0.25F * 0.7F + 0.01F);
	for (int i = 0; i < 10; i++) {
		still.Step(1.F / 60.F);
		blown.Step(1.F / 60.F);
	}
	EXPECT_NE(still.Heat(), blown.Heat());
	// It pushed the heat to the right: its middle (weighted by heat) is further right than without the wind.
	const auto middle = [](const LiveFire &f) {
		double sum = 0;
		double weighted = 0;
		for (int y = 0; y < 96; y++) {
			for (int x = 0; x < 160; x++) {
				const float t = f.Heat()[static_cast<size_t>(y) * 160 + x];
				sum += t;
				weighted += t * x;
			}
		}
		return weighted / sum;
	};
	EXPECT_GT(middle(blown), middle(still) + 0.5);
	for (int i = 0; i < 240; i++)
		blown.Step(1.F / 60.F);
	EXPECT_EQ(Sum(blown.Wind()), 0.F);
}

TEST(LiveFireTest, SlowOrWindlessPointerDoesNothing)
{
	FireSettings calm;
	calm.wind = 0;
	LiveFire fire(SmallSource(160, 96, 40, 120), calm, 4, 0);
	fire.Push(40, 40, 60, 40, 1.F / 60.F);
	EXPECT_EQ(Sum(fire.Wind()), 0.F);
	LiveFire slow(SmallSource(160, 96, 40, 120), FireSettings {}, 4, 0);
	// 6 grid pixels a second, under the 8 it takes; and a pause of over 0.2 s.
	slow.Push(40, 40, 40.1F, 40, 1.F / 60.F);
	slow.Push(40, 40, 80, 40, 0.5F);
	EXPECT_EQ(Sum(slow.Wind()), 0.F);
}

TEST(LiveFireTest, FlareUpThrowsSparksFromItsZone)
{
	FireSource source = SmallSource(160, 96, 0, 160);
	FireZone left;
	left.centre = 40;
	left.halfWidth = 40;
	FireZone right;
	right.centre = 120;
	right.halfWidth = 40;
	source.zones = { left, right };
	FireSettings settings;
	settings.flares = 0;
	settings.sparks = 0;
	LiveFire fire(std::move(source), settings, 9, 0);
	EXPECT_EQ(fire.SparkCount(), 0U);
	settings.sparks = 1;
	fire.SetSettings(settings);
	fire.Flare(1);
	EXPECT_EQ(fire.SparkCount(), 50U);
	// None start yet (they wait a quarter second), then they fly up from the right zone.
	fire.Step(0.3F);
	fire.Step(0.3F);
	ASSERT_GT(fire.SparkCount(), 0U);
	fire.Flare(7);
}

TEST(LiveFireTest, LightOnTheLettersFollowsTheHeat)
{
	LiveFire fire(SmallSource(96, 96, 30, 66), FireSettings {}, 6, 0);
	fire.WarmUp(1.F);
	std::vector<uint8_t> rgba(96 * 96 * 4);
	fire.Paint(rgba.data(), 96 * 4);
	EXPECT_GT(fire.LightAt(48, 20), 0.F);
	EXPECT_LE(fire.LightAt(48, 20), 1.F);
	EXPECT_GT(fire.LightAt(48, 20), fire.LightAt(2, 94));
	std::vector<uint8_t> multiply(fire.LightWidth() * fire.LightHeight() * 4), add(multiply.size());
	fire.PaintLight(multiply.data(), fire.LightWidth() * 4, add.data(), fire.LightWidth() * 4, false);
	// Where lit, green and blue are taken down more than red (firelight is warm), and warmth is added in the dark.
	bool warm = false;
	for (size_t i = 0; i < multiply.size(); i += 4) {
		EXPECT_GE(multiply[i], multiply[i + 1]);
		EXPECT_GE(multiply[i + 1], multiply[i + 2]);
		warm = warm || (multiply[i + 2] < 250 && add[i] > 0);
	}
	EXPECT_TRUE(warm);
}

TEST(LiveFireTest, LogoGridFrameTime)
{
	// The full title-logo grid with letter-like maps (the real ones are in the HD pack): how long a frame takes.
	const int w = LogoGridWidth;
	const int h = LogoGridHeight;
	std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4, 0);
	for (const FireZone &zone : LogoZones()) {
		for (int row = h / 2; row < h - 60; row++) {
			for (int x = static_cast<int>(zone.centre - zone.sparkHalfWidth); x < static_cast<int>(zone.centre + zone.sparkHalfWidth); x++) {
				uint8_t *p = &rgba[(static_cast<size_t>(row) * w + x) * 4];
				p[0] = row < h / 2 + 8 ? 255 : 120;
				p[2] = row == h / 2 + 2 ? 200 : 0;
			}
		}
	}
	for (int row = 0; row < h; row++)
		for (int x = 0; x < w; x++)
			rgba[(static_cast<size_t>(row) * w + x) * 4 + 1] = static_cast<uint8_t>(std::clamp((h - 60 - row) * 255 / 160, 0, 255));
	FireSource source;
	ASSERT_TRUE(SourceFromMaps(rgba.data(), w, h, w * 4, source));
	source.zones = LogoZones();
	LiveFire fire(std::move(source), FireSettings {});
	const auto warmStart = std::chrono::steady_clock::now();
	fire.WarmUp();
	const auto warmEnd = std::chrono::steady_clock::now();
	std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4);
	std::vector<uint8_t> multiply(static_cast<size_t>(fire.LightWidth()) * fire.LightHeight() * 4), add(multiply.size());
	constexpr int Frames = 120;
	const auto start = std::chrono::steady_clock::now();
	for (int i = 0; i < Frames; i++) {
		if (i == 30)
			fire.Push(200, 200, 260, 205, 1.F / 100.F);
		fire.Advance(1.F / 100.F);
		fire.Paint(out.data(), w * 4);
		fire.PaintLight(multiply.data(), fire.LightWidth() * 4, add.data(), fire.LightWidth() * 4, true);
	}
	const auto end = std::chrono::steady_clock::now();
	const double ms = std::chrono::duration<double, std::milli>(end - start).count() / Frames;
	const double warm = std::chrono::duration<double, std::milli>(warmEnd - warmStart).count();
	std::printf("Live fire, %dx%d grid: %.2f ms a frame (step, paint and light), warm-up %.0f ms\n", w, h, ms, warm);
	EXPECT_GT(Sum(fire.Heat()), 0.F);
}

} // namespace
} // namespace fire
} // namespace devilution
