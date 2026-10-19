#include <gtest/gtest.h>

#include <set>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/minion_gems.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/spirit.hpp"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2MinionGemsTest : public ::testing::Test {
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
		for (size_t i = 0; i < MinionCountSkills.size(); i++)
			LevelMonsterTypes[i + 1].type = FindMinionGem(MinionCountSkills[i])->type;
		leveltype = DTYPE_CATHEDRAL;
		MyPlayer->setCharacterLevel(30);
		MyPlayer->position.tile = { 40, 40 };
		ActiveMonsterCount = 0;
		// Damage to monsters goes out as a network message.
		SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
	}

	void TearDown() override
	{
		ActiveMonsterCount = 0;
		gbIsPoe2 = false;
		leveltype = DTYPE_TOWN;
		UnloadModArchives();
	}

	static Monster &Place(size_t slot, Point tile, int levelType, int life)
	{
		Monster &monster = Monsters[slot];
		monster = {};
		monster.levelType = levelType;
		monster.talkMsg = TEXT_NONE;
		monster.position.tile = tile;
		monster.maxHitPoints = monster.hitPoints = Fixed26_6::fromInt(life);
		ActiveMonsters[ActiveMonsterCount] = static_cast<unsigned>(slot);
		ActiveMonsterCount++;
		return monster;
	}

	/** @brief One of the hero's skeletons of the gem. */
	static Monster &PlaceMinion(size_t slot, Point tile, PersistentSkill gem, int life = 100)
	{
		Monster &minion = Place(slot, tile, MinionCountSkillIndex(gem) + 1, life);
		minion.flags |= MFLAG_GOLEM;
		minion.goalVar3 = static_cast<int8_t>(MyPlayerId);
		return minion;
	}
};

TEST_F(Poe2MinionGemsTest, EachSkeletonGemRaisesItsOwnKind)
{
	std::set<_monster_id> types;
	for (const PersistentSkill skill : MinionCountSkills) {
		const MinionGemData *gem = FindMinionGem(skill);
		ASSERT_NE(gem, nullptr) << static_cast<int>(skill);
		EXPECT_EQ(FindMinionGemByType(gem->type), gem);
		EXPECT_TRUE(IsMinionGemType(gem->type));
		types.insert(gem->type);
		EXPECT_FALSE(PersistentSkillName(skill).empty());
		// The gem item exists and grants the skill.
		Item item;
		ASSERT_TRUE(CreateSkillGemItem(item, SkillSlot::ForPersistent(skill))) << PersistentSkillName(skill);
		EXPECT_EQ(SkillForGem(item), SkillSlot::ForPersistent(skill));
	}
	EXPECT_EQ(types.size(), MinionCountSkills.size());
	// The two older gems keep their skeletons, so saved games keep them too.
	EXPECT_EQ(FindMinionGem(PersistentSkill::SkeletalWarriors)->type, SkeletonMinionType);
	EXPECT_EQ(FindMinionGem(PersistentSkill::SkeletalMages)->type, SkeletonMageMinionType);
	EXPECT_EQ(FindMinionGem(PersistentSkill::HeraldOfAsh), nullptr);
}

TEST_F(Poe2MinionGemsTest, SpiritPerMinionFollowsPoe2)
{
	EXPECT_EQ(BaseSpiritPerMinion(PersistentSkill::SkeletalSnipers, 1), 90);
	EXPECT_EQ(BaseSpiritPerMinion(PersistentSkill::SkeletalSnipers, 20), 30);
	EXPECT_EQ(BaseSpiritPerMinion(PersistentSkill::SkeletalFrostMages, 1), 120);
	EXPECT_EQ(BaseSpiritPerMinion(PersistentSkill::SkeletalStormMages, 7), 45);
	EXPECT_EQ(BaseSpiritPerMinion(PersistentSkill::SkeletalReavers, 4), 41);
	EXPECT_EQ(BaseSpiritPerMinion(PersistentSkill::SkeletalBrutes, 1), 165);
	EXPECT_EQ(BaseSpiritPerMinion(PersistentSkill::SkeletalClerics, 20), 20);
	EXPECT_EQ(BaseSpiritPerMinion(PersistentSkill::SkeletalMages, 1), 90);
	// Only the Warriors come free.
	EXPECT_EQ(FreeMinions(PersistentSkill::SkeletalWarriors), 2);
	EXPECT_EQ(FreeMinions(PersistentSkill::SkeletalBrutes), 0);
	EXPECT_EQ(StartingMinionCount(PersistentSkill::SkeletalClerics), 1);
	EXPECT_EQ(MinionCountSkillIndex(PersistentSkill::SkeletalClerics), 7);
}

TEST_F(Poe2MinionGemsTest, OnlyTheHerosRaisedSkeletonsBelongToAGem)
{
	Monster &sniper = PlaceMinion(0, { 41, 40 }, PersistentSkill::SkeletalSnipers);
	ASSERT_NE(MinionGemOf(sniper), nullptr);
	EXPECT_EQ(MinionGemOf(sniper)->skill, PersistentSkill::SkeletalSnipers);
	// A skeleton archer of the dungeon is an enemy.
	Monster &enemy = Place(1, { 45, 40 }, MinionCountSkillIndex(PersistentSkill::SkeletalSnipers) + 1, 100);
	EXPECT_EQ(MinionGemOf(enemy), nullptr);
	// One brought back by Revive keeps its own strength.
	sniper.poe2.revived = true;
	EXPECT_EQ(MinionGemOf(sniper), nullptr);
}

TEST_F(Poe2MinionGemsTest, ShotsFlyAsTheirKind)
{
	EXPECT_EQ(MinionShotMissile(FindMinionGem(PersistentSkill::SkeletalMages)->shot), MissileID::Firebolt);
	EXPECT_EQ(MinionShotMissile(FindMinionGem(PersistentSkill::SkeletalSnipers)->shot), MissileID::Arrow);
	EXPECT_EQ(MinionShotMissile(FindMinionGem(PersistentSkill::SkeletalFrostMages)->shot), MissileID::BlueFlare);
	EXPECT_EQ(MinionShotMissile(FindMinionGem(PersistentSkill::SkeletalStormMages)->shot), MissileID::ChargedBolt);
	EXPECT_EQ(FindMinionGem(PersistentSkill::SkeletalReavers)->shot, MinionShot::None);
	// Clerics don't fight.
	EXPECT_EQ(FindMinionGem(PersistentSkill::SkeletalClerics)->damagePercent, 0);
}

TEST_F(Poe2MinionGemsTest, ReaversBuildRageThatFades)
{
	Player &player = *MyPlayer;
	Monster &reaver = PlaceMinion(0, { 41, 40 }, PersistentSkill::SkeletalReavers);
	Monster &target = Place(1, { 42, 40 }, 0, 1000);
	EXPECT_EQ(MinionGemDamagePercent(reaver), 70);
	for (int i = 0; i < ReaverMaxRage + 3; i++)
		OnMinionGemHit(player, reaver, target, Fixed26_6::fromInt(5));
	EXPECT_EQ(reaver.poe2.minionRage, ReaverMaxRage);
	EXPECT_EQ(MinionGemDamagePercent(reaver), 70 * (100 + (ReaverRagePercent * ReaverMaxRage)) / 100);

	// It holds a while after the last blow, then fades a point at a time.
	for (int i = 0; i < ReaverRageTicks; i++)
		ProcessMinionGems(player);
	EXPECT_EQ(reaver.poe2.minionRage, ReaverMaxRage);
	ProcessMinionGems(player);
	EXPECT_EQ(reaver.poe2.minionRage, ReaverMaxRage - 1);
	for (int i = 0; i < ReaverRageFadeTicks * ReaverMaxRage; i++)
		ProcessMinionGems(player);
	EXPECT_EQ(reaver.poe2.minionRage, 0);
}

TEST_F(Poe2MinionGemsTest, BrutesHitHardAndStun)
{
	Player &player = *MyPlayer;
	Monster &brute = PlaceMinion(0, { 41, 40 }, PersistentSkill::SkeletalBrutes);
	Monster &target = Place(1, { 42, 40 }, 0, 1000);
	EXPECT_GT(MinionGemDamagePercent(brute), 200);
	EXPECT_EQ(target.poe2.stunBuildup, 0);
	OnMinionGemHit(player, brute, target, Fixed26_6::fromInt(50));
	EXPECT_GT(target.poe2.stunBuildup, 0);

	// A Warrior's blows don't build stun.
	Monster &warrior = PlaceMinion(2, { 43, 41 }, PersistentSkill::SkeletalWarriors);
	Monster &other = Place(3, { 44, 41 }, 0, 1000);
	OnMinionGemHit(player, warrior, other, Fixed26_6::fromInt(50));
	EXPECT_EQ(other.poe2.stunBuildup, 0);
	EXPECT_EQ(MinionGemDamagePercent(warrior), 100);
}

TEST_F(Poe2MinionGemsTest, ClericsHealTheMostHurtMinionNearby)
{
	Player &player = *MyPlayer;
	player.poe2.skillSlots[0] = SkillSlot::ForPersistent(PersistentSkill::SkeletalClerics);
	EXPECT_FALSE(ClericStands(player));
	Monster &cleric = PlaceMinion(0, { 41, 40 }, PersistentSkill::SkeletalClerics);
	EXPECT_TRUE(ClericStands(player));
	Monster &scratched = PlaceMinion(1, { 42, 40 }, PersistentSkill::SkeletalWarriors);
	scratched.hitPoints = Fixed26_6::fromInt(95);
	Monster &hurt = PlaceMinion(2, { 42, 41 }, PersistentSkill::SkeletalWarriors);
	hurt.hitPoints = Fixed26_6::fromInt(40);
	Monster &far = PlaceMinion(3, { 41 + ClericRadius + 2, 40 }, PersistentSkill::SkeletalWarriors);
	far.hitPoints = Fixed26_6::fromInt(10);

	ProcessMinionGems(player);
	EXPECT_EQ(hurt.hitPoints.whole(), 40 + ClericHealPercent(1));
	EXPECT_EQ(scratched.hitPoints.whole(), 95);
	EXPECT_EQ(far.hitPoints.whole(), 10);
	EXPECT_EQ(cleric.poe2.clericTicks, ClericHealTicks);

	// Then it waits before the next heal.
	ProcessMinionGems(player);
	EXPECT_EQ(hurt.hitPoints.whole(), 40 + ClericHealPercent(1));
	for (int i = 0; i < ClericHealTicks; i++)
		ProcessMinionGems(player);
	EXPECT_EQ(hurt.hitPoints.whole(), 40 + (2 * ClericHealPercent(1)));
	EXPECT_GT(ClericHealPercent(20), ClericHealPercent(1));
}

TEST_F(Poe2MinionGemsTest, SlowKindsWaitBetweenAttacks)
{
	Player &player = *MyPlayer;
	Monster &sniper = PlaceMinion(0, { 41, 40 }, PersistentSkill::SkeletalSnipers);
	sniper.poe2.minionAttackWait = FindMinionGem(PersistentSkill::SkeletalSnipers)->attackTicks;
	ASSERT_GT(sniper.poe2.minionAttackWait, 0);
	for (int i = 0; i < FindMinionGem(PersistentSkill::SkeletalSnipers)->attackTicks; i++)
		ProcessMinionGems(player);
	EXPECT_EQ(sniper.poe2.minionAttackWait, 0);
}

} // namespace
} // namespace devilution
