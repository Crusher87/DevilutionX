#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <sstream>
#include <string>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/defenses.hpp"
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

std::string ReadLatest(std::string_view extension)
{
	TelemetryFlush();
	std::string latest;
	for (const std::string &file : ListFiles(LogFolder().c_str())) {
		if (file.ends_with(extension) && file > latest)
			latest = file;
	}
	std::ifstream stream(LogFolder() + latest);
	std::stringstream contents;
	contents << stream.rdbuf();
	return contents.str();
}

std::string ReadLatestLog()
{
	return ReadLatest(".log");
}

std::string ReadLatestEvents()
{
	return ReadLatest(".events");
}

class Poe2TelemetryTest : public ::testing::Test {
public:
	static void SetUpTestSuite()
	{
		// A folder of its own, as tests run side by side and a session file is named by the second it starts.
		paths::SetPrefPath(::testing::TempDir() + "poe2_telemetry_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + DIRECTORY_SEPARATOR_STR);
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

TEST_F(Poe2TelemetryTest, EffectsAreLoggedWithoutFlooding)
{
	for (int i = 0; i < 10; i++)
		TelemetryEffect("visual", "BloodHit", 20 + i, 30);
	const std::string log = ReadLatestLog();
	EXPECT_NE(log.find("[fx] visual BloodHit at 20,30, hero at"), std::string::npos) << log;
	EXPECT_NE(log.find("[fx] visual BloodHit at 22,30"), std::string::npos) << log;
	EXPECT_EQ(log.find("[fx] visual BloodHit at 23,30"), std::string::npos) << "Only a few lines a second for each kind";
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

TEST_F(Poe2TelemetryTest, HitsOnEnergyShieldCountAsHitsTaken)
{
	Player &player = *MyPlayer;
	SetMaxEnergyShield(player, 50);
	TelemetryNoteAttacker("Fallen One");
	ApplyPlrDamage(DamageType::Physical, player, AbsorbWithEnergyShield(player, Fixed26_6::fromInt(20)));
	EXPECT_EQ(player._pHitPoints, Fixed26_6::fromInt(100)) << "the shield took it all";
	EXPECT_NE(ReadLatestLog().find("[hit] took 20 physical damage from Fallen One (20 to energy shield), life 100/100, energy shield 30/50"), std::string::npos) << ReadLatestLog();
	const std::string stats = TelemetryStatsText();
	EXPECT_NE(stats.find("hits taken 1 (1 big, 0 close calls, 0 damage, 20 to energy shield, lowest life 100%)"), std::string::npos) << stats;
}

TEST_F(Poe2TelemetryTest, CountsMeleeSwingsByOutcome)
{
	TelemetryMeleeSwing(*MyPlayer, SwingOutcome::Landed);
	TelemetryMeleeSwing(*MyPlayer, SwingOutcome::Evaded);
	TelemetryMeleeSwing(*MyPlayer, SwingOutcome::Evaded);
	TelemetryMeleeSwing(*MyPlayer, SwingOutcome::Blocked);
	TelemetryMeleeSwing(*MyPlayer, SwingOutcome::Dodged);
	const std::string stats = TelemetryStatsText();
	EXPECT_NE(stats.find("melee swings 5 (2 evaded, 1 blocked, 1 dodged)"), std::string::npos) << stats;
}

TEST_F(Poe2TelemetryTest, GemLevelsGoToTheGemLog)
{
	TelemetryGemLevelChanged(*MyPlayer, SkillSlot::ForAttack(WeaponSkill::Cleave), 8, 9);
	std::ifstream stream(LogFolder() + "gem_levels.log");
	std::stringstream contents;
	contents << stream.rdbuf();
	const std::string log = contents.str();
	EXPECT_NE(log.find("Cleave raised from gem level 8 to 9"), std::string::npos) << log;
	EXPECT_NE(log.find("level 9: 182% weapon damage"), std::string::npos) << log;
}

TEST_F(Poe2TelemetryTest, EventsGoToTheEventFileAsData)
{
	TelemetryMark("events check");
	TelemetrySkillUsed(*MyPlayer, "Spark", 7, 3);
	TelemetryNoteAttacker("Hidden");
	TelemetryPlayerDamaged(*MyPlayer, DamageType::Lightning, Fixed26_6::fromInt(5));
	const std::string events = ReadLatestEvents();
	EXPECT_EQ(events.rfind("{\"k\":\"header\",\"version\":1,", 0), 0U) << events;
	EXPECT_NE(events.find("\"k\":\"mark\",\"area\":\"town\",\"text\":\"events check\""), std::string::npos) << events;
	EXPECT_NE(events.find("\"k\":\"skill\",\"area\":\"town\",\"p\":0,\"skill\":\"Spark\",\"gem\":3,\"mana\":7"), std::string::npos) << events;
	EXPECT_NE(events.find("\"attacker\":\"Hidden\",\"type\":\"lightning\",\"damage\":5.00"), std::string::npos) << events;
}

TEST_F(Poe2TelemetryTest, ChannelsSwitchFromTheConsole)
{
	EXPECT_NE(TelemetryLogLevelsText("", "").find("skills: normal"), std::string::npos);
	EXPECT_NE(TelemetryLogLevelsText("skills", "off").find("skills: off"), std::string::npos);
	TelemetrySkillUsed(*MyPlayer, "Quiet Spell", 1);
	EXPECT_EQ(ReadLatestEvents().find("Quiet Spell"), std::string::npos) << "the skills channel is off";
	EXPECT_NE(ReadLatestLog().find("[skill] Quiet Spell"), std::string::npos) << "the readable log keeps every line";
	TelemetryLogLevelsText("skills", "normal");
	EXPECT_NE(TelemetryLogLevelsText("session", "off").find("session: always"), std::string::npos) << "session events can't be switched off";
	EXPECT_NE(TelemetryLogLevelsText("sound", "deep").find("No channel"), std::string::npos);
}

TEST_F(Poe2TelemetryTest, DeepDamageBreaksDownHitsTaken)
{
	Player &player = *MyPlayer;
	SetMaxEnergyShield(player, 0);
	TelemetryHitStart(true, "Not on yet");
	EXPECT_FALSE(TelemetryDeepDamage());
	TelemetryLogLevelsText("damage", "deep");
	ASSERT_TRUE(TelemetryDeepDamage());
	TelemetryHitStart(true, "Overlord");
	TelemetryHitStep(true, "rolled", 40 * 64);
	TelemetryHitStep(true, "resistance", 25, true);
	TelemetryHitStep(true, "after resistance", 30 * 64);
	TelemetryNoteAttacker("Overlord");
	ApplyPlrDamage(DamageType::Fire, player, Fixed26_6::fromInt(30));
	TelemetryLogLevelsText("damage", "normal");
	const std::string events = ReadLatestEvents();
	EXPECT_NE(events.find("\"hit\":\"Overlord\",\"steps\":[{\"step\":\"rolled\",\"damage\":40.00},{\"step\":\"resistance\",\"percent\":25},{\"step\":\"after resistance\",\"damage\":30.00}]"), std::string::npos) << events;
}

TEST_F(Poe2TelemetryTest, GameEndWritesSummaryAndHistory)
{
	TelemetryGameEnded();
	const std::string events = ReadLatestEvents();
	EXPECT_NE(events.find("\"k\":\"summary\""), std::string::npos) << events;
	std::ifstream stream(LogFolder() + "history.tsv");
	std::stringstream contents;
	contents << stream.rdbuf();
	EXPECT_EQ(contents.str().rfind("date\tsession\thero", 0), 0U) << contents.str();
}

TEST_F(Poe2TelemetryTest, SkillDamageStartsEmpty)
{
	EXPECT_EQ(TelemetrySkillDamageText(), "damage by skill: none yet");
}

} // namespace
} // namespace devilution
