#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "dead.h"
#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "monster.h"
#include "panels/monster_spawner.hpp"
#include "tables/monstdat.h"

namespace devilution {
namespace {

class Poe2MonsterSpawnerTest : public ::testing::Test {
public:
	void SetUp() override
	{
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		gbIsPoe2 = true;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadMonsterData();
		list = BuildMonsterSpawnerList([](const MonsterData &) { return true; });
	}

	void TearDown() override
	{
		gbIsSpawn = false;
	}

	[[nodiscard]] const MonsterSpawnerEntry *FindUnique(std::string_view name) const
	{
		const auto it = std::find_if(list.begin(), list.end(), [name](const MonsterSpawnerEntry &entry) { return entry.unique >= 0 && entry.name == name; });
		return it != list.end() ? &*it : nullptr;
	}

	std::vector<MonsterSpawnerEntry> list;
};

TEST_F(Poe2MonsterSpawnerTest, ListsNormalMonstersThenUniques)
{
	ASSERT_FALSE(list.empty());
	EXPECT_EQ(list.front().name, "Zombie");
	EXPECT_EQ(list.front().type, MT_NZOMBIE);
	EXPECT_EQ(list.front().unique, -1);
	EXPECT_EQ(list.front().detail, "zombie");

	// The Butcher is listed as his monster type and as the unique.
	const MonsterSpawnerEntry *butcher = FindUnique("The Butcher");
	ASSERT_NE(butcher, nullptr);
	EXPECT_EQ(butcher->type, MT_CLEAVER);
	EXPECT_GE(butcher->unique, 0);

	// Every unique comes after every normal monster.
	const auto firstUnique = std::find_if(list.begin(), list.end(), [](const MonsterSpawnerEntry &entry) { return entry.unique >= 0; });
	EXPECT_TRUE(std::all_of(firstUnique, list.end(), [](const MonsterSpawnerEntry &entry) { return entry.unique >= 0; }));
}

TEST_F(Poe2MonsterSpawnerTest, LeavesOutMonstersWithoutPictures)
{
	const std::string zombiePath = MonstersData[MT_NZOMBIE].spritePath();
	const std::vector<MonsterSpawnerEntry> noZombies = BuildMonsterSpawnerList([&zombiePath](const MonsterData &data) { return data.spritePath() != zombiePath; });
	for (const MonsterSpawnerEntry &entry : noZombies)
		EXPECT_NE(entry.type, MT_NZOMBIE) << entry.name;
	// Nor the zombie uniques.
	EXPECT_TRUE(std::none_of(noZombies.begin(), noZombies.end(), [](const MonsterSpawnerEntry &entry) { return entry.name == "Rotfeast the Hungry"; }));
}

TEST_F(Poe2MonsterSpawnerTest, SharewareLeavesOutRetailMonsters)
{
	gbIsSpawn = true;
	const std::vector<MonsterSpawnerEntry> shareware = BuildMonsterSpawnerList([](const MonsterData &) { return true; });
	ASSERT_FALSE(shareware.empty());
	for (const MonsterSpawnerEntry &entry : shareware)
		EXPECT_NE(MonstersData[entry.type].availability, MonsterAvailability::Retail) << entry.name;
}

TEST_F(Poe2MonsterSpawnerTest, FiltersBySearchAndKind)
{
	const std::vector<size_t> zombies = FilterMonsterSpawnerList(list, "ZOMB", MonsterSpawnerFilter::Normal);
	ASSERT_FALSE(zombies.empty());
	for (const size_t i : zombies)
		EXPECT_EQ(list[i].unique, -1);
	EXPECT_EQ(list[zombies.front()].name, "Zombie");

	const std::vector<size_t> rotfeast = FilterMonsterSpawnerList(list, "rotfeast", MonsterSpawnerFilter::All);
	ASSERT_EQ(rotfeast.size(), 1U);
	EXPECT_EQ(list[rotfeast[0]].type, MT_NZOMBIE);
	EXPECT_TRUE(FilterMonsterSpawnerList(list, "rotfeast", MonsterSpawnerFilter::Normal).empty());

	const std::vector<size_t> uniques = FilterMonsterSpawnerList(list, "", MonsterSpawnerFilter::Unique);
	ASSERT_FALSE(uniques.empty());
	for (const size_t i : uniques)
		EXPECT_GE(list[i].unique, 0);
	EXPECT_EQ(FilterMonsterSpawnerList(list, "", MonsterSpawnerFilter::All).size(), list.size());

	// "hd" finds the monsters that have HD art.
	list.front().hdArt = true;
	const std::vector<size_t> hd = FilterMonsterSpawnerList(list, "hd", MonsterSpawnerFilter::All);
	ASSERT_FALSE(hd.empty());
	EXPECT_EQ(hd.front(), 0U);
}

TEST_F(Poe2MonsterSpawnerTest, NamesHdArtAsTheGameLooksItUp)
{
	EXPECT_EQ(MonsterArtName(MonstersData[MT_NZOMBIE], MonsterGraphic::Stand, Direction::South, 0), "monsters/zombie/zombien/d0_f00");
	EXPECT_EQ(MonsterArtName(MonstersData[MT_NZOMBIE], MonsterGraphic::Death, Direction::East, 15), "monsters/zombie/zombied/d6_f15");
	// A recoloured type's art is its own.
	EXPECT_EQ(MonsterArtName(MonstersData[MT_BZOMBIE], MonsterGraphic::Attack, Direction::NorthWest, 3), "monsters/zombie/zombiea@zombie/bluered/d3_f03");
}

TEST_F(Poe2MonsterSpawnerTest, AddsACorpseKindForASpawnedType)
{
	HeadlessMode = true;
	LevelMonsterTypeCount = 1;
	LevelMonsterTypes[0].type = MT_NZOMBIE;
	ActiveMonsterCount = 0;
	InitCorpses();
	// The level's one type, the blood spatter and the stone shatter.
	ASSERT_EQ(stonendx, 3);

	CMonster spawned;
	spawned.type = MT_SKING;
	spawned.anims[static_cast<size_t>(MonsterGraphic::Death)].frames = 16;
	spawned.anims[static_cast<size_t>(MonsterGraphic::Death)].width = 160;
	EXPECT_EQ(AddCorpseKind(spawned, 0), 4);
	EXPECT_EQ(Corpses[3].frame, 15);
	EXPECT_EQ(Corpses[3].width, 160);
	EXPECT_EQ(Corpses[3].translationPaletteIndex, 0);
	// A unique's corpse keeps its colours.
	EXPECT_EQ(AddCorpseKind(spawned, 12), 5);
	EXPECT_EQ(Corpses[4].translationPaletteIndex, 12);

	// Every slot taken: none left.
	for (unsigned id = 6; id <= MaxCorpses; id++)
		EXPECT_EQ(AddCorpseKind(spawned, 0), static_cast<int8_t>(id));
	EXPECT_EQ(AddCorpseKind(spawned, 0), 0);

	// A new level starts over, with nothing left in the slots after its own.
	InitCorpses();
	EXPECT_EQ(Corpses[3].width, 0);
	EXPECT_EQ(Corpses[MaxCorpses - 1].translationPaletteIndex, 0);
	EXPECT_EQ(AddCorpseKind(spawned, 0), 4);
	LevelMonsterTypeCount = 0;
}

TEST(Poe2MonsterSpawnerLogic, TurnsClockwiseOnScreen)
{
	EXPECT_EQ(TurnDirection(Direction::South, 1), Direction::SouthWest);
	EXPECT_EQ(TurnDirection(Direction::South, -1), Direction::SouthEast);
	EXPECT_EQ(TurnDirection(Direction::SouthEast, 1), Direction::South);
	EXPECT_EQ(TurnDirection(Direction::North, 4), Direction::South);
	EXPECT_EQ(TurnDirection(Direction::West, 8), Direction::West);
	EXPECT_EQ(TurnDirection(Direction::West, -10), Direction::South);
}

TEST(Poe2MonsterSpawnerLogic, LoopsAnimationsAtTheGameSpeed)
{
	// 11 frames, 4 game ticks (200 ms) each.
	EXPECT_EQ(MonsterPreviewFrameAt(11, 4, 0, false).frame, 0);
	const MonsterPreviewFrame almost = MonsterPreviewFrameAt(11, 4, 150, false);
	EXPECT_EQ(almost.frame, 0);
	EXPECT_FLOAT_EQ(almost.fraction, 0.75F);
	EXPECT_EQ(MonsterPreviewFrameAt(11, 4, 200, false).frame, 1);
	EXPECT_EQ(MonsterPreviewFrameAt(11, 4, 2199, false).frame, 10);
	EXPECT_EQ(MonsterPreviewFrameAt(11, 4, 2200, false).frame, 0);
	// Bad data doesn't divide by zero.
	EXPECT_EQ(MonsterPreviewFrameAt(0, 0, 1234, false).frame, 0);
}

TEST(Poe2MonsterSpawnerLogic, DeathHoldsItsLastFrame)
{
	// 16 frames, a tick each: the last frame shows from 750 ms, held for the pause, then it plays again.
	EXPECT_EQ(MonsterPreviewFrameAt(16, 1, 749, true).frame, 14);
	EXPECT_EQ(MonsterPreviewFrameAt(16, 1, 750, true).frame, 15);
	const MonsterPreviewFrame held = MonsterPreviewFrameAt(16, 1, 750 + MonsterPreviewDeathHoldMs - 1, true);
	EXPECT_EQ(held.frame, 15);
	EXPECT_FLOAT_EQ(held.fraction, 0.F);
	EXPECT_EQ(MonsterPreviewFrameAt(16, 1, 750 + MonsterPreviewDeathHoldMs, true).frame, 0);
}

TEST(Poe2MonsterSpawnerCl2Test, RejectsTheBrokenMalignusWalkStub)
{
	// monsters/darkmage/dmagew.cl2 from Diablo's data: 96 bytes whose frame tables point past the file.
	const std::string hex = "20000000270000002e000000350000003c000000430000004a0000005100000000000000080000000000000008000000000000000800000000000000080000000000000008000000000000000800000000000000080000000000000008000000";
	std::vector<uint8_t> stub;
	for (size_t i = 0; i < hex.size(); i += 2)
		stub.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
	ASSERT_EQ(stub.size(), 96U);
	EXPECT_FALSE(MonsterCl2LooksValid(stub.data(), stub.size()));
}

TEST(Poe2MonsterSpawnerCl2Test, AcceptsWellFormedListsAndSheets)
{
	const auto put = [](std::vector<uint8_t> &out, uint32_t value) {
		for (int i = 0; i < 4; i++)
			out.push_back(static_cast<uint8_t>(value >> (8 * i)));
	};
	// One list: 2 frames of 3 bytes each after a header of 4 * (2 + 2) bytes.
	std::vector<uint8_t> list;
	put(list, 2);
	put(list, 16);
	put(list, 19);
	put(list, 22);
	list.resize(22, 0);
	EXPECT_TRUE(MonsterCl2LooksValid(list.data(), list.size()));

	// A sheet of two such lists.
	std::vector<uint8_t> sheet;
	put(sheet, 8);
	put(sheet, 8 + 22);
	sheet.insert(sheet.end(), list.begin(), list.end());
	sheet.insert(sheet.end(), list.begin(), list.end());
	EXPECT_TRUE(MonsterCl2LooksValid(sheet.data(), sheet.size()));

	// Cut short: the second list's frames end past the file.
	sheet.resize(sheet.size() - 5);
	EXPECT_FALSE(MonsterCl2LooksValid(sheet.data(), sheet.size()));
	EXPECT_FALSE(MonsterCl2LooksValid(nullptr, 0));
}

} // namespace
} // namespace devilution
