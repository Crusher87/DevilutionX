#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "monster.h"
#include "player.h"
#include "poe2/experience.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/waystones.hpp"
#include "spells.h"
#include "stores.h"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2ExperienceTest : public ::testing::Test {
public:
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
		sgGameInitInfo.nDifficulty = DIFF_NORMAL;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadSpellData();
		LoadMonsterData();
		LoadPlayerDataFiles();
		LevelMonsterTypes[0].type = MT_NZOMBIE;
		LevelMonsterTypes[1].type = MT_DIABLO;
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		gbIsMultiplayer = false;
		currlevel = 0;
		UnloadModArchives();
	}

	/** A monster of level type 0 (or the given one) whose seed rolls no rarity. */
	static Monster MakeMonster(MonsterRarity rarity = MonsterRarity::Normal, uint8_t levelType = 0)
	{
		Monster monster {};
		monster.levelType = levelType;
		monster.uniqueType = UniqueMonsterType::None;
		monster.flags = 0;
		for (uint32_t seed = 1; seed < 100000; seed++) {
			monster.rndItemSeed = seed;
			if (GetMonsterAffixes(monster).rarity == rarity)
				return monster;
		}
		ADD_FAILURE() << "no seed rolls that rarity";
		return monster;
	}

	static Monster MakeUnique(UniqueMonsterType unique)
	{
		Monster monster = MakeMonster();
		monster.uniqueType = unique;
		return monster;
	}

	static uint32_t Threshold(int level)
	{
		return level <= 0 ? 0 : GetNextExperienceThresholdForLevel(static_cast<unsigned>(level));
	}

	static int MonsterLevel(const Monster &monster)
	{
		return static_cast<int>(monster.level(sgGameInitInfo.nDifficulty));
	}
};

TEST_F(Poe2ExperienceTest, TableHasHundredLevels)
{
	EXPECT_EQ(GetMaximumCharacterLevel(), 100);
	EXPECT_EQ(MyPlayer->getMaxCharacterLevel(), 100);
	EXPECT_EQ(Threshold(1), 525);
	EXPECT_EQ(Threshold(12), 157384);
	EXPECT_EQ(Threshold(85), 1009443795U);
	EXPECT_EQ(Threshold(99), 3185330229U);
	EXPECT_EQ(Threshold(100), Threshold(99));
	for (int level = 2; level < 100; level++)
		EXPECT_GT(Threshold(level), Threshold(level - 1)) << level;
}

TEST_F(Poe2ExperienceTest, WhiteAtArea13Gives683)
{
	// Refit for level 10 after about 550 kills (was 470 at 650 kills).
	EXPECT_EQ(MonsterBaseExperience(2), 18);
	EXPECT_EQ(MonsterBaseExperience(5), 73);
	EXPECT_EQ(MonsterBaseExperience(13), 683);
	EXPECT_EQ(MonsterBaseExperience(25), 4814);
	EXPECT_EQ(MonsterBaseExperience(38), 20240);
	EXPECT_EQ(MonsterBaseExperience(63), 114234);
	EXPECT_EQ(MonsterBaseExperience(80), 135287);
	EXPECT_EQ(MonsterBaseExperience(100), 135287);
	EXPECT_EQ(MonsterBaseExperience(0), MonsterBaseExperience(1));
	for (int level = 2; level <= 100; level++)
		EXPECT_GE(MonsterBaseExperience(level), MonsterBaseExperience(level - 1)) << level;

	const Monster white = MakeMonster();
	EXPECT_EQ(Poe2MonsterExperience(white), MonsterBaseExperience(MonsterLevel(white)));
}

TEST_F(Poe2ExperienceTest, RarityCountsOnce)
{
	const Monster magic = MakeMonster(MonsterRarity::Magic);
	const Monster rare = MakeMonster(MonsterRarity::Rare);
	const uint32_t base = MonsterBaseExperience(MonsterLevel(rare));
	EXPECT_EQ(Poe2MonsterExperience(magic), base * 150 / 100);
	EXPECT_EQ(Poe2MonsterExperience(rare), base * 300 / 100);
}

TEST_F(Poe2ExperienceTest, BossesUseTheirMultiplier)
{
	const Monster butcher = MakeUnique(UniqueMonsterType::Butcher);
	EXPECT_EQ(MonsterBossExperiencePercent(butcher), 700);
	EXPECT_EQ(Poe2MonsterExperience(butcher), MonsterBaseExperience(MonsterLevel(butcher)) * 7);
	EXPECT_EQ(MonsterBossExperiencePercent(MakeUnique(UniqueMonsterType::SkeletonKing)), 800);
	EXPECT_EQ(MonsterBossExperiencePercent(MakeUnique(UniqueMonsterType::Lazarus)), 1000);
	EXPECT_EQ(MonsterBossExperiencePercent(MakeUnique(UniqueMonsterType::NaKrul)), 1500);
	// Bonehead Keenaxe: any unique not in the table.
	EXPECT_EQ(MonsterBossExperiencePercent(MakeUnique(static_cast<UniqueMonsterType>(13))), 450);
	// Diablo isn't a unique; his type carries the multiplier.
	const Monster diablo = MakeMonster(MonsterRarity::Normal, 1);
	EXPECT_EQ(MonsterBossExperiencePercent(diablo), 1500);
	EXPECT_EQ(MonsterBossExperiencePercent(MakeMonster()), 100);
}

TEST_F(Poe2ExperienceTest, SafeZoneFullWithinThreeAtLevel10)
{
	for (int monsterLevel = 7; monsterLevel <= 13; monsterLevel++)
		EXPECT_EQ(ExperienceLevelGapPermille(10, monsterLevel), 1000) << monsterLevel;
}

TEST_F(Poe2ExperienceTest, GapAtLevel10)
{
	EXPECT_EQ(ExperienceLevelGapPermille(10, 14), 908);
	EXPECT_EQ(ExperienceLevelGapPermille(10, 15), 619);
	EXPECT_EQ(ExperienceLevelGapPermille(10, 16), 343);
	// Monsters under the hero cost the same.
	EXPECT_EQ(ExperienceLevelGapPermille(10, 6), 908);
	// Floor 1%.
	EXPECT_EQ(ExperienceLevelGapPermille(10, 40), 10);
	EXPECT_EQ(ExperienceLevelGapPermille(10, 79), 10);
	// The safe zone grows: 3 + level / 16.
	EXPECT_EQ(ExperienceLevelGapPermille(32, 37), 1000);
	EXPECT_LT(ExperienceLevelGapPermille(32, 38), 1000);
}

TEST_F(Poe2ExperienceTest, TopMapsPayFull)
{
	EXPECT_EQ(ExperienceLevelGapPermille(30, 80), 1000);
	EXPECT_EQ(ExperienceLevelGapPermille(50, 82), 1000);
	EXPECT_EQ(ExperienceLevelGapPermille(100, 80), 1000);
	EXPECT_LT(ExperienceLevelGapPermille(100, 79), 1000);
}

TEST_F(Poe2ExperienceTest, MultiplayerKeepsAreaKillXp)
{
	// Diablo's 200 x level cap would cut this kill to 7,600.
	EXPECT_EQ(Poe2ExperienceGain(38, 20240, 38, true), 20240);
	EXPECT_EQ(Poe2ExperienceGain(38, 20240, 38, false), 20240);
	// The cap at a twentieth of the level's threshold stays.
	EXPECT_EQ(Poe2ExperienceGain(1, 1000, 1, true), 525 / 20);
	EXPECT_EQ(Poe2ExperienceGain(1, 1000, 1, false), 1000);
	EXPECT_EQ(Poe2ExperienceGain(10, 1000, 15, false), 619);
	EXPECT_EQ(Poe2ExperienceGain(100, std::numeric_limits<uint32_t>::max(), 100, false), std::numeric_limits<uint32_t>::max());
}

TEST_F(Poe2ExperienceTest, MapDeathLosesTenPercent)
{
	const uint32_t start = Threshold(9);
	const uint32_t need = Threshold(10) - start;
	EXPECT_EQ(ExperienceAfterMapDeath(10, start + 5000), start + 5000 - need / 10);
	EXPECT_EQ(ExperienceAfterMapDeath(10, start + 1000), start);
	EXPECT_EQ(ExperienceAfterMapDeath(10, start), start);
	EXPECT_EQ(ExperienceAfterMapDeath(100, Threshold(99)), Threshold(99));

	Player &player = *MyPlayer;
	player.setCharacterLevel(10);
	player._pExperience = start + 5000;
	// Outside a map nothing is lost.
	LoseMapDeathExperience(player);
	EXPECT_EQ(player._pExperience, start + 5000);

	const int level = PrepareMap(player, 2, 5);
	currlevel = static_cast<uint8_t>(level);
	player._pMaxHP = Fixed26_6::fromInt(100);
	player._pMaxHPBase = player._pMaxHP;
	player.poe2.mapRevives = 1;
	EXPECT_TRUE(TryMapRevive(player));
	EXPECT_EQ(player._pExperience, start + 5000 - need / 10);
	EXPECT_EQ(player.getCharacterLevel(), 10);
}

TEST_F(Poe2ExperienceTest, MigrationKeepsLevelAndFraction)
{
	Player &player = *MyPlayer;
	player.setCharacterLevel(30);
	const uint32_t oldLow = DiabloExperienceThreshold(29);
	const uint32_t oldHigh = DiabloExperienceThreshold(30);
	EXPECT_EQ(oldLow, 15571031);
	EXPECT_EQ(oldHigh, 20066900);
	player._pExperience = oldLow + (oldHigh - oldLow) / 4;
	EXPECT_TRUE(CorrectPoe2Experience(player));
	const uint64_t low = Threshold(29);
	const uint64_t high = Threshold(30);
	EXPECT_EQ(player._pExperience, low + (static_cast<uint64_t>(oldHigh - oldLow) / 4) * (high - low) / (oldHigh - oldLow));
	EXPECT_EQ(player.getCharacterLevel(), 30);
	// Once inside the band it never moves again.
	const uint32_t moved = player._pExperience;
	EXPECT_FALSE(CorrectPoe2Experience(player));
	EXPECT_EQ(player._pExperience, moved);
}

TEST_F(Poe2ExperienceTest, OldCapHeroStaysAtLevel50)
{
	Player &player = *MyPlayer;
	player.setCharacterLevel(50);
	player._pExperience = DiabloExperienceThreshold(50);
	EXPECT_EQ(player._pExperience, 1583495809U);
	EXPECT_TRUE(CorrectPoe2Experience(player));
	EXPECT_EQ(player._pExperience, Threshold(50) - 1);
	EXPECT_EQ(player.getCharacterLevel(), 50);
}

TEST_F(Poe2ExperienceTest, OutsideBothBandsClampsIntoTheNewBand)
{
	Player &player = *MyPlayer;
	player.setCharacterLevel(30);
	player._pExperience = 1;
	EXPECT_TRUE(CorrectPoe2Experience(player));
	EXPECT_EQ(player._pExperience, Threshold(29));
	player._pExperience = 3000000000U;
	EXPECT_TRUE(CorrectPoe2Experience(player));
	EXPECT_EQ(player._pExperience, Threshold(30) - 1);
	// Past the old table's 50 levels there is only the new band.
	player.setCharacterLevel(70);
	player._pExperience = 5;
	EXPECT_TRUE(CorrectPoe2Experience(player));
	EXPECT_EQ(player._pExperience, Threshold(69));
	// A hero exactly on a band edge is left alone.
	player._pExperience = Threshold(70);
	EXPECT_FALSE(CorrectPoe2Experience(player));
}

TEST_F(Poe2ExperienceTest, Level50HeroLevelsOn)
{
	Player &player = *MyPlayer;
	player.setCharacterLevel(50);
	EXPECT_FALSE(player.isMaxCharacterLevel());
	EXPECT_EQ(player.getNextExperienceThreshold(), 60565335U);
	player.setCharacterLevel(100);
	EXPECT_TRUE(player.isMaxCharacterLevel());
}

TEST_F(Poe2ExperienceTest, SparklingShrineGivesTenWhites)
{
	EXPECT_EQ(Poe2ShrineExperience(13), 6830);
	EXPECT_EQ(Poe2ShrineExperience(80), 1352870);
}

TEST_F(Poe2ExperienceTest, WirtLevelStaysInSixBits)
{
	MyPlayer->setCharacterLevel(80);
	BoyItem = {};
	SetRndSeed(7);
	SpawnBoy(MyPlayer->getCharacterLevel());
	ASSERT_FALSE(BoyItem.isEmpty());
	EXPECT_EQ(BoyItem._iCreateInfo & CF_LEVEL, CF_LEVEL);
	EXPECT_NE(BoyItem._iCreateInfo & CF_BOY, 0);
}

TEST_F(Poe2ExperienceTest, DiabloModeXpUnchanged)
{
	gbIsPoe2 = false;
	UnloadModArchives();
	LoadModArchives({ { "hf" } });
	LoadPlayerDataFiles();
	EXPECT_EQ(GetMaximumCharacterLevel(), 50);
	EXPECT_EQ(Threshold(1), 2000);
	EXPECT_EQ(Threshold(50), 1583495809U);
	EXPECT_EQ(MonsterBaseExperience(13), 0);
	Player &player = *MyPlayer;
	player.setCharacterLevel(30);
	player._pExperience = 1;
	EXPECT_FALSE(CorrectPoe2Experience(player));
	EXPECT_EQ(player._pExperience, 1);
	// No map experience loss outside POE2, even with a map's state set.
	player._pExperience = 15571031 + 5000;
	player.poe2.mapTier = 2;
	player.poe2.mapLevel = 13;
	currlevel = 13;
	LoseMapDeathExperience(player);
	EXPECT_EQ(player._pExperience, 15571031 + 5000);
}

} // namespace
} // namespace devilution
