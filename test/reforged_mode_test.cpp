/**
 * @file reforged_mode_test.cpp
 *
 * Sanctuary Reforged: the HD look and the comfort features on plain Diablo and Hellfire rules.
 *
 * These tests hold the two promises the mod makes: its switch (gbIsReforged, HasEnhancements) follows the mods, and
 * with it on the game's rules are Hellfire's own: items, levels, monsters and experience come out the same as without it.
 */
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <sol/sol.hpp>

#include "drlg_test.hpp"
#include "engine/assets.hpp"
#include "engine/load_file.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "lua/modules/poe2.hpp"
#include "lua/modules/reforged.hpp"
#include "mods/mod_identity.h"
#include "automap.h"
#include "monster.h"
#include "options.h"
#include "player.h"
#include "poe2/autosave.hpp"
#include "poe2/corner_map.hpp"
#include "poe2/experience.hpp"
#include "poe2/floors.hpp"
#include "poe2/monster_mods.hpp"
#include "qol/visual_store.h"
#include "spells.h"
#include "tables/itemdat.h"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"
#include "utils/paths.h"
#include "utils/str_cat.hpp"

namespace devilution {
namespace {

/** @brief Turns an area's New ... Floors setting off for one test, and on again (its default) after. */
class ScopedFloorsSetting {
public:
	explicit ScopedFloorsSetting(OptionEntryBoolean &setting)
	    : setting_(setting)
	{
		Poe2FloorsReset();
		setting_.SetValue(false);
	}
	~ScopedFloorsSetting()
	{
		setting_.SetValue(true);
		Poe2FloorsReset();
	}
	ScopedFloorsSetting(const ScopedFloorsSetting &) = delete;
	ScopedFloorsSetting &operator=(const ScopedFloorsSetting &) = delete;

private:
	OptionEntryBoolean &setting_;
};

/** @brief Puts the mode switches back however a test ends. */
class ReforgedModeTest : public ::testing::Test {
public:
	void SetUp() override
	{
		gbIsPoe2 = false;
		gbIsReforged = false;
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		gbIsReforged = false;
		gbIsHellfire = false;
		gbIsMultiplayer = false;
		UnloadModArchives();
	}
};

std::string ReadWholeFile(const std::string &path)
{
	std::string contents;
	FILE *file = std::fopen(path.c_str(), "rb");
	if (file == nullptr)
		return contents;
	char buffer[4096];
	size_t read;
	while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
		contents.append(buffer, read);
	std::fclose(file);
	return contents;
}

/** @brief Every number and name item generation fills in, so two items can be told apart by anything it rolled. */
std::string Describe(const Item &item)
{
	std::string text = StrCat(item._iSeed, ",", item._iCreateInfo, ",", static_cast<int>(item._itype), ",", static_cast<int>(item._iMagical), ",", item._iIdentified ? 1 : 0, ",",
	    item._iName, ",", item._iIName, ",", static_cast<int>(item._iLoc), ",", static_cast<int>(item._iClass), ",", static_cast<int>(item._iCurs), ",");
	text += StrCat(item._ivalue, ",", item._iIvalue, ",", item._iMinDam, ",", item._iMaxDam, ",", item._iAC, ",", static_cast<uint32_t>(item._iFlags), ",",
	    static_cast<int>(item._iMiscId), ",", static_cast<int>(item._iSpell), ",", static_cast<int>(item.IDidx), ",");
	text += StrCat(item._iCharges, ",", item._iMaxCharges, ",", item._iDurability, ",", item._iMaxDur, ",", item._iPLDam, ",", item._iPLToHit, ",", item._iPLAC, ",",
	    item._iPLStr, ",", item._iPLMag, ",", item._iPLDex, ",", item._iPLVit, ",", item._iPLFR, ",", item._iPLLR, ",", item._iPLMR, ",");
	text += StrCat(item._iPLDamMod, ",", item._iPLGetHit, ",", item._iPLLight, ",", static_cast<int>(item._iSplLvlAdd), ",", item._iUid, ",", item._iFMinDam, ",",
	    item._iFMaxDam, ",", item._iLMinDam, ",", item._iLMaxDam, ",", item._iPLEnAc, ",", item._iPLLifeRegen, ",", item._iPLManaRegen, ",");
	text += StrCat(static_cast<int>(item._iPrePower), ",", static_cast<int>(item._iSufPower), ",", static_cast<int>(item._iPreTier), ",", static_cast<int>(item._iSufTier), ",",
	    item._iVAdd1, ",", item._iVMult1, ",", item._iVAdd2, ",", item._iVMult2, ",", static_cast<int>(item._iMinStr), ",", static_cast<int>(item._iMinMag), ",",
	    static_cast<int>(item._iMinDex), ",", static_cast<uint32_t>(item._iDamAcFlags), ",", item.dwBuff, ",");
	for (size_t i = 0; i < item._iRarePowers.size(); i++)
		text += StrCat(static_cast<int>(item._iRarePowers[i]), "/", static_cast<int>(item._iRareTiers[i]), "/", static_cast<int>(item._iPoe2Mods[i].family), ",");
	text += item._iPoe2Craft.isEmpty() ? "plain" : "crafted";
	return text;
}

/**
 * @brief Rolls items the way a monster's drop does, from fixed seeds: every base that can drop, at three levels, as a
 * normal drop and as a "good" one, and each rebuilt from its seed as a save or a net message would.
 */
std::vector<std::string> RollItems()
{
	std::vector<std::string> rolled;
	Players.resize(1);
	MyPlayer = &Players[0];
	for (bool &flag : UniqueItemFlags)
		flag = false;
	for (size_t idx = 0; idx < AllItemsList.size(); idx++) {
		if (AllItemsList[idx].dropRate <= 0)
			continue;
		for (const int level : { 2, 13, 30 }) {
			for (const bool onlyGood : { false, true }) {
				const uint32_t seed = 0x5EED0000U + static_cast<uint32_t>(idx * 64) + static_cast<uint32_t>(level) + (onlyGood ? 1U << 20 : 0U);
				Item item = {};
				item._iMiscId = AllItemsList[idx].iMiscId;
				SetRndSeed(seed);
				SetupAllItems(*MyPlayer, item, static_cast<_item_indexes>(idx), AdvanceRndSeed(), level, 15, onlyGood, false);
				rolled.push_back(Describe(item));
				Item again = {};
				RecreateItem(*MyPlayer, again, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff);
				rolled.push_back(Describe(again));
				rolled.push_back(StrCat("rng ", GetLCGEngineState()));
			}
		}
	}
	return rolled;
}

void LoadRules(std::initializer_list<std::string_view> mods)
{
	UnloadModArchives();
	LoadModArchives(mods);
	LoadItemData();
	LoadSpellData();
	LoadMonsterData();
	LoadPlayerDataFiles();
}

/** @brief The monsters' numbers a fight is made of, for every kind of monster. */
std::vector<std::string> DescribeMonsters()
{
	std::vector<std::string> lines;
	for (const MonsterData &data : MonstersData) {
		lines.push_back(StrCat(data.name, ",", static_cast<int>(data.level), ",", data.hitPointsMinimum, ",", data.hitPointsMaximum, ",", static_cast<int>(data.toHit), ",",
		    static_cast<int>(data.minDamage), ",", static_cast<int>(data.maxDamage), ",", static_cast<int>(data.minDunLvl), ",", static_cast<int>(data.maxDunLvl), ",",
		    static_cast<int>(data.armorClass), ",", data.resistance, ",", data.resistanceHell, ",", data.exp));
	}
	return lines;
}

std::vector<uint32_t> ExperienceTable()
{
	std::vector<uint32_t> table;
	for (unsigned level = 1; level <= 50; level++)
		table.push_back(GetNextExperienceThresholdForLevel(level));
	return table;
}

TEST_F(ReforgedModeTest, EnhancementsAreOnWithEitherMod)
{
	EXPECT_FALSE(HasEnhancements()) << "plain Diablo and Hellfire have none";
	gbIsReforged = true;
	EXPECT_TRUE(HasEnhancements());
	gbIsReforged = false;
	gbIsPoe2 = true;
	EXPECT_TRUE(HasEnhancements()) << "POE2 Mashup has always had them";
}

TEST_F(ReforgedModeTest, TheModSwitchesItselfOnThroughLua)
{
	sol::state lua;
	sol::state_view view(lua);
	sol::table reforged = LuaReforgedModule(view);
	EXPECT_FALSE(reforged["is_enabled"]().get<bool>());
	reforged["enable"]();
	EXPECT_TRUE(gbIsReforged);
	EXPECT_FALSE(gbIsPoe2) << "it never switches POE2's rules on";
	EXPECT_TRUE(reforged["is_enabled"]().get<bool>());
	EXPECT_TRUE(reforged["has_enhancements"]().get<bool>());
}

TEST_F(ReforgedModeTest, BesidePoe2TheModDoesNothing)
{
	sol::state lua;
	sol::state_view view(lua);
	sol::table poe2 = LuaPoe2Module(view);
	sol::table reforged = LuaReforgedModule(view);
	poe2["enable"]();
	reforged["enable"]();
	EXPECT_TRUE(gbIsPoe2);
	EXPECT_FALSE(gbIsReforged);
	EXPECT_TRUE(reforged["has_enhancements"]().get<bool>());
}

TEST_F(ReforgedModeTest, Poe2NeverLoadsBesideReforgedWhicheverComesFirst)
{
	std::vector<std::string_view> both = { "clock", "reforged", "hf", "poe2" };
	DropReforgedBesidePoe2(both);
	EXPECT_EQ(both, (std::vector<std::string_view> { "clock", "hf", "poe2" }));

	std::vector<std::string_view> other = { "poe2", "hf", "reforged" };
	DropReforgedBesidePoe2(other);
	EXPECT_EQ(other, (std::vector<std::string_view> { "poe2", "hf" }));

	std::vector<std::string_view> alone = { "hf", "reforged" };
	DropReforgedBesidePoe2(alone);
	EXPECT_EQ(alone, (std::vector<std::string_view> { "hf", "reforged" }));
}

TEST_F(ReforgedModeTest, TheManifestKeepsItsSavesToItself)
{
	const std::string reforgedText = ReadWholeFile(paths::BasePath() + "mods/reforged/manifest.ini");
	const std::string poe2Text = ReadWholeFile(paths::BasePath() + "mods/poe2/manifest.ini");
	const std::string hellfireText = ReadWholeFile(paths::BasePath() + "mods/hf/manifest.ini");
	ASSERT_FALSE(reforgedText.empty()) << "mods/reforged/manifest.ini isn't beside the tests";
	ASSERT_FALSE(poe2Text.empty());
	ASSERT_FALSE(hellfireText.empty());
	const ModManifest reforged = ParseModManifest(reforgedText);
	const ModManifest poe2 = ParseModManifest(poe2Text);
	const ModManifest hellfire = ParseModManifest(hellfireText);
	EXPECT_EQ(reforged.name, "Sanctuary Reforged");
	EXPECT_EQ(reforged.saveExtension, "srv");
	EXPECT_NE(reforged.saveExtension, poe2.saveExtension);
	EXPECT_NE(reforged.saveExtension, hellfire.saveExtension);
	EXPECT_NE(reforged.saveExtension, "sv") << "plain Diablo's";
	EXPECT_EQ(reforged.programId.size(), 4U);
	EXPECT_NE(reforged.programId, poe2.programId);
	EXPECT_NE(reforged.programId, hellfire.programId);
	EXPECT_EQ(reforged.requiredMods, (std::vector<std::string> { "hf" }));
}

TEST_F(ReforgedModeTest, TheModsTablesAreTheSameAsPoe2s)
{
	// The look and comfort tables, and the designed rooms the floor layouts are built from, are copies: a change to one
	// mod's is a change to the other's.
	for (const std::string_view file : { "atmosphere.tsv", "map_icons.tsv", "quest_tracker.tsv", "skill_looks.tsv", "cathedral_rooms.tsv",
	         "catacombs_rooms.tsv", "caves_rooms.tsv", "hell_rooms.tsv", "hive_rooms.tsv", "crypt_rooms.tsv" }) {
		const std::string reforged = ReadWholeFile(StrCat(paths::BasePath(), "mods/reforged/txtdata/poe2/", file));
		const std::string poe2 = ReadWholeFile(StrCat(paths::BasePath(), "mods/poe2/txtdata/poe2/", file));
		ASSERT_FALSE(reforged.empty()) << file;
		EXPECT_EQ(reforged, poe2) << file;
	}
}

TEST_F(ReforgedModeTest, ComfortFeaturesFollowTheSwitchAndPoe2OnlyOnesDoNot)
{
	GetOptions().Gameplay.showCornerMap.SetValue(true);
	AutomapActive = false;
	EXPECT_FALSE(CornerMapShown());
	EXPECT_FALSE(AutosaveEnabled());
	gbIsReforged = true;
	EXPECT_TRUE(CornerMapShown());
	EXPECT_TRUE(AutosaveEnabled());
	EXPECT_TRUE(UseVisualStoreUi());
	// POE2's own rules stay off.
	Monster monster {};
	monster.rndItemSeed = 12345;
	EXPECT_EQ(GetMonsterAffixes(monster).rarity, MonsterRarity::Normal);
	gbIsMultiplayer = true;
	EXPECT_FALSE(AutosaveEnabled()) << "single player only, as in POE2 Mashup";
}

TEST_F(ReforgedModeTest, ItemsRollTheSameAsHellfire)
{
	gbIsSpawn = false;
	gbIsMultiplayer = false;
	gbIsHellfire = true;
	LoadRules({ "hf" });
	const std::vector<std::string> hellfire = RollItems();
	ASSERT_GT(hellfire.size(), 500U);

	// The switch alone.
	gbIsReforged = true;
	EXPECT_EQ(RollItems(), hellfire);

	// And with the mod's own files loaded over Hellfire's.
	LoadRules({ "hf", "reforged" });
	EXPECT_EQ(RollItems(), hellfire);

	// POE2 Mashup's rolls are its own: the comparison can tell rules apart.
	gbIsReforged = false;
	gbIsPoe2 = true;
	LoadRules({ "hf", "poe2" });
	EXPECT_NE(RollItems(), hellfire);
}

TEST_F(ReforgedModeTest, ItemsRollTheSameAsDiablo)
{
	gbIsSpawn = false;
	gbIsMultiplayer = false;
	gbIsHellfire = false;
	LoadRules({});
	const std::vector<std::string> diablo = RollItems();
	gbIsReforged = true;
	EXPECT_EQ(RollItems(), diablo);
}

TEST_F(ReforgedModeTest, MonstersAndExperienceAreHellfires)
{
	gbIsHellfire = true;
	LoadRules({ "hf" });
	const std::vector<std::string> monsters = DescribeMonsters();
	const std::vector<uint32_t> experience = ExperienceTable();
	ASSERT_GT(monsters.size(), 100U);

	gbIsReforged = true;
	LoadRules({ "hf", "reforged" });
	EXPECT_EQ(DescribeMonsters(), monsters);
	EXPECT_EQ(ExperienceTable(), experience);

	// A monster on the floor is the level its table row says, worth the experience it says, on every difficulty.
	LevelMonsterTypes[0].type = MT_NZOMBIE;
	Monster monster {};
	monster.levelType = 0;
	monster.uniqueType = UniqueMonsterType::None;
	monster.rndItemSeed = 4242;
	currlevel = 2;
	for (const _difficulty difficulty : { DIFF_NORMAL, DIFF_NIGHTMARE, DIFF_HELL }) {
		gbIsReforged = false;
		const unsigned level = monster.level(difficulty);
		const unsigned worth = monster.exp(difficulty);
		gbIsReforged = true;
		EXPECT_EQ(monster.level(difficulty), level);
		EXPECT_EQ(monster.exp(difficulty), worth);
		EXPECT_EQ(MonsterExperiencePercent(monster), 100);
	}
	EXPECT_EQ(monster.level(DIFF_NORMAL), static_cast<unsigned>(MonstersData[MT_NZOMBIE].level));
	currlevel = 0;
}

TEST_F(ReforgedModeTest, ACathedralLevelIsBuiltTheSameAsDiablos)
{
	// With New Cathedral Floors off (on, the floor is built from the designed rooms: reforged_floors_test.cpp).
	const ScopedFloorsSetting off(GetOptions().Gameplay.newCathedralFloors);
	LoadExpectedLevelData("diablo/1-2588.dun");
	TestInitGame();
	gbIsReforged = true;
	TestCreateDungeon(1, 2588, ENTRY_MAIN);
	EXPECT_EQ(ViewPosition, Point(77, 46));
	TestCreateDungeon(1, 2588, ENTRY_PREV);
	EXPECT_EQ(ViewPosition, Point(49, 49));
}

TEST_F(ReforgedModeTest, AHellfireLevelIsBuiltTheSameAsHellfires)
{
	const ScopedFloorsSetting off(GetOptions().Gameplay.newCathedralFloors);
	LoadExpectedLevelData("hellfire/1-401921334.dun");
	TestInitGame(true, false);
	gbIsReforged = true;
	TestCreateDungeon(1, 401921334, ENTRY_MAIN);
	EXPECT_EQ(ViewPosition, Point(79, 80));
	TestCreateDungeon(1, 401921334, ENTRY_PREV);
	EXPECT_EQ(ViewPosition, Point(49, 63));
}

TEST_F(ReforgedModeTest, ACryptLevelIsBuiltTheSameAsHellfires)
{
	const ScopedFloorsSetting off(GetOptions().Gameplay.newCryptFloors);
	LoadExpectedLevelData("hellfire/21-2122696790.dun");
	TestInitGame(true, true, true);
	gbIsReforged = true;
	TestCreateDungeon(21, 2122696790, ENTRY_TWARPDN);
	EXPECT_EQ(ViewPosition, Point(61, 81));
	TestCreateDungeon(21, 2122696790, ENTRY_PREV);
	EXPECT_EQ(ViewPosition, Point(53, 67));
}

} // namespace
} // namespace devilution
