/**
 * @file poe2_effect_primitives_test.cpp
 *
 * Build spec step 4's effect primitives, one test each: what a ModEffect puts on the item, and what the hero gets from
 * it. Runes, soulstones, relics and essences name these effects in their tables.
 */
#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "items.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/default_attack.hpp"
#include "poe2/defenses.hpp"
#include "poe2/effect_primitives.hpp"
#include "poe2/flasks.hpp"
#include "poe2/item_stats.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/unique_rules.hpp"
#include "poe2/vfx.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

int Stat(const Item &item, Poe2ItemStat stat)
{
	return item._iPoe2Stats[static_cast<size_t>(stat)];
}

void SetGearStat(Player &player, Poe2ItemStat stat, int value)
{
	player.poe2.itemStats[static_cast<size_t>(stat)] = static_cast<int16_t>(value);
}

class Poe2EffectPrimitivesTest : public ::testing::Test {
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
		Player &player = *MyPlayer;
		player._pClass = HeroClass::Warrior;
		player.setCharacterLevel(20);
		player._pMaxHPBase = player._pMaxHP = Fixed26_6::fromInt(1000);
		player._pHPBase = player._pHitPoints = Fixed26_6::fromInt(100);
		player._pMaxManaBase = player._pMaxMana = Fixed26_6::fromInt(1000);
		player._pManaBase = player._pMana = Fixed26_6::fromInt(0);
	}

	void TearDown() override
	{
		MyPlayer->poe2.clear();
		gbIsPoe2 = false;
		LoadModifierData();
		UnloadModArchives();
	}

	/** @brief Puts a ring with the effect on the hero and works out their stats. */
	static void WearRingWith(Player &player, ModEffect effect, int v0, int v1 = 0)
	{
		Item &ring = player.InvBody[INVLOC_RING_LEFT];
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iMiscId == IMISC_RING) {
				InitializeItem(ring, static_cast<_item_indexes>(i));
				break;
			}
		}
		ring._iStatFlag = true;
		ring._iIdentified = true;
		ring._iMagical = ITEM_QUALITY_MAGIC;
		ApplyModEffect(ring, effect, v0, v1);
		CalcPlrItemVals(player, false);
	}

	static int Life(const Player &player) { return player._pHitPoints.whole(); }
	static int Mana(const Player &player) { return player._pMana.whole(); }
};

TEST_F(Poe2EffectPrimitivesTest, LeechHealsFromAttackHitsUpToItsCap)
{
	Item item {};
	ApplyModEffect(item, ModEffect::LifeLeech, 2, 0);
	ApplyModEffect(item, ModEffect::ManaLeech, 3, 0);
	EXPECT_EQ(Stat(item, Poe2ItemStat::LifeLeech), 2);
	EXPECT_EQ(Stat(item, Poe2ItemStat::ManaLeech), 3);

	Player &player = *MyPlayer;
	SetGearStat(player, Poe2ItemStat::LifeLeech, 3);
	SetGearStat(player, Poe2ItemStat::ManaLeech, 2);
	LeechFromHit(player, SkillSlot::ForAttack(WeaponSkill::Cleave), Fixed26_6::fromInt(1000).raw());
	EXPECT_EQ(Life(player), 130);
	EXPECT_EQ(Mana(player), 20);

	// Spells don't leech.
	LeechFromHit(player, SkillSlot::ForSpell(SpellID::Firebolt), Fixed26_6::fromInt(1000).raw());
	EXPECT_EQ(Life(player), 130);
	EXPECT_EQ(Mana(player), 20);

	// Leech caps at 4% each.
	SetGearStat(player, Poe2ItemStat::LifeLeech, 9);
	EXPECT_EQ(GearLeechPercent(player, false), MaxLeechPercent);
	LeechFromHit(player, SkillSlot::ForDefault(static_cast<uint8_t>(DefaultAttack::SwordStrike)), Fixed26_6::fromInt(1000).raw());
	EXPECT_EQ(Life(player), 170);
}

TEST_F(Poe2EffectPrimitivesTest, ThornsHitMeleeAttackersBack)
{
	Item item {};
	ApplyModEffect(item, ModEffect::Thorns, 6, 9);
	EXPECT_EQ(Stat(item, Poe2ItemStat::ThornsMin), 6);
	EXPECT_EQ(Stat(item, Poe2ItemStat::ThornsMax), 9);

	Player &player = *MyPlayer;
	LoadMonsterData();
	SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
	ActiveMonsterCount = 0;
	LevelMonsterTypes[0].type = MT_NZOMBIE;
	Monster &monster = Monsters[0];
	monster = {};
	monster.levelType = 0;
	monster.talkMsg = TEXT_NONE;
	monster.mode = MonsterMode::Stand;
	monster.position.tile = { 41, 40 };
	monster.maxHitPoints = monster.hitPoints = Fixed26_6::fromInt(1000);

	// The thorns show on the monster they hurt (the effects need a screen).
	HeadlessMode = false;
	vfx::Clear();

	// No thorns, no damage.
	DealThorns(player, monster);
	EXPECT_EQ(monster.hitPoints.whole(), 1000);
	EXPECT_EQ(vfx::ThornsCueCount(), 0U);

	SetGearStat(player, Poe2ItemStat::ThornsMin, 6);
	SetGearStat(player, Poe2ItemStat::ThornsMax, 9);
	EXPECT_EQ(ThornsDamage(player), (std::pair { 6, 9 }));
	SetRndSeed(42);
	DealThorns(player, monster);
	const int dealt = 1000 - monster.hitPoints.whole();
	EXPECT_GE(dealt, 6);
	EXPECT_LE(dealt, 9);
	EXPECT_EQ(vfx::ThornsCueCount(), 1U);
	vfx::Clear();
	HeadlessMode = true;
}

TEST_F(Poe2EffectPrimitivesTest, KillsRecoverFlatAndPercentLifeAndMana)
{
	Item item {};
	ApplyModEffect(item, ModEffect::LifePercentOnKill, 5, 0);
	ApplyModEffect(item, ModEffect::ManaPercentOnKill, 4, 0);
	EXPECT_EQ(Stat(item, Poe2ItemStat::LifePercentOnKill), 5);
	EXPECT_EQ(Stat(item, Poe2ItemStat::ManaPercentOnKill), 4);

	Player &player = *MyPlayer;
	// The flat amounts (Io and Tir runes) are the existing LifeOnKill and ManaOnKill.
	SetGearStat(player, Poe2ItemStat::LifeOnKill, 15);
	SetGearStat(player, Poe2ItemStat::ManaOnKill, 10);
	SetGearStat(player, Poe2ItemStat::LifePercentOnKill, 5);
	SetGearStat(player, Poe2ItemStat::ManaPercentOnKill, 4);
	RecoverOnKill(player);
	EXPECT_EQ(Life(player), 100 + 15 + 50);
	EXPECT_EQ(Mana(player), 10 + 40);
}

TEST_F(Poe2EffectPrimitivesTest, DamageGainedAsExtraElementAddsToTheShrines)
{
	Item item {};
	ApplyModEffect(item, ModEffect::FireAsExtra, 6, 0);
	ApplyModEffect(item, ModEffect::LightningAsExtra, 8, 0);
	ApplyModEffect(item, ModEffect::ColdAsExtra, 10, 0);
	EXPECT_EQ(Stat(item, Poe2ItemStat::FireAsExtra), 6);
	EXPECT_EQ(Stat(item, Poe2ItemStat::LightningAsExtra), 8);
	EXPECT_EQ(Stat(item, Poe2ItemStat::ColdAsExtra), 10);

	Player &player = *MyPlayer;
	SetGearStat(player, Poe2ItemStat::FireAsExtra, 6);
	SetGearStat(player, Poe2ItemStat::LightningAsExtra, 8);
	SetGearStat(player, Poe2ItemStat::ColdAsExtra, 10);
	EXPECT_EQ(GainAsExtraPercent(player, DamageType::Fire), 6);
	EXPECT_EQ(GainAsExtraPercent(player, DamageType::Lightning), 8);
	EXPECT_EQ(GainAsExtraPercent(player, DamageType::Cold), 10);
	EXPECT_EQ(GainAsExtraPercent(player, DamageType::Physical), 0);
}

TEST_F(Poe2EffectPrimitivesTest, AddedColdIsARangeForMeleeHits)
{
	Item item {};
	ApplyModEffect(item, ModEffect::AddedCold, 3, 5);
	ApplyModEffect(item, ModEffect::AddedCold, 6, 10);
	EXPECT_EQ(Stat(item, Poe2ItemStat::AddedColdMin), 9);
	EXPECT_EQ(Stat(item, Poe2ItemStat::AddedColdMax), 15);

	Player &player = *MyPlayer;
	WearRingWith(player, ModEffect::AddedCold, 3, 5);
	EXPECT_EQ(AddedColdDamage(player), (std::pair { 3, 5 }));
	// The weapon explosion it makes is cold, which is magic resistance's type, apart from fire (1) and lightning.
	EXPECT_NE(WeaponExplosionCold, 1);
	EXPECT_EQ(static_cast<int>(DamageType::Cold), 3);
}

TEST_F(Poe2EffectPrimitivesTest, FlaskRecoveryJoinsTheTreesTotal)
{
	Player &player = *MyPlayer;
	WearRingWith(player, ModEffect::FlaskRecovery, 20);
	EXPECT_EQ(player.poe2.itemStats[static_cast<size_t>(Poe2ItemStat::FlaskRecovery)], 20);
	EXPECT_EQ(PassiveTotal(player, PassiveStat::FlaskRecovery), 20);
}

TEST_F(Poe2EffectPrimitivesTest, StunBuildupJoinsTheTreesTotal)
{
	Player &player = *MyPlayer;
	const int before = UniqueStunBuildupPercent(player);
	WearRingWith(player, ModEffect::StunBuildup, 30);
	EXPECT_EQ(PassiveTotal(player, PassiveStat::StunBuildup), 30);
	EXPECT_EQ(UniqueStunBuildupPercent(player), before + 30);
}

TEST_F(Poe2EffectPrimitivesTest, StunThresholdAddsToLifeAndTakesAShareOfEnergyShield)
{
	Item item {};
	ApplyModEffect(item, ModEffect::StunThreshold, 50, 0);
	ApplyModEffect(item, ModEffect::EnergyShieldAsStunThreshold, 12, 0);
	EXPECT_EQ(Stat(item, Poe2ItemStat::StunThreshold), 50);
	EXPECT_EQ(Stat(item, Poe2ItemStat::EnergyShieldAsStunThreshold), 12);

	Player &player = *MyPlayer;
	const int32_t hit = Fixed26_6::fromInt(500).raw();
	// Without gear threshold, the stun damage is the hit over the threshold percent, as before.
	EXPECT_EQ(StunDamageAgainstLife(player, hit, 100), hit);
	EXPECT_EQ(StunDamageAgainstLife(player, hit, 125), hit * 100 / 125);

	// +1000 stun threshold doubles the hero's 1000 life: half the stun damage.
	SetGearStat(player, Poe2ItemStat::StunThreshold, 1000);
	EXPECT_EQ(StunThresholdBonusRaw(player), Fixed26_6::fromInt(1000).raw());
	EXPECT_EQ(StunDamageAgainstLife(player, hit, 100), hit / 2);

	// 50% of 2000 energy shield is another 1000.
	SetGearStat(player, Poe2ItemStat::StunThreshold, 0);
	SetGearStat(player, Poe2ItemStat::EnergyShieldAsStunThreshold, 50);
	player.poe2.maxEnergyShield = 2000;
	EXPECT_EQ(StunThresholdBonusRaw(player), Fixed26_6::fromInt(1000).raw());
	EXPECT_EQ(StunDamageAgainstLife(player, hit, 100), hit / 2);
}

TEST_F(Poe2EffectPrimitivesTest, ArmourAppliesToOneElement)
{
	Item item {};
	ApplyModEffect(item, ModEffect::ArmourToFire, 40, 0);
	ApplyModEffect(item, ModEffect::ArmourToLightning, 30, 0);
	ApplyModEffect(item, ModEffect::ArmourToCold, 20, 0);
	EXPECT_EQ(Stat(item, Poe2ItemStat::ArmourToFire), 40);
	EXPECT_EQ(Stat(item, Poe2ItemStat::ArmourToLightning), 30);
	EXPECT_EQ(Stat(item, Poe2ItemStat::ArmourToCold), 20);

	Player &player = *MyPlayer;
	player._pIAC = 2000;
	player._pMaxHP = Fixed26_6::fromInt(100000);
	const Fixed26_6 hit = Fixed26_6::fromInt(100);
	const Fixed26_6 fireBefore = ApplyUniqueHitTaken(player, hit, DamageType::Fire);
	EXPECT_EQ(fireBefore, hit);

	SetGearStat(player, Poe2ItemStat::ArmourToFire, 40);
	EXPECT_EQ(ArmourAppliesToElementPercent(player, DamageType::Fire), 40);
	EXPECT_EQ(ArmourAppliesToElementPercent(player, DamageType::Lightning), 0);
	EXPECT_EQ(ArmourAppliesToElementPercent(player, DamageType::Physical), 0);
	EXPECT_LT(ApplyUniqueHitTaken(player, hit, DamageType::Fire), hit);
	EXPECT_EQ(ApplyUniqueHitTaken(player, hit, DamageType::Lightning), hit);

	// A unique's armour against all elements adds to it.
	player.poe2.uniqueRules[static_cast<size_t>(UniqueRule::ArmourVsElements)] = 20;
	EXPECT_EQ(ArmourAppliesToElementPercent(player, DamageType::Fire), 60);
	EXPECT_EQ(ArmourAppliesToElementPercent(player, DamageType::Cold), 20);
}

TEST_F(Poe2EffectPrimitivesTest, RecoupReturnsAShareOfHitsOverFourSeconds)
{
	Item item {};
	ApplyModEffect(item, ModEffect::LifeRecoup, 10, 0);
	EXPECT_EQ(Stat(item, Poe2ItemStat::LifeRecoup), 10);

	Player &player = *MyPlayer;
	player._pMaxHP = Fixed26_6::fromInt(100000);
	// Nothing without the stat.
	ApplyUniqueHitTaken(player, Fixed26_6::fromInt(400), DamageType::Physical);
	EXPECT_EQ(player.poe2.uniqueState.recoupPool, 0);

	SetGearStat(player, Poe2ItemStat::LifeRecoup, 10);
	const Fixed26_6 taken = ApplyUniqueHitTaken(player, Fixed26_6::fromInt(400), DamageType::Physical);
	EXPECT_EQ(player.poe2.uniqueState.recoupPool, taken.raw() / 10);
	const int32_t start = player._pHitPoints.raw();
	ProcessRecoup(player);
	// Evenly: the first tick pays 1/80th.
	EXPECT_EQ(player._pHitPoints.raw() - start, taken.raw() / 10 / (RecoupSeconds * 20));
	for (int tick = 1; tick < RecoupSeconds * 20; tick++)
		ProcessRecoup(player);
	EXPECT_EQ(player._pHitPoints.raw() - start, taken.raw() / 10);
	EXPECT_EQ(player.poe2.uniqueState.recoupTicks, 0);
	ProcessRecoup(player);
	EXPECT_EQ(player._pHitPoints.raw() - start, taken.raw() / 10);
}

TEST_F(Poe2EffectPrimitivesTest, EnergyShieldRechargesFaster)
{
	Player &player = *MyPlayer;
	EXPECT_EQ(EnergyShieldRechargePercent(player), 100);
	WearRingWith(player, ModEffect::EnergyShieldRecharge, 12);
	EXPECT_EQ(PassiveTotal(player, PassiveStat::EnergyShieldRecharge), 12);
	EXPECT_EQ(EnergyShieldRechargePercent(player), 112);
}

TEST_F(Poe2EffectPrimitivesTest, CastSpeedFromGearIsTheExistingStat)
{
	Player &player = *MyPlayer;
	WearRingWith(player, ModEffect::CastSpeed, 8);
	EXPECT_EQ(player.poe2.itemStats[static_cast<size_t>(Poe2ItemStat::CastSpeed)], 8);
}

TEST_F(Poe2EffectPrimitivesTest, SpellDamageFromGearJoinsTheTreesRule)
{
	Player &player = *MyPlayer;
	WearRingWith(player, ModEffect::SpellDamage, 25);
	EXPECT_EQ(player.poe2.rule(UniqueRule::SpellDamage), 25);
}

TEST_F(Poe2EffectPrimitivesTest, SkillTypeLevelsGoToSkillsWithTheirTags)
{
	struct Case {
		ModEffect effect;
		Poe2ItemStat stat;
	};
	for (const Case &c : { Case { ModEffect::StrikeSkillLevels, Poe2ItemStat::StrikeSkillLevels }, Case { ModEffect::SlamSkillLevels, Poe2ItemStat::SlamSkillLevels },
	         Case { ModEffect::WarcrySkillLevels, Poe2ItemStat::WarcrySkillLevels }, Case { ModEffect::CurseSkillLevels, Poe2ItemStat::CurseSkillLevels },
	         Case { ModEffect::HeraldSkillLevels, Poe2ItemStat::HeraldSkillLevels }, Case { ModEffect::PlacedSkillLevels, Poe2ItemStat::PlacedSkillLevels },
	         Case { ModEffect::BowSkillLevels, Poe2ItemStat::BowSkillLevels }, Case { ModEffect::AreaSpellSkillLevels, Poe2ItemStat::AreaSpellSkillLevels },
	         Case { ModEffect::ElementalSpellSkillLevels, Poe2ItemStat::ElementalSpellSkillLevels } }) {
		Item item {};
		ApplyModEffect(item, c.effect, 1, 0);
		EXPECT_EQ(Stat(item, c.stat), 1) << static_cast<int>(c.effect);
	}

	Player &player = *MyPlayer;
	const Poe2PlayerData &data = player.poe2;
	const SkillSlot cleave = SkillSlot::ForAttack(WeaponSkill::Cleave);
	const SkillSlot slam = SkillSlot::ForAttack(WeaponSkill::GroundSlam);
	const SkillSlot battleCry = SkillSlot::ForAttack(WeaponSkill::BattleCry);
	const SkillSlot barrage = SkillSlot::ForAttack(WeaponSkill::Barrage);
	const SkillSlot firebolt = SkillSlot::ForSpell(SpellID::Firebolt);
	const SkillSlot fireWall = SkillSlot::ForSpell(SpellID::FireWall);
	const SkillSlot nova = SkillSlot::ForSpell(SpellID::Nova);
	const SkillSlot bloodStar = SkillSlot::ForSpell(SpellID::BloodStar);
	const SkillSlot herald = SkillSlot::ForPersistent(PersistentSkill::HeraldOfAsh);
	const auto levels = [&](SkillSlot skill) { return ItemSkillLevelBonus(data, skill); };
	for (const SkillSlot skill : { cleave, slam, battleCry, barrage, firebolt, fireWall, nova, bloodStar, herald })
		EXPECT_EQ(levels(skill), 0);

	SetGearStat(player, Poe2ItemStat::StrikeSkillLevels, 1);
	EXPECT_EQ(levels(cleave), 1);
	EXPECT_EQ(levels(slam), 0);
	SetGearStat(player, Poe2ItemStat::StrikeSkillLevels, 0);

	SetGearStat(player, Poe2ItemStat::SlamSkillLevels, 1);
	EXPECT_EQ(levels(slam), 1);
	EXPECT_EQ(levels(cleave), 0);
	SetGearStat(player, Poe2ItemStat::SlamSkillLevels, 0);

	SetGearStat(player, Poe2ItemStat::WarcrySkillLevels, 1);
	EXPECT_EQ(levels(battleCry), 1);
	EXPECT_EQ(levels(cleave), 0);
	SetGearStat(player, Poe2ItemStat::WarcrySkillLevels, 0);

	SetGearStat(player, Poe2ItemStat::BowSkillLevels, 2);
	EXPECT_EQ(levels(barrage), 2);
	EXPECT_EQ(levels(SkillSlot::ForDefault(static_cast<uint8_t>(DefaultAttack::BowShot))), 2);
	EXPECT_EQ(levels(cleave), 0);
	SetGearStat(player, Poe2ItemStat::BowSkillLevels, 0);

	SetGearStat(player, Poe2ItemStat::PlacedSkillLevels, 1);
	EXPECT_EQ(levels(fireWall), 1);
	EXPECT_EQ(levels(firebolt), 0);
	SetGearStat(player, Poe2ItemStat::PlacedSkillLevels, 0);

	SetGearStat(player, Poe2ItemStat::HeraldSkillLevels, 1);
	EXPECT_EQ(levels(herald), 1);
	EXPECT_EQ(levels(nova), 0);
	SetGearStat(player, Poe2ItemStat::HeraldSkillLevels, 0);

	// Area spells need both tags: Nova is one, Firebolt isn't, and a herald's area isn't a spell.
	SetGearStat(player, Poe2ItemStat::AreaSpellSkillLevels, 1);
	EXPECT_EQ(levels(nova), 1);
	EXPECT_EQ(levels(firebolt), 0);
	EXPECT_EQ(levels(herald), 0);
	SetGearStat(player, Poe2ItemStat::AreaSpellSkillLevels, 0);

	// Elemental spells: fire, lightning or cold; Blood Star is physical.
	SetGearStat(player, Poe2ItemStat::ElementalSpellSkillLevels, 1);
	EXPECT_EQ(levels(firebolt), 1);
	EXPECT_EQ(levels(nova), 1);
	EXPECT_EQ(levels(bloodStar), 0);
	EXPECT_EQ(levels(cleave), 0);
	SetGearStat(player, Poe2ItemStat::ElementalSpellSkillLevels, 0);

	SetGearStat(player, Poe2ItemStat::CurseSkillLevels, 1);
	EXPECT_EQ(levels(SkillSlot::ForAttack(WeaponSkill::AmplifyDamage)), 1);
	EXPECT_EQ(levels(cleave), 0);
}

TEST_F(Poe2EffectPrimitivesTest, RequirementsConvertToOneAttribute)
{
	Item item {};
	item._iMinStr = 100;
	item._iMinDex = 60;
	item._iMinMag = 40;
	ApplyModEffect(item, ModEffect::RequirementsToStrength, 40, 0);
	EXPECT_EQ(item._iMinStr, 100 + 24 + 16);
	EXPECT_EQ(item._iMinDex, 36);
	EXPECT_EQ(item._iMinMag, 24);

	Item dexterous {};
	dexterous._iMinStr = 50;
	dexterous._iMinMag = 50;
	ApplyModEffect(dexterous, ModEffect::RequirementsToDexterity, 40, 0);
	EXPECT_EQ(dexterous._iMinDex, 40);
	EXPECT_EQ(dexterous._iMinStr, 30);
	EXPECT_EQ(dexterous._iMinMag, 30);

	Item clever {};
	clever._iMinStr = 200;
	clever._iMinDex = 200;
	clever._iMinMag = 200;
	ApplyModEffect(clever, ModEffect::RequirementsToIntelligence, 40, 0);
	// The requirement field stops at 255.
	EXPECT_EQ(clever._iMinMag, 255);
	EXPECT_EQ(clever._iMinStr, 120);
	EXPECT_EQ(clever._iMinDex, 120);
}

TEST_F(Poe2EffectPrimitivesTest, OutsidePoe2TheyDoNothing)
{
	Player &player = *MyPlayer;
	SetGearStat(player, Poe2ItemStat::LifeLeech, 3);
	SetGearStat(player, Poe2ItemStat::ThornsMax, 9);
	SetGearStat(player, Poe2ItemStat::AddedColdMax, 9);
	SetGearStat(player, Poe2ItemStat::ArmourToFire, 40);
	SetGearStat(player, Poe2ItemStat::StunThreshold, 100);
	SetGearStat(player, Poe2ItemStat::LifeRecoup, 10);
	SetGearStat(player, Poe2ItemStat::FireAsExtra, 10);
	SetGearStat(player, Poe2ItemStat::LifePercentOnKill, 5);
	gbIsPoe2 = false;
	EXPECT_EQ(GearLeechPercent(player, false), 0);
	EXPECT_EQ(ThornsDamage(player), (std::pair { 0, 0 }));
	EXPECT_EQ(AddedColdDamage(player), (std::pair { 0, 0 }));
	EXPECT_EQ(ArmourAppliesToElementPercent(player, DamageType::Fire), 0);
	EXPECT_EQ(StunThresholdBonusRaw(player), 0);
	EXPECT_EQ(LifeRecoupPercent(player), 0);
	EXPECT_EQ(GainAsExtraPercent(player, DamageType::Fire), 0);
	EXPECT_EQ(PercentOnKill(player, false), 0);
	LeechFromHit(player, SkillSlot::ForAttack(WeaponSkill::Cleave), Fixed26_6::fromInt(1000).raw());
	EXPECT_EQ(Life(player), 100);
	gbIsPoe2 = true;
}

TEST_F(Poe2EffectPrimitivesTest, NewEffectsReadFromTheTables)
{
	for (const std::string_view name : { "LifeLeech", "ManaLeech", "Thorns", "LifePercentOnKill", "ManaPercentOnKill", "FireAsExtra", "LightningAsExtra", "ColdAsExtra",
	         "AddedCold", "FlaskRecovery", "StunBuildup", "StunThreshold", "EnergyShieldAsStunThreshold", "ArmourToFire", "ArmourToLightning", "ArmourToCold",
	         "LifeRecoup", "EnergyShieldRecharge", "StrikeSkillLevels", "SlamSkillLevels", "WarcrySkillLevels", "CurseSkillLevels", "HeraldSkillLevels",
	         "PlacedSkillLevels", "BowSkillLevels", "AreaSpellSkillLevels", "ElementalSpellSkillLevels", "RequirementsToStrength", "RequirementsToDexterity",
	         "RequirementsToIntelligence", "PoisonMagnitude", "BleedMagnitude", "ElementalAttackDamage", "AttackElementalPenetration", "MaxFireResistance",
	         "MaxLightningResistance", "MaxColdResistance", "SkillDurationAndCooldown", "ManaPerArmour", "ArmourFromRecentHitLoss", "PreventedPhysicalRecoup" }) {
		const std::optional<ModEffect> effect = ParseModEffectName(name);
		ASSERT_TRUE(effect.has_value()) << name;
		EXPECT_TRUE(ModEffectBits(*effect).any()) << name;
	}
}

// The soulstones' primitives (build spec step 6).

TEST_F(Poe2EffectPrimitivesTest, PoisonsAndBleedsGrowWithMagnitude)
{
	Item item {};
	ApplyModEffect(item, ModEffect::PoisonMagnitude, 25, 0);
	ApplyModEffect(item, ModEffect::BleedMagnitude, 40, 0);
	EXPECT_EQ(Stat(item, Poe2ItemStat::PoisonMagnitude), 25);
	EXPECT_EQ(Stat(item, Poe2ItemStat::BleedMagnitude), 40);

	Player &player = *MyPlayer;
	EXPECT_EQ(PoisonMagnitudePercent(player), 100);
	EXPECT_EQ(BleedMagnitudePercent(player), 100);
	SetGearStat(player, Poe2ItemStat::PoisonMagnitude, 25);
	SetGearStat(player, Poe2ItemStat::BleedMagnitude, 40);
	EXPECT_EQ(PoisonMagnitudePercent(player), 125);
	EXPECT_EQ(BleedMagnitudePercent(player), 140);
}

TEST_F(Poe2EffectPrimitivesTest, AttacksGetElementalDamageAndPenetration)
{
	Item item {};
	ApplyModEffect(item, ModEffect::ElementalAttackDamage, 40, 0);
	ApplyModEffect(item, ModEffect::AttackElementalPenetration, 25, 0);
	EXPECT_EQ(Stat(item, Poe2ItemStat::ElementalAttackDamage), 40);
	EXPECT_EQ(Stat(item, Poe2ItemStat::AttackElementalPenetration), 25);

	Player &player = *MyPlayer;
	SetGearStat(player, Poe2ItemStat::ElementalAttackDamage, 40);
	SetGearStat(player, Poe2ItemStat::AttackElementalPenetration, 25);
	EXPECT_EQ(GearElementalAttackDamage(player), 40);
	for (const DamageType type : { DamageType::Fire, DamageType::Lightning, DamageType::Cold })
		EXPECT_EQ(GearAttackPenetration(player, type), 25);
	EXPECT_EQ(GearAttackPenetration(player, DamageType::Physical), 0);
	EXPECT_EQ(GearAttackPenetration(player, DamageType::Chaos), 0);
	// Attack hits get past the resistance; spell hits don't.
	player.poe2.uniqueState.lastHitBySpell = false;
	EXPECT_EQ(HitPenetration(DamageType::Fire), 25);
	player.poe2.uniqueState.lastHitBySpell = true;
	EXPECT_EQ(HitPenetration(DamageType::Fire), 0);
}

TEST_F(Poe2EffectPrimitivesTest, MaximumResistancesRiseByElement)
{
	Player &player = *MyPlayer;
	CalcPlrItemVals(player, false);
	const std::array<int16_t, 3> before = player.poe2.maxResistances;
	WearRingWith(player, ModEffect::MaxFireResistance, 2);
	EXPECT_EQ(player.poe2.maxResistances[0], before[0] + 2);
	EXPECT_EQ(player.poe2.maxResistances[1], before[1]);
	EXPECT_EQ(player.poe2.maxResistances[Poe2PlayerData::ColdResistanceIndex], before[Poe2PlayerData::ColdResistanceIndex]);
	WearRingWith(player, ModEffect::MaxColdResistance, 2);
	EXPECT_EQ(player.poe2.maxResistances[0], before[0]);
	EXPECT_EQ(player.poe2.maxResistances[Poe2PlayerData::ColdResistanceIndex], before[Poe2PlayerData::ColdResistanceIndex] + 2);
	WearRingWith(player, ModEffect::MaxLightningResistance, 2);
	EXPECT_EQ(player.poe2.maxResistances[1], before[1] + 2);
}

TEST_F(Poe2EffectPrimitivesTest, SkillDurationAndCooldownRecovery)
{
	Player &player = *MyPlayer;
	EXPECT_EQ(SkillDurationPercent(player), 100);
	EXPECT_EQ(CooldownTicks(player, 100), 100);
	SetGearStat(player, Poe2ItemStat::SkillDurationAndCooldown, 8);
	EXPECT_EQ(SkillDurationPercent(player), 108);
	// 8% faster recovery: 100 ticks pass in 92.
	EXPECT_EQ(CooldownTicks(player, 100), 92);
	EXPECT_EQ(CooldownTicks(player, 1), 1);
	EXPECT_EQ(CooldownTicks(player, 0), 0);
}

TEST_F(Poe2EffectPrimitivesTest, AHelmetsArmourGivesMana)
{
	EXPECT_EQ(ManaFromItemArmour(3, 100), 33);
	EXPECT_EQ(ManaFromItemArmour(0, 100), 0);
	EXPECT_EQ(ManaFromItemArmour(3, 0), 0);

	Player &player = *MyPlayer;
	Item &helm = player.InvBody[INVLOC_HEAD];
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (AllItemsList[i].iName == "Iron Cap") {
			InitializeItem(helm, static_cast<_item_indexes>(i));
			break;
		}
	}
	ASSERT_EQ(helm._itype, ItemType::Helm);
	helm._iAC = 90;
	helm._iStatFlag = true;
	helm._iIdentified = true;
	CalcPlrItemVals(player, false);
	const Fixed26_6 before = player._pMaxMana;
	DefensePoints base;
	DefensePoints bonus;
	ApplyModEffect(helm, ModEffect::ManaPerArmour, 3, 0);
	SplitItemDefense(helm, base, bonus);
	ASSERT_GT(base.armour + bonus.armour, 0);
	CalcPlrItemVals(player, false);
	EXPECT_EQ((player._pMaxMana - before).whole(), (base.armour + bonus.armour) / 3);
}

TEST_F(Poe2EffectPrimitivesTest, ArmourFromLifeLostToHitsLately)
{
	Item item {};
	ApplyModEffect(item, ModEffect::ArmourFromRecentHitLoss, 35, 0);
	EXPECT_EQ(Stat(item, Poe2ItemStat::ArmourFromRecentHitLoss), 35);

	Player &player = *MyPlayer;
	player._pIAC = 100;
	// What reaches life after the shield counts.
	AbsorbWithEnergyShield(player, Fixed26_6::fromInt(200));
	EXPECT_EQ(player.poe2.uniqueState.hitLossSeconds[0], Fixed26_6::fromInt(200).raw());
	EXPECT_EQ(RecentHitLossArmour(player), 0) << "Nothing without the stat";
	const int before = ArmourRating(player);
	SetGearStat(player, Poe2ItemStat::ArmourFromRecentHitLoss, 35);
	EXPECT_EQ(RecentHitLossArmour(player), 70);
	EXPECT_EQ(ArmourRating(player), before + 70);
	// Eight seconds on, it has gone.
	for (int tick = 0; tick < (RecentHitLossSeconds - 1) * 20; tick++)
		ProcessRecentHitLoss(player);
	EXPECT_EQ(RecentHitLossArmour(player), 70);
	for (int tick = 0; tick < 20; tick++)
		ProcessRecentHitLoss(player);
	EXPECT_EQ(RecentHitLossArmour(player), 0);
}

TEST_F(Poe2EffectPrimitivesTest, PhysicalDamageArmourStopsIsRecouped)
{
	Item item {};
	ApplyModEffect(item, ModEffect::PreventedPhysicalRecoup, 10, 0);
	EXPECT_EQ(Stat(item, Poe2ItemStat::PreventedPhysicalRecoup), 10);

	Player &player = *MyPlayer;
	player._pIAC = 2000;
	const Fixed26_6 hit = Fixed26_6::fromInt(100);
	ApplyArmour(player, hit);
	EXPECT_EQ(player.poe2.uniqueState.recoupPool, 0);
	SetGearStat(player, Poe2ItemStat::PreventedPhysicalRecoup, 10);
	const Fixed26_6 taken = ApplyArmour(player, hit);
	ASSERT_LT(taken, hit);
	EXPECT_EQ(player.poe2.uniqueState.recoupPool, (hit.raw() - taken.raw()) / 10);
	EXPECT_EQ(player.poe2.uniqueState.recoupTicks, RecoupSeconds * 20);
}

} // namespace
} // namespace devilution
