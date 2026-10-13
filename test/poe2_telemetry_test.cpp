#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/telemetry.hpp"
#include "tables/playerdat.hpp"
#include "utils/file_util.h"
#include "utils/paths.h"

namespace devilution {
namespace {

std::string LogFolder()
{
	return paths::PrefPath() + "poe2_logs" + DIRECTORY_SEPARATOR_STR;
}

std::string ReadLatestLog()
{
	std::string latest;
	for (const std::string &file : ListFiles(LogFolder().c_str())) {
		if (file > latest)
			latest = file;
	}
	std::ifstream stream(LogFolder() + latest);
	std::stringstream contents;
	contents << stream.rdbuf();
	return contents.str();
}

class Poe2TelemetryTest : public ::testing::Test {
public:
	static void SetUpTestSuite()
	{
		paths::SetPrefPath(::testing::TempDir() + "poe2_telemetry" + DIRECTORY_SEPARATOR_STR);
		TelemetryInit();
	}

	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		*MyPlayer = {};
		MyPlayer->poe2.clear();
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		gbIsPoe2 = true;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadPlayerDataFiles();

		Player &player = *MyPlayer;
		player._pMaxHP = player._pMaxHPBase = Fixed26_6::fromInt(100);
		player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(100);
		player._pMaxMana = player._pMaxManaBase = Fixed26_6::fromInt(100);
		player._pMana = player._pManaBase = Fixed26_6::fromInt(100);
		TelemetryGameStarted(player, false);
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		UnloadModArchives();
	}
};

TEST_F(Poe2TelemetryTest, WritesEventsToTheSessionLog)
{
	ASSERT_TRUE(TelemetryEnabled());
	TelemetryMark("checking the log");
	TelemetrySkillUsed(*MyPlayer, "Fireball", 12);
	const std::string log = ReadLatestLog();
	EXPECT_NE(log.find("POE2 Mashup play log"), std::string::npos);
	EXPECT_NE(log.find("[mark] checking the log"), std::string::npos);
	EXPECT_NE(log.find("[skill] Fireball, 12 mana"), std::string::npos);
}

TEST_F(Poe2TelemetryTest, CountsWastedFlasksAndFailedSkills)
{
	Item flask {};
	TelemetryFlaskUsed(*MyPlayer, flask, 10, true, false, 95, 40);
	TelemetryFlaskUsed(*MyPlayer, flask, 0, true, false, 30, 40);
	TelemetrySkillFailed(*MyPlayer, "Leap Slam", "not enough mana");
	const std::string stats = TelemetryStatsText();
	EXPECT_NE(stats.find("flasks 2 (1 wasted)"), std::string::npos) << stats;
	EXPECT_NE(stats.find("skills 0 (1 failed"), std::string::npos) << stats;
}

TEST_F(Poe2TelemetryTest, CloseCallNamesTheAttacker)
{
	Player &player = *MyPlayer;
	player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(15);
	TelemetryNoteAttacker("Goatman");
	TelemetryPlayerDamaged(player, DamageType::Fire, Fixed26_6::fromInt(10));
	EXPECT_NE(ReadLatestLog().find("[close call] took 10 fire damage from Goatman, life 15/100"), std::string::npos);
	EXPECT_NE(TelemetryStatsText().find("1 close calls"), std::string::npos);
}

TEST_F(Poe2TelemetryTest, SkillDamageStartsEmpty)
{
	EXPECT_EQ(TelemetrySkillDamageText(), "damage by skill: none yet");
}

} // namespace
} // namespace devilution
