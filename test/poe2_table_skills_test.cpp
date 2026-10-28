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
#include "poe2/bases.hpp"
#include "poe2/gem_levels.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/spell_base.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "tables/monstdat.h"
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
	size_t gems = 0;
	for (const TableSkillData &data : TableSkills()) {
		EXPECT_TRUE(seen.insert(data.skill).second) << data.name;
		EXPECT_EQ(FindTableSkill(data.skill), &data);
		// A caster weapon's granted skills have no gem (caster-weapons-plan-v2.md 9.1).
		if (IsGrantedTableSkill(data)) {
			EXPECT_FALSE(SkillGemItemIndex(SkillSlot::ForAttack(data.skill)).has_value()) << data.name;
			continue;
		}
		gems++;
		const std::optional<size_t> index = SkillGemItemIndex(SkillSlot::ForAttack(data.skill));
		ASSERT_TRUE(index.has_value()) << data.name;
		EXPECT_EQ(AllItemsList[*index].iMinMLvl, data.level) << data.name;
	}
	EXPECT_EQ(gems, 55U);
}

TEST_F(Poe2TableSkillsTest, TheTableCoversTheRestOfTheRoster)
{
	for (int i = static_cast<int>(WeaponSkill::Whirlwind); i < static_cast<int>(FirstGrantedSkill); i++)
		EXPECT_NE(FindTableSkill(static_cast<WeaponSkill>(i)), nullptr) << i;
	EXPECT_EQ(FindTableSkill(WeaponSkill::Cleave), nullptr);
	// The granted skills come in as they are built; those that are in are granted rows, and only they are.
	for (int i = static_cast<int>(WeaponSkill::Whirlwind); i <= static_cast<int>(WeaponSkill::Last); i++) {
		const auto skill = static_cast<WeaponSkill>(i);
		if (const TableSkillData *table = FindTableSkill(skill); table != nullptr)
			EXPECT_EQ(IsGrantedTableSkill(*table), IsGrantedSkill(skill)) << table->name;
	}
	EXPECT_NE(FindTableSkill(WeaponSkill::FreezingShards), nullptr);
}

TEST_F(Poe2TableSkillsTest, TableSkillsHaveNamesCostsAndCurves)
{
	LoadBaseData();
	for (const TableSkillData &data : TableSkills()) {
		EXPECT_EQ(WeaponSkillName(data.skill), data.name);
		EXPECT_FALSE(WeaponSkillDescription(data.skill).empty()) << data.name;
		if (IsGrantedTableSkill(data)) {
			// A granted skill has no gem tier, cost or gemcutting tab, and a base grants it (caster-weapons-plan-v2.md 9.1).
			EXPECT_EQ(data.level, 0) << data.name;
			EXPECT_EQ(data.mana, 0) << data.name;
			EXPECT_EQ(data.tabs, 0) << data.name;
			EXPECT_EQ(WeaponSkillManaCost(data.skill), 0) << data.name;
			EXPECT_NE(data.flags & TF_Spell, 0) << data.name;
			bool granted = false;
			for (size_t i = 0; i < AllItemsList.size(); i++) {
				const BaseMeta *base = BaseMetaFor(static_cast<int>(i));
				granted = granted || (base != nullptr && base->grantedSkill == SkillSlot::ForAttack(data.skill));
			}
			EXPECT_TRUE(granted) << data.name;
		} else {
			EXPECT_GT(WeaponSkillManaCost(data.skill), 0) << data.name;
		}
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

	// Landing bursts: a set share of a base hit on top of the arrow; Fire Arrow keeps two thirds of its hit.
	EXPECT_EQ(TableLandingBurstPercent(*FindTableSkill(WeaponSkill::FreezingArrow)), 40);
	EXPECT_EQ(TableLandingBurstPercent(*FindTableSkill(WeaponSkill::ExplosiveBolt)), 45);
	EXPECT_EQ(TableLandingBurstPercent(*FindTableSkill(WeaponSkill::ClusterBolt)), 66);
}

TEST_F(Poe2TableSkillsTest, TableSpellsGrowLikeDiabloSpells)
{
	// Every damaging table spell on Fireball's steps ends at 312% at gem level 20, like the Diablo spells.
	const int fireball = GemCurveDamagePercent(SkillSlot::ForSpell(SpellID::Fireball), 20);
	EXPECT_EQ(fireball, 312);
	int spells = 0;
	for (const TableSkillData &data : TableSkills()) {
		if ((data.flags & TF_Spell) == 0 || data.curve != TableCurve::Fireball)
			continue;
		// The caster weapons' granted skills follow the same curve; the count is of the gems'.
		if (!IsGrantedTableSkill(data))
			spells++;
		EXPECT_EQ(data.curveEnd, 312) << data.name;
		EXPECT_EQ(GemCurveDamagePercent(SkillSlot::ForAttack(data.skill), 20), fireball) << data.name;
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

TEST_F(Poe2TableSkillsTest, WarCryOnlyStuns)
{
	// Curtis 2026-10-23: War Cry gave Battle Cry's +50% (with no Battle Cry socketed) and shared its timer. Now it stuns only.
	leveltype = DTYPE_CATHEDRAL;
	Player &player = *MyPlayer;
	player.position.tile = { 40, 40 };
	player._pdir = Direction::South;
	ExecuteSkillCast(player, WeaponSkill::WarCry, { 41, 40 });
	EXPECT_EQ(player.poe2.warcryTicks, 0);

	// A Battle Cry already running keeps its own time left.
	player.poe2.warcryTicks = 37;
	ExecuteSkillCast(player, WeaponSkill::WarCry, { 41, 40 });
	EXPECT_EQ(player.poe2.warcryTicks, 37);
	player.poe2.warcryTicks = 0;
	ClearSkillZones();
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

TEST_F(Poe2TableSkillsTest, GlacialSpikeLeadsAWalkingMonster)
{
	// Curtis 2026-10-28: the spike came up behind moving enemies. It erupts where the monster will be.
	Monster monster {};
	monster.mode = MonsterMode::MoveSideways;
	monster.position.tile = { 10, 10 };
	monster.position.future = { 11, 10 };
	monster.var1 = 1;
	monster.var2 = 0;
	monster.animInfo.numberOfFrames = 8;
	monster.animInfo.ticksPerFrame = 1;
	monster.animInfo.currentFrame = 2;
	monster.animInfo.tickCounterOfCurrentFrame = 0;
	// 6 ticks are left of this step, then 8 a tile.
	EXPECT_EQ(PredictMonsterTile(monster, 0), Point(10, 10));
	EXPECT_EQ(PredictMonsterTile(monster, 5), Point(10, 10));
	EXPECT_EQ(PredictMonsterTile(monster, 6), Point(11, 10));
	EXPECT_EQ(PredictMonsterTile(monster, 13), Point(11, 10));
	EXPECT_EQ(PredictMonsterTile(monster, 14), Point(12, 10));
	// A very fast monster outruns the lead.
	EXPECT_EQ(PredictMonsterTile(monster, 200), Point(10 + MaxPredictedSteps, 10));
	// One standing still is hit where it stands.
	monster.mode = MonsterMode::Stand;
	EXPECT_EQ(PredictMonsterTile(monster, 14), Point(10, 10));
	// Glacial Spike's delay is short; the lead covers it.
	EXPECT_LT(FindTableSkill(WeaponSkill::GlacialSpike)->delay, 8);
}

/** @brief The visual effects with this look that are waiting or playing. */
int VisualEffectsShowing(MissileGraphicID look)
{
	int count = 0;
	for (const Missile &missile : Missiles) {
		if (missile._mitype == MissileID::VisualEffect && static_cast<MissileGraphicID>(missile._mispllvl & 0xFF) == look)
			count++;
	}
	return count;
}

TEST_F(Poe2TableSkillsTest, HauntIsASpiritThatJumps)
{
	// Curtis 2026-10-28: "is it a bleed skill, or a ghost that finds a monster then jumps?"
	EXPECT_EQ(WeaponSkillDescription(WeaponSkill::Haunt), "A spirit drains the enemy over time. When it dies, the spirit jumps to the nearest enemy.");
	EXPECT_EQ(FindTableSkill(WeaponSkill::Haunt)->effect, MissileGraphicID::BoneSpirit);

	leveltype = DTYPE_CATHEDRAL;
	LoadMonsterData();
	LevelMonsterTypes[0].type = MT_NZOMBIE;
	ActiveMonsterCount = 2;
	for (size_t i = 0; i < 2; i++) {
		Monster &monster = Monsters[i];
		monster = {};
		monster.levelType = 0;
		monster.uniqueType = UniqueMonsterType::None;
		monster.maxHitPoints = monster.hitPoints = Fixed26_6::fromInt(100);
		monster.position.tile = { static_cast<WorldTileCoord>(40 + (3 * i)), 40 };
		monster.talkMsg = TEXT_NONE;
		ActiveMonsters[i] = static_cast<unsigned>(i);
	}
	Monster &haunted = Monsters[0];
	haunted.poe2.hauntTicks = 100;
	haunted.poe2.bleedTicks = 11;
	haunted.poe2.bleedSource = -1;
	// The spirit shows on the monster it drains, not a wound.
	ProcessUniqueMonsterAilments(haunted);
	EXPECT_EQ(VisualEffectsShowing(MissileGraphicID::BoneSpirit), 1);
	EXPECT_EQ(VisualEffectsShowing(MissileGraphicID::RedFlareExplosion), 0);

	// When it dies, the spirit leaves it and shows up on the next one.
	Missiles.clear();
	haunted.poe2.bleedSource = 0;
	haunted.poe2.bleedSkill = WeaponSkill::Haunt;
	OnTableCursedMonsterKilled(haunted);
	EXPECT_EQ(Monsters[1].poe2.hauntTicks, 100);
	EXPECT_EQ(Monsters[1].poe2.bleedSkill, WeaponSkill::Haunt);
	EXPECT_EQ(VisualEffectsShowing(MissileGraphicID::BoneSpirit), 2);
	EXPECT_EQ(VisualEffectsShowing(MissileGraphicID::BloodStarRedExplosion), 0);
	ActiveMonsterCount = 0;
}

} // namespace
} // namespace devilution
