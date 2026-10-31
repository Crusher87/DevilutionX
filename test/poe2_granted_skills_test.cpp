/**
 * @file poe2_granted_skills_test.cpp
 *
 * The caster weapons rework's granted skills (caster-weapons-plan-v2.md 6.6, 6.7 and 6.12, step S14): a staff or wand
 * that grants a table skill casts it with a left click, its hold-to-repeat, the controller's attack and the skill bar,
 * at the weapon's grant level, taking the default attack row's supports. The first one is the Gelid Staff's Freezing
 * Shards (S14); the plain rows follow (S15), with the numbers of plan 7.2 and the gem comparison of 7.3.
 */
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "controls/plrctrls.h"
#include "cursor.h"
#include "diablo.h"
#include "engine/assets.hpp"
#include "engine/lighting_defs.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "panels/skills_panel.hpp"
#include "player.h"
#include "poe2/attack_speed.hpp"
#include "poe2/default_attack.hpp"
#include "poe2/gem_levels.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/skill_details.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/spirit.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/support_table.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"
#include "track.h"
#include "utils/file_util.h"
#include "utils/paths.h"
#include "utils/is_of.hpp"

namespace devilution {

extern void TestPlayerProcessAction(Player &player);

namespace {

const SkillSlot Shards = SkillSlot::ForAttack(WeaponSkill::FreezingShards);

class Poe2GrantedSkillsTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayerId = 0;
		MyPlayer = &Players[0];
		*MyPlayer = {};
		MyPlayer->poe2.clear();
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		gbIsPoe2 = true;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadPlayerDataFiles();
		LoadMissileData();
		LoadItemData();
		LoadSpellData();
		LoadModifierData();
		LoadSkillSpeedData();
		Missiles.clear();
		leveltype = DTYPE_CATHEDRAL;
		ActiveMonsterCount = 0;
		pcursmonst = -1;

		Player &player = *MyPlayer;
		player._pClass = HeroClass::Sorcerer;
		player.setCharacterLevel(80);
		player._pBaseStr = player._pStrength = 300;
		player._pBaseMag = player._pMagic = 300;
		player._pBaseDex = player._pDexterity = 300;
		player.lightId = NO_LIGHT;
		player.position.tile = { 20, 20 };
		player.position.future = player.position.tile;
		player.position.old = player.position.tile;
		player._pdir = Direction::South;
		Stand(player);
	}

	void TearDown() override
	{
		Missiles.clear();
		ClearSkillZones();
		leveltype = DTYPE_TOWN;
		pcursmonst = -1;
		sgbMouseDown = CLICK_NONE;
		LastPlayerAction = PlayerActionType::None;
		gbIsMultiplayer = false;
		gbIsPoe2 = false;
		LoadModifierData();
		LoadSkillSpeedData();
		UnloadModArchives();
		LoadMissileData();
	}

	static void Stand(Player &player)
	{
		player._pmode = PM_STAND;
		player.destAction = ACTION_NONE;
		player.poe2.castSkill = WeaponSkill::None;
		player._pHitPoints = player._pHPBase = player._pMaxHP = Fixed26_6::fromInt(200);
		player._pMana = player._pManaBase = Fixed26_6::fromInt(0);
	}

	static _item_indexes Row(std::string_view name)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == name)
				return static_cast<_item_indexes>(i);
		}
		ADD_FAILURE() << name;
		return IDI_GOLD;
	}

	/** @brief Puts a normal item of the base in the hero's left hand, at a POE2 item level, and works out the hero again. */
	static Item &Hold(Player &player, std::string_view name, int itemLevel = 33)
	{
		Item &item = player.InvBody[INVLOC_HAND_LEFT];
		item = {};
		SetLootRarity(item, ITEM_QUALITY_NORMAL);
		SetRndSeed(1);
		SetupAllItems(player, item, Row(name), AdvanceRndSeed(), 30, 1, /*onlygood=*/false, /*pregen=*/false, 0, /*forceNotUnique=*/true);
		SetPoe2ItemLevel(item, itemLevel);
		item._iStatFlag = true;
		CalcPlrInv(player, false);
		SetPlrAnims(player);
		Stand(player);
		return item;
	}

	/** @brief Runs the hero's action until the cast is over: the missiles of the type it fired. */
	static int FinishCast(Player &player, MissileID type)
	{
		int fired = 0;
		for (int tick = 0; tick < 60 && player._pmode == PM_SPELL; tick++) {
			TestPlayerProcessAction(player);
			player.poe2.tick();
			for (const Missile &missile : Missiles) {
				if (missile._mitype == type)
					fired++;
			}
			Missiles.clear();
		}
		return fired;
	}

	/** @brief A monster to aim at, south of the hero. */
	static void Target()
	{
		Monster &monster = Monsters[0];
		monster = {};
		monster.talkMsg = TEXT_NONE;
		monster.goal = MonsterGoal::Normal;
		monster.position.tile = monster.position.old = monster.position.future = Point { 20, 24 };
		monster.hitPoints = monster.maxHitPoints = Fixed26_6::fromInt(1000);
		pcursmonst = 0;
	}

	/** @brief A monster in the level that spells can hurt: unique, so a hit never knocks it back (that needs graphics). */
	static Monster &PlaceMonster(size_t slot, Point tile, bool unique = true)
	{
		Monster &monster = Monsters[slot];
		monster = {};
		monster.levelType = 0;
		monster.uniqueType = unique ? UniqueMonsterType::Garbud : UniqueMonsterType::None;
		monster.flags = 0;
		monster.rndItemSeed = 1;
		monster.talkMsg = TEXT_NONE;
		monster.goal = MonsterGoal::Normal;
		monster.position.tile = monster.position.old = monster.position.future = tile;
		monster.maxHitPoints = monster.hitPoints = Fixed26_6::fromInt(1000);
		ActiveMonsters[ActiveMonsterCount] = static_cast<unsigned>(slot);
		ActiveMonsterCount++;
		return monster;
	}

	static void LoadMonsters()
	{
		LoadMonsterData();
		LevelMonsterTypes[0].type = MT_NZOMBIE;
		ActiveMonsterCount = 0;
		// Damage to monsters goes out as a network message.
		SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
	}

	struct Shot {
		MissileID type;
		int8_t damageType;
		int8_t pierces;
		int16_t damagePercent;
	};

	/** @brief Casts the granted skill and runs the cast and its stream: what it fired. */
	static std::vector<Shot> CastAndCollect(Player &player)
	{
		std::vector<Shot> shots;
		if (!TryUseGrantedSkill(player))
			return shots;
		for (int tick = 0; tick < 60; tick++) {
			TestPlayerProcessAction(player);
			ProcessSkillVolley(player);
			player.poe2.tick();
			for (const Missile &missile : Missiles) {
				if (missile._mitype != MissileID::VisualEffect)
					shots.push_back({ missile._mitype, missile.poe2.damageType, missile.poe2.piercesLeft, missile.poe2.damagePercent });
			}
			Missiles.clear();
		}
		return shots;
	}
};

/** @brief The staves and wands whose skills are built (S14-S16), whether each is a wand, and the skill's rider. */
struct GrantRow {
	std::string_view base;
	WeaponSkill skill;
	bool wand;
	GrantedRider rider = GrantedRider::None;
};
constexpr GrantRow PlainGrants[] = {
	{ "Gelid Staff", WeaponSkill::FreezingShards, false },
	{ "Voltaic Staff", WeaponSkill::LightningBolt, false },
	{ "Pyrophyte Staff", WeaponSkill::SolarOrb, false },
	{ "Chiming Staff", WeaponSkill::ChimingBolt, false, GrantedRider::SigilEveryFourth },
	{ "Rending Staff", WeaponSkill::Soulrend, false },
	{ "Reaping Staff", WeaponSkill::Reap, false },
	{ "Icicle Staff", WeaponSkill::FrostLance, false },
	{ "Paralysing Staff", WeaponSkill::EnervatingNova, false },
	{ "Roaring Staff", WeaponSkill::EchoingBlast, false, GrantedRider::Echo },
	{ "Sanctified Staff", WeaponSkill::ConsecratedGround, false, GrantedRider::RegenBuff },
	{ "Dark Staff", WeaponSkill::DarkPact, false },
	{ "Ravenous Staff", WeaponSkill::FeastOfFlesh, false, GrantedRider::KillRestoreBoth },
	{ "Volatile Wand", WeaponSkill::VolatileBolt, true, GrantedRider::BurstOnKill },
	{ "Galvanic Wand", WeaponSkill::GalvanicBolt, true, GrantedRider::ChainOnce },
	{ "Acrid Wand", WeaponSkill::Decompose, true, GrantedRider::Plague },
	{ "Offering Wand", WeaponSkill::Exsanguinate, true, GrantedRider::KillRestoreLife },
	{ "Withered Wand", WeaponSkill::WitheringBolt, true, GrantedRider::Wither },
	{ "Bone Wand", WeaponSkill::BoneBolt, true, GrantedRider::StunBuildup },
	{ "Attuned Wand", WeaponSkill::ManaDrain, true, GrantedRider::ManaDrain },
	{ "Siphoning Wand", WeaponSkill::PowerSiphon, true, GrantedRider::CullFrenzy },
	{ "Frigid Wand", WeaponSkill::FrostBolt, true },
	{ "Torture Wand", WeaponSkill::TormentBolt, true, GrantedRider::Weaken },
	{ "Critical Wand", WeaponSkill::CriticalBolt, true, GrantedRider::EveryFifthDouble },
	{ "Primordial Wand", WeaponSkill::WitheringRay, true, GrantedRider::Wither },
	{ "Dueling Wand", WeaponSkill::TwinBolts, true },
};

/**
 * @brief A rider's worth on one target, in percent of the hits (plan 7.1): a bleed +75%, Wither and a cull about +10%,
 * a double every fifth cast +20%, an echo +60%, Decompose's plague the hit again; stun, mana, slow, Weaken, healing and
 * what lands on other monsters (a burst on a kill, a chain) add nothing on one target.
 */
int RiderPercent(const TableSkillData &row)
{
	if ((row.flags & TF_Bleed) != 0)
		return 100 + GrantedBleedPercent;
	switch (row.rider) {
	case GrantedRider::Wither:
	case GrantedRider::CullFrenzy:
		return 110;
	case GrantedRider::EveryFifthDouble:
		return 100 + 100 / CriticalBoltEvery;
	case GrantedRider::Echo:
		return 100 + EchoPercent;
	case GrantedRider::Plague:
		return 200;
	default:
		return 100;
	}
}

double CastSeconds(SkillSlot slot)
{
	const SkillSpeedRow *row = FindSkillSpeedRow(slot);
	return row == nullptr ? 1.0 : (row->castMs + row->addedMs) / 1000.0;
}

/** @brief Hits one target takes from a use: a fan's middle one or two, a stream's every shot, anything else once. */
int HitsOnOneTarget(const TableSkillData &row)
{
	if (row.shape == TableShape::Projectiles)
		return row.count <= 2 ? row.count : 1;
	if (row.shape == TableShape::Stream)
		return row.count;
	return 1;
}

/**
 * @brief The skill's damage a second on one target that stays put, in percent of the spell base before its curve
 * (plan 7.1 and 7.3): a use's hits on it, with a bleed's extra, over the use's time. Zones count what lands on the one
 * target; a curse's damage over time, which doesn't stack on one target, a storm that follows the hero and a granted
 * zone, one at a time (Solar Orb), count by their own time.
 */
double OneTargetPerSecond(const TableSkillData &row)
{
	double perUse = row.damagePercent * HitsOnOneTarget(row);
	if (row.shape == TableShape::Curse)
		return row.damagePercent * 20.0 / std::max<int>(row.duration, 1);
	if (row.shape == TableShape::Zone) {
		const int strikes = row.duration / std::max<int>(row.interval, 1);
		switch (row.zone) {
		case TableZone::Pulse:
			if (IsGrantedTableSkill(row))
				return row.damagePercent * 20.0 / std::max<int>(row.interval, 1);
			perUse *= strikes;
			break;
		case TableZone::RandomStrikes: {
			// Each strike lands on a random tile of the square; a 3x3 burst around it catches the target.
			const int side = 2 * row.radius + 1;
			perUse *= strikes * 9.0 / (side * side);
			break;
		}
		case TableZone::NearestStrike:
			return row.damagePercent * 20.0 / std::max<int>(row.interval, 1);
		default:
			break;
		}
	}
	perUse = perUse * RiderPercent(row) / 100;
	return perUse / CastSeconds(SkillSlot::ForAttack(row.skill));
}

TEST_F(Poe2GrantedSkillsTest, EachStaffAndWandCastsItsSkill)
{
	Player &player = *MyPlayer;
	for (const GrantRow &grant : PlainGrants) {
		Hold(player, grant.base, 70);
		EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::Granted) << grant.base;
		EXPECT_EQ(GrantedAttackSkill(player), grant.skill) << grant.base;
		const TableSkillData *row = FindTableSkill(grant.skill);
		ASSERT_NE(row, nullptr) << grant.base;
		EXPECT_TRUE(IsGrantedTableSkill(*row));
		EXPECT_NE(row->flags & TF_Spell, 0) << row->name;
		EXPECT_EQ(row->level, 0) << row->name;
		EXPECT_EQ(row->mana, 0) << row->name;
		EXPECT_EQ(row->tabs, 0) << row->name;
		EXPECT_EQ(row->curve, TableCurve::Fireball) << row->name;
		EXPECT_EQ(row->curveEnd, 312) << row->name;
		EXPECT_EQ(row->rider, grant.rider) << row->name;
		EXPECT_EQ(DefaultAttackNameForSet(player, player.poe2.weaponSet), row->name);
	}

	// The shapes of plan 6.12: bursts at the target, novas around the hero, piercing bolts, a stream of four.
	const auto shape = [](WeaponSkill skill) { return FindTableSkill(skill)->shape; };
	const auto radius = [](WeaponSkill skill) { return FindTableSkill(skill)->radius; };
	for (const WeaponSkill skill : { WeaponSkill::LightningBolt, WeaponSkill::Reap, WeaponSkill::DarkPact }) {
		EXPECT_EQ(shape(skill), TableShape::Burst);
		EXPECT_EQ(radius(skill), 1);
		EXPECT_TRUE(IsRangedGrantedSkill(skill));
	}
	EXPECT_EQ(shape(WeaponSkill::EnervatingNova), TableShape::Around);
	EXPECT_EQ(radius(WeaponSkill::EnervatingNova), 3);
	EXPECT_EQ(shape(WeaponSkill::ConsecratedGround), TableShape::Around);
	EXPECT_EQ(radius(WeaponSkill::ConsecratedGround), 2);
	EXPECT_EQ(shape(WeaponSkill::FeastOfFlesh), TableShape::Around);
	EXPECT_EQ(radius(WeaponSkill::FeastOfFlesh), 2);
	for (const WeaponSkill skill : { WeaponSkill::ChimingBolt, WeaponSkill::Soulrend, WeaponSkill::FrostLance })
		EXPECT_NE(FindTableSkill(skill)->flags & TF_Pierce, 0);
	EXPECT_EQ(shape(WeaponSkill::WitheringRay), TableShape::Stream);
	EXPECT_EQ(FindTableSkill(WeaponSkill::WitheringRay)->count, 4);
	EXPECT_EQ(FindTableSkill(WeaponSkill::TwinBolts)->count, 2);
	EXPECT_NE(FindTableSkill(WeaponSkill::DarkPact)->flags & TF_LifeCost, 0);
	EXPECT_NE(FindTableSkill(WeaponSkill::Soulrend)->flags & TF_Bleed, 0);
}

TEST_F(Poe2GrantedSkillsTest, BoltsFlyWithTheirOwnElement)
{
	Player &player = *MyPlayer;
	const struct {
		std::string_view base;
		MissileID missile;
		DamageType type;
		int count;
		bool pierce;
	} Bolts[] = {
		{ "Chiming Staff", MissileID::YellowFlare, DamageType::Lightning, 1, true },
		{ "Rending Staff", MissileID::RedFlare, DamageType::Chaos, 1, true },
		{ "Icicle Staff", MissileID::BlueFlare, DamageType::Cold, 1, true },
		{ "Frigid Wand", MissileID::BlueFlare, DamageType::Cold, 1, false },
		{ "Primordial Wand", MissileID::RedFlare, DamageType::Chaos, 4, false },
		{ "Dueling Wand", MissileID::OrangeFlare, DamageType::Physical, 2, false },
		{ "Volatile Wand", MissileID::OrangeFlare, DamageType::Fire, 1, false },
		{ "Galvanic Wand", MissileID::YellowFlare, DamageType::Lightning, 1, false },
		{ "Acrid Wand", MissileID::BloodStar, DamageType::Chaos, 1, false },
		{ "Offering Wand", MissileID::RedFlare, DamageType::Physical, 1, false },
	};
	for (const auto &bolt : Bolts) {
		Hold(player, bolt.base, 70);
		Target();
		const std::vector<Shot> shots = CastAndCollect(player);
		ASSERT_EQ(static_cast<int>(shots.size()), bolt.count) << bolt.base;
		const TableSkillData &row = *FindTableSkill(GrantedAttackSkill(player));
		for (const Shot &shot : shots) {
			EXPECT_EQ(shot.type, bolt.missile) << bolt.base;
			// The flare's own element is Magic; the bolt's hits take the skill's.
			EXPECT_EQ(shot.damageType, static_cast<int8_t>(bolt.type)) << bolt.base;
			EXPECT_EQ(shot.pierces >= 3, bolt.pierce) << bolt.base;
			EXPECT_EQ(shot.damagePercent, GemDamagePercent(SkillSlot::ForAttack(row.skill), player.poe2.effectiveLevel(row.skill)) * row.damagePercent / 100) << bolt.base;
		}
		Stand(player);
	}
	EXPECT_EQ(GetMissileData(MissileID::RedFlare).damageType(), DamageType::Cold);
}

TEST_F(Poe2GrantedSkillsTest, BurstsHitAroundTheTargetAndNovasAroundTheHero)
{
	Player &player = *MyPlayer;
	LoadMonsters();
	// Lightning Bolt: the target and what stands next to it, not what's two tiles off.
	Hold(player, "Voltaic Staff", 70);
	Monster &target = PlaceMonster(0, { 20, 24 });
	Monster &beside = PlaceMonster(1, { 21, 25 });
	Monster &apart = PlaceMonster(2, { 20, 27 });
	pcursmonst = 0;
	CastAndCollect(player);
	EXPECT_LT(target.hitPoints, target.maxHitPoints);
	EXPECT_LT(beside.hitPoints, beside.maxHitPoints);
	EXPECT_EQ(apart.hitPoints, apart.maxHitPoints);
	// The target takes the whole hit; the one beside it its per-target share.
	const auto taken = [](const Monster &monster) { return (monster.maxHitPoints - monster.hitPoints).raw(); };
	const int share = GrantedExtraTargetPercent(*FindTableSkill(WeaponSkill::LightningBolt));
	EXPECT_EQ(share, 48);
	EXPECT_NEAR(taken(beside), taken(target) * share / 100, 2);

	// Enervating Nova: everything within 3 tiles of the hero, wherever the cursor is.
	Stand(player);
	ActiveMonsterCount = 0;
	Monster &close = PlaceMonster(0, { 22, 21 });
	Monster &edge = PlaceMonster(1, { 20, 23 });
	Monster &far = PlaceMonster(2, { 20, 25 });
	pcursmonst = 2;
	Hold(player, "Paralysing Staff", 70);
	CastAndCollect(player);
	EXPECT_LT(close.hitPoints, close.maxHitPoints);
	EXPECT_LT(edge.hitPoints, edge.maxHitPoints);
	EXPECT_EQ(far.hitPoints, far.maxHitPoints);
	// The one nearest the aim (the cursor's monster beyond the nova) takes it in full, the other its share.
	EXPECT_NEAR(taken(close), taken(edge) * GrantedExtraTargetPercent(*FindTableSkill(WeaponSkill::EnervatingNova)) / 100, 2);
	ActiveMonsterCount = 0;
}

TEST_F(Poe2GrantedSkillsTest, AStaffsProjectilesHitTheFirstInFullAndEachOnce)
{
	Player &player = *MyPlayer;
	LoadMonsters();
	Monster &first = PlaceMonster(0, { 20, 24 });
	Monster &second = PlaceMonster(1, { 21, 25 });
	Missile shard {};
	SetMissileSkill(shard, player, SkillSlot::ForAttack(WeaponSkill::FreezingShards));
	shard.poe2.grantedCast = 7;
	// The cast's first monster: in full, and no shard of the cast hits it again.
	EXPECT_FALSE(GrantedMissileSkipsMonster(shard, first));
	EXPECT_EQ(GrantedMissileTargetPercent(shard, player, first), 100);
	EXPECT_TRUE(GrantedMissileSkipsMonster(shard, first));
	// Another: its per-target share (30% of the spell base for a 1.2 second cast, of a 64% shard).
	const int share = GrantedExtraTargetPercent(*FindTableSkill(WeaponSkill::FreezingShards));
	EXPECT_EQ(share, 46);
	EXPECT_EQ(GrantedMissileTargetPercent(shard, player, second), share);
	EXPECT_TRUE(GrantedMissileSkipsMonster(shard, second));
	// The next cast starts over, while the one before keeps its first.
	Missile next = shard;
	next.poe2.grantedCast = 8;
	EXPECT_FALSE(GrantedMissileSkipsMonster(next, first));
	EXPECT_EQ(GrantedMissileTargetPercent(next, player, second), 100);
	EXPECT_EQ(GrantedMissileTargetPercent(next, player, first), share);
	Monster &third = PlaceMonster(2, { 22, 25 });
	EXPECT_EQ(GrantedMissileTargetPercent(shard, player, third), share);

	// A piercing bolt's line: the first in full, the ones behind for their share, the bleed included.
	EXPECT_EQ(GrantedExtraTargetPercent(*FindTableSkill(WeaponSkill::Soulrend)), 45);
	EXPECT_EQ(GrantedExtraTargetPercent(*FindTableSkill(WeaponSkill::FrostLance)), 46);
	EXPECT_EQ(GrantedExtraTargetPercent(*FindTableSkill(WeaponSkill::ChimingBolt)), 48);
	// Solar Orb's pulse: 25% a second of its 1.4 second pulse; Echoing Blast's echo counts.
	EXPECT_EQ(GrantedExtraTargetPercent(*FindTableSkill(WeaponSkill::SolarOrb)), 49);
	EXPECT_EQ(GrantedExtraTargetPercent(*FindTableSkill(WeaponSkill::EchoingBlast)), 45);

	// Wand bolts, a cast without a serial, and anything outside POE2 hit in full.
	Missile bolt {};
	SetMissileSkill(bolt, player, SkillSlot::ForAttack(WeaponSkill::TwinBolts));
	EXPECT_FALSE(GrantedMissileSkipsMonster(bolt, first));
	EXPECT_EQ(GrantedMissileTargetPercent(bolt, player, first), 100);
	EXPECT_EQ(GrantedExtraTargetPercent(*FindTableSkill(WeaponSkill::TwinBolts)), 100);
	EXPECT_EQ(GrantedExtraTargetPercent(*FindTableSkill(WeaponSkill::Blizzard)), 100);
	gbIsPoe2 = false;
	EXPECT_FALSE(GrantedMissileSkipsMonster(shard, first));
	EXPECT_EQ(GrantedMissileTargetPercent(shard, player, third), 100);
	gbIsPoe2 = true;

	// A cast of a staff's projectiles takes the next serial and gives it to its shards.
	Hold(player, "Gelid Staff", 70);
	Target();
	const uint16_t before = player.poe2.grantedState.castSerial;
	ASSERT_TRUE(TryUseGrantedSkill(player));
	uint16_t carried = 0;
	for (int tick = 0; tick < 60 && carried == 0; tick++) {
		TestPlayerProcessAction(player);
		for (const Missile &missile : Missiles)
			carried = std::max(carried, missile.poe2.grantedCast);
	}
	Missiles.clear();
	EXPECT_EQ(player.poe2.grantedState.castSerial, before + 1);
	EXPECT_EQ(carried, before + 1);
	ActiveMonsterCount = 0;
}

TEST_F(Poe2GrantedSkillsTest, SoulrendBleedsAndDarkPactCostsLife)
{
	Player &player = *MyPlayer;
	LoadMonsters();
	Monster &monster = PlaceMonster(0, { 20, 24 });

	// A Soulrend hit bleeds for 75% of itself over 5 seconds; a Frost Bolt hit doesn't.
	Missile frost {};
	SetMissileSkill(frost, player, SkillSlot::ForAttack(WeaponSkill::FrostBolt));
	OnTableMissileHit(frost, player, monster, 40, false);
	EXPECT_EQ(monster.poe2.bleedTicks, 0);
	Missile rend {};
	SetMissileSkill(rend, player, SkillSlot::ForAttack(WeaponSkill::Soulrend));
	OnTableMissileHit(rend, player, monster, 40, false);
	EXPECT_EQ(monster.poe2.bleedTicks, GrantedBleedTicks);
	EXPECT_EQ(monster.poe2.bleedSkill, WeaponSkill::Soulrend);
	EXPECT_EQ(monster.poe2.bleedDamagePerTick, 40 * 64 * GrantedBleedPercent / 100 / GrantedBleedTicks);
	EXPECT_EQ(GrantedBleedPercent, 75);
	// Nothing outside POE2.
	monster.poe2 = {};
	gbIsPoe2 = false;
	OnTableMissileHit(rend, player, monster, 40, false);
	EXPECT_EQ(monster.poe2.bleedTicks, 0);
	gbIsPoe2 = true;

	// Dark Pact takes 5% of the hero's current life a cast, and never the last point.
	Hold(player, "Dark Staff", 70);
	pcursmonst = 0;
	CastAndCollect(player);
	EXPECT_EQ(player._pHitPoints, Fixed26_6::fromInt(190));
	EXPECT_LT(monster.hitPoints, monster.maxHitPoints);
	Stand(player);
	player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(1);
	EXPECT_FALSE(TryUseGrantedSkill(player));
	ActiveMonsterCount = 0;
}

TEST_F(Poe2GrantedSkillsTest, TheWandRidersWork)
{
	Player &player = *MyPlayer;
	LoadMonsters();
	const auto hit = [&player](WeaponSkill skill, Monster &monster) {
		Missile bolt {};
		SetMissileSkill(bolt, player, SkillSlot::ForAttack(skill));
		OnTableMissileHit(bolt, player, monster, 10, false);
	};

	// Wither: a stack a hit up to 10 (5% more chaos damage taken each), for 4 seconds; more from Withering Touch stay.
	Monster &monster = PlaceMonster(0, { 20, 24 });
	for (int i = 0; i < 12; i++)
		hit(WeaponSkill::WitheringBolt, monster);
	EXPECT_EQ(monster.poe2.witherStacks, GrantedWitherMaxStacks);
	EXPECT_EQ(monster.poe2.witherTicks, GrantedWitherTicks);
	EXPECT_EQ(SupportHitDamagePercent(monster, DamageType::Chaos), 150);
	EXPECT_EQ(SupportHitDamagePercent(monster, DamageType::Physical), 100);
	monster.poe2.witherStacks = 12;
	hit(WeaponSkill::WitheringRay, monster);
	EXPECT_EQ(monster.poe2.witherStacks, 12);
	monster.poe2 = {};

	// Weaken for 2 seconds.
	hit(WeaponSkill::TormentBolt, monster);
	EXPECT_EQ(monster.poe2.weakenTicks, TormentWeakenTicks);
	monster.poe2 = {};

	// Mana Drain: 1 + level / 3 mana, at most once a second from a monster, and a 20% slow for 2 seconds.
	EXPECT_EQ(ManaDrainMana(1), 1);
	EXPECT_EQ(ManaDrainMana(9), 4);
	EXPECT_EQ(ManaDrainMana(20), 7);
	player.poe2.granted[0] = { SkillSlot::ForAttack(WeaponSkill::ManaDrain), 9, 0 };
	player._pMaxMana = player._pMaxManaBase = Fixed26_6::fromInt(100);
	hit(WeaponSkill::ManaDrain, monster);
	EXPECT_EQ(player._pMana, Fixed26_6::fromInt(4));
	EXPECT_EQ(monster.poe2.chillTicks, ManaDrainSlowTicks);
	EXPECT_EQ(monster.poe2.chillPercent, ManaDrainSlowPercent);
	hit(WeaponSkill::ManaDrain, monster);
	EXPECT_EQ(player._pMana, Fixed26_6::fromInt(4));
	Monster &other = PlaceMonster(1, { 21, 24 });
	hit(WeaponSkill::ManaDrain, other);
	EXPECT_EQ(player._pMana, Fixed26_6::fromInt(8));
	for (int tick = 0; tick < ManaDrainCooldownTicks; tick++)
		ProcessMonsterAilments(monster);
	hit(WeaponSkill::ManaDrain, monster);
	EXPECT_EQ(player._pMana, Fixed26_6::fromInt(12));
	player.poe2.granted = {};
	monster.poe2 = {};

	// Power Siphon culls on 10% life (5% for a unique) and gives a Frenzy charge.
	monster.uniqueType = UniqueMonsterType::None;
	monster.hitPoints = Fixed26_6::fromInt(110);
	hit(WeaponSkill::PowerSiphon, monster);
	EXPECT_EQ(monster.hitPoints, Fixed26_6::fromInt(110));
	EXPECT_EQ(player.poe2.frenzyCharges, 0);
	monster.hitPoints = Fixed26_6::fromInt(100);
	hit(WeaponSkill::PowerSiphon, monster);
	EXPECT_TRUE(monster.hasNoLife());
	EXPECT_TRUE(monster.poe2.culled);
	EXPECT_EQ(player.poe2.frenzyCharges, 1);
	other.hitPoints = Fixed26_6::fromInt(60);
	hit(WeaponSkill::PowerSiphon, other);
	EXPECT_EQ(other.hitPoints, Fixed26_6::fromInt(60));
	other.hitPoints = Fixed26_6::fromInt(50);
	hit(WeaponSkill::PowerSiphon, other);
	EXPECT_TRUE(other.hasNoLife());
	EXPECT_EQ(player.poe2.frenzyCharges, 2);

	// Bone Bolt's hits build twice the heavy stun; another bolt's don't.
	const int bare = UniqueStunBuildupPercent(player);
	{
		const SupportHitScope scope(player, SkillSlot::ForAttack(WeaponSkill::BoneBolt));
		EXPECT_EQ(HitGrantedRider(), GrantedRider::StunBuildup);
		EXPECT_EQ(UniqueStunBuildupPercent(player), 2 * bare);
	}
	{
		const SupportHitScope scope(player, SkillSlot::ForAttack(WeaponSkill::TwinBolts));
		EXPECT_EQ(UniqueStunBuildupPercent(player), bare);
	}
	EXPECT_EQ(HitGrantedRider(), GrantedRider::None);
	ActiveMonsterCount = 0;
}

TEST_F(Poe2GrantedSkillsTest, TheKillAndSpreadRidersWork)
{
	Player &player = *MyPlayer;
	LoadMonsters();
	const auto hit = [&player](WeaponSkill skill, Monster &monster) {
		Missile bolt {};
		SetMissileSkill(bolt, player, SkillSlot::ForAttack(skill));
		OnTableMissileHit(bolt, player, monster, 40, false);
	};
	Monster &first = PlaceMonster(0, { 20, 24 }, false);
	Monster &near = PlaceMonster(1, { 21, 24 }, false);
	Monster &twoAway = PlaceMonster(2, { 20, 26 }, false);
	Monster &threeAway = PlaceMonster(3, { 23, 24 }, false);

	// Galvanic Bolt: the nearest other monster within 2 tiles takes half the hit.
	hit(WeaponSkill::GalvanicBolt, first);
	EXPECT_EQ(first.hitPoints, first.maxHitPoints);
	EXPECT_LT(near.hitPoints, near.maxHitPoints);
	EXPECT_GE(near.hitPoints, near.maxHitPoints - Fixed26_6::fromInt(20));
	EXPECT_EQ(twoAway.hitPoints, twoAway.maxHitPoints);
	near.hitPoints = near.maxHitPoints;

	// Volatile Bolt: a monster it kills bursts for half the hit on what stands next to it.
	first.hitPoints = Fixed26_6::fromInt(0);
	hit(WeaponSkill::VolatileBolt, first);
	EXPECT_LT(near.hitPoints, near.maxHitPoints);
	EXPECT_GE(near.hitPoints, near.maxHitPoints - Fixed26_6::fromInt(20));
	EXPECT_EQ(twoAway.hitPoints, twoAway.maxHitPoints);
	near.hitPoints = near.maxHitPoints;

	// Exsanguinate bleeds; a kill on a bleeding monster restores 1% of life, on another nothing.
	first.hitPoints = first.maxHitPoints;
	hit(WeaponSkill::Exsanguinate, first);
	EXPECT_EQ(first.poe2.bleedTicks, GrantedBleedTicks);
	player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(100);
	first.hitPoints = Fixed26_6::fromInt(0);
	hit(WeaponSkill::Exsanguinate, first);
	EXPECT_EQ(player._pHitPoints, Fixed26_6::fromInt(102));
	first.poe2 = {};
	hit(WeaponSkill::Exsanguinate, first);
	EXPECT_EQ(player._pHitPoints, Fixed26_6::fromInt(102));

	// Decompose: the hit again as chaos over 4 seconds; when the monster dies the plague passes to the one nearest monster.
	first.hitPoints = first.maxHitPoints;
	hit(WeaponSkill::Decompose, first);
	EXPECT_EQ(first.poe2.plagueTicks, PlagueTicks);
	EXPECT_EQ(first.poe2.chaosTicks, PlagueTicks);
	EXPECT_EQ(first.poe2.chaosSkill, WeaponSkill::Decompose);
	EXPECT_EQ(first.poe2.chaosDamagePerTick, 40 * 64 / PlagueTicks);
	EXPECT_EQ(first.poe2.bleedTicks, 0);
	OnTableCursedMonsterKilled(first);
	EXPECT_EQ(near.poe2.plagueTicks, PlagueTicks);
	EXPECT_EQ(near.poe2.chaosSkill, WeaponSkill::Decompose);
	EXPECT_EQ(twoAway.poe2.plagueTicks, 0);
	EXPECT_EQ(threeAway.poe2.plagueTicks, 0);
	// Its time runs down with the curses'.
	ProcessTableCurses(near);
	EXPECT_EQ(near.poe2.plagueTicks, PlagueTicks - 1);
	ActiveMonsterCount = 0;
}

TEST_F(Poe2GrantedSkillsTest, TheStaffRidersWork)
{
	Player &player = *MyPlayer;
	LoadMonsters();

	// Feast of Flesh: what it kills restores 2% of life and of mana.
	Hold(player, "Ravenous Staff", 70);
	player._pMaxMana = player._pMaxManaBase = Fixed26_6::fromInt(100);
	player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(100);
	Monster &weak = PlaceMonster(0, { 21, 21 }, false);
	weak.hitPoints = Fixed26_6::fromInt(1);
	pcursmonst = -1;
	CastAndCollect(player);
	EXPECT_TRUE(weak.hasNoLife());
	EXPECT_EQ(player._pHitPoints, Fixed26_6::fromInt(104));
	EXPECT_EQ(player._pMana, Fixed26_6::fromInt(2));
	ActiveMonsterCount = 0;

	// Consecrated Ground: then 1% of maximum life a second for 4 seconds; a second cast starts it over.
	Stand(player);
	Hold(player, "Sanctified Staff", 70);
	player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(100);
	CastAndCollect(player);
	Stand(player);
	player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(100);
	CastAndCollect(player);
	EXPECT_EQ(player.poe2.grantedState.regenRaw, Fixed26_6::fromInt(8).raw());
	EXPECT_EQ(player.poe2.grantedState.regenTicks, ConsecrateRegenTicks);
	for (int tick = 0; tick < ConsecrateRegenTicks; tick++)
		ProcessSkillZones(player);
	EXPECT_EQ(player.poe2.grantedState.regenTicks, 0);
	EXPECT_EQ(player._pHitPoints, Fixed26_6::fromInt(108));

	// Echoing Blast goes off again a quarter second later for 60% of the hit.
	Stand(player);
	Hold(player, "Roaring Staff", 70);
	Monster &target = PlaceMonster(0, { 20, 24 }, false);
	pcursmonst = 0;
	CastAndCollect(player);
	const Fixed26_6 first = target.maxHitPoints - target.hitPoints;
	EXPECT_GT(first, Fixed26_6::fromInt(0));
	EXPECT_EQ(SkillZoneCount(), 1U);
	for (int tick = 0; tick <= EchoDelayTicks; tick++)
		ProcessSkillZones(player);
	EXPECT_EQ(SkillZoneCount(), 0U);
	const Fixed26_6 second = target.maxHitPoints - target.hitPoints - first;
	EXPECT_NEAR(second.raw(), first.raw() * EchoPercent / 100, 64);
	ActiveMonsterCount = 0;
}

TEST_F(Poe2GrantedSkillsTest, SolarOrbIsOneAtATime)
{
	Player &player = *MyPlayer;
	Hold(player, "Pyrophyte Staff", 70);
	Target();
	const TableSkillData &row = *FindTableSkill(WeaponSkill::SolarOrb);
	EXPECT_EQ(row.shape, TableShape::Zone);
	EXPECT_EQ(row.zone, TableZone::Pulse);
	EXPECT_EQ(row.radius, 1);
	EXPECT_EQ(row.interval, 28);
	EXPECT_EQ(row.duration, 200);
	EXPECT_EQ(row.type, DamageType::Fire);
	EXPECT_TRUE(IsRangedGrantedSkill(WeaponSkill::SolarOrb));
	CastAndCollect(player);
	EXPECT_EQ(SkillZoneCount(), 1U);
	// A second orb replaces the first, even with Legion, which adds a zone to other skills.
	player.poe2.defaultSupports[0] = SupportGem::Legion;
	player._pMana = player._pManaBase = Fixed26_6::fromInt(100);
	Stand(player);
	player._pMana = player._pManaBase = Fixed26_6::fromInt(100);
	CastAndCollect(player);
	EXPECT_EQ(SkillZoneCount(), 1U);
	player.poe2.defaultSupports.fill(SupportGem::None);
	// It pulses every 1.4 seconds for its 10: 8 pulses.
	int pulses = 0;
	for (int tick = 0; tick < 400 && SkillZoneCount() > 0; tick++) {
		const size_t before = Missiles.size();
		ProcessSkillZones(player);
		if (Missiles.size() > before)
			pulses++;
	}
	EXPECT_EQ(pulses, 8);
}

TEST_F(Poe2GrantedSkillsTest, ChimingBoltLeavesASigilEveryFourthCast)
{
	Player &player = *MyPlayer;
	LoadMonsters();
	Hold(player, "Chiming Staff", 70);
	Target();
	Monster &monster = PlaceMonster(1, { 25, 25 }, false);
	const int outside = UniqueSpellHitPercent(player, monster, DamageType::Lightning);
	for (int cast = 1; cast < SigilEvery; cast++) {
		CastAndCollect(player);
		Stand(player);
		EXPECT_EQ(player.poe2.grantedState.sigilTicks, 0);
	}
	CastAndCollect(player);
	EXPECT_EQ(player.poe2.grantedState.sigilTicks, SigilTicks);
	EXPECT_EQ(player.poe2.grantedState.chimingCasts, 0);
	EXPECT_EQ(player.poe2.grantedState.sigilX, player.position.tile.x);
	EXPECT_EQ(player.poe2.grantedState.sigilY, player.position.tile.y);
	// +20% spell damage while the hero stands in it: bolts (the spell hit rules) and bursts (table spell hits).
	EXPECT_EQ(SigilSpellDamagePercent(player), SigilSpellDamagePercentInside);
	EXPECT_EQ(SigilSpellDamagePercentInside, 120);
	EXPECT_EQ(UniqueSpellHitPercent(player, monster, DamageType::Lightning), outside * 120 / 100);
	player.position.tile = { 20 + SigilRadius + 1, 20 };
	EXPECT_EQ(SigilSpellDamagePercent(player), 100);
	player.position.tile = { 20 + SigilRadius, 20 };
	EXPECT_EQ(SigilSpellDamagePercent(player), SigilSpellDamagePercentInside);
	// It lasts 4 seconds, and stays behind on a new level.
	for (int tick = 0; tick < SigilTicks; tick++)
		ProcessSkillZones(player);
	EXPECT_EQ(SigilSpellDamagePercent(player), 100);
	player.poe2.grantedState.sigilTicks = SigilTicks;
	ClearSkillZones();
	EXPECT_EQ(player.poe2.grantedState.sigilTicks, 0);
	ActiveMonsterCount = 0;
}

TEST_F(Poe2GrantedSkillsTest, AreaSupportsWidenTheNovasByATile)
{
	// The skill-balance review: Feast of Flesh's 2% life and mana a kill is fine at radius 2; these are the widest
	// radii the best pair of supports gives each nova.
	Player &player = *MyPlayer;
	for (const WeaponSkill skill : { WeaponSkill::FeastOfFlesh, WeaponSkill::ConsecratedGround, WeaponSkill::EnervatingNova }) {
		const SkillSlot slot = SkillSlot::ForAttack(skill);
		player.poe2.granted[0] = { slot, 1, 0 };
		const int radius = FindTableSkill(skill)->radius;
		int widest = radius;
		for (int a = 1; a < static_cast<int>(SupportGem::Count); a++) {
			for (int b = a + 1; b < static_cast<int>(SupportGem::Count); b++) {
				const auto first = static_cast<SupportGem>(a);
				const auto second = static_cast<SupportGem>(b);
				if (!SupportGemCanSupport(first, slot) || !SupportGemCanSupport(second, slot) || SupportsConflict(first, second))
					continue;
				player.poe2.defaultSupports[0] = first;
				player.poe2.defaultSupports[1] = second;
				widest = std::max(widest, SupportAreaRadius(player, slot, radius));
			}
		}
		player.poe2.defaultSupports.fill(SupportGem::None);
		std::printf("%s: radius %d, %d with supports\n", std::string(WeaponSkillName(skill)).c_str(), radius, widest);
		EXPECT_LE(widest, radius + 1) << WeaponSkillName(skill);
	}
	player.poe2.granted = {};
}

TEST_F(Poe2GrantedSkillsTest, CriticalBoltDoublesEveryFifthCast)
{
	Player &player = *MyPlayer;
	Hold(player, "Critical Wand", 70);
	Target();
	const int normal = GemDamagePercent(SkillSlot::ForAttack(WeaponSkill::CriticalBolt), player.poe2.effectiveLevel(WeaponSkill::CriticalBolt)) * 34 / 100;
	for (int cast = 1; cast <= 10; cast++) {
		const std::vector<Shot> shots = CastAndCollect(player);
		ASSERT_EQ(shots.size(), 1U);
		EXPECT_NEAR(shots[0].damagePercent, cast % CriticalBoltEvery == 0 ? normal * 2 : normal, 1) << cast;
		Stand(player);
	}
	EXPECT_EQ(player.poe2.grantedState.criticalCasts, 0);
}

TEST_F(Poe2GrantedSkillsTest, ManaDrainDealsNoDamageButDrainsAndSlows)
{
	// Curtis 2026-10-29: "it says Mana Drain, so it should just drain mana from enemies". Its bolt flies and hits as
	// before, for nothing, and the hit still gives the hero mana and slows the enemy.
	Player &player = *MyPlayer;
	LoadMonsters();
	// Open floor for the bolt.
	SOLData[0] = TileProperties::None;
	for (int x = 10; x < 40; x++) {
		for (int y = 10; y < 40; y++) {
			dPiece[x][y] = 0;
			dObject[x][y] = 0;
			dMonster[x][y] = 0;
		}
	}
	const TableSkillData &row = *FindTableSkill(WeaponSkill::ManaDrain);
	EXPECT_EQ(row.damagePercent, 0);
	EXPECT_EQ(row.rider, GrantedRider::ManaDrain);
	// No longer a damage skill: no damage tag, so no damage supports, and its details show its effect, not a hit.
	EXPECT_EQ(SkillTags(SkillSlot::ForAttack(WeaponSkill::ManaDrain)) & ST_Damage, 0U);
	EXPECT_EQ(SkillHitAverage(player, SkillSlot::ForAttack(WeaponSkill::ManaDrain), 9), 0.0);

	Hold(player, "Attuned Wand", 70);
	ASSERT_EQ(GrantedAttackSkill(player), WeaponSkill::ManaDrain);
	bool damageLines = false;
	bool effectLine = false;
	for (const SkillDetailLine &line : SkillDetails(player, DefaultAttackRow)) {
		damageLines = damageLines || line.label == "Damage" || line.label == "Damage per hit" || line.label == "Average hit";
		effectLine = effectLine || line.label.starts_with("No damage");
	}
	EXPECT_FALSE(damageLines);
	EXPECT_TRUE(effectLine);
	Monster &monster = PlaceMonster(0, { 20, 24 });
	dMonster[20][24] = 1;
	pcursmonst = 0;
	player._pMaxMana = player._pMaxManaBase = Fixed26_6::fromInt(100);
	ASSERT_TRUE(TryUseGrantedSkill(player));
	EXPECT_EQ(player._pMana, Fixed26_6::fromInt(0)) << "free with no supports";
	for (int tick = 0; tick < 60 && player._pmode == PM_SPELL; tick++) {
		TestPlayerProcessAction(player);
		player.poe2.tick();
	}
	Missile *bolt = nullptr;
	for (Missile &missile : Missiles) {
		if (missile._mitype == MissileID::BlueFlare2)
			bolt = &missile;
	}
	ASSERT_NE(bolt, nullptr);
	EXPECT_EQ(bolt->poe2.damagePercent, 0);
	const Fixed26_6 manaBefore = player._pMana;
	for (int tick = 0; tick < 60 && !bolt->_miDelFlag; tick++)
		GetMissileData(bolt->_mitype).processFn(*bolt);
	EXPECT_TRUE(bolt->_miDelFlag) << "the bolt reached the monster";
	EXPECT_EQ(monster.hitPoints, monster.maxHitPoints) << "no damage";
	EXPECT_GT(ManaDrainMana(player.poe2.effectiveLevel(WeaponSkill::ManaDrain)), 0);
	EXPECT_EQ(player._pMana, manaBefore + Fixed26_6::fromInt(ManaDrainMana(player.poe2.effectiveLevel(WeaponSkill::ManaDrain))));
	EXPECT_EQ(monster.poe2.chillTicks, ManaDrainSlowTicks);
	EXPECT_EQ(monster.poe2.chillPercent, ManaDrainSlowPercent);
	dMonster[20][24] = 0;
	ActiveMonsterCount = 0;
}

TEST_F(Poe2GrantedSkillsTest, TheGrantedSkillsHitAsPlanned)
{
	// Plan 7.2: the hit (% of the spell base), the cast time, and the damage a second on one target, within 5%. The
	// staves' skills are priced per target (the coordinator, 10-28): 70-85% of Staff Bolt's on one target, and every other
	// monster a cast hits takes at most 25% of the spell base a second of the cast.
	const struct {
		WeaponSkill skill;
		int hit;
		int castMs;
		int oneTarget;
	} Planned[] = {
		{ WeaponSkill::FreezingShards, 64, 1200, 53 }, // one shard a monster a cast
		{ WeaponSkill::LightningBolt, 46, 900, 51 },
		{ WeaponSkill::ChimingBolt, 46, 900, 51 },
		{ WeaponSkill::Soulrend, 28, 900, 54 }, // with the bleed
		{ WeaponSkill::Reap, 64, 1200, 53 },
		{ WeaponSkill::FrostLance, 48, 900, 53 },
		{ WeaponSkill::EnervatingNova, 48, 1000, 48 },
		{ WeaponSkill::ConsecratedGround, 63, 1250, 50 },
		// Dark Pact keeps the most of the staves for its 5% life cost (the skill-balance review).
		{ WeaponSkill::DarkPact, 50, 900, 56 },
		{ WeaponSkill::FeastOfFlesh, 45, 900, 50 },
		{ WeaponSkill::FrostBolt, 40, 750, 53 },
		// Riders in: Wither and a cull about +10%, Critical Bolt's double every fifth cast +20%.
		{ WeaponSkill::WitheringBolt, 37, 750, 54 }, // 49 with Wither
		{ WeaponSkill::BoneBolt, 38, 750, 51 },
		{ WeaponSkill::ManaDrain, 0, 850, 0 }, // no damage, only mana and a slow (Curtis 2026-10-29)
		{ WeaponSkill::PowerSiphon, 35, 700, 55 }, // 50 with the cull
		{ WeaponSkill::TormentBolt, 38, 750, 51 },
		{ WeaponSkill::CriticalBolt, 34, 750, 54 }, // on average
		{ WeaponSkill::WitheringRay, 9, 750, 53 }, // 48 with Wither
		{ WeaponSkill::EchoingBlast, 31, 900, 55 }, // with the echo's 60%
		{ WeaponSkill::SolarOrb, 71, 900, 51 }, // a pulse every 1.4 seconds, one orb at a time
		{ WeaponSkill::VolatileBolt, 40, 800, 50 },
		{ WeaponSkill::GalvanicBolt, 34, 650, 52 },
		{ WeaponSkill::Decompose, 22, 800, 55 }, // the hit, then as much again over 4 seconds
		{ WeaponSkill::Exsanguinate, 24, 800, 52 }, // with the bleed
		// Both bolts. Plan 7.2's 24 a bolt (69) beat Condemn and Corpse Explosion; the wand bolts beat no gem but Haunt,
		// Glacial Spike and Locust Swarm, so it is a wand bolt like the others.
		{ WeaponSkill::TwinBolts, 19, 700, 54 },
	};
	for (const auto &planned : Planned) {
		const TableSkillData &row = *FindTableSkill(planned.skill);
		const SkillSlot slot = SkillSlot::ForAttack(planned.skill);
		EXPECT_EQ(row.damagePercent, planned.hit) << row.name;
		EXPECT_EQ(SkillCastTimeMs(slot), planned.castMs) << row.name;
		// No speed damage: each hit is set for its cast time.
		EXPECT_EQ(SkillSpeedDamagePercent(slot), 100) << row.name;
		EXPECT_NEAR(OneTargetPerSecond(row), planned.oneTarget, planned.oneTarget * 0.05) << row.name;

		const double cast = planned.castMs / 1000.0;
		const double hit = row.damagePercent * RiderPercent(row) / 100.0;
		const bool wand = std::find_if(std::begin(PlainGrants), std::end(PlainGrants), [&](const GrantRow &g) { return g.skill == planned.skill; })->wand;
		if (wand) {
			// Wand bolts: 55% a second on one target (plan 7.1), and every monster they hit takes the whole hit.
			EXPECT_LE(OneTargetPerSecond(row), 55 * 1.001) << row.name;
			EXPECT_EQ(row.flags & TF_PerTarget, 0) << row.name;
			EXPECT_EQ(GrantedExtraTargetPercent(row), 100) << row.name;
			continue;
		}
		// Staves: one target at 70-85% of Staff Bolt's, under Condemn's 56 (the cheapest gem that isn't excused against
		// them); every other monster a cast hits takes at most 25% of the spell base a second of the cast, or of a zone's
		// pulse, its bleed and echo included.
		const double staffBolt = GrantedStaffBoltPercent / CastSeconds(SkillSlot::ForDefault(static_cast<uint8_t>(DefaultAttack::StaffBolt)));
		EXPECT_GE(OneTargetPerSecond(row), staffBolt * 0.70) << row.name;
		EXPECT_LE(OneTargetPerSecond(row), staffBolt * 0.85) << row.name;
		EXPECT_LT(OneTargetPerSecond(row), 56.0) << row.name;
		EXPECT_NE(row.flags & TF_PerTarget, 0) << row.name;
		const double window = row.shape == TableShape::Zone ? row.interval / 20.0 : cast;
		EXPECT_LT(GrantedExtraTargetPercent(row), 100) << row.name;
		EXPECT_LE(hit * GrantedExtraTargetPercent(row) / 100.0, ExtraTargetPercentPerSecond * window * 1.01) << row.name;
		// Not so far under that the area gives nothing: within a tenth of the budget.
		EXPECT_GE(hit * GrantedExtraTargetPercent(row) / 100.0, ExtraTargetPercentPerSecond * window * 0.9) << row.name;
		std::printf("%-18s one target %5.1f%% a second (%2.0f%% of Staff Bolt), each other %4.1f%% (%d%% of the hit)\n", std::string(row.name).c_str(),
		    OneTargetPerSecond(row), OneTargetPerSecond(row) * 100 / staffBolt, hit * GrantedExtraTargetPercent(row) / 100.0 / window, GrantedExtraTargetPercent(row));
	}
	// Staff Bolt, the free bolt of the Ashen and Spriggan staves: 61% at 0.9 seconds.
	EXPECT_NEAR(GrantedStaffBoltPercent / CastSeconds(SkillSlot::ForDefault(static_cast<uint8_t>(DefaultAttack::StaffBolt))), 68, 68 * 0.05);
}

/**
 * @brief The most a support pair raises the skill's damage a second on one target: their more and less damage on its
 * hits, and their cast speed. Extra projectiles fan out and aren't counted on one target; repeats and triggers aren't
 * counted either. Each fitting pair is put on the default row for a granted skill, or beside the gem.
 */
double BestSupportPair(Player &player, SkillSlot slot, DamageType type, bool granted, std::string *bestPair = nullptr)
{
	Monster &dummy = Monsters[0];
	dummy = {};
	dummy.position.tile = player.position.tile + Displacement { 0, 3 };
	SupportSockets &sockets = granted ? player.poe2.defaultSupports : player.poe2.slotSupports[0];
	sockets.fill(SupportGem::None);
	const double bare = CastTime(player, slot);
	double best = 1.0;
	for (int a = 1; a < static_cast<int>(SupportGem::Count); a++) {
		const auto first = static_cast<SupportGem>(a);
		if (!SupportGemCanSupport(first, slot))
			continue;
		for (int b = a + 1; b < static_cast<int>(SupportGem::Count); b++) {
			const auto second = static_cast<SupportGem>(b);
			if (!SupportGemCanSupport(second, slot) || SupportsConflict(first, second))
				continue;
			sockets[0] = first;
			sockets[1] = second;
			int hit;
			{
				const SupportHitScope scope(player, slot);
				hit = SupportHitDamagePercent(dummy, type);
			}
			const double factor = hit / 100.0 * bare / CastTime(player, slot);
			if (factor > best && bestPair != nullptr)
				*bestPair = std::string(SupportGemName(first)) + " + " + std::string(SupportGemName(second));
			best = std::max(best, factor);
			if (granted && slot.kind == SkillKind::Attack) {
				// Supported, a granted skill costs mana (the skill-balance review's change 2), some of it paid in life with Blood Tithe.
				EXPECT_GT(WeaponSkillManaCost(player, static_cast<WeaponSkill>(slot.id), /*lifeShare=*/false), 0) << SupportGemName(first) << " + " << SupportGemName(second);
			}
		}
	}
	sockets.fill(SupportGem::None);
	return best;
}

TEST_F(Poe2GrantedSkillsTest, FreeSkillsDontBeatGemSpells)
{
	// Plan 7.3: on one target, at equal level, every spell gem deals at least what every free skill does, bare and
	// with the best pair of supports on each (the skill-balance review). The gems on the allow-list lose to some free
	// skills on one target; their numbers are for Curtis (the gems keep their numbers for now). Condemn and Corpse
	// Explosion lose bare only to Staff Bolt, the free bolt every staff has cast since before the rework. Glacial Spike
	// (400%) came off the list with round 3 (Curtis 2026-10-28); Haunt (350%) now beats every free skill bare and loses
	// only to a supported Staff Bolt, and so does Locust Swarm since Q28 (130% over 2 s instead of 120% over 5 s).
	enum class Excused : uint8_t {
		/** Against every free skill. */
		Everything,
		/** Against Staff Bolt. */
		StaffBolt,
		/** Against Staff Bolt, and with supports against any: the gem can't take the strongest spell support. */
		StaffBoltAndSupported,
		/** Against Staff Bolt with supports only: the gem can't take Prismatic Focus, which Staff Bolt can. */
		SupportedStaffBolt,
	};
	const struct {
		WeaponSkill skill;
		Excused excused;
		std::string_view reason;
	} AllowList[] = {
		{ WeaponSkill::Haunt, Excused::SupportedStaffBolt, "a physical damage over time (70% a second on one), which can't take Prismatic Focus as Staff Bolt can" },
		{ WeaponSkill::LocustSwarm, Excused::SupportedStaffBolt, "like Haunt, a physical curse's damage over time (about 73% a second on one), which can't take Prismatic Focus" },
		{ WeaponSkill::Condemn, Excused::StaffBoltAndSupported, "a delayed burst all around the hero (about 56% a second on one); a shield spell, it can't take Measured Ritual" },
		{ WeaponSkill::CorpseExplosion, Excused::StaffBolt, "an area burst for each corpse, which it needs (about 62% a second on one)" },
	};
	Player &player = *MyPlayer;
	player.poe2.clear();

	struct Rate {
		std::string_view name;
		SkillSlot slot;
		double perSecond;
		double supported;
		std::string pair;
	};
	std::vector<Rate> freeSkills;
	std::vector<Rate> gems;
	for (const TableSkillData &row : TableSkills()) {
		if ((row.flags & TF_Spell) == 0 || row.curve != TableCurve::Fireball)
			continue;
		const SkillSlot slot = SkillSlot::ForAttack(row.skill);
		if (IsGrantedTableSkill(row)) {
			player.poe2.granted[0] = { slot, 1, 0 };
			Rate rate { row.name, slot, OneTargetPerSecond(row), 1.0, {} };
			rate.supported = BestSupportPair(player, slot, row.type, true, &rate.pair);
			freeSkills.push_back(rate);
			player.poe2.granted = {};
		} else {
			player.poe2.skillSlots[0] = slot;
			Rate rate { row.name, slot, OneTargetPerSecond(row), 1.0, {} };
			rate.supported = BestSupportPair(player, slot, row.type, false, &rate.pair);
			gems.push_back(rate);
			player.poe2.skillSlots[0] = {};
		}
	}
	// Staff Bolt, the default of the staves and wands whose skill comes later.
	const SkillSlot staffBolt = SkillSlot::ForDefault(static_cast<uint8_t>(DefaultAttack::StaffBolt));
	Rate bolt { "Staff Bolt", staffBolt, GrantedStaffBoltPercent / CastSeconds(staffBolt), 1.0, {} };
	bolt.supported = BestSupportPair(player, staffBolt, DamageType::Fire, true, &bolt.pair);
	freeSkills.push_back(bolt);
	// 12, then Hex Rupture, Bone Prison and Bone Spikes.
	EXPECT_EQ(gems.size(), 15U);
	EXPECT_GE(freeSkills.size(), 14U);

	for (const int level : { 1, 20 }) {
		const auto at = [level](const Rate &rate, bool supported) {
			// Staff Bolt and the granted rows share the gems' Fireball curve, so the curve keeps the ratio at every level.
			const int curve = rate.slot.kind == SkillKind::Default ? FireballCurvePercent(GrantedCurveEndAt20, level) : GemDamagePercent(rate.slot, level);
			return rate.perSecond * curve / 100 * (supported ? rate.supported : 1.0);
		};
		for (const Rate &gem : gems) {
			const auto entry = std::find_if(std::begin(AllowList), std::end(AllowList), [&](const auto &allowed) { return WeaponSkillName(allowed.skill) == gem.name; });
			const bool listed = entry != std::end(AllowList);
			std::printf("level %2d  %-18s %6.1f%% a second (%6.1f%% with %s)%s\n", level, std::string(gem.name).c_str(), at(gem, false), at(gem, true), gem.pair.c_str(), listed ? "  [allow-list]" : "");
			for (const Rate &skill : freeSkills) {
				const bool supportedOnly = listed && entry->excused == Excused::SupportedStaffBolt;
				if (listed && (entry->excused == Excused::Everything || (skill.slot == staffBolt && !supportedOnly)))
					continue;
				EXPECT_GE(at(gem, false), at(skill, false)) << gem.name << " against " << skill.name << " at level " << level;
				if (supportedOnly && skill.slot == staffBolt)
					continue;
				if (!listed || entry->excused != Excused::StaffBoltAndSupported)
					EXPECT_GE(at(gem, true), at(skill, true)) << gem.name << " against " << skill.name << " with supports at level " << level;
			}
		}
		for (const Rate &skill : freeSkills)
			std::printf("level %2d  %-18s %6.1f%% a second (%6.1f%% with %s)  [free]\n", level, std::string(skill.name).c_str(), at(skill, false), at(skill, true), skill.pair.c_str());
	}
	// Each allow-listed gem does lose to some free skill, or it comes off the list.
	for (const auto &entry : AllowList) {
		const auto gem = std::find_if(gems.begin(), gems.end(), [&](const Rate &rate) { return rate.name == WeaponSkillName(entry.skill); });
		ASSERT_NE(gem, gems.end()) << entry.reason;
		// One excused only with supports loses with supports.
		const bool supported = entry.excused == Excused::SupportedStaffBolt;
		EXPECT_TRUE(std::any_of(freeSkills.begin(), freeSkills.end(), [&](const Rate &skill) {
			return skill.perSecond * (supported ? skill.supported : 1.0) > gem->perSecond * GemDamagePercent(gem->slot, 1) / 100 * (supported ? gem->supported : 1.0);
		})) << gem->name << ": " << entry.reason;
	}
}

TEST_F(Poe2GrantedSkillsTest, TheGelidStaffGrantsFreezingShards)
{
	Player &player = *MyPlayer;
	Hold(player, "Gelid Staff", 33);
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::Granted);
	EXPECT_EQ(GrantedAttackSkill(player), WeaponSkill::FreezingShards);
	EXPECT_TRUE(UsesGrantedSkill(player));
	EXPECT_EQ(DefaultSkillSlot(player), Shards);
	EXPECT_EQ(DefaultSkillSlotForSet(player, player.poe2.weaponSet), Shards);
	EXPECT_EQ(DefaultAttackNameForSet(player, player.poe2.weaponSet), "Freezing Shards");
	EXPECT_TRUE(AttacksAtRange(player));
	EXPECT_TRUE(IsRangedDefaultAttack(player));
	EXPECT_FALSE(FiresStaffBolt(player));
	// At the staff's grant level, with no gem; free while no support sits on the default row.
	EXPECT_EQ(player.poe2.skillLevel(Shards), 0);
	EXPECT_EQ(player.poe2.effectiveLevel(Shards), 9);
	EXPECT_EQ(WeaponSkillManaCost(player, WeaponSkill::FreezingShards), 0);
	EXPECT_EQ(WeaponSkillManaCost(WeaponSkill::FreezingShards), 0);
	// A cast takes its 1.2 seconds against Staff Bolt's 0.9.
	const double ratio = static_cast<double>(CastTime(player, Shards)) / CastTime(player, SkillSlot::ForDefault(static_cast<uint8_t>(DefaultAttack::StaffBolt)));
	EXPECT_NEAR(ratio, 1.2 / 0.9, 0.02);
	EXPECT_NE(SkillTags(Shards) & ST_Spell, 0U);
	EXPECT_NE(SkillTags(Shards) & ST_Projectile, 0U);

	// In multiplayer it casts Staff Bolt, which the network knows.
	gbIsMultiplayer = true;
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::StaffBolt);
	EXPECT_TRUE(FiresGrantedBolt(player));
	EXPECT_FALSE(TryUseGrantedSkill(player));
	gbIsMultiplayer = false;

	// The Ashen Staff's Staff Bolt and a staff the hero can't use cast Staff Bolt.
	Hold(player, "Ashen Staff");
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::StaffBolt);
	EXPECT_EQ(GrantedAttackSkill(player), WeaponSkill::None);
	Item &gelid = Hold(player, "Gelid Staff");
	gelid._iStatFlag = false;
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::StaffBolt);
}

TEST_F(Poe2GrantedSkillsTest, ACastFiresFiveChillingShards)
{
	Player &player = *MyPlayer;
	Hold(player, "Gelid Staff", 33);
	Target();
	const TableSkillData &row = *FindTableSkill(WeaponSkill::FreezingShards);
	// Curtis 2026-10-29: three shards a cast, not five.
	EXPECT_EQ(row.count, 3);
	EXPECT_EQ(row.type, DamageType::Cold);
	ASSERT_TRUE(TryUseGrantedSkill(player));
	EXPECT_EQ(player._pmode, PM_SPELL);
	EXPECT_EQ(player.poe2.castSkill, WeaponSkill::FreezingShards);
	EXPECT_EQ(LastPlayerAction, PlayerActionType::AttackMonsterTarget);
	EXPECT_EQ(player._pMana, Fixed26_6::fromInt(0)) << "free with no mana at all";

	// Each shard hits for 64% of the spell base along the Fireball curve at the grant level (9); a monster takes one a cast.
	int shards = 0;
	int damagePercent = 0;
	for (int tick = 0; tick < 60 && player._pmode == PM_SPELL; tick++) {
		TestPlayerProcessAction(player);
		for (const Missile &missile : Missiles) {
			if (missile._mitype != MissileID::BlueFlare)
				continue;
			shards++;
			damagePercent = missile.poe2.damagePercent;
		}
		Missiles.clear();
	}
	EXPECT_EQ(shards, 3);
	EXPECT_EQ(damagePercent, GemDamagePercent(Shards, 9) * 64 / 100);
	EXPECT_EQ(GemDamagePercent(Shards, 9), FireballCurvePercent(GrantedCurveEndAt20, 9));
}

TEST_F(Poe2GrantedSkillsTest, ACastIsLoggedWithTheGrantLevel)
{
	// Plan S24: the balance pass reads which granted skills are cast, and at what level, from the play log.
	paths::SetPrefPath(::testing::TempDir() + "poe2_granted_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + DIRECTORY_SEPARATOR_STR);
	TelemetryInit();
	ASSERT_TRUE(TelemetryEnabled());
	Player &player = *MyPlayer;
	Hold(player, "Gelid Staff", 33);
	Target();
	ASSERT_TRUE(TryUseGrantedSkill(player));
	for (int tick = 0; tick < 60 && player._pmode == PM_SPELL; tick++)
		TestPlayerProcessAction(player);
	TelemetryFlush();
	const std::string folder = paths::PrefPath() + "poe2_logs" + DIRECTORY_SEPARATOR_STR;
	std::string log;
	for (const std::string &file : ListFiles(folder.c_str())) {
		if (!file.ends_with(".log") || file.rfind("session_", 0) != 0)
			continue;
		std::ifstream stream(folder + file);
		std::stringstream contents;
		contents << stream.rdbuf();
		log += contents.str();
	}
	EXPECT_NE(log.find("[granted skill used] Freezing Shards at level 9, 0 mana"), std::string::npos) << log;
	// The skill line carries the level too, so each grant level's damage is kept apart.
	EXPECT_NE(log.find("[skill] Freezing Shards"), std::string::npos) << log;
}

TEST_F(Poe2GrantedSkillsTest, HoldControllerAndSkillBarCastIt)
{
	Player &player = *MyPlayer;
	Hold(player, "Gelid Staff");
	// Held: the left button repeats it.
	pcurs = CURSOR_HAND;
	sgbMouseDown = CLICK_LEFT;
	LastPlayerAction = PlayerActionType::Attack;
	RepeatPlayerAction();
	EXPECT_EQ(player._pmode, PM_SPELL);
	EXPECT_EQ(player.poe2.castSkill, WeaponSkill::FreezingShards);
	EXPECT_EQ(FinishCast(player, MissileID::BlueFlare), 3);
	sgbMouseDown = CLICK_NONE;

	// The controller's attack button at a monster.
	Stand(player);
	Target();
	PerformPrimaryAction();
	EXPECT_EQ(player._pmode, PM_SPELL);
	EXPECT_EQ(player.poe2.castSkill, WeaponSkill::FreezingShards);
	EXPECT_EQ(FinishCast(player, MissileID::BlueFlare), 3);

	// The skill bar's weapon attack (the left mouse button's tray binding).
	Stand(player);
	ASSERT_TRUE(UseDefaultAttack(player, player.poe2.weaponSet));
	EXPECT_EQ(player.poe2.castSkill, WeaponSkill::FreezingShards);
	EXPECT_EQ(FinishCast(player, MissileID::BlueFlare), 3);

	// Never in town.
	Stand(player);
	leveltype = DTYPE_TOWN;
	EXPECT_FALSE(TryUseGrantedSkill(player));
	EXPECT_EQ(player._pmode, PM_STAND);
}

TEST_F(Poe2GrantedSkillsTest, TheDefaultRowsSupportsApply)
{
	Player &player = *MyPlayer;
	Hold(player, "Gelid Staff");
	// A spell projectile takes Volley (Multiple Projectiles is for Diablo's spells only); Brutality is melee only and doesn't fit.
	ASSERT_TRUE(SupportGemCanSupport(SupportGem::Volley, Shards));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::Brutality, Shards));
	player.poe2.defaultSupports[0] = SupportGem::Volley;
	player.poe2.defaultSupports[1] = SupportGem::Brutality;
	EXPECT_TRUE(player.poe2.takesDefaultSupports(Shards));
	EXPECT_TRUE(player.poe2.hasSupport(Shards, SupportGem::Volley));
	EXPECT_FALSE(player.poe2.hasSupport(Shards, SupportGem::Brutality));
	EXPECT_EQ(player.poe2.supportTier(Shards, SupportGem::Volley), player.poe2.socketTier(DefaultAttackRow, 0));

	// Supported, it costs its base of 2, a third dearer for a spell (3, Curtis 2026-10-29), times the supports'
	// multiplier, and can't be cast without the mana.
	EXPECT_EQ(ApplySkillKindManaPercent(Shards, GrantedSupportedManaCost), 3);
	const int cost = WeaponSkillManaCost(player, WeaponSkill::FreezingShards);
	EXPECT_EQ(cost, 3 * SupportManaCostPercent(player, Shards, true) / 100);
	EXPECT_GT(cost, 0);
	EXPECT_FALSE(TryUseGrantedSkill(player));
	EXPECT_EQ(player._pmode, PM_STAND);

	player._pMana = player._pManaBase = Fixed26_6::fromInt(100);
	ASSERT_TRUE(TryUseGrantedSkill(player));
	EXPECT_EQ(player._pMana, Fixed26_6::fromInt(100 - cost));
	EXPECT_EQ(FinishCast(player, MissileID::BlueFlare), 3 + SupportExtraProjectileCount(player, Shards));
	EXPECT_GT(SupportExtraProjectileCount(player, Shards), 0);

	// Bare again, it's free.
	player.poe2.defaultSupports.fill(SupportGem::None);
	EXPECT_EQ(WeaponSkillManaCost(player, WeaponSkill::FreezingShards), 0);
	// Not held, the skill takes no supports from the default row.
	player.poe2.defaultSupports[0] = SupportGem::Volley;
	Hold(player, "Ashen Staff");
	EXPECT_FALSE(player.poe2.takesDefaultSupports(Shards));
	EXPECT_FALSE(player.poe2.hasSupport(Shards, SupportGem::Volley));
}

TEST_F(Poe2GrantedSkillsTest, ASceptresSpiritSkillReservesAndSwitchesOnItsOwnRow)
{
	Player &player = *MyPlayer;
	const PersistentSkill ward = PersistentSkill::ManaShield;
	Hold(player, "Fine Stoic Sceptre", 33);
	ASSERT_EQ(GrantedSpiritSkill(player, 0), ward);
	EXPECT_EQ(GrantedSpiritSkill(player, 1), PersistentSkill::None);
	EXPECT_EQ(player.poe2.skillLevel(ward), 0) << "No gem of it";
	EXPECT_EQ(player.poe2.effectiveLevel(ward), GrantedGemLevel(33));
	// On from the start, as in every hero saved before it: it reserves its Spirit, after the gems.
	EXPECT_EQ(player.poe2.spiritGemsOn, 0);
	EXPECT_FALSE(player.poe2.grantedOff(0));
	EXPECT_TRUE(IsPersistentSkillActive(player, ward));
	const int cost = GemSpiritCost(player, ward);
	EXPECT_GT(cost, 0);
	EXPECT_EQ(ReservedSpirit(player), cost);
	EXPECT_EQ(GemSpiritReservation(player, ward), cost);
	EXPECT_EQ(MaxSpirit(player), BaseSpirit + SceptreSpirit);

	// The default attack's row does what it did: nothing.
	EXPECT_FALSE(RightClickSkillsPanelRow(player, DefaultAttackRow));
	EXPECT_EQ(player.poe2.spiritGemsOn, 0);
	// The granted row flips bit 12, and only it: no socket's gem is switched.
	EXPECT_EQ(GrantedRowMain, 13U);
	EXPECT_EQ(GrantedRowOff, 14U);
	ASSERT_TRUE(RightClickSkillsPanelRow(player, GrantedRowMain));
	EXPECT_EQ(player.poe2.spiritGemsOn, 1U << 12);
	EXPECT_TRUE(player.poe2.grantedOff(0));
	EXPECT_FALSE(player.poe2.grantedOff(1));
	for (size_t socket = 0; socket < NumSkillSlots; socket++)
		EXPECT_FALSE(player.poe2.spiritGemOn(socket)) << socket;
	EXPECT_FALSE(IsPersistentSkillActive(player, ward));
	EXPECT_EQ(ReservedSpirit(player), 0);
	// Nothing grants in the off hand: its row does nothing.
	EXPECT_FALSE(RightClickSkillsPanelRow(player, GrantedRowOff));
	EXPECT_EQ(player.poe2.spiritGemsOn, 1U << 12);
	ASSERT_TRUE(RightClickSkillsPanelRow(player, GrantedRowMain));
	EXPECT_EQ(player.poe2.spiritGemsOn, 0);
	EXPECT_TRUE(IsPersistentSkillActive(player, ward));
	// The gem's own bits never reach the granted rows.
	EXPECT_FALSE(ToggleSpiritGem(player, GrantedSpiritOffBit));
	EXPECT_FALSE(ToggleSpiritGem(player, GrantedSpiritOffBit + 1));
	EXPECT_EQ(player.poe2.spiritGemsOn, 0);

	// A gem of it in the Skills panel wins: it reserves once, and switched off, the sceptre doesn't fill in.
	SocketSkill(player, 0, SkillSlot::ForPersistent(ward));
	ASSERT_TRUE(ToggleSpiritGem(player, 0));
	EXPECT_EQ(ReservedSpirit(player), cost);
	ASSERT_TRUE(ToggleSpiritGem(player, 0));
	EXPECT_FALSE(IsPersistentSkillActive(player, ward));
	EXPECT_EQ(ReservedSpirit(player), 0);
	SocketSkill(player, 0, SkillSlot {});
	EXPECT_TRUE(IsPersistentSkillActive(player, ward));

	// Two sceptres that grant it reserve once, on while either hand's is; the off hand's row flips bit 13 only.
	Item &right = player.InvBody[INVLOC_HAND_RIGHT];
	right = player.InvBody[INVLOC_HAND_LEFT];
	CalcPlrInv(player, false);
	ASSERT_EQ(GrantedSpiritSkill(player, 1), ward);
	EXPECT_EQ(MaxSpirit(player), BaseSpirit + (2 * SceptreSpirit));
	EXPECT_EQ(ReservedSpirit(player), cost);
	ASSERT_TRUE(RightClickSkillsPanelRow(player, GrantedRowOff));
	EXPECT_EQ(player.poe2.spiritGemsOn, 1U << 13);
	EXPECT_TRUE(IsPersistentSkillActive(player, ward));
	EXPECT_EQ(ReservedSpirit(player), cost);
	ASSERT_TRUE(RightClickSkillsPanelRow(player, GrantedRowMain));
	EXPECT_FALSE(IsPersistentSkillActive(player, ward));
	EXPECT_EQ(ReservedSpirit(player), 0);
	player.poe2.spiritGemsOn = 0;
	right = {};

	// Skeleton Warriors from a sceptre: minions as for a gem, at the level it grants.
	Hold(player, "Fine Rattling Sceptre", 65);
	const PersistentSkill warriors = PersistentSkill::SkeletalWarriors;
	ASSERT_EQ(GrantedSpiritSkill(player, 0), warriors);
	EXPECT_EQ(player.poe2.effectiveLevel(warriors), GrantedGemLevel(65));
	EXPECT_EQ(ActiveMinionCount(player, warriors), StartingMinionCount(warriors));
	EXPECT_EQ(SpiritPerMinion(player, warriors), BaseSpiritPerMinion(warriors, GrantedGemLevel(65)));
	ASSERT_TRUE(RightClickSkillsPanelRow(player, GrantedRowMain));
	EXPECT_EQ(ActiveMinionCount(player, warriors), 0);
	player.poe2.spiritGemsOn = 0;

	// A staff's or wand's grant is an attack, with no Spirit row; outside POE2 nothing is granted.
	Hold(player, "Gelid Staff");
	EXPECT_EQ(GrantedSpiritSkill(player, 0), PersistentSkill::None);
	EXPECT_FALSE(RightClickSkillsPanelRow(player, GrantedRowMain));
	Hold(player, "Fine Stoic Sceptre", 33);
	gbIsPoe2 = false;
	EXPECT_EQ(GrantedSpiritSkill(player, 0), PersistentSkill::None);
	EXPECT_FALSE(IsPersistentSkillActive(player, ward));
	EXPECT_EQ(player.poe2.spiritGemsOn, 0);
}

TEST_F(Poe2GrantedSkillsTest, OutsidePoe2NothingIsGranted)
{
	Player &player = *MyPlayer;
	Hold(player, "Gelid Staff");
	gbIsPoe2 = false;
	EXPECT_EQ(GrantedAttackSkill(player), WeaponSkill::None);
	EXPECT_FALSE(UsesGrantedSkill(player));
	EXPECT_FALSE(TryUseGrantedSkill(player));
}

} // namespace
} // namespace devilution
