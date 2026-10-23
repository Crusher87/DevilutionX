#include <set>
#include <string_view>
#include <utility>

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

TEST_F(Poe2TableSkillsTest, AreaSkillsHitEachMonsterForFiftyToEightyPercent)
{
	// Area skills hit each monster for 50-80% of a hit; a delay, crowd control, a leap or damage over time leaves some
	// outside that band.
	const std::pair<WeaponSkill, int> Percents[] = {
		{ WeaponSkill::Whirlwind, 85 },
		{ WeaponSkill::Fissure, 65 },
		{ WeaponSkill::Shockwave, 70 },
		{ WeaponSkill::CallOfTheAncients, 60 },
		{ WeaponSkill::FallingSword, 100 },
		{ WeaponSkill::LashingTailKick, 65 },
		{ WeaponSkill::WaveOfLight, 50 },
		{ WeaponSkill::CycloneStrike, 50 },
		{ WeaponSkill::BlessedHammer, 20 },
		{ WeaponSkill::Condemn, 50 },
		{ WeaponSkill::BlessedShield, 70 },
		{ WeaponSkill::FistOfTheHeavens, 110 },
		{ WeaponSkill::CorpseExplosion, 50 },
		{ WeaponSkill::GlacialSpike, 35 },
		{ WeaponSkill::Blizzard, 21 },
		{ WeaponSkill::FrozenOrb, 70 },
		{ WeaponSkill::Meteor, 150 },
		{ WeaponSkill::Thunderstorm, 100 },
		{ WeaponSkill::SpikeTrap, 75 },
		{ WeaponSkill::BladeSentinel, 35 },
		{ WeaponSkill::Sentry, 60 },
		{ WeaponSkill::SiegeVolley, 75 },
		{ WeaponSkill::FreezingArrow, 100 },
		{ WeaponSkill::ExplosiveBolt, 110 },
		// Kept at its old value until the shortfall fix: the Monk has no Act 1 clear without it.
		{ WeaponSkill::DashingStrike, 150 },
	};
	for (const auto &[skill, percent] : Percents) {
		const TableSkillData *data = FindTableSkill(skill);
		ASSERT_NE(data, nullptr);
		EXPECT_EQ(data->damagePercent, percent) << data->name;
	}
	EXPECT_EQ(FindTableSkill(WeaponSkill::BlessedHammer)->radius, 1);
	EXPECT_EQ(FindTableSkill(WeaponSkill::SpikeTrap)->radius, 1);
	EXPECT_EQ(FindTableSkill(WeaponSkill::FallingSword)->curveEnd, 340);

	// Pulses: Blessed Hammer 4 x 20 = 80 a monster; Blade Sentinel 5 x 35 = 175 over its 5 seconds.
	const TableSkillData &hammer = *FindTableSkill(WeaponSkill::BlessedHammer);
	EXPECT_EQ(hammer.duration / hammer.interval, 4);
	EXPECT_EQ(hammer.damagePercent * hammer.duration / hammer.interval, 80);
	const TableSkillData &sentinel = *FindTableSkill(WeaponSkill::BladeSentinel);
	EXPECT_EQ(sentinel.duration / sentinel.interval, 5);
	EXPECT_EQ(sentinel.damagePercent * sentinel.duration / sentinel.interval, 175);

	// Landing bursts: a set share of a base hit on top of the arrow; Cluster Bolt keeps two thirds of its hit.
	EXPECT_EQ(TableLandingBurstPercent(*FindTableSkill(WeaponSkill::FreezingArrow)), 40);
	EXPECT_EQ(TableLandingBurstPercent(*FindTableSkill(WeaponSkill::ExplosiveBolt)), 45);
	EXPECT_EQ(TableLandingBurstPercent(*FindTableSkill(WeaponSkill::ClusterBolt)), 66);
}

TEST_F(Poe2TableSkillsTest, TableSpellsGrowLikeDiabloSpells)
{
	// Every damaging table spell on Fireball's steps ends at 312% at gem level 20, like the Diablo spells.
	const int fireball = GemDamagePercent(SkillSlot::ForSpell(SpellID::Fireball), 20);
	EXPECT_EQ(fireball, 312);
	int spells = 0;
	for (const TableSkillData &data : TableSkills()) {
		if ((data.flags & TF_Spell) == 0 || data.curve != TableCurve::Fireball)
			continue;
		spells++;
		EXPECT_EQ(data.curveEnd, 312) << data.name;
		EXPECT_EQ(GemDamagePercent(SkillSlot::ForAttack(data.skill), 20), fireball) << data.name;
	}
	EXPECT_EQ(spells, 12);
}

TEST_F(Poe2TableSkillsTest, PulsingZonesHitOncePerInterval)
{
	leveltype = DTYPE_CATHEDRAL;
	Player &player = *MyPlayer;
	player._pHitPoints = player._pMaxHP = Fixed26_6::fromInt(100);
	// Each pulse plays the skill's effect, so a tick that adds missiles is a pulse.
	const auto pulses = [&player](WeaponSkill skill) {
		Missiles.clear();
		ClearSkillZones();
		player.position.tile = { 40, 40 };
		player._pdir = Direction::South;
		player.poe2.castSkill = skill;
		ExecuteSkillCast(player, skill, { 43, 40 });
		int count = 0;
		for (int tick = 0; tick < 400 && SkillZoneCount() > 0; tick++) {
			const size_t before = Missiles.size();
			ProcessSkillZones(player);
			if (Missiles.size() > before)
				count++;
		}
		player.poe2.castSkill = WeaponSkill::None;
		return count;
	};
	EXPECT_EQ(pulses(WeaponSkill::BlessedHammer), 4);
	EXPECT_EQ(pulses(WeaponSkill::BladeSentinel), 5);
	leveltype = DTYPE_TOWN;
}

TEST_F(Poe2TableSkillsTest, FistOfTheHeavensBoltsHitForHalfTheSpellBase)
{
	leveltype = DTYPE_CATHEDRAL;
	Player &player = *MyPlayer;
	player._pMagic = 80;
	player.position.tile = { 40, 40 };
	player._pdir = Direction::South;
	Missiles.clear();
	ExecuteSkillCast(player, WeaponSkill::FistOfTheHeavens, { 44, 40 });
	const int curve = GemDamagePercent(SkillSlot::ForAttack(WeaponSkill::FistOfTheHeavens), player.poe2.skillLevel(WeaponSkill::FistOfTheHeavens));
	int bolts = 0;
	for (const Missile &missile : Missiles) {
		if (missile._mitype != MissileID::ChargedBolt)
			continue;
		bolts++;
		EXPECT_EQ(missile.poe2.damagePercent, curve * BurstBoltDamagePercent / 100);
		EXPECT_GE(missile._midam, SpellBaseMin(player, 1));
		EXPECT_LE(missile._midam, SpellBaseMax(player, 1));
	}
	EXPECT_EQ(bolts, FindTableSkill(WeaponSkill::FistOfTheHeavens)->count);
	EXPECT_EQ(BurstBoltDamagePercent, 50);
	ClearSkillZones();
	leveltype = DTYPE_TOWN;
}

TEST_F(Poe2TableSkillsTest, FrostNovaShardsHitForAQuarter)
{
	// About two shards reach a monster: 2 x 25 = 50% of the spell base.
	leveltype = DTYPE_CATHEDRAL;
	Player &player = *MyPlayer;
	player._pMagic = 80;
	player.position.tile = { 40, 40 };
	player._pdir = Direction::South;
	Missiles.clear();
	ExecuteSkillCast(player, WeaponSkill::FrostNova, { 44, 40 });
	const int curve = GemDamagePercent(SkillSlot::ForAttack(WeaponSkill::FrostNova), 1);
	int shards = 0;
	for (const Missile &missile : Missiles) {
		if (missile._mitype != MissileID::BlueFlare)
			continue;
		shards++;
		EXPECT_EQ(missile.poe2.damagePercent, curve * FrostNovaShardDamagePercent / 100);
	}
	EXPECT_GT(shards, 0);
	EXPECT_EQ(FrostNovaShardDamagePercent, 25);
	leveltype = DTYPE_TOWN;
}

} // namespace
} // namespace devilution
