#include <set>
#include <string_view>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/gem_levels.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/spell_base.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2TableSkillsTest : public ::testing::Test {
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
		Missiles.clear();
	}

	void TearDown() override
	{
		Missiles.clear();
		ClearSkillZones();
		gbIsPoe2 = false;
		UnloadModArchives();
	}
};

TEST_F(Poe2TableSkillsTest, SpellBaseRollMatchesTheTooltipRange)
{
	Player &player = *MyPlayer;
	player._pMagic = 80;
	EXPECT_EQ(SpellBaseMin(player, 3), 15);
	EXPECT_EQ(SpellBaseMax(player, 3), 24);
	for (uint32_t seed = 1; seed < 50; seed++) {
		SetRndSeed(seed);
		const int rolled = RollSpellBase(player, 3);
		EXPECT_GE(rolled, SpellBaseMin(player, 3));
		EXPECT_LE(rolled, SpellBaseMax(player, 3));
		SetRndSeed(seed);
		EXPECT_EQ(rolled, GenerateRnd(10) + (80 / 8) + 3 + 2) << "same roll as before the helper";
	}
}

TEST_F(Poe2TableSkillsTest, EveryTableSkillHasAGem)
{
	std::set<WeaponSkill> seen;
	for (const TableSkillData &data : TableSkills()) {
		EXPECT_TRUE(seen.insert(data.skill).second) << data.name;
		EXPECT_EQ(FindTableSkill(data.skill), &data);
		const std::optional<size_t> index = SkillGemItemIndex(SkillSlot::ForAttack(data.skill));
		ASSERT_TRUE(index.has_value()) << data.name;
		EXPECT_EQ(AllItemsList[*index].iMinMLvl, data.level) << data.name;
	}
	EXPECT_EQ(TableSkills().size(), 55U);
}

TEST_F(Poe2TableSkillsTest, TheTableCoversTheRestOfTheRoster)
{
	for (int i = static_cast<int>(WeaponSkill::Whirlwind); i <= static_cast<int>(WeaponSkill::Last); i++)
		EXPECT_NE(FindTableSkill(static_cast<WeaponSkill>(i)), nullptr) << i;
	EXPECT_EQ(FindTableSkill(WeaponSkill::Cleave), nullptr);
}

TEST_F(Poe2TableSkillsTest, TableSkillsHaveNamesCostsAndCurves)
{
	for (const TableSkillData &data : TableSkills()) {
		EXPECT_EQ(WeaponSkillName(data.skill), data.name);
		EXPECT_FALSE(WeaponSkillDescription(data.skill).empty()) << data.name;
		EXPECT_GT(WeaponSkillManaCost(data.skill), 0) << data.name;
		EXPECT_EQ(WeaponSkillNeedsWeapon(data.skill), data.weapon != TableWeapon::None) << data.name;
		if (data.weapon != TableWeapon::None)
			EXPECT_FALSE(WeaponSkillWeapons(data.skill).empty()) << data.name;
		// The gem grows with its level, unless it is a flat curse or buff.
		const SkillSlot slot = SkillSlot::ForAttack(data.skill);
		EXPECT_GE(GemDamagePercent(slot, 20), GemDamagePercent(slot, 1)) << data.name;
	}
}

TEST_F(Poe2TableSkillsTest, EveryTableSkillGoesOffInAnEmptyDungeon)
{
	leveltype = DTYPE_CATHEDRAL;
	for (const TableSkillData &data : TableSkills()) {
		Missiles.clear();
		MyPlayer->position.tile = { 40, 40 };
		MyPlayer->_pdir = Direction::South;
		MyPlayer->poe2.castSkill = data.skill;
		ExecuteSkillCast(*MyPlayer, data.skill, { 43, 40 });
		for (int tick = 0; tick < 400; tick++) {
			ProcessSkillVolley(*MyPlayer);
			ProcessSkillZones(*MyPlayer);
			MyPlayer->poe2.tick();
		}
		EXPECT_EQ(SkillZoneCount(), 0U) << data.name;
		ClearSkillZones();
	}
	MyPlayer->poe2.castSkill = WeaponSkill::None;
	leveltype = DTYPE_TOWN;
}

TEST_F(Poe2TableSkillsTest, CursesMakeMonstersTakeMoreDamage)
{
	Monster monster {};
	EXPECT_EQ(TableCurseDamageTakenPercent(monster, DamageType::Physical), 0);
	monster.poe2.deathMarkTicks = 10;
	EXPECT_GT(TableCurseDamageTakenPercent(monster, DamageType::Physical), 0);
	monster.poe2.deathMarkTicks = 0;
	monster.poe2.lowerResistTicks = 10;
	// Lower Resist lowers resistance rather than adding damage taken.
	EXPECT_EQ(TableCurseDamageTakenPercent(monster, DamageType::Physical), 0);
	EXPECT_EQ(TableCurseDamageTakenPercent(monster, DamageType::Fire), 0);
	ProcessTableCurses(monster);
	EXPECT_EQ(monster.poe2.lowerResistTicks, 9);
}

TEST_F(Poe2TableSkillsTest, SoulsAddDamage)
{
	EXPECT_EQ(SoulHarvestDamagePercent(*MyPlayer), 0);
	MyPlayer->poe2.soulStacks = 3;
	EXPECT_GT(SoulHarvestDamagePercent(*MyPlayer), 0);
}

} // namespace
} // namespace devilution
