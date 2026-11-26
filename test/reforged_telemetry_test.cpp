/**
 * @file reforged_telemetry_test.cpp
 *
 * Sanctuary Reforged's play log: its own folder (logs) and name, Diablo's own stats only, no gem log, its own crash
 * reports, and none of POE2 Mashup's Skill Lab looks. The log opens before the mods load, so these tests start it in
 * POE2 Mashup's folder and switch the mode after, as the game does.
 */
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/skill_looks.hpp"
#include "poe2/telemetry.hpp"
#include "tables/misdat.h"
#include "tables/playerdat.hpp"
#include "utils/file_util.h"
#include "utils/paths.h"

namespace devilution {
namespace {

std::string Folder(bool reforged)
{
	return paths::PrefPath() + std::string(TelemetryFolderName(reforged)) + DIRECTORY_SEPARATOR_STR;
}

std::string ReadFile(const std::string &path)
{
	std::ifstream stream(path);
	std::stringstream contents;
	contents << stream.rdbuf();
	return contents.str();
}

/** @brief The newest file in a folder whose name starts and ends so; empty when there's none. */
std::string Latest(const std::string &folder, std::string_view prefix, std::string_view extension)
{
	std::string latest;
	for (const std::string &file : ListFiles(folder.c_str())) {
		if (file.starts_with(prefix) && file.ends_with(extension) && file > latest)
			latest = file;
	}
	return latest;
}

std::string ReadLatest(bool reforged, std::string_view extension)
{
	TelemetryFlush();
	const std::string file = Latest(Folder(reforged), "session_", extension);
	return file.empty() ? std::string() : ReadFile(Folder(reforged) + file);
}

/** @brief Words of POE2 Mashup's that must not show in Sanctuary Reforged's lines. */
void ExpectNoPoe2Words(std::string_view text)
{
	for (const std::string_view word : { "POE2", "poe2", "energy shield", "spirit", "Spirit", "passives", " gem ", "evasion", "chaos", "cold", "flasks", "charms", "crafts", "rare", "default attack", "light stuns" })
		EXPECT_EQ(text.find(word), std::string_view::npos) << "\"" << word << "\" in: " << text;
}

class ReforgedTelemetryTest : public ::testing::Test {
public:
	static void SetUpTestSuite()
	{
		paths::SetPrefPath(::testing::TempDir() + "reforged_telemetry_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + DIRECTORY_SEPARATOR_STR);
		// The game opens the log before the mods have loaded: no mod is on yet, so it starts as POE2 Mashup's.
		gbIsPoe2 = false;
		gbIsReforged = false;
		TelemetryInit();
		TelemetryLog("load", "a step before the mods loaded");
		// Then Sanctuary Reforged's mod loads.
		gbIsHellfire = true;
		gbIsReforged = true;
		TelemetryModeChanged();
		UnloadModArchives();
		LoadModArchives({ { "hf", "reforged" } });
		LoadItemData();
		LoadPlayerDataFiles();
	}

	static void TearDownTestSuite()
	{
		gbIsReforged = false;
		gbIsPoe2 = false;
		UnloadModArchives();
	}

	void SetUp() override
	{
		gbIsPoe2 = false;
		gbIsReforged = true;
		Players.resize(1);
		MyPlayer = &Players[0];
		*MyPlayer = {};
		Player &player = *MyPlayer;
		player._pName[0] = 'R';
		player._pName[1] = '\0';
		player._pMaxHP = player._pMaxHPBase = Fixed26_6::fromInt(100);
		player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(100);
		player._pMaxMana = player._pMaxManaBase = Fixed26_6::fromInt(50);
		player._pMana = player._pManaBase = Fixed26_6::fromInt(50);
		player._pStrength = 30;
		player._pMagResist = 10;
		player._pFireResist = 20;
		player._pLghtResist = 30;
		player._pRSpell = SpellID::Invalid;
	}
};

TEST_F(ReforgedTelemetryTest, LogMovesToItsOwnFolderUnderItsOwnName)
{
	ASSERT_TRUE(TelemetryEnabled());
	EXPECT_TRUE(TelemetryIsReforged());
	EXPECT_EQ(TelemetryFolder(), Folder(true));
	EXPECT_EQ(TelemetryFolderName(true), "logs");
	const std::string log = ReadLatest(true, ".log");
	EXPECT_EQ(log.rfind("Sanctuary Reforged play log, ", 0), 0U) << log;
	EXPECT_NE(log.find("[load] a step before the mods loaded"), std::string::npos) << "the early lines move with the log: " << log;
	const std::string events = ReadLatest(true, ".events");
	EXPECT_NE(events.find("\"game\":\"Sanctuary Reforged\""), std::string::npos) << events;
	EXPECT_NE(events.find("a step before the mods loaded"), std::string::npos) << events;
	ExpectNoPoe2Words(log);
	ExpectNoPoe2Words(events);
	EXPECT_TRUE(Latest(Folder(false), "session_", ".log").empty()) << "nothing is left in poe2_logs";
}

TEST_F(ReforgedTelemetryTest, LinesCarryDiablosOwnStatsOnly)
{
	Player &player = *MyPlayer;
	TelemetryGameStarted(player, false);
	TelemetryMark("checking the frame rate");
	TelemetryNoteAttacker("Skeleton");
	TelemetryPlayerDamaged(player, DamageType::Cold, Fixed26_6::fromInt(15));
	TelemetryMeleeSwing(player, SwingOutcome::Evaded);
	TelemetryLog("spirit", "a POE2 Mashup system wrote this");
	TelemetryLevelUp(player);

	const std::string build = TelemetryBuildText(player);
	EXPECT_NE(build.find("| defences: armor class "), std::string::npos) << build;
	EXPECT_NE(build.find("resist magic 10% fire 20% lightning 30%"), std::string::npos) << build;
	ExpectNoPoe2Words(build);
	const std::string stats = TelemetryStatsText();
	EXPECT_NE(stats.find("melee swings 1 (1 missed, 0 blocked)"), std::string::npos) << stats;
	EXPECT_NE(stats.find("fps "), std::string::npos) << stats;
	ExpectNoPoe2Words(stats);

	const std::string log = ReadLatest(true, ".log");
	EXPECT_NE(log.find("[game] new game R: level "), std::string::npos) << log;
	EXPECT_NE(log.find("mode Sanctuary Reforged"), std::string::npos) << log;
	EXPECT_NE(log.find("[mark] checking the frame rate"), std::string::npos) << log;
	EXPECT_NE(log.find("[hit] took 15 magic damage from Skeleton, life 100/100"), std::string::npos) << log;
	EXPECT_NE(log.find("[levelup] character level"), std::string::npos) << log;
	ExpectNoPoe2Words(log);
	ExpectNoPoe2Words(ReadLatest(true, ".events"));
	EXPECT_TRUE(FileExists(Folder(true) + "playtest_notes.txt"));

	TelemetryGameEnded();
	const std::string history = ReadFile(Folder(true) + "history.tsv");
	EXPECT_NE(history.find("drops normal/magic/unique"), std::string::npos) << history;
	ExpectNoPoe2Words(history);
	EXPECT_FALSE(FileExists(Folder(true) + "gem_levels.log")) << "Sanctuary Reforged keeps no gem log";
}

TEST_F(ReforgedTelemetryTest, CrashReportsGoToItsOwnFolder)
{
	MyPlayer = nullptr;
	TelemetryFatal("a test crash");
	const std::string crashes = Folder(true) + "crashes" + DIRECTORY_SEPARATOR_STR;
	const std::string report = Latest(crashes, "crash_", ".txt");
	ASSERT_FALSE(report.empty());
	const std::string text = ReadFile(crashes + report);
	EXPECT_EQ(text.rfind("Sanctuary Reforged crash report, ", 0), 0U) << text;
	EXPECT_NE(text.find("What happened: a test crash"), std::string::npos) << text;
	ExpectNoPoe2Words(text);
	EXPECT_FALSE(DirectoryExists(Folder(false) + "crashes"));
}

TEST_F(ReforgedTelemetryTest, SkillLabEditsAreNotRead)
{
	// POE2 Mashup's Skill Lab edits, in its folder and (were it read from there) in Sanctuary Reforged's.
	for (const bool reforged : { false, true }) {
		RecursivelyCreateDir(Folder(reforged).c_str());
		std::ofstream(Folder(reforged) + "skill_looks_mine.tsv") << "key\tname\tsize\n"
		                                                         << "lab test\tLab test\t3\n";
	}
	LoadSkillLooks();
	EXPECT_FALSE(HasSkillLookEdit("lab test"));
	EXPECT_EQ(GetSkillLook("lab test").size, 1.F);

	// POE2 Mashup reads them, when the mode changes back.
	gbIsReforged = false;
	gbIsPoe2 = true;
	TelemetryModeChanged();
	EXPECT_TRUE(HasSkillLookEdit("lab test"));
	EXPECT_EQ(GetSkillLook("lab test").size, 3.F);
}

// Last: it changes the mode for good.
TEST_F(ReforgedTelemetryTest, AfterAGameTheOtherModeStartsItsOwnLog)
{
	// Run on its own, it plays the Sanctuary Reforged game first; after the tests above, POE2 Mashup's log is open already.
	if (TelemetryIsReforged()) {
		TelemetryGameStarted(*MyPlayer, false);
		TelemetryMark("checking the frame rate");
		TelemetryGameEnded();
		gbIsReforged = false;
		gbIsPoe2 = true;
		TelemetryModeChanged();
	}
	ASSERT_FALSE(TelemetryIsReforged());
	EXPECT_EQ(TelemetryFolder(), Folder(false));
	const std::string poe2 = ReadLatest(false, ".log");
	EXPECT_EQ(poe2.rfind("POE2 Mashup play log, ", 0), 0U) << poe2;
	EXPECT_EQ(poe2.find("[mark] checking the frame rate"), std::string::npos) << "the Sanctuary Reforged game stays in its own log";
	EXPECT_NE(ReadLatest(false, ".events").find("\"game\":\"POE2 Mashup\""), std::string::npos) << "POE2 Mashup's header";
	const std::string reforged = ReadLatest(true, ".log");
	EXPECT_EQ(reforged.rfind("Sanctuary Reforged play log, ", 0), 0U);
	EXPECT_NE(reforged.find("[mark] checking the frame rate"), std::string::npos) << reforged;
	ExpectNoPoe2Words(reforged);
}

} // namespace
} // namespace devilution
