#include <gtest/gtest.h>

#include <algorithm>
#include <array>
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

/** @brief The physical spells batch (research/physical-spells): bone and blood spells and their supports. */
class Poe2PhysicalSpellsTest : public ::testing::Test {
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
		player._pClass = HeroClass::Sorcerer;
		player.setCharacterLevel(30);
		player._pBaseStr = player._pStrength = 100;
		player._pBaseMag = player._pMagic = 200;
		player._pBaseDex = player._pDexterity = 100;
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
		for (int x = 30; x < 55; x++) {
			for (int y = 30; y < 55; y++) {
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
};

// Physical spells no longer draw as Blessed Hammer's holy gold: bone spells are bone, blood spells blood.
TEST_F(Poe2PhysicalSpellsTest, PhysicalSpellsLookLikeBoneOrBlood)
{
	EXPECT_EQ(vfx::ElementOf(DamageType::Physical, true), vfx::Element::Bone);
	EXPECT_EQ(vfx::ElementOf(DamageType::Physical, false), vfx::Element::Physical);
	EXPECT_EQ(TableSkillLook(Row(WeaponSkill::BlessedHammer)), vfx::Element::Holy);
	for (const WeaponSkill skill : { WeaponSkill::Reap, WeaponSkill::Exsanguinate, WeaponSkill::PowerSiphon, WeaponSkill::TormentBolt })
		EXPECT_EQ(TableSkillLook(Row(skill)), vfx::Element::Blood) << Row(skill).name;
	for (const WeaponSkill skill : { WeaponSkill::BoneBolt, WeaponSkill::FeastOfFlesh, WeaponSkill::TwinBolts, WeaponSkill::CriticalBolt })
		EXPECT_EQ(TableSkillLook(Row(skill)), vfx::Element::Bone) << Row(skill).name;
	// Attacks and the elements keep theirs.
	EXPECT_EQ(TableSkillLook(Row(WeaponSkill::Fissure)), vfx::Element::Physical);
	EXPECT_EQ(TableSkillLook(Row(WeaponSkill::GlacialSpike)), vfx::Element::Cold);
	EXPECT_EQ(TableSkillLook(Row(WeaponSkill::HexRupture)), vfx::Element::Chaos);
}

TEST_F(Poe2PhysicalSpellsTest, ABoneBoltOnAFlareIsDrawnAsBone)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	player.poe2.granted[0] = { SkillSlot::ForAttack(WeaponSkill::BoneBolt), 1, 0 };
	ExecuteTableSkill(player, Row(WeaponSkill::BoneBolt), { 46, 40 });
	ASSERT_FALSE(Missiles.empty());
	const Missile &bolt = Missiles.back();
	EXPECT_EQ(bolt._mitype, MissileID::OrangeFlare);
	EXPECT_EQ(bolt.poe2.vfxElement, static_cast<int8_t>(vfx::Element::Bone));
	EXPECT_EQ(bolt.poe2.damageType, static_cast<int8_t>(DamageType::Physical));
	// A blood bolt is blood.
	Missiles.clear();
	player.poe2.granted[0] = { SkillSlot::ForAttack(WeaponSkill::TormentBolt), 1, 0 };
	ExecuteTableSkill(player, Row(WeaponSkill::TormentBolt), { 46, 40 });
	ASSERT_FALSE(Missiles.empty());
	EXPECT_EQ(Missiles.back().poe2.vfxElement, static_cast<int8_t>(vfx::Element::Blood));
	// An elemental bolt keeps its graphic's look.
	Missiles.clear();
	player.poe2.granted[0] = { SkillSlot::ForAttack(WeaponSkill::FrostBolt), 1, 0 };
	ExecuteTableSkill(player, Row(WeaponSkill::FrostBolt), { 46, 40 });
	ASSERT_FALSE(Missiles.empty());
	EXPECT_EQ(Missiles.back().poe2.vfxElement, -1);
	player.poe2.granted = {};
}

TEST_F(Poe2PhysicalSpellsTest, BoneHitsAreDrawnAsBone)
{
	AddVisualEffect({ 40, 40 }, MissileGraphicID::BoneHit);
	AddVisualEffect({ 41, 40 }, MissileGraphicID::StoneCurseShatter);
	ASSERT_EQ(Missiles.size(), 2U);
	EXPECT_EQ(Missiles.front()._miAnimType, MissileGraphicID::StoneCurseShatter);
	EXPECT_EQ(Missiles.front().poe2.vfxElement, static_cast<int8_t>(vfx::Element::Bone));
	EXPECT_EQ(Missiles.back().poe2.vfxElement, -1);
}

TEST_F(Poe2PhysicalSpellsTest, BoneEffectsSprayAndDieOut)
{
	HeadlessMode = false;
	vfx::Clear();
	vfx::Impact({ 40, 40 }, vfx::Element::Bone, 1.2F);
	vfx::Slam({ 42, 40 }, 2, vfx::Element::Bone);
	vfx::Fissure({ 40, 42 }, { 45, 42 }, vfx::Element::Bone);
	vfx::BoneSpikes({ 44, 44 }, 100, 1);
	vfx::BoneCage({ 46, 46 }, 150, 1, 3000);
	EXPECT_GT(vfx::ParticleCount(), 0U);
	EXPECT_GT(vfx::ShapeCount(), 0U);
	for (int i = 0; i < 200; i++)
		vfx::Advance(50);
	EXPECT_EQ(vfx::ParticleCount(), 0U);
	EXPECT_EQ(vfx::ShapeCount(), 0U);
	vfx::Clear();
	HeadlessMode = true;
}

// Bone Prison (POE2's Bone Cage): the cage bursts up under the target a moment later, hits the enemies there and traps
// the non-unique survivors for its duration.
TEST_F(Poe2PhysicalSpellsTest, BonePrisonHitsAndTraps)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	const TableSkillData &data = Row(WeaponSkill::BonePrison);
	EXPECT_EQ(data.level, 5);
	EXPECT_EQ(data.shape, TableShape::Zone);
	EXPECT_EQ(data.zone, TableZone::IceSpike);
	EXPECT_EQ(data.curse, TableCurse::None);
	EXPECT_EQ(data.type, DamageType::Physical);
	EXPECT_EQ(data.damagePercent, 200);
	EXPECT_EQ(data.duration, 60);
	EXPECT_NE(data.flags & TF_Pin, 0U);
	const SkillSlot prison = SkillSlot::ForAttack(WeaponSkill::BonePrison);
	EXPECT_NE(SkillTags(prison) & ST_Physical, 0U);
	EXPECT_NE(SkillTags(prison) & ST_Damage, 0U);
	EXPECT_NE(SkillTags(prison) & ST_Area, 0U);
	EXPECT_EQ(SkillTags(prison) & ST_Curse, 0U);
	Item gem;
	ASSERT_TRUE(CreateSkillGemItem(gem, prison, 1));
	EXPECT_EQ(SkillForGem(gem), prison);
	// It drops from tier 5 now, with tier 5's Magic and price.
	const auto row = std::find_if(AllItemsList.begin(), AllItemsList.end(), [](const ItemData &item) { return item.iName == std::string_view("Bone Prison Gem"); });
	ASSERT_NE(row, AllItemsList.end());
	EXPECT_EQ(row->iMinMLvl, 5);
	EXPECT_EQ(row->iMinMag, 33);
	EXPECT_EQ(row->iValue, 1100);

	Supported(prison);
	const Point target { 45, 45 };
	Monster &inside = PlaceMonster(0, target);
	Monster &beside = PlaceMonster(1, { 46, 45 });
	Monster &outside = PlaceMonster(2, { 49, 45 });
	Monster &boss = PlaceMonster(3, { 44, 45 });
	boss.uniqueType = UniqueMonsterType::Garbud;
	ExecuteTableSkill(player, data, target);
	ASSERT_EQ(SkillZoneCount(), 1U);
	// Nothing until the bones come up.
	ProcessSkillZones(player);
	EXPECT_EQ(inside.hitPoints, inside.maxHitPoints);
	for (int i = 0; i < 10 && SkillZoneCount() > 0; i++)
		ProcessSkillZones(player);
	EXPECT_EQ(SkillZoneCount(), 0U);
	for (const Monster *monster : { &inside, &beside, &boss })
		EXPECT_LT(monster->hitPoints, monster->maxHitPoints);
	EXPECT_EQ(outside.hitPoints, outside.maxHitPoints);
	EXPECT_EQ(inside.poe2.heavyStunTicks, 60);
	EXPECT_EQ(beside.poe2.heavyStunTicks, 60);
	EXPECT_EQ(inside.poe2.stunBuildup, 0);
	// A unique only takes the damage.
	EXPECT_EQ(boss.poe2.heavyStunTicks, 0);
	EXPECT_EQ(outside.poe2.heavyStunTicks, 0);
}

// Bone Spikes (POE2's Unearth, its damage half): bones burst up in a line ahead, hitting and staggering what's on it once.
TEST_F(Poe2PhysicalSpellsTest, BoneSpikesBurstAlongALine)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	const TableSkillData &data = Row(WeaponSkill::BoneSpikes);
	EXPECT_EQ(data.level, 3);
	EXPECT_EQ(data.mana, 8);
	EXPECT_EQ(data.shape, TableShape::Line);
	EXPECT_EQ(data.count, 5);
	EXPECT_EQ(data.radius, 1);
	EXPECT_EQ(data.type, DamageType::Physical);
	EXPECT_EQ(data.curve, TableCurve::Fireball);
	EXPECT_EQ(data.curveEnd, 312);
	EXPECT_NE(data.flags & TF_Spell, 0U);
	EXPECT_NE(data.flags & TF_Stun, 0U);
	EXPECT_FALSE(IsGrantedSkill(WeaponSkill::BoneSpikes));
	EXPECT_EQ(TableSkillLook(data), vfx::Element::Bone);
	const SkillSlot spikes = SkillSlot::ForAttack(WeaponSkill::BoneSpikes);
	EXPECT_NE(SkillTags(spikes) & ST_Spell, 0U);
	EXPECT_NE(SkillTags(spikes) & ST_Physical, 0U);
	EXPECT_NE(SkillTags(spikes) & ST_Area, 0U);
	Item gem;
	ASSERT_TRUE(CreateSkillGemItem(gem, spikes, 1));
	EXPECT_EQ(SkillForGem(gem), spikes);

	Supported(spikes);
	Monster &near = PlaceMonster(0, { 41, 40 });
	Monster &far = PlaceMonster(1, { 45, 40 });
	Monster &beside = PlaceMonster(2, { 43, 41 });
	Monster &past = PlaceMonster(3, { 47, 40 });
	Monster &behind = PlaceMonster(4, { 38, 40 });
	ExecuteTableSkill(player, data, { 46, 40 });
	for (const Monster *monster : { &near, &far, &beside }) {
		EXPECT_LT(monster->hitPoints, monster->maxHitPoints);
		EXPECT_EQ(monster->maxHitPoints - monster->hitPoints, near.maxHitPoints - near.hitPoints);
		EXPECT_EQ(monster->poe2.heavyStunTicks, HeavyStunTicks);
	}
	for (const Monster *monster : { &past, &behind }) {
		EXPECT_EQ(monster->hitPoints, monster->maxHitPoints);
		EXPECT_EQ(monster->poe2.heavyStunTicks, 0);
	}
}

// Bone Spear (POE2's Bonestorm, one heavy spear): a big bone shard that runs through every enemy in its path, building
// up twice the stun, and bursting in splinters on each.
TEST_F(Poe2PhysicalSpellsTest, BoneSpearRunsThroughEveryEnemy)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	const TableSkillData &data = Row(WeaponSkill::BoneSpear);
	EXPECT_EQ(data.level, 7);
	EXPECT_EQ(data.mana, 12);
	EXPECT_EQ(data.shape, TableShape::Projectiles);
	EXPECT_EQ(data.count, 1);
	EXPECT_EQ(data.damagePercent, 180);
	EXPECT_EQ(data.type, DamageType::Physical);
	EXPECT_EQ(data.rider, GrantedRider::StunBuildup);
	EXPECT_NE(data.flags & TF_Pierce, 0U);
	EXPECT_FALSE(IsGrantedSkill(WeaponSkill::BoneSpear));
	const SkillSlot spear = SkillSlot::ForAttack(WeaponSkill::BoneSpear);
	EXPECT_NE(SkillTags(spear) & ST_Spell, 0U);
	EXPECT_NE(SkillTags(spear) & ST_Projectile, 0U);
	EXPECT_NE(SkillTags(spear) & ST_Physical, 0U);
	Item gem;
	ASSERT_TRUE(CreateSkillGemItem(gem, spear, 1));
	EXPECT_EQ(SkillForGem(gem), spear);

	Supported(spear);
	ExecuteTableSkill(player, data, { 46, 40 });
	ASSERT_FALSE(Missiles.empty());
	Missile &missile = Missiles.back();
	EXPECT_EQ(missile._mitype, MissileID::OrangeFlare);
	EXPECT_GE(missile.poe2.piercesLeft, BoneSpearPierces);
	EXPECT_EQ(missile.poe2.look, 2);
	EXPECT_EQ(missile.poe2.vfxElement, static_cast<int8_t>(vfx::Element::Bone));
	EXPECT_EQ(MissileSkill(missile), spear);

	// Its hits build up twice the stun, as Bone Bolt's do.
	{
		const SupportHitScope scope(player, spear);
		EXPECT_EQ(HitGrantedRider(), GrantedRider::StunBuildup);
		EXPECT_EQ(UniqueStunBuildupPercent(player), 200);
	}
	{
		const SupportHitScope scope(player, SkillSlot::ForAttack(WeaponSkill::BoneSpikes));
		EXPECT_EQ(UniqueStunBuildupPercent(player), 100);
	}

	// Each enemy it runs through bursts in bone.
	Monster &first = PlaceMonster(0, { 42, 40 });
	const size_t before = Missiles.size();
	OnTableMissileHit(missile, player, first, 40, false);
	ASSERT_GT(Missiles.size(), before);
	EXPECT_EQ(Missiles.back().poe2.vfxElement, static_cast<int8_t>(vfx::Element::Bone));
}

// Blood Nova (Bone Cage's ring with Exsanguinate's blood): a ring of blood bursts out around the hero, hitting and
// bleeding everything within 2 tiles, paid for with 5% of current life.
TEST_F(Poe2PhysicalSpellsTest, BloodNovaBleedsTheRingForLife)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	const TableSkillData &data = Row(WeaponSkill::BloodNova);
	EXPECT_EQ(data.level, 9);
	EXPECT_EQ(data.mana, 10);
	EXPECT_EQ(data.shape, TableShape::Around);
	EXPECT_EQ(data.radius, 2);
	EXPECT_EQ(data.damagePercent, 120);
	EXPECT_EQ(data.type, DamageType::Physical);
	EXPECT_NE(data.flags & TF_LifeCost, 0U);
	EXPECT_NE(data.flags & TF_Bleed, 0U);
	EXPECT_EQ(TableSkillLook(data), vfx::Element::Blood);
	EXPECT_FALSE(IsGrantedSkill(WeaponSkill::BloodNova));
	const SkillSlot nova = SkillSlot::ForAttack(WeaponSkill::BloodNova);
	EXPECT_NE(SkillTags(nova) & ST_Spell, 0U);
	EXPECT_NE(SkillTags(nova) & ST_Area, 0U);
	EXPECT_NE(SkillTags(nova) & ST_Physical, 0U);
	EXPECT_NE(SkillTags(nova) & ST_DamageOverTime, 0U);
	Item gem;
	ASSERT_TRUE(CreateSkillGemItem(gem, nova, 1));
	EXPECT_EQ(SkillForGem(gem), nova);

	// It costs 5% of current life.
	Supported(nova);
	const Fixed26_6 life = player._pHitPoints;
	ASSERT_TRUE(StartTableSkill(player, data));
	EXPECT_EQ(player._pHitPoints, life - Fixed26_6::fromRaw(life.raw() / 20));

	Monster &near = PlaceMonster(0, { 41, 40 });
	Monster &ring = PlaceMonster(1, { 42, 42 });
	Monster &far = PlaceMonster(2, { 44, 40 });
	ExecuteTableSkill(player, data, { 44, 40 });
	for (Monster *monster : { &near, &ring }) {
		const Fixed26_6 hit = monster->maxHitPoints - monster->hitPoints;
		EXPECT_GT(hit, 0);
		EXPECT_EQ(monster->poe2.bleedTicks, GrantedBleedTicks);
		// The bleed is 75% of the hit over its 5 seconds.
		const int64_t bled = static_cast<int64_t>(monster->poe2.bleedDamagePerTick) * GrantedBleedTicks;
		EXPECT_NEAR(static_cast<double>(bled), static_cast<double>(hit.raw()) * GrantedBleedPercent / 100, hit.raw() * 0.05 + GrantedBleedTicks);
	}
	EXPECT_EQ(far.hitPoints, far.maxHitPoints);
	EXPECT_EQ(far.poe2.bleedTicks, 0);
}

// Open Wounds and Crack Armor fit physical spells now, not only attacks.
TEST_F(Poe2PhysicalSpellsTest, OpenWoundsAndCrackArmorWorkForSpells)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	const SkillSlot spikes = SkillSlot::ForAttack(WeaponSkill::BoneSpikes);
	for (const SupportGem gem : { SupportGem::SerratedEdge, SupportGem::SunderArmour }) {
		for (const WeaponSkill skill : { WeaponSkill::BoneSpikes, WeaponSkill::BoneSpear, WeaponSkill::BloodNova, WeaponSkill::BonePrison, WeaponSkill::Cleave })
			EXPECT_TRUE(SupportGemCanSupport(gem, SkillSlot::ForAttack(skill))) << SupportGemName(gem) << " " << Row(skill).name;
		EXPECT_FALSE(SupportGemCanSupport(gem, SkillSlot::ForSpell(SpellID::Fireball))) << SupportGemName(gem);
		EXPECT_FALSE(SupportGemCanSupport(gem, SkillSlot::ForAttack(WeaponSkill::GlacialSpike))) << SupportGemName(gem);
	}

	// Open Wounds III always bleeds.
	Supported(spikes, SupportGem::SerratedEdge, 3);
	Monster &cut = PlaceMonster(0, { 41, 40 });
	{
		const SupportHitScope scope(player, spikes);
		ExecuteTableSkill(player, Row(WeaponSkill::BoneSpikes), { 46, 40 });
	}
	EXPECT_GT(cut.poe2.bleedTicks, 0);
	ClearMonsters();

	Supported(spikes, SupportGem::SunderArmour);
	Monster &cracked = PlaceMonster(0, { 41, 40 });
	{
		const SupportHitScope scope(player, spikes);
		ExecuteTableSkill(player, Row(WeaponSkill::BoneSpikes), { 46, 40 });
	}
	EXPECT_GT(cracked.poe2.armourBreakTicks, 0);
}

// Might (POE2's Brutality): 30% more physical damage, and none of any other type.
TEST_F(Poe2PhysicalSpellsTest, MightIsPhysicalOnly)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	const SupportData *data = FindSupportData(SupportGem::Might);
	ASSERT_NE(data, nullptr);
	EXPECT_EQ(data->name, "Might");
	EXPECT_EQ(SupportTierGemLevel(SupportGem::Might, 1, 0), 3);
	const SkillSlot spikes = SkillSlot::ForAttack(WeaponSkill::BoneSpikes);
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::Might, spikes));
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::Might, SkillSlot::ForAttack(WeaponSkill::Cleave)));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::Might, SkillSlot::ForSpell(SpellID::Fireball)));
	for (const SupportGem other : { SupportGem::InfusedFlames, SupportGem::InfusedStorm, SupportGem::InfusedFrost, SupportGem::HolyInfusion, SupportGem::DarkEnchant, SupportGem::BloodSacrifice })
		EXPECT_TRUE(SupportsConflict(SupportGem::Might, other)) << SupportGemName(other);
	EXPECT_FALSE(SupportsConflict(SupportGem::Might, SupportGem::SerratedEdge));

	Supported(spikes, SupportGem::Might);
	Monster &hit = PlaceMonster(0, { 45, 45 });
	{
		const SupportHitScope scope(player, spikes);
		EXPECT_EQ(SupportHitDamagePercent(hit, DamageType::Physical), 130);
		EXPECT_EQ(SupportHitDamagePercent(hit, DamageType::Fire), 0);
		EXPECT_EQ(SupportHitDamagePercent(hit, DamageType::Chaos), 0);
	}
	// What a support adds on top is gone too: Fire Enchant's fire never lands.
	player.poe2.slotSupports[0][1] = SupportGem::InfusedFlames;
	player.poe2.slotSupportTiers[0][1] = 1;
	{
		const SupportHitScope scope(player, spikes);
		NoteSupportedDamage(hit, DamageType::Physical, Fixed26_6::fromInt(50));
	}
	EXPECT_EQ(hit.hitPoints, hit.maxHitPoints);
	Supported(spikes, SupportGem::InfusedFlames);
	Monster &burnt = PlaceMonster(1, { 46, 45 });
	{
		const SupportHitScope scope(player, spikes);
		NoteSupportedDamage(burnt, DamageType::Physical, Fixed26_6::fromInt(50));
	}
	EXPECT_LT(burnt.hitPoints, burnt.maxHitPoints);

	// A cast hits 30% harder.
	ClearMonsters();
	Supported(spikes);
	Monster &plain = PlaceMonster(0, { 41, 40 });
	SetRndSeed(7);
	{
		const SupportHitScope scope(player, spikes);
		ExecuteTableSkill(player, Row(WeaponSkill::BoneSpikes), { 46, 40 });
	}
	const int64_t base = (plain.maxHitPoints - plain.hitPoints).raw();
	ClearMonsters();
	Supported(spikes, SupportGem::Might);
	Monster &mighty = PlaceMonster(0, { 41, 40 });
	SetRndSeed(7);
	{
		const SupportHitScope scope(player, spikes);
		ExecuteTableSkill(player, Row(WeaponSkill::BoneSpikes), { 46, 40 });
	}
	const int64_t more = (mighty.maxHitPoints - mighty.hitPoints).raw();
	ASSERT_GT(base, 0);
	EXPECT_NEAR(static_cast<double>(more), static_cast<double>(base) * 1.3, static_cast<double>(base) * 0.02 + 2);
}

// Bone Splinters (POE2's Bone Shrapnel): a monster killed while trapped or heavily stunned bursts for 25% of its life.
TEST_F(Poe2PhysicalSpellsTest, BoneSplintersBurstTrappedKills)
{
	Player &player = *MyPlayer;
	EnterDungeon();
	const SupportData *data = FindSupportData(SupportGem::BoneSplinters);
	ASSERT_NE(data, nullptr);
	EXPECT_EQ(data->name, "Bone Splinters");
	EXPECT_EQ(SupportTierGemLevel(SupportGem::BoneSplinters, 1, 0), 6);
	const SkillSlot spikes = SkillSlot::ForAttack(WeaponSkill::BoneSpikes);
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::BoneSplinters, spikes));
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::BoneSplinters, SkillSlot::ForSpell(SpellID::Fireball)));
	Supported(spikes, SupportGem::BoneSplinters);

	const auto killedBy = [&](Monster &monster, SkillSlot skill) {
		monster.poe2.lastHitPlayer = 0;
		monster.poe2.lastHitSkillKind = static_cast<uint8_t>(skill.kind);
		monster.poe2.lastHitSkillId = skill.id;
		monster.hitPoints = Fixed26_6::fromInt(0);
		monster.mode = MonsterMode::Death;
		OnSupportedKill(monster);
		for (int i = 0; i < 3; i++)
			ProcessSupportEffects(player);
	};
	// Trapped: it bursts on the monsters around it, not further.
	Monster &trapped = PlaceMonster(0, { 45, 45 });
	trapped.maxHitPoints = Fixed26_6::fromInt(1000);
	trapped.poe2.heavyStunTicks = 30;
	Monster &beside = PlaceMonster(1, { 46, 45 });
	Monster &far = PlaceMonster(2, { 49, 45 });
	ASSERT_EQ(MonsterDamageTakenPercent(beside, DamageType::Physical), 100);
	killedBy(trapped, spikes);
	EXPECT_EQ(beside.hitPoints, beside.maxHitPoints - Fixed26_6::fromInt(250));
	EXPECT_EQ(far.hitPoints, far.maxHitPoints);

	// Not trapped, or killed by another skill: nothing.
	beside.hitPoints = beside.maxHitPoints;
	Monster &free = PlaceMonster(3, { 45, 44 });
	free.maxHitPoints = Fixed26_6::fromInt(1000);
	killedBy(free, spikes);
	Monster &other = PlaceMonster(4, { 44, 45 });
	other.maxHitPoints = Fixed26_6::fromInt(1000);
	other.poe2.heavyStunTicks = 30;
	killedBy(other, SkillSlot::ForAttack(WeaponSkill::BoneSpear));
	EXPECT_EQ(beside.hitPoints, beside.maxHitPoints);
}

TEST_F(Poe2PhysicalSpellsTest, PhysicalSupportsHaveItems)
{
	for (const SupportGem gem : { SupportGem::Might, SupportGem::BoneSplinters }) {
		const SupportData *data = FindSupportData(gem);
		ASSERT_NE(data, nullptr);
		EXPECT_GT(static_cast<int>(gem), static_cast<int>(SupportGem::BloodSacrifice));
		Item item;
		ASSERT_TRUE(CreateSupportGemItem(item, gem)) << data->name;
		EXPECT_EQ(SupportGemForItem(item), gem) << data->name;
		EXPECT_FALSE(IsLineageSupport(gem)) << data->name;
	}
}

} // namespace
} // namespace devilution
