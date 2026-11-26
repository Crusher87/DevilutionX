#include <array>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "levels/gendung.h"
#include "poe2/atmosphere.hpp"

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

} // namespace
} // namespace devilution
