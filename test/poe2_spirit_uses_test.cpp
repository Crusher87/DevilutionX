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
#include "poe2/spirit.hpp"
#include "poe2/spirit_skills.hpp"
#include "poe2/spirit_uses.hpp"
#include "poe2/support_table.hpp"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2SpiritUsesTest : public ::testing::Test {
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
		LevelMonsterTypes[1].type = SkeletonMinionType;
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
			ProcessSpiritUses(*MyPlayer);
	}

	/** @brief Puts a unique monster (Monster Power 20) in the level as the given slot. */
	static Monster &PlaceMonster(size_t slot, Point tile, int life = 1000)
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
};

TEST_F(Poe2SpiritUsesTest, IronWardStoresWhatArmourStopsAndReleasesItAsANova)
{
	Player &player = *MyPlayer;
	Socket(PersistentSkill::IronWard);
	player._pIAC = 500;
	EXPECT_EQ(UseSpiritGem(player, PersistentSkill::IronWard), SpiritUseResult::Empty);

	const Fixed26_6 hit = Fixed26_6::fromInt(100);
	const Fixed26_6 taken = ApplyArmour(player, hit);
	const int32_t stopped = hit.raw() - taken.raw();
	ASSERT_GT(stopped, 0);
	// Gem level 1 stores 100% of it.
	EXPECT_EQ(player.poe2.spiritState.ironWardStore, stopped);
	EXPECT_TRUE(SpiritGemReady(player, PersistentSkill::IronWard));

	// It holds up to the hero's maximum life at gem level 1.
	for (int i = 0; i < 200; i++)
		ApplyArmour(player, Fixed26_6::fromInt(400));
	EXPECT_EQ(SpiritGemStore(player, PersistentSkill::IronWard).value, 1000);

	Monster &near = PlaceMonster(0, { 43, 40 }, 5000);
	Monster &far = PlaceMonster(1, { 47, 40 }, 5000);
	EXPECT_EQ(UseSpiritGem(player, PersistentSkill::IronWard), SpiritUseResult::Used);
	EXPECT_LT(near.hitPoints.whole(), 5000);
	EXPECT_GE(near.hitPoints.whole(), 4000);
	EXPECT_EQ(far.hitPoints.whole(), 5000);
	EXPECT_EQ(player.poe2.spiritState.ironWardStore, 0);
	EXPECT_EQ(UseSpiritGem(player, PersistentSkill::IronWard), SpiritUseResult::Empty);
}

TEST_F(Poe2SpiritUsesTest, PlagueBearerStoresPoisonDamageAndItsNovaPoisons)
{
	Player &player = *MyPlayer;
	Socket(PersistentSkill::PlagueBearer);
	Monster &poisoned = PlaceMonster(0, { 45, 45 }, 100000);
	poisoned.poe2.poisonTicks = 200;
	poisoned.poe2.poisonDamagePerTick = 10 * 64;
	poisoned.poe2.poisonSource = static_cast<int8_t>(MyPlayerId);
	for (int i = 0; i < 100; i++)
		ProcessSupportMonsterEffects(poisoned);
	// 1,000 poison damage dealt stores 4%.
	EXPECT_EQ(SpiritGemStore(player, PersistentSkill::PlagueBearer).value, 40);

	Monster &target = PlaceMonster(1, { 41, 41 }, 5000);
	EXPECT_EQ(UseSpiritGem(player, PersistentSkill::PlagueBearer), SpiritUseResult::Used);
	EXPECT_LT(target.hitPoints.whole(), 5000);
	EXPECT_GT(target.poe2.poisonTicks, 0);
	EXPECT_EQ(target.poe2.poisonSource, static_cast<int8_t>(MyPlayerId));
	EXPECT_EQ(SpiritGemStore(player, PersistentSkill::PlagueBearer).value, 0);
}

TEST_F(Poe2SpiritUsesTest, ConvalescenceStartsTheRechargeAndHitsCantStopIt)
{
	Player &player = *MyPlayer;
	Socket(PersistentSkill::Convalescence);
	player.poe2.maxEnergyShield = 100;
	player.poe2.energyShield = 100 * 64;
	EXPECT_EQ(UseSpiritGem(player, PersistentSkill::Convalescence), SpiritUseResult::ShieldFull);

	player.poe2.energyShield = 0;
	EXPECT_EQ(UseSpiritGem(player, PersistentSkill::Convalescence), SpiritUseResult::Used);
	EXPECT_EQ(player._pMana.whole(), 1000 - ConvalescenceManaCost);
	EXPECT_EQ(player.poe2.energyShieldRechargeTicks, EnergyShieldRechargeDelayTicks);
	AbsorbWithEnergyShield(player, Fixed26_6::fromInt(10));
	EXPECT_EQ(player.poe2.energyShieldRechargeTicks, EnergyShieldRechargeDelayTicks);
	EXPECT_EQ(UseSpiritGem(player, PersistentSkill::Convalescence), SpiritUseResult::Cooldown);

	// After it ends, a hit stops the recharge again and the cooldown runs.
	Tick(ConvalescenceTicks(1));
	AbsorbWithEnergyShield(player, Fixed26_6::fromInt(10));
	EXPECT_EQ(player.poe2.energyShieldRechargeTicks, 0);
	EXPECT_EQ(UseSpiritGem(player, PersistentSkill::Convalescence), SpiritUseResult::Cooldown);
	Tick(ConvalescenceCooldownTicks);
	EXPECT_EQ(UseSpiritGem(player, PersistentSkill::Convalescence), SpiritUseResult::Used);
}

TEST_F(Poe2SpiritUsesTest, GrimFeastStoresLifeFromFallenSkeletonsAndRaisesThem)
{
	Player &player = *MyPlayer;
	Socket(PersistentSkill::GrimFeast);
	Monster skeleton {};
	skeleton.levelType = 1;
	skeleton.position.tile = { 41, 40 };
	skeleton.maxHitPoints = Fixed26_6::fromInt(200);
	OnSpiritMinionDied(player, skeleton);
	ASSERT_EQ(GrimRemnantCount(), 1U);

	// Walking over the remnant stores 35% of the skeleton's life.
	Tick(1);
	EXPECT_EQ(GrimRemnantCount(), 0U);
	EXPECT_EQ(SpiritGemStore(player, PersistentSkill::GrimFeast).value, 70);
	// It holds up to twice the toughest skeleton's life at gem level 1.
	EXPECT_EQ(SpiritGemStore(player, PersistentSkill::GrimFeast).cap, 400);

	EXPECT_EQ(UseSpiritGem(player, PersistentSkill::GrimFeast), SpiritUseResult::NothingToRaise);
	MinionRevives &revives = player.poe2.minionRevives[0];
	revives.pending = 1;
	revives.ticks[0] = 200;
	// 70 life can't pay for a 200 life skeleton.
	EXPECT_EQ(UseSpiritGem(player, PersistentSkill::GrimFeast), SpiritUseResult::Empty);
	player.poe2.spiritState.grimFeastStore = 300 * 64;
	EXPECT_EQ(UseSpiritGem(player, PersistentSkill::GrimFeast), SpiritUseResult::Used);
	EXPECT_EQ(revives.ticks[0], 1);
	EXPECT_EQ(SpiritGemStore(player, PersistentSkill::GrimFeast).value, 100);
}

TEST_F(Poe2SpiritUsesTest, WarBannerNeedsGloryAndEmpowersAttacksInItsArea)
{
	Player &player = *MyPlayer;
	Socket(PersistentSkill::WarBanner);
	const Monster &unique = PlaceMonster(0, { 41, 40 });
	for (int i = 0; i < 9; i++)
		OnSpiritAttackHit(player, unique);
	EXPECT_EQ(SpiritGemStore(player, PersistentSkill::WarBanner).value, 180);
	EXPECT_EQ(UseSpiritGem(player, PersistentSkill::WarBanner), SpiritUseResult::NotEnoughGlory);
	OnSpiritAttackHit(player, unique);
	EXPECT_EQ(UseSpiritGem(player, PersistentSkill::WarBanner), SpiritUseResult::Used);
	EXPECT_EQ(SpiritGemStore(player, PersistentSkill::WarBanner).value, 0);

	EXPECT_EQ(BannerAttackDamagePercent(player), WarBannerDamagePercent);
	EXPECT_EQ(BannerAccuracyPercent(player), WarBannerAccuracyPercent);
	EXPECT_EQ(BannerAttackSpeedPercent(player), WarBannerAttackSpeedPercent);
	player.position.tile = { 40 + BannerTiles + 1, 40 };
	EXPECT_EQ(BannerAttackDamagePercent(player), 0);
	player.position.tile = { 40, 40 };

	// It stands for 6 seconds at gem level 1.
	Tick(BannerTicks(1) - 1);
	EXPECT_EQ(BannerAttackDamagePercent(player), WarBannerDamagePercent);
	Tick(2);
	EXPECT_EQ(BannerAttackDamagePercent(player), 0);
}

TEST_F(Poe2SpiritUsesTest, DefianceBannerGivesMoreArmourEvasionAndSpeed)
{
	Player &player = *MyPlayer;
	Socket(PersistentSkill::DefianceBanner);
	player._pIAC = 100;
	player.poe2.evasion = 200;
	const int armour = ArmourRating(player);
	const int evasion = EvasionRating(player);
	player.poe2.spiritState.glory[1] = GloryForBanner;
	EXPECT_EQ(UseSpiritGem(player, PersistentSkill::DefianceBanner), SpiritUseResult::Used);
	EXPECT_EQ(ArmourRating(player), armour * 130 / 100);
	EXPECT_EQ(EvasionRating(player), evasion * 130 / 100);
	// A tile normally takes 6 walk frames; 30% faster skips about 1.4 more each tile.
	int skipped = 0;
	for (int tile = 0; tile < 10; tile++)
		skipped += BannerWalkSkippedFrames(player, 8);
	EXPECT_GE(skipped, 13);
	EXPECT_LE(skipped, 14);
}

TEST_F(Poe2SpiritUsesTest, DreadBannerRaisesTheMaximumResistances)
{
	Player &player = *MyPlayer;
	Socket(PersistentSkill::DreadBanner);
	// Working out the stats again starts from the base attributes, which the gem needs.
	player._pBaseStr = player._pBaseMag = player._pBaseDex = 200;
	CalcPlrInv(player, false);
	const int16_t usual = player.poe2.maxResistances[0];
	player.poe2.spiritState.glory[2] = GloryForBanner;
	EXPECT_EQ(UseSpiritGem(player, PersistentSkill::DreadBanner), SpiritUseResult::Used);
	Tick(1);
	EXPECT_EQ(BannerMaxResistance(player), DreadBannerMaxResistance);
	EXPECT_EQ(player.poe2.maxResistances[0], usual + DreadBannerMaxResistance);
	// Stepping out of its area puts them back.
	player.position.tile = { 40 + BannerTiles + 1, 40 };
	Tick(1);
	EXPECT_EQ(player.poe2.maxResistances[0], usual);
}

TEST_F(Poe2SpiritUsesTest, BannersNeedTheirGemOnAndLeaveTown)
{
	Player &player = *MyPlayer;
	Socket(PersistentSkill::WarBanner);
	player.poe2.spiritState.glory[0] = GloryForBanner;
	leveltype = DTYPE_TOWN;
	EXPECT_EQ(UseSpiritGem(player, PersistentSkill::WarBanner), SpiritUseResult::Town);
	leveltype = DTYPE_CATHEDRAL;
	EXPECT_EQ(UseSpiritGem(player, PersistentSkill::WarBanner), SpiritUseResult::Used);
	// Switching the gem off takes the banner down and loses its Glory.
	player.poe2.spiritGemsOn = 0;
	Tick(1);
	EXPECT_EQ(BannerAttackDamagePercent(player), 0);
	EXPECT_EQ(UseSpiritGem(player, PersistentSkill::WarBanner), SpiritUseResult::Off);
}

} // namespace
} // namespace devilution
