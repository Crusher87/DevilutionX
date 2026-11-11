#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "drlg_test.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "levels/themes.h"
#include "monster.h"
#include "objects.h"
#include "options.h"
#include "poe2/cathedral_floors.hpp"
#include "poe2/floors.hpp"
#include "tables/objdat.h"

namespace devilution {
namespace {

/** Ends the test run when the guarded work does not finish in time, naming what it was doing (a hang would never fail). */
class Watchdog {
public:
	explicit Watchdog(std::chrono::seconds limit)
	    : limit_(limit)
	    , thread_([this]() {
		    while (!done_) {
			    std::this_thread::sleep_for(std::chrono::milliseconds(50));
			    const std::lock_guard<std::mutex> lock(mutex_);
			    if (armed_ && std::chrono::steady_clock::now() > deadline_) {
				    std::fprintf(stderr, "Hung for over %d seconds: %s\n", static_cast<int>(limit_.count()), what_.c_str());
				    std::fflush(stderr);
				    std::_Exit(1);
			    }
		    }
	    })
	{
	}

	~Watchdog()
	{
		done_ = true;
		thread_.join();
	}

	template <typename F>
	void Guard(const std::string &what, F &&work)
	{
		{
			const std::lock_guard<std::mutex> lock(mutex_);
			what_ = what;
			deadline_ = std::chrono::steady_clock::now() + limit_;
			armed_ = true;
		}
		work();
		const std::lock_guard<std::mutex> lock(mutex_);
		armed_ = false;
	}

private:
	std::chrono::seconds limit_;
	std::mutex mutex_;
	std::string what_;
	std::chrono::steady_clock::time_point deadline_;
	bool armed_ = false;
	std::atomic<bool> done_ { false };
	std::thread thread_;
};

class Poe2CathedralObjectsTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		paths::SetPrefPath(paths::BasePath() + "test/fixtures/");
		TestInitGame(true, false, true);
		LoadGameArchives();
		if (!HaveMainData())
			GTEST_SKIP() << "MPQ assets (spawn.mpq or DIABDAT.MPQ) not found - skipping";
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		gbIsHellfire = true;
		gbIsPoe2 = true;
		LoadMonsterData();
		LoadObjectData();
		InitQuests();
		SetCathedralRooms({});
		Poe2FloorsReset();
		GetOptions().Gameplay.newCathedralFloors.SetValue(true);
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		gbIsHellfire = false;
		SetCathedralFunnelFloor(false);
		pMegaTiles = nullptr;
	}
};

/**
 * Builds fresh Cathedral floors the way a first visit does and places their objects. The designed rooms bring their own
 * props, so random chests and sarcophagi only go in the passages and plain rooms: a floor whose few open spots ran out
 * used to keep looking forever, and the game hung on the loading screen (a new Warrior's first Cathedral floor).
 */
TEST_F(Poe2CathedralObjectsTest, EveryCathedralFloorGetsItsObjects)
{
	Watchdog watchdog(std::chrono::seconds(20));
	const char *seedsEnv = std::getenv("POE2_OBJECT_SEEDS");
	// About one fresh Cathedral floor in fifty used to hang here (the first in this sweep is level 1, seed 17489744).
	const uint32_t seeds = seedsEnv != nullptr ? static_cast<uint32_t>(std::strtoul(seedsEnv, nullptr, 10)) : 400U;
	int floors = 0;
	int floorsWithChests = 0;
	leveltype = DTYPE_CATHEDRAL;
	// The walls' solidity: without it every tile is floor and no placement ever has to look hard.
	const std::expected<void, std::string> sol = LoadLevelSOLData();
	ASSERT_TRUE(sol.has_value()) << sol.error();
	pMegaTiles = LoadFileInMem<MegaTile>("levels\\l1data\\l1.til");
	ASSERT_NE(pMegaTiles, nullptr);
	for (int level = 1; level <= 4; level++) {
		for (uint32_t seed = 1; seed <= seeds; seed++) {
			const uint32_t levelSeed = (seed * 104729U) + static_cast<uint32_t>(level);
			LevelSeeds[level] = std::nullopt;
			currlevel = level;
			leveltype = DTYPE_CATHEDRAL;
			Poe2ApplyFreshFloor(level, false, levelSeed, 1);
			CreateDungeon(levelSeed, ENTRY_MAIN);
			ASSERT_TRUE(IsCathedralFunnelFloor());
			memset(dMonster, 0, sizeof(dMonster));
			memset(dPlayer, 0, sizeof(dPlayer));
			memset(dObject, 0, sizeof(dObject));
			InitLevelMonsters();
			SetRndSeed(levelSeed);
			ASSERT_TRUE(GetLevelMTypes().has_value());
			InitThemes();
			HoldThemeRooms();
			const std::string what = "InitObjects on level " + std::to_string(level) + ", seed " + std::to_string(levelSeed);
			watchdog.Guard(what, []() { InitObjects(); });
			EXPECT_GT(ActiveObjectCount, 0) << what;
			floors++;
			for (int i = 0; i < ActiveObjectCount; i++) {
				if (Objects[ActiveObjects[i]].IsChest()) {
					floorsWithChests++;
					break;
				}
			}
		}
	}
	// A crowded floor can go without a chest or two, but the floors still get theirs.
	EXPECT_GE(floorsWithChests * 10, floors * 9);
}

} // namespace
} // namespace devilution
