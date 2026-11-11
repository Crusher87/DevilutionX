#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/dun_tile_data.hpp"
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
#include "poe2/skill_details.hpp"
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

/** @brief The draft gems' Dagger batch (Curtis 2026-11-07, Q48, build order 4), each built as its row in drafts-61-priced.json. */
class Poe2DaggerDraftsTest : public ::testing::Test {
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
				dCorpse[x][y] = 0;
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

	static bool Fits(SupportGem gem, WeaponSkill skill)
	{
		const SupportData *data = FindSupportData(gem);
		EXPECT_NE(data, nullptr);
		return data != nullptr && TableSupportFits(*data, SkillSlot::ForAttack(skill));
	}

	/** @brief Uses the skill on the target as its animation's action frame would. */
	static void Use(Player &player, WeaponSkill skill, Point target)
	{
		player.poe2.castSkill = skill;
		ExecuteSkillCast(player, skill, target);
		player.poe2.castSkill = WeaponSkill::None;
	}

	/** @brief Uses the skill with one seed after another until the monster is hurt (a weapon hit can miss), at most eight times. */
	static void UseUntilHurt(Player &player, WeaponSkill skill, Point target, const Monster &monster)
	{
		for (uint32_t seed = 1; seed <= 8 && !Hurt(monster); seed++) {
			SetRndSeed(seed);
			Use(player, skill, target);
		}
	}

	static void Zones(Player &player, int ticks)
	{
		for (int tick = 0; tick < ticks; tick++)
			ProcessSkillZones(player);
	}

	/** @brief Runs the zones until the monster is hurt, at most this many ticks; the ticks it took. */
	static int ZonesUntilHurt(Player &player, const Monster &monster, int most)
	{
		int ticks = 0;
		while (ticks < most && !Hurt(monster)) {
			ProcessSkillZones(player);
			ticks++;
		}
		return ticks;
	}

	static int Curve(const Player &player, WeaponSkill skill)
	{
		return GemDamagePercent(SkillSlot::ForAttack(skill), player.poe2.effectiveLevel(skill));
	}
};

struct DraftRow {
	WeaponSkill skill;
	int tier;
	int level;
	/** The listed mana, before the melee third off, and what the gem costs at level 1 (mana_gem1). */
	int mana;
	int gemOneMana;
	int percent;
	/** The speed row's damage_pct and mana_pct. */
	int speedDamage;
	int speedMana;
	DamageType type;
	/** The row's convertPercent: its stab lands as its element. */
	int convert;
	/** Whether it pays two thirds (melee_mana_cut); the throws pay it all. */
	bool meleeCut;
};

// drafts-61-priced.json (Curtis 2026-11-07, Q46-Q48; build order 4, the Dagger batch).
constexpr std::array<DraftRow, 9> Drafts { {
	{ WeaponSkill::FireBlast, 1, 1, 6, 7, 60, 125, 113, DamageType::Fire, 0, false },
	{ WeaponSkill::ShockWeb, 1, 1, 6, 7, 50, 125, 113, DamageType::Lightning, 0, false },
	{ WeaponSkill::BladesOfIce, 3, 3, 4, 3, 190, 100, 100, DamageType::Cold, 100, true },
	{ WeaponSkill::PhoenixStrike, 9, 15, 12, 8, 235, 100, 100, DamageType::Physical, 0, true },
	{ WeaponSkill::Bloodletting, 9, 15, 13, 9, 415, 100, 100, DamageType::Physical, 0, true },
	{ WeaponSkill::EmberFang, 11, 20, 15, 10, 380, 100, 100, DamageType::Fire, 100, true },
	{ WeaponSkill::DeathSentry, 13, 25, 18, 20, 155, 125, 113, DamageType::Physical, 0, false },
	{ WeaponSkill::VizJaqtaarStrike, 13, 25, 18, 13, 285, 124, 112, DamageType::Physical, 0, true },
	{ WeaponSkill::WintersEdge, 13, 25, 18, 15, 525, 143, 122, DamageType::Cold, 100, true },
} };

TEST_F(Poe2DaggerDraftsTest, DaggerDraftsAreDexterityGemsAtTheirTiers)
{
	// Appended after the Bow batch, in the batch's order.
	EXPECT_EQ(static_cast<int>(WeaponSkill::FireBlast), static_cast<int>(WeaponSkill::SwampGasArrow) + 1);
	EXPECT_EQ(WeaponSkill::Last, WeaponSkill::WintersEdge);
	for (size_t i = 0; i < Drafts.size(); i++)
		EXPECT_EQ(static_cast<int>(Drafts[i].skill), static_cast<int>(WeaponSkill::FireBlast) + static_cast<int>(i));
	for (const DraftRow &draft : Drafts) {
		const TableSkillData &data = Row(draft.skill);
		const SkillSlot slot = SkillSlot::ForAttack(draft.skill);
		EXPECT_EQ(data.weapon, TableWeapon::Dagger) << data.name;
		EXPECT_EQ(data.tabs, TT_Dagger) << data.name;
		EXPECT_EQ(data.graphic, 0) << data.name;
		EXPECT_FALSE(IsGrantedTableSkill(data)) << data.name;
		EXPECT_EQ(data.flags & TF_Spell, 0U) << data.name;
		EXPECT_EQ(data.level, draft.level) << data.name;
		EXPECT_EQ(GemTier(data.level), draft.tier) << data.name;
		EXPECT_EQ(data.mana, draft.mana) << data.name;
		EXPECT_EQ(data.damagePercent, draft.percent) << data.name;
		EXPECT_EQ(data.type, draft.type) << data.name;
		EXPECT_EQ(data.convertPercent, draft.convert) << data.name;
		EXPECT_EQ(data.curve, TableCurve::Boneshatter) << data.name;
		EXPECT_EQ(data.curveEnd, 0) << data.name;
		// No channelling, no charges.
		EXPECT_NE(data.shape, TableShape::Stream) << data.name;
		EXPECT_EQ(data.flags & (TF_Combo | TF_Frenzy), 0U) << data.name;
		EXPECT_EQ(HasMeleeManaCut(draft.skill), draft.meleeCut) << data.name;
		EXPECT_EQ(IsTableMeleeSkill(data), draft.meleeCut) << data.name;
		EXPECT_EQ(WeaponSkillManaCost(draft.skill), draft.meleeCut ? std::max(((draft.mana * 2) + 1) / 3, 1) : draft.mana) << data.name;
		EXPECT_EQ(GemBaseManaCost(slot, 1), draft.gemOneMana) << data.name;
		EXPECT_EQ(SkillSpeedDamagePercent(slot), draft.speedDamage) << data.name;
		EXPECT_EQ(SkillSpeedManaPercent(slot), draft.speedMana) << data.name;
		// Its item row: appended, uncut-only, at the table's level, Dexterity as the other dagger gems.
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
		// It is cut on the Dagger tab only.
		const std::vector<GemEntry> &catalog = GemCatalog();
		const auto entry = std::find_if(catalog.begin(), catalog.end(), [&](const GemEntry &gemEntry) { return gemEntry.skill == slot; });
		ASSERT_NE(entry, catalog.end()) << data.name;
		EXPECT_EQ(entry->level, draft.level) << data.name;
		EXPECT_TRUE(GemInCategory(*entry, GemCategory::Dagger)) << data.name;
		EXPECT_FALSE(GemInCategory(*entry, GemCategory::Sword)) << data.name;
		EXPECT_FALSE(GemInCategory(*entry, GemCategory::Elemental)) << data.name;
		EXPECT_FALSE(TableSkillExtraNote(draft.skill).empty()) << data.name;
	}
	// The drafts' shapes and extras.
	for (const WeaponSkill thrown : { WeaponSkill::FireBlast, WeaponSkill::ShockWeb, WeaponSkill::DeathSentry }) {
		EXPECT_EQ(Row(thrown).shape, TableShape::Zone);
		EXPECT_EQ(Row(thrown).flags, static_cast<uint32_t>(TF_Lobbed));
		EXPECT_EQ(Row(thrown).radius, 1);
	}
	EXPECT_EQ(Row(WeaponSkill::ShockWeb).duration, 40);
	EXPECT_EQ(Row(WeaponSkill::ShockWeb).interval, 10);
	EXPECT_EQ(ShockWebBurstPercent, 50);
	EXPECT_EQ(ShockWebPulsePercent, 25);
	EXPECT_EQ(ShockWebWebs, 2);
	EXPECT_EQ(BladesOfIceFreezeEvery, 3);
	EXPECT_EQ(BladesOfIceFreezeTicks, 20);
	EXPECT_EQ(PhoenixEvery, 3);
	EXPECT_EQ(PhoenixFireRadius, 1);
	EXPECT_EQ(PhoenixFirePercent, 150);
	EXPECT_EQ(PhoenixShards, 6);
	EXPECT_EQ(PhoenixShardPercent, 60);
	EXPECT_EQ(PhoenixChains, 3);
	EXPECT_EQ(PhoenixChainPercent, 80);
	EXPECT_EQ(BloodlettingBleedingPercent, 160);
	EXPECT_EQ(EmberFangSpreads, 2);
	EXPECT_EQ(Row(WeaponSkill::DeathSentry).duration, 200);
	EXPECT_EQ(DeathSentryIntervalTicks, 20);
	EXPECT_EQ(DeathSentryReachTiles, 4);
	EXPECT_EQ(Row(WeaponSkill::VizJaqtaarStrike).shape, TableShape::Seven);
	EXPECT_EQ(Row(WeaponSkill::VizJaqtaarStrike).count, 4);
	EXPECT_EQ(Row(WeaponSkill::VizJaqtaarStrike).radius, 4);
	EXPECT_EQ(VizJaqtaarStabsEach, 2);
	EXPECT_EQ(TableAilmentPercent(Row(WeaponSkill::WintersEdge)), 200);
	EXPECT_EQ(WintersEdgeFrozenPercent, 150);
	EXPECT_EQ(WintersEdgeShards, 4);
	EXPECT_EQ(WintersEdgeShardRadius, 1);
	EXPECT_EQ(WintersEdgeShardPercent, 15);
	// Fan of Blades keeps its 70%, and now bleeds.
	EXPECT_EQ(Row(WeaponSkill::FanOfBlades).damagePercent, 70);
	EXPECT_EQ(FanOfBladesBleedPercent, 30);
	EXPECT_EQ(FanOfBladesBleedTicks, 60);
	EXPECT_FALSE(TableSkillExtraNote(WeaponSkill::FanOfBlades).empty());
}

TEST_F(Poe2DaggerDraftsTest, DaggerDraftsHaveTheirTags)
{
	const auto has = [](WeaponSkill skill, uint32_t tag) { return (SkillTags(SkillSlot::ForAttack(skill)) & tag) == tag; };
	for (const DraftRow &draft : Drafts) {
		EXPECT_TRUE(has(draft.skill, ST_Attack)) << Row(draft.skill).name;
		EXPECT_FALSE(has(draft.skill, ST_Channelling)) << Row(draft.skill).name;
		EXPECT_FALSE(has(draft.skill, ST_Spell)) << Row(draft.skill).name;
		EXPECT_FALSE(has(draft.skill, ST_Bow)) << Row(draft.skill).name;
		EXPECT_EQ(has(draft.skill, ST_Melee), draft.meleeCut) << Row(draft.skill).name;
	}
	EXPECT_TRUE(has(WeaponSkill::FireBlast, ST_Projectile | ST_Fire | ST_Area));
	EXPECT_FALSE(has(WeaponSkill::FireBlast, ST_Duration));
	EXPECT_TRUE(has(WeaponSkill::ShockWeb, ST_Projectile | ST_Lightning | ST_Area | ST_Duration));
	EXPECT_TRUE(has(WeaponSkill::BladesOfIce, ST_Strike | ST_Frost));
	EXPECT_TRUE(has(WeaponSkill::PhoenixStrike, ST_Strike | ST_Fire | ST_Frost | ST_Lightning | ST_Area));
	EXPECT_TRUE(has(WeaponSkill::Bloodletting, ST_Strike | ST_Physical));
	// It deals no damage over time of its own (audit-drafts-dagger.md 6.5).
	EXPECT_FALSE(has(WeaponSkill::Bloodletting, ST_DamageOverTime));
	EXPECT_TRUE(has(WeaponSkill::EmberFang, ST_Strike | ST_Fire));
	EXPECT_TRUE(has(WeaponSkill::DeathSentry, ST_Placed | ST_Duration | ST_Area | ST_Physical));
	EXPECT_FALSE(has(WeaponSkill::DeathSentry, ST_Projectile));
	EXPECT_TRUE(has(WeaponSkill::VizJaqtaarStrike, ST_Strike | ST_Movement | ST_Physical));
	EXPECT_TRUE(has(WeaponSkill::WintersEdge, ST_Strike | ST_Frost));
	EXPECT_TRUE(has(WeaponSkill::FanOfBlades, ST_Projectile | ST_DamageOverTime));
	// The supports that would do nothing don't fit: more projectiles or a rebound on a single throw, Flurry of Blows on
	// Viz-Jaq'taar Strike's four stabs (drafts-61-priced.json).
	for (const WeaponSkill thrown : { WeaponSkill::FireBlast, WeaponSkill::ShockWeb }) {
		for (const SupportGem gem : { SupportGem::Volley, SupportGem::StoredVolley, SupportGem::NovaProjectiles, SupportGem::Ricochet })
			EXPECT_FALSE(Fits(gem, thrown)) << Row(thrown).name;
	}
	EXPECT_TRUE(Fits(SupportGem::Volley, WeaponSkill::FanOfBlades));
	EXPECT_FALSE(Fits(SupportGem::Multistrike, WeaponSkill::VizJaqtaarStrike));
	EXPECT_TRUE(Fits(SupportGem::Multistrike, WeaponSkill::BladesOfIce));
	EXPECT_TRUE(Fits(SupportGem::BlinkStrike, WeaponSkill::VizJaqtaarStrike));
}

// No two fire, cold or lightning gems in one tier of the Dagger tab, the older dagger gems counted.
TEST_F(Poe2DaggerDraftsTest, NoTwoGemsOfOneElementInADaggerTier)
{
	std::map<std::pair<int, uint32_t>, std::vector<std::string>> byTierAndElement;
	size_t daggerGems = 0;
	for (const GemEntry &gem : GemCatalog()) {
		if (gem.skill.kind != SkillKind::Attack || !GemInCategory(gem, GemCategory::Dagger) || IsRetiredSkill(gem.skill))
			continue;
		daggerGems++;
		const uint32_t tags = SkillTags(gem.skill);
		const uint32_t elements = tags & (ST_Fire | ST_Frost | ST_Lightning);
		// A gem of mixed elements is fine.
		if (elements == 0 || (elements & (elements - 1)) != 0)
			continue;
		byTierAndElement[{ GemTier(gem.level), elements }].push_back(GemName(gem));
	}
	EXPECT_GE(daggerGems, Drafts.size());
	for (const auto &[key, names] : byTierAndElement)
		EXPECT_EQ(names.size(), 1U) << "tier " << key.first << ": " << names.front() << " and " << names.back();
}

TEST_F(Poe2DaggerDraftsTest, FanOfBladesBleedsTheStrongestBlade)
{
	Player &player = SetUpFight();
	Monster &monster = PlaceMonster(0, { 43, 40 });
	Use(player, WeaponSkill::FanOfBlades, { 43, 40 });
	const std::vector<const Missile *> blades = MissilesOf(MissileID::Arrow);
	ASSERT_FALSE(blades.empty());
	EXPECT_EQ(blades.front()->poe2.damagePercent, Curve(player, WeaponSkill::FanOfBlades) * 70 / 100);
	const Missile blade = *blades.front();
	// A blade's hit of 10: 30% of it over 3 seconds.
	OnTableMissileHit(blade, player, monster, 10, false);
	EXPECT_EQ(monster.poe2.bleedTicks, FanOfBladesBleedTicks);
	EXPECT_EQ(monster.poe2.bleedDamagePerTick, 10 * 64 * FanOfBladesBleedPercent / 100 / FanOfBladesBleedTicks);
	EXPECT_EQ(monster.poe2.bleedSkill, WeaponSkill::FanOfBlades);
	EXPECT_FALSE(monster.poe2.bleedFromBloodletting);
	// One bleed: a weaker blade doesn't add to it or take its place; a stronger one takes its place.
	const int32_t first = monster.poe2.bleedDamagePerTick;
	OnTableMissileHit(blade, player, monster, 4, false);
	EXPECT_EQ(monster.poe2.bleedDamagePerTick, first);
	OnTableMissileHit(blade, player, monster, 40, false);
	EXPECT_EQ(monster.poe2.bleedDamagePerTick, 40 * 64 * FanOfBladesBleedPercent / 100 / FanOfBladesBleedTicks);
	EXPECT_EQ(monster.poe2.bleedTicks, FanOfBladesBleedTicks);
	// Another dagger's thrown blade doesn't bleed.
	Monster &other = PlaceMonster(1, { 45, 42 });
	Missiles.clear();
	Use(player, WeaponSkill::BladeFury, { 45, 42 });
	ASSERT_FALSE(MissilesOf(MissileID::Arrow).empty());
	OnTableMissileHit(*MissilesOf(MissileID::Arrow).front(), player, other, 40, false);
	EXPECT_EQ(other.poe2.bleedTicks, 0);
}

TEST_F(Poe2DaggerDraftsTest, FireBlastIsThrownAndBurstsOnce)
{
	Player &player = SetUpFight();
	Monster &target = PlaceMonster(0, { 44, 40 });
	Monster &beside = PlaceMonster(1, { 45, 40 });
	Monster &far = PlaceMonster(2, { 47, 40 });
	Use(player, WeaponSkill::FireBlast, { 44, 40 });
	ASSERT_EQ(SkillZoneCount(), 1U);
	EXPECT_EQ(SkillZoneCenter(0), std::optional<Point>(Point(44, 40)));
	EXPECT_EQ(TableSkillHitTarget(player, WeaponSkill::FireBlast, { 44, 40 }), Point(44, 40));
	// It flies first: nothing is hurt until it lands.
	EXPECT_FALSE(Hurt(target));
	const int ticks = ZonesUntilHurt(player, target, 20);
	EXPECT_GT(ticks, 1);
	EXPECT_TRUE(Hurt(target));
	EXPECT_TRUE(Hurt(beside));
	EXPECT_FALSE(Hurt(far));
	// Its hit is fire.
	EXPECT_GT(target.poe2.fireHitTicks, 0);
	// It bursts once and is gone.
	EXPECT_EQ(SkillZoneCount(), 0U);
	const int32_t lost = Lost(target);
	Zones(player, 20);
	EXPECT_EQ(Lost(target), lost);
}

TEST_F(Poe2DaggerDraftsTest, ShockWebBurstsThenPulsesFourTimes)
{
	Player &player = SetUpFight();
	Monster &monster = PlaceMonster(0, { 44, 40 });
	Use(player, WeaponSkill::ShockWeb, { 44, 40 });
	ASSERT_EQ(SkillZoneCount(), 1U);
	ZonesUntilHurt(player, monster, 20);
	ASSERT_TRUE(Hurt(monster));
	const int32_t burst = Lost(monster);
	// Four pulses of half the burst each over its 2 seconds; a shocked monster takes more of each.
	std::vector<int32_t> steps;
	int32_t before = Lost(monster);
	for (int tick = 0; tick < 60 && SkillZoneCount() > 0; tick++) {
		ProcessSkillZones(player);
		if (Lost(monster) != before) {
			steps.push_back(Lost(monster) - before);
			before = Lost(monster);
		}
	}
	EXPECT_EQ(SkillZoneCount(), 0U);
	ASSERT_EQ(steps.size(), 4U);
	for (const int32_t pulse : steps) {
		EXPECT_GE(pulse * 100, burst * 40);
		EXPECT_LE(pulse * 100, burst * 80);
	}
	// Two webs at most.
	for (int i = 0; i < 3; i++)
		Use(player, WeaponSkill::ShockWeb, { 44, 36 + (2 * i) });
	EXPECT_EQ(SkillZoneCount(), static_cast<size_t>(ShockWebWebs));
}

TEST_F(Poe2DaggerDraftsTest, BladesOfIceChillsAndFreezesEveryThirdStab)
{
	Player &player = SetUpFight();
	Monster &front = PlaceMonster(0, { 41, 40 });
	int frozenStabs = 0;
	for (int stab = 1; stab <= 6; stab++) {
		EXPECT_EQ(BladesOfIceNextFreezes(player), stab % BladesOfIceFreezeEvery == 0) << stab;
		front.poe2.freezeTicks = 0;
		front.poe2.freezeBuildup = 0;
		front.poe2.chillTicks = 0;
		Heal(front);
		SetRndSeed(static_cast<uint32_t>(stab));
		Use(player, WeaponSkill::BladesOfIce, { 41, 40 });
		player.poe2.tick();
		if (!Hurt(front))
			continue;
		// Every stab that lands chills: its hit is cold.
		EXPECT_GT(front.poe2.chillTicks, 0) << stab;
		if (stab % BladesOfIceFreezeEvery == 0) {
			EXPECT_GE(front.poe2.freezeTicks, BladesOfIceFreezeTicks - 1) << stab;
			frozenStabs++;
		}
	}
	EXPECT_GT(frozenStabs, 0);
	// The count starts over when the hero stops.
	Use(player, WeaponSkill::BladesOfIce, { 41, 40 });
	EXPECT_EQ(player.poe2.bladesOfIceCount, 1);
	for (int i = 0; i < BladesOfIceResetTicks; i++)
		player.poe2.tick();
	EXPECT_EQ(player.poe2.bladesOfIceCount, 0);
}

TEST_F(Poe2DaggerDraftsTest, PhoenixStrikeAddsFireIceAndLightningInTurn)
{
	Player &player = SetUpFight();
	// What it strikes, one beside that, and four more for the lightning, each within a leap of the last.
	PlaceMonster(0, { 41, 40 });
	Monster &beside = PlaceMonster(1, { 42, 40 });
	Monster &second = PlaceMonster(2, { 43, 42 });
	Monster &third = PlaceMonster(3, { 45, 43 });
	Monster &fourth = PlaceMonster(4, { 47, 44 });
	Monster &fifth = PlaceMonster(5, { 49, 45 });
	const auto three = [&]() {
		for (int strike = 0; strike < PhoenixEvery; strike++) {
			Heal(beside);
			Use(player, WeaponSkill::PhoenixStrike, { 41, 40 });
			player.poe2.tick();
		}
	};
	// The first two strikes add nothing.
	EXPECT_EQ(PhoenixNextElement(player), std::nullopt);
	Use(player, WeaponSkill::PhoenixStrike, { 41, 40 });
	Use(player, WeaponSkill::PhoenixStrike, { 41, 40 });
	EXPECT_FALSE(Hurt(beside));
	EXPECT_EQ(PhoenixNextElement(player), std::optional<DamageType>(DamageType::Fire));
	// The third: fire bursts round what it struck.
	Use(player, WeaponSkill::PhoenixStrike, { 41, 40 });
	EXPECT_TRUE(Hurt(beside));
	EXPECT_FALSE(Hurt(second));
	EXPECT_EQ(MissilesOf(MissileID::BlueFlare).size(), 0U);
	// The sixth: a ring of ice shards from it, each its own way, on past it.
	Heal(beside);
	Use(player, WeaponSkill::PhoenixStrike, { 41, 40 });
	Use(player, WeaponSkill::PhoenixStrike, { 41, 40 });
	EXPECT_EQ(PhoenixNextElement(player), std::optional<DamageType>(DamageType::Cold));
	Use(player, WeaponSkill::PhoenixStrike, { 41, 40 });
	const std::vector<const Missile *> shards = MissilesOf(MissileID::BlueFlare);
	ASSERT_EQ(shards.size(), static_cast<size_t>(PhoenixShards));
	std::vector<std::pair<int, int>> ways;
	for (const Missile *shard : shards) {
		EXPECT_TRUE(shard->poe2.draftPart);
		EXPECT_EQ(shard->poe2.damageType, static_cast<int8_t>(DamageType::Cold));
		EXPECT_EQ(shard->poe2.damagePercent, Curve(player, WeaponSkill::PhoenixStrike) * (235 * PhoenixShardPercent / 100) / 100);
		EXPECT_EQ(Point(shard->position.start), Point(41, 40));
		EXPECT_EQ(shard->lastCollisionTargetHash, dMonster[41][40] ^ dPlayer[41][40]);
		ways.emplace_back(shard->position.velocity.deltaX, shard->position.velocity.deltaY);
	}
	std::sort(ways.begin(), ways.end());
	EXPECT_EQ(std::unique(ways.begin(), ways.end()) - ways.begin(), PhoenixShards);
	// Each along its own whole direction (the fan rule): on screen (a missile's y speed is halved there) they fly a
	// multiple of 45 degrees apart, six of the eight ways.
	std::vector<int> octants;
	for (const Missile *shard : shards) {
		const double degrees = std::atan2(2.0 * shard->position.velocity.deltaY, static_cast<double>(shard->position.velocity.deltaX)) * 180.0 / 3.14159265358979;
		const double octant = (degrees + 360.0) / 45.0;
		EXPECT_NEAR(octant, std::round(octant), 0.07);
		octants.push_back(static_cast<int>(std::round(octant)) % 8);
	}
	std::sort(octants.begin(), octants.end());
	EXPECT_EQ(std::unique(octants.begin(), octants.end()) - octants.begin(), PhoenixShards);
	EXPECT_FALSE(Hurt(beside));
	// The ninth: lightning leaps on to three more, the nearest first, not back to what it struck.
	Missiles.clear();
	for (Monster *monster : { &beside, &second, &third, &fourth, &fifth })
		Heal(*monster);
	three();
	EXPECT_TRUE(Hurt(beside));
	EXPECT_TRUE(Hurt(second));
	EXPECT_TRUE(Hurt(third));
	EXPECT_FALSE(Hurt(fourth));
	EXPECT_FALSE(Hurt(fifth));
	EXPECT_EQ(MissilesOf(MissileID::BlueFlare).size(), 0U);
	// Then fire again.
	EXPECT_EQ(player.poe2.phoenixElement, 0);
}

TEST_F(Poe2DaggerDraftsTest, BloodlettingTakesMoreFromTheBleedingAndConsumesOthersBleeds)
{
	Player &player = SetUpFight();
	// The same stab on a bleeding target and on a plain one: the bleeding takes 60% more.
	const auto stab = [&](Monster &monster, uint32_t seed) {
		SetRndSeed(seed);
		Use(player, WeaponSkill::Bloodletting, { 41, 40 });
		return Lost(monster);
	};
	uint32_t seed = 1;
	int32_t plain = 0;
	for (; seed <= 8 && plain == 0; seed++) {
		ClearMonsters();
		Monster &monster = PlaceMonster(0, { 41, 40 });
		plain = stab(monster, seed);
	}
	ASSERT_GT(plain, 0);
	seed--;
	ClearMonsters();
	Monster &own = PlaceMonster(0, { 41, 40 });
	own.poe2.bleedTicks = 60;
	own.poe2.bleedDamagePerTick = 64;
	own.poe2.bleedFromBloodletting = true;
	const int32_t ownLost = stab(own, seed);
	EXPECT_NEAR(static_cast<double>(ownLost) / plain, BloodlettingBleedingPercent / 100.0, 0.03);
	// Its own bleed stays (the Cobra Strike rule).
	EXPECT_EQ(own.poe2.bleedTicks, 60);
	EXPECT_EQ(own.poe2.bleedDamagePerTick, 64);
	// Another skill's bleed deals all it has left at once, and ends.
	ClearMonsters();
	Monster &other = PlaceMonster(0, { 41, 40 });
	other.poe2.bleedTicks = 60;
	other.poe2.bleedDamagePerTick = 64;
	other.poe2.bleedSource = 0;
	other.poe2.bleedSkill = WeaponSkill::FanOfBlades;
	const int32_t otherLost = stab(other, seed);
	EXPECT_EQ(otherLost - ownLost, 60 * 64);
	EXPECT_EQ(other.poe2.bleedTicks, 0);
	EXPECT_EQ(other.poe2.bleedDamagePerTick, 0);
}

TEST_F(Poe2DaggerDraftsTest, BloodlettingDoesNotConsumeTheBleedItMadeItself)
{
	Player &player = SetUpFight();
	Monster &monster = PlaceMonster(0, { 41, 40 });
	// The Butcher's Hook bleeds every hit: the first stab's bleed is Bloodletting's own.
	Supported(WeaponSkill::Bloodletting, SupportGem::ButchersHook);
	UseUntilHurt(player, WeaponSkill::Bloodletting, { 41, 40 }, monster);
	ASSERT_TRUE(Hurt(monster));
	ASSERT_GT(monster.poe2.bleedTicks, 0);
	EXPECT_TRUE(monster.poe2.bleedFromBloodletting);
	const int32_t perTick = monster.poe2.bleedDamagePerTick;
	const uint8_t ticks = monster.poe2.bleedTicks;
	// The next stab takes the bleeding's 60% more, but leaves its own bleed ticking.
	for (int tick = 0; tick < 10; tick++)
		ProcessUniqueMonsterAilments(monster);
	ASSERT_EQ(monster.poe2.bleedTicks, ticks - 10);
	Heal(monster);
	for (uint32_t seed = 1; seed <= 8 && !Hurt(monster); seed++) {
		SetRndSeed(seed);
		Use(player, WeaponSkill::Bloodletting, { 41, 40 });
	}
	ASSERT_TRUE(Hurt(monster));
	EXPECT_GT(monster.poe2.bleedTicks, 0);
	EXPECT_GE(monster.poe2.bleedDamagePerTick, perTick / 2);
	EXPECT_TRUE(monster.poe2.bleedFromBloodletting);
	// Another skill's bleed is its to consume: a Fan of Blades blade's.
	ClearMonsters();
	player.poe2.slotSupports[0] = {};
	Monster &fresh = PlaceMonster(0, { 41, 40 });
	Use(player, WeaponSkill::FanOfBlades, { 41, 40 });
	ASSERT_FALSE(MissilesOf(MissileID::Arrow).empty());
	OnTableMissileHit(*MissilesOf(MissileID::Arrow).front(), player, fresh, 40, false);
	ASSERT_GT(fresh.poe2.bleedTicks, 0);
	EXPECT_FALSE(fresh.poe2.bleedFromBloodletting);
	UseUntilHurt(player, WeaponSkill::Bloodletting, { 41, 40 }, fresh);
	EXPECT_EQ(fresh.poe2.bleedTicks, 0);
}

TEST_F(Poe2DaggerDraftsTest, EmberFangAlwaysIgnitesAndSpreadsToTwo)
{
	Player &player = SetUpFight();
	Monster &target = PlaceMonster(0, { 41, 40 });
	Monster &a = PlaceMonster(1, { 42, 40 });
	Monster &b = PlaceMonster(2, { 42, 41 });
	Monster &c = PlaceMonster(3, { 41, 41 });
	Monster &far = PlaceMonster(4, { 44, 40 });
	UseUntilHurt(player, WeaponSkill::EmberFang, { 41, 40 }, target);
	ASSERT_TRUE(Hurt(target));
	// Its hit is fire, and always ignites: 20% of what it dealt a second.
	EXPECT_GT(target.poe2.fireHitTicks, 0);
	EXPECT_EQ(target.poe2.igniteTicks, IgniteTicks);
	EXPECT_GE(target.poe2.igniteDamagePerTick, Lost(target) * IgnitePercentPerSecond / 100 / 20);
	// Two of those beside it catch the same fire; they take no hit.
	int lit = 0;
	for (Monster *monster : { &a, &b, &c }) {
		EXPECT_FALSE(Hurt(*monster));
		if (monster->poe2.igniteTicks > 0) {
			lit++;
			EXPECT_EQ(monster->poe2.igniteTicks, IgniteTicks);
			EXPECT_EQ(monster->poe2.igniteDamagePerTick, std::max<int32_t>(Lost(target) * IgnitePercentPerSecond / 100 / 20, 1));
		}
	}
	EXPECT_EQ(lit, EmberFangSpreads);
	EXPECT_EQ(far.poe2.igniteTicks, 0);
}

TEST_F(Poe2DaggerDraftsTest, DeathSentryBlowsUpACorpseEverySecond)
{
	Player &player = SetUpFight();
	Use(player, WeaponSkill::DeathSentry, { 44, 40 });
	ASSERT_EQ(SkillZoneCount(), 1U);
	EXPECT_EQ(SkillZoneCenter(0), std::optional<Point>(Point(44, 40)));
	// Two corpses within its reach, one beyond it; a monster beside each.
	dCorpse[47][40] = 1;
	dCorpse[44][42] = 1;
	dCorpse[49][40] = 1;
	Monster &nearFirst = PlaceMonster(0, { 44, 43 });
	Monster &nearSecond = PlaceMonster(1, { 47, 41 });
	Monster &beyond = PlaceMonster(2, { 50, 40 });
	// It lands (a throw of 8 ticks, LobTicks), then blows up the nearest corpse a second later, then one a second.
	constexpr int Flight = 8;
	Zones(player, Flight + DeathSentryIntervalTicks - 1);
	EXPECT_FALSE(Hurt(nearFirst));
	Zones(player, 1);
	EXPECT_TRUE(Hurt(nearFirst));
	EXPECT_EQ(dCorpse[44][42], 0);
	EXPECT_FALSE(Hurt(nearSecond));
	Zones(player, DeathSentryIntervalTicks - 1);
	EXPECT_FALSE(Hurt(nearSecond));
	Zones(player, 1);
	EXPECT_TRUE(Hurt(nearSecond));
	EXPECT_EQ(dCorpse[47][40], 0);
	// None left within reach: the one beyond stays.
	Zones(player, DeathSentryIntervalTicks * 3);
	EXPECT_EQ(dCorpse[49][40], 1);
	EXPECT_FALSE(Hurt(beyond));
	// It lasts 10 seconds; three sentries at most.
	Zones(player, Row(WeaponSkill::DeathSentry).duration);
	EXPECT_EQ(SkillZoneCount(), 0U);
	for (int i = 0; i < 4; i++)
		Use(player, WeaponSkill::DeathSentry, { 44, 36 + (2 * i) });
	EXPECT_EQ(SkillZoneCount(), static_cast<size_t>(MaxPlacedTraps));
}

TEST_F(Poe2DaggerDraftsTest, VizJaqtaarStrikeStabsFourAndComesOutBehind)
{
	Player &player = SetUpFight();
	// Four within reach, one beyond: each of the four takes a stab.
	std::vector<Monster *> near;
	const std::array<Point, 4> tiles { { { 42, 40 }, { 40, 42 }, { 43, 41 }, { 41, 43 } } };
	for (size_t i = 0; i < tiles.size(); i++)
		near.push_back(&PlaceMonster(i, tiles[i]));
	Monster &beyond = PlaceMonster(4, { 46, 40 });
	SetRndSeed(3);
	Use(player, WeaponSkill::VizJaqtaarStrike, { 42, 40 });
	int hurt = 0;
	for (const Monster *monster : near)
		hurt += Hurt(*monster) ? 1 : 0;
	EXPECT_GE(hurt, 3);
	EXPECT_FALSE(Hurt(beyond));
	// The hero came out behind the last of them, facing it.
	EXPECT_NE(player.position.tile, Point(40, 40));
	// A lone monster takes two of the stabs: about twice what each of four takes, over many uses.
	int32_t eachOfFour = 0;
	for (uint32_t seed = 1; seed <= 20; seed++) {
		for (Monster *monster : near)
			Heal(*monster);
		player.position.tile = player.position.future = player.position.old = { 40, 40 };
		SetRndSeed(seed);
		Use(player, WeaponSkill::VizJaqtaarStrike, { 42, 40 });
		for (const Monster *monster : near)
			eachOfFour += Lost(*monster);
	}
	eachOfFour /= static_cast<int32_t>(near.size());
	ClearMonsters();
	Monster &lone = PlaceMonster(0, { 42, 40 });
	int32_t twoStabs = 0;
	for (uint32_t seed = 1; seed <= 20; seed++) {
		Heal(lone);
		player.position.tile = player.position.future = player.position.old = { 40, 40 };
		SetRndSeed(seed);
		Use(player, WeaponSkill::VizJaqtaarStrike, { 42, 40 });
		twoStabs += Lost(lone);
	}
	ASSERT_GT(eachOfFour, 0);
	EXPECT_NEAR(static_cast<double>(twoStabs) / eachOfFour, 2.0, 0.4);
	// It comes out behind the lone monster, facing it.
	EXPECT_EQ(player.position.tile, Point(43, 40));
	EXPECT_EQ(player._pdir, GetDirection(Point(43, 40), Point(42, 40)));
}

TEST_F(Poe2DaggerDraftsTest, WintersEdgeShattersTheFrozen)
{
	Player &player = SetUpFight();
	const auto stab = [&](uint32_t seed, bool frozen, std::vector<Monster *> &around) {
		ClearMonsters();
		Monster &target = PlaceMonster(0, { 42, 40 });
		around.clear();
		const std::array<Point, 5> tiles { { { 43, 40 }, { 43, 41 }, { 42, 41 }, { 43, 39 }, { 42, 39 } } };
		for (size_t i = 0; i < tiles.size(); i++)
			around.push_back(&PlaceMonster(i + 1, tiles[i]));
		if (frozen)
			target.poe2.freezeTicks = 40;
		player.position.tile = player.position.future = player.position.old = { 41, 40 };
		SetRndSeed(seed);
		Use(player, WeaponSkill::WintersEdge, { 42, 40 });
		return &target;
	};
	std::vector<Monster *> around;
	uint32_t seed = 1;
	int32_t plain = 0;
	for (; seed <= 8 && plain == 0; seed++)
		plain = Lost(*stab(seed, false, around));
	ASSERT_GT(plain, 0);
	// No shards off a target that wasn't frozen.
	for (const Monster *monster : around)
		EXPECT_FALSE(Hurt(*monster));
	Monster *frozen = stab(seed - 1, true, around);
	EXPECT_NEAR(static_cast<double>(Lost(*frozen)) / plain, WintersEdgeFrozenPercent / 100.0, 0.03);
	EXPECT_EQ(frozen->poe2.freezeTicks, 0);
	// Its shards hit four of the five beside it, each once, as cold.
	int hit = 0;
	for (const Monster *monster : around) {
		if (Hurt(*monster)) {
			hit++;
			EXPECT_TRUE(monster->poe2.chillTicks > 0 || monster->poe2.freezeBuildup > 0 || monster->poe2.freezeTicks > 0);
		}
	}
	EXPECT_EQ(hit, WintersEdgeShards);
}

// audit-drafts-dagger.md 6.1: Ember Fang's sure ignite keeps the ignite supports' rules.
TEST_F(Poe2DaggerDraftsTest, DaggerFix1EmberFangsIgniteFollowsTheIgniteSupports)
{
	Player &player = SetUpFight();
	const auto stab = [&](std::array<Monster *, 3> &out) {
		ClearMonsters();
		out[0] = &PlaceMonster(0, { 41, 40 });
		out[1] = &PlaceMonster(1, { 42, 40 });
		out[2] = &PlaceMonster(2, { 42, 41 });
		UseUntilHurt(player, WeaponSkill::EmberFang, { 41, 40 }, *out[0]);
	};
	std::array<Monster *, 3> hit {};
	// Hellfire's ignites burn 75% harder: the sure ignite and its spread with them.
	ASSERT_TRUE(Fits(SupportGem::Hellfire, WeaponSkill::EmberFang));
	Supported(WeaponSkill::EmberFang, SupportGem::Hellfire);
	stab(hit);
	ASSERT_TRUE(Hurt(*hit[0]));
	const double base = static_cast<double>(Lost(*hit[0])) * IgnitePercentPerSecond / 100 / 20;
	EXPECT_GE(hit[0]->poe2.igniteDamagePerTick / base, 1.7);
	for (size_t i = 1; i < hit.size(); i++) {
		ASSERT_GT(hit[i]->poe2.igniteTicks, 0);
		EXPECT_NEAR(hit[i]->poe2.igniteDamagePerTick / base, 1.75, 0.03);
	}
	// What says it can't ignite stops the sure ignite and its spread.
	for (const SupportGem gem : { SupportGem::FuelThePyre, SupportGem::AndarielsAnguish }) {
		Supported(WeaponSkill::EmberFang, gem);
		stab(hit);
		ASSERT_TRUE(Hurt(*hit[0]));
		for (const Monster *monster : hit)
			EXPECT_EQ(monster->poe2.igniteTicks, 0) << static_cast<int>(gem);
	}
}

// audit-drafts-dagger.md 6.2: Shock Web's text says it can shock, and its details show the burst it deals.
TEST_F(Poe2DaggerDraftsTest, DaggerFix2ShockWebSaysWhatItDoes)
{
	const std::string description(Row(WeaponSkill::ShockWeb).description);
	EXPECT_NE(description.find("can shock"), std::string::npos) << description;
	EXPECT_EQ(description.find("shocks what"), std::string::npos) << description;
	const std::string note = TableSkillExtraNote(WeaponSkill::ShockWeb);
	EXPECT_NE(note.find("50% of that"), std::string::npos) << note;
	EXPECT_NE(note.find("chance to shock"), std::string::npos) << note;
	// Fire Blast is on the same curve and speed row, and its hit is its whole row: the web shows half its 50%.
	Player &player = SetUpFight();
	const double web = SkillHitAverage(player, SkillSlot::ForAttack(WeaponSkill::ShockWeb), 1);
	const double blast = SkillHitAverage(player, SkillSlot::ForAttack(WeaponSkill::FireBlast), 1);
	ASSERT_GT(blast, 0.0);
	EXPECT_NEAR(web / blast, (50.0 * ShockWebBurstPercent / 100) / 60.0, 0.02);
}

// audit-drafts-dagger.md 6.3: the Cold Arrow wording.
TEST_F(Poe2DaggerDraftsTest, DaggerFix3WintersEdgeFreezeBuildsTwiceAsFast)
{
	const std::string note = TableSkillExtraNote(WeaponSkill::WintersEdge);
	EXPECT_NE(note.find("twice as fast"), std::string::npos) << note;
	EXPECT_EQ(note.find("200%"), std::string::npos) << note;
}

// audit-drafts-dagger.md 6.4: a Pestilence spread marks only the bleeds it lands as its own.
TEST_F(Poe2DaggerDraftsTest, DaggerFix4PestilencePassesTheBleedMarkOnlyWithTheBleed)
{
	SetUpFight();
	Supported(WeaponSkill::Bloodletting, SupportGem::Pestilence);
	Monster &dead = PlaceMonster(0, { 45, 45 });
	Monster &stronger = PlaceMonster(1, { 46, 45 });
	Monster &clean = PlaceMonster(2, { 45, 46 });
	dead.poe2.lastHitPlayer = 0;
	dead.poe2.lastHitSkillKind = static_cast<uint8_t>(SkillKind::Attack);
	dead.poe2.lastHitSkillId = SkillSlot::ForAttack(WeaponSkill::Bloodletting).id;
	dead.poe2.bleedTicks = 40;
	dead.poe2.bleedDamagePerTick = 100;
	dead.poe2.bleedSkill = WeaponSkill::Bloodletting;
	dead.poe2.bleedFromBloodletting = true;
	stronger.poe2.bleedTicks = 20;
	stronger.poe2.bleedDamagePerTick = 200;
	stronger.poe2.bleedSkill = WeaponSkill::FanOfBlades;
	stronger.poe2.bleedFromBloodletting = false;
	OnSupportedKill(dead);
	// The neighbour keeps its stronger Fan bleed, so its mark stays the Fan's.
	EXPECT_EQ(stronger.poe2.bleedDamagePerTick, 200);
	EXPECT_EQ(stronger.poe2.bleedSkill, WeaponSkill::FanOfBlades);
	EXPECT_FALSE(stronger.poe2.bleedFromBloodletting);
	// One with no bleed takes Bloodletting's, mark and all.
	EXPECT_EQ(clean.poe2.bleedDamagePerTick, 80);
	EXPECT_EQ(clean.poe2.bleedSkill, WeaponSkill::Bloodletting);
	EXPECT_TRUE(clean.poe2.bleedFromBloodletting);
}

} // namespace
} // namespace devilution
