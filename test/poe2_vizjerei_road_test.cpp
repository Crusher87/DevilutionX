#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/monster_mods.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/unique_rules.hpp"
#include "storm/storm_net.hpp"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

/** The Vizjerei Road (research/tree-rework/profane-road/sickness-rework/spec-v1.md): elemental blasts and their backlash. */
class Poe2VizjereiRoadTest : public ::testing::Test {
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
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadSpellData();
		LoadPlayerDataFiles();
		LoadMissileData();
		LoadMonsterData();
		LevelMonsterTypes[0].type = MT_NZOMBIE;
		Missiles.clear();
		leveltype = DTYPE_CATHEDRAL;
		MyPlayer->setCharacterLevel(30);
		MyPlayer->position.tile = { 30, 30 };
		MyPlayer->_pMaxHP = MyPlayer->_pHitPoints = Fixed26_6::fromInt(1000);
		MyPlayer->_pMaxHPBase = MyPlayer->_pHPBase = Fixed26_6::fromInt(1000);
		ActiveMonsterCount = 0;
		// Damage to monsters goes out as a network message.
		SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
	}

	void TearDown() override
	{
		ActiveMonsterCount = 0;
		Missiles.clear();
		CachePassiveTotals(*MyPlayer, {});
		gbIsPoe2 = false;
		leveltype = DTYPE_TOWN;
		UnloadModArchives();
	}

	/** @brief Walks the road up to the toll, and to the pact if asked. */
	static void WalkRoad(bool pact)
	{
		PassiveTotals totals {};
		totals[static_cast<size_t>(PassiveStat::DarkSickness)] = 1;
		if (pact)
			totals[static_cast<size_t>(PassiveStat::HiveBreath)] = 1;
		CachePassiveTotals(*MyPlayer, totals);
		MyPlayer->poe2.uniqueRules[static_cast<size_t>(UniqueRule::KillBurst)] = 10;
	}

	static Monster &PlaceMonster(size_t slot, Point tile, int life = 10000)
	{
		Monster &monster = Monsters[slot];
		monster = {};
		monster.levelType = 0;
		monster.uniqueType = UniqueMonsterType::Garbud;
		monster.rndItemSeed = 1;
		monster.talkMsg = TEXT_NONE;
		monster.position.tile = tile;
		monster.maxHitPoints = monster.hitPoints = Fixed26_6::fromInt(life);
		ActiveMonsters[ActiveMonsterCount] = static_cast<unsigned>(slot);
		ActiveMonsterCount++;
		return monster;
	}

	static void MeleeKill(const Monster &corpse)
	{
		UniqueRuleState &state = MyPlayer->poe2.uniqueState;
		state.lastHitBySpell = false;
		state.lastHitRanged = false;
		OnMonsterKilled(*MyPlayer, corpse);
	}

	static void SpellKill(const Monster &corpse)
	{
		UniqueRuleState &state = MyPlayer->poe2.uniqueState;
		state.lastHitBySpell = true;
		state.lastHitRanged = false;
		OnMonsterKilled(*MyPlayer, corpse);
	}
};

TEST_F(Poe2VizjereiRoadTest, OnlyFireColdAndLightningMakeABlast)
{
	WalkRoad(false);
	Monster &corpse = PlaceMonster(0, { 32, 32 }, 1000);
	Monster &beside = PlaceMonster(1, { 33, 32 });
	// Poison alone doesn't count.
	corpse.poe2.poisonTicks = 40;
	corpse.poe2.poisonDamagePerTick = 5;
	MeleeKill(corpse);
	EXPECT_EQ(beside.hitPoints, beside.maxHitPoints);
	for (const DamageType element : { DamageType::Fire, DamageType::Cold, DamageType::Lightning })
		EXPECT_EQ(VizjereiBacklashTicks(*MyPlayer, element), 0);
	EXPECT_FALSE(ElementalAilmentOf(corpse).has_value());
}

TEST_F(Poe2VizjereiRoadTest, TheBlastTakesTheAilmentWithTheMostTimeLeft)
{
	Monster &monster = PlaceMonster(0, { 32, 32 });
	monster.poe2.igniteTicks = 10;
	monster.poe2.chillTicks = 30;
	monster.poe2.shockTicks = 20;
	EXPECT_EQ(ElementalAilmentOf(monster), DamageType::Cold);
	monster.poe2.chillTicks = 0;
	monster.poe2.freezeTicks = 5;
	EXPECT_EQ(ElementalAilmentOf(monster), DamageType::Lightning);
}

TEST_F(Poe2VizjereiRoadTest, AChilledCorpseShattersAndTheColdBitesBack)
{
	WalkRoad(false);
	Monster &corpse = PlaceMonster(0, { 32, 32 }, 1000);
	Monster &beside = PlaceMonster(1, { 33, 33 });
	Monster &far = PlaceMonster(2, { 35, 32 });
	corpse.poe2.chillTicks = 30;
	corpse.poe2.chillPercent = 20;
	MeleeKill(corpse);
	EXPECT_GT(beside.poe2.chillTicks, 0);
	EXPECT_GT(beside.poe2.freezeBuildup, 0);
	EXPECT_EQ(far.poe2.chillTicks, 0);
	EXPECT_EQ(far.hitPoints, far.maxHitPoints);
	EXPECT_EQ(VizjereiBacklashTicks(*MyPlayer, DamageType::Cold), 2 * 20);
	EXPECT_EQ(VizjereiLastBacklash(*MyPlayer), DamageType::Cold);
	EXPECT_EQ(UniqueMovementSlowPercent(*MyPlayer), 20);
	EXPECT_EQ(UniqueAttackSpeedPercent(*MyPlayer), -20);
	// Only melee kills blast without the pact.
	beside.poe2.chillTicks = 0;
	MyPlayer->poe2.uniqueState.backlashTicks = {};
	SpellKill(corpse);
	EXPECT_EQ(beside.poe2.chillTicks, 0);
	EXPECT_EQ(VizjereiBacklashTicks(*MyPlayer, DamageType::Cold), 0);
}

TEST_F(Poe2VizjereiRoadTest, ABurningCorpseIgnitesItsNeighboursAndTheFireBurnsTheHero)
{
	WalkRoad(false);
	Monster &corpse = PlaceMonster(0, { 32, 32 }, 1000);
	Monster &beside = PlaceMonster(1, { 31, 32 });
	corpse.poe2.igniteTicks = 30;
	corpse.poe2.igniteDamagePerTick = 64;
	MeleeKill(corpse);
	EXPECT_GT(beside.poe2.igniteTicks, 0);
	EXPECT_GT(beside.poe2.igniteDamagePerTick, 0);
	ASSERT_EQ(VizjereiBacklashTicks(*MyPlayer, DamageType::Fire), 2 * 20);
	// 1.5% of maximum life a second for 2 seconds: 30 of 1000.
	for (int tick = 0; tick < 2 * 20; tick++)
		ProcessUniqueRules(*MyPlayer);
	EXPECT_EQ(MyPlayer->_pHitPoints, Fixed26_6::fromInt(970));
	EXPECT_EQ(VizjereiBacklashTicks(*MyPlayer, DamageType::Fire), 0);
	EXPECT_FALSE(VizjereiLastBacklash(*MyPlayer).has_value());
}

TEST_F(Poe2VizjereiRoadTest, LightningArcsToTheThreeNearestWithinFourTiles)
{
	WalkRoad(false);
	Monster &corpse = PlaceMonster(0, { 32, 32 }, 1000);
	corpse.poe2.shockTicks = 30;
	Monster &one = PlaceMonster(1, { 33, 32 });
	Monster &two = PlaceMonster(2, { 34, 32 });
	Monster &three = PlaceMonster(3, { 32, 35 });
	Monster &fourth = PlaceMonster(4, { 36, 32 });
	Monster &tooFar = PlaceMonster(5, { 37, 32 });
	MeleeKill(corpse);
	EXPECT_GT(one.poe2.shockTicks, 0);
	EXPECT_GT(two.poe2.shockTicks, 0);
	EXPECT_GT(three.poe2.shockTicks, 0);
	EXPECT_EQ(fourth.poe2.shockTicks, 0);
	EXPECT_EQ(tooFar.poe2.shockTicks, 0);
	EXPECT_EQ(VizjereiBacklashTicks(*MyPlayer, DamageType::Lightning), 2 * 20);
	// The lightning backlash: 15% more damage taken.
	EXPECT_EQ(ApplyUniqueHitTaken(*MyPlayer, Fixed26_6::fromInt(100), DamageType::Physical), Fixed26_6::fromInt(115));
}

TEST_F(Poe2VizjereiRoadTest, ANewBlastResetsItsBacklashButNeverAddsToIt)
{
	WalkRoad(false);
	Monster &first = PlaceMonster(0, { 32, 32 }, 1000);
	first.poe2.chillTicks = 30;
	MeleeKill(first);
	for (int tick = 0; tick < 15; tick++)
		ProcessUniqueRules(*MyPlayer);
	EXPECT_EQ(VizjereiBacklashTicks(*MyPlayer, DamageType::Cold), 25);
	MeleeKill(first);
	EXPECT_EQ(VizjereiBacklashTicks(*MyPlayer, DamageType::Cold), 40);
	// Another element runs beside it.
	first.poe2.chillTicks = 0;
	first.poe2.shockTicks = 30;
	MeleeKill(first);
	EXPECT_EQ(VizjereiBacklashTicks(*MyPlayer, DamageType::Cold), 40);
	EXPECT_EQ(VizjereiBacklashTicks(*MyPlayer, DamageType::Lightning), 40);
	EXPECT_EQ(VizjereiLastBacklash(*MyPlayer), DamageType::Lightning);
}

TEST_F(Poe2VizjereiRoadTest, ThePactMakesSpellKillsBlastAndStopsSpellAilments)
{
	WalkRoad(true);
	Monster &corpse = PlaceMonster(0, { 32, 32 }, 1000);
	Monster &beside = PlaceMonster(1, { 33, 32 });
	corpse.poe2.chillTicks = 30;
	SpellKill(corpse);
	EXPECT_GT(beside.poe2.chillTicks, 0);
	EXPECT_EQ(VizjereiBacklashTicks(*MyPlayer, DamageType::Cold), 2 * 20);
	EXPECT_FALSE(AilmentAllowed(*MyPlayer, AilmentSource::Spell));
	// The backlash no longer stops the hero's own ailments.
	EXPECT_TRUE(AilmentAllowed(*MyPlayer, AilmentSource::Strike));
	// 20% less to a monster not burning, chilled, frozen or shocked; poison doesn't protect it.
	Monster &poisoned = PlaceMonster(2, { 40, 40 });
	poisoned.poe2.poisonTicks = 40;
	EXPECT_EQ(VizjereiSpellHitPercent(*MyPlayer, poisoned), 80);
	poisoned.poe2.shockTicks = 40;
	EXPECT_EQ(VizjereiSpellHitPercent(*MyPlayer, poisoned), 100);
}

TEST_F(Poe2VizjereiRoadTest, TheRoadSaysNothingOfSickness)
{
	for (const PassiveStat stat : { PassiveStat::DarkSickness, PassiveStat::HiveBreath }) {
		for (const PassiveStatLine &line : PassiveStatLines(stat, 1)) {
			for (const std::string_view word : { "sick", "Sick", "fever", "Hive", "carr", "contag", "poison" })
				EXPECT_EQ(line.text.find(word), std::string::npos) << line.text;
		}
	}
}

} // namespace
} // namespace devilution
