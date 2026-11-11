#include <algorithm>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "engine/random.hpp"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "monster.h"
#include "options.h"
#include "poe2/vfx.hpp"
#include "tables/monstdat.h"

namespace devilution {
namespace {

using vfx::DebrisKind;
namespace debris = vfx::debris_test;

class Poe2VfxDebrisTest : public ::testing::Test {
public:
	void SetUp() override
	{
		gbIsPoe2 = true;
		HeadlessMode = false;
		GetOptions().Graphics.physicsDebris.SetValue(true);
		// An open floor, with walls added by the tests.
		SOLData[0] = TileProperties::None;
		SOLData[1] = TileProperties::Solid | TileProperties::BlockLight | TileProperties::BlockMissile;
		for (int x = 25; x < 65; x++) {
			for (int y = 25; y < 65; y++)
				dPiece[x][y] = 0;
		}
		vfx::Clear();
	}

	void TearDown() override
	{
		vfx::Clear();
		for (int x = 25; x < 65; x++) {
			for (int y = 25; y < 65; y++)
				dPiece[x][y] = 0;
		}
		GetOptions().Graphics.physicsDebris.SetValue(true);
		HeadlessMode = true;
	}
};

TEST_F(Poe2VfxDebrisTest, APieceFallsBouncesLowerEachTimeAndComesToRest)
{
	debris::Throw(DebrisKind::Rubble, 40, 40, 200, 0, 0, 0);
	ASSERT_EQ(debris::Count(), 1U);
	std::vector<float> peaks { 200 };
	float lastVz = 0;
	bool rested = false;
	for (int i = 0; i < 400 && debris::Count() == 1; i++) {
		vfx::Advance(5);
		if (debris::Count() == 0)
			break;
		const debris::Piece piece = debris::At(0);
		ASSERT_GE(piece.z, 0.F);
		// The top of a bounce: going up, now coming down.
		if (lastVz > 0 && piece.vz <= 0)
			peaks.push_back(piece.z);
		lastVz = piece.vz;
		if (piece.resting) {
			rested = true;
			EXPECT_EQ(piece.z, 0.F);
			EXPECT_EQ(piece.vz, 0.F);
		}
	}
	EXPECT_TRUE(rested);
	ASSERT_GE(peaks.size(), 3U) << "it should bounce at least twice";
	for (size_t i = 1; i < peaks.size(); i++)
		EXPECT_LT(peaks[i], peaks[i - 1] * 0.5F) << "bounce " << i;
	// Once at rest it lingers a moment, fading, and is gone.
	for (int i = 0; i < 100 && debris::Count() > 0; i++)
		vfx::Advance(20);
	EXPECT_EQ(debris::Count(), 0U);
}

TEST_F(Poe2VfxDebrisTest, ARestingPieceFadesOutOverItsLinger)
{
	debris::Throw(DebrisKind::Droplet, 40, 40, 0, 0, 0, 0);
	vfx::Advance(16);
	ASSERT_EQ(debris::Count(), 1U);
	ASSERT_TRUE(debris::At(0).resting);
	EXPECT_FLOAT_EQ(debris::At(0).alpha, 1.F);
	vfx::Advance(700);
	ASSERT_EQ(debris::Count(), 1U);
	EXPECT_LT(debris::At(0).alpha, 0.5F);
	vfx::Advance(400);
	EXPECT_EQ(debris::Count(), 0U);
}

TEST_F(Poe2VfxDebrisTest, APieceBouncesOffAWallInsteadOfPassingThrough)
{
	// A wall along x = 42; tile 42 starts half a tile before its centre.
	for (int y = 30; y < 50; y++)
		dPiece[42][y] = 1;
	for (const DebrisKind kind : { DebrisKind::Rubble, DebrisKind::Droplet, DebrisKind::Spark }) {
		vfx::Clear();
		// Fast at the wall, low, and a little sideways.
		debris::Throw(kind, 40, 40, 10, 9, 1, 120);
		float farthest = 0;
		int wallHits = 0;
		bool turned = false;
		for (int i = 0; i < 300 && debris::Count() > 0; i++) {
			vfx::Advance(5);
			if (debris::Count() == 0)
				break;
			const debris::Piece piece = debris::At(0);
			farthest = std::max(farthest, piece.x);
			wallHits = piece.wallHits;
			if (piece.vx < 0)
				turned = true;
		}
		EXPECT_LT(farthest, 41.5F) << static_cast<int>(kind);
		EXPECT_GE(wallHits, 1) << static_cast<int>(kind);
		EXPECT_TRUE(turned || kind == DebrisKind::Droplet) << static_cast<int>(kind);
	}
	// Into a corner: it comes back out on both axes.
	for (int x = 30; x < 50; x++)
		dPiece[x][42] = 1;
	vfx::Clear();
	debris::Throw(DebrisKind::Rubble, 41, 41, 5, 6, 6, 60);
	for (int i = 0; i < 200 && debris::Count() > 0; i++) {
		vfx::Advance(5);
		if (debris::Count() == 0)
			break;
		const debris::Piece piece = debris::At(0);
		ASSERT_LT(piece.x, 41.5F);
		ASSERT_LT(piece.y, 41.5F);
	}
}

TEST_F(Poe2VfxDebrisTest, AFrameRateDoesNotChangeWhereAPieceSettles)
{
	// The same throw moved on at 5 frames a second, then at 200: it comes to rest in the same place.
	debris::Throw(DebrisKind::Rubble, 40, 40, 20, 2, -1, 250);
	for (int i = 0; i < 8; i++)
		vfx::Advance(200);
	ASSERT_EQ(debris::Count(), 1U);
	ASSERT_TRUE(debris::At(0).resting);
	const float slowX = debris::At(0).x;
	const float slowY = debris::At(0).y;
	EXPECT_GT(slowX, 40.5F) << "it should have moved";
	vfx::Clear();
	debris::Throw(DebrisKind::Rubble, 40, 40, 20, 2, -1, 250);
	for (int i = 0; i < 320; i++)
		vfx::Advance(5);
	ASSERT_EQ(debris::Count(), 1U);
	ASSERT_TRUE(debris::At(0).resting);
	EXPECT_NEAR(debris::At(0).x, slowX, 0.1F);
	EXPECT_NEAR(debris::At(0).y, slowY, 0.1F);
}

TEST_F(Poe2VfxDebrisTest, ThePieceCapHoldsAndTheOldestGoFirst)
{
	for (int i = 0; i < 500; i++)
		debris::Throw(DebrisKind::Rubble, 35 + (static_cast<float>(i) * 0.01F), 40, 50, 0, 0, 0);
	EXPECT_EQ(debris::Count(), vfx::MaxDebris);
	// The first hundred made way for the last hundred.
	EXPECT_NEAR(debris::At(0).x, 36.F, 0.001F);
	EXPECT_NEAR(debris::At(vfx::MaxDebris - 1).x, 39.99F, 0.001F);
	// Big hits never go past it either.
	for (int i = 0; i < 60; i++)
		vfx::Slam({ 40, 40 }, 3, vfx::Element::Physical);
	EXPECT_EQ(debris::Count(), vfx::MaxDebris);
}

TEST_F(Poe2VfxDebrisTest, HitsThrowDebrisWithoutTouchingTheGameRng)
{
	SetRndSeed(0x1234567);
	const uint32_t before = GetLCGEngineState();
	vfx::Slam({ 40, 40 }, 2, vfx::Element::Physical);
	const size_t slam = debris::Count();
	vfx::Fissure({ 36, 36 }, { 36, 41 }, vfx::Element::Physical);
	const size_t fissure = debris::Count() - slam;
	vfx::Impact({ 44, 44 }, vfx::Element::Blood, 1.2F);
	const size_t blood = debris::Count() - slam - fissure;
	vfx::Showcase({ 45, 40 });
	for (int i = 0; i < 300; i++)
		vfx::Advance(10);
	EXPECT_EQ(GetLCGEngineState(), before);
	// A slam of radius 2 throws eight chips at full intensity, a fissure's end three, a big bloody hit a few drops.
	EXPECT_EQ(slam, 8U);
	EXPECT_EQ(fissure, 3U);
	EXPECT_GE(blood, 3U);
	EXPECT_LE(blood, 4U);
	// And it all settles and goes.
	EXPECT_EQ(debris::Count(), 0U);
	// The game's dice carry on as if nothing was drawn.
	const int32_t next = GenerateRnd(1000);
	SetRndSeed(0x1234567);
	EXPECT_EQ(GenerateRnd(1000), next);
}

TEST_F(Poe2VfxDebrisTest, TheOptionOffThrowsNothing)
{
	GetOptions().Graphics.physicsDebris.SetValue(false);
	EXPECT_FALSE(vfx::DebrisEnabled());
	vfx::Slam({ 40, 40 }, 2, vfx::Element::Physical);
	vfx::Fissure({ 36, 36 }, { 36, 41 }, vfx::Element::Physical);
	vfx::Impact({ 44, 44 }, vfx::Element::Blood, 1.5F);
	vfx::Showcase({ 45, 40 });
	EXPECT_EQ(debris::Count(), 0U);
	// The slam's own effects still show.
	EXPECT_GT(vfx::ParticleCount(), 0U);
	GetOptions().Graphics.physicsDebris.SetValue(true);
	EXPECT_TRUE(vfx::DebrisEnabled());
	vfx::Slam({ 40, 40 }, 2, vfx::Element::Physical);
	EXPECT_GT(debris::Count(), 0U);
}

TEST_F(Poe2VfxDebrisTest, NothingIsThrownWithoutTheEffects)
{
	HeadlessMode = true;
	vfx::Slam({ 40, 40 }, 2, vfx::Element::Physical);
	EXPECT_EQ(debris::Count(), 0U);
	HeadlessMode = false;
	GetOptions().Graphics.modernEffects.SetValue(false);
	EXPECT_FALSE(vfx::DebrisEnabled());
	GetOptions().Graphics.modernEffects.SetValue(true);
}

TEST_F(Poe2VfxDebrisTest, TheUndeadShedBoneInsteadOfBlood)
{
	LoadMonsterData();
	// A monster of a kind standing on a tile, as far as the effects look.
	const auto place = [](size_t slot, _monster_id kind, Point tile) {
		LevelMonsterTypes[slot].type = kind;
		Monster &monster = Monsters[slot];
		monster = {};
		monster.levelType = static_cast<uint8_t>(slot);
		monster.position.tile = tile;
		dMonster[tile.x][tile.y] = static_cast<int16_t>(slot + 1);
	};
	ASSERT_EQ(MonstersData[MT_WSKELAX].monsterClass, MonsterClass::Undead);
	ASSERT_NE(MonstersData[MT_FIEND].monsterClass, MonsterClass::Undead);
	place(0, MT_WSKELAX, { 42, 40 });
	place(1, MT_FIEND, { 46, 40 });

	// A big bloody hit on a skeleton: pale chips of bone with rubble's physics, never pure white.
	vfx::Impact({ 42, 40 }, vfx::Element::Blood, 1.2F);
	ASSERT_GT(debris::Count(), 0U);
	const size_t bone = debris::Count();
	for (size_t i = 0; i < bone; i++) {
		const debris::Piece piece = debris::At(i);
		EXPECT_EQ(piece.kind, DebrisKind::Rubble);
		EXPECT_GT(piece.colour[0], 150);
		EXPECT_GT(piece.colour[2], 120);
		EXPECT_LT(piece.colour[0], 255);
		EXPECT_LT(piece.colour[1], 255);
		EXPECT_LT(piece.colour[2], 255);
	}

	// The same hit on a living monster flings dark red drops.
	vfx::Impact({ 46, 40 }, vfx::Element::Blood, 1.2F);
	ASSERT_GT(debris::Count(), bone);
	for (size_t i = bone; i < debris::Count(); i++) {
		const debris::Piece piece = debris::At(i);
		EXPECT_EQ(piece.kind, DebrisKind::Droplet);
		EXPECT_LT(piece.colour[0], 130);
		EXPECT_GT(piece.colour[0], piece.colour[1] * 4);
	}

	// And on bare floor, blood as before.
	vfx::Clear();
	vfx::Impact({ 50, 44 }, vfx::Element::Blood, 1.2F);
	ASSERT_GT(debris::Count(), 0U);
	EXPECT_EQ(debris::At(0).kind, DebrisKind::Droplet);

	dMonster[42][40] = 0;
	dMonster[46][40] = 0;
	Monsters[0] = {};
	Monsters[1] = {};
}

TEST_F(Poe2VfxDebrisTest, APieceIsNeverThrownInsideAWall)
{
	dPiece[40][40] = 1;
	debris::Throw(DebrisKind::Rubble, 40, 40, 10, 1, 0, 100);
	EXPECT_EQ(debris::Count(), 0U);
}

} // namespace
} // namespace devilution
