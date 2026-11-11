#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "engine/assets.hpp"
#include "engine/displacement.hpp"
#include "engine/load_file.hpp"
#include "engine/palette.h"
#include "engine/random.hpp"
#include "engine/render/dun_render.hpp"
#include "engine/render/light_render.hpp"
#include "engine/surface.hpp"
#include "items.h"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "levels/setmaps.h"
#include "levels/tile_properties.hpp"
#include "levels/tileset_mix.hpp"
#include "levels/trigs.h"
#include "lighting.h"
#include "monster.h"
#include "multi.h"
#include "objects.h"
#include "options.h"
#include "player.h"
#include "quests.h"
#include "tables/objdat.h"
#include "utils/sdl_wrap.h"

using namespace devilution;

namespace {

constexpr const char MissingMpqAssetsSkipReason[] = "MPQ assets (spawn.mpq or DIABDAT.MPQ) not found - skipping test suite";

class TilesetMixTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		LoadCoreArchives();
		LoadGameArchives();
		if (!HaveMainData())
			GTEST_SKIP() << MissingMpqAssetsSkipReason;
		Players.resize(1);
		MyPlayer = &Players[0];
		sgGameInitInfo.fullQuests = 1;
		gbIsMultiplayer = false;
		LoadQuestData();
		LoadMonsterData();
		LoadObjectData();
		LoadItemData();
		InitQuests();

		setlevel = true;
		setlvlnum = SL_MIXTEST;
		setlvltype = DTYPE_CATHEDRAL;
		leveltype = DTYPE_CATHEDRAL;
		pDungeonCels = nullptr;
		tilesetmix::SelectForCurrentLevel();
	}

	void TearDown() override
	{
		tilesetmix::Clear();
		tilesetmix::SetRandomFloors(false);
		pDungeonCels = nullptr;
		setlevel = false;
		setlvlnum = SL_NONE;
	}

	void LoadTiles()
	{
		MakeLightTable();
		ASSERT_TRUE(tilesetmix::LoadMixedSOLData().has_value());
		ASSERT_TRUE(tilesetmix::LoadMixedLvlGFX().has_value());
		tilesetmix::SetMixedDungeonMicros(pDungeonCels, MicroTileLen);
	}
};

TEST_F(TilesetMixTest, MergesTownIntoCathedral)
{
	ASSERT_TRUE(tilesetmix::Active());
	LoadTiles();

	const std::vector<tilesetmix::LoadedSet> &sets = tilesetmix::LoadedSets();
	ASSERT_EQ(sets.size(), 2U);
	EXPECT_EQ(sets[0].type, DTYPE_CATHEDRAL);
	EXPECT_EQ(sets[1].type, DTYPE_TOWN);
	EXPECT_EQ(sets[0].pieceOffset, 0);
	EXPECT_EQ(sets[0].pieceCount, 453);
	EXPECT_EQ(sets[1].pieceOffset, 453);
	EXPECT_GE(sets[1].pieceCount, 1258);
	EXPECT_EQ(sets[0].frameCount, 1119);
	EXPECT_EQ(sets[1].frameOffset, 1119);
	EXPECT_GE(sets[1].frameCount, 3547);
	EXPECT_EQ(MicroTileLen, 16);

	// Every piece of both sets has at least one picture, and every picture number is inside the merged cel.
	const auto *table = reinterpret_cast<const uint32_t *>(pDungeonCels.get());
	const uint32_t frameCount = table[0];
	EXPECT_GE(frameCount, 1119U + 3547U);
	for (const tilesetmix::LoadedSet &set : sets) {
		int empty = 0;
		for (size_t p = 0; p < set.pieceCount; ++p) {
			bool any = false;
			for (int b = 0; b < 16; ++b) {
				const LevelCelBlock block = DPieceMicros[set.pieceOffset + p].mt[b];
				if (!block.hasValue())
					continue;
				any = true;
				EXPECT_GE(block.frame(), 1U);
				EXPECT_LE(block.frame(), frameCount);
			}
			if (!any)
				++empty;
		}
		EXPECT_LT(empty, 4) << "set " << static_cast<int>(set.type) << " has pieces without pictures";
	}

	// Town pieces sit above block 10, Cathedral pieces never do.
	bool townTall = false;
	for (size_t p = 0; p < sets[1].pieceCount; ++p)
		for (int b = 10; b < 16; ++b)
			if (DPieceMicros[sets[1].pieceOffset + p].mt[b].hasValue()) townTall = true;
	EXPECT_TRUE(townTall);
	for (size_t p = 0; p < sets[0].pieceCount; ++p)
		for (int b = 10; b < 16; ++b)
			EXPECT_FALSE(DPieceMicros[p].mt[b].hasValue());

	// The shared palette is eight ramps that darken: inside each ramp the brightness never rises.
	const std::array<uint8_t, 128 * 3> &pal = tilesetmix::SharedPalette();
	int nonBlack = 0;
	for (int ramp = 0; ramp < 8; ++ramp) {
		float last = 1e9F;
		for (int s = 0; s < 16; ++s) {
			const int i = ramp * 16 + s;
			const float lum = 0.299F * pal[i * 3] + 0.587F * pal[i * 3 + 1] + 0.114F * pal[i * 3 + 2];
			if (i > 1) {
				EXPECT_LE(lum, last + 0.6F) << "ramp " << ramp << " step " << s;
			}
			last = lum;
			if (pal[i * 3] + pal[i * 3 + 1] + pal[i * 3 + 2] > 0)
				++nonBlack;
		}
	}
	EXPECT_GT(nonBlack, 100);
}

TEST_F(TilesetMixTest, SameSeedSameFrames)
{
	LoadTiles();
	std::vector<uint16_t> first;
	for (size_t p = 0; p < 1711; ++p)
		for (int b = 0; b < 16; ++b)
			first.push_back(DPieceMicros[p].mt[b].data);
	const std::array<uint8_t, 128 * 3> pal = tilesetmix::SharedPalette();

	tilesetmix::Clear();
	pDungeonCels = nullptr;
	tilesetmix::SelectForCurrentLevel();
	LoadTiles();
	for (size_t p = 0; p < 1711; ++p)
		for (int b = 0; b < 16; ++b)
			ASSERT_EQ(DPieceMicros[p].mt[b].data, first[p * 16 + b]);
	EXPECT_EQ(pal, tilesetmix::SharedPalette());
}

TEST_F(TilesetMixTest, TestLevelHasTownYard)
{
	LoadTiles();
	SetRndSeed(1234);
	ASSERT_TRUE(LoadSetMap().has_value());

	const std::vector<tilesetmix::LoadedSet> &sets = tilesetmix::LoadedSets();
	int townPieces = 0;
	int cathedralPieces = 0;
	for (int x = 0; x < MAXDUNX; ++x) {
		for (int y = 0; y < MAXDUNY; ++y) {
			if (dPiece[x][y] >= sets[1].pieceOffset)
				++townPieces;
			else if (dPiece[x][y] != 0)
				++cathedralPieces;
		}
	}
	EXPECT_EQ(townPieces, 16 * 16);
	EXPECT_GT(cathedralPieces, 200);
	// The yard's edge ring is flat ground, so you can always walk in.
	const Point corner = Point { 3, 13 }.megaToWorld();
	EXPECT_FALSE(IsTileSolid(corner));
	EXPECT_EQ(numtrigs, 1);

	// Optional picture of the yard for a human to look at: POE2_MIX_DUMP=<file prefix>.
	const char *dump = std::getenv("POE2_MIX_DUMP");
	if (dump == nullptr)
		return;
	constexpr int Width = 1600;
	constexpr int Height = 1100;
	SDLSurfaceUniquePtr sdlSurface = SDLWrap::CreateRGBSurfaceWithFormat(0, Width, Height, 8, SDL_PIXELFORMAT_INDEX8);
	ASSERT_NE(sdlSurface, nullptr);
	const Surface out(sdlSurface.get());
	std::memset(sdlSurface->pixels, 0, static_cast<size_t>(sdlSurface->pitch) * Height);
	GetOptions().Graphics.perPixelLighting.SetValue(false);
	const Lightmap lightmap(nullptr, {}, 1, LightTables, FullyLitLightTable, FullyDarkLightTable);
	const uint8_t *tbl = LightTables[0].data();
	// World tiles 16..60 x 16..64 cover the arena.
	for (int sum = 32; sum < 125; ++sum) {
		for (int x = 16; x < 61; ++x) {
			const int y = sum - x;
			if (y < 16 || y >= 65)
				continue;
			const uint16_t piece = dPiece[x][y];
			if (piece == 0)
				continue;
			const MICROS &micros = DPieceMicros[piece];
			Point pos { (x - y) * 32 + Width / 2 - 32, (x + y - 32) * 16 + 32 * 10 };
			if (pos.y >= Height || pos.x < 0 || pos.x + 64 > Width)
				continue;
			for (int b = 0; b < MicroTileLen; b += 2) {
				if (micros.mt[b].hasValue())
					RenderTile(out, lightmap, pos, pDungeonCels.get(), micros.mt[b], MaskType::Solid, tbl);
				if (micros.mt[b + 1].hasValue())
					RenderTile(out, lightmap, pos + Displacement { 32, 0 }, pDungeonCels.get(), micros.mt[b + 1], MaskType::Solid, tbl);
				pos.y -= 32;
			}
		}
	}
	const std::string raw = std::string(dump) + ".raw";
	FILE *f = std::fopen(raw.c_str(), "wb");
	ASSERT_NE(f, nullptr);
	for (int y = 0; y < Height; ++y)
		std::fwrite(static_cast<const uint8_t *>(sdlSurface->pixels) + static_cast<size_t>(y) * sdlSurface->pitch, 1, Width, f);
	std::fclose(f);
	// Palette: shared tile colours, then the Cathedral's own upper 128.
	const std::string palPath = std::string(dump) + ".pal";
	f = std::fopen(palPath.c_str(), "wb");
	ASSERT_NE(f, nullptr);
	std::array<uint8_t, 256 * 3> upper {};
	LoadFileInMem("levels\\l1data\\l1_1.pal", upper.data(), upper.size());
	std::fwrite(tilesetmix::SharedPalette().data(), 1, 128 * 3, f);
	std::fwrite(upper.data() + 128 * 3, 1, 128 * 3, f);
	std::fclose(f);
}

TEST_F(TilesetMixTest, RandomCathedralFloorsGetFields)
{
	setlevel = false;
	setlvlnum = SL_NONE;
	currlevel = 2;
	leveltype = DTYPE_CATHEDRAL;
	tilesetmix::SetRandomFloors(true);
	tilesetmix::SelectForCurrentLevel();
	ASSERT_TRUE(tilesetmix::Active());
	LoadTiles();
	const uint16_t townOffset = tilesetmix::LoadedSets()[1].pieceOffset;

	int levelsWithFields = 0;
	for (const uint32_t seed : { 12345U, 777U, 2024U, 99U, 4242U, 31337U, 8U, 1000001U }) {
		LevelSeeds[currlevel] = std::nullopt;
		CreateDungeon(seed, ENTRY_MAIN);
		const uint32_t rngAfterLevel = GetLCGEngineState();
		std::array<std::array<uint8_t, DMAXY>, DMAXX> before {};
		for (int x = 0; x < DMAXX; ++x)
			for (int y = 0; y < DMAXY; ++y)
				before[x][y] = dungeon[x][y];
		std::vector<uint16_t> plain(&dPiece[0][0], &dPiece[0][0] + MAXDUNX * MAXDUNY);

		const int areas = tilesetmix::PlaceGuestRegions(seed);
		EXPECT_EQ(GetLCGEngineState(), rngAfterLevel) << "the level's own random numbers must not move";
		EXPECT_LE(areas, 3);
		if (areas > 0)
			++levelsWithFields;

		int townPieces = 0;
		for (int x = 0; x < MAXDUNX; ++x) {
			for (int y = 0; y < MAXDUNY; ++y) {
				if (dPiece[x][y] == plain[x * MAXDUNY + y])
					continue;
				++townPieces;
				EXPECT_GE(dPiece[x][y], townOffset);
				// Only plain room floor ever turns into field, and the map still calls it floor.
				const int mx = (x - 16) / 2;
				const int my = (y - 16) / 2;
				ASSERT_TRUE(mx >= 0 && my >= 0 && mx < DMAXX && my < DMAXY);
				EXPECT_TRUE(before[mx][my] == 13 || before[mx][my] == 162 || before[mx][my] == 163) << static_cast<int>(before[mx][my]);
				EXPECT_EQ(dungeon[mx][my], 13);
				EXPECT_FALSE(Protected.test(mx, my));
			}
		}
		EXPECT_EQ(townPieces > 0, areas > 0) << "seed " << seed;

		// Same seed, same fields.
		std::vector<uint16_t> first(&dPiece[0][0], &dPiece[0][0] + MAXDUNX * MAXDUNY);
		LevelSeeds[currlevel] = std::nullopt;
		CreateDungeon(seed, ENTRY_MAIN);
		tilesetmix::PlaceGuestRegions(seed);
		EXPECT_TRUE(std::equal(first.begin(), first.end(), &dPiece[0][0])) << "seed " << seed;

		if (const char *dump = std::getenv("POE2_MIX_FLOOR_DUMP"); dump != nullptr) {
			FILE *f = std::fopen((std::string(dump) + "_" + std::to_string(seed) + ".bin").c_str(), "wb");
			ASSERT_NE(f, nullptr);
			const int header[4] = { ViewPosition.x, ViewPosition.y, townOffset, areas };
			std::fwrite(header, sizeof(int), 4, f);
			std::fwrite(tilesetmix::SharedPalette().data(), 1, 128 * 3, f);
			for (auto &t : LightTables)
				std::fwrite(t.data(), 1, t.size(), f);
			for (int x = 0; x < MAXDUNX; ++x)
				std::fwrite(dPiece[x], sizeof(dPiece[x][0]), MAXDUNY, f);
			std::fclose(f);
		}
	}
	EXPECT_GE(levelsWithFields, 6) << "most Cathedral floors have room for a field";
}

} // namespace
