/**
 * @file reforged_clean_ui_test.cpp
 *
 * Sanctuary Reforged shows nothing of POE2 Mashup: not its mod in Settings > Mods, not its settings, not its keys, not the
 * admin tools (unless the game was started with --admin). POE2 Mashup keeps all of them, and a player's own key bindings
 * survive going between the modes.
 */
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "options.h"
#include "utils/paths.h"
#include "utils/str_cat.hpp"

namespace devilution {
namespace {

class ReforgedCleanUiTest : public ::testing::Test {
protected:
	static void SetUpTestSuite()
	{
		// The actions the modes share keys over, as the game adds them (diablo.cpp InitKeymapActions).
		KeymapperOptions &keymapper = GetOptions().Keymapper;
		keymapper.AddAction("QuestLog", "Quest log", "", 'Q', [] {});
		keymapper.LastAction().SetPoe2DefaultKey('L').RestoreDefaultOutsidePoe2();
		keymapper.AddAction("SortInv", "Sort Inventory", "", SDLK_UNKNOWN, [] {});
		keymapper.AddAction("PauseGame", "Pause Game", "", 'P', [] {});
		keymapper.LastAction().RestoreDefaultOutsidePoe2();
		keymapper.AddAction("ItemSpawner", "Item Spawner", "", SDLK_INSERT, [] {});
		keymapper.LastAction().SetModeFlags(OptionEntryFlags::AdminTool);
		keymapper.AddAction("PassiveTree", "Passive Tree", "", 'P', [] {});
		keymapper.LastAction().SetModeFlags(OptionEntryFlags::OnlyPoe2);
		keymapper.AddAction("TraySlot{}", "Skill tray key {}", "", 'Q', [] {}, nullptr, nullptr, 1);
		keymapper.LastAction().SetModeFlags(OptionEntryFlags::OnlyPoe2);
		keymapper.CommitActions();
	}

	void SetUp() override
	{
		// One folder per test: ctest runs them side by side.
		configDir_ = std::filesystem::temp_directory_path() / StrCat("reforged_clean_ui_test_", ::testing::UnitTest::GetInstance()->current_test_info()->name());
		std::filesystem::create_directories(configDir_);
		paths::SetConfigPath(configDir_.string() + "/");
		paths::SetPrefPath(configDir_.string() + "/");
		HasHellfireMpq = true;
		gbIsPoe2 = false;
		gbIsReforged = false;
		gbAdminSwitch = false;
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		gbIsReforged = false;
		gbAdminSwitch = false;
		HasHellfireMpq = false;
		std::filesystem::remove_all(configDir_);
	}

	void WriteIni(std::string_view text)
	{
		std::ofstream(configDir_ / "diablo.ini", std::ios::binary) << text;
	}

	std::string ReadIni()
	{
		std::ifstream in(configDir_ / "diablo.ini", std::ios::binary);
		std::stringstream text;
		text << in.rdbuf();
		return text.str();
	}

	/** @brief Loads the ini, then binds the keys for the given mode, as the game does once the mods are loaded. */
	void LoadFor(bool poe2)
	{
		LoadOptions();
		gbIsPoe2 = poe2;
		gbIsReforged = !poe2;
		GetOptions().Keymapper.BindForThisMode();
	}

	static std::string_view ActionOn(uint32_t key)
	{
		const KeymapperOptions::Action *action = GetOptions().Keymapper.findAction(key);
		return action != nullptr ? action->key : std::string_view {};
	}

	static KeymapperOptions::Action &Action(std::string_view key)
	{
		for (OptionEntryBase *entry : GetOptions().Keymapper.GetEntries()) {
			if (entry->key == key)
				return *static_cast<KeymapperOptions::Action *>(entry);
		}
		std::abort();
	}

	std::filesystem::path configDir_;
};

/** What an older build wrote for Sanctuary Reforged: POE2 Mashup's keys, with Pause Game left unbound by the Passive Tree. */
constexpr std::string_view OldReforgedIni = "[Mods]\nhf=1\npoe2=0\nreforged=1\n\n"
                                            "[Keymapping]\nQuestLog=L\nSortInv=J\nPauseGame=\nPassiveTree=P\nTraySlot1=Q\nItemSpawner=INSERT\n";

TEST_F(ReforgedCleanUiTest, AReforgedSetupDoesNotListTheOtherMode)
{
	WriteIni(OldReforgedIni);
	LoadOptions();
	const std::vector<std::string_view> mods = GetOptions().Mods.GetModList();
	EXPECT_NE(std::find(mods.begin(), mods.end(), "reforged"), mods.end());
	EXPECT_NE(std::find(mods.begin(), mods.end(), "hf"), mods.end());
	EXPECT_EQ(std::find(mods.begin(), mods.end(), "poe2"), mods.end());

	WriteIni("[Mods]\nhf=1\npoe2=1\nreforged=0\n");
	LoadOptions();
	const std::vector<std::string_view> poe2Mods = GetOptions().Mods.GetModList();
	EXPECT_NE(std::find(poe2Mods.begin(), poe2Mods.end(), "poe2"), poe2Mods.end()) << "POE2 Mashup's setup lists both";
	EXPECT_NE(std::find(poe2Mods.begin(), poe2Mods.end(), "reforged"), poe2Mods.end());
}

TEST_F(ReforgedCleanUiTest, ReforgedGetsDiablosKeysBackFromAnOlderIni)
{
	WriteIni(OldReforgedIni);
	LoadFor(/*poe2=*/false);
	EXPECT_EQ(ActionOn('Q'), "QuestLog");
	EXPECT_EQ(ActionOn('P'), "PauseGame");
	EXPECT_EQ(ActionOn('J'), "SortInv") << "The player's own binding stays";
	EXPECT_EQ(ActionOn('L'), "");
	EXPECT_EQ(ActionOn(SDLK_INSERT), "") << "No item spawner without --admin";
}

TEST_F(ReforgedCleanUiTest, Poe2KeepsItsKeys)
{
	WriteIni("[Mods]\nhf=1\npoe2=1\nreforged=0\n\n[Keymapping]\nQuestLog=L\nSortInv=J\nPauseGame=\nPassiveTree=P\nTraySlot1=Q\nItemSpawner=INSERT\n");
	LoadFor(/*poe2=*/true);
	EXPECT_EQ(ActionOn('Q'), "TraySlot1");
	EXPECT_EQ(ActionOn('L'), "QuestLog");
	EXPECT_EQ(ActionOn('P'), "PassiveTree");
	EXPECT_EQ(ActionOn('J'), "SortInv");
	EXPECT_EQ(ActionOn(SDLK_INSERT), "ItemSpawner");

	// A fresh POE2 Mashup ini has the same keys.
	WriteIni("[Mods]\nhf=1\npoe2=1\n");
	LoadFor(/*poe2=*/true);
	EXPECT_EQ(ActionOn('Q'), "TraySlot1");
	EXPECT_EQ(ActionOn('L'), "QuestLog");
	EXPECT_EQ(ActionOn('P'), "PassiveTree");
}

TEST_F(ReforgedCleanUiTest, SavingInReforgedKeepsTheOtherModesBindings)
{
	WriteIni(OldReforgedIni);
	LoadFor(/*poe2=*/false);
	SaveOptions();
	const std::string saved = ReadIni();
	EXPECT_NE(saved.find("TraySlot1=Q"), std::string::npos) << saved;
	EXPECT_NE(saved.find("PassiveTree=P"), std::string::npos) << saved;
	EXPECT_NE(saved.find("SortInv=J"), std::string::npos) << saved;
	EXPECT_EQ(saved.find("Loot Filter"), std::string::npos) << "No POE2 Mashup options written in Reforged: " << saved;

	// Going back to POE2 Mashup with the same ini finds its keys where they were.
	LoadFor(/*poe2=*/true);
	EXPECT_EQ(ActionOn('Q'), "TraySlot1");
	EXPECT_EQ(ActionOn('P'), "PassiveTree");
}

TEST_F(ReforgedCleanUiTest, AKeyChosenInReforgedIsKept)
{
	WriteIni(OldReforgedIni);
	LoadFor(/*poe2=*/false);
	ASSERT_TRUE(Action("QuestLog").SetValue('L'));
	ASSERT_TRUE(Action("PauseGame").SetValue(SDLK_UNKNOWN));
	SaveOptions();
	LoadFor(/*poe2=*/false);
	EXPECT_EQ(ActionOn('L'), "QuestLog");
	EXPECT_EQ(ActionOn('P'), "") << "Unbound on purpose";
}

TEST_F(ReforgedCleanUiTest, OptionsFollowTheMode)
{
	gbIsReforged = true;
	EXPECT_FALSE(IsOptionInThisMode(OptionEntryFlags::OnlyPoe2));
	EXPECT_FALSE(IsOptionInThisMode(OptionEntryFlags::AdminTool));
	EXPECT_TRUE(IsOptionInThisMode(OptionEntryFlags::OnlyEnhanced));
	EXPECT_FALSE(IsOptionInThisMode(GetOptions().Gameplay.lootFilter.GetFlags()));
	EXPECT_FALSE(IsOptionInThisMode(GetOptions().Gameplay.heroMiniBarEnergyShield.GetFlags()));
	EXPECT_TRUE(IsOptionInThisMode(GetOptions().Gameplay.showCornerMap.GetFlags()));
	gbAdminSwitch = true;
	EXPECT_TRUE(IsOptionInThisMode(OptionEntryFlags::AdminTool)) << "--admin";

	gbAdminSwitch = false;
	gbIsReforged = false;
	EXPECT_FALSE(IsOptionInThisMode(OptionEntryFlags::OnlyEnhanced)) << "Plain Diablo and Hellfire";

	gbIsPoe2 = true;
	EXPECT_TRUE(IsOptionInThisMode(GetOptions().Gameplay.lootFilter.GetFlags()));
	EXPECT_TRUE(IsOptionInThisMode(OptionEntryFlags::AdminTool));
	EXPECT_TRUE(IsOptionInThisMode(OptionEntryFlags::OnlyEnhanced));
}

TEST_F(ReforgedCleanUiTest, ReforgedOptionTextsNameNoOtherGame)
{
	for (OptionCategoryBase *category : GetOptions().GetCategories()) {
		for (OptionEntryBase *entry : category->GetEntries()) {
			// The other mode's own options, and its mod (which a Reforged setup does not list).
			if (HasAnyOf(entry->GetFlags(), OptionEntryFlags::OnlyPoe2 | OptionEntryFlags::AdminTool) || entry->key == "poe2")
				continue;
			const std::string text = StrCat(entry->GetName(), " ", entry->GetDescription());
			for (std::string_view word : { "POE2", "Path of Exile", "Mashup" })
				EXPECT_EQ(text.find(word), std::string::npos) << category->GetKey() << "." << entry->key << ": " << text;
		}
	}
}

} // namespace
} // namespace devilution
