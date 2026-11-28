#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "levels/gendung.h"
#include "poe2/atmosphere.hpp"
#include "poe2/echo.hpp"

namespace devilution {
namespace {

class Poe2AtmosphereTest : public ::testing::Test {
public:
	void SetUp() override
	{
		gbIsPoe2 = true;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		leveltype = DTYPE_TOWN;
		atmosphere::Feel(std::string("reload"), std::nullopt);
	}

	void TearDown() override
	{
		leveltype = DTYPE_NONE;
		gbIsPoe2 = false;
		UnloadModArchives();
		atmosphere::Feel(std::string("reload"), std::nullopt);
	}
};

// The numbers themselves are Curtis's to tune (mods/poe2/txtdata/poe2/atmosphere.tsv), so these tests ask only whether
// a name is found: a found number is never the negative fallback given here.

TEST_F(Poe2AtmosphereTest, NumbersAreFoundByName)
{
	ASSERT_TRUE(atmosphere::Has("flicker")) << "the table (txtdata/poe2/atmosphere.tsv) didn't load";
	// A short name and ones too long for a std::string to hold without the heap: all found.
	EXPECT_GE(atmosphere::Value("flicker", -1.F), 0.F);
	EXPECT_GE(atmosphere::Value("town_water_alpha", -1.F), 0.F);
	EXPECT_GE(atmosphere::Value("cathedral_glow_deep", -1.F), 0.F);
	const std::array<float, 3> colour = atmosphere::Colour("town_water_colour", { -1.F, -1.F, -1.F });
	EXPECT_GE(colour[0], 0.F);
	EXPECT_GE(colour[1], 0.F);
	EXPECT_GE(colour[2], 0.F);
}

TEST_F(Poe2AtmosphereTest, NameIsTakenAsGivenNotToTheNextZeroByte)
{
	ASSERT_TRUE(atmosphere::Has("town_water_alpha"));
	// A name that is part of a longer text: only its own letters count.
	const std::string_view text = "town_water_alpha_and_more";
	EXPECT_GE(atmosphere::Value(text.substr(0, 16), -1.F), 0.F);
	EXPECT_FLOAT_EQ(atmosphere::Value(text.substr(0, 16), -1.F), atmosphere::Value("town_water_alpha", -2.F));
	EXPECT_FALSE(atmosphere::Has(text));
	EXPECT_FALSE(atmosphere::Has(text.substr(0, 15)));
	EXPECT_FALSE(atmosphere::Has(""));
}

TEST_F(Poe2AtmosphereTest, UnknownNamesAndOtherAreasGiveTheFallback)
{
	EXPECT_FLOAT_EQ(atmosphere::Value("no_such_number_anywhere", 7.5F), 7.5F);
	// The town's numbers aren't the Cathedral's.
	ASSERT_TRUE(atmosphere::Has("town_water_alpha"));
	leveltype = DTYPE_CATHEDRAL;
	EXPECT_FALSE(atmosphere::Has("town_water_alpha"));
	EXPECT_FLOAT_EQ(atmosphere::Value("town_water_alpha", -3.F), -3.F);
	// A room's number is found from anywhere.
	EXPECT_GE(atmosphere::RoomValue("town", "town_water_alpha", -1.F), 0.F);
	leveltype = DTYPE_NONE;
	EXPECT_FLOAT_EQ(atmosphere::Value("town_water_alpha", -3.F), -3.F);
}

/** How long a click rings in a room of this feedback: the seconds until 99.9% of the ring's strength has gone. */
float RingSeconds(float feedback, float *strength = nullptr)
{
	constexpr int Rate = 22050;
	echo::Room room;
	room.Setup(Rate);
	room.Shape(feedback, 0.45F);
	std::vector<float> in(Rate * 4, 0.F);
	in[0] = 1.F;
	std::vector<float> out(in.size() * 2);
	room.Process(in.data(), out.data(), static_cast<int>(in.size()), 2);
	double total = 0;
	for (size_t i = 0; i < out.size(); i += 2)
		total += out[i] * out[i];
	if (strength != nullptr)
		*strength = static_cast<float>(total);
	double left = total;
	for (size_t i = 0; i < out.size(); i += 2) {
		left -= out[i] * out[i];
		if (left < total * 0.001)
			return static_cast<float>(i / 2) / Rate;
	}
	return 4.F;
}

TEST(Poe2EchoTest, ABiggerRoomRingsLongerAndEveryRingDiesAway)
{
	float small = 0;
	float big = 0;
	const float shortRing = RingSeconds(0.70F, &small);
	const float longRing = RingSeconds(0.87F, &big);
	// A passage: a short ring. A great hall: about twice as long, and never more than a second or so.
	EXPECT_GT(shortRing, 0.1F);
	EXPECT_LT(shortRing, 0.5F);
	EXPECT_GT(longRing, shortRing * 1.5F);
	EXPECT_LT(longRing, 1.2F);
	// The whole ring of a click is about as strong as the click (the send then makes it a small share of that).
	EXPECT_GT(small, 0.4F);
	EXPECT_LT(big, 2.F);
}

TEST(Poe2EchoTest, SilenceInGivesSilenceOutAndALoudRunNeverRunsAway)
{
	constexpr int Rate = 22050;
	echo::Room room;
	room.Setup(Rate);
	EXPECT_TRUE(room.Silent());
	// The longest ring the room allows, fed full-strength noise for two seconds.
	room.Shape(2.F, 0.F);
	std::vector<float> in(Rate * 2);
	uint32_t seed = 7;
	for (float &sample : in) {
		seed = seed * 1664525U + 1013904223U;
		sample = static_cast<float>(seed >> 8) / 8388608.F - 1.F;
	}
	std::vector<float> out(in.size() * 2);
	room.Process(in.data(), out.data(), static_cast<int>(in.size()), 2);
	EXPECT_FALSE(room.Silent());
	float loudest = 0;
	bool earsDiffer = false;
	for (size_t i = 0; i < out.size(); i += 2) {
		ASSERT_TRUE(std::isfinite(out[i]));
		loudest = std::max(loudest, std::abs(out[i]));
		earsDiffer = earsDiffer || std::abs(out[i] - out[i + 1]) > 0.01F;
	}
	EXPECT_LT(loudest, 10.F);
	// The two ears hear different rings (a wide sound, not one stuck in the middle).
	EXPECT_TRUE(earsDiffer);
	// Then silence: the ring is gone within a few seconds and the room says so.
	std::fill(in.begin(), in.end(), 0.F);
	room.Shape(0.8F, 0.45F);
	for (int i = 0; i < 4 && !room.Silent(); i++)
		room.Process(in.data(), out.data(), static_cast<int>(in.size()), 2);
	EXPECT_TRUE(room.Silent());
	// One ear only works too.
	in[0] = 1.F;
	room.Process(in.data(), out.data(), 4096, 1);
	EXPECT_FALSE(room.Silent());
}

TEST_F(Poe2AtmosphereTest, NothingRingsUnderTheOpenSkyOrWithTheEchoSwitchedOff)
{
	leveltype = DTYPE_TOWN;
	echo::Update();
	EXPECT_FLOAT_EQ(echo::Send(), 0.F);
}

} // namespace
} // namespace devilution
