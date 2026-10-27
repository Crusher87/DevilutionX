#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/defenses.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/skill_resources.hpp"
#include "poe2/spirit.hpp"
#include "poe2/spirit_auras.hpp"
#include "poe2/spirit_buffs.hpp"
#include "poe2/spirit_skills.hpp"
#include "poe2/support_table.hpp"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2SpiritAurasTest : public ::testing::Test {
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
		Missiles.clear();
		leveltype = DTYPE_CATHEDRAL;
		MyPlayer->setCharacterLevel(30);
		MyPlayer->position.tile = { 40, 40 };
		MyPlayer->_pdir = Direction::South;
		MyPlayer->_pMaxMana = MyPlayer->_pMana = Fixed26_6::fromInt(1000);
		MyPlayer->_pMaxManaBase = MyPlayer->_pManaBase = Fixed26_6::fromInt(1000);
		MyPlayer->_pMaxHP = MyPlayer->_pHitPoints = Fixed26_6::fromInt(1000);
		MyPlayer->_pMaxHPBase = MyPlayer->_pHPBase = Fixed26_6::fromInt(1000);
		// Damage to monsters goes out as a network message.
		SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
	}

	void TearDown() override
	{
		ActiveMonsterCount = 0;
		Missiles.clear();
		ClearSpiritSkills();
		gbIsPoe2 = false;
		leveltype = DTYPE_TOWN;
		UnloadModArchives();
	}

	static void Socket(PersistentSkill skill, size_t slot = 0)
	{
		MyPlayer->poe2.skillSlots[slot] = SkillSlot::ForPersistent(skill);
	}

	static void Tick(int ticks)
	{
		for (int i = 0; i < ticks; i++)
			ProcessSpiritAuras(*MyPlayer);
	}

	/** @brief Puts a unique monster (Monster Power 20) in the level as the given slot. */
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

	static void HoldShield(int armour)
	{
		Item &shield = MyPlayer->InvBody[INVLOC_HAND_LEFT];
		shield = {};
		shield._itype = ItemType::Shield;
		shield._iStatFlag = true;
		shield._iAC = armour;
		MyPlayer->_pBlockFlag = true;
		// What CalcPlrItemVals finds: in POE2 the shield's own chance is the block.
		MyPlayer->poe2.shieldBlockChance = 25;
	}
};

TEST_F(Poe2SpiritAurasTest, EveryNewGemHasAGemItemANameAndADescription)
{
	for (const PersistentSkill skill : { PersistentSkill::Briarpatch, PersistentSkill::MagmaBarrier, PersistentSkill::ScavengedPlating, PersistentSkill::TimeOfNeed,
	         PersistentSkill::ChargeRegulation, PersistentSkill::WitheringPresence, PersistentSkill::Trinity, PersistentSkill::SiphonElements, PersistentSkill::HeraldOfBlood }) {
		EXPECT_TRUE(SkillGemItemIndex(SkillSlot::ForPersistent(skill)).has_value()) << PersistentSkillName(skill);
		EXPECT_FALSE(PersistentSkillName(skill).empty());
		EXPECT_FALSE(PersistentSkillDescription(skill).empty()) << PersistentSkillName(skill);
		EXPECT_GT(SpiritReservation(skill), 0) << PersistentSkillName(skill);
	}
	EXPECT_EQ(SpiritReservation(PersistentSkill::Trinity), 100);
	EXPECT_NE(SkillTags(SkillSlot::ForPersistent(PersistentSkill::HeraldOfBlood)) & ST_Herald, 0U);
}

TEST_F(Poe2SpiritAurasTest, BriarpatchStoresAttacksAndSpreadsThemWhenYouCast)
{
	Player &player = *MyPlayer;
	Socket(PersistentSkill::Briarpatch);
	OnSpiritPhysicalAttack(player, Fixed26_6::fromInt(1000).raw());
	// 10% of the hit's physical damage.
	EXPECT_EQ(player.poe2.spiritState.briarStore, Fixed26_6::fromInt(100).raw());
	// Up to 50% of maximum life at gem level 1.
	for (int i = 0; i < 20; i++)
		OnSpiritPhysicalAttack(player, Fixed26_6::fromInt(1000).raw());
	EXPECT_EQ(player.poe2.spiritState.briarStore, Fixed26_6::fromInt(500).raw());

	Monster &inside = PlaceMonster(0, { 41, 41 });
	Monster &outside = PlaceMonster(1, { 44, 40 });
	OnSpiritSpellCast(player);
	EXPECT_EQ(player.poe2.spiritState.briarStore, 0);
	EXPECT_EQ(player.poe2.spiritState.briarTicks, BriarpatchTicks);
	// The hero walks away; the thorny ground stays where it was spread.
	player.position.tile = { 50, 50 };
	Tick(BriarpatchTicks);
	// It dealt what was stored, a part each pulse, to what stood in it, and hindered it.
	const int dealt = 10000 - inside.hitPoints.whole();
	EXPECT_GE(dealt, 450);
	EXPECT_LE(dealt, 500);
	EXPECT_GT(inside.poe2.hinderTicks, 0);
	EXPECT_EQ(outside.hitPoints.whole(), 10000);

	// It can't be spread again for 8 seconds.
	OnSpiritPhysicalAttack(player, Fixed26_6::fromInt(1000).raw());
	OnSpiritSpellCast(player);
	EXPECT_EQ(player.poe2.spiritState.briarTicks, 0);
	Tick(BriarpatchCooldownTicks);
	OnSpiritSpellCast(player);
	EXPECT_EQ(player.poe2.spiritState.briarTicks, BriarpatchTicks);
}

TEST_F(Poe2SpiritAurasTest, MagmaBarrierRaisesBlockAndAChargedBlockSpraysFireAhead)
{
	Player &player = *MyPlayer;
	Socket(PersistentSkill::MagmaBarrier);
	Tick(1);
	// Without a shield there is no block, and it does nothing.
	EXPECT_EQ(player.GetBlockChance(false), 0);
	EXPECT_EQ(player.poe2.spiritState.blockPercent, 0);
	HoldShield(100);
	const int before = player.GetBlockChance(false);
	EXPECT_EQ(before, 25);
	Tick(1);
	EXPECT_EQ(player.GetBlockChance(false), before * 125 / 100);

	Monster &ahead = PlaceMonster(0, player.position.tile + Displacement { Direction::South } * 2);
	Monster &behind = PlaceMonster(1, player.position.tile + Displacement { Direction::North } * 2);
	// It isn't charged yet.
	OnSpiritBlocked(player);
	EXPECT_EQ(ahead.hitPoints.whole(), 10000);
	Tick(MagmaBarrierChargeTicks(1));
	EXPECT_EQ(player.poe2.spiritState.magmaTicks, 0);
	OnSpiritBlocked(player);
	EXPECT_LT(ahead.hitPoints.whole(), 10000);
	EXPECT_EQ(behind.hitPoints.whole(), 10000);
	// Then it charges again.
	EXPECT_EQ(player.poe2.spiritState.magmaTicks, MagmaBarrierChargeTicks(1));
	const Fixed26_6 life = ahead.hitPoints;
	OnSpiritBlocked(player);
	EXPECT_EQ(ahead.hitPoints, life);
}

TEST_F(Poe2SpiritAurasTest, ScavengedPlatingStacksFromBrokenArmourForArmourAndThorns)
{
	Player &player = *MyPlayer;
	Socket(PersistentSkill::ScavengedPlating);
	player._pIAC = 500;
	const int armour = ArmourRating(player);
	Monster &unique = PlaceMonster(0, { 41, 40 });
	BreakMonsterArmour(unique, &player);
	// A unique gives the most, capped at 10.
	EXPECT_EQ(player.poe2.spiritState.platingStacks, ScavengedPlatingMaxStacks);
	EXPECT_EQ(ScavengedArmourPercent(player), 40);
	EXPECT_GT(ArmourRating(player), armour);

	// A melee hit taken pays the attacker back with thorns, more on its broken armour.
	OnSpiritMeleeHitTaken(player, unique);
	EXPECT_LE(unique.hitPoints.whole(), 10000 - (ScavengedPlatingMaxStacks * ScavengedPlatingThorns(1)));
	EXPECT_GT(unique.hitPoints.whole(), 10000 - (2 * ScavengedPlatingMaxStacks * ScavengedPlatingThorns(1)));

	Tick(ScavengedPlatingTicks);
	EXPECT_EQ(player.poe2.spiritState.platingStacks, 0);
	// Armour that's already broken gives nothing more.
	BreakMonsterArmour(unique, &player);
	EXPECT_EQ(player.poe2.spiritState.platingStacks, 0);
}

TEST_F(Poe2SpiritAurasTest, TimeOfNeedHealsWhenHurtThenWaits)
{
	Player &player = *MyPlayer;
	Socket(PersistentSkill::TimeOfNeed);
	// At full life it waits.
	Tick(5);
	EXPECT_EQ(player.poe2.spiritState.needTicks, 0);
	player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(500);
	Tick(1);
	EXPECT_EQ(player._pHitPoints.whole(), 600);
	Tick(TimeOfNeedTicks(1) - 1);
	EXPECT_EQ(player._pHitPoints.whole(), 600);
	Tick(2);
	EXPECT_EQ(player._pHitPoints.whole(), 700);
}

TEST_F(Poe2SpiritAurasTest, ChargeRegulationSpendsFrenzyChargesOnSpeed)
{
	Player &player = *MyPlayer;
	Socket(PersistentSkill::ChargeRegulation);
	Tick(1);
	EXPECT_EQ(SpiritAuraSpeedPercent(player), 0);
	player.poe2.frenzyCharges = 2;
	Tick(1);
	EXPECT_EQ(player.poe2.frenzyCharges, 1);
	EXPECT_EQ(SpiritAuraSpeedPercent(player), 20);
	// The next charge goes only as the speed runs low.
	Tick(ChargeRegulationTicksPerCharge - ChargeRegulationRefillTicks - 2);
	EXPECT_EQ(player.poe2.frenzyCharges, 1);
	Tick(2);
	EXPECT_EQ(player.poe2.frenzyCharges, 0);
	Tick(ChargeRegulationTicksPerCharge + ChargeRegulationRefillTicks);
	EXPECT_EQ(SpiritAuraSpeedPercent(player), 0);
}

TEST_F(Poe2SpiritAurasTest, WitheringPresenceWithersMonstersNearbyUpToFiveStacks)
{
	Socket(PersistentSkill::WitheringPresence);
	Monster &near = PlaceMonster(0, { 42, 40 });
	Monster &far = PlaceMonster(1, { 40 + PresenceTiles + 1, 40 });
	Tick(1);
	EXPECT_EQ(near.poe2.witherStacks, 1);
	EXPECT_EQ(far.poe2.witherStacks, 0);
	Tick(WitheringPresencePulseTicks(1) * 10);
	EXPECT_EQ(near.poe2.witherStacks, WitheringPresenceMaxStacks);
	EXPECT_GT(near.poe2.witherTicks, 0);
}

TEST_F(Poe2SpiritAurasTest, TrinityRewardsMixingElements)
{
	Player &player = *MyPlayer;
	Socket(PersistentSkill::Trinity);
	OnSpiritElementalHit(player, DamageType::Fire);
	// One gain an element a tick.
	OnSpiritElementalHit(player, DamageType::Fire);
	EXPECT_EQ(player.poe2.spiritState.resonance[0], 6);
	OnSpiritElementalHit(player, DamageType::Lightning);
	EXPECT_EQ(player.poe2.spiritState.resonance[0], 4);
	EXPECT_EQ(player.poe2.spiritState.resonance[1], 6);
	OnSpiritElementalHit(player, DamageType::Physical);
	EXPECT_EQ(player.poe2.spiritState.resonance[1], 6);

	// Mixing all three fills them all.
	for (int i = 0; i < 100; i++) {
		Tick(1);
		OnSpiritElementalHit(player, DamageType::Fire);
		OnSpiritElementalHit(player, DamageType::Lightning);
		OnSpiritElementalHit(player, DamageType::Magic);
	}
	EXPECT_GE(player.poe2.spiritState.resonance[0] + player.poe2.spiritState.resonance[1] + player.poe2.spiritState.resonance[2], 270);
	EXPECT_EQ(TrinityDamagePercent(player, DamageType::Fire), 9);
	EXPECT_EQ(TrinityDamagePercent(player, DamageType::Physical), 0);

	// It fades 10 a second after 8 seconds without an elemental hit.
	Tick(TrinityIdleTicks + 40);
	EXPECT_LE(player.poe2.spiritState.resonance[0], 100 - 19);
	Tick(400);
	EXPECT_EQ(TrinityDamagePercent(player, DamageType::Fire), 0);
}

TEST_F(Poe2SpiritAurasTest, SiphonElementsLeavesInfusionsFromAilments)
{
	Player &player = *MyPlayer;
	Socket(PersistentSkill::SiphonElements);
	const Monster &monster = PlaceMonster(0, { 45, 40 });
	// Without a skill that eats infusions, none are left.
	for (int i = 0; i < 200; i++)
		OnSpiritElementalAilment(player, monster, DamageType::Fire);
	EXPECT_EQ(InfusionMoteCount(), 0U);
	player.poe2.skillSlots[1] = SkillSlot::ForAttack(WeaponSkill::FrozenOrb);
	for (int i = 0; i < 400; i++)
		OnSpiritElementalAilment(player, monster, DamageType::Fire);
	// At most 3 of an element lie around.
	EXPECT_EQ(InfusionMoteCount(), 3U);
	EXPECT_EQ(InfusionPickupTiles(player), 1);
	player.poe2.slotLevels[0] = 11;
	EXPECT_EQ(InfusionPickupTiles(player), 2);
}

TEST_F(Poe2SpiritAurasTest, HeraldOfBloodBurstsBleedingMonstersThatDie)
{
	Player &player = *MyPlayer;
	Socket(PersistentSkill::HeraldOfBlood);
	Monster &dead = PlaceMonster(0, { 45, 40 }, 1000);
	Monster &neighbour = PlaceMonster(1, { 46, 40 });
	Monster &away = PlaceMonster(2, { 50, 40 });
	// Not bleeding: no burst.
	QueueHeraldTriggers(player, dead);
	ProcessHeralds();
	EXPECT_EQ(neighbour.hitPoints.whole(), 10000);

	dead.poe2.bleedTicks = 20;
	QueueHeraldTriggers(player, dead);
	ProcessHeralds();
	// 15% of the dead monster's 1000 life.
	EXPECT_LT(neighbour.hitPoints.whole(), 10000);
	EXPECT_GE(neighbour.hitPoints.whole(), 10000 - 150);
	EXPECT_EQ(away.hitPoints.whole(), 10000);
}

} // namespace
} // namespace devilution
