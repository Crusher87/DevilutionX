/**
 * @file poe2_minion_travel_test.cpp
 *
 * POE2 Mashup: the hero's minions go with the hero between levels (Curtis 2026-10-29: "they should follow me like
 * POE2"), keeping their kind and life, wait in town, where the game keeps no monsters, and are kept in the hero's save.
 */
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/minion_gems.hpp"
#include "poe2/minion_travel.hpp"
#include "poe2/spirit.hpp"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2MinionTravelTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayerId = 0;
		MyPlayer = &Players[0];
		*MyPlayer = {};
		MyPlayer->poe2.clear();
		MyPlayer->_pStrength = 200;
		MyPlayer->_pMagic = 200;
		MyPlayer->_pDexterity = 200;
		MyPlayer->poe2.spiritSkulls = 7;
		MyPlayer->poe2.spiritGemsOn = 0xFFFF;
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
		for (size_t i = 0; i < MinionGemTable().size(); i++)
			LevelMonsterTypes[i + 1].type = MinionGemTable()[i].type;
		leveltype = DTYPE_CATHEDRAL;
		MyPlayer->setCharacterLevel(30);
		MyPlayer->position.tile = { 40, 40 };
		MyPlayer->_pMaxHP = MyPlayer->_pHitPoints = Fixed26_6::fromInt(1000);
		MyPlayer->_pMaxHPBase = MyPlayer->_pHPBase = Fixed26_6::fromInt(1000);
		ActiveMonsterCount = 0;
		memset(dMonster, 0, sizeof(dMonster));
		Missiles.clear();
		ClearMinionGems();
		SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
	}

	void TearDown() override
	{
		for (size_t i = 0; i < MaxMonsters; i++)
			Monsters[i] = {};
		ActiveMonsterCount = 0;
		memset(dMonster, 0, sizeof(dMonster));
		Missiles.clear();
		ClearMinionGems();
		gbIsPoe2 = false;
		leveltype = DTYPE_TOWN;
		UnloadModArchives();
	}

	static void Socket(PersistentSkill skill, int gemLevel = 1)
	{
		MyPlayer->poe2.skillSlots[0] = SkillSlot::ForPersistent(skill);
		MyPlayer->poe2.slotLevels[0] = static_cast<uint8_t>(gemLevel);
		ASSERT_TRUE(IsPersistentSkillActive(*MyPlayer, skill)) << PersistentSkillName(skill);
	}

	static Monster &Place(size_t slot, Point tile, int levelType, int life)
	{
		Monster &monster = Monsters[slot];
		monster = {};
		monster.levelType = levelType;
		monster.talkMsg = TEXT_NONE;
		monster.position.tile = monster.position.old = monster.position.future = tile;
		monster.maxHitPoints = monster.hitPoints = Fixed26_6::fromInt(life);
		dMonster[tile.x][tile.y] = static_cast<int16_t>(slot + 1);
		ActiveMonsters[ActiveMonsterCount] = static_cast<unsigned>(slot);
		ActiveMonsterCount++;
		return monster;
	}

	/** @brief One of the hero's minions of the gem. */
	static Monster &PlaceMinion(size_t slot, Point tile, PersistentSkill gem, int life = 100)
	{
		int levelType = 0;
		for (size_t i = 0; i < MinionGemTable().size(); i++) {
			if (MinionGemTable()[i].skill == gem)
				levelType = static_cast<int>(i) + 1;
		}
		Monster &minion = Place(slot, tile, levelType, life);
		minion.flags |= MFLAG_GOLEM;
		minion.goalVar3 = static_cast<int8_t>(MyPlayerId);
		return minion;
	}

	static bool IsActive(size_t slot)
	{
		for (size_t i = 0; i < ActiveMonsterCount; i++) {
			if (ActiveMonsters[i] == slot)
				return true;
		}
		return false;
	}
};

TEST_F(Poe2MinionTravelTest, TheHerosSkeletonsComeAlongWithTheirLife)
{
	Player &player = *MyPlayer;
	Socket(PersistentSkill::SkeletalWarriors);
	Monster &warrior = PlaceMinion(4, { 41, 40 }, PersistentSkill::SkeletalWarriors, 100);
	warrior.hitPoints = Fixed26_6::fromInt(40);
	Monster &enemy = Place(5, { 44, 40 }, 0, 1000);
	// A raging spirit lasts a few seconds, so it stays behind.
	Monster &spirit = PlaceMinion(6, { 42, 40 }, PersistentSkill::RagingSpirits);
	spirit.poe2.minionLifeTicks = 50;
	// So does an enemy that happens to be a skeleton.
	Place(7, { 45, 40 }, 1, 100);

	TakeMinionsAlong(player);
	ASSERT_EQ(player.poe2.travellingMinionCount, 1);
	const TravellingMinion &going = player.poe2.travellingMinions[0];
	EXPECT_EQ(going.kind, MinionTravelKind::Gem);
	EXPECT_EQ(going.type, static_cast<uint16_t>(FindMinionGem(PersistentSkill::SkeletalWarriors)->type));
	EXPECT_EQ(going.life, Fixed26_6::fromInt(40).raw());
	EXPECT_EQ(going.maxLife, Fixed26_6::fromInt(100).raw());
	// Off the level alive, without a death; the rest stay.
	EXPECT_FALSE(IsActive(4));
	EXPECT_EQ(dMonster[41][40], 0);
	EXPECT_TRUE(IsActive(5));
	EXPECT_TRUE(IsActive(6));
	EXPECT_TRUE(IsActive(7));
	EXPECT_EQ(ActiveMonsterCount, 3U);
	EXPECT_FALSE(enemy.hasNoLife());
}

TEST_F(Poe2MinionTravelTest, InTownTheyWaitForTheNextDungeonLevel)
{
	Player &player = *MyPlayer;
	Socket(PersistentSkill::SkeletalWarriors);
	PlaceMinion(4, { 41, 40 }, PersistentSkill::SkeletalWarriors, 100);
	TakeMinionsAlong(player);
	ASSERT_EQ(player.poe2.travellingMinionCount, 1);

	// The game keeps no monsters in town: arriving there, they wait.
	leveltype = DTYPE_TOWN;
	ActiveMonsterCount = 0;
	BringMinionsAlong(player);
	EXPECT_EQ(player.poe2.travellingMinionCount, 1);
	EXPECT_EQ(ActiveMonsterCount, 0U);
	// Leaving town takes nothing more and loses none.
	TakeMinionsAlong(player);
	EXPECT_EQ(player.poe2.travellingMinionCount, 1);
}

TEST_F(Poe2MinionTravelTest, OnesWhoseGemWasTakenOffStayBehind)
{
	Player &player = *MyPlayer;
	Poe2PlayerData &data = player.poe2;
	// No gem, golem skill or Revive is on.
	data.travellingMinions[0].kind = MinionTravelKind::Gem;
	data.travellingMinions[0].type = static_cast<uint16_t>(FindMinionGem(PersistentSkill::SkeletalWarriors)->type);
	data.travellingMinions[1].kind = MinionTravelKind::Golem;
	data.travellingMinions[1].type = MT_GOLEM;
	data.travellingMinions[2].kind = MinionTravelKind::Revived;
	data.travellingMinions[2].type = MT_NZOMBIE;
	data.travellingMinionCount = 3;
	BringMinionsAlong(player);
	EXPECT_EQ(data.travellingMinionCount, 0);
	EXPECT_EQ(ActiveMonsterCount, 0U);
}

TEST_F(Poe2MinionTravelTest, TheHerosSaveKeepsTheWaitingMinions)
{
	Poe2PlayerData data;
	data.clear();
	data.travellingMinions[0] = { MinionTravelKind::Gem, static_cast<uint16_t>(SkeletonMinionType), 40 * 64, 100 * 64, 0, 0, 0, 0 };
	data.travellingMinions[1] = { MinionTravelKind::Revived, static_cast<uint16_t>(MT_NZOMBIE), 1234, 5678, 3, 9, 20, 0x0102 };
	data.travellingMinionCount = 2;
	const std::vector<uint8_t> bytes = PackTravellingMinions(data);
	EXPECT_EQ(bytes.size(), 1 + (2 * TravellingMinionSaveSize));

	Poe2PlayerData loaded;
	loaded.clear();
	ASSERT_TRUE(UnpackTravellingMinions(loaded, bytes));
	ASSERT_EQ(loaded.travellingMinionCount, 2);
	for (size_t i = 0; i < 2; i++) {
		const TravellingMinion &a = data.travellingMinions[i];
		const TravellingMinion &b = loaded.travellingMinions[i];
		EXPECT_EQ(a.kind, b.kind);
		EXPECT_EQ(a.type, b.type);
		EXPECT_EQ(a.life, b.life);
		EXPECT_EQ(a.maxLife, b.maxLife);
		EXPECT_EQ(a.minDamage, b.minDamage);
		EXPECT_EQ(a.maxDamage, b.maxDamage);
		EXPECT_EQ(a.armorClass, b.armorClass);
		EXPECT_EQ(a.resistance, b.resistance);
	}

	// A cut-off list reads as none; one of a kind this build doesn't know is skipped.
	EXPECT_FALSE(UnpackTravellingMinions(loaded, std::vector<uint8_t>(bytes.begin(), bytes.end() - 1)));
	EXPECT_EQ(loaded.travellingMinionCount, 0);
	std::vector<uint8_t> unknown = bytes;
	unknown[1] = 9;
	ASSERT_TRUE(UnpackTravellingMinions(loaded, unknown));
	ASSERT_EQ(loaded.travellingMinionCount, 1);
	EXPECT_EQ(loaded.travellingMinions[0].kind, MinionTravelKind::Revived);
	// An empty list is a single count byte.
	data.travellingMinionCount = 0;
	EXPECT_EQ(PackTravellingMinions(data), std::vector<uint8_t> { 0 });
}

} // namespace
} // namespace devilution
