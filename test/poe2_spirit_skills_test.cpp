#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/skill_gems.hpp"
#include "poe2/spirit.hpp"
#include "poe2/spirit_skills.hpp"
#include "poe2/support_gems.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2SpiritSkillsTest : public ::testing::Test {
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
		Missiles.clear();
	}

	void TearDown() override
	{
		ClearSpiritSkills();
		Missiles.clear();
		gbIsPoe2 = false;
		UnloadModArchives();
	}

	static void Socket(PersistentSkill skill)
	{
		MyPlayer->poe2.skillSlots[0] = SkillSlot::ForPersistent(skill);
	}
};

TEST_F(Poe2SpiritSkillsTest, EveryNewSpiritSkillHasAGemNameAndReservation)
{
	for (int i = static_cast<int>(PersistentSkill::BloodGolem); i <= static_cast<int>(PersistentSkill::Last); i++) {
		const auto skill = static_cast<PersistentSkill>(i);
		EXPECT_FALSE(PersistentSkillName(skill).empty()) << i;
		EXPECT_FALSE(PersistentSkillDescription(skill).empty()) << i;
		EXPECT_GT(SpiritReservation(skill), 0) << i;
		EXPECT_LE(SpiritReservation(skill), 100) << i;
		Item gem;
		ASSERT_TRUE(CreateSkillGemItem(gem, SkillSlot::ForPersistent(skill), 1)) << PersistentSkillName(skill);
		EXPECT_EQ(SkillForGem(gem), SkillSlot::ForPersistent(skill));
		Socket(skill);
		EXPECT_TRUE(IsPersistentSkillActive(*MyPlayer, skill)) << PersistentSkillName(skill);
	}
}

TEST_F(Poe2SpiritSkillsTest, OneGolemSkillAtATime)
{
	EXPECT_EQ(ActiveGolemSkill(*MyPlayer), PersistentSkill::None);
	MyPlayer->poe2.skillSlots[0] = SkillSlot::ForPersistent(PersistentSkill::BloodGolem);
	MyPlayer->poe2.skillSlots[1] = SkillSlot::ForPersistent(PersistentSkill::SummonGolem);
	EXPECT_EQ(ActiveGolemSkill(*MyPlayer), PersistentSkill::BloodGolem);
	EXPECT_GT(GolemLifePercent(*MyPlayer), 100);
	EXPECT_TRUE(SkillTakesSupports(SkillSlot::ForPersistent(PersistentSkill::FireGolem)));
}

TEST_F(Poe2SpiritSkillsTest, ManaShieldTakesPartOfTheHitFromMana)
{
	MyPlayer->_pMaxMana = MyPlayer->_pMana = Fixed26_6::fromInt(100);
	MyPlayer->_pMaxManaBase = MyPlayer->_pManaBase = Fixed26_6::fromInt(100);
	EXPECT_EQ(ApplySpiritHitTaken(*MyPlayer, Fixed26_6::fromInt(10)), Fixed26_6::fromInt(10));
	Socket(PersistentSkill::ManaShield);
	EXPECT_EQ(ApplySpiritHitTaken(*MyPlayer, Fixed26_6::fromInt(10)), Fixed26_6::fromInt(7));
	EXPECT_EQ(MyPlayer->_pMana, Fixed26_6::fromInt(97));
}

TEST_F(Poe2SpiritSkillsTest, BerserkTradesDefenceForDamage)
{
	Socket(PersistentSkill::Berserk);
	EXPECT_GT(SpiritAttackDamagePercent(*MyPlayer), 100);
	EXPECT_GT(ApplySpiritHitTaken(*MyPlayer, Fixed26_6::fromInt(100)), Fixed26_6::fromInt(100));
	OnSpiritMeleeHit(*MyPlayer);
	EXPECT_GT(MyPlayer->poe2.rage, 0);
}

TEST_F(Poe2SpiritSkillsTest, VizjereiFocusScalesWithMana)
{
	MyPlayer->_pMaxMana = Fixed26_6::fromInt(500);
	EXPECT_EQ(SpiritSpellDamagePercent(*MyPlayer), 100);
	EXPECT_EQ(SpiritSpellExtraManaRaw(*MyPlayer), 0);
	Socket(PersistentSkill::VizjereiFocus);
	EXPECT_EQ(SpiritSpellDamagePercent(*MyPlayer), 120);
	EXPECT_EQ(SpiritSpellExtraManaRaw(*MyPlayer), Fixed26_6::fromInt(30).raw());
}

TEST_F(Poe2SpiritSkillsTest, InfusionsAreEatenByTheirSkills)
{
	uint8_t &carried = MyPlayer->poe2.spiritState.infusions;
	EXPECT_EQ(TakeInfusion(*MyPlayer, SkillSlot::ForAttack(WeaponSkill::Meteor)), 0);
	carried = Infusion_Cold | Infusion_Fire;
	// A skill that takes none leaves them alone.
	EXPECT_EQ(TakeInfusion(*MyPlayer, SkillSlot::ForAttack(WeaponSkill::Cleave)), 0);
	// The skill's own element first, then any.
	EXPECT_EQ(TakeInfusion(*MyPlayer, SkillSlot::ForAttack(WeaponSkill::FrozenOrb)), Infusion_Cold);
	EXPECT_EQ(TakeInfusion(*MyPlayer, SkillSlot::ForAttack(WeaponSkill::Thunderstorm)), Infusion_Fire);
	EXPECT_EQ(carried, 0);
	EXPECT_EQ(InfusionDamageType(Infusion_Lightning), DamageType::Lightning);
}

TEST_F(Poe2SpiritSkillsTest, InfusionMotesComeFromAilingMonsters)
{
	Monster monster {};
	monster.poe2.igniteTicks = 10;
	size_t drops = 0;
	for (int i = 0; i < 200; i++) {
		ClearSpiritSkills();
		TryDropInfusion(monster);
		drops += InfusionMoteCount();
	}
	EXPECT_GT(drops, 0U);
	EXPECT_LT(drops, 200U);
	monster.poe2.igniteTicks = 0;
	ClearSpiritSkills();
	TryDropInfusion(monster);
	EXPECT_EQ(InfusionMoteCount(), 0U);
}

TEST_F(Poe2SpiritSkillsTest, SpiritSkillsTickInAnEmptyDungeon)
{
	leveltype = DTYPE_CATHEDRAL;
	MyPlayer->position.tile = { 40, 40 };
	for (int i = static_cast<int>(PersistentSkill::BloodGolem); i <= static_cast<int>(PersistentSkill::Last); i++) {
		Socket(static_cast<PersistentSkill>(i));
		MyPlayer->poe2.spiritState.infusions = Infusion_Fire | Infusion_Lightning | Infusion_Cold;
		for (int tick = 0; tick < 200; tick++)
			ProcessSpiritSkills(*MyPlayer);
	}
	leveltype = DTYPE_TOWN;
}

} // namespace
} // namespace devilution
