#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "poe2/carved_bodies.hpp"

namespace devilution {
namespace {

void Put32(std::vector<uint8_t> &out, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void PutF(std::vector<uint8_t> &out, float f)
{
	uint32_t v;
	std::memcpy(&v, &f, sizeof(v));
	Put32(out, v);
}

void Put16(std::vector<uint8_t> &out, int16_t v)
{
	out.push_back(static_cast<uint8_t>(static_cast<uint16_t>(v) & 0xFF));
	out.push_back(static_cast<uint8_t>(static_cast<uint16_t>(v) >> 8));
}

/** @brief A carved.bin with two poses: a ball on the floor, and a column of three. */
std::vector<uint8_t> TwoPoses()
{
	std::vector<uint8_t> out { 'C', 'R', 'V', '1' };
	Put32(out, 2);
	PutF(out, 30.F);
	PutF(out, 64.F);
	PutF(out, 88.5F);
	PutF(out, -2.F);
	for (int i = 0; i < 16; i++)
		PutF(out, i == 2 ? 1.F : 0.F);
	const auto pose = [&](const char *name, uint32_t first, uint32_t count) {
		char padded[12] {};
		std::strncpy(padded, name, sizeof(padded));
		out.insert(out.end(), padded, padded + 12);
		Put32(out, first);
		Put32(out, count);
	};
	pose("f00", 0, 1);
	pose("f00_m1", 1, 3);
	const auto ball = [&](float x, float y, float z, float r) {
		Put16(out, static_cast<int16_t>(x * 8));
		Put16(out, static_cast<int16_t>(y * 8));
		Put16(out, static_cast<int16_t>(z * 8));
		Put16(out, static_cast<int16_t>(r * 64));
	};
	ball(0, 0, 4, 4);
	ball(10, 0, 4, 4);
	ball(10, 0, 12, 4);
	ball(10, 0, 20, 4);
	return out;
}

TEST(CarvedBodies, ReadsPosesAndBalls)
{
	const std::vector<uint8_t> data = TwoPoses();
	carved::Sprite sprite;
	ASSERT_TRUE(carved::Parse(data, sprite));
	EXPECT_FLOAT_EQ(sprite.el, 30.F);
	ASSERT_EQ(sprite.poses.size(), 2U);
	const carved::Pose *pose = sprite.Find("f00_m1");
	ASSERT_NE(pose, nullptr);
	const auto balls = sprite.BallsOf(*pose);
	ASSERT_EQ(balls.size(), 3U);
	EXPECT_FLOAT_EQ(balls[2].z, 20.F);
	EXPECT_FLOAT_EQ(balls[2].r, 4.F);
	EXPECT_EQ(sprite.Find("f01"), nullptr);
	float x;
	float y;
	sprite.FeetAt(1, x, y);
	EXPECT_FLOAT_EQ(x, 65.F);
	EXPECT_NEAR(y, 88.5F + 2.F * 0.8660254F, 1e-4F);
}

TEST(CarvedBodies, RefusesBrokenFiles)
{
	std::vector<uint8_t> data = TwoPoses();
	carved::Sprite sprite;
	EXPECT_FALSE(carved::Parse(std::span<const uint8_t>(data).first(40), sprite));
	data.resize(data.size() - 8);
	EXPECT_FALSE(carved::Parse(data, sprite));
	data = TwoPoses();
	data[0] = 'X';
	EXPECT_FALSE(carved::Parse(data, sprite));
}

TEST(CarvedBodies, CardShowsTheBodyAcrossTheLight)
{
	const std::vector<uint8_t> data = TwoPoses();
	carved::Sprite sprite;
	ASSERT_TRUE(carved::Parse(data, sprite));
	const auto balls = sprite.BallsOf(*sprite.Find("f00_m1"));
	// Facing south, the column stands 10 px to the viewer's right: wx = 10/64, wy = -10/64. A light shining along +x
	// (dx 1, dy 0) sees it across by dx * wy = -10/64 tiles: to the left of the card's middle.
	carved::Card card;
	carved::MakeCard(balls, 0, 1.F, 1.F, 0.F, 45.254834F, 16, card);
	ASSERT_GT(card.width, 0);
	EXPECT_EQ(card.height, card.floorRow + 17);
	int sumX = 0;
	int n = 0;
	for (int y = 0; y < card.height; y++) {
		for (int x = 0; x < card.width; x++) {
			if (card.cover[static_cast<size_t>(y) * card.width + x] > 0.5F) {
				sumX += x;
				n++;
			}
		}
	}
	ASSERT_GT(n, 0);
	const float middle = static_cast<float>(sumX) / static_cast<float>(n) + 0.5F;
	EXPECT_NEAR(middle, card.width / 2.F - 10.F / 64.F * 45.254834F, 1.F);
	// Three balls up to 24 high: the top row covered is about 24 above the floor.
	EXPECT_NEAR(static_cast<float>(card.floorRow - card.top), 24.F, 1.5F);
	// The column stands along that light's way (wx = -wy): the light sees it behind the first ball, in the middle.
	carved::MakeCard(balls, 0, 1.F, 0.70710678F, -0.70710678F, 45.254834F, 0, card);
	sumX = 0;
	n = 0;
	for (int y = 0; y < card.height; y++) {
		for (int x = 0; x < card.width; x++) {
			if (card.cover[static_cast<size_t>(y) * card.width + x] > 0.5F) {
				sumX += x;
				n++;
			}
		}
	}
	EXPECT_LT(std::abs(static_cast<float>(sumX) / static_cast<float>(n) + 0.5F - card.width / 2.F), 1.F);
}

TEST(CarvedBodies, FacingsRoundTheBall)
{
	const std::vector<uint8_t> data = TwoPoses();
	carved::Sprite sprite;
	ASSERT_TRUE(carved::Parse(data, sprite));
	const auto balls = sprite.BallsOf(*sprite.Find("f00"));
	// One ball of radius 4 (8 px at k 2), its middle at (2 * 64, 2 * (88.5 - 2 * cos 30)) for facing 0.
	std::vector<float> facings;
	carved::ViewFacings(sprite, balls, 0, 2.F, 256, 192, 0.F, facings);
	const auto at = [&](int x, int y, int q) { return facings[(static_cast<size_t>(y) * 256 + static_cast<size_t>(x)) * 4 + static_cast<size_t>(q)]; };
	const int cx = 128;
	const int cy = static_cast<int>(2 * (88.5F - 2.F * 0.8660254F));
	EXPECT_EQ(at(cx, cy, 3), 1.F);
	EXPECT_GT(at(cx, cy, 2), 0.95F);
	EXPECT_GT(at(cx + 6, cy, 0), 0.6F);
	EXPECT_LT(at(cx - 6, cy, 0), -0.6F);
	EXPECT_GT(at(cx, cy - 6, 1), 0.6F);
	EXPECT_EQ(at(cx + 20, cy, 3), 0.F);
	carved::ViewFacings(sprite, balls, 0, 2.F, 256, 192, 1.5F, facings);
	EXPECT_GT(at(cx + 6, cy, 0), 0.4F);
	EXPECT_NEAR(at(cx, cy, 0) * at(cx, cy, 0) + at(cx, cy, 1) * at(cx, cy, 1) + at(cx, cy, 2) * at(cx, cy, 2), 1.F, 1e-3F);
}

TEST(CarvedBodies, NamesGiveFacingAndPose)
{
	EXPECT_EQ(carved::FacingOf("monsters/zombie/zombien/d3_f05"), 3);
	EXPECT_EQ(carved::FacingOf("monsters/zombie/zombien/x3_f05"), -1);
	EXPECT_EQ(carved::PoseOf("monsters/zombie/zombien/d3_f05", 0), "f05");
	EXPECT_EQ(carved::PoseOf("monsters/zombie/zombien/d3_f05", 2), "f05_m2");
	EXPECT_EQ(carved::PoseOf("monsters/zombie/zombien/d3_f05", 3), "f05_b1");
}

} // namespace
} // namespace devilution
