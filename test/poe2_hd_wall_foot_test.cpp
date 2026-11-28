#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

#include "engine/assets.hpp"
#include "engine/load_file.hpp"
#include "engine/render/dun_render.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "lighting.h"
#include "poe2/hd_wall_foot.hpp"
#include "utils/is_of.hpp"

using namespace devilution;

namespace {

uint32_t Hash(uint32_t a, uint32_t b)
{
	uint32_t h = a * 73856093U ^ b * 19349663U ^ 0x9E3779B9U;
	h ^= h >> 13;
	h *= 0x5BD1E995U;
	h ^= h >> 15;
	return h;
}

/** @brief A floor piece of the left column: its half diamond in colours 16 to 79, a pattern by `seed`. */
hd::FootPixels Floor(uint32_t seed)
{
	hd::FootPixels floor;
	for (int y = 0; y < hd::FootSize; y++) {
		for (int x = 0; x < hd::FootSize; x++) {
			if (y == 0 || !hd::InFootDiamond(false, x, y))
				continue;
			const auto i = static_cast<size_t>(y * hd::FootSize + x);
			floor.drawn[i] = 255;
			floor.index[i] = static_cast<uint8_t>(16 + Hash(seed, static_cast<uint32_t>(i)) % 64);
		}
	}
	return floor;
}

hd::FootFloors Floors()
{
	hd::FootFloors floors;
	floors.left = { Floor(1), Floor(2), Floor(3) };
	return floors;
}

/** @brief Where the made-up door leaf stands: a band of the left column from its top down to near the tile's front edge. */
bool InLeaf(int x, int y)
{
	return x >= 18 && x < 26 && y < 27;
}

/**
 * @brief A wall's bottom piece made up as the game's are: the floor (Floor(2)) with the wall drawn over it, above the
 * floor's back edge and in a door leaf that stands on the floor in front of it. The wall is in colours 128 to 255,
 * and a few pixels of the floor are stained (not as in any floor piece).
 */
hd::FootPixels WallPiece(hd::FootParts &truth, uint32_t floorSeed = 2)
{
	hd::FootPixels piece = Floor(floorSeed);
	for (int y = 0; y < hd::FootSize; y++) {
		for (int x = 0; x < hd::FootSize; x++) {
			const auto i = static_cast<size_t>(y * hd::FootSize + x);
			const bool wall = InLeaf(x, y) || piece.drawn[i] == 0;
			truth[i] = wall ? hd::FootPart::Wall : hd::FootPart::Floor;
			if (!wall)
				continue;
			piece.drawn[i] = 255;
			piece.index[i] = static_cast<uint8_t>(128 + Hash(7, static_cast<uint32_t>(i)) % 128);
		}
	}
	// A stain on the floor, away from the wall.
	for (const auto &[x, y] : { std::pair { 10, 15 }, std::pair { 11, 15 }, std::pair { 10, 16 }, std::pair { 28, 20 } })
		piece.index[static_cast<size_t>(y * hd::FootSize + x)] = 200;
	return piece;
}

TEST(HdWallFoot, EachPixelGoesWithTheWallOrTheFloor)
{
	hd::FootParts truth;
	const hd::FootPixels piece = WallPiece(truth);
	const hd::FootFloors floors = Floors();
	hd::FootParts parts;
	ASSERT_TRUE(hd::SplitWallFoot(piece, floors, false, parts));
	int leafInDiamond = 0;
	for (int y = 0; y < hd::FootSize; y++) {
		for (int x = 0; x < hd::FootSize; x++) {
			const auto i = static_cast<size_t>(y * hd::FootSize + x);
			EXPECT_EQ(parts[i], truth[i]) << x << "," << y;
			if (InLeaf(x, y) && hd::InFootDiamond(false, x, y))
				leafInDiamond++;
		}
	}
	// The leaf reaches well over the floor's back edge: cut along it, all that took the floor's light.
	EXPECT_GE(leafInDiamond, 100);
}

TEST(HdWallFoot, AFloorDrawnAnewUnderTheWallIsFoundByItsColours)
{
	// The floor under this wall is like no floor piece of the level (as in town and the Cathedral, where the wall's
	// pieces were drawn with a floor of their own), but in the floors' colours.
	hd::FootParts truth;
	const hd::FootPixels piece = WallPiece(truth, 9);
	hd::FootFloors floors = Floors();
	std::vector<hd::FootPixels> standing(4);
	for (size_t n = 0; n < standing.size(); n++) {
		for (size_t i = 0; i < hd::FootPixelCount; i++) {
			standing[n].drawn[i] = 255;
			standing[n].index[i] = static_cast<uint8_t>(128 + Hash(static_cast<uint32_t>(20 + n), static_cast<uint32_t>(i)) % 128);
		}
	}
	hd::CountFootColours(floors, standing);
	ASSERT_TRUE(floors.coloured);
	hd::FootParts parts;
	ASSERT_TRUE(hd::SplitWallFoot(piece, floors, false, parts));
	int wrong = 0;
	int leafAsWall = 0;
	for (int y = 0; y < hd::FootSize; y++) {
		for (int x = 0; x < hd::FootSize; x++) {
			const auto i = static_cast<size_t>(y * hd::FootSize + x);
			wrong += parts[i] != truth[i] ? 1 : 0;
			leafAsWall += InLeaf(x, y) && hd::InFootDiamond(false, x, y) && parts[i] == hd::FootPart::Wall ? 1 : 0;
		}
	}
	// Only at the leaf's bottom corners, where more floor than leaf is round a pixel.
	EXPECT_LE(wrong, 6);
	EXPECT_GE(leafAsWall, 100);
}

TEST(HdWallFoot, WithNoFloorsItIsCutAlongTheBackEdge)
{
	hd::FootParts truth;
	const hd::FootPixels piece = WallPiece(truth);
	hd::FootParts parts;
	ASSERT_TRUE(hd::SplitWallFoot(piece, hd::FootFloors {}, false, parts));
	for (int y = 0; y < hd::FootSize; y++) {
		for (int x = 0; x < hd::FootSize; x++)
			EXPECT_EQ(parts[static_cast<size_t>(y * hd::FootSize + x)] == hd::FootPart::Floor, y > 0 && hd::InFootDiamond(false, x, y)) << x << "," << y;
	}
}

TEST(HdWallFoot, AllWallHasNoFloor)
{
	hd::FootPixels piece;
	for (size_t i = 0; i < hd::FootPixelCount; i++) {
		piece.drawn[i] = 255;
		piece.index[i] = static_cast<uint8_t>(Hash(9, static_cast<uint32_t>(i)));
	}
	hd::FootParts parts;
	EXPECT_FALSE(hd::SplitWallFoot(piece, Floors(), false, parts));
}

/**
 * @brief The piece drawn as the HD world draws it: its floor's part in the floor's light over black (the floor's pass),
 * then its wall's part in the wall's light over that (with the walls), each blended by its alpha as SDL does.
 */
std::vector<float> DrawBothParts(const std::vector<uint8_t> &floorPart, const std::vector<uint8_t> &wallPart, float floorLight, float wallLight)
{
	std::vector<float> out(floorPart.size() / 4 * 3, 0.F);
	for (const auto &[part, light] : { std::pair { &floorPart, floorLight }, std::pair { &wallPart, wallLight } }) {
		for (size_t p = 0; p < out.size() / 3; p++) {
			const float alpha = static_cast<float>((*part)[p * 4 + 3]) / 255;
			for (size_t c = 0; c < 3; c++)
				out[p * 3 + c] = static_cast<float>((*part)[p * 4 + c]) * light * alpha + out[p * 3 + c] * (1 - alpha);
		}
	}
	return out;
}

TEST(HdWallFoot, TheWallsPartTakesTheWallsShadeAndTheFloorsTheFloorsLight)
{
	constexpr int Scale = 2;
	constexpr int Side = hd::FootSize * Scale;
	hd::FootParts truth;
	const hd::FootPixels piece = WallPiece(truth);
	hd::FootParts parts;
	ASSERT_TRUE(hd::SplitWallFoot(piece, Floors(), false, parts));
	// An HD picture of it: any colours, solid where the game draws.
	std::vector<uint8_t> art(static_cast<size_t>(Side) * Side * 4);
	for (int y = 0; y < Side; y++) {
		for (int x = 0; x < Side; x++) {
			const auto p = static_cast<size_t>(y * Side + x);
			for (size_t c = 0; c < 3; c++)
				art[p * 4 + c] = static_cast<uint8_t>(40 + Hash(static_cast<uint32_t>(p), static_cast<uint32_t>(c)) % 200);
			art[p * 4 + 3] = piece.drawn[static_cast<size_t>((y / Scale) * hd::FootSize + x / Scale)];
		}
	}
	std::vector<uint8_t> floorPart(art.size());
	std::vector<uint8_t> wallPart(art.size());
	int floorCount = 0;
	int wallCount = 0;
	hd::SplitFootArt(art.data(), Side, Side, Side * 4, parts, Scale, floorPart.data(), wallPart.data(), floorCount, wallCount);
	EXPECT_EQ(floorCount + wallCount, Side * Side);
	constexpr float FloorLight = 0.9F;
	constexpr float WallShade = 0.35F;
	const std::vector<float> drawn = DrawBothParts(floorPart, wallPart, FloorLight, WallShade);
	// As it was: the half diamond in the floor's light, the rest in the wall's.
	int steppedBefore = 0;
	for (int y = 0; y < Side; y++) {
		for (int x = 0; x < Side; x++) {
			const auto p = static_cast<size_t>(y * Side + x);
			const bool wall = truth[static_cast<size_t>((y / Scale) * hd::FootSize + x / Scale)] == hd::FootPart::Wall;
			const float light = wall ? WallShade : FloorLight;
			for (size_t c = 0; c < 3; c++)
				ASSERT_NEAR(drawn[p * 3 + c], static_cast<float>(art[p * 4 + c]) * light, 0.01F) << x << "," << y << (wall ? " wall" : " floor");
			if (wall && hd::InFootDiamond(false, x / Scale, y / Scale))
				steppedBefore++;
		}
	}
	EXPECT_GE(steppedBefore, 400);
}

class HdWallFootTown : public ::testing::Test {
protected:
	void SetUp() override
	{
		LoadCoreArchives();
		LoadGameArchives();
		if (!HaveMainData())
			GTEST_SKIP() << "MPQ assets (spawn.mpq or DIABDAT.MPQ) not found";
		leveltype = DTYPE_TOWN;
		std::memset(DPieceMicros, 0, sizeof(DPieceMicros));
		ASSERT_TRUE(LoadLevelSOLData().has_value());
		pDungeonCels = LoadFileInMem("levels\\towndata\\town.cel");
		ASSERT_NE(pDungeonCels, nullptr);
		SetDungeonMicros(pDungeonCels, MicroTileLen);
		MakeLightTable();
	}
	void TearDown() override
	{
		pDungeonCels = nullptr;
	}

	/** @brief A tile's bottom piece (block 0 or 1) split as the HD world splits it, with how many of its pixels in the floor's half diamond are wall and floor. */
	static void Split(int piece, int block, const hd::FootFloors &floors, int &wallIn, int &floorIn)
	{
		const LevelCelBlock cel = DPieceMicros[piece].mt[block];
		ASSERT_TRUE(cel.hasValue());
		const hd::FootPixels pixels = hd::ReadFootPiece(GetDunFrame(pDungeonCels.get(), cel.frame()), cel.type(), DunFrameHeight);
		hd::FootParts parts;
		hd::SplitWallFoot(pixels, floors, block == 1, parts, hd::FootAbove(static_cast<size_t>(piece), block == 1));
		wallIn = 0;
		floorIn = 0;
		for (int y = 0; y < hd::FootSize; y++) {
			for (int x = 0; x < hd::FootSize; x++) {
				if (!hd::InFootDiamond(block == 1, x, y))
					continue;
				const hd::FootPart part = parts[static_cast<size_t>(y * hd::FootSize + x)];
				wallIn += part == hd::FootPart::Wall ? 1 : 0;
				floorIn += part == hd::FootPart::Floor ? 1 : 0;
			}
		}
	}
};

TEST_F(HdWallFootTown, ADoorLeafAtTheTilesFrontIsAllWall)
{
	const hd::FootFloors floors = hd::LevelFootFloors();
	ASSERT_FALSE(floors.left.empty());
	ASSERT_FALSE(floors.right.empty());
	ASSERT_TRUE(floors.coloured);
	// A house's door (town.til block 139, its piece 530): the door's leaf fills the left column's bottom piece down to
	// the tile's front edge. Cut along the floor's back edge, its whole half diamond (512 pixels) took the floor's light.
	int wallIn = 0;
	int floorIn = 0;
	Split(530, 0, floors, wallIn, floorIn);
	EXPECT_GE(wallIn, 480);
	// The doorway beside it (piece 528) as well.
	Split(528, 0, floors, wallIn, floorIn);
	EXPECT_GE(wallIn, 480);
}

TEST_F(HdWallFootTown, ATreeStandsOnTheFloor)
{
	const hd::FootFloors floors = hd::LevelFootFloors();
	// A bare tree (piece 167): its trunk is the wall's, the grass round it the floor's.
	for (const int block : { 0, 1 }) {
		int wallIn = 0;
		int floorIn = 0;
		Split(167, block, floors, wallIn, floorIn);
		EXPECT_GE(wallIn, 30) << block;
		EXPECT_LE(wallIn, 150) << block;
		EXPECT_GE(floorIn, 350) << block;
	}
}

} // namespace
