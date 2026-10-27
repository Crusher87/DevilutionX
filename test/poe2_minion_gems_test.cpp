#include <gtest/gtest.h>

#include <set>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/area_levels.hpp"
#include "poe2/minion_gems.hpp"
#include "poe2/monster_levels.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/skill_resources.hpp"
#include "poe2/spirit.hpp"
#include "poe2/spirit_skills.hpp"
#include "poe2/support_table.hpp"
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
		MyPlayer->_pStrength = 200;
		MyPlayer->_pMagic = 200;
		MyPlayer->_pDexterity = 200;
		// Every Spirit skull broken: 100 Spirit.
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
		Missiles.clear();
		ClearMinionGems();
		// Damage to monsters goes out as a network message.
		SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
	}

	void TearDown() override
	{
		ActiveMonsterCount = 0;
		Missiles.clear();
		ClearMinionGems();
		ClearSpiritSkills();
		gbIsPoe2 = false;
		leveltype = DTYPE_TOWN;
		currlevel = 0;
		sgGameInitInfo.nDifficulty = DIFF_NORMAL;
		// Clears the area and monster level tables some tests load.
		LoadAreaLevels();
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
		monster.position.tile = tile;
		monster.maxHitPoints = monster.hitPoints = Fixed26_6::fromInt(life);
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

TEST_F(Poe2MinionGemsTest, TheOtherMinionGemsHaveTheirOwnKinds)
{
	EXPECT_EQ(FindMinionGem(PersistentSkill::WolfPack)->type, MT_WSCAV);
	EXPECT_EQ(FindMinionGem(PersistentSkill::RagingSpirits)->type, MT_FIREBAT);
	EXPECT_EQ(FindMinionGem(PersistentSkill::LingeringIllusion)->type, MT_ILLWEAV);
	// Swarms aren't monsters, so nothing can hit them.
	EXPECT_EQ(FindMinionGem(PersistentSkill::RavenousSwarm), nullptr);
	std::set<_monster_id> types;
	for (const MinionGemData &gem : MinionGemTable())
		types.insert(gem.type);
	EXPECT_EQ(types.size(), MinionGemTable().size());
	for (const PersistentSkill skill : { PersistentSkill::WolfPack, PersistentSkill::RagingSpirits, PersistentSkill::RavenousSwarm, PersistentSkill::Sacrifice, PersistentSkill::LingeringIllusion }) {
		EXPECT_FALSE(PersistentSkillName(skill).empty());
		EXPECT_FALSE(PersistentSkillDescription(skill).empty());
		EXPECT_FALSE(IsMinionCountSkill(skill));
		Item item;
		ASSERT_TRUE(CreateSkillGemItem(item, SkillSlot::ForPersistent(skill))) << PersistentSkillName(skill);
		EXPECT_EQ(SkillForGem(item), SkillSlot::ForPersistent(skill));
	}
	// POE2's reservations: the pack and Sacrifice 60, the others 30.
	EXPECT_EQ(SpiritReservation(PersistentSkill::WolfPack), 60);
	EXPECT_EQ(SpiritReservation(PersistentSkill::Sacrifice), 60);
	EXPECT_EQ(SpiritReservation(PersistentSkill::RagingSpirits), 30);
	EXPECT_EQ(SpiritReservation(PersistentSkill::RavenousSwarm), 30);
	EXPECT_EQ(SpiritReservation(PersistentSkill::LingeringIllusion), 30);
	// Wolves rise again like the skeletons.
	EXPECT_EQ(RevivingMinionIndex(PersistentSkill::WolfPack), static_cast<int>(MinionCountSkills.size()));
	EXPECT_EQ(RevivingMinionIndex(PersistentSkill::RagingSpirits), -1);
}

TEST_F(Poe2MinionGemsTest, WolfPackKeepsAPackThatGrowsWithTheGem)
{
	Player &player = *MyPlayer;
	EXPECT_EQ(WolfPackSize(1), 3);
	EXPECT_EQ(WolfPackSize(20), 6);
	EXPECT_EQ(KeptMinionCount(player, PersistentSkill::WolfPack), 0);
	Socket(PersistentSkill::WolfPack, 20);
	EXPECT_EQ(KeptMinionCount(player, PersistentSkill::WolfPack), 6);
	Monster &wolf = PlaceMinion(0, { 41, 40 }, PersistentSkill::WolfPack);
	EXPECT_EQ(MinionGemDamagePercent(wolf), FindMinionGem(PersistentSkill::WolfPack)->damagePercent);
	EXPECT_EQ(MinionGemFirePercent(wolf), 0);
}

TEST_F(Poe2MinionGemsTest, RagingSpiritsComeFromTheHerosOwnFireSpells)
{
	Player &player = *MyPlayer;
	const uint8_t &budget = player.poe2.spiritState.ragingBudget;
	const SkillSlot fireball = SkillSlot::ForSpell(SpellID::Fireball);
	const uint32_t fireTags = SkillTags(fireball);
	Monster &enemy = Place(0, { 44, 40 }, 0, 1000);
	// Without the gem, nothing.
	OnRagingSpiritsCast(player, fireball, fireTags);
	EXPECT_EQ(budget, 0);
	Socket(PersistentSkill::RagingSpirits);
	// Nor from a lightning spell.
	OnRagingSpiritsCast(player, SkillSlot::ForSpell(SpellID::ChargedBolt), SkillTags(SkillSlot::ForSpell(SpellID::ChargedBolt)));
	EXPECT_EQ(budget, 0);
	OnRagingSpiritsCast(player, fireball, fireTags);
	EXPECT_EQ(budget, RagingSpiritsPerCast);
	// Hits of another skill, or not of fire, call none.
	OnRagingSpiritsHit(player, SkillSlot::ForSpell(SpellID::Firebolt), enemy, DamageType::Fire);
	OnRagingSpiritsHit(player, fireball, enemy, DamageType::Physical);
	EXPECT_EQ(budget, RagingSpiritsPerCast);
	// Two at most from a cast, however many it hits.
	for (int i = 0; i < 5; i++)
		OnRagingSpiritsHit(player, fireball, enemy, DamageType::Fire);
	EXPECT_EQ(budget, 0);
	ClearMinionGems();
	// A cast's hits have a few seconds to call theirs.
	OnRagingSpiritsCast(player, fireball, fireTags);
	for (int i = 0; i < RagingSpiritsCastTicks; i++)
		ProcessMinionGems(player);
	EXPECT_EQ(budget, 0);
	// Most of a spirit's blow is fire.
	Monster &spirit = PlaceMinion(1, { 41, 40 }, PersistentSkill::RagingSpirits);
	EXPECT_EQ(MinionGemFirePercent(spirit), RagingSpiritFirePercent);
}

TEST_F(Poe2MinionGemsTest, SwarmsLeaveForNearbyEnemiesBiteAndPoison)
{
	Player &player = *MyPlayer;
	Socket(PersistentSkill::RavenousSwarm, 20);
	EXPECT_EQ(SwarmTicks(20), 196);
	EXPECT_GT(SwarmBiteRaw(player, 20), SwarmBiteRaw(player, 1));
	// No enemy near: no swarm.
	Monster &far = Place(0, { 40 + SwarmReachTiles + 3, 40 }, 0, 10000);
	ProcessMinionGems(player);
	EXPECT_EQ(SwarmCount(player), 0);
	Monster &near = Place(1, { 42, 40 }, 0, 10000);
	ProcessMinionGems(player);
	ASSERT_EQ(SwarmCount(player), 1);
	EXPECT_EQ(player.poe2.spiritState.swarms[0].target, 1);
	// It bites at once and poisons; the enemy further off is left alone.
	EXPECT_LT(near.hitPoints, near.maxHitPoints);
	EXPECT_GT(near.poe2.poisonTicks, 0);
	EXPECT_GT(near.poe2.poisonDamagePerTick, 0);
	EXPECT_EQ(far.hitPoints, far.maxHitPoints);
	// The next swarm leaves 6 seconds later.
	for (int i = 0; i < SwarmEveryTicks - 1; i++)
		ProcessMinionGems(player);
	EXPECT_EQ(SwarmCount(player), 1);
	ProcessMinionGems(player);
	EXPECT_EQ(SwarmCount(player), 2);
	// Taking the gem out calls them back.
	player.poe2.skillSlots[0] = {};
	ProcessMinionGems(player);
	EXPECT_EQ(SwarmCount(player), 0);
}

TEST_F(Poe2MinionGemsTest, SacrificeUsesSkeletonsAsCorpsesAndSlowsRevives)
{
	Player &player = *MyPlayer;
	Monster &warrior = PlaceMinion(0, { 43, 40 }, PersistentSkill::SkeletalWarriors);
	PlaceMinion(1, { 41, 40 }, PersistentSkill::WolfPack);
	EXPECT_EQ(SacrificeMinionNear(player, { 40, 40 }, 5), nullptr);
	EXPECT_EQ(MinionReviveTicks(player), SkeletonReviveTicks);
	Socket(PersistentSkill::Sacrifice);
	// The wolf is nearer, but only the undead can be used.
	EXPECT_EQ(SacrificeMinionNear(player, { 40, 40 }, 5), &warrior);
	EXPECT_EQ(SacrificeMinionNear(player, { 40, 40 }, 2), nullptr);
	EXPECT_EQ(MinionReviveTicks(player), SkeletonReviveTicks * SacrificeRevivePercent(1) / 100);
	EXPECT_EQ(SacrificeRevivePercent(1), 159);
	EXPECT_EQ(SacrificeRevivePercent(20), 140);
}

TEST_F(Poe2MinionGemsTest, BrokenIllusionsLeaveFrenzyRemnants)
{
	Player &player = *MyPlayer;
	Socket(PersistentSkill::LingeringIllusion);
	Monster &illusion = PlaceMinion(0, { 44, 40 }, PersistentSkill::LingeringIllusion);
	illusion.poe2.decoy = true;
	EXPECT_EQ(MinionGemDamagePercent(illusion), 0);
	// One that fades on its own leaves nothing.
	OnMinionGemDied(player, illusion);
	EXPECT_EQ(FrenzyRemnantCount(), 0U);
	// One an enemy breaks leaves a remnant (a second one only at higher gem levels).
	illusion.poe2.minionLifeTicks = 20;
	OnMinionGemDied(player, illusion);
	EXPECT_EQ(FrenzyRemnantCount(), 1U);
	EXPECT_EQ(IllusionSecondRemnantChance(1), 0);
	EXPECT_EQ(IllusionSecondRemnantChance(20), 76);
	// Walking over it gives a Frenzy charge.
	player.position.tile = { 44, 41 };
	ProcessMinionGems(player);
	EXPECT_EQ(player.poe2.frenzyCharges, 1);
	EXPECT_EQ(FrenzyRemnantCount(), 0U);
	EXPECT_EQ(illusion.poe2.minionLifeTicks, 19);
}

/** Hive 3 on Nightmare: area level 38, with the monster level tables loaded. */
static void GoToAreaThirtyEight()
{
	LoadAreaLevels();
	sgGameInitInfo.nDifficulty = DIFF_NIGHTMARE;
	setlevel = false;
	currlevel = 19;
	leveltype = DTYPE_NEST;
}

TEST_F(Poe2MinionGemsTest, MinionsKeepUpWithMonsters)
{
	GoToAreaThirtyEight();
	ASSERT_TRUE(HasMonsterLevelTable());
	ASSERT_EQ(CurrentAreaLevel(), 38);
	// A character of the area's level with the gem at the right level for them.
	Player &player = *MyPlayer;
	player.setCharacterLevel(38);
	const int gem = MaxGemLevelForCharacter(38);
	Socket(PersistentSkill::SkeletalWarriors, gem);
	const int level = MinionLevelForGemLevel(gem);
	EXPECT_GE(level, 38);
	Monster &warrior = PlaceMinion(0, { 41, 40 }, PersistentSkill::SkeletalWarriors);
	// InitSkeletonMinion's blow, without the owner's minion damage (that applies at the hit now).
	warrior.minDamage = static_cast<uint8_t>(2 + (level / 3) + gem);
	warrior.maxDamage = static_cast<uint8_t>(5 + (level / 2) + (2 * gem));
	EXPECT_EQ(Poe2MinionLevel(warrior), level);

	// Its average blow is the table's hit at its level x its blow over its type's ref (held to 250%).
	const int sum = warrior.minDamage + warrior.maxDamage;
	const int hit10 = MinionHit10(MT_WSKELAX, level, sum);
	EXPECT_EQ(hit10, MonsterLevelDamage10(level) * 2500 / 1000);
	const int blow = ScaleMonsterHit(warrior, sum * 32);
	EXPECT_NEAR(blow, hit10 * 64 / 10 * MinionDamagePercent(player) / 100, 64);

	// 3-8 blows kill a white monster of the area.
	const int64_t white = static_cast<int64_t>(MonsterLevelLife(38)) * 64;
	const int64_t blows = (white + blow - 1) / blow;
	EXPECT_GE(blows, 3);
	EXPECT_LE(blows, 8);

	// Its life is a white's at its level (x 100% for a Warrior).
	EXPECT_EQ(MinionLifeRaw(level, 100), MonsterLevelLife(level) * 64);
}

TEST_F(Poe2MinionGemsTest, MinionDamagePassesTheOldByteCap)
{
	GoToAreaThirtyEight();
	Socket(PersistentSkill::SkeletalWarriors, 10);
	Monster &warrior = PlaceMinion(0, { 41, 40 }, PersistentSkill::SkeletalWarriors);
	warrior.minDamage = 40;
	warrior.maxDamage = 80;
	// 120 x 64 x the table scale: far past what a byte held, and no wrap.
	const int blow = ScaleMonsterHit(warrior, 255 * 64);
	EXPECT_GT(blow, 255 * 64);
	EXPECT_GT(ScaleMonsterHit(warrior, 255), 255);
}

TEST_F(Poe2MinionGemsTest, RevivedAndKingsDeadKeepTheirKindsRatio)
{
	GoToAreaThirtyEight();
	Player &player = *MyPlayer;
	// Socketed is enough for its level.
	MyPlayer->poe2.skillSlots[0] = SkillSlot::ForPersistent(PersistentSkill::Revive);
	MyPlayer->poe2.slotLevels[0] = 5;
	Monster &zombie = Place(0, { 41, 40 }, 0, 100);
	zombie.flags |= MFLAG_GOLEM;
	zombie.goalVar3 = static_cast<int8_t>(MyPlayerId);
	zombie.poe2.revived = true;
	// A revived monster keeps its data damage, so its blow is the table's at Revive's minion level x its kind's ratio.
	zombie.minDamage = zombie.data().minDamage;
	zombie.maxDamage = zombie.data().maxDamage;
	EXPECT_EQ(Poe2MinionLevel(zombie), MinionLevelForGemLevel(5));
	const int sum = zombie.minDamage + zombie.maxDamage;
	const int ref = MonsterTypeRow(MT_NZOMBIE).damageRef10;
	ASSERT_GT(ref, 0);
	const int64_t ratio = std::clamp<int64_t>(static_cast<int64_t>(sum) * 5000 / ref, 300, 2500);
	EXPECT_EQ(MinionHit10(MT_NZOMBIE, MinionLevelForGemLevel(5), sum), static_cast<int>(MonsterLevelDamage10(MinionLevelForGemLevel(5)) * ratio / 1000));
	EXPECT_NEAR(ScaleMonsterHit(zombie, sum * 32), MinionHit10(MT_NZOMBIE, MinionLevelForGemLevel(5), sum) * 64 / 10 * MinionDamagePercent(player) / 100, 64);

	// The King's dead come from no gem: the area's level.
	zombie.poe2.revived = false;
	zombie.flags |= MFLAG_KINGS_DEAD;
	EXPECT_EQ(Poe2MinionLevel(zombie), 38);
}

TEST_F(Poe2MinionGemsTest, LachdanansNoDamageMinionIsSafe)
{
	GoToAreaThirtyEight();
	// Socketed is enough for its level.
	MyPlayer->poe2.skillSlots[0] = SkillSlot::ForPersistent(PersistentSkill::Revive);
	MyPlayer->poe2.slotLevels[0] = 5;
	Monster &minion = Place(0, { 41, 40 }, 0, 100);
	minion.flags |= MFLAG_GOLEM;
	minion.goalVar3 = static_cast<int8_t>(MyPlayerId);
	minion.poe2.revived = true;
	minion.minDamage = 0;
	minion.maxDamage = 0;
	// Nothing to divide by: only the owner's minion damage.
	EXPECT_EQ(ScaleMonsterHit(minion, 6400), 6400 * MinionDamagePercent(*MyPlayer) / 100);
}

TEST_F(Poe2MinionGemsTest, DiabloModeMinionsUnscaled)
{
	GoToAreaThirtyEight();
	Socket(PersistentSkill::SkeletalWarriors, 10);
	Monster &warrior = PlaceMinion(0, { 41, 40 }, PersistentSkill::SkeletalWarriors);
	warrior.minDamage = 10;
	warrior.maxDamage = 20;
	gbIsPoe2 = false;
	EXPECT_EQ(Poe2MinionLevel(warrior), 0);
	EXPECT_EQ(ScaleMonsterHit(warrior, 1000), 1000);
}

} // namespace
} // namespace devilution
