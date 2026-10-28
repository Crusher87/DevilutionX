/**
 * @file poe2_granted_skills_test.cpp
 *
 * The caster weapons rework's granted skills (caster-weapons-plan-v2.md 6.6, 6.7 and 6.12, step S14): a staff or wand
 * that grants a table skill casts it with a left click, its hold-to-repeat, the controller's attack and the skill bar,
 * at the weapon's grant level, taking the default attack row's supports. The first one is the Gelid Staff's Freezing
 * Shards.
 */
#include <algorithm>
#include <string_view>

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
#include "tables/playerdat.hpp"
#include "track.h"

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
};

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
	Hold(player, "Voltaic Staff");
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
