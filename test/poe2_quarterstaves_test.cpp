/**
 * @file poe2_quarterstaves_test.cpp
 *
 * The caster weapons rework's quarterstaves (caster-weapons-plan-v2.md 6.11, step S11): their own crit, block from the
 * Barrier, Guardian and Aegis staves for any class, and the Hefty and Skullcrusher staves' Daze.
 */
#include <string_view>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/item_stats.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/unique_effects.hpp"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2QuarterstavesTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		MyPlayerId = 0;
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
		LoadPassiveTreeData();
		LoadModifierData();
		LoadMissileData();
		LoadMonsterData();
		LevelMonsterTypes[0].type = MT_NZOMBIE;
		Missiles.clear();
		leveltype = DTYPE_CATHEDRAL;
		SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);

		Player &player = *MyPlayer;
		player._pClass = HeroClass::Warrior;
		player.setCharacterLevel(85);
		player._pBaseStr = player._pStrength = 200;
		player._pBaseMag = player._pMagic = 200;
		player._pBaseDex = player._pDexterity = 200;
		player._pBaseVit = player._pVitality = 50;
		player.position.tile = { 40, 40 };
		player._pdir = Direction::East;
	}

	void TearDown() override
	{
		ActiveMonsterCount = 0;
		Missiles.clear();
		leveltype = DTYPE_TOWN;
		gbIsPoe2 = false;
		LoadModifierData();
		UnloadModArchives();
	}

	static _item_indexes Row(std::string_view name)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == name)
				return static_cast<_item_indexes>(i);
		}
		ADD_FAILURE() << name;
		return IDI_GOLD;
	}

	static Item &Hold(std::string_view name, uint32_t seed = 1)
	{
		Player &player = *MyPlayer;
		Item &item = player.InvBody[INVLOC_HAND_LEFT];
		item = {};
		SetLootRarity(item, ITEM_QUALITY_NORMAL);
		SetRndSeed(seed);
		SetupAllItems(player, item, Row(name), AdvanceRndSeed(), 30, 1, /*onlygood=*/false, /*pregen=*/false, 0, /*forceNotUnique=*/true);
		item._iStatFlag = true;
		CalcPlrInv(player, false);
		return item;
	}

	/** @brief A normal monster with plenty of life. */
	static Monster &PlaceMonster(size_t slot, Point tile, UniqueMonsterType unique = UniqueMonsterType::None)
	{
		Monster &monster = Monsters[slot];
		monster = {};
		monster.levelType = 0;
		monster.uniqueType = unique;
		monster.talkMsg = TEXT_NONE;
		for (uint32_t seed = 1; seed < 100000; seed++) {
			monster.rndItemSeed = seed;
			if (unique != UniqueMonsterType::None || GetMonsterAffixes(monster).rarity == MonsterRarity::Normal)
				break;
		}
		monster.mode = MonsterMode::Petrified;
		monster.position.tile = tile;
		monster.maxHitPoints = monster.hitPoints = Fixed26_6::fromInt(100000);
		ActiveMonsters[ActiveMonsterCount] = static_cast<unsigned>(slot);
		ActiveMonsterCount++;
		return monster;
	}
};

TEST_F(Poe2QuarterstavesTest, NewStavesHaveTheirOwnCrit)
{
	Player &player = *MyPlayer;
	EXPECT_EQ(AttackCritChance(player), BaseAttackCritPercent);
	// The older staves keep the default; the new ones have POE2's halved: 10% is 5%, 12% is 6%.
	Hold("Dreaming Quarterstaff");
	EXPECT_EQ(AttackCritChance(player), 5);
	Hold("Fine Wrapped Quarterstaff");
	EXPECT_EQ(AttackCritChance(player), 5);
	Hold("Wyrm Quarterstaff");
	EXPECT_EQ(AttackCritChance(player), 5);
	Hold("Sinister Quarterstaff");
	EXPECT_EQ(AttackCritChance(player), 6);
	// Only a staff the hero can use counts.
	player.InvBody[INVLOC_HAND_LEFT]._iStatFlag = false;
	EXPECT_EQ(AttackCritChance(player), BaseAttackCritPercent);
}

TEST_F(Poe2QuarterstavesTest, BarrierStavesBlockForAnyClass)
{
	Player &player = *MyPlayer;
	Hold("Fine Wrapped Quarterstaff");
	EXPECT_FALSE(player._pBlockFlag);
	EXPECT_EQ(player.poe2.shieldBlockChance, 0);
	for (const std::string_view name : { "Barrier Quarterstaff", "Guardian Quarterstaff", "Fine Aegis Quarterstaff" }) {
		for (uint32_t seed = 1; seed <= 5; seed++) {
			const Item &staff = Hold(name, seed);
			const int block = staff._iPoe2Stats[static_cast<size_t>(Poe2ItemStat::BlockChance)];
			EXPECT_GE(block, 12) << name;
			EXPECT_LE(block, 18) << name;
			EXPECT_TRUE(player._pBlockFlag) << name;
			EXPECT_EQ(player.poe2.shieldBlockChance, block) << name;
			EXPECT_GE(player.GetBlockChance(), block) << name;
		}
	}
	// One the hero can't use doesn't block.
	player._pBaseDex = player._pDexterity = 10;
	CalcPlrInv(player, false);
	ASSERT_FALSE(player.InvBody[INVLOC_HAND_LEFT]._iStatFlag);
	EXPECT_FALSE(player._pBlockFlag);
	EXPECT_EQ(player.poe2.shieldBlockChance, 0);
}

TEST_F(Poe2QuarterstavesTest, DazeDoublesStunBuildupButNotOnUniques)
{
	Player &player = *MyPlayer;
	const Fixed26_6 hit = Fixed26_6::fromInt(50);
	const auto buildup = [&](int dazeChance, UniqueMonsterType unique) {
		Item &staff = Hold("Hefty Quarterstaff");
		staff._iPoe2Stats[static_cast<size_t>(Poe2ItemStat::DazeChance)] = static_cast<int16_t>(dazeChance);
		ActiveMonsterCount = 0;
		Monster &monster = PlaceMonster(0, { 41, 40 }, unique);
		ApplyPlayerAttackDamage(player, monster, hit, false, false);
		return monster.poe2.stunBuildup;
	};
	const int32_t plain = buildup(0, UniqueMonsterType::None);
	ASSERT_GT(plain, 0);
	EXPECT_NEAR(buildup(100, UniqueMonsterType::None), plain * 2, 1);
	// Unique monsters are never Dazed.
	const int32_t uniquePlain = buildup(0, UniqueMonsterType::SkeletonKing);
	ASSERT_GT(uniquePlain, 0);
	EXPECT_EQ(buildup(100, UniqueMonsterType::SkeletonKing), uniquePlain);

	// The Hefty staff's own chance, 20-50%: some hits Daze and some don't.
	Item &staff = Hold("Hefty Quarterstaff");
	const int chance = staff._iPoe2Stats[static_cast<size_t>(Poe2ItemStat::DazeChance)];
	EXPECT_GE(chance, 20);
	EXPECT_LE(chance, 50);
	int dazed = 0;
	SetRndSeed(7);
	for (int i = 0; i < 200; i++) {
		ActiveMonsterCount = 0;
		Monster &monster = PlaceMonster(0, { 41, 40 });
		ApplyPlayerAttackDamage(player, monster, hit, false, false);
		if (monster.poe2.stunBuildup > plain * 3 / 2)
			dazed++;
		else
			EXPECT_EQ(monster.poe2.stunBuildup, plain);
	}
	EXPECT_GT(dazed, 200 * (chance - 15) / 100);
	EXPECT_LT(dazed, 200 * (chance + 15) / 100);

	// Arrows and other ranged hits never Daze.
	ActiveMonsterCount = 0;
	staff._iPoe2Stats[static_cast<size_t>(Poe2ItemStat::DazeChance)] = 100;
	Monster &monster = PlaceMonster(0, { 41, 40 });
	ApplyPlayerAttackDamage(player, monster, hit, false, true);
	EXPECT_EQ(monster.poe2.stunBuildup, plain);
}

} // namespace
} // namespace devilution
