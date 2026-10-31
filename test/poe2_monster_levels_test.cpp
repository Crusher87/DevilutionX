#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>
#include <utility>

#include "data/file.hpp"
#include "data/record_reader.hpp"
#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "levels/gendung.h"
#include "monster.h"
#include "player.h"
#include "poe2/area_levels.hpp"
#include "poe2/defenses.hpp"
#include "poe2/experience.hpp"
#include "poe2/monster_levels.hpp"
#include "poe2/monster_mods.hpp"
#include "tables/monstdat.h"

namespace devilution {
namespace {

class Poe2MonsterLevelsTest : public ::testing::Test {
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
		setlevel = false;
		setlvlnum = SL_NONE;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadMonsterData();
		// Also loads the monster level tables.
		LoadAreaLevels();
		GoTo(8, DTYPE_CATACOMBS);
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		gbIsMultiplayer = false;
		sgGameInitInfo.nDifficulty = DIFF_NORMAL;
		currlevel = 0;
		leveltype = DTYPE_TOWN;
		LoadAreaLevels();
		UnloadModArchives();
	}

	static void GoTo(int floor, dungeon_type type)
	{
		setlevel = false;
		setlvlnum = SL_NONE;
		currlevel = static_cast<uint8_t>(floor);
		leveltype = type;
	}

	/** A monster of a type (in level type slot @p slot) whose seed rolls no rarity. */
	static Monster MakeMonster(_monster_id type, UniqueMonsterType unique = UniqueMonsterType::None, uint8_t slot = 0)
	{
		LevelMonsterTypes[slot].type = type;
		Monster monster {};
		monster.levelType = slot;
		monster.uniqueType = unique;
		monster.flags = 0;
		for (uint32_t seed = 1; seed < 100000; seed++) {
			monster.rndItemSeed = seed;
			if (GetMonsterAffixes(monster).rarity == MonsterRarity::Normal)
				break;
		}
		monster.minDamage = monster.data().minDamage;
		monster.maxDamage = monster.data().maxDamage;
		return monster;
	}

	static int64_t LifeRaw(int life, int permille)
	{
		return static_cast<int64_t>(life) * 64 * permille / 1000;
	}
};

TEST_F(Poe2MonsterLevelsTest, TableMatchesTheFit)
{
	ASSERT_TRUE(HasMonsterLevelTable());
	// POE2's table times the step-1 dials (KILL_TARGET 2), and up to 15 the attack-speed rebalance's pace (0.875 up to 13).
	EXPECT_EQ(MonsterLevelLife(2), 26);
	EXPECT_EQ(MonsterLevelLife(13), 150);
	EXPECT_EQ(MonsterLevelLife(25), 469);
	EXPECT_EQ(MonsterLevelLife(37), 1194);
	EXPECT_EQ(MonsterLevelLife(80), 5169);
	EXPECT_EQ(MonsterLevelLife(100), 14588);
	EXPECT_EQ(MonsterLevelDamage10(13), 252);
	EXPECT_EQ(MonsterLevelDamage10(80), 1602);
	EXPECT_EQ(MonsterLevelAccuracy(38), 274);
	// Past the table's ends.
	EXPECT_EQ(MonsterLevelLife(0), MonsterLevelLife(1));
	EXPECT_EQ(MonsterLevelLife(200), MonsterLevelLife(100));
}

/**
 * Plan step 14 (Curtis 2026-10-27): map monsters hit the step-13 check's self-found evasion Rogue (935 / 2,016 / 3,727
 * evasion at areas 41 / 59 / 80) about 65% of the time at every tier; up to area 38 nothing changed.
 */
TEST_F(Poe2MonsterLevelsTest, MapMonstersHitEvasionAboutTwoThirds)
{
	EXPECT_EQ(MonsterLevelAccuracy(38), 274);
	EXPECT_EQ(MonsterLevelAccuracy(41), 304);
	EXPECT_EQ(MonsterLevelAccuracy(59), 655);
	EXPECT_EQ(MonsterLevelAccuracy(80), 1211);
	const std::pair<int, int> Tiers[] = { { 41, 935 }, { 59, 2016 }, { 80, 3727 } };
	for (const auto &[level, evasion] : Tiers)
		EXPECT_NEAR(Poe2HitChance(MonsterLevelAccuracy(level), evasion), 65, 1) << level;
	for (int level = 2; level <= 100; level++)
		EXPECT_GE(MonsterLevelAccuracy(level), MonsterLevelAccuracy(level - 1)) << level;
}

TEST_F(Poe2MonsterLevelsTest, SmoothedTableMatchesSource)
{
	// The reference columns hold POE2's table before the dials, 65 -> 80 smoothed (80 = 31,065 / 1.25^2).
	constexpr std::string_view Filename = "txtdata\\poe2\\monster_levels.tsv";
	auto dataFile = DataFile::load(Filename);
	ASSERT_TRUE(dataFile.has_value());
	dataFile->skipHeaderOrDie(Filename);
	int checked = 0;
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		int level = 0;
		int life = 0;
		int unused = 0;
		int poe2Life = 0;
		reader.readInt("level", level);
		reader.readInt("life", life);
		reader.readInt("damage10", unused);
		reader.readInt("accuracy", unused);
		reader.readInt("poe2Life", poe2Life);
		if (level == 1)
			EXPECT_EQ(poe2Life, 15);
		if (level == 10)
			EXPECT_EQ(poe2Life, 67);
		if (level == 40)
			EXPECT_EQ(poe2Life, 1253);
		if (level == 65)
			EXPECT_EQ(poe2Life, 6555);
		if (level == 80)
			EXPECT_EQ(poe2Life, 19882);
		EXPECT_EQ(life, MonsterLevelLife(level));
		checked++;
	}
	EXPECT_EQ(checked, 100);
}

TEST_F(Poe2MonsterLevelsTest, ArmourKeepsPoe2RatioToLife)
{
	// POE2's armour table times our white life over POE2's (monster armour plan, step 1; fresh life values).
	EXPECT_EQ(MonsterLevelArmour(2), 9);
	EXPECT_EQ(MonsterLevelArmour(13), 65);
	EXPECT_NEAR(MonsterLevelArmour(38), 433, 1);
	EXPECT_NEAR(MonsterLevelArmour(80), 1249, 1);
	// Past the table's end the last row repeats.
	EXPECT_EQ(MonsterLevelArmour(150), MonsterLevelArmour(100));
}

TEST_F(Poe2MonsterLevelsTest, ArmourNeverFalls)
{
	EXPECT_GT(MonsterLevelArmour(1), 0);
	for (int level = 2; level <= 100; level++)
		EXPECT_GE(MonsterLevelArmour(level), MonsterLevelArmour(level - 1)) << level;
}

TEST_F(Poe2MonsterLevelsTest, LifeNeverFallsAndStepsAtMostThirtyPercent)
{
	for (int level = 2; level <= 100; level++) {
		EXPECT_GE(MonsterLevelLife(level), MonsterLevelLife(level - 1)) << level;
		// The largest step is about 30%, into level 10 (white gear to self-found in the fit).
		EXPECT_LE(MonsterLevelLife(level) * 100, MonsterLevelLife(level - 1) * 131) << level;
		EXPECT_GE(MonsterLevelDamage10(level), MonsterLevelDamage10(level - 1)) << level;
	}
}

TEST_F(Poe2MonsterLevelsTest, WhiteAtAreaThirteenHas150Life)
{
	// Catacombs 4 is area 13; a zombie keeps its roll's ratio to the floor's average life.
	Monster zombie = MakeMonster(MT_NZOMBIE);
	const int roll = 6;
	ASSERT_TRUE(ApplyPoe2MonsterLife(zombie, roll));
	const int permille = MonsterLifeRatioPermille(MT_NZOMBIE, roll);
	EXPECT_EQ(permille, static_cast<int>(int64_t { roll } * 10000 / MonsterTypeRow(MT_NZOMBIE).lifeRef10));
	EXPECT_EQ(zombie.maxHitPoints.raw(), LifeRaw(150, permille));
	EXPECT_EQ(zombie.hitPoints, zombie.maxHitPoints);
	EXPECT_NE(zombie.flags & MFLAG_POE2_TABLES, 0U);

	// The ratio is clamped to 30-250%.
	EXPECT_EQ(MonsterLifeRatioPermille(MT_NZOMBIE, 1000), 2500);
	EXPECT_EQ(MonsterLifeRatioPermille(MT_NZOMBIE, 0), 300);
}

TEST_F(Poe2MonsterLevelsTest, EveryMonsterTypeHasARef)
{
	for (size_t i = 0; i < MonstersData.size(); i++) {
		const MonsterTypeLevelRow &row = MonsterTypeRow(static_cast<_monster_id>(i));
		EXPECT_GT(row.lifeRef10, 0) << MonstersData[i].name;
		EXPECT_GT(row.damageRef10, 0) << MonstersData[i].name;
		EXPECT_GT(row.toHitRef10, 0) << MonstersData[i].name;
		EXPECT_GE(row.damagePercent, MinMonsterTypePercent) << MonstersData[i].name;
		EXPECT_LE(row.damagePercent, MaxMonsterTypePercent) << MonstersData[i].name;
		EXPECT_GE(row.xpPercent, MinMonsterTypePercent) << MonstersData[i].name;
		EXPECT_LE(row.xpPercent, MaxMonsterTypePercent) << MonstersData[i].name;
	}
	// Diablo and Na-Krul never spawn on floors 1-24 by their own range: they take floors 16 and 24.
	EXPECT_EQ(MonsterTypeRow(MT_DIABLO).lifeRef10, MonsterTypeRow(MT_ADVOCATE).lifeRef10);
}

TEST_F(Poe2MonsterLevelsTest, MissingRefFallsBackToHundred)
{
	gbIsPoe2 = false;
	LoadMonsterLevels();
	EXPECT_FALSE(HasMonsterLevelTable());
	EXPECT_EQ(MonsterTypeRow(MT_NZOMBIE).lifeRef10, 0);
	EXPECT_EQ(MonsterTypeRow(MT_NZOMBIE).damagePercent, 100);
	EXPECT_EQ(MonsterLifeRatioPermille(MT_NZOMBIE, 5), 1000);
	EXPECT_EQ(MonsterLevelLife(13), 0);
	gbIsPoe2 = true;
	// Without the table a POE2 monster keeps Diablo's numbers, and its hits stay as rolled.
	Monster zombie = MakeMonster(MT_NZOMBIE);
	zombie.maxHitPoints = Fixed26_6::fromInt(3);
	EXPECT_FALSE(ApplyPoe2MonsterLife(zombie, 5));
	EXPECT_EQ(zombie.maxHitPoints, Fixed26_6::fromInt(3));
	EXPECT_EQ(Poe2MonsterDamagePercent(zombie), 100);
	EXPECT_EQ(MonsterTypeExperiencePercent(zombie), 100);
}

TEST_F(Poe2MonsterLevelsTest, BossLifeIsFloorWhiteTimesMultiplier)
{
	// Diablo on Hell 4 (area 37): a white there times 15, whatever his roll; his level (39) drives his hits.
	GoTo(16, DTYPE_HELL);
	Monster diablo = MakeMonster(MT_DIABLO);
	ASSERT_TRUE(ApplyPoe2MonsterLife(diablo, 1666));
	EXPECT_EQ(diablo.maxHitPoints, Fixed26_6::fromInt(1194 * 15));
	EXPECT_EQ(diablo.level(DIFF_NORMAL), 39U);

	// The Butcher on Cathedral 2 (area 4): 7 whites.
	GoTo(2, DTYPE_CATHEDRAL);
	Monster butcher = MakeMonster(MT_CLEAVER, UniqueMonsterType::Butcher);
	ASSERT_TRUE(ApplyPoe2MonsterLife(butcher, 220));
	EXPECT_EQ(butcher.maxHitPoints, Fixed26_6::fromInt(MonsterLevelLife(4) * 7));

	EXPECT_EQ(MonsterBossRow(MakeMonster(MT_SKING, UniqueMonsterType::SkeletonKing))->lifePercent, 800);
	EXPECT_EQ(MonsterBossRow(MakeMonster(MT_ADVOCATE, UniqueMonsterType::Lazarus))->lifePercent, 1000);
	EXPECT_EQ(MonsterBossRow(MakeMonster(MT_DEFILER, UniqueMonsterType::Defiler))->lifePercent, 1000);
	EXPECT_EQ(MonsterBossRow(MakeMonster(MT_NAKRUL, UniqueMonsterType::NaKrul))->lifePercent, 1500);
	EXPECT_EQ(MonsterBossRow(MakeMonster(MT_NAKRUL))->lifePercent, 1500);
	EXPECT_EQ(MonsterBossRow(MakeMonster(MT_NZOMBIE)), nullptr);
}

TEST_F(Poe2MonsterLevelsTest, UniqueBandThreeToSix)
{
	for (size_t i = static_cast<size_t>(UniqueMonsterType::NaKrul) + 1; i < UniqueMonstersData.size(); i++) {
		const Monster unique = MakeMonster(UniqueMonstersData[i].mtype, static_cast<UniqueMonsterType>(i));
		const MonsterBossLevelRow *row = MonsterBossRow(unique);
		ASSERT_NE(row, nullptr) << UniqueMonstersData[i].mName;
		EXPECT_GE(row->lifePercent, 300) << UniqueMonstersData[i].mName;
		EXPECT_LE(row->lifePercent, 600) << UniqueMonstersData[i].mName;
		EXPECT_GE(row->damagePercent, 100) << UniqueMonstersData[i].mName;
		EXPECT_LE(row->damagePercent, 250) << UniqueMonstersData[i].mName;
	}
}

TEST_F(Poe2MonsterLevelsTest, HitsFollowTheTableAndThe255CapIsGone)
{
	// A zombie at area 13 averages the table's hit times its type's damage ratio, and Act 1's 70% (plan step 14).
	Monster zombie = MakeMonster(MT_NZOMBIE);
	ASSERT_TRUE(ApplyPoe2MonsterLife(zombie, 5));
	const int sum = zombie.data().minDamage + zombie.data().maxDamage;
	const int64_t permille = int64_t { 252 } * MonsterTypeRow(MT_NZOMBIE).damagePercent * 2 / sum * 70 / 100;
	EXPECT_EQ(Poe2MonsterDamagePercent(zombie), static_cast<int>((permille + 5) / 10));
	EXPECT_EQ(ScaleMonsterHit(zombie, 64 * 10), static_cast<int>(64 * 10 * permille / 1000));
	EXPECT_EQ(zombie.minDamage, zombie.data().minDamage);

	// Diablo on Hell difficulty: level 69, twice the table's hit, past the 255 a byte could hold.
	sgGameInitInfo.nDifficulty = DIFF_HELL;
	GoTo(16, DTYPE_HELL);
	Monster diablo = MakeMonster(MT_DIABLO);
	ASSERT_TRUE(ApplyPoe2MonsterLife(diablo, 1666));
	EXPECT_EQ(diablo.level(DIFF_HELL), 69U);
	const int rolled = 45 << 6;
	const int scaled = ScaleMonsterHit(diablo, rolled);
	EXPECT_EQ(scaled, static_cast<int>(int64_t { rolled } * (int64_t { MonsterLevelDamage10(69) } * 200 * 2 / 90) / 1000));
	EXPECT_GT(scaled, 255 << 6);
}

/** Plan step 14 (Curtis 2026-10-27): Act 1's monsters hit 30% softer; the next act's and the minions don't. */
TEST_F(Poe2MonsterLevelsTest, ActOneMonstersHitThirtyPercentSofter)
{
	const auto tableHit = [](Monster &monster) {
		const int sum = monster.data().minDamage + monster.data().maxDamage;
		return int64_t { MonsterLevelDamage10(13) } * MonsterTypeRow(monster.type().type).damagePercent * 2 / sum;
	};
	// Catacombs 4 (area 13) and Caves 1 (area 14): the same zombie, the table's hit at 70% and at 100%.
	Monster act1 = MakeMonster(MT_NZOMBIE);
	ASSERT_TRUE(ApplyPoe2MonsterLife(act1, 5));
	ASSERT_EQ(act1.level(DIFF_NORMAL), 13U);
	EXPECT_EQ(ScaleMonsterHit(act1, 1000), static_cast<int>(tableHit(act1) * 70 / 100));
	GoTo(9, DTYPE_CAVES);
	Monster act2 = MakeMonster(MT_NZOMBIE);
	ASSERT_TRUE(ApplyPoe2MonsterLife(act2, 5));
	ASSERT_EQ(act2.level(DIFF_NORMAL), 14U);
	const int sum = act2.data().minDamage + act2.data().maxDamage;
	EXPECT_EQ(ScaleMonsterHit(act2, 1000), static_cast<int>(int64_t { MonsterLevelDamage10(14) } * MonsterTypeRow(MT_NZOMBIE).damagePercent * 2 / sum));
}

TEST_F(Poe2MonsterLevelsTest, LachdanansNoDamageIsSafe)
{
	GoTo(14, DTYPE_HELL);
	Monster lachdanan = MakeMonster(MT_RBLACK, UniqueMonsterType::Lachdan);
	lachdanan.minDamage = 0;
	lachdanan.maxDamage = 0;
	ASSERT_TRUE(ApplyPoe2MonsterLife(lachdanan, 500));
	EXPECT_EQ(Poe2MonsterDamagePercent(lachdanan), 100);
	EXPECT_EQ(ScaleMonsterHit(lachdanan, 640), 640);
}

TEST_F(Poe2MonsterLevelsTest, SameSeedSameLifeAndNoRandomCalls)
{
	SetRndSeed(1234);
	const uint32_t before = GetLCGEngineState();
	Monster first = MakeMonster(MT_GZOMBIE);
	Monster second = MakeMonster(MT_GZOMBIE);
	ASSERT_TRUE(ApplyPoe2MonsterLife(first, 20));
	ASSERT_TRUE(ApplyPoe2MonsterLife(second, 20));
	EXPECT_EQ(first.maxHitPoints, second.maxHitPoints);
	(void)ScaleMonsterHit(first, 640);
	(void)Poe2MonsterDamagePercent(first);
	EXPECT_EQ(GetLCGEngineState(), before);
}

TEST_F(Poe2MonsterLevelsTest, OldSaveMonstersMoveOntoTheTablesOnce)
{
	// A zombie saved by an older build on Catacombs 4: Diablo's single-player life (data midpoint 5, halved), doubled
	// as a unique's pack, at half life, with Nightmare's bytes.
	sgGameInitInfo.nDifficulty = DIFF_NIGHTMARE;
	Monster zombie = MakeMonster(MT_NZOMBIE);
	const int64_t diablo = (3 * (5 * 64 / 2)) + (64 * 50);
	zombie.maxHitPoints = Fixed26_6::fromRaw(static_cast<int32_t>(2 * diablo));
	zombie.hitPoints = Fixed26_6::fromRaw(static_cast<int32_t>(diablo));
	zombie.minDamage = 8;
	zombie.maxDamage = 14;
	zombie.armorClass = 5 + NightmareAcBonus;
	RescaleLoadedMonster(zombie);
	// Area 13 + 15 on Nightmare.
	const int64_t table = LifeRaw(MonsterLevelLife(28), MonsterLifeRatioPermille(MT_NZOMBIE, 5));
	EXPECT_EQ(zombie.maxHitPoints.raw(), 2 * table);
	EXPECT_EQ(zombie.hitPoints.raw(), table);
	EXPECT_EQ(zombie.minDamage, zombie.data().minDamage);
	EXPECT_EQ(zombie.maxDamage, zombie.data().maxDamage);
	EXPECT_EQ(zombie.armorClass, 5);
	EXPECT_NE(zombie.flags & MFLAG_POE2_TABLES, 0U);

	// Loading it again changes nothing.
	const Fixed26_6 maxLife = zombie.maxHitPoints;
	const Fixed26_6 life = zombie.hitPoints;
	RescaleLoadedMonster(zombie);
	EXPECT_EQ(zombie.maxHitPoints, maxLife);
	EXPECT_EQ(zombie.hitPoints, life);
	EXPECT_EQ(zombie.armorClass, 5);
}

TEST_F(Poe2MonsterLevelsTest, OldSaveRescaleSkipsMinionsAndBerserk)
{
	Monster minion = MakeMonster(MT_WSKELAX);
	minion.flags = MFLAG_GOLEM;
	minion.maxHitPoints = minion.hitPoints = Fixed26_6::fromInt(77);
	minion.minDamage = 9;
	RescaleLoadedMonster(minion);
	EXPECT_EQ(minion.maxHitPoints, Fixed26_6::fromInt(77));
	EXPECT_EQ(minion.minDamage, 9);
	EXPECT_EQ(minion.flags & MFLAG_POE2_TABLES, 0U);

	Monster berserk = MakeMonster(MT_NZOMBIE);
	berserk.flags = MFLAG_BERSERK;
	berserk.maxHitPoints = berserk.hitPoints = Fixed26_6::fromInt(3);
	berserk.minDamage = 9;
	RescaleLoadedMonster(berserk);
	EXPECT_EQ(berserk.maxHitPoints, Fixed26_6::fromInt(3));
	EXPECT_EQ(berserk.minDamage, 9);

	// A monster made on the tables is left as it is (its pack or map multipliers included).
	Monster fresh = MakeMonster(MT_NZOMBIE);
	ASSERT_TRUE(ApplyPoe2MonsterLife(fresh, 5));
	fresh.maxHitPoints *= 2;
	const Fixed26_6 life = fresh.maxHitPoints;
	RescaleLoadedMonster(fresh);
	EXPECT_EQ(fresh.maxHitPoints, life);
}

TEST_F(Poe2MonsterLevelsTest, InfoPanelShowsTableLife)
{
	int minLife = 0;
	int maxLife = 0;
	Poe2TypeLifeRange(MT_NZOMBIE, 13, minLife, maxLife);
	EXPECT_EQ(minLife, 150 * MonsterLifeRatioPermille(MT_NZOMBIE, MonstersData[MT_NZOMBIE].hitPointsMinimum) / 1000);
	EXPECT_EQ(maxLife, 150 * MonsterLifeRatioPermille(MT_NZOMBIE, MonstersData[MT_NZOMBIE].hitPointsMaximum) / 1000);
	Poe2TypeLifeRange(MT_DIABLO, 37, minLife, maxLife);
	EXPECT_EQ(minLife, 1194 * 15);
	EXPECT_EQ(maxLife, 1194 * 15);
}

TEST_F(Poe2MonsterLevelsTest, TypeExperienceIsTheTypesRatio)
{
	const Monster zombie = MakeMonster(MT_NZOMBIE);
	EXPECT_EQ(MonsterTypeExperiencePercent(zombie), MonsterTypeRow(MT_NZOMBIE).xpPercent);
	EXPECT_NE(MonsterTypeExperiencePercent(zombie), 100);
	// Uniques and bosses pay by their own multiplier.
	EXPECT_EQ(MonsterTypeExperiencePercent(MakeMonster(MT_CLEAVER, UniqueMonsterType::Butcher)), 100);
	EXPECT_EQ(MonsterTypeExperiencePercent(MakeMonster(MT_DIABLO)), 100);
}

TEST_F(Poe2MonsterLevelsTest, DiabloModeUntouched)
{
	gbIsPoe2 = false;
	Monster zombie = MakeMonster(MT_NZOMBIE);
	zombie.maxHitPoints = zombie.hitPoints = Fixed26_6::fromInt(3);
	EXPECT_FALSE(ApplyPoe2MonsterLife(zombie, 5));
	zombie.flags |= MFLAG_POE2_TABLES;
	EXPECT_EQ(Poe2MonsterDamagePercent(zombie), 100);
	EXPECT_EQ(ScaleMonsterHit(zombie, 640), 640);
	zombie.flags = 0;
	RescaleLoadedMonster(zombie);
	EXPECT_EQ(zombie.maxHitPoints, Fixed26_6::fromInt(3));
	EXPECT_EQ(zombie.flags, 0U);
}

} // namespace
} // namespace devilution
