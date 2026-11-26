// How long the HD Lighting's light takes to work out for one frame (hd::light::Update), on a made-up floor of
// rooms the size of the Catacombs': with the hero standing still (everything a light sees is kept from the frame
// before) and walking (the hero's light sees the floor afresh every eighth of a tile).
//
// Each run also reports a checksum: a hash of every pixel of the light it worked out, and of the light read back at
// points the way the renderer reads it, over the same walk. A change meant only to make this faster must leave the
// checksum as it was (on the same machine and compiler).

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

#include <benchmark/benchmark.h>

#include "game_mode.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "lighting.h"
#include "missiles.h"
#include "monster.h"
#include "objects.h"
#include "player.h"
#include "poe2/hd_light.hpp"

namespace devilution {
namespace {

constexpr uint16_t FloorPiece = 0;
constexpr uint16_t WallPiece = 1;
constexpr int HeroTileX = 56;
constexpr int HeroTileY = 56;
/** @brief Half the width of the world on screen at 3440x1440, zoomed in: about 27 tiles across and 23 down. */
constexpr float ViewReach = 26.F;

/** @brief Rock everywhere, with rooms of 9 by 9 tiles cut into it: a door in each wall, and a pillar in each room. */
void MakeFloor()
{
	leveltype = DTYPE_CATACOMBS;
	currlevel = 5;
	setlevel = false;
	SOLData[FloorPiece] = TileProperties::None;
	SOLData[WallPiece] = TileProperties::Solid | TileProperties::BlockLight | TileProperties::BlockMissile;
	std::memset(dPreLight, 15, sizeof(dPreLight));
	std::memset(dLight, 15, sizeof(dLight));
	for (int x = 0; x < MAXDUNX; x++) {
		for (int y = 0; y < MAXDUNY; y++) {
			const int rx = x % 10;
			const int ry = y % 10;
			const bool inside = x >= 16 && y >= 16 && x < MAXDUNX - 16 && y < MAXDUNY - 16;
			bool wall = !inside || rx == 0 || ry == 0;
			// A door in the middle of each wall.
			if (inside && ((rx == 0 && ry == 5) || (ry == 0 && rx == 5)) && x > 16 && y > 16)
				wall = false;
			// A pillar off the middle of each room.
			if (rx == 3 && ry == 7)
				wall = true;
			dPiece[x][y] = wall ? WallPiece : FloorPiece;
		}
	}
	ActiveObjectCount = 0;
	ActiveMonsterCount = 0;
	Missiles.clear();

	Players.resize(1);
	MyPlayer = &Players[0];
	Player &player = Players[0];
	player.plractive = true;
	player.plrlevel = currlevel;
	player.plrIsOnSetLevel = false;
	player.lightId = 0;
	// The hero's light, and a few more standing about the rooms nearby (as other heroes' or monsters' would).
	const std::array<Point, 5> at { { { HeroTileX, HeroTileY }, { 44, 55 }, { 66, 47 }, { 57, 68 }, { 45, 44 } } };
	ActiveLightCount = static_cast<int>(at.size());
	for (size_t i = 0; i < at.size(); i++) {
		ActiveLights[i] = static_cast<uint8_t>(i);
		Light &light = Lights[i];
		light = {};
		light.position.tile = at[i];
		light.radius = i == 0 ? 10 : 8;
		light.isInvalid = false;
	}
}

hd::light::Look GentleLook()
{
	hd::light::Look look;
	for (size_t level = 0; level < NumLightingLevels; level++)
		look.levelBrightness[level] = 1.F - static_cast<float>(level) / static_cast<float>(NumLightingLevels - 1);
	look.falloff = 1.15F;
	look.warm = { 1.05F, 0.97F, 0.86F };
	look.cool = { 0.86F, 0.91F, 1.05F };
	look.tint = { 1.F, 1.F, 1.F };
	return look;
}

uint64_t Mix(uint64_t hash, const void *data, size_t size)
{
	const auto *bytes = static_cast<const uint8_t *>(data);
	for (size_t i = 0; i < size; i++)
		hash = (hash ^ bytes[i]) * 1099511628211ULL;
	return hash;
}

/** @brief Everything the renderer reads of the light this frame, hashed. */
uint64_t HashLight(uint64_t hash)
{
	const size_t count = static_cast<size_t>(hd::light::FieldWidth()) * static_cast<size_t>(hd::light::FieldHeight());
	hash = Mix(hash, hd::light::FieldPixels(), count * sizeof(uint32_t));
	hash = Mix(hash, hd::light::FieldMulPixels(), count * sizeof(uint32_t));
	for (int step = 0; step < 64; step++) {
		const float x = static_cast<float>(HeroTileX) - 12.F + static_cast<float>(step) * 0.37F;
		const float y = static_cast<float>(HeroTileY) - 9.F + static_cast<float>(step) * 0.29F;
		for (const hd::light::Rgb &rgb : { hd::light::At(x, y), hd::light::GroundAt(x, y), hd::light::FaceAt(x, y), hd::light::BehindAt(x, y, (step & 1) != 0), hd::light::RockTopAt(x, y) })
			hash = Mix(hash, &rgb, sizeof(rgb));
	}
	return hash;
}

/** @brief Where the hero is drawn on frame `frame` of a walk round the room: an eighth of a tile further each frame. */
void HeroAt(int frame, bool walking, float &x, float &y)
{
	x = static_cast<float>(HeroTileX);
	y = static_cast<float>(HeroTileY);
	if (!walking)
		return;
	// Out along x for 24 frames (three tiles) and back, and half as far along y.
	const int phase = frame % 48;
	const int along = phase < 24 ? phase : 48 - phase;
	x += static_cast<float>(along) / 8.F;
	y += static_cast<float>(along / 2) / 8.F;
}

void RunUpdate(benchmark::State &state, bool walking)
{
	// (Without the mod the Atmosphere option's flicker and the like are off: this times the light itself.)
	gbIsPoe2 = false;
	MakeFloor();
	hd::light::Reset();
	const hd::light::Look look = GentleLook();
	const auto update = [&](int frame) {
		float heroX;
		float heroY;
		HeroAt(frame, walking, heroX, heroY);
		return hd::light::Update(look, heroX - ViewReach, heroY - ViewReach, heroX + ViewReach, heroY + ViewReach, heroX, heroY);
	};
	// The same walk every run, hashed before the clock starts.
	uint64_t checksum = 14695981039346656037ULL;
	for (int frame = 0; frame < 96; frame++) {
		if (!update(frame)) {
			state.SkipWithError("hd::light::Update had no level to light");
			return;
		}
		checksum = HashLight(checksum);
	}
	int frame = 0;
	for (auto _ : state) {
		bool lit = update(frame++);
		benchmark::DoNotOptimize(lit);
	}
	state.counters["samples"] = static_cast<double>(hd::light::FieldWidth()) * hd::light::FieldHeight();
	char text[32];
	std::snprintf(text, sizeof(text), "checksum %016llx", static_cast<unsigned long long>(checksum));
	state.SetLabel(text);
}

void BM_LightStanding(benchmark::State &state)
{
	RunUpdate(state, false);
}

void BM_LightWalking(benchmark::State &state)
{
	RunUpdate(state, true);
}

BENCHMARK(BM_LightStanding)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_LightWalking)->Unit(benchmark::kMicrosecond);

} // namespace
} // namespace devilution
