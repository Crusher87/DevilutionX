#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "lighting.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/attack_speed.hpp"
#include "poe2/gem_levels.hpp"
#include "poe2/gemcutting.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/player_data.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/support_table.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/vfx.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

/** @brief The draft gems' Bow batch (Curtis 2026-11-07, Q48, build order 3), each built as its row in drafts-61-priced.json. */
class Poe2BowDraftsTest : public ::testing::Test {
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
		LoadMissileData();
		LoadMonsterData();
		LevelMonsterTypes[0].type = MT_NZOMBIE;
		Missiles.clear();
		ClearMonsters();
	}

	void TearDown() override
	{
		Missiles.clear();
		ClearSkillZones();
		ClearSupportEffects();
		ClearMonsters();
		gbIsPoe2 = false;
		leveltype = DTYPE_TOWN;
		UnloadModArchives();
	}

	/** @brief A hero who hits often, in a dungeon, with the network up for the hits. */
	static Player &SetUpFight()
	{
		leveltype = DTYPE_CATHEDRAL;
		SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
		Player &player = *MyPlayer;
		player.setCharacterLevel(30);
		player._pBaseDex = player._pDexterity = 200;
		player._pBaseStr = player._pStrength = 100;
		player._pIMinDam = 20;
		player._pIMaxDam = 30;
		player._pMaxHP = player._pHitPoints = Fixed26_6::fromInt(200);
		player.position.tile = player.position.future = player.position.old = { 40, 40 };
		player._pdir = Direction::SouthEast;
		return player;
	}

	/** @brief A normal monster standing on the tile, with plenty of life. */
	static Monster &PlaceMonster(size_t slot, Point tile)
	{
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
				dPlayer[x][y] = 0;
			}
		}
		for (size_t i = 0; i < ActiveMonsterCount; i++)
			Monsters[ActiveMonsters[i]] = {};
		ActiveMonsterCount = 0;
	}

	static const TableSkillData &Row(WeaponSkill skill)
	{
		const TableSkillData *data = FindTableSkill(skill);
		EXPECT_NE(data, nullptr);
		return *data;
	}

	static bool Hurt(const Monster &monster)
	{
		return monster.hitPoints < monster.maxHitPoints;
	}

	static int32_t Lost(const Monster &monster)
	{
		return (monster.maxHitPoints - monster.hitPoints).raw();
	}

	static void Heal(Monster &monster)
	{
		monster.hitPoints = monster.maxHitPoints;
	}

	static std::vector<const Missile *> MissilesOf(MissileID type)
	{
		std::vector<const Missile *> found;
		for (const Missile &missile : Missiles) {
			if (missile._mitype == type)
				found.push_back(&missile);
		}
		return found;
	}

	/** @brief The skill in the first Skills panel socket, with the support gem. */
	static void Supported(WeaponSkill skill, SupportGem gem)
	{
		Player &player = *MyPlayer;
		player.poe2.skillSlots[0] = SkillSlot::ForAttack(skill);
		player.poe2.slotLevels[0] = 1;
		player.poe2.slotSupports[0] = {};
		player.poe2.slotSupportTiers[0] = {};
		player.poe2.slotSupports[0][0] = gem;
		player.poe2.slotSupportTiers[0][0] = 1;
	}

	/** @brief Uses the skill on the target as its animation's action frame would. */
	static void Use(Player &player, WeaponSkill skill, Point target)
	{
		player.poe2.castSkill = skill;
		ExecuteSkillCast(player, skill, target);
		player.poe2.castSkill = WeaponSkill::None;
	}

	static void Zones(Player &player, int ticks)
	{
		for (int tick = 0; tick < ticks; tick++)
			ProcessSkillZones(player);
	}

	static int Curve(const Player &player, WeaponSkill skill)
	{
		return GemDamagePercent(SkillSlot::ForAttack(skill), player.poe2.effectiveLevel(skill));
	}

	/** @brief One arrow of the skill landing on the monster for this much, as the arrow's hit does (MonsterMHit). */
	static void ArrowHit(Player &player, Monster &monster, WeaponSkill skill, int damage)
	{
		const SupportHitScope scope(player, SkillSlot::ForAttack(skill));
		ApplyPlayerAttackDamage(player, monster, Fixed26_6::fromInt(damage), false, true);
	}

	/** @brief The one arrow the skill shot just now, copied (OnTableMissileHit takes what the missile was). */
	static Missile OnlyArrow()
	{
		const std::vector<const Missile *> arrows = MissilesOf(MissileID::Arrow);
		EXPECT_EQ(arrows.size(), 1U);
		return arrows.empty() ? Missile {} : *arrows.front();
	}
};

struct DraftRow {
	WeaponSkill skill;
	int tier;
	int level;
	/** The listed mana, and what the gem costs at level 1 (mana_gem1): bow gems pay it in full. */
	int mana;
	int gemOneMana;
	int percent;
	/** The speed row's damage_pct and mana_pct. */
	int speedDamage;
	int speedMana;
	DamageType type;
	/** The row's convertPercent: its arrow lands as its element. */
	int convert;
};

// drafts-61-priced.json (Curtis 19:33 UTC 11-07, Q46-Q48; build order 3, the Bow batch).
constexpr std::array<DraftRow, 9> Drafts { {
	{ WeaponSkill::ColdArrow, 3, 3, 7, 7, 355, 100, 100, DamageType::Cold, 100 },
	{ WeaponSkill::DeadeyeShot, 3, 3, 7, 10, 225, 182, 141, DamageType::Physical, 0 },
	{ WeaponSkill::ShatterShot, 9, 15, 14, 15, 285, 112, 106, DamageType::Cold, 100 },
	{ WeaponSkill::SparkArrow, 9, 15, 13, 13, 145, 100, 100, DamageType::Lightning, 100 },
	{ WeaponSkill::Strafe, 11, 20, 13, 13, 195, 100, 100, DamageType::Physical, 0 },
	{ WeaponSkill::KindlingArrow, 11, 20, 16, 20, 95, 143, 122, DamageType::Fire, 100 },
	{ WeaponSkill::WhirlingVolley, 13, 25, 18, 24, 135, 162, 131, DamageType::Physical, 0 },
	{ WeaponSkill::LodestoneSalvo, 13, 25, 18, 27, 115, 200, 150, DamageType::Lightning, 0 },
	{ WeaponSkill::SwampGasArrow, 13, 25, 18, 18, 130, 100, 100, DamageType::Physical, 0 },
} };

TEST_F(Poe2BowDraftsTest, BowDraftsAreDexterityGemsAtTheirTiers)
{
	// Appended after the Sword and Shield batch, in the batch's order.
	EXPECT_EQ(static_cast<int>(WeaponSkill::ColdArrow), static_cast<int>(WeaponSkill::ZakarumsJudgement) + 1);
	EXPECT_EQ(WeaponSkill::Last, WeaponSkill::SwampGasArrow);
	for (size_t i = 0; i < Drafts.size(); i++)
		EXPECT_EQ(static_cast<int>(Drafts[i].skill), static_cast<int>(WeaponSkill::ColdArrow) + static_cast<int>(i));
	for (const DraftRow &draft : Drafts) {
		const TableSkillData &data = Row(draft.skill);
		const SkillSlot slot = SkillSlot::ForAttack(draft.skill);
		EXPECT_EQ(data.weapon, TableWeapon::Bow) << data.name;
		EXPECT_EQ(data.tabs, TT_Bow) << data.name;
		EXPECT_EQ(data.graphic, 0) << data.name;
		EXPECT_FALSE(IsGrantedTableSkill(data)) << data.name;
		EXPECT_EQ(data.flags & TF_Spell, 0U) << data.name;
		EXPECT_EQ(data.level, draft.level) << data.name;
		EXPECT_EQ(GemTier(data.level), draft.tier) << data.name;
		EXPECT_EQ(data.mana, draft.mana) << data.name;
		EXPECT_EQ(data.damagePercent, draft.percent) << data.name;
		EXPECT_EQ(data.type, draft.type) << data.name;
		EXPECT_EQ(data.convertPercent, draft.convert) << data.name;
		EXPECT_EQ(data.curve, TableCurve::LightningArrow) << data.name;
		EXPECT_EQ(data.curveEnd, 0) << data.name;
		// No channelling, and real arrows where they shoot (so the bow's arrow supports work with them).
		EXPECT_NE(data.shape, TableShape::Stream) << data.name;
		if (data.shape == TableShape::Projectiles) {
			EXPECT_EQ(data.missile, MissileID::Arrow) << data.name;
		}
		// Bow gems are no melee: they pay their listed mana in full, then their speed's share.
		EXPECT_FALSE(IsTableMeleeSkill(data)) << data.name;
		EXPECT_FALSE(HasMeleeManaCut(draft.skill)) << data.name;
		EXPECT_EQ(WeaponSkillManaCost(draft.skill), draft.mana) << data.name;
		EXPECT_EQ(GemBaseManaCost(slot, 1), draft.gemOneMana) << data.name;
		EXPECT_EQ(SkillSpeedDamagePercent(slot), draft.speedDamage) << data.name;
		EXPECT_EQ(SkillSpeedManaPercent(slot), draft.speedMana) << data.name;
		// Its item row: appended, uncut-only, at the table's level, Dexterity as the other bow gems.
		const std::string name = std::string(data.name) + " Gem";
		const auto row = std::find_if(AllItemsList.begin(), AllItemsList.end(), [&](const ItemData &item) { return item.iName == name; });
		ASSERT_NE(row, AllItemsList.end()) << name;
		EXPECT_EQ(row->iMinMLvl, data.level) << name;
		EXPECT_EQ(row->dropRate, 0) << name;
		EXPECT_EQ(row->iMinDex, 18 + 3 * draft.level) << name;
		EXPECT_EQ(row->iMinStr, 0) << name;
		EXPECT_EQ(row->iMinMag, 0) << name;
		EXPECT_EQ(row->iValue, 450 + 130 * draft.level) << name;
		EXPECT_EQ(FindTableSkillGem(*row), &data) << name;
		Item gem;
		ASSERT_TRUE(CreateSkillGemItem(gem, slot, 1)) << data.name;
		EXPECT_EQ(SkillForGem(gem), slot) << data.name;
		// It is cut on the Bow tab only.
		const std::vector<GemEntry> &catalog = GemCatalog();
		const auto entry = std::find_if(catalog.begin(), catalog.end(), [&](const GemEntry &gemEntry) { return gemEntry.skill == slot; });
		ASSERT_NE(entry, catalog.end()) << data.name;
		EXPECT_EQ(entry->level, draft.level) << data.name;
		EXPECT_TRUE(GemInCategory(*entry, GemCategory::Bow)) << data.name;
		EXPECT_FALSE(GemInCategory(*entry, GemCategory::Elemental)) << data.name;
		EXPECT_FALSE(TableSkillExtraNote(draft.skill).empty()) << data.name;
	}
	// The drafts' extras.
	EXPECT_EQ(TableAilmentPercent(Row(WeaponSkill::ColdArrow)), 200);
	EXPECT_EQ(ColdArrowChillPercent, 50);
	EXPECT_EQ(DeadeyeFrozenPercent, 160);
	EXPECT_EQ(ShatterShotShards, 6);
	EXPECT_EQ(ShatterShotShardTiles, 3);
	EXPECT_EQ(ShatterShotShardPercent, 50);
	EXPECT_EQ(SparkArrowBurstRadius, 1);
	EXPECT_EQ(SparkArrowBurstPercent, 60);
	EXPECT_EQ(StrafeSplitArrows, 6);
	EXPECT_EQ(StrafeSplitPercent, 50);
	EXPECT_EQ(StrafeSplitPierces, 1);
	EXPECT_EQ(KindlingBlastRadius, 1);
	EXPECT_EQ(KindlingBurningPercent, 160);
	EXPECT_EQ(Row(WeaponSkill::WhirlingVolley).shape, TableShape::Dash);
	EXPECT_EQ(Row(WeaponSkill::WhirlingVolley).count, 3);
	EXPECT_EQ(WhirlingVolleyArrows, 16);
	EXPECT_EQ(Row(WeaponSkill::LodestoneSalvo).count, 8);
	EXPECT_EQ(Row(WeaponSkill::LodestoneSalvo).radius, 3);
	EXPECT_EQ(LodestoneBoltsPerMonster, 2);
	EXPECT_EQ(LodestoneShockedPercent, 160);
	const TableSkillData &swamp = Row(WeaponSkill::SwampGasArrow);
	EXPECT_EQ(swamp.zone, TableZone::PoisonGround);
	EXPECT_EQ(swamp.flags, static_cast<uint32_t>(TF_LandingZone));
	EXPECT_EQ(swamp.radius, 1);
	EXPECT_EQ(swamp.duration, 80);
	EXPECT_EQ(TablePoisonPercentPerSecond(swamp), 25);
	EXPECT_EQ(SwampGasArrowPercent, 30);
	EXPECT_EQ(SwampGasClouds, 3);
	EXPECT_EQ(SwampGasBurstRadius, 2);
	EXPECT_EQ(SwampGasBurstPercent, 40);
}

TEST_F(Poe2BowDraftsTest, BowDraftsHaveTheirTags)
{
	const auto has = [](WeaponSkill skill, uint32_t tag) { return (SkillTags(SkillSlot::ForAttack(skill)) & tag) == tag; };
	for (const DraftRow &draft : Drafts) {
		EXPECT_TRUE(has(draft.skill, ST_Attack | ST_Bow)) << Row(draft.skill).name;
		EXPECT_FALSE(has(draft.skill, ST_Melee)) << Row(draft.skill).name;
		EXPECT_FALSE(has(draft.skill, ST_Channelling)) << Row(draft.skill).name;
		EXPECT_FALSE(has(draft.skill, ST_Spell)) << Row(draft.skill).name;
	}
	EXPECT_TRUE(has(WeaponSkill::ColdArrow, ST_Projectile | ST_Frost));
	EXPECT_FALSE(has(WeaponSkill::ColdArrow, ST_Area));
	EXPECT_TRUE(has(WeaponSkill::DeadeyeShot, ST_Projectile | ST_Physical));
	EXPECT_TRUE(has(WeaponSkill::ShatterShot, ST_Projectile | ST_Frost));
	EXPECT_TRUE(has(WeaponSkill::SparkArrow, ST_Projectile | ST_Lightning | ST_Area));
	EXPECT_TRUE(has(WeaponSkill::Strafe, ST_Projectile | ST_Physical));
	EXPECT_TRUE(has(WeaponSkill::KindlingArrow, ST_Projectile | ST_Fire | ST_Area));
	EXPECT_TRUE(has(WeaponSkill::WhirlingVolley, ST_Projectile | ST_Movement | ST_Physical));
	EXPECT_FALSE(has(WeaponSkill::WhirlingVolley, ST_Area));
	EXPECT_TRUE(has(WeaponSkill::LodestoneSalvo, ST_Lightning | ST_Area));
	EXPECT_FALSE(has(WeaponSkill::LodestoneSalvo, ST_Projectile));
	EXPECT_TRUE(has(WeaponSkill::SwampGasArrow, ST_Projectile | ST_Area | ST_Duration | ST_Poison | ST_Chaos | ST_Fire));
}

// No two fire, cold or lightning gems in one tier of the Bow tab, the older bow gems counted.
TEST_F(Poe2BowDraftsTest, NoTwoGemsOfOneElementInABowTier)
{
	std::map<std::pair<int, uint32_t>, std::vector<std::string>> byTierAndElement;
	size_t bowGems = 0;
	for (const GemEntry &gem : GemCatalog()) {
		if (gem.skill.kind != SkillKind::Attack || !GemInCategory(gem, GemCategory::Bow) || IsRetiredSkill(gem.skill))
			continue;
		bowGems++;
		const uint32_t tags = SkillTags(gem.skill);
		const uint32_t elements = tags & (ST_Fire | ST_Frost | ST_Lightning);
		// A gem of mixed elements is fine.
		if (elements == 0 || (elements & (elements - 1)) != 0)
			continue;
		byTierAndElement[{ GemTier(gem.level), elements }].push_back(GemName(gem));
	}
	EXPECT_GE(bowGems, Drafts.size());
	for (const auto &[key, names] : byTierAndElement)
		EXPECT_EQ(names.size(), 1U) << "tier " << key.first << ": " << names.front() << " and " << names.back();
}

TEST_F(Poe2BowDraftsTest, ColdArrowLandsAsColdAndChillsHard)
{
	Player &player = SetUpFight();
	Monster &monster = PlaceMonster(0, { 43, 40 });
	Use(player, WeaponSkill::ColdArrow, { 43, 40 });
	const Missile arrow = OnlyArrow();
	EXPECT_EQ(arrow.poe2.damagePercent, Curve(player, WeaponSkill::ColdArrow) * 355 / 100);
	EXPECT_EQ(arrow.poe2.vfxElement, static_cast<int8_t>(vfx::Element::Cold));
	// No burst of its own (unlike Freezing Arrow).
	EXPECT_EQ(SkillZoneCount(), 0U);
	OnTableMissileHit(arrow, player, monster, 10, false);
	EXPECT_GE(monster.poe2.chillPercent, ColdArrowChillPercent);
	EXPECT_GT(monster.poe2.chillTicks, 0);
	// The arrow's hit is cold: it builds freeze, which a physical arrow's doesn't.
	Monster &plain = PlaceMonster(1, { 44, 42 });
	ArrowHit(player, plain, WeaponSkill::DeadeyeShot, 40);
	EXPECT_TRUE(Hurt(plain));
	EXPECT_EQ(plain.poe2.freezeBuildup, 0);
	Monster &cold = PlaceMonster(2, { 45, 42 });
	ArrowHit(player, cold, WeaponSkill::ColdArrow, 40);
	EXPECT_TRUE(Hurt(cold));
	EXPECT_TRUE(cold.poe2.freezeBuildup > 0 || cold.poe2.freezeTicks > 0);
}

TEST_F(Poe2BowDraftsTest, DeadeyeShotShattersTheFrozen)
{
	Player &player = SetUpFight();
	Monster &plain = PlaceMonster(0, { 43, 40 });
	Monster &frozen = PlaceMonster(1, { 44, 42 });
	frozen.poe2.freezeTicks = 40;
	ArrowHit(player, plain, WeaponSkill::DeadeyeShot, 100);
	ArrowHit(player, frozen, WeaponSkill::DeadeyeShot, 100);
	EXPECT_EQ(frozen.poe2.freezeTicks, 0);
	EXPECT_GT(Lost(plain), 0);
	EXPECT_NEAR(static_cast<double>(Lost(frozen)) / Lost(plain), DeadeyeFrozenPercent / 100.0, 0.03);
	// Its freeze used up, the next shot hits it as the plain one.
	Heal(frozen);
	Heal(plain);
	ArrowHit(player, plain, WeaponSkill::DeadeyeShot, 100);
	ArrowHit(player, frozen, WeaponSkill::DeadeyeShot, 100);
	EXPECT_NEAR(static_cast<double>(Lost(frozen)) / Lost(plain), 1.0, 0.03);
	// Another skill's arrow takes nothing from the freeze.
	frozen.poe2.freezeTicks = 40;
	EXPECT_EQ(TableArrowHitPercent(Row(WeaponSkill::ColdArrow), frozen), 100);
	EXPECT_EQ(frozen.poe2.freezeTicks, 40);
}

TEST_F(Poe2BowDraftsTest, ShatterShotSpraysShardsBehindItsFirstHit)
{
	Player &player = SetUpFight();
	Monster &target = PlaceMonster(0, { 43, 40 });
	Use(player, WeaponSkill::ShatterShot, { 43, 40 });
	const Missile arrow = OnlyArrow();
	EXPECT_EQ(arrow.poe2.vfxElement, static_cast<int8_t>(vfx::Element::Cold));
	OnTableMissileHit(arrow, player, target, 10, false);
	const std::vector<const Missile *> shards = MissilesOf(MissileID::BlueFlare);
	ASSERT_EQ(shards.size(), static_cast<size_t>(ShatterShotShards));
	for (const Missile *shard : shards) {
		EXPECT_TRUE(shard->poe2.draftPart);
		EXPECT_EQ(shard->poe2.damageType, static_cast<int8_t>(DamageType::Cold));
		EXPECT_EQ(shard->poe2.damagePercent, Curve(player, WeaponSkill::ShatterShot) * (285 * ShatterShotShardPercent / 100) / 100);
		EXPECT_EQ(Point(shard->position.start), Point(43, 40));
		EXPECT_EQ(shard->lastCollisionTargetHash, dMonster[43][40] ^ dPlayer[43][40]);
		// Three tiles at a flare's speed.
		EXPECT_LE(shard->duration, 20);
	}
	// A shard sets off no more, nor does the arrow on a monster after its first.
	OnTableMissileHit(*shards.front(), player, target, 10, false);
	Missile pierced = arrow;
	pierced.poe2.lastHitMonster = 5;
	OnTableMissileHit(pierced, player, target, 10, false);
	EXPECT_EQ(MissilesOf(MissileID::BlueFlare).size(), static_cast<size_t>(ShatterShotShards));
}

TEST_F(Poe2BowDraftsTest, SparkArrowBurstsAndAlwaysShocks)
{
	Player &player = SetUpFight();
	Monster &target = PlaceMonster(0, { 43, 40 });
	Monster &beside = PlaceMonster(1, { 44, 40 });
	Monster &far = PlaceMonster(2, { 46, 40 });
	Use(player, WeaponSkill::SparkArrow, { 43, 40 });
	const Missile arrow = OnlyArrow();
	EXPECT_EQ(arrow.poe2.vfxElement, static_cast<int8_t>(vfx::Element::Lightning));
	OnTableMissileHit(arrow, player, target, 10, false);
	EXPECT_TRUE(Hurt(target));
	EXPECT_TRUE(Hurt(beside));
	EXPECT_FALSE(Hurt(far));
	EXPECT_EQ(target.poe2.shockTicks, ShockTicks);
	EXPECT_EQ(beside.poe2.shockTicks, ShockTicks);
	EXPECT_EQ(far.poe2.shockTicks, 0);
	// Only the first monster the arrow hits bursts.
	Heal(beside);
	Missile pierced = arrow;
	pierced.poe2.lastHitMonster = 0;
	OnTableMissileHit(pierced, player, beside, 10, false);
	EXPECT_FALSE(Hurt(beside));
}

TEST_F(Poe2BowDraftsTest, StrafeSplitsIntoARingOfPiercingArrows)
{
	Player &player = SetUpFight();
	Monster &target = PlaceMonster(0, { 43, 40 });
	Use(player, WeaponSkill::Strafe, { 43, 40 });
	const Missile arrow = OnlyArrow();
	EXPECT_EQ(arrow.poe2.damagePercent, Curve(player, WeaponSkill::Strafe) * 195 / 100);
	Missiles.clear();
	OnTableMissileHit(arrow, player, target, 10, false);
	const std::vector<const Missile *> splits = MissilesOf(MissileID::Arrow);
	ASSERT_EQ(splits.size(), static_cast<size_t>(StrafeSplitArrows));
	std::vector<std::pair<int, int>> ways;
	for (const Missile *split : splits) {
		EXPECT_TRUE(split->poe2.draftPart);
		EXPECT_GE(split->poe2.piercesLeft, StrafeSplitPierces);
		EXPECT_EQ(split->poe2.damagePercent, Curve(player, WeaponSkill::Strafe) * (195 * StrafeSplitPercent / 100) / 100);
		EXPECT_EQ(Point(split->position.start), Point(43, 40));
		EXPECT_EQ(split->lastCollisionTargetHash, dMonster[43][40] ^ dPlayer[43][40]);
		ways.emplace_back(split->position.velocity.deltaX, split->position.velocity.deltaY);
	}
	// Each its own way, all round the target.
	std::sort(ways.begin(), ways.end());
	EXPECT_EQ(std::unique(ways.begin(), ways.end()) - ways.begin(), StrafeSplitArrows);
	EXPECT_LT(ways.front().first, 0);
	EXPECT_GT(ways.back().first, 0);
	// The split arrows split no more.
	OnTableMissileHit(*splits.front(), player, target, 10, false);
	EXPECT_EQ(MissilesOf(MissileID::Arrow).size(), static_cast<size_t>(StrafeSplitArrows));
}

TEST_F(Poe2BowDraftsTest, KindlingArrowLandsAsFireAndExplodesHarderOnTheBurning)
{
	Player &player = SetUpFight();
	Monster &target = PlaceMonster(0, { 43, 40 });
	Monster &burning = PlaceMonster(1, { 44, 40 });
	Monster &cool = PlaceMonster(2, { 43, 41 });
	burning.poe2.igniteTicks = 60;
	Use(player, WeaponSkill::KindlingArrow, { 43, 40 });
	const Missile arrow = OnlyArrow();
	EXPECT_EQ(arrow.poe2.vfxElement, static_cast<int8_t>(vfx::Element::Fire));
	OnTableMissileHit(arrow, player, target, 10, false);
	// The blast hits those beside the target, not the target (its arrow did).
	EXPECT_FALSE(Hurt(target));
	EXPECT_TRUE(Hurt(burning));
	EXPECT_TRUE(Hurt(cool));
	EXPECT_NEAR(static_cast<double>(Lost(burning)) / Lost(cool), KindlingBurningPercent / 100.0, 0.05);
	// The arrow's own hit is fire, and more on a burning target.
	Monster &plain = PlaceMonster(3, { 46, 44 });
	Monster &lit = PlaceMonster(4, { 47, 44 });
	lit.poe2.igniteTicks = 60;
	EXPECT_EQ(TableArrowHitPercent(Row(WeaponSkill::KindlingArrow), lit), KindlingBurningPercent);
	EXPECT_EQ(TableArrowHitPercent(Row(WeaponSkill::KindlingArrow), plain), 100);
	ArrowHit(player, plain, WeaponSkill::KindlingArrow, 100);
	EXPECT_GT(plain.poe2.fireHitTicks, 0);
	Monster &physical = PlaceMonster(5, { 48, 44 });
	ArrowHit(player, physical, WeaponSkill::Strafe, 100);
	EXPECT_TRUE(Hurt(physical));
	EXPECT_EQ(physical.poe2.fireHitTicks, 0);
}

TEST_F(Poe2BowDraftsTest, WhirlingVolleyLoosesARingThatHitsEachMonsterOnce)
{
	Player &player = SetUpFight();
	EXPECT_TRUE(SpinsWhileStriking(WeaponSkill::WhirlingVolley));
	Monster &monster = PlaceMonster(0, { 44, 40 });
	Use(player, WeaponSkill::WhirlingVolley, { 44, 40 });
	const std::vector<const Missile *> arrows = MissilesOf(MissileID::Arrow);
	ASSERT_EQ(arrows.size(), static_cast<size_t>(WhirlingVolleyArrows));
	std::vector<std::pair<int, int>> ways;
	const uint16_t cast = arrows.front()->poe2.volleyCast;
	EXPECT_NE(cast, 0);
	for (const Missile *arrow : arrows) {
		EXPECT_EQ(arrow->poe2.volleyCast, cast);
		EXPECT_EQ(Point(arrow->position.start), player.position.tile);
		EXPECT_EQ(arrow->poe2.damagePercent, Curve(player, WeaponSkill::WhirlingVolley) * 135 / 100);
		ways.emplace_back(arrow->position.velocity.deltaX, arrow->position.velocity.deltaY);
	}
	std::sort(ways.begin(), ways.end());
	// Each its own way, all round the hero.
	EXPECT_EQ(std::unique(ways.begin(), ways.end()) - ways.begin(), WhirlingVolleyArrows);
	EXPECT_LT(ways.front().first, 0);
	EXPECT_GT(ways.back().first, 0);
	// One arrow of the ring hits the monster; the others pass it by.
	const Missile first = *arrows[0];
	const Missile second = *arrows[1];
	EXPECT_FALSE(GrantedMissileSkipsMonster(second, monster));
	OnTableMissileHit(first, player, monster, 10, false);
	EXPECT_TRUE(GrantedMissileSkipsMonster(second, monster));
	// The next use's ring hits it again.
	Missiles.clear();
	Use(player, WeaponSkill::WhirlingVolley, { 44, 40 });
	ASSERT_FALSE(MissilesOf(MissileID::Arrow).empty());
	EXPECT_FALSE(GrantedMissileSkipsMonster(*MissilesOf(MissileID::Arrow).front(), monster));
	// Volley adds arrows to the ring.
	Missiles.clear();
	Supported(WeaponSkill::WhirlingVolley, SupportGem::Volley);
	Use(player, WeaponSkill::WhirlingVolley, { 44, 40 });
	EXPECT_GT(MissilesOf(MissileID::Arrow).size(), static_cast<size_t>(WhirlingVolleyArrows));
}

TEST_F(Poe2BowDraftsTest, LodestoneSalvoDropsAtMostTwoBoltsOnEach)
{
	Player &player = SetUpFight();
	// Five within reach of the target, one beyond: eight bolts are one on each, then a second on the three nearest.
	std::vector<Monster *> near;
	const std::array<Point, 5> tiles { { { 46, 46 }, { 47, 46 }, { 46, 48 }, { 48, 48 }, { 46, 43 } } };
	for (size_t i = 0; i < tiles.size(); i++)
		near.push_back(&PlaceMonster(i, tiles[i]));
	Monster &beyond = PlaceMonster(5, { 46, 51 });
	Use(player, WeaponSkill::LodestoneSalvo, { 46, 46 });
	EXPECT_FALSE(Hurt(beyond));
	for (Monster *monster : near)
		EXPECT_TRUE(Hurt(*monster));
	// The farthest two took one bolt; the nearest took two, more than they did.
	EXPECT_GT(Lost(*near[0]), Lost(*near[3]));
	EXPECT_GT(Lost(*near[1]), Lost(*near[4]));
	// A shocked monster takes more from a bolt.
	ClearMonsters();
	Monster &shocked = PlaceMonster(0, { 46, 46 });
	shocked.poe2.shockTicks = ShockTicks;
	Use(player, WeaponSkill::LodestoneSalvo, { 46, 46 });
	const int32_t shockedLost = Lost(shocked);
	ClearMonsters();
	Monster &plain = PlaceMonster(0, { 46, 46 });
	Use(player, WeaponSkill::LodestoneSalvo, { 46, 46 });
	EXPECT_GT(shockedLost, Lost(plain));
	EXPECT_EQ(MissilesOf(MissileID::Arrow).size(), 0U);
}

TEST_F(Poe2BowDraftsTest, SwampGasArrowLeavesACloudThatFireSetsOff)
{
	Player &player = SetUpFight();
	Monster &monster = PlaceMonster(0, { 44, 40 });
	Use(player, WeaponSkill::SwampGasArrow, { 44, 40 });
	const Missile arrow = OnlyArrow();
	// The arrow is a share of the hit; the cloud takes the rest.
	EXPECT_EQ(arrow.poe2.damagePercent, Curve(player, WeaponSkill::SwampGasArrow) * (130 * SwampGasArrowPercent / 100) / 100);
	ASSERT_EQ(SkillZoneCount(), 1U);
	EXPECT_EQ(SkillZoneCenter(0), std::optional<Point>(Point(44, 40)));
	// It lands, and poisons what is in it: one stack, no hinder.
	Zones(player, 12);
	EXPECT_GT(monster.poe2.poisonTicks, 0);
	EXPECT_EQ(monster.poe2.hinderTicks, 0);
	EXPECT_EQ(SkillZoneCount(), 1U);
	// A burning monster in it sets it off: a fire burst, and the cloud is gone.
	Monster &aside = PlaceMonster(1, { 46, 40 });
	Heal(monster);
	monster.poe2.igniteTicks = 40;
	Zones(player, 1);
	EXPECT_EQ(SkillZoneCount(), 0U);
	EXPECT_TRUE(Hurt(monster));
	EXPECT_TRUE(Hurt(aside));
	EXPECT_GT(aside.poe2.fireHitTicks, 0);
	// A fresh fire hit does too.
	ClearMonsters();
	Monster &struck = PlaceMonster(0, { 44, 40 });
	Use(player, WeaponSkill::SwampGasArrow, { 44, 40 });
	Zones(player, 12);
	ASSERT_EQ(SkillZoneCount(), 1U);
	ApplyMonsterDamage(DamageType::Fire, struck, Fixed26_6::fromInt(1));
	EXPECT_EQ(struck.poe2.fireHitTicks, FireHitTicks);
	Zones(player, 1);
	EXPECT_EQ(SkillZoneCount(), 0U);
	// Three clouds at most.
	ClearMonsters();
	for (int i = 0; i < 4; i++)
		Use(player, WeaponSkill::SwampGasArrow, { 44, 36 + 2 * i });
	EXPECT_EQ(SkillZoneCount(), static_cast<size_t>(SwampGasClouds));
}

} // namespace
} // namespace devilution
