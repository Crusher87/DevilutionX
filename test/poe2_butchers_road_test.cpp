#include <gtest/gtest.h>

#include "engine/animationinfo.h"
#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "lighting.h"
#include "missiles.h"
#include "monster.h"
#include "nthread.h"
#include "player.h"
#include "poe2/monster_mods.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/profane_roads.hpp"
#include "poe2/unique_rules.hpp"
#include "storm/storm_net.hpp"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

/** The Butcher's Road (research/tree-rework/profane-road/leoric-rework/spec-v1.md): the hook, the cry and the rage. */
class Poe2ButchersRoadTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayerId = 0;
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
		currlevel = 2;
		MyPlayer->setCharacterLevel(30);
		MyPlayer->setLevel(2);
		MyPlayer->position.tile = MyPlayer->position.future = Hero;
		ClearTiles();
		dPlayer[Hero.x][Hero.y] = 1;
		ActiveMonsterCount = 0;
		SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
	}

	void TearDown() override
	{
		ClearTiles();
		ActiveMonsterCount = 0;
		Missiles.clear();
		CachePassiveTotals(*MyPlayer, {});
		gbIsPoe2 = false;
		leveltype = DTYPE_TOWN;
		currlevel = 0;
		UnloadModArchives();
	}

	static constexpr Point Hero { 30, 30 };

	static void ClearTiles()
	{
		for (int x = 10; x < 60; x++) {
			for (int y = 10; y < 60; y++) {
				dMonster[x][y] = 0;
				dPlayer[x][y] = 0;
			}
		}
	}

	/** @brief Walks the road up to the toll, and to the pact if asked. */
	static void WalkRoad(bool pact)
	{
		PassiveTotals totals {};
		totals[static_cast<size_t>(PassiveStat::DeadDoNotRest)] = 1;
		if (pact)
			totals[static_cast<size_t>(PassiveStat::KingsDeadPact)] = 1;
		CachePassiveTotals(*MyPlayer, totals);
	}

	/** @brief A monster of that rarity standing on the tile, with plenty of life. */
	static Monster &PlaceMonster(size_t slot, Point tile, MonsterRarity rarity = MonsterRarity::Normal)
	{
		Monster &monster = Monsters[slot];
		monster = {};
		monster.levelType = 0;
		monster.uniqueType = UniqueMonsterType::None;
		monster.talkMsg = TEXT_NONE;
		monster.lightId = NO_LIGHT;
		monster.goal = MonsterGoal::Normal;
		monster.mode = MonsterMode::Stand;
		monster.maxHitPoints = monster.hitPoints = Fixed26_6::fromInt(10000);
		monster.position.tile = monster.position.old = monster.position.future = tile;
		for (uint32_t seed = 1; seed < 100000; seed++) {
			monster.rndItemSeed = seed;
			if (GetMonsterAffixes(monster).rarity == rarity)
				break;
		}
		EXPECT_EQ(GetMonsterAffixes(monster).rarity, rarity);
		dMonster[tile.x][tile.y] = static_cast<int16_t>(slot + 1);
		ActiveMonsters[ActiveMonsterCount] = static_cast<unsigned>(slot);
		ActiveMonsterCount++;
		return monster;
	}

	/** @brief A monster the hero has just Culled, as MonsterDeath hands it to the road. */
	static Monster &PlaceCulled(size_t slot, Point tile)
	{
		Monster &corpse = PlaceMonster(slot, tile);
		corpse.poe2.culled = true;
		corpse.hitPoints = Fixed26_6::fromInt(0);
		return corpse;
	}

	static void MeleeCull(const Monster &corpse)
	{
		UniqueRuleState &state = MyPlayer->poe2.uniqueState;
		state.lastHitBySpell = false;
		state.lastHitRanged = false;
		OnProfaneRoadKill(*MyPlayer, corpse);
	}

	static void ArrowCull(const Monster &corpse, Point from)
	{
		UniqueRuleState &state = MyPlayer->poe2.uniqueState;
		state.lastHitBySpell = false;
		state.lastHitRanged = true;
		NoteArrowLine(*MyPlayer, from);
		OnProfaneRoadKill(*MyPlayer, corpse);
	}

	static void Ticks(int count)
	{
		for (int i = 0; i < count; i++)
			ProcessProfaneRoads();
	}
};

TEST_F(Poe2ButchersRoadTest, ACullHooksTheNearestEnemyAndDragsItBesideTheHero)
{
	WalkRoad(false);
	const Monster &corpse = PlaceCulled(0, { 31, 31 });
	Monster &near = PlaceMonster(1, { 35, 30 });
	Monster &far = PlaceMonster(2, { 36, 33 });
	MeleeCull(corpse);
	EXPECT_TRUE(IsHooked(near));
	EXPECT_FALSE(IsHooked(far));
	EXPECT_EQ(near.mode, MonsterMode::HitRecovery);
	// It can't act while it is dragged.
	EXPECT_TRUE(MonsterSkipsTick(near));
	Ticks(1);
	EXPECT_LT(Point(near.position.tile).WalkingDistance(Hero), 5);
	Ticks(HookDragTicks - 1);
	EXPECT_FALSE(IsHooked(near));
	EXPECT_EQ(Point(near.position.tile), Point(31, 30));
	EXPECT_EQ(dMonster[31][30], 2);
	EXPECT_EQ(dMonster[35][30], 0);
	EXPECT_EQ(Point(far.position.tile), Point(36, 33));
}

TEST_F(Poe2ButchersRoadTest, ANextCullCatchesAnotherEnemy)
{
	WalkRoad(false);
	const Monster &corpse = PlaceCulled(0, { 31, 31 });
	const Monster &first = PlaceMonster(1, { 35, 30 });
	const Monster &second = PlaceMonster(2, { 35, 33 });
	MeleeCull(corpse);
	EXPECT_TRUE(IsHooked(first));
	EXPECT_FALSE(IsHooked(second));
	MeleeCull(corpse);
	EXPECT_TRUE(IsHooked(second));
}

TEST_F(Poe2ButchersRoadTest, OnlyACullOnTheRoadThrowsTheHook)
{
	Monster &corpse = PlaceCulled(0, { 31, 31 });
	const Monster &near = PlaceMonster(1, { 35, 30 });
	// Not on the road.
	MeleeCull(corpse);
	EXPECT_FALSE(IsHooked(near));
	EXPECT_EQ(near.activeForTicks, 0);
	// On it, a kill that wasn't a Cull.
	WalkRoad(false);
	corpse.poe2.culled = false;
	MeleeCull(corpse);
	EXPECT_FALSE(IsHooked(near));
	// Never in town.
	corpse.poe2.culled = true;
	leveltype = DTYPE_TOWN;
	MeleeCull(corpse);
	EXPECT_FALSE(IsHooked(near));
	leveltype = DTYPE_CATHEDRAL;
	MeleeCull(corpse);
	EXPECT_TRUE(IsHooked(near));
}

TEST_F(Poe2ButchersRoadTest, TheHookReachesSixTilesAndNeverAMinion)
{
	WalkRoad(false);
	const Monster &corpse = PlaceCulled(0, { 31, 31 });
	Monster &minion = PlaceMonster(1, { 33, 31 });
	minion.flags |= MFLAG_GOLEM;
	const Monster &beyond = PlaceMonster(2, { 38, 31 });
	MeleeCull(corpse);
	EXPECT_FALSE(IsHooked(minion));
	EXPECT_FALSE(IsHooked(beyond));
	EXPECT_EQ(Point(minion.position.tile), Point(33, 31));
	const Monster &inReach = PlaceMonster(3, { 37, 31 });
	MeleeCull(corpse);
	EXPECT_TRUE(IsHooked(inReach));
}

TEST_F(Poe2ButchersRoadTest, TheDragStopsAtAnotherMonster)
{
	WalkRoad(false);
	const Monster &corpse = PlaceCulled(0, { 38, 31 });
	Monster &hooked = PlaceMonster(1, { 36, 30 });
	const Monster &blocker = PlaceMonster(2, { 33, 30 });
	MeleeCull(corpse);
	ASSERT_TRUE(IsHooked(hooked));
	EXPECT_FALSE(IsHooked(blocker));
	Ticks(HookDragTicks);
	EXPECT_FALSE(IsHooked(hooked));
	EXPECT_EQ(Point(hooked.position.tile), Point(34, 30));
}

TEST_F(Poe2ButchersRoadTest, ARareComesOneTileAndSlows)
{
	WalkRoad(false);
	const Monster &corpse = PlaceCulled(0, { 31, 31 });
	Monster &rare = PlaceMonster(1, { 35, 30 }, MonsterRarity::Rare);
	MeleeCull(corpse);
	EXPECT_FALSE(IsHooked(rare));
	EXPECT_EQ(Point(rare.position.tile), Point(34, 30));
	EXPECT_EQ(dMonster[34][30], 2);
	EXPECT_EQ(dMonster[35][30], 0);
	EXPECT_EQ(HookSlowPercent(rare), HookRareSlowPercent);
	Ticks(HookRareSlowTicks - 1);
	EXPECT_EQ(HookSlowPercent(rare), HookRareSlowPercent);
	Ticks(1);
	EXPECT_EQ(HookSlowPercent(rare), 0);
}

TEST_F(Poe2ButchersRoadTest, AUniqueFlinchesWhereItStands)
{
	WalkRoad(false);
	const Monster &corpse = PlaceCulled(0, { 31, 31 });
	Monster &boss = PlaceMonster(1, { 35, 30 });
	boss.uniqueType = UniqueMonsterType::Garbud;
	ASSERT_TRUE(boss.isUnique());
	MeleeCull(corpse);
	EXPECT_FALSE(IsHooked(boss));
	EXPECT_EQ(Point(boss.position.tile), Point(35, 30));
	EXPECT_EQ(boss.mode, MonsterMode::HitRecovery);
	EXPECT_TRUE(IsFlinching(boss));
	Ticks(HookFlinchTicks);
	EXPECT_FALSE(IsFlinching(boss));
}

TEST_F(Poe2ButchersRoadTest, TheCryWakesEveryEnemyNearTheHeroThroughWalls)
{
	WalkRoad(false);
	const Monster &corpse = PlaceCulled(0, { 31, 31 });
	Monster &asleep = PlaceMonster(1, { 30, 37 });
	const Monster &tooFar = PlaceMonster(2, { 30, 39 });
	Monster &minion = PlaceMonster(3, { 25, 30 });
	minion.flags |= MFLAG_GOLEM;
	asleep.flags |= MFLAG_NO_ENEMY;
	MeleeCull(corpse);
	EXPECT_EQ(asleep.activeForTicks, UINT8_MAX);
	EXPECT_EQ(asleep.enemy, MyPlayerId);
	EXPECT_EQ(Point(asleep.position.last), Hero);
	EXPECT_EQ(asleep.flags & MFLAG_NO_ENEMY, 0U);
	EXPECT_EQ(tooFar.activeForTicks, 0);
	EXPECT_EQ(minion.activeForTicks, 0);
	// Without the pact nothing is enraged.
	EXPECT_FALSE(IsEnraged(asleep));
	// The sound rests a while; the enemies answer every Cull.
	EXPECT_EQ(MyPlayer->poe2.uniqueState.butcherCryTicks, ButcherCryCooldownTicks);
	asleep.activeForTicks = 0;
	MeleeCull(corpse);
	EXPECT_EQ(asleep.activeForTicks, UINT8_MAX);
}

TEST_F(Poe2ButchersRoadTest, ThePactsCallReachesFurtherAndEnrages)
{
	WalkRoad(true);
	const Monster &corpse = PlaceCulled(0, { 31, 31 });
	Monster &answers = PlaceMonster(1, { 30, 45 });
	const Monster &tooFar = PlaceMonster(2, { 30, 47 });
	Monster &hooked = PlaceMonster(3, { 35, 30 });
	MeleeCull(corpse);
	EXPECT_EQ(answers.activeForTicks, UINT8_MAX);
	EXPECT_EQ(tooFar.activeForTicks, 0);
	EXPECT_TRUE(IsEnraged(answers));
	EXPECT_TRUE(IsHooked(hooked));
	EXPECT_TRUE(IsEnraged(hooked));
	EXPECT_FALSE(IsEnraged(tooFar));
	// 20% faster: one extra turn in five.
	int extra = 0;
	for (int i = 0; i < 10; i++)
		extra += EnragedExtraTick(answers) ? 1 : 0;
	EXPECT_EQ(extra, 2);
	// A new answer resets the rage; it never stacks.
	Ticks(EnrageTicks - 1);
	EXPECT_TRUE(IsEnraged(answers));
	MeleeCull(corpse);
	EXPECT_EQ(answers.poe2.enrageTicks, EnrageTicks);
	Ticks(EnrageTicks);
	EXPECT_FALSE(IsEnraged(answers));
	EXPECT_FALSE(EnragedExtraTick(answers));
}

TEST_F(Poe2ButchersRoadTest, ThePactHooksBehindTheArrowsMark)
{
	const Monster &corpse = PlaceCulled(0, { 34, 30 });
	Monster &beside = PlaceMonster(1, { 33, 31 });
	Monster &behind = PlaceMonster(2, { 37, 30 });
	// The toll alone: the nearest.
	WalkRoad(false);
	ArrowCull(corpse, Hero);
	EXPECT_TRUE(IsHooked(beside));
	EXPECT_FALSE(IsHooked(behind));
	beside.poe2.hookTicks = 0;
	// The pact: the one behind the mark, along the arrow's line.
	WalkRoad(true);
	ArrowCull(corpse, Hero);
	EXPECT_FALSE(IsHooked(beside));
	EXPECT_TRUE(IsHooked(behind));
	behind.poe2.hookTicks = 0;
	// A melee Cull with the pact still takes the nearest.
	MeleeCull(corpse);
	EXPECT_TRUE(IsHooked(beside));
	beside.poe2.hookTicks = 0;
	// Nothing behind: the nearest, as with the toll.
	dMonster[37][30] = 0;
	behind.position.tile = behind.position.old = behind.position.future = { 30, 34 };
	dMonster[30][34] = 3;
	ArrowCull(corpse, Hero);
	EXPECT_TRUE(IsHooked(beside));
	EXPECT_FALSE(IsHooked(behind));
}

TEST_F(Poe2ButchersRoadTest, TheDragIsDrawnSmoothlyBetweenTicks)
{
	WalkRoad(false);
	const Monster &corpse = PlaceCulled(0, { 31, 31 });
	Monster &near = PlaceMonster(1, { 35, 30 });
	MeleeCull(corpse);
	EXPECT_EQ(HookDrawOffset(near), Displacement(0, 0));
	Ticks(1);
	const Point stepped = near.position.tile;
	ASSERT_NE(stepped, Point(35, 30));
	// At the start of the tick it is drawn where the step began, and on its tile by the next.
	ProgressToNextGameTick = 0;
	const int tiles = 35 - stepped.x;
	EXPECT_EQ(HookDrawOffset(near), Displacement(tiles * 32, tiles * 16));
	ProgressToNextGameTick = AnimationInfo::baseValueFraction;
	EXPECT_EQ(HookDrawOffset(near), Displacement(0, 0));
	Ticks(HookDragTicks);
	ProgressToNextGameTick = 0;
	EXPECT_EQ(HookDrawOffset(near), Displacement(0, 0));
}

TEST_F(Poe2ButchersRoadTest, ProfaneRoadTextNamesNoKing)
{
	for (const PassiveStat stat : { PassiveStat::DeadDoNotRest, PassiveStat::KingsDeadPact }) {
		for (const PassiveStatLine &line : PassiveStatLines(stat, 1)) {
			for (const char *word : { "Leoric", "King", "curse", "Curse", "risen", "rise", "the dead" })
				EXPECT_EQ(line.text.find(word), std::string::npos) << line.text;
		}
	}
}

} // namespace
} // namespace devilution
