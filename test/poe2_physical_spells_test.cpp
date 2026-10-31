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
	EXPECT_GT(vfx::ParticleCount(), 0U);
	EXPECT_GT(vfx::ShapeCount(), 0U);
	for (int i = 0; i < 200; i++)
		vfx::Advance(50);
	EXPECT_EQ(vfx::ParticleCount(), 0U);
	EXPECT_EQ(vfx::ShapeCount(), 0U);
	vfx::Clear();
	HeadlessMode = true;
}

} // namespace
} // namespace devilution
