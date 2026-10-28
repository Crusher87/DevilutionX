/**
 * @file poe2_granted_skills_test.cpp
 *
 * The caster weapons rework's granted skills (caster-weapons-plan-v2.md 6.6, 6.7 and 6.12, step S14): a staff or wand
 * that grants a table skill casts it with a left click, its hold-to-repeat, the controller's attack and the skill bar,
 * at the weapon's grant level, taking the default attack row's supports. The first one is the Gelid Staff's Freezing
 * Shards (S14); the plain rows follow (S15), with the numbers of plan 7.2 and the gem comparison of 7.3.
 */
#include <algorithm>
#include <cstdio>
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
#include "player.h"
#include "poe2/attack_speed.hpp"
#include "poe2/default_attack.hpp"
#include "poe2/gem_levels.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/support_table.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"
#include "track.h"
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
	static Monster &PlaceMonster(size_t slot, Point tile)
	{
		Monster &monster = Monsters[slot];
		monster = {};
		monster.levelType = 0;
		monster.uniqueType = UniqueMonsterType::Garbud;
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

/** @brief The staves and wands whose skills S15 builds, with Freezing Shards (S14), and whether each is a wand. */
struct GrantRow {
	std::string_view base;
	WeaponSkill skill;
	bool wand;
};
constexpr GrantRow PlainGrants[] = {
	{ "Gelid Staff", WeaponSkill::FreezingShards, false },
	{ "Voltaic Staff", WeaponSkill::LightningBolt, false },
	{ "Chiming Staff", WeaponSkill::ChimingBolt, false },
	{ "Rending Staff", WeaponSkill::Soulrend, false },
	{ "Reaping Staff", WeaponSkill::Reap, false },
	{ "Icicle Staff", WeaponSkill::FrostLance, false },
	{ "Paralysing Staff", WeaponSkill::EnervatingNova, false },
	{ "Sanctified Staff", WeaponSkill::ConsecratedGround, false },
	{ "Dark Staff", WeaponSkill::DarkPact, false },
	{ "Ravenous Staff", WeaponSkill::FeastOfFlesh, false },
	{ "Frigid Wand", WeaponSkill::FrostBolt, true },
	{ "Primordial Wand", WeaponSkill::WitheringRay, true },
	{ "Dueling Wand", WeaponSkill::TwinBolts, true },
};

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
 * target; a curse's damage over time, which doesn't stack on one target, and a storm that follows the hero count by
 * their own time.
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
	if ((row.flags & TF_Bleed) != 0)
		perUse = perUse * (100 + GrantedBleedPercent) / 100;
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
		EXPECT_EQ(row->rider, GrantedRider::None) << row->name;
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
		{ "Rending Staff", MissileID::RedFlare, DamageType::Physical, 1, true },
		{ "Icicle Staff", MissileID::BlueFlare, DamageType::Magic, 1, true },
		{ "Frigid Wand", MissileID::BlueFlare, DamageType::Magic, 1, false },
		{ "Primordial Wand", MissileID::RedFlare, DamageType::Physical, 4, false },
		{ "Dueling Wand", MissileID::OrangeFlare, DamageType::Physical, 2, false },
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
	EXPECT_EQ(GetMissileData(MissileID::RedFlare).damageType(), DamageType::Magic);
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

TEST_F(Poe2GrantedSkillsTest, TheGrantedSkillsHitAsPlanned)
{
	// Plan 7.2: the hit (% of the spell base), the cast time, and the damage a second on one target, within 5%.
	const struct {
		WeaponSkill skill;
		int hit;
		int castMs;
		int oneTarget;
	} Planned[] = {
		{ WeaponSkill::FreezingShards, 18, 1200, 15 }, // one shard of the fan
		{ WeaponSkill::LightningBolt, 22, 900, 24 },
		{ WeaponSkill::ChimingBolt, 28, 900, 31 },
		{ WeaponSkill::Soulrend, 17, 900, 33 }, // with the bleed
		{ WeaponSkill::Reap, 30, 1200, 25 },
		{ WeaponSkill::FrostLance, 30, 900, 33 },
		{ WeaponSkill::EnervatingNova, 25, 1000, 25 },
		{ WeaponSkill::ConsecratedGround, 26, 1250, 21 },
		// The skill-balance review raised Dark Pact's hit from 23 to 30, keeping its 5% life cost.
		{ WeaponSkill::DarkPact, 30, 900, 33 },
		{ WeaponSkill::FeastOfFlesh, 20, 900, 22 },
		{ WeaponSkill::FrostBolt, 40, 750, 53 },
		{ WeaponSkill::WitheringRay, 9, 750, 48 }, // before Wither (S16)
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
		const double hit = row.damagePercent * ((row.flags & TF_Bleed) != 0 ? (100.0 + GrantedBleedPercent) / 100 : 1.0);
		const bool wand = std::find_if(std::begin(PlainGrants), std::end(PlainGrants), [&](const GrantRow &g) { return g.skill == planned.skill; })->wand;
		// The budget of plan 7.1: area hits at most 25% a second of cast time (Dark Pact pays life for its 30), piercing
		// bolts 34%, a whole fan 75% a second, wand bolts 55% a second on one target.
		if (IsAnyOf(row.shape, TableShape::Burst, TableShape::Around) && planned.skill != WeaponSkill::DarkPact)
			EXPECT_LE(hit, 25 * cast * 1.01) << row.name;
		if ((row.flags & TF_Pierce) != 0)
			EXPECT_LE(hit, 34 * cast * 1.01) << row.name;
		if (row.shape == TableShape::Projectiles && row.count > 2)
			EXPECT_LE(hit * row.count / cast, 75 * 1.01) << row.name;
		if (wand)
			EXPECT_LE(OneTargetPerSecond(row), 55) << row.name;
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
	// Explosion lose bare only to Staff Bolt, the free bolt every staff has cast since before the rework.
	enum class Excused : uint8_t {
		/** Against every free skill. */
		Everything,
		/** Against Staff Bolt. */
		StaffBolt,
		/** Against Staff Bolt, and with supports against any: the gem can't take the strongest spell support. */
		StaffBoltAndSupported,
	};
	const struct {
		WeaponSkill skill;
		Excused excused;
		std::string_view reason;
	} AllowList[] = {
		{ WeaponSkill::Haunt, Excused::Everything, "a damage over time that doesn't stack on one target (about 30% a second); audit-casters-v3's buff" },
		{ WeaponSkill::GlacialSpike, Excused::Everything, "an area spike that wins on packs (about 44% a second on one); audit-casters-v3's buff" },
		{ WeaponSkill::LocustSwarm, Excused::Everything, "like Haunt, a curse's damage over time, which spreads on death (about 27% a second)" },
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
	EXPECT_EQ(gems.size(), 12U);
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
				if (listed && (entry->excused == Excused::Everything || skill.slot == staffBolt))
					continue;
				EXPECT_GE(at(gem, false), at(skill, false)) << gem.name << " against " << skill.name << " at level " << level;
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
		EXPECT_TRUE(std::any_of(freeSkills.begin(), freeSkills.end(), [&](const Rate &skill) { return skill.perSecond > gem->perSecond * GemDamagePercent(gem->slot, 1) / 100; })) << gem->name << ": " << entry.reason;
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

	// Staves whose skill isn't in yet, the Ashen Staff's Staff Bolt and a staff the hero can't use cast Staff Bolt.
	Hold(player, "Pyrophyte Staff");
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::StaffBolt);
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
	EXPECT_EQ(row.count, 5);
	EXPECT_EQ(row.type, DamageType::Magic);
	ASSERT_TRUE(TryUseGrantedSkill(player));
	EXPECT_EQ(player._pmode, PM_SPELL);
	EXPECT_EQ(player.poe2.castSkill, WeaponSkill::FreezingShards);
	EXPECT_EQ(LastPlayerAction, PlayerActionType::AttackMonsterTarget);
	EXPECT_EQ(player._pMana, Fixed26_6::fromInt(0)) << "free with no mana at all";

	// Each shard hits for 18% of the spell base along the Fireball curve at the grant level (9).
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
	EXPECT_EQ(shards, 5);
	EXPECT_EQ(damagePercent, GemDamagePercent(Shards, 9) * 18 / 100);
	EXPECT_EQ(GemDamagePercent(Shards, 9), FireballCurvePercent(GrantedCurveEndAt20, 9));
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
	EXPECT_EQ(FinishCast(player, MissileID::BlueFlare), 5);
	sgbMouseDown = CLICK_NONE;

	// The controller's attack button at a monster.
	Stand(player);
	Target();
	PerformPrimaryAction();
	EXPECT_EQ(player._pmode, PM_SPELL);
	EXPECT_EQ(player.poe2.castSkill, WeaponSkill::FreezingShards);
	EXPECT_EQ(FinishCast(player, MissileID::BlueFlare), 5);

	// The skill bar's weapon attack (the left mouse button's tray binding).
	Stand(player);
	ASSERT_TRUE(UseDefaultAttack(player, player.poe2.weaponSet));
	EXPECT_EQ(player.poe2.castSkill, WeaponSkill::FreezingShards);
	EXPECT_EQ(FinishCast(player, MissileID::BlueFlare), 5);

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

	// Supported, it costs its base of 2 times the supports' multiplier, and can't be cast without the mana.
	const int cost = WeaponSkillManaCost(player, WeaponSkill::FreezingShards);
	EXPECT_EQ(cost, GrantedSupportedManaCost * SupportManaCostPercent(player, Shards, true) / 100);
	EXPECT_GT(cost, 0);
	EXPECT_FALSE(TryUseGrantedSkill(player));
	EXPECT_EQ(player._pmode, PM_STAND);

	player._pMana = player._pManaBase = Fixed26_6::fromInt(100);
	ASSERT_TRUE(TryUseGrantedSkill(player));
	EXPECT_EQ(player._pMana, Fixed26_6::fromInt(100 - cost));
	EXPECT_EQ(FinishCast(player, MissileID::BlueFlare), 5 + SupportExtraProjectileCount(player, Shards));
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
