#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/defenses.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/skill_resources.hpp"
#include "poe2/spirit.hpp"
#include "poe2/spirit_buffs.hpp"
#include "poe2/spirit_skills.hpp"
#include "poe2/unique_effects.hpp"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2SpiritBuffsTest : public ::testing::Test {
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
		MyPlayer->_pMaxMana = MyPlayer->_pMana = Fixed26_6::fromInt(1000);
		MyPlayer->_pMaxManaBase = MyPlayer->_pManaBase = Fixed26_6::fromInt(1000);
		MyPlayer->_pMaxHP = MyPlayer->_pHitPoints = Fixed26_6::fromInt(1000);
		MyPlayer->_pMaxHPBase = MyPlayer->_pHPBase = Fixed26_6::fromInt(1000);
		MyPlayer->_pIMinDam = 20;
		MyPlayer->_pIMaxDam = 20;
		// Damage to monsters goes out as a network message.
		SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
	}

	void TearDown() override
	{
		ActiveMonsterCount = 0;
		Missiles.clear();
		ClearSpiritSkills();
		ClearHeralds();
		gbIsPoe2 = false;
		leveltype = DTYPE_TOWN;
		UnloadModArchives();
	}

	static void Socket(PersistentSkill skill)
	{
		MyPlayer->poe2.skillSlots[0] = SkillSlot::ForPersistent(skill);
	}

	static void Tick(int ticks)
	{
		for (int i = 0; i < ticks; i++)
			ProcessSpiritBuffs(*MyPlayer);
	}

	static Monster MakeMonster(uint32_t seed)
	{
		Monster monster {};
		monster.levelType = 0;
		monster.uniqueType = UniqueMonsterType::None;
		monster.flags = 0;
		monster.rndItemSeed = seed;
		monster.talkMsg = TEXT_NONE;
		return monster;
	}

	/** @brief Puts a monster in the level as the given slot; a unique one isn't knocked back, which needs its graphics. */
	static Monster &PlaceMonster(size_t slot, Point tile, int life = 1000, MonsterRarity rarity = MonsterRarity::Normal, bool unique = true)
	{
		Monster &monster = Monsters[slot];
		monster = MakeMonster(1);
		for (uint32_t seed = 1; seed < 100000; seed++) {
			monster.rndItemSeed = seed;
			if (GetMonsterAffixes(monster).rarity == rarity)
				break;
		}
		if (unique)
			monster.uniqueType = UniqueMonsterType::Garbud;
		monster.position.tile = tile;
		monster.maxHitPoints = monster.hitPoints = Fixed26_6::fromInt(life);
		ActiveMonsters[ActiveMonsterCount] = static_cast<unsigned>(slot);
		ActiveMonsterCount++;
		return monster;
	}
};

TEST_F(Poe2SpiritBuffsTest, WindDancerStagesRaiseEvasionAndAHitReleasesGaleForce)
{
	Player &player = *MyPlayer;
	player.poe2.evasion = 160;
	// 160 from gear and 40 from Dexterity.
	EXPECT_EQ(EvasionRating(player), 200);
	Socket(PersistentSkill::WindDancer);
	Tick(WindDancerStageTicks(1));
	EXPECT_EQ(player.poe2.spiritState.windStages, 1);
	EXPECT_EQ(EvasionRating(player), 220);
	Tick(WindDancerStageTicks(1) * 5);
	EXPECT_EQ(player.poe2.spiritState.windStages, WindDancerMaxStages);
	EXPECT_EQ(EvasionRating(player), 260);
	EXPECT_EQ(SpiritBuffPortraitText(player, PersistentSkill::WindDancer), "3");

	Monster &near = PlaceMonster(0, { 42, 40 });
	Monster &far = PlaceMonster(1, { 45, 40 });
	OnSpiritHitTaken(player, Fixed26_6::fromInt(0));
	EXPECT_EQ(player.poe2.spiritState.windStages, 0);
	Tick(1);
	// Three stages: 3 tiles, 20% of the weapon's 20 with 150% more.
	EXPECT_LT(near.hitPoints, near.maxHitPoints);
	EXPECT_GE(near.hitPoints, Fixed26_6::fromInt(985));
	EXPECT_EQ(far.hitPoints, Fixed26_6::fromInt(1000));
	EXPECT_EQ(GaleForceDamagePercent(20), 82);
}

TEST_F(Poe2SpiritBuffsTest, GhostDanceShroudsTurnHitsIntoEnergyShield)
{
	Player &player = *MyPlayer;
	player.poe2.evasion = 960;
	player.poe2.maxEnergyShield = 100;
	Socket(PersistentSkill::GhostDance);
	Tick(GhostShroudTicks(1));
	EXPECT_EQ(player.poe2.spiritState.shrouds, 1);
	OnSpiritHitTaken(player, Fixed26_6::fromInt(0));
	EXPECT_EQ(player.poe2.spiritState.shrouds, 0);
	// A second: 2% of 1000 evasion.
	Tick(20);
	EXPECT_EQ(player.poe2.energyShield / 64, 20);
	// It lasts 4 seconds, then stops.
	Tick(GhostDanceRegenTicks);
	EXPECT_EQ(player.poe2.energyShield / 64, 80);
	// No shroud left: a hit starts nothing.
	OnSpiritHitTaken(player, Fixed26_6::fromInt(0));
	Tick(20);
	EXPECT_EQ(player.poe2.energyShield / 64, 80);
}

TEST_F(Poe2SpiritBuffsTest, BarkskinTurnsShieldLostIntoArmourUpToTheGears)
{
	Player &player = *MyPlayer;
	player._pIAC = 200;
	EXPECT_EQ(ArmourRating(player), 200);
	Socket(PersistentSkill::Barkskin);
	OnSpiritHitTaken(player, Fixed26_6::fromInt(100));
	EXPECT_EQ(SpiritBuffArmour(player), 43);
	EXPECT_EQ(ArmourRating(player), 243);
	OnSpiritHitTaken(player, Fixed26_6::fromInt(1000));
	EXPECT_EQ(SpiritBuffArmour(player), 200);
	Tick(BarkskinTicks);
	EXPECT_EQ(SpiritBuffArmour(player), 0);
	// A hit the shield didn't take gives nothing.
	OnSpiritHitTaken(player, Fixed26_6::fromInt(0));
	EXPECT_EQ(SpiritBuffArmour(player), 0);
}

TEST_F(Poe2SpiritBuffsTest, ArcticArmourBlastsMeleeAttackersWithCold)
{
	Player &player = *MyPlayer;
	Socket(PersistentSkill::ArcticArmour);
	Tick(ArcticArmourStageTicks(1) * 10);
	EXPECT_EQ(player.poe2.spiritState.arcticStages, ArcticArmourMaxStages(1));
	EXPECT_EQ(ArcticArmourMaxStages(20), 5);
	Monster &monster = PlaceMonster(0, { 41, 40 });
	OnSpiritMeleeHitTaken(player, monster);
	EXPECT_EQ(player.poe2.spiritState.arcticStages, ArcticArmourMaxStages(1) - 1);
	EXPECT_LT(monster.hitPoints, monster.maxHitPoints);
	EXPECT_GT(monster.poe2.chillTicks, 0);
}

TEST_F(Poe2SpiritBuffsTest, CombatFrenzyGivesAFrenzyChargeOnceInAWhile)
{
	Player &player = *MyPlayer;
	OnSpiritFreezeOrElectrocute(player);
	EXPECT_EQ(player.poe2.frenzyCharges, 0);
	Socket(PersistentSkill::CombatFrenzy);
	OnSpiritFreezeOrElectrocute(player);
	OnSpiritFreezeOrElectrocute(player);
	EXPECT_EQ(player.poe2.frenzyCharges, 1);
	Tick(CombatFrenzyCooldownTicks(1));
	OnSpiritFreezeOrElectrocute(player);
	EXPECT_EQ(player.poe2.frenzyCharges, 2);
}

TEST_F(Poe2SpiritBuffsTest, EternalRageRegeneratesRageThatDoesNotFade)
{
	Player &player = *MyPlayer;
	Socket(PersistentSkill::EternalRage);
	// A quarter of a point a second at level 1.
	Tick(4 * 20);
	EXPECT_EQ(player.poe2.rage, 1);
	EXPECT_GE(player.poe2.rageTicks, RageHoldTicks);
	EXPECT_EQ(EternalRageCentiPerSecond(20), 500);
}

TEST_F(Poe2SpiritBuffsTest, ElementalConfluxFavoursOneElementAtATime)
{
	Player &player = *MyPlayer;
	Monster &monster = PlaceMonster(0, { 42, 40 });
	EXPECT_EQ(ConfluxElement(player), -1);
	Socket(PersistentSkill::ElementalConflux);
	Tick(1);
	const int element = ConfluxElement(player);
	ASSERT_GE(element, 0);
	EXPECT_EQ(SpiritBuffDamagePercent(monster, static_cast<DamageType>(element)), 140);
	EXPECT_EQ(SpiritBuffDamagePercent(monster, DamageType::Physical), 100);
	EXPECT_FALSE(SpiritBuffPortraitText(player, PersistentSkill::ElementalConflux).empty());
}

TEST_F(Poe2SpiritBuffsTest, OverwhelmingPresenceLowersThresholdsNearTheHero)
{
	Monster &near = PlaceMonster(0, { 42, 40 });
	Monster &far = PlaceMonster(1, { 45, 40 });
	const int chance = AilmentChancePerMille(near, DamageType::Lightning, Fixed26_6::fromInt(100));
	Socket(PersistentSkill::OverwhelmingPresence);
	EXPECT_EQ(PresenceThresholdPercent(near, true), 85);
	EXPECT_EQ(PresenceThresholdPercent(near, false), 85);
	EXPECT_EQ(PresenceThresholdPercent(far, true), 100);
	EXPECT_GT(AilmentChancePerMille(near, DamageType::Lightning, Fixed26_6::fromInt(100)), chance);
	EXPECT_EQ(PresenceStunThresholdPercent(20), 25);
}

TEST_F(Poe2SpiritBuffsTest, AlchemistsBoonSharesFlaskRecoveryWithMinions)
{
	EXPECT_EQ(FlaskMinionHealPercent(*MyPlayer), 0);
	Socket(PersistentSkill::AlchemistsBoon);
	EXPECT_EQ(FlaskMinionHealPercent(*MyPlayer), 15);
	EXPECT_EQ(AlchemistChargesMilliPerSecond(1), 170);
	EXPECT_EQ(AlchemistChargesMilliPerSecond(20), 230);
}

TEST_F(Poe2SpiritBuffsTest, CaltropsHurtAndHinderTheMonsterThatStepsOnOne)
{
	Player &player = *MyPlayer;
	OnSpiritDodge(player, { 40, 40 }, { 43, 40 });
	EXPECT_EQ(CaltropCount(), 0U);
	Socket(PersistentSkill::TrailOfCaltrops);
	OnSpiritDodge(player, { 40, 40 }, { 43, 40 });
	EXPECT_EQ(CaltropCount(), 3U);
	Monster &monster = PlaceMonster(0, { 41, 40 });
	dMonster[41][40] = 1;
	Tick(1);
	dMonster[41][40] = 0;
	EXPECT_EQ(CaltropCount(), 2U);
	EXPECT_LT(monster.hitPoints, monster.maxHitPoints);
	EXPECT_EQ(monster.poe2.hinderTicks, CaltropHinderTicks);
	EXPECT_EQ(MonsterHinderPercent(monster), HinderPercent);
	Tick(CaltropTicks);
	EXPECT_EQ(CaltropCount(), 0U);
}

TEST_F(Poe2SpiritBuffsTest, ManaRemnantsComeFromAilingMonstersAndGiveMana)
{
	Player &player = *MyPlayer;
	player._pMana = player._pManaBase = Fixed26_6::fromInt(500);
	Monster &monster = PlaceMonster(0, { 43, 40 });
	Socket(PersistentSkill::ManaRemnants);
	OnSpiritCrit(player, monster);
	EXPECT_EQ(ManaRemnantCount(), 0U);
	monster.poe2.shockTicks = 10;
	OnSpiritCrit(player, monster);
	OnSpiritCrit(player, monster);
	EXPECT_EQ(ManaRemnantCount(), 1U);
	Tick(1);
	EXPECT_EQ(ManaRemnantCount(), 1U);
	player.position.tile = { 43, 40 };
	Tick(1);
	EXPECT_EQ(ManaRemnantCount(), 0U);
	EXPECT_EQ(player._pMana, Fixed26_6::fromInt(528));
}

TEST_F(Poe2SpiritBuffsTest, AttritionWearsDownRaresUntilTheyCanBeCulled)
{
	Player &player = *MyPlayer;
	Monster &rare = PlaceMonster(0, { 42, 40 }, 1000, MonsterRarity::Rare, false);
	Monster &normal = PlaceMonster(1, { 41, 40 }, 1000, MonsterRarity::Normal, false);
	Socket(PersistentSkill::Attrition);
	Tick(10 * AttritionTicksPerPercent);
	EXPECT_EQ(SpiritBuffDamagePercent(rare, DamageType::Physical), 110);
	EXPECT_EQ(SpiritBuffDamagePercent(normal, DamageType::Physical), 100);
	rare.hitPoints = Fixed26_6::fromInt(50);
	EXPECT_FALSE(AttritionCulls(player, rare));
	// After 40 seconds near the hero it can be culled at 10% life.
	Tick(AttritionCullTicks(1));
	EXPECT_EQ(SpiritBuffDamagePercent(rare, DamageType::Physical), 130);
	EXPECT_TRUE(AttritionCulls(player, rare));
	rare.hitPoints = Fixed26_6::fromInt(500);
	EXPECT_FALSE(AttritionCulls(player, rare));
}

TEST_F(Poe2SpiritBuffsTest, HeraldOfPlagueSpreadsTheDeadMonstersPoison)
{
	Player &player = *MyPlayer;
	Monster dead = MakeMonster(1);
	dead.position.tile = { 43, 40 };
	dead.maxHitPoints = Fixed26_6::fromInt(100);
	dead.poe2.poisonTicks = 60;
	dead.poe2.poisonDamagePerTick = 128;
	Monster &next = PlaceMonster(0, { 44, 40 });
	Monster &away = PlaceMonster(1, { 48, 40 });
	Socket(PersistentSkill::HeraldOfPlague);
	QueueHeraldTriggers(player, dead);
	ProcessHeralds();
	EXPECT_EQ(next.poe2.poisonTicks, 60);
	EXPECT_EQ(next.poe2.poisonDamagePerTick, 128);
	EXPECT_EQ(away.poe2.poisonTicks, 0);
}

TEST_F(Poe2SpiritBuffsTest, BlinkReplacesTheDodgeRoll)
{
	EXPECT_FALSE(BlinkReplacesDodge(*MyPlayer));
	Socket(PersistentSkill::Blink);
	EXPECT_TRUE(BlinkReplacesDodge(*MyPlayer));
	EXPECT_EQ(BlinkTiles(1), 3);
	EXPECT_EQ(BlinkTiles(20), 4);
	EXPECT_EQ(BlinkCooldownTicks(1), 90);
	EXPECT_EQ(BlinkCooldownTicks(20), 70);
}

} // namespace
} // namespace devilution
