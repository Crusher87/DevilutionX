#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <string_view>
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
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

/** @brief The draft gems' Staff batch (Curtis 2026-11-07, Q48), each built as its row in drafts-61-priced.json. */
class Poe2StaffDraftsTest : public ::testing::Test {
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

	static std::vector<const Missile *> MissilesOf(MissileID type)
	{
		std::vector<const Missile *> found;
		for (const Missile &missile : Missiles) {
			if (missile._mitype == type)
				found.push_back(&missile);
		}
		return found;
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
			Missiles.clear();
			SetRndSeed(seed);
			Use(player, skill, target);
		}
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
};

// drafts-61-priced.json (Curtis 19:33 UTC 11-07, Q46-Q48; build order 1, the Staff batch; Shard of Winter dropped).
constexpr std::array<DraftRow, 12> Drafts { {
	{ WeaponSkill::WrathOfTheSkies, 1, 1, 6, 5, 55, 143, 122, DamageType::Lightning },
	{ WeaponSkill::FrostveinRupture, 1, 1, 6, 5, 28, 134, 117, DamageType::Cold },
	{ WeaponSkill::RimeStrikes, 3, 3, 8, 5, 140, 100, 100, DamageType::Cold },
	{ WeaponSkill::NorthwindWave, 7, 10, 12, 11, 85, 182, 141, DamageType::Cold },
	{ WeaponSkill::ThunderFurrow, 9, 15, 14, 11, 150, 134, 117, DamageType::Lightning },
	{ WeaponSkill::Shatterpalm, 9, 15, 14, 11, 70, 134, 117, DamageType::Cold },
	{ WeaponSkill::DervishAdvance, 11, 20, 16, 21, 55, 273, 187, DamageType::Physical },
	{ WeaponSkill::StormSiphon, 11, 20, 15, 10, 380, 100, 100, DamageType::Lightning },
	{ WeaponSkill::TempestSweep, 11, 20, 16, 13, 120, 134, 117, DamageType::Physical },
	{ WeaponSkill::PhantomStep, 13, 25, 18, 16, 85, 167, 134, DamageType::Physical },
	{ WeaponSkill::SkylordsStorm, 13, 25, 18, 15, 105, 143, 122, DamageType::Lightning },
	{ WeaponSkill::PalmOfTheFrozenNorth, 13, 25, 15, 10, 275, 100, 100, DamageType::Cold },
} };

TEST_F(Poe2StaffDraftsTest, StaffDraftsAreStaffGemsAtTheirTiers)
{
	// Appended after the poison skills, in the batch's order.
	EXPECT_EQ(static_cast<int>(WeaponSkill::WrathOfTheSkies), static_cast<int>(WeaponSkill::AcidCloud) + 1);
	EXPECT_EQ(static_cast<int>(WeaponSkill::HellfireRoar), static_cast<int>(WeaponSkill::PalmOfTheFrozenNorth) + 1);
	for (size_t i = 0; i < Drafts.size(); i++)
		EXPECT_EQ(static_cast<int>(Drafts[i].skill), static_cast<int>(WeaponSkill::WrathOfTheSkies) + static_cast<int>(i));
	for (const DraftRow &draft : Drafts) {
		const TableSkillData &data = Row(draft.skill);
		const SkillSlot slot = SkillSlot::ForAttack(draft.skill);
		EXPECT_EQ(data.weapon, TableWeapon::Staff) << data.name;
		EXPECT_EQ(data.tabs, TT_Staff) << data.name;
		EXPECT_EQ(data.graphic, 0) << data.name;
		EXPECT_FALSE(IsGrantedTableSkill(data)) << data.name;
		EXPECT_EQ(data.flags & TF_Spell, 0U) << data.name;
		EXPECT_EQ(data.level, draft.level) << data.name;
		EXPECT_EQ(GemTier(data.level), draft.tier) << data.name;
		EXPECT_EQ(data.mana, draft.mana) << data.name;
		EXPECT_EQ(data.damagePercent, draft.percent) << data.name;
		EXPECT_EQ(data.type, draft.type) << data.name;
		EXPECT_EQ(data.curve, TableCurve::Boneshatter) << data.name;
		EXPECT_EQ(data.curveEnd, 0) << data.name;
		// Every one is a melee attack and pays two thirds of its listed mana (melee_mana_cut), then its speed's share.
		EXPECT_TRUE(HasMeleeManaCut(draft.skill)) << data.name;
		EXPECT_EQ(WeaponSkillManaCost(draft.skill), std::max(((draft.mana * 2) + 1) / 3, 1)) << data.name;
		EXPECT_EQ(GemBaseManaCost(slot, 1), draft.gemOneMana) << data.name;
		EXPECT_EQ(SkillSpeedDamagePercent(slot), draft.speedDamage) << data.name;
		EXPECT_EQ(SkillSpeedManaPercent(slot), draft.speedMana) << data.name;
		// Its item row: appended, uncut-only, at the table's level, Dexterity as the other staff gems.
		const std::string name = std::string(data.name) + " Gem";
		const auto row = std::find_if(AllItemsList.begin(), AllItemsList.end(), [&](const ItemData &item) { return item.iName == name; });
		ASSERT_NE(row, AllItemsList.end()) << name;
		EXPECT_EQ(row->iMinMLvl, data.level) << name;
		EXPECT_EQ(row->dropRate, 0) << name;
		EXPECT_EQ(row->iMinDex, 18 + 3 * draft.level) << name;
		EXPECT_EQ(row->iMinMag, 0) << name;
		EXPECT_EQ(row->iValue, 450 + 130 * draft.level) << name;
		EXPECT_EQ(FindTableSkillGem(*row), &data) << name;
		Item gem;
		ASSERT_TRUE(CreateSkillGemItem(gem, slot, 1)) << data.name;
		EXPECT_EQ(SkillForGem(gem), slot) << data.name;
		// It is cut on the Staff tab only.
		const std::vector<GemEntry> &catalog = GemCatalog();
		const auto entry = std::find_if(catalog.begin(), catalog.end(), [&](const GemEntry &gemEntry) { return gemEntry.skill == slot; });
		ASSERT_NE(entry, catalog.end()) << data.name;
		EXPECT_EQ(entry->level, draft.level) << data.name;
		EXPECT_TRUE(GemInCategory(*entry, GemCategory::Staff)) << data.name;
		EXPECT_FALSE(GemInCategory(*entry, GemCategory::Elemental)) << data.name;
		EXPECT_FALSE(TableSkillExtraNote(draft.skill).empty()) << data.name;
	}
	// The drafts' extras.
	EXPECT_EQ(Row(WeaponSkill::WrathOfTheSkies).convertPercent, 60);
	EXPECT_EQ(Row(WeaponSkill::WrathOfTheSkies).missile, MissileID::ChargedBolt);
	EXPECT_EQ(Row(WeaponSkill::WrathOfTheSkies).count, 3);
	EXPECT_EQ(WrathBoltSharePercent, 50);
	EXPECT_EQ(FrostveinSpikePercent, 200);
	EXPECT_EQ(FrostveinFrozenPercent, 150);
	EXPECT_EQ(Row(WeaponSkill::FrostveinRupture).count, 5);
	EXPECT_EQ(Row(WeaponSkill::NorthwindWave).count, 4);
	EXPECT_EQ(TableAilmentPercent(Row(WeaponSkill::NorthwindWave)), 200);
	EXPECT_EQ(Row(WeaponSkill::ThunderFurrow).count, 8);
	EXPECT_EQ(TableAilmentPercent(Row(WeaponSkill::ThunderFurrow)), 200);
	EXPECT_EQ(TableAilmentPercent(Row(WeaponSkill::PalmOfTheFrozenNorth)), 200);
	EXPECT_EQ(TableAilmentPercent(Row(WeaponSkill::RimeStrikes)), 100);
	EXPECT_EQ(Row(WeaponSkill::Shatterpalm).count, 2);
	EXPECT_EQ(ShatterpalmFrozenPercent, 140);
	EXPECT_EQ(ShatterpalmBurstPercent, 25);
	EXPECT_EQ(Row(WeaponSkill::DervishAdvance).count, 3);
	EXPECT_EQ(Row(WeaponSkill::StormSiphon).count, 3);
	EXPECT_EQ(StormSiphonShockedPercent, 150);
	EXPECT_EQ(StormSiphonArcPercent, 25);
	EXPECT_EQ(Row(WeaponSkill::TempestSweep).count, 6);
	EXPECT_EQ(Row(WeaponSkill::TempestSweep).flags, static_cast<uint32_t>(TF_Stun | TF_Knockback));
	EXPECT_EQ(TempestGalePercent, 60);
	EXPECT_EQ(Row(WeaponSkill::PhantomStep).count, 3);
	EXPECT_EQ(Row(WeaponSkill::PhantomStep).radius, 4);
	EXPECT_EQ(Row(WeaponSkill::SkylordsStorm).zone, TableZone::Delayed);
	EXPECT_EQ(Row(WeaponSkill::SkylordsStorm).delay, 6);
	EXPECT_EQ(Row(WeaponSkill::SkylordsStorm).radius, 2);
	EXPECT_TRUE(Row(WeaponSkill::SkylordsStorm).description.starts_with("Blink back"));
	EXPECT_EQ(Row(WeaponSkill::PalmOfTheFrozenNorth).flags, static_cast<uint32_t>(TF_Pierce));
	EXPECT_EQ(PalmLancePercent, 60);
	// Shard of Winter was dropped (Q47).
	for (const TableSkillData &data : TableSkills())
		EXPECT_NE(data.name, "Shard of Winter");
}

TEST_F(Poe2StaffDraftsTest, StaffDraftsHaveTheWorkshopsTags)
{
	const auto has = [](WeaponSkill skill, uint32_t tag) { return (SkillTags(SkillSlot::ForAttack(skill)) & tag) == tag; };
	for (const DraftRow &draft : Drafts)
		EXPECT_TRUE(has(draft.skill, ST_Attack | ST_Melee | ST_Staff)) << Row(draft.skill).name;
	EXPECT_TRUE(has(WeaponSkill::WrathOfTheSkies, ST_Slam | ST_Lightning | ST_Area));
	EXPECT_TRUE(has(WeaponSkill::FrostveinRupture, ST_Slam | ST_Frost));
	EXPECT_TRUE(has(WeaponSkill::RimeStrikes, ST_Strike | ST_Frost));
	EXPECT_TRUE(has(WeaponSkill::NorthwindWave, ST_Frost | ST_Area));
	EXPECT_TRUE(has(WeaponSkill::ThunderFurrow, ST_Slam | ST_Lightning));
	EXPECT_TRUE(has(WeaponSkill::Shatterpalm, ST_Strike | ST_Frost | ST_Movement));
	EXPECT_TRUE(has(WeaponSkill::DervishAdvance, ST_Strike | ST_Movement | ST_Physical));
	EXPECT_TRUE(has(WeaponSkill::StormSiphon, ST_Strike | ST_Movement | ST_Lightning));
	EXPECT_TRUE(has(WeaponSkill::TempestSweep, ST_Strike | ST_Physical));
	EXPECT_TRUE(has(WeaponSkill::PhantomStep, ST_Strike | ST_Movement | ST_Physical));
	EXPECT_TRUE(has(WeaponSkill::SkylordsStorm, ST_Movement | ST_Lightning | ST_Area));
	EXPECT_FALSE(has(WeaponSkill::SkylordsStorm, ST_Duration));
	EXPECT_TRUE(has(WeaponSkill::PalmOfTheFrozenNorth, ST_Strike | ST_Frost));
	// No channelling.
	for (const DraftRow &draft : Drafts)
		EXPECT_FALSE(has(draft.skill, ST_Channelling)) << Row(draft.skill).name;
}

// The drafts' rule: no two fire, cold or lightning gems in one tier of a tab.
TEST_F(Poe2StaffDraftsTest, NoTwoGemsOfOneElementInAStaffTier)
{
	std::map<std::pair<int, uint32_t>, std::vector<std::string>> byTierAndElement;
	for (const GemEntry &gem : GemCatalog()) {
		if (gem.skill.kind != SkillKind::Attack || !GemInCategory(gem, GemCategory::Staff) || IsRetiredSkill(gem.skill))
			continue;
		const uint32_t tags = SkillTags(gem.skill);
		const uint32_t elements = tags & (ST_Fire | ST_Frost | ST_Lightning);
		// A gem of mixed elements is fine.
		if (elements == 0 || (elements & (elements - 1)) != 0)
			continue;
		byTierAndElement[{ GemTier(gem.level), elements }].push_back(GemName(gem));
	}
	for (const auto &[key, names] : byTierAndElement)
		EXPECT_EQ(names.size(), 1U) << "tier " << key.first << ": " << names.front() << " and " << names.back();
}

TEST_F(Poe2StaffDraftsTest, RimeStrikesThirdStrikeIsSlowerAndThrowsShards)
{
	Player &player = SetUpFight();
	EXPECT_TRUE(IsKnownSkillSpeedKey("RimeStrikes:finisher"));
	EXPECT_TRUE(IsKnownSkillSpeedKey("FistsOfThunder:finisher"));
	const SkillSlot slot = SkillSlot::ForAttack(WeaponSkill::RimeStrikes);
	EXPECT_GT(SkillUseTime(player, slot, false, SkillStep::Finisher), SkillUseTime(player, slot, false));
	PlaceMonster(0, { 41, 40 });
	for (int use = 1; use <= 2 * RimeStrikesShardEvery; use++) {
		Missiles.clear();
		EXPECT_EQ(RimeStrikesNextIsThird(player), use % RimeStrikesShardEvery == 0);
		Use(player, WeaponSkill::RimeStrikes, { 41, 40 });
		const std::vector<const Missile *> shards = MissilesOf(MissileID::BlueFlare);
		EXPECT_EQ(shards.size(), use % RimeStrikesShardEvery == 0 ? static_cast<size_t>(RimeStrikesShards) : 0U) << use;
		for (const Missile *shard : shards) {
			EXPECT_EQ(shard->poe2.damageType, static_cast<int8_t>(DamageType::Cold));
			// Four tiles at a flare's speed, not the whole screen.
			EXPECT_LE(shard->duration, 25);
		}
	}
	EXPECT_EQ(player.poe2.rimeCount, 0);
	Use(player, WeaponSkill::RimeStrikes, { 41, 40 });
	EXPECT_EQ(player.poe2.rimeCount, 1);
	for (int i = 0; i < RimeStrikesResetTicks; i++)
		player.poe2.tick();
	EXPECT_EQ(player.poe2.rimeCount, 0);
}

TEST_F(Poe2StaffDraftsTest, WrathOfTheSkiesSlamsAndFansThreeBolts)
{
	Player &player = SetUpFight();
	Monster &front = PlaceMonster(0, { 41, 40 });
	Monster &aimed = PlaceMonster(1, { 44, 40 });
	UseUntilHurt(player, WeaponSkill::WrathOfTheSkies, aimed.position.tile, front);
	EXPECT_TRUE(Hurt(front));
	// The slam is on the tile in front, not where it was aimed.
	EXPECT_FALSE(Hurt(aimed));
	const std::vector<const Missile *> bolts = MissilesOf(MissileID::ChargedBolt);
	ASSERT_EQ(bolts.size(), 3U);
	const int percent = GemDamagePercent(SkillSlot::ForAttack(WeaponSkill::WrathOfTheSkies), player.poe2.effectiveLevel(WeaponSkill::WrathOfTheSkies));
	for (const Missile *bolt : bolts) {
		EXPECT_EQ(bolt->poe2.damagePercent, percent * (55 * WrathBoltSharePercent / 100) / 100);
		EXPECT_GT(bolt->_midam, 0);
		EXPECT_EQ(Point(bolt->position.start), Point(41, 40));
		EXPECT_LE(bolt->duration, 40);
	}
}

// The drafts' weapon hits land as their element: a Northwind Wave hit chills, a physical hit doesn't.
TEST_F(Poe2StaffDraftsTest, DraftHitsLandAsTheirElement)
{
	Player &player = SetUpFight();
	Monster &monster = PlaceMonster(0, { 41, 40 });
	monster.maxHitPoints = monster.hitPoints = Fixed26_6::fromInt(200);
	ApplyPlayerAttackDamage(player, monster, Fixed26_6::fromInt(50), false);
	EXPECT_EQ(monster.poe2.chillTicks, 0);
	monster.hitPoints = monster.maxHitPoints;
	player.poe2.castSkill = WeaponSkill::NorthwindWave;
	ApplyPlayerAttackDamage(player, monster, Fixed26_6::fromInt(50), false);
	EXPECT_GT(monster.poe2.chillTicks, 0);
	// Not a ranged hit's.
	Monster &other = PlaceMonster(1, { 42, 40 });
	other.maxHitPoints = other.hitPoints = Fixed26_6::fromInt(200);
	ApplyPlayerAttackDamage(player, other, Fixed26_6::fromInt(50), false, true);
	EXPECT_EQ(other.poe2.chillTicks, 0);
	player.poe2.castSkill = WeaponSkill::None;
	// They don't also land a third again as it, as the older elemental attacks do.
	for (const DraftRow &draft : Drafts) {
		if (draft.type != DamageType::Physical && draft.skill != WeaponSkill::SkylordsStorm) {
			EXPECT_GT(Row(draft.skill).convertPercent, 0) << Row(draft.skill).name;
		}
	}
	EXPECT_EQ(Row(WeaponSkill::CycloneStrike).convertPercent, 0);
}

TEST_F(Poe2StaffDraftsTest, FrostveinSpikeShattersTheFrozen)
{
	Player &player = SetUpFight();
	int lineHits = 0;
	int cleared = 0;
	const auto cast = [&](uint32_t seed, bool frozen) {
		ClearMonsters();
		Monster &onLine = PlaceMonster(0, { 42, 40 });
		Monster &atEnd = PlaceMonster(1, { 45, 40 });
		Monster &beside = PlaceMonster(2, { 42, 41 });
		atEnd.poe2.freezeTicks = frozen ? 60 : 0;
		SetRndSeed(seed);
		Use(player, WeaponSkill::FrostveinRupture, { 46, 40 });
		lineHits += Hurt(onLine) ? 1 : 0;
		EXPECT_FALSE(Hurt(beside));
		cleared += frozen && atEnd.poe2.freezeTicks == 0 ? 1 : 0;
		return Lost(atEnd);
	};
	int64_t plain = 0;
	int64_t frozen = 0;
	for (uint32_t seed = 1; seed <= 12; seed++) {
		plain += cast(seed, false);
		frozen += cast(seed, true);
	}
	EXPECT_GT(lineHits, 0);
	EXPECT_GT(cleared, 0);
	// The line's 28% and the spike's 56%; frozen, the spike's 84%.
	EXPECT_GT(plain, 0);
	EXPECT_GT(frozen, plain * 23 / 20);
}

TEST_F(Poe2StaffDraftsTest, NorthwindWaveIsAChillingCone)
{
	Player &player = SetUpFight();
	// Along the dungeon's grid, 3, 3, 5 and 5 wide.
	EXPECT_EQ(NorthwindWaveTiles({ 40, 40 }, Direction::SouthEast, 4).size(), 16U);
	EXPECT_GE(NorthwindWaveTiles({ 40, 40 }, Direction::South, 4).size(), 15U);
	Monster &near = PlaceMonster(0, { 41, 40 });
	Monster &far = PlaceMonster(1, { 44, 42 });
	Monster &beyond = PlaceMonster(2, { 45, 40 });
	Monster &behind = PlaceMonster(3, { 39, 40 });
	for (uint32_t seed = 1; seed <= 8 && !(Hurt(near) && Hurt(far)); seed++) {
		SetRndSeed(seed);
		Use(player, WeaponSkill::NorthwindWave, { 44, 40 });
	}
	EXPECT_TRUE(Hurt(near));
	EXPECT_TRUE(Hurt(far));
	EXPECT_FALSE(Hurt(beyond));
	EXPECT_FALSE(Hurt(behind));
	EXPECT_GE(near.poe2.chillPercent, NorthwindChillPercent);
	EXPECT_GE(far.poe2.chillTicks, ChillTicks);
}

TEST_F(Poe2StaffDraftsTest, ThunderFurrowRunsEightTiles)
{
	Player &player = SetUpFight();
	Monster &eighth = PlaceMonster(0, { 48, 40 });
	Monster &ninth = PlaceMonster(1, { 49, 40 });
	UseUntilHurt(player, WeaponSkill::ThunderFurrow, { 49, 40 }, eighth);
	EXPECT_TRUE(Hurt(eighth));
	EXPECT_FALSE(Hurt(ninth));
}

TEST_F(Poe2StaffDraftsTest, ShatterpalmShattersTheFrozenIntoTheirNeighbours)
{
	Player &player = SetUpFight();
	for (const bool frozen : { false, true }) {
		ClearMonsters();
		Monster &target = PlaceMonster(0, { 41, 40 });
		Monster &neighbour = PlaceMonster(1, { 42, 40 });
		target.poe2.freezeTicks = frozen ? 60 : 0;
		UseUntilHurt(player, WeaponSkill::Shatterpalm, { 41, 40 }, target);
		EXPECT_TRUE(Hurt(target));
		EXPECT_EQ(target.poe2.freezeTicks, 0);
		// Two tiles from the hero: only the shatter reaches it.
		EXPECT_EQ(Hurt(neighbour), frozen);
	}
}

TEST_F(Poe2StaffDraftsTest, DervishAdvanceSpinsThreeTimesAlongItsWalk)
{
	Player &player = SetUpFight();
	EXPECT_TRUE(SpinsWhileStriking(WeaponSkill::DervishAdvance));
	player.position.tile = { 43, 40 };
	player.poe2.dashStart = Point(40, 40);
	Monster &onTheWay = PlaceMonster(0, { 41, 41 });
	Monster &atTheEnd = PlaceMonster(1, { 44, 41 });
	Monster &behind = PlaceMonster(2, { 38, 40 });
	for (uint32_t seed = 1; seed <= 8 && !(Hurt(onTheWay) && Hurt(atTheEnd)); seed++) {
		player.poe2.dashStart = Point(40, 40);
		SetRndSeed(seed);
		Use(player, WeaponSkill::DervishAdvance, { 45, 40 });
		EXPECT_EQ(player.poe2.dashStart, Point());
	}
	EXPECT_TRUE(Hurt(onTheWay));
	EXPECT_TRUE(Hurt(atTheEnd));
	EXPECT_FALSE(Hurt(behind));
	// With no walk, all three spins are where the hero stands: a monster beside takes all three, 3 x 55% against
	// Whirlwind's one 70% at the same speed.
	const auto standing = [&](WeaponSkill skill) {
		ClearMonsters();
		Monster &monster = PlaceMonster(0, { 44, 40 });
		int64_t lost = 0;
		for (uint32_t seed = 1; seed <= 16; seed++) {
			monster.hitPoints = monster.maxHitPoints;
			player.poe2.dashStart = {};
			SetRndSeed(seed);
			Use(player, skill, { 45, 40 });
			lost += Lost(monster);
		}
		return lost;
	};
	const int64_t whirlwind = standing(WeaponSkill::Whirlwind);
	EXPECT_GT(whirlwind, 0);
	EXPECT_GT(standing(WeaponSkill::DervishAdvance), whirlwind * 3 / 2);
}

TEST_F(Poe2StaffDraftsTest, StormSiphonUsesUpTheShockAndArcs)
{
	Player &player = SetUpFight();
	for (const bool shocked : { false, true }) {
		ClearMonsters();
		Monster &target = PlaceMonster(0, { 41, 40 });
		Monster &nearby = PlaceMonster(1, { 43, 41 });
		target.poe2.shockTicks = shocked ? 60 : 0;
		bool arced = false;
		for (uint32_t seed = 1; seed <= 8 && !arced; seed++) {
			target.poe2.shockTicks = shocked ? 60 : 0;
			SetRndSeed(seed);
			Use(player, WeaponSkill::StormSiphon, { 41, 40 });
			arced = Hurt(nearby);
		}
		EXPECT_TRUE(Hurt(target));
		EXPECT_EQ(arced, shocked);
		if (shocked) {
			EXPECT_EQ(target.poe2.shockTicks, 0);
		}
	}
}

TEST_F(Poe2StaffDraftsTest, TempestSweepDazesNearAndBlowsAGaleFar)
{
	Player &player = SetUpFight();
	Monster &swept = PlaceMonster(0, { 41, 40 });
	Monster &inTheGale = PlaceMonster(1, { 45, 41 });
	Monster &pastIt = PlaceMonster(2, { 48, 40 });
	bool dazed = false;
	for (uint32_t seed = 1; seed <= 8 && !dazed; seed++) {
		SetRndSeed(seed);
		Use(player, WeaponSkill::TempestSweep, { 46, 40 });
		dazed = swept.poe2.heavyStunTicks > 0;
	}
	EXPECT_TRUE(dazed);
	EXPECT_TRUE(Hurt(inTheGale));
	EXPECT_EQ(inTheGale.poe2.heavyStunTicks, 0);
	EXPECT_FALSE(Hurt(pastIt));
}

TEST_F(Poe2StaffDraftsTest, PhantomStepStrikesThreeAndEndsBesideTheLast)
{
	Player &player = SetUpFight();
	std::array<Monster *, 4> monsters { &PlaceMonster(0, { 42, 40 }), &PlaceMonster(1, { 40, 43 }), &PlaceMonster(2, { 37, 40 }), &PlaceMonster(3, { 45, 45 }) };
	for (uint32_t seed = 1; seed <= 8 && !(Hurt(*monsters[0]) && Hurt(*monsters[1]) && Hurt(*monsters[2])); seed++) {
		player.position.tile = player.position.future = player.position.old = { 40, 40 };
		SetRndSeed(seed);
		Use(player, WeaponSkill::PhantomStep, { 42, 40 });
	}
	for (size_t i = 0; i < 3; i++)
		EXPECT_TRUE(Hurt(*monsters[i])) << i;
	// The fourth is past the radius.
	EXPECT_FALSE(Hurt(*monsters[3]));
	EXPECT_EQ(player.position.tile, Point(38, 40));
	EXPECT_EQ(dPlayer[40][40], 0);
}

TEST_F(Poe2StaffDraftsTest, SkylordsStormBlinksBackAndStrikesWhereYouStood)
{
	Player &player = SetUpFight();
	player._pdir = Direction::SouthEast;
	Monster &monster = PlaceMonster(0, { 41, 40 });
	Use(player, WeaponSkill::SkylordsStorm, { 41, 40 });
	// South-east is +x: the blink goes three tiles the other way, with no leap.
	EXPECT_EQ(player.position.tile, Point(37, 40));
	ASSERT_EQ(SkillZoneCount(), 1U);
	EXPECT_EQ(SkillZoneCenter(0), std::optional<Point>(Point(40, 40)));
	EXPECT_FALSE(Hurt(monster));
	for (int tick = 0; tick < 20 && SkillZoneCount() > 0; tick++)
		ProcessSkillZones(player);
	// The burst always hits, as a zone's does.
	EXPECT_TRUE(Hurt(monster));
	EXPECT_EQ(SkillZoneCount(), 0U);
	// With a monster where it would land, it blinks as far as there is room.
	ClearSkillZones();
	ClearMonsters();
	player.position.tile = { 40, 40 };
	PlaceMonster(0, { 37, 40 });
	Use(player, WeaponSkill::SkylordsStorm, { 41, 40 });
	EXPECT_EQ(player.position.tile, Point(38, 40));
}

TEST_F(Poe2StaffDraftsTest, PalmOfTheFrozenNorthThrowsAPiercingLance)
{
	Player &player = SetUpFight();
	Monster &monster = PlaceMonster(0, { 41, 40 });
	UseUntilHurt(player, WeaponSkill::PalmOfTheFrozenNorth, { 41, 40 }, monster);
	EXPECT_TRUE(Hurt(monster));
	const std::vector<const Missile *> lances = MissilesOf(MissileID::BlueFlare);
	ASSERT_EQ(lances.size(), 1U);
	const Missile &lance = *lances.front();
	const int percent = GemDamagePercent(SkillSlot::ForAttack(WeaponSkill::PalmOfTheFrozenNorth), player.poe2.effectiveLevel(WeaponSkill::PalmOfTheFrozenNorth));
	EXPECT_EQ(lance.poe2.damagePercent, percent * (275 * PalmLancePercent / 100) / 100);
	EXPECT_EQ(lance.poe2.damageType, static_cast<int8_t>(DamageType::Cold));
	EXPECT_GE(lance.poe2.piercesLeft, 3);
	EXPECT_LE(lance.duration, 25);
}

} // namespace
} // namespace devilution
