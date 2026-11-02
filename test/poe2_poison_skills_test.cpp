#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "lighting.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/modifiers.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/player_data.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/skill_resources.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/support_table.hpp"
#include "poe2/support_tiers.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/vfx.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/misdat.h"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

/** @brief The poison skills plan (research/skill-gems-per-weapon/poison-skills-plan.md): the poison skills and supports. */
class Poe2PoisonSkillsTest : public ::testing::Test {
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
		sgGameInitInfo.nDifficulty = DIFF_NORMAL;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadSpellData();
		LoadMissileData();
		LoadPlayerDataFiles();
		LoadPassiveTreeData();
		LoadModifierData();
		LoadMonsterData();
		LevelMonsterTypes[0].type = MT_NZOMBIE;
		Missiles.clear();

		Player &player = *MyPlayer;
		player._pClass = HeroClass::Rogue;
		player.setCharacterLevel(30);
		player._pBaseStr = player._pStrength = 100;
		player._pBaseMag = player._pMagic = 200;
		player._pBaseDex = player._pDexterity = 200;
		player._pBaseVit = player._pVitality = 25;
		player._pMaxHPBase = player._pHPBase = player._pMaxHP = player._pHitPoints = Fixed26_6::fromInt(200);
		player._pMaxManaBase = player._pManaBase = player._pMaxMana = player._pMana = Fixed26_6::fromInt(200);
		player._pIMinDam = 20;
		player._pIMaxDam = 30;
		player.position.tile = { 40, 40 };
		player._pdir = Direction::East;
	}

	void TearDown() override
	{
		Missiles.clear();
		ClearSkillZones();
		ClearSupportEffects();
		ClearMonsters();
		gbIsPoe2 = false;
		leveltype = DTYPE_TOWN;
		LoadPassiveTreeData();
		UnloadModArchives();
	}

	/** @brief Socket a skill in the first Skills panel row with one support of the given tier. */
	static SkillSlot Supported(SkillSlot skill, SupportGem gem = SupportGem::None, int tier = 1)
	{
		Player &player = *MyPlayer;
		player.poe2.skillSlots[0] = skill;
		player.poe2.slotLevels[0] = 1;
		player.poe2.slotSupports[0] = {};
		player.poe2.slotSupportTiers[0] = {};
		if (gem != SupportGem::None) {
			player.poe2.slotSupports[0][0] = gem;
			player.poe2.slotSupportTiers[0][0] = static_cast<uint8_t>(tier);
		}
		return skill;
	}

	static SkillSlot Supported(WeaponSkill skill, SupportGem gem = SupportGem::None, int tier = 1)
	{
		return Supported(SkillSlot::ForAttack(skill), gem, tier);
	}

	/** @brief A normal monster of the kind standing on the tile, with plenty of life. */
	static Monster &PlaceMonster(size_t slot, Point tile, _monster_id kind = MT_NZOMBIE)
	{
		LevelMonsterTypes[0].type = kind;
		Monster &monster = Monsters[slot];
		monster = {};
		monster.levelType = 0;
		monster.uniqueType = UniqueMonsterType::None;
		monster.talkMsg = TEXT_NONE;
		monster.lightId = NO_LIGHT;
		monster.mode = MonsterMode::Stand;
		monster.maxHitPoints = monster.hitPoints = Fixed26_6::fromInt(100000);
		monster.position.tile = monster.position.old = monster.position.future = tile;
		dMonster[tile.x][tile.y] = static_cast<int16_t>(slot + 1);
		ActiveMonsters[ActiveMonsterCount] = static_cast<unsigned>(slot);
		ActiveMonsterCount++;
		return monster;
	}

	static void ClearMonsters()
	{
		for (int x = 25; x < 60; x++) {
			for (int y = 25; y < 60; y++) {
				dMonster[x][y] = 0;
				dCorpse[x][y] = 0;
			}
		}
		for (size_t i = 0; i < ActiveMonsterCount; i++)
			Monsters[ActiveMonsters[i]] = {};
		ActiveMonsterCount = 0;
	}

	/** @brief A dungeon to fight in, with the network up for the hits. */
	static void EnterDungeon()
	{
		leveltype = DTYPE_CATHEDRAL;
		SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
	}

	static const TableSkillData &Row(WeaponSkill skill)
	{
		const TableSkillData *data = FindTableSkill(skill);
		EXPECT_NE(data, nullptr);
		return *data;
	}

	static void Zones(int ticks)
	{
		for (int i = 0; i < ticks; i++)
			ProcessSkillZones(*MyPlayer);
	}

	static bool Hurt(const Monster &monster)
	{
		return monster.hitPoints < monster.maxHitPoints;
	}
};

struct PoisonSkill {
	WeaponSkill skill;
	int level;
	bool spell;
};

constexpr std::array<PoisonSkill, 11> PoisonSkills { {
    { WeaponSkill::PoisonDagger, 1, false },
    { WeaponSkill::AcidFlask, 12, false },
    { WeaponSkill::VenomSentry, 20, false },
    { WeaponSkill::ThornvineArrow, 10, false },
    { WeaponSkill::BloatedPods, 15, false },
    { WeaponSkill::PlagueEffigy, 20, false },
    { WeaponSkill::WitchsBog, 7, false },
    { WeaponSkill::VenomBolt, 1, true },
    { WeaponSkill::PoisonExplosion, 8, true },
    { WeaponSkill::PoisonNova, 22, true },
    { WeaponSkill::AcidCloud, 25, true },
} };

// Eleven new gems, appended, each an uncut-only gem (no drop rate of its own) at the plan's gem level, Dexterity for the
// Rogue's and Magic for the Sorcerer's, and drawn sickly green.
TEST_F(Poe2PoisonSkillsTest, PoisonSkillsAreGreenGemsAtThePlansLevels)
{
	EXPECT_EQ(WeaponSkill::Last, WeaponSkill::AcidCloud);
	for (const PoisonSkill &entry : PoisonSkills) {
		const TableSkillData &data = Row(entry.skill);
		EXPECT_EQ(data.level, entry.level) << data.name;
		EXPECT_EQ((data.flags & TF_Spell) != 0, entry.spell) << data.name;
		EXPECT_EQ(data.type, entry.spell ? DamageType::Chaos : DamageType::Physical) << data.name;
		EXPECT_FALSE(IsGrantedTableSkill(data)) << data.name;
		EXPECT_EQ(TableSkillLook(data), vfx::Element::Poison) << data.name;
		const SkillSlot slot = SkillSlot::ForAttack(entry.skill);
		Item gem;
		ASSERT_TRUE(CreateSkillGemItem(gem, slot, 1)) << data.name;
		EXPECT_EQ(SkillForGem(gem), slot) << data.name;
		const std::string name = std::string(data.name) + " Gem";
		const auto row = std::find_if(AllItemsList.begin(), AllItemsList.end(), [&](const ItemData &item) { return item.iName == name; });
		ASSERT_NE(row, AllItemsList.end()) << name;
		EXPECT_EQ(row->dropRate, 0) << name;
		EXPECT_EQ(row->iMinMLvl, entry.level) << name;
		EXPECT_EQ(row->iValue, 450 + 130 * entry.level) << name;
		EXPECT_EQ(entry.spell ? row->iMinMag : row->iMinDex, 18 + 3 * entry.level) << name;
		EXPECT_EQ(entry.spell ? row->iMinDex : row->iMinMag, 0) << name;
	}
	// The Rogue's take their weapons; the Sorcerer's none.
	EXPECT_EQ(Row(WeaponSkill::PoisonDagger).weapon, TableWeapon::Dagger);
	EXPECT_EQ(Row(WeaponSkill::AcidFlask).weapon, TableWeapon::Dagger);
	EXPECT_EQ(Row(WeaponSkill::VenomSentry).weapon, TableWeapon::Dagger);
	for (const WeaponSkill skill : { WeaponSkill::ThornvineArrow, WeaponSkill::BloatedPods, WeaponSkill::PlagueEffigy, WeaponSkill::WitchsBog })
		EXPECT_EQ(Row(skill).weapon, TableWeapon::Bow) << Row(skill).name;
	// Cobra Strike and Plague Arrow are green now too.
	EXPECT_EQ(TableSkillLook(Row(WeaponSkill::ViperStrike)), vfx::Element::Poison);
	EXPECT_EQ(TableSkillLook(Row(WeaponSkill::PlagueArrow)), vfx::Element::Poison);
	EXPECT_EQ(TableSkillLook(Row(WeaponSkill::LocustSwarm)), vfx::Element::Poison);
	EXPECT_EQ(TableSkillLook(Row(WeaponSkill::Haunt)), vfx::Element::Chaos);
}

TEST_F(Poe2PoisonSkillsTest, PoisonSkillsHaveTheirTags)
{
	const auto tags = [](WeaponSkill skill) { return SkillTags(SkillSlot::ForAttack(skill)); };
	const auto has = [&](WeaponSkill skill, uint32_t tag) { return (tags(skill) & tag) == tag; };
	EXPECT_TRUE(has(WeaponSkill::PoisonDagger, ST_Attack | ST_Melee | ST_Strike | ST_Physical | ST_Damage | ST_DamageOverTime));
	EXPECT_TRUE(has(WeaponSkill::AcidFlask, ST_Projectile | ST_Area | ST_Duration | ST_DamageOverTime));
	EXPECT_TRUE(has(WeaponSkill::VenomSentry, ST_Placed | ST_Area | ST_Duration | ST_DamageOverTime));
	EXPECT_FALSE(has(WeaponSkill::VenomSentry, ST_Projectile));
	EXPECT_TRUE(has(WeaponSkill::ThornvineArrow, ST_Bow | ST_Projectile | ST_Area | ST_Duration | ST_DamageOverTime));
	EXPECT_TRUE(has(WeaponSkill::BloatedPods, ST_Placed | ST_Projectile | ST_Area | ST_Duration));
	EXPECT_TRUE(has(WeaponSkill::PlagueEffigy, ST_Placed | ST_Projectile | ST_Duration | ST_Damage));
	EXPECT_TRUE(has(WeaponSkill::WitchsBog, ST_Area | ST_Duration));
	EXPECT_FALSE(has(WeaponSkill::WitchsBog, ST_Damage));
	EXPECT_TRUE(has(WeaponSkill::VenomBolt, ST_Spell | ST_Projectile | ST_Chaos | ST_Damage | ST_DamageOverTime));
	EXPECT_TRUE(has(WeaponSkill::PoisonExplosion, ST_Spell | ST_Area | ST_Chaos | ST_Duration | ST_DamageOverTime));
	EXPECT_TRUE(has(WeaponSkill::PoisonNova, ST_Spell | ST_Area | ST_Chaos | ST_DamageOverTime));
	EXPECT_TRUE(has(WeaponSkill::AcidCloud, ST_Spell | ST_Area | ST_Chaos | ST_Duration | ST_DamageOverTime));
	for (const PoisonSkill &entry : PoisonSkills)
		EXPECT_EQ(tags(entry.skill) & ST_Elemental, 0U) << Row(entry.skill).name;
}

// Poison Dagger: a 130% stab whose 50% a second poison stacks three times.
TEST_F(Poe2PoisonSkillsTest, PoisonDaggerStacksThreePoisons)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	const TableSkillData &dagger = Row(WeaponSkill::PoisonDagger);
	EXPECT_EQ(dagger.shape, TableShape::Strike);
	EXPECT_EQ(dagger.damagePercent, 130);
	EXPECT_EQ(dagger.poisonPercent, 50);
	EXPECT_EQ(TablePoisonStacks(dagger), 3);
	EXPECT_EQ(TablePoisonStacks(Row(WeaponSkill::ViperStrike)), 1);
	const SkillSlot slot = Supported(WeaponSkill::PoisonDagger);

	// The stab poisons.
	const Point front = player.position.tile + player._pdir;
	int32_t perTick = 0;
	for (uint32_t seed = 1; seed < 50 && perTick == 0; seed++) {
		ClearMonsters();
		Monster &monster = PlaceMonster(0, front);
		SetRndSeed(seed);
		{
			const SupportHitScope scope(player, slot);
			ExecuteTableSkill(player, dagger, front);
		}
		if (monster.poe2.poisonTicks > 0) {
			EXPECT_EQ(monster.poe2.poisonTicks, PoisonTicks);
			perTick = monster.poe2.poisonDamagePerTick;
		}
	}
	EXPECT_GT(perTick, 0);

	// Three of its poisons add up; a fourth doesn't.
	ClearMonsters();
	Monster &monster = PlaceMonster(0, { 45, 45 });
	for (int i = 0; i < 4; i++) {
		const SupportHitScope scope(player, slot);
		InflictPoison(player, monster, 40 * 100, PoisonTicks, TablePoisonStacks(dagger));
		EXPECT_EQ(monster.poe2.poisonDamagePerTick, 100 * std::min(i + 1, 3)) << i;
	}
}

// Acid Flask: the flask flies, bursts into a pool that poisons what stays in it; three pools at most.
TEST_F(Poe2PoisonSkillsTest, AcidFlaskLeavesAPoolThatPoisons)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	const TableSkillData &flask = Row(WeaponSkill::AcidFlask);
	EXPECT_EQ(flask.zone, TableZone::PoisonGround);
	EXPECT_NE(flask.flags & TF_Lobbed, 0U);
	EXPECT_EQ(flask.damagePercent, 70);
	EXPECT_EQ(flask.duration, 40);
	EXPECT_TRUE(IsTableProjectileSkill(flask));
	Supported(WeaponSkill::AcidFlask);
	const Point target { 45, 40 };
	Monster &hit = PlaceMonster(0, target);
	Monster &outside = PlaceMonster(1, { 49, 40 });
	ExecuteTableSkill(player, flask, target);
	ASSERT_EQ(SkillZoneCount(), 1U);
	// In the air: nothing yet.
	Zones(5);
	EXPECT_FALSE(Hurt(hit));
	EXPECT_EQ(hit.poe2.poisonTicks, 0);
	Zones(10);
	EXPECT_TRUE(Hurt(hit));
	EXPECT_GT(hit.poe2.poisonTicks, 0);
	EXPECT_FALSE(Hurt(outside));
	EXPECT_EQ(outside.poe2.poisonTicks, 0);
	// One that walks in later is poisoned by the pool, not hit.
	Monster &later = PlaceMonster(2, { 46, 40 });
	Zones(10);
	EXPECT_FALSE(Hurt(later));
	EXPECT_GT(later.poe2.poisonTicks, 0);
	EXPECT_EQ(later.poe2.poisonDamagePerTick, hit.poe2.poisonDamagePerTick);
	// The pool dries up.
	Zones(40);
	EXPECT_EQ(SkillZoneCount(), 0U);

	// Three pools at once; a fourth takes the oldest's place.
	for (int i = 0; i < 4; i++)
		ExecuteTableSkill(player, flask, { 44 + i, 44 });
	EXPECT_EQ(SkillZoneCount(), static_cast<size_t>(AcidFlaskPools));
}

// Venom Sentry: the canister waits for an enemy, then puffs poison gas for 4 seconds; it's gone if none comes.
TEST_F(Poe2PoisonSkillsTest, VenomSentryWaitsThenGasses)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	const TableSkillData &sentry = Row(WeaponSkill::VenomSentry);
	EXPECT_EQ(sentry.zone, TableZone::GasTrap);
	EXPECT_EQ(sentry.radius, 2);
	EXPECT_EQ(sentry.duration, 80);
	Supported(WeaponSkill::VenomSentry);
	const Point target { 45, 40 };
	ExecuteTableSkill(player, sentry, target);
	ASSERT_EQ(SkillZoneCount(), 1U);
	// Long past its cloud's time, it still waits.
	Zones(120);
	EXPECT_EQ(SkillZoneCount(), 1U);
	Monster &monster = PlaceMonster(0, { 47, 40 });
	Zones(2);
	EXPECT_TRUE(Hurt(monster));
	EXPECT_GT(monster.poe2.poisonTicks, 0);
	// It puffs again and again, then is gone.
	const Fixed26_6 once = monster.hitPoints;
	Zones(10);
	EXPECT_LT(monster.hitPoints, once);
	Zones(80);
	EXPECT_EQ(SkillZoneCount(), 0U);

	// No enemy: it is gone after its wait.
	ClearMonsters();
	ExecuteTableSkill(player, sentry, target);
	Zones(VenomSentryArmTicks + 20);
	EXPECT_EQ(SkillZoneCount(), 0U);
}

// Thornvine Arrow: where the arrow comes down a thorny vine grows, poisoning and slowing what stands in it.
TEST_F(Poe2PoisonSkillsTest, ThornvineArrowGrowsAVine)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	const TableSkillData &arrow = Row(WeaponSkill::ThornvineArrow);
	EXPECT_EQ(arrow.shape, TableShape::Projectiles);
	EXPECT_NE(arrow.flags & TF_LandingZone, 0U);
	EXPECT_EQ(arrow.flags & TF_Poison, 0U);
	EXPECT_TRUE(IsTableAreaSkill(arrow));
	// Deadly Venom has no hit to strengthen.
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::DeadlyVenom, SkillSlot::ForAttack(WeaponSkill::ThornvineArrow)));
	Supported(WeaponSkill::ThornvineArrow);
	const Point target { 46, 40 };
	Monster &monster = PlaceMonster(0, { 47, 40 });
	ExecuteTableSkill(player, arrow, target);
	ASSERT_EQ(SkillZoneCount(), 1U);
	Zones(20);
	// The vine poisons and hinders, a milder hinder than caltrops; it doesn't hit.
	EXPECT_FALSE(Hurt(monster));
	EXPECT_GT(monster.poe2.poisonTicks, 0);
	EXPECT_GT(monster.poe2.hinderTicks, 0);
	EXPECT_EQ(MonsterHinderPercent(monster), ThornvineHinderPercent);
	Zones(80);
	EXPECT_EQ(SkillZoneCount(), 0U);
}

// Bloated Pods: three pods in a spread across the shot; one bursts when an enemy comes next to it, or when its time runs out.
TEST_F(Poe2PoisonSkillsTest, BloatedPodsBurstWhenEnemiesComeNear)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	const TableSkillData &pods = Row(WeaponSkill::BloatedPods);
	EXPECT_EQ(pods.zone, TableZone::Pod);
	EXPECT_EQ(pods.count, 3);
	const Point target { 46, 40 };
	const std::vector<Point> tiles = PodTiles(player.position.tile, target, 3);
	ASSERT_EQ(tiles.size(), 3U);
	EXPECT_EQ(tiles[0], target);
	EXPECT_EQ(tiles[1] - target, target - tiles[2]);
	EXPECT_EQ(tiles[0].WalkingDistance(tiles[1]), PodSpreadTiles);

	Supported(WeaponSkill::BloatedPods);
	Monster &near = PlaceMonster(0, target);
	ExecuteTableSkill(player, pods, target);
	EXPECT_EQ(SkillZoneCount(), 3U);
	Zones(20);
	// The pod by the monster burst; the others wait.
	EXPECT_TRUE(Hurt(near));
	EXPECT_GT(near.poe2.poisonTicks, 0);
	EXPECT_EQ(SkillZoneCount(), 2U);
	// The others burst when their time runs out.
	Zones(pods.duration);
	EXPECT_EQ(SkillZoneCount(), 0U);
	// Three volleys of three at most out at once.
	ClearMonsters();
	for (int i = 0; i < 4; i++)
		ExecuteTableSkill(player, pods, target);
	EXPECT_EQ(SkillZoneCount(), static_cast<size_t>(SupportTrapLimit(player, SkillSlot::ForAttack(WeaponSkill::BloatedPods))));
}

// Plague Effigy: shoots only the enemies the hero has poisoned.
TEST_F(Poe2PoisonSkillsTest, PlagueEffigyShootsOnlyThePoisoned)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	const TableSkillData &effigy = Row(WeaponSkill::PlagueEffigy);
	EXPECT_EQ(effigy.zone, TableZone::Turret);
	EXPECT_NE(effigy.flags & TF_PoisonedOnly, 0U);
	Supported(WeaponSkill::PlagueEffigy);
	Monster &clean = PlaceMonster(0, { 46, 40 });
	ExecuteTableSkill(player, effigy, { 44, 40 });
	ASSERT_EQ(SkillZoneCount(), 1U);
	Missiles.clear();
	Zones(30);
	EXPECT_TRUE(Missiles.empty());
	EXPECT_FALSE(PoisonedBy(clean, player));
	// Poisoned, it is shot.
	Monster &sick = PlaceMonster(1, { 47, 41 });
	InflictPoison(player, sick, 40 * 100, PoisonTicks);
	EXPECT_TRUE(PoisonedBy(sick, player));
	Zones(10);
	ASSERT_FALSE(Missiles.empty());
	EXPECT_EQ(Missiles.back()._mitype, MissileID::Arrow);
	EXPECT_EQ(MissileSkill(Missiles.back()), SkillSlot::ForAttack(WeaponSkill::PlagueEffigy));
	// One effigy at a time.
	ExecuteTableSkill(player, effigy, { 44, 41 });
	EXPECT_EQ(SkillZoneCount(), 1U);
}

// Witch's Bog: a bog around the hero slows enemies; the hero's arrows shot from or over it poison what they hit.
TEST_F(Poe2PoisonSkillsTest, WitchsBogSlowsAndPoisonsArrows)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	const TableSkillData &bog = Row(WeaponSkill::WitchsBog);
	EXPECT_EQ(bog.zone, TableZone::Bog);
	EXPECT_EQ(bog.damagePercent, 0);
	EXPECT_EQ(bog.poisonPercent, 30);
	Supported(WeaponSkill::Multishot);
	Monster &inside = PlaceMonster(0, { 41, 40 });
	Monster &target = PlaceMonster(1, { 47, 40 });
	EXPECT_FALSE(ArrowCrossedBog(player, player.position.tile, target.position.tile));
	ExecuteTableSkill(player, bog, { 45, 40 });
	ASSERT_EQ(SkillZoneCount(), 1U);
	Zones(1);
	EXPECT_EQ(MonsterHinderPercent(inside), WitchsBogHinderPercent);
	EXPECT_EQ(inside.poe2.poisonTicks, 0);
	EXPECT_FALSE(Hurt(inside));
	EXPECT_TRUE(ArrowCrossedBog(player, player.position.tile, target.position.tile));
	EXPECT_FALSE(ArrowCrossedBog(player, { 50, 50 }, { 55, 50 }));

	// An arrow from the bog poisons for 30% of its physical and chaos hit a second.
	const SkillSlot multishot = SkillSlot::ForAttack(WeaponSkill::Multishot);
	Missile arrow {};
	arrow._mitype = MissileID::Arrow;
	arrow.position.start = player.position.tile;
	SetMissileSkill(arrow, player, multishot);
	{
		const SupportHitScope scope(player, multishot);
		NoteSupportedDamage(target, DamageType::Physical, Fixed26_6::fromInt(100));
		OnTableMissileHit(arrow, player, target, 100 << 6, true);
	}
	EXPECT_EQ(target.poe2.poisonTicks, PoisonTicks);
	EXPECT_EQ(target.poe2.poisonDamagePerTick, 64 * 100 * 30 / 100 / 20);
	// A purely elemental arrow has nothing to poison with.
	Monster &cold = PlaceMonster(2, { 47, 42 });
	{
		const SupportHitScope scope(player, multishot);
		NoteSupportedDamage(cold, DamageType::Cold, Fixed26_6::fromInt(100));
		OnTableMissileHit(arrow, player, cold, 100 << 6, true);
	}
	EXPECT_EQ(cold.poe2.poisonTicks, 0);
	// Without the bog, no poison.
	ClearSkillZones();
	Monster &plain = PlaceMonster(3, { 47, 44 });
	{
		const SupportHitScope scope(player, multishot);
		NoteSupportedDamage(plain, DamageType::Physical, Fixed26_6::fromInt(100));
		OnTableMissileHit(arrow, player, plain, 100 << 6, true);
	}
	EXPECT_EQ(plain.poe2.poisonTicks, 0);
}

// Venom Bolt: a green chaos bolt whose poison is 60% of its hit a second.
TEST_F(Poe2PoisonSkillsTest, VenomBoltPoisons)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	const TableSkillData &bolt = Row(WeaponSkill::VenomBolt);
	EXPECT_EQ(bolt.damagePercent, 130);
	EXPECT_EQ(bolt.poisonPercent, 60);
	const SkillSlot slot = Supported(WeaponSkill::VenomBolt);
	ExecuteTableSkill(player, bolt, { 46, 40 });
	ASSERT_FALSE(Missiles.empty());
	EXPECT_EQ(Missiles.back()._mitype, MissileID::OrangeFlare);
	EXPECT_EQ(Missiles.back().poe2.vfxElement, static_cast<int8_t>(vfx::Element::Poison));
	EXPECT_EQ(Missiles.back().poe2.damageType, static_cast<int8_t>(DamageType::Chaos));

	Monster &monster = PlaceMonster(0, { 45, 45 });
	Missile missile {};
	SetMissileSkill(missile, player, slot);
	{
		const SupportHitScope scope(player, slot);
		NoteSupportedDamage(monster, DamageType::Chaos, Fixed26_6::fromInt(50));
		OnTableMissileHit(missile, player, monster, 50 << 6, true);
	}
	EXPECT_EQ(monster.poe2.poisonTicks, PoisonTicks);
	EXPECT_EQ(monster.poe2.poisonDamagePerTick, 64 * 50 * 60 / 100 / 20);
}

// Poison Explosion: a corpse bursts in chaos and leaves a cloud that poisons for 2 seconds.
TEST_F(Poe2PoisonSkillsTest, PoisonExplosionLeavesACloud)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	const TableSkillData &burst = Row(WeaponSkill::PoisonExplosion);
	EXPECT_EQ(burst.shape, TableShape::CorpseBurst);
	EXPECT_EQ(burst.zone, TableZone::PoisonGround);
	Supported(WeaponSkill::PoisonExplosion);
	// No corpse, nothing.
	Monster &near = PlaceMonster(0, { 46, 40 });
	ExecuteTableSkill(player, burst, { 45, 40 });
	EXPECT_FALSE(Hurt(near));
	EXPECT_EQ(SkillZoneCount(), 0U);
	dCorpse[45][40] = 1;
	ExecuteTableSkill(player, burst, { 45, 40 });
	EXPECT_EQ(dCorpse[45][40], 0);
	EXPECT_TRUE(Hurt(near));
	EXPECT_GT(near.poe2.poisonTicks, 0);
	ASSERT_EQ(SkillZoneCount(), 1U);
	// The cloud poisons what comes in without hitting it again.
	Monster &later = PlaceMonster(1, { 45, 41 });
	const Fixed26_6 before = near.hitPoints;
	Zones(1);
	EXPECT_EQ(near.hitPoints, before);
	EXPECT_FALSE(Hurt(later));
	EXPECT_GT(later.poe2.poisonTicks, 0);
	Zones(burst.duration);
	EXPECT_EQ(SkillZoneCount(), 0U);
}

// Poison Nova: a ring of chaos all around that poisons for 60% a second.
TEST_F(Poe2PoisonSkillsTest, PoisonNovaPoisonsTheRing)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	const TableSkillData &nova = Row(WeaponSkill::PoisonNova);
	EXPECT_EQ(nova.shape, TableShape::Around);
	EXPECT_EQ(nova.radius, 3);
	Supported(WeaponSkill::PoisonNova);
	Monster &near = PlaceMonster(0, { 42, 40 });
	Monster &edge = PlaceMonster(1, { 40, 43 });
	Monster &far = PlaceMonster(2, { 46, 40 });
	ExecuteTableSkill(player, nova, player.position.tile);
	for (const Monster *monster : { &near, &edge }) {
		EXPECT_TRUE(Hurt(*monster));
		EXPECT_EQ(monster->poe2.poisonTicks, PoisonTicks);
		EXPECT_GT(monster->poe2.poisonDamagePerTick, 0);
	}
	EXPECT_FALSE(Hurt(far));
	EXPECT_EQ(far.poe2.poisonTicks, 0);
}

// Acid Cloud: acid gathers, rains on the target a moment later and leaves a pool; two pools at most.
TEST_F(Poe2PoisonSkillsTest, AcidCloudRainsAfterAMoment)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	const TableSkillData &cloud = Row(WeaponSkill::AcidCloud);
	EXPECT_EQ(cloud.delay, 8);
	EXPECT_EQ(cloud.zone, TableZone::PoisonGround);
	Supported(WeaponSkill::AcidCloud);
	const Point target { 46, 40 };
	Monster &monster = PlaceMonster(0, target);
	ExecuteTableSkill(player, cloud, target);
	Zones(cloud.delay);
	EXPECT_FALSE(Hurt(monster));
	Zones(1);
	EXPECT_TRUE(Hurt(monster));
	EXPECT_GT(monster.poe2.poisonTicks, 0);
	for (int i = 0; i < 3; i++)
		ExecuteTableSkill(player, cloud, { 44, 44 + i });
	EXPECT_EQ(SkillZoneCount(), static_cast<size_t>(AcidCloudPools));
}

// Locust Swarm: a poison now, at gem 10, 165%; it spreads to the two nearest when its monster dies.
TEST_F(Poe2PoisonSkillsTest, LocustSwarmIsAPoisonThatSpreads)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	const TableSkillData &locust = Row(WeaponSkill::LocustSwarm);
	EXPECT_EQ(locust.level, 10);
	EXPECT_EQ(locust.damagePercent, LocustSwarmPercent);
	EXPECT_EQ(LocustSwarmPercent, 165);
	EXPECT_EQ(locust.curse, TableCurse::Locust);
	const auto row = std::find_if(AllItemsList.begin(), AllItemsList.end(), [](const ItemData &item) { return item.iName == std::string_view("Locust Swarm Gem"); });
	ASSERT_NE(row, AllItemsList.end());
	EXPECT_EQ(row->iMinMLvl, 10);
	EXPECT_EQ(row->iMinMag, 48);
	EXPECT_EQ(row->iValue, 1750);
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::DeadlyVenom, SkillSlot::ForAttack(WeaponSkill::LocustSwarm)));
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::DeadlyVenom, SkillSlot::ForAttack(WeaponSkill::PoisonDagger)));

	Supported(WeaponSkill::LocustSwarm);
	Monster &cursed = PlaceMonster(0, { 45, 45 });
	ExecuteTableSkill(player, locust, cursed.position.tile);
	EXPECT_EQ(cursed.poe2.locustTicks, locust.duration);
	EXPECT_EQ(cursed.poe2.poisonTicks, locust.duration);
	EXPECT_GT(cursed.poe2.poisonDamagePerTick, 0);
	EXPECT_EQ(cursed.poe2.chaosTicks, 0);
	// It dies: the poison passes to the two nearest, with the time it had left.
	ClearMonsters();
	Monster &dead = PlaceMonster(0, { 45, 45 });
	dead.poe2.locustTicks = 30;
	dead.poe2.poisonTicks = 30;
	dead.poe2.poisonDamagePerTick = 500;
	dead.poe2.poisonSource = 0;
	Monster &first = PlaceMonster(1, { 46, 45 });
	Monster &second = PlaceMonster(2, { 45, 47 });
	Monster &third = PlaceMonster(3, { 48, 45 });
	Monster &stronger = PlaceMonster(4, { 44, 45 });
	stronger.poe2.poisonTicks = 10;
	stronger.poe2.poisonDamagePerTick = 900;
	stronger.poe2.poisonSource = 0;
	OnTableCursedMonsterKilled(dead);
	EXPECT_EQ(first.poe2.poisonTicks, 30);
	EXPECT_EQ(first.poe2.poisonDamagePerTick, 500);
	EXPECT_EQ(first.poe2.locustTicks, 30);
	// A stronger poison stays as it was.
	EXPECT_EQ(stronger.poe2.poisonDamagePerTick, 900);
	EXPECT_EQ(stronger.poe2.poisonTicks, 10);
	EXPECT_EQ(second.poe2.poisonTicks + third.poe2.poisonTicks, 0);
	EXPECT_EQ(first.poe2.chaosTicks, 0);
}

// The supports: items, cut from uncut support gems at the plan's levels.
TEST_F(Poe2PoisonSkillsTest, PoisonSupportsHaveItems)
{
	for (const auto [gem, level] : { std::pair<SupportGem, int> { SupportGem::BloodAndVenom, 3 }, { SupportGem::SporePods, 6 }, { SupportGem::LingeringVenom, 6 }, { SupportGem::RathmasDoom, 10 }, { SupportGem::FesteringHex, 3 } }) {
		const SupportData *data = FindSupportData(gem);
		ASSERT_NE(data, nullptr);
		EXPECT_GT(static_cast<int>(gem), static_cast<int>(SupportGem::BoneSplinters));
		EXPECT_EQ(SupportTierCount(gem), 1) << data->name;
		EXPECT_EQ(SupportTierGemLevel(gem, 1, 0), level) << data->name;
		Item item;
		ASSERT_TRUE(CreateSupportGemItem(item, gem)) << data->name;
		EXPECT_EQ(SupportGemForItem(item), gem) << data->name;
		EXPECT_FALSE(IsLineageSupport(gem)) << data->name;
	}
	EXPECT_EQ(static_cast<int>(SupportGem::FesteringHex) + 1, static_cast<int>(SupportGem::Count));
}

// Blood and Venom (POE2's Admixture): its poison deals 60% more to a bleeding monster, its bleed 60% more to a poisoned one.
TEST_F(Poe2PoisonSkillsTest, BloodAndVenomFeedEachOther)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	const SkillSlot cobra = SkillSlot::ForAttack(WeaponSkill::ViperStrike);
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::BloodAndVenom, cobra));
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::BloodAndVenom, SkillSlot::ForAttack(WeaponSkill::VenomBolt)));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::BloodAndVenom, SkillSlot::ForAttack(WeaponSkill::FrostBolt)));

	// Poison on a bleeding monster.
	for (const bool supported : { false, true }) {
		Supported(cobra, supported ? SupportGem::BloodAndVenom : SupportGem::None);
		ClearMonsters();
		Monster &monster = PlaceMonster(0, { 45, 45 });
		{
			const SupportHitScope scope(player, cobra);
			InflictPoison(player, monster, 64 * 10 * PoisonTicks, PoisonTicks);
		}
		EXPECT_EQ(monster.poe2.poisonVsBleed, supported);
		monster.poe2.bleedTicks = 10;
		const Fixed26_6 before = monster.hitPoints;
		ProcessSupportMonsterEffects(monster);
		const int32_t perTick = 64 * 10 * (supported ? 100 + BloodAndVenomPercent : 100) / 100;
		EXPECT_EQ(before.raw() - monster.hitPoints.raw(), ChaosTickDamage(monster, perTick)) << supported;
	}
	// A bleed on a poisoned monster.
	for (const bool supported : { false, true }) {
		Supported(cobra, supported ? SupportGem::BloodAndVenom : SupportGem::None);
		ClearMonsters();
		Monster &monster = PlaceMonster(0, { 45, 45 });
		{
			const SupportHitScope scope(player, cobra);
			NoteSupportedBleed(player, monster);
		}
		EXPECT_EQ(monster.poe2.bleedVsPoison, supported);
		monster.poe2.bleedSource = 0;
		monster.poe2.bleedDamagePerTick = 100;
		monster.poe2.bleedTicks = 5;
		monster.poe2.poisonTicks = 5;
		const Fixed26_6 before = monster.hitPoints;
		ProcessUniqueMonsterAilments(monster);
		EXPECT_EQ(before.raw() - monster.hitPoints.raw(), supported ? 100 + BloodAndVenomPercent : 100) << supported;
	}
}

// Spore Pods (POE2's Poison Spores): a hit may drop a pod that bursts in poison when an enemy comes next to it.
TEST_F(Poe2PoisonSkillsTest, SporePodsDropAndBurst)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	const SkillSlot cleave = SkillSlot::ForAttack(WeaponSkill::Cleave);
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::SporePods, cleave));
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::SporePods, SkillSlot::ForAttack(WeaponSkill::VenomBolt)));
	Supported(cleave, SupportGem::SporePods);
	int drops = 0;
	for (uint32_t seed = 1; seed <= 100; seed++) {
		ClearMonsters();
		ClearSupportEffects();
		player.poe2.supportState.sporeCooldown = 0;
		Monster &monster = PlaceMonster(0, { 45, 45 });
		SetRndSeed(seed);
		{
			const SupportHitScope scope(player, cleave);
			NoteSupportedDamage(monster, DamageType::Physical, Fixed26_6::fromInt(100));
		}
		const Fixed26_6 hit = monster.hitPoints;
		// Not before it arms.
		for (int i = 0; i < SporePodArmTicks; i++)
			ProcessSupportEffects(player);
		if (monster.hitPoints != hit)
			ADD_FAILURE() << "burst before arming, seed " << seed;
		for (int i = 0; i < 2; i++)
			ProcessSupportEffects(player);
		if (monster.poe2.poisonTicks == 0)
			continue;
		drops++;
		EXPECT_EQ(monster.hitPoints.raw(), hit.raw() - Fixed26_6::fromInt(40).raw()) << seed;
	}
	// About one hit in five.
	EXPECT_GT(drops, 8);
	EXPECT_LT(drops, 35);
}

// Lingering Venom: poisons last twice as long and deal 20% less a second.
TEST_F(Poe2PoisonSkillsTest, LingeringVenomPoisonsLonger)
{
	Player &player = *MyPlayer;
	const SkillSlot cobra = Supported(WeaponSkill::ViperStrike, SupportGem::LingeringVenom);
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::LingeringVenom, cobra));
	Monster &monster = PlaceMonster(0, { 45, 45 });
	{
		const SupportHitScope scope(player, cobra);
		InflictPoison(player, monster, 40 * 100, PoisonTicks);
	}
	EXPECT_EQ(monster.poe2.poisonTicks, 2 * PoisonTicks);
	EXPECT_EQ(monster.poe2.poisonDamagePerTick, 80);
}

// Rathma's Doom (POE2's Impending Doom): a hand-cast curse that runs out bursts in chaos and poison around its monster.
TEST_F(Poe2PoisonSkillsTest, RathmasDoomBurstsAsTheCurseRunsOut)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	const SkillSlot despair = SkillSlot::ForAttack(WeaponSkill::Despair);
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::RathmasDoom, despair));
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::RathmasDoom, SkillSlot::ForAttack(WeaponSkill::Weaken)));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::RathmasDoom, SkillSlot::ForAttack(WeaponSkill::LocustSwarm)));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::RathmasDoom, SkillSlot::ForAttack(WeaponSkill::ViperStrike)));

	for (const bool supported : { false, true }) {
		Supported(despair, supported ? SupportGem::RathmasDoom : SupportGem::None);
		ClearMonsters();
		Monster &cursed = PlaceMonster(0, { 45, 45 });
		Monster &beside = PlaceMonster(1, { 46, 45 });
		Monster &far = PlaceMonster(2, { 49, 45 });
		ExecuteTableSkill(player, Row(WeaponSkill::Despair), cursed.position.tile);
		ASSERT_GT(cursed.poe2.despairTicks, 0);
		EXPECT_EQ(cursed.poe2.doomCurses != 0, supported);
		// Nothing while it lasts.
		ProcessCurseSupports(player);
		EXPECT_FALSE(Hurt(beside));
		cursed.poe2.despairTicks = 1;
		ProcessTableCurses(cursed);
		EXPECT_EQ(cursed.poe2.despairTicks, 0);
		EXPECT_EQ(cursed.poe2.doomPending, supported);
		ProcessCurseSupports(player);
		EXPECT_EQ(Hurt(beside), supported);
		EXPECT_EQ(Hurt(cursed), supported);
		EXPECT_EQ(beside.poe2.poisonTicks > 0, supported);
		EXPECT_FALSE(Hurt(far));
		EXPECT_FALSE(cursed.poe2.doomPending);
	}
}

// Festering Hex (POE2's Decaying Hex): the cursed are poisoned while the curse lasts.
TEST_F(Poe2PoisonSkillsTest, FesteringHexPoisonsTheCursed)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	const SkillSlot weaken = Supported(WeaponSkill::Weaken, SupportGem::FesteringHex);
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::FesteringHex, weaken));
	Monster &cursed = PlaceMonster(0, { 45, 45 });
	ExecuteTableSkill(player, Row(WeaponSkill::Weaken), cursed.position.tile);
	ASSERT_NE(cursed.poe2.festerCurses, 0);
	ASSERT_GT(cursed.poe2.festerPoison, 0);
	ProcessCurseSupports(player);
	EXPECT_EQ(cursed.poe2.poisonTicks, PoisonTicks);
	EXPECT_EQ(cursed.poe2.poisonDamagePerTick, std::max<int32_t>(cursed.poe2.festerPoison / PoisonTicks, 1));
	EXPECT_EQ(cursed.poe2.festerTicks, FesteringHexEvery);
	// The curse ends: no more.
	cursed.poe2.weakenTicks = 1;
	ProcessTableCurses(cursed);
	EXPECT_EQ(cursed.poe2.festerCurses, 0);
	// Unsupported, nothing.
	Supported(WeaponSkill::Weaken);
	Monster &plain = PlaceMonster(1, { 49, 49 });
	ExecuteTableSkill(player, Row(WeaponSkill::Weaken), plain.position.tile);
	ProcessCurseSupports(player);
	EXPECT_EQ(plain.poe2.poisonTicks, 0);
}

// The poison effects: pools, clouds, a bog, vines, pods, a canister, an effigy and the throws all die out, early when ended.
TEST_F(Poe2PoisonSkillsTest, PoisonEffectsDieOut)
{
	HeadlessMode = false;
	vfx::Clear();
	vfx::Impact({ 40, 40 }, vfx::Element::Poison, 1.4F);
	vfx::Nova({ 42, 40 }, 3, vfx::Element::Poison);
	vfx::Miasma({ 40, 42 }, 2, 2000, vfx::MiasmaKind::Acid);
	vfx::Miasma({ 42, 42 }, 2, 2000, vfx::MiasmaKind::Gas);
	vfx::Miasma({ 44, 42 }, 2, 2000, vfx::MiasmaKind::Bog);
	vfx::Thorns({ 44, 44 }, 2, 2000);
	vfx::Pod({ 46, 44 }, 3000, 7);
	vfx::Canister({ 46, 46 }, 3000, 8);
	vfx::Effigy({ 48, 46 }, 3000, 9);
	vfx::EffigyShot({ 48, 46 });
	vfx::Lob({ 40, 40 }, { 45, 45 }, 400, vfx::LobKind::Flask);
	vfx::Lob({ 40, 40 }, { 45, 47 }, 400, vfx::LobKind::Seed);
	vfx::Lob({ 40, 40 }, { 47, 45 }, 400, vfx::LobKind::Canister);
	vfx::AcidRain({ 46, 40 }, 2, 400);
	EXPECT_GT(vfx::ParticleCount() + vfx::ShapeCount(), 0U);
	for (int i = 0; i < 200; i++)
		vfx::Advance(50);
	EXPECT_EQ(vfx::ParticleCount(), 0U);
	EXPECT_EQ(vfx::ShapeCount(), 0U);

	// Ended early, a long cloud, vine, pod and effigy go well before their time.
	vfx::Miasma({ 40, 42 }, 2, 600000, vfx::MiasmaKind::Gas, 11);
	vfx::Thorns({ 44, 44 }, 2, 600000, 12);
	vfx::Pod({ 46, 44 }, 600000, 13);
	vfx::Effigy({ 48, 46 }, 600000, 14);
	vfx::Advance(50);
	EXPECT_GT(vfx::ShapeCount(), 0U);
	for (const uint32_t key : { 11U, 12U, 13U, 14U })
		vfx::EndKeyed(key);
	for (int i = 0; i < 200; i++)
		vfx::Advance(50);
	EXPECT_EQ(vfx::ParticleCount(), 0U);
	EXPECT_EQ(vfx::ShapeCount(), 0U);
	vfx::Clear();
	HeadlessMode = true;
}

} // namespace
} // namespace devilution
