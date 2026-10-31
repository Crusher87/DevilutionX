/**
 * @file poe2_caster_stats_test.cpp
 *
 * The caster weapons rework's modifier effects (45 to 62) and the stats they give, on hand-built items: per-element
 * spell damage and levels, minion levels, life and resistances, life and mana on kill, a sceptre's own Spirit, and
 * the element tags that first-element exclusivity narrows.
 */
#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "monster.h"
#include "player.h"
#include "poe2/flasks.hpp"
#include "poe2/item_stats.hpp"
#include "poe2/minion_gems.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/monster_resistance.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/skill_details.hpp"
#include "poe2/support_table.hpp"
#include "poe2/spirit.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/weapon_kinds.hpp"
#include "spells.h"
#include "tables/misdat.h"
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

class Poe2CasterStatsTest : public ::testing::Test {
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
		LoadPassiveTreeData();
		LoadModifierData();
		MyPlayer->_pClass = HeroClass::Sorcerer;
		MyPlayer->setCharacterLevel(10);
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		LoadModifierData();
		UnloadModArchives();
	}

	static _item_indexes FindBase(std::string_view name)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == name)
				return static_cast<_item_indexes>(i);
		}
		ADD_FAILURE() << name;
		return IDI_GOLD;
	}
};

TEST_F(Poe2CasterStatsTest, NewEffectsGiveTheirStats)
{
	struct Case {
		ModEffect effect;
		Poe2ItemStat stat;
	};
	for (const Case &c : { Case { ModEffect::LocalSpiritPercent, Poe2ItemStat::LocalSpiritPercent }, Case { ModEffect::FireSpellDamage, Poe2ItemStat::FireSpellDamage },
	         Case { ModEffect::ColdSpellDamage, Poe2ItemStat::ColdSpellDamage }, Case { ModEffect::LightningSpellDamage, Poe2ItemStat::LightningSpellDamage },
	         Case { ModEffect::PhysicalSpellDamage, Poe2ItemStat::PhysicalSpellDamage }, Case { ModEffect::ColdSkillLevels, Poe2ItemStat::ColdSkillLevels },
	         Case { ModEffect::PhysicalSkillLevels, Poe2ItemStat::PhysicalSkillLevels }, Case { ModEffect::MinionSkillLevels, Poe2ItemStat::MinionSkillLevels },
	         Case { ModEffect::MinionLife, Poe2ItemStat::MinionLife }, Case { ModEffect::MinionResistances, Poe2ItemStat::MinionResistances },
	         Case { ModEffect::LifeOnKill, Poe2ItemStat::LifeOnKill }, Case { ModEffect::ManaOnKill, Poe2ItemStat::ManaOnKill },
	         Case { ModEffect::BlockChance, Poe2ItemStat::BlockChance }, Case { ModEffect::DazeChance, Poe2ItemStat::DazeChance },
	         Case { ModEffect::MinionLifeRegen, Poe2ItemStat::MinionLifeRegen }, Case { ModEffect::ChaosSpellDamage, Poe2ItemStat::ChaosSpellDamage },
	         Case { ModEffect::ChaosSkillLevels, Poe2ItemStat::ChaosSkillLevels } }) {
		Item item {};
		ApplyModEffect(item, c.effect, 7, 0);
		ApplyModEffect(item, c.effect, 5, 0);
		EXPECT_EQ(Stat(item, c.stat), 12) << static_cast<int>(c.effect);
		EXPECT_EQ(item._iPLMana.whole(), 0);
	}

	// The hybrids: their second value is maximum mana.
	Item sceptre {};
	ApplyModEffect(sceptre, ModEffect::SpiritAndMana, 30, 40);
	EXPECT_EQ(Stat(sceptre, Poe2ItemStat::LocalSpiritPercent), 30);
	EXPECT_EQ(sceptre._iPLMana.whole(), 40);
	Item staff {};
	ApplyModEffect(staff, ModEffect::SpellDamageAndMana, 45, 83);
	EXPECT_EQ(Stat(staff, Poe2ItemStat::SpellDamage), 45);
	EXPECT_EQ(staff._iPLMana.whole(), 83);

	// The ally added-damage families share one range.
	Item allies {};
	ApplyModEffect(allies, ModEffect::MinionAddedDamage, 3, 5);
	ApplyModEffect(allies, ModEffect::MinionAddedDamage, 10, 14);
	EXPECT_EQ(Stat(allies, Poe2ItemStat::MinionAddedDamageMin), 13);
	EXPECT_EQ(Stat(allies, Poe2ItemStat::MinionAddedDamageMax), 19);
}

TEST_F(Poe2CasterStatsTest, HybridsAvoidBothParts)
{
	EXPECT_EQ(ModEffectBits(ModEffect::SpiritAndMana), ModEffectBits(ModEffect::LocalSpiritPercent) | ModEffectBits(ModEffect::Mana));
	EXPECT_EQ(ModEffectBits(ModEffect::SpellDamageAndMana), ModEffectBits(ModEffect::SpellDamage) | ModEffectBits(ModEffect::Mana));
	EXPECT_TRUE(ModEffectBits(ModEffect::MinionLifeRegen).any());
	EXPECT_LT(static_cast<size_t>(ModEffect::MinionLifeRegen), MaxModEffects);
}

TEST_F(Poe2CasterStatsTest, ElementLevelsGoToTheSpellsOfTheirElement)
{
	Player &player = *MyPlayer;
	LoadMissileData();
	SetGearStat(player, Poe2ItemStat::FireSkillLevels, 1);
	SetGearStat(player, Poe2ItemStat::LightningSkillLevels, 2);
	SetGearStat(player, Poe2ItemStat::ColdSkillLevels, 4);
	SetGearStat(player, Poe2ItemStat::PhysicalSkillLevels, 8);
	SetGearStat(player, Poe2ItemStat::ChaosSkillLevels, 16);
	const auto levels = [&player](SpellID spell) { return ItemSkillLevelBonus(player.poe2, SkillSlot::ForSpell(spell)); };
	int cold = 0;
	for (size_t i = 1; i < SpellsData.size(); i++) {
		const auto spell = static_cast<SpellID>(i);
		const int bonus = levels(spell);
		// A damaging spell takes the levels of the type its hits deal; one without damage its school's.
		DamageType type = DamageType::Cold;
		if ((SkillTags(SkillSlot::ForSpell(spell)) & ST_Damage) != 0)
			type = SpellDamageType(spell);
		else if (GetSpellData(spell).type() == MagicType::Fire)
			type = DamageType::Fire;
		else if (GetSpellData(spell).type() == MagicType::Lightning)
			type = DamageType::Lightning;
		const int expected = type == DamageType::Fire ? 1 : type == DamageType::Lightning ? 2 : type == DamageType::Physical ? 8 : type == DamageType::Chaos ? 16 : 4;
		EXPECT_EQ(bonus, expected) << i;
		if (expected == 4)
			cold++;
	}
	EXPECT_GT(cold, 0);
	EXPECT_EQ(levels(SpellID::Firebolt), 1);
	// Nova and Flash hit as lightning, Holy Bolt as fire, Blood Star as physical and Bone Spirit as chaos.
	EXPECT_EQ(levels(SpellID::Nova), 2);
	EXPECT_EQ(levels(SpellID::Flash), 2);
	EXPECT_EQ(levels(SpellID::HolyBolt), 1);
	EXPECT_EQ(levels(SpellID::Apocalypse), 1);
	EXPECT_EQ(levels(SpellID::BloodStar), 8);
	EXPECT_EQ(levels(SpellID::BoneSpirit), 16);
	// Stone Curse and Teleport deal no damage and keep their school's cold levels.
	EXPECT_EQ(levels(SpellID::StoneCurse), 4);
	EXPECT_EQ(levels(SpellID::Teleport), 4);
	// Attacks keep their own levels.
	EXPECT_EQ(ItemSkillLevelBonus(player.poe2, SkillSlot::ForAttack(WeaponSkill::Cleave)), 0);
}

TEST_F(Poe2CasterStatsTest, MinionLevelsGoToTheMinionGemsOnly)
{
	Player &player = *MyPlayer;
	SetGearStat(player, Poe2ItemStat::MinionSkillLevels, 2);
	for (const PersistentSkill skill : { PersistentSkill::SkeletalWarriors, PersistentSkill::SkeletalMages, PersistentSkill::SummonGolem, PersistentSkill::WolfPack, PersistentSkill::Revive }) {
		EXPECT_TRUE(IsMinionSkill(skill)) << static_cast<int>(skill);
		EXPECT_EQ(ItemSkillLevelBonus(player.poe2, SkillSlot::ForPersistent(skill)), 2) << static_cast<int>(skill);
	}
	for (const PersistentSkill skill : { PersistentSkill::ManaShield, PersistentSkill::HeraldOfAsh, PersistentSkill::PurityOfElements }) {
		EXPECT_FALSE(IsMinionSkill(skill)) << static_cast<int>(skill);
		EXPECT_EQ(ItemSkillLevelBonus(player.poe2, SkillSlot::ForPersistent(skill)), 0) << static_cast<int>(skill);
	}
	EXPECT_EQ(ItemSkillLevelBonus(player.poe2, SkillSlot::ForSpell(SpellID::Firebolt)), 0);
}

TEST_F(Poe2CasterStatsTest, ElementSpellDamageByHitType)
{
	Player &player = *MyPlayer;
	SetGearStat(player, Poe2ItemStat::FireSpellDamage, 10);
	SetGearStat(player, Poe2ItemStat::ColdSpellDamage, 20);
	SetGearStat(player, Poe2ItemStat::LightningSpellDamage, 30);
	SetGearStat(player, Poe2ItemStat::PhysicalSpellDamage, 40);
	SetGearStat(player, Poe2ItemStat::ChaosSpellDamage, 50);
	EXPECT_EQ(GearElementSpellDamage(player, DamageType::Fire), 10);
	EXPECT_EQ(GearElementSpellDamage(player, DamageType::Cold), 20);
	EXPECT_EQ(GearElementSpellDamage(player, DamageType::Lightning), 30);
	EXPECT_EQ(GearElementSpellDamage(player, DamageType::Physical), 40);
	EXPECT_EQ(GearElementSpellDamage(player, DamageType::Chaos), 50);
}

TEST_F(Poe2CasterStatsTest, ChaosLevelsGoToTheChaosSkillsOnly)
{
	Player &player = *MyPlayer;
	SetGearStat(player, Poe2ItemStat::ChaosSkillLevels, 3);
	for (const WeaponSkill skill : { WeaponSkill::Haunt, WeaponSkill::LocustSwarm, WeaponSkill::WitheringBolt, WeaponSkill::Decompose, WeaponSkill::WitheringRay, WeaponSkill::Soulrend, WeaponSkill::DarkPact })
		EXPECT_EQ(ItemSkillLevelBonus(player.poe2, SkillSlot::ForAttack(skill)), 3) << static_cast<int>(skill);
	EXPECT_EQ(ItemSkillLevelBonus(player.poe2, SkillSlot::ForAttack(WeaponSkill::Cleave)), 0);
	EXPECT_EQ(ItemSkillLevelBonus(player.poe2, SkillSlot::ForSpell(SpellID::Firebolt)), 0);
}

TEST_F(Poe2CasterStatsTest, SceptreSpiritTakesItsOwnIncrease)
{
	Player &player = *MyPlayer;
	Item sceptre {};
	InitializeItem(sceptre, FindBase("Rattling Sceptre"));
	sceptre._iStatFlag = true;
	ASSERT_TRUE(IsAnySceptre(sceptre));
	EXPECT_EQ(SceptreSpiritOf(sceptre), SceptreSpirit);
	sceptre._iPoe2Stats[static_cast<size_t>(Poe2ItemStat::LocalSpiritPercent)] = 103;
	EXPECT_EQ(SceptreSpiritOf(sceptre), 203) << "The most a sceptre grants";
	// Unidentified, its modifiers don't count yet.
	sceptre._iMagical = ITEM_QUALITY_MAGIC;
	sceptre._iIdentified = false;
	EXPECT_EQ(SceptreSpiritOf(sceptre), SceptreSpirit);
	sceptre._iIdentified = true;

	const int bare = MaxSpirit(player);
	player.InvBody[INVLOC_HAND_LEFT] = sceptre;
	EXPECT_EQ(MaxSpirit(player), bare + 203);
	// Another hand's increase isn't this sceptre's.
	player.InvBody[INVLOC_HAND_LEFT]._iPoe2Stats = {};
	SetGearStat(player, Poe2ItemStat::LocalSpiritPercent, 103);
	EXPECT_EQ(MaxSpirit(player), bare + SceptreSpirit);
}

TEST_F(Poe2CasterStatsTest, MinionLifeAndResistances)
{
	Player &player = *MyPlayer;
	const int life = UniqueMinionLifePercent(player);
	SetGearStat(player, Poe2ItemStat::MinionLife, 25);
	EXPECT_EQ(UniqueMinionLifePercent(player), life + 25);

	LevelMonsterTypes[0].type = MT_NZOMBIE;
	Monster minion {};
	minion.levelType = 0;
	minion.flags = MFLAG_POE2_TABLES | MFLAG_GOLEM;
	minion.goalVar3 = 0;
	ASSERT_TRUE(minion.isPlayerMinion());
	Monster enemy {};
	enemy.levelType = 0;
	enemy.flags = MFLAG_POE2_TABLES;
	const int before = MonsterBaseResistance(minion, DamageType::Fire);
	const int enemyBefore = MonsterBaseResistance(enemy, DamageType::Fire);
	SetGearStat(player, Poe2ItemStat::MinionResistances, 15);
	for (const DamageType type : { DamageType::Fire, DamageType::Lightning, DamageType::Cold })
		EXPECT_EQ(MonsterBaseResistance(minion, type), before + 15);
	EXPECT_EQ(MonsterBaseResistance(minion, DamageType::Physical), 0);
	EXPECT_EQ(MonsterBaseResistance(enemy, DamageType::Fire), enemyBefore) << "Only the owner's minions";
}

TEST_F(Poe2CasterStatsTest, LifeAndManaOnKill)
{
	Player &player = *MyPlayer;
	const auto reset = [&player]() {
		player._pMaxHPBase = player._pMaxHP = Fixed26_6::fromInt(200);
		player._pHPBase = player._pHitPoints = Fixed26_6::fromInt(100);
		player._pMaxManaBase = player._pMaxMana = Fixed26_6::fromInt(200);
		player._pManaBase = player._pMana = Fixed26_6::fromInt(100);
	};
	reset();
	RecoverOnKill(player);
	const Fixed26_6 life = player._pHitPoints;
	const Fixed26_6 mana = player._pMana;
	reset();
	SetGearStat(player, Poe2ItemStat::LifeOnKill, 6);
	SetGearStat(player, Poe2ItemStat::ManaOnKill, 4);
	RecoverOnKill(player);
	EXPECT_EQ(player._pHitPoints.raw() - life.raw(), Fixed26_6::fromInt(6).raw() * LifeRecoveryPercent(player, false) / 100);
	EXPECT_EQ(player._pMana.raw() - mana.raw(), Fixed26_6::fromInt(4).raw() * ManaRecoveryPercent(player) / 100);
	EXPECT_GT(player._pHitPoints, life);
	EXPECT_GT(player._pMana, mana);
}

TEST_F(Poe2CasterStatsTest, ElementTagsNarrowOnlyWithElementTags)
{
	// Every old item rolls without element tags, so nothing narrows them.
	const std::vector<Poe2ItemMod> rolled = RollItemMods(ModSlot::BodyArmour, ModBaseArmour, 82, 6, MaxRareModsPerSide, 7, 7);
	ASSERT_FALSE(rolled.empty());
	EXPECT_EQ(NarrowedModBaseTags(ModBaseArmour, rolled), ModBaseArmour);
	EXPECT_EQ(NarrowedModBaseTags(0, rolled), 0);
	// A caster base with every element keeps them while no element modifier is rolled.
	const uint16_t all = ModBaseElementSpellBits | ModBaseArmour;
	EXPECT_EQ(NarrowedModBaseTags(all, rolled), all);
	EXPECT_EQ(NarrowedModBaseTags(ModBaseColdSpell, {}), ModBaseColdSpell);

	// The old staves and sceptres have no element tags.
	for (int row = 544; row <= 575; row++) {
		Item item {};
		InitializeItem(item, static_cast<_item_indexes>(row));
		EXPECT_EQ(ItemModBaseTags(item), 0) << AllItemsList[row].iName;
	}
}

} // namespace
} // namespace devilution
