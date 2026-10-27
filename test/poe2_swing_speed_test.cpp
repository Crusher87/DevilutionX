#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/lighting_defs.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/dun_tile_data.hpp"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/attack_speed.hpp"
#include "poe2/default_attack.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/player_data.hpp"
#include "poe2/skill_details.hpp"
#include "poe2/skill_resources.hpp"
#include "poe2/support_table.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"
#include "tables/spelldat.h"

namespace devilution {

extern void TestPlayerProcessAction(Player &player);

namespace {

/** @brief The ticks on which a held attack started a swing, how many backhands Double Swing added and how many hit frames came. */
struct HeldSwings {
	std::vector<int> starts;
	int backhands = 0;
	int hits = 0;

	/** @brief Ticks from one swing to the next, if they're all the same; 0 otherwise. */
	[[nodiscard]] int cycle() const
	{
		if (starts.size() < 3)
			return 0;
		const int first = starts[1] - starts[0];
		for (size_t i = 2; i < starts.size(); i++) {
			if (starts[i] - starts[i - 1] != first)
				return 0;
		}
		return first;
	}

	/** @brief Average game ticks from one swing to the next. */
	[[nodiscard]] double averageCycle() const
	{
		if (starts.size() < 2)
			return 0;
		return static_cast<double>(starts.back() - starts.front()) / static_cast<double>(starts.size() - 1);
	}
};

class Poe2SwingSpeedTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayerId = 0;
		MyPlayer = &Players[0];
		*MyPlayer = {};
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

		// A Warrior with a sword: 16 frames, hitting on the ninth.
		Player &player = *MyPlayer;
		player._pClass = HeroClass::Warrior;
		player._pgfxnum = static_cast<uint8_t>(PlayerWeaponGraphic::Sword);
		const PlayerAnimData &data = GetPlayerAnimDataForClass(HeroClass::Warrior);
		player._pAFrames = data.swordFrames;
		player._pAFNum = data.swordActionFrame;
		player._pNFrames = data.idleFrames;
		player.lightId = NO_LIGHT;
		player._pHitPoints = Fixed26_6::fromInt(100);
		player._pHPBase = player._pHitPoints;
		player._pMaxHP = player._pHitPoints;
		player._pMana = Fixed26_6::fromInt(1000);
		player._pManaBase = player._pMana;
		player.position.tile = { 20, 20 };
		player.position.future = player.position.tile;
		player.position.old = player.position.tile;
		player._pdir = Direction::South;
		player._pmode = PM_STAND;
		player.destAction = ACTION_NONE;
		ClrPlrPath(player);
	}

	void TearDown() override
	{
		dMonster[Front().x][Front().y] = 0;
		Missiles.clear();
		leveltype = DTYPE_TOWN;
		gbIsPoe2 = false;
		LoadModifierData();
		LoadSkillSpeedData();
		UnloadModArchives();
		LoadMissileData();
	}

	static _item_indexes FindBase(std::string_view name)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == name)
				return static_cast<_item_indexes>(i);
		}
		ADD_FAILURE() << name;
		return IDI_GOLD;
	}

	/** @brief Puts a normal item of the base in the hero's hand and works out the hero's stats and animations again. */
	static void Equip(Player &player, std::string_view base)
	{
		Item &hand = player.InvBody[INVLOC_HAND_LEFT];
		hand = {};
		SetLootRarity(hand, ITEM_QUALITY_NORMAL);
		SetRndSeed(1);
		SetupAllItems(player, hand, FindBase(base), AdvanceRndSeed(), 30, 1, /*onlygood=*/false, /*pregen=*/false, 0, /*forceNotUnique=*/true);
		hand._iStatFlag = true;
		// Enough of each attribute to use any base.
		player._pBaseStr = player._pStrength = 300;
		player._pBaseMag = player._pMagic = 300;
		player._pBaseDex = player._pDexterity = 300;
		player.setCharacterLevel(80);
		CalcPlrInv(player, false);
		SetPlrAnims(player);
		// Skills cost mana, which the hero's stats just worked out.
		player._pMana = Fixed26_6::fromInt(100000);
		player._pManaBase = player._pMana;
		player._pmode = PM_STAND;
		player.destAction = ACTION_NONE;
		player.poe2.actionTimeRemainder = 0;
	}

	static Point Front()
	{
		return Point { 20, 20 } + Direction::South;
	}

	/**
	 * @brief Holds the attack button for a number of game ticks: as RepeatPlayerAction does, the next attack is queued
	 * as soon as the hero can change action, then the hero's action runs as ProcessPlayers runs it.
	 * @param targetStands Whether a monster stands in front through the follow-through, for Double Swing's backhand.
	 * It is away on the hit frames, which would need the game's monster data.
	 * @param ranged Shoots (StartRangeAttack, PM_RATTACK) instead of swinging.
	 */
	static HeldSwings HoldAttack(Player &player, WeaponSkill skill, int ticks, bool targetStands, bool ranged = false)
	{
		const PLR_MODE attackMode = ranged ? PM_RATTACK : PM_ATTACK;
		HeldSwings swings;
		const Point front = Front();
		Monster &monster = Monsters[0];
		monster.position.tile = front;
		monster.position.old = front;
		monster.position.future = front;
		monster.hitPoints = Fixed26_6::fromInt(100);
		monster.goal = MonsterGoal::Normal;
		for (int tick = 0; tick < ticks; tick++) {
			if (player.destAction == ACTION_NONE && player.CanChangeAction()) {
				player.poe2.pendingSkill = skill;
				player.poe2.pendingSkillTicks = 40;
				player.destAction = ranged ? ACTION_RATTACK : ACTION_ATTACK;
				player.destParam1 = front.x;
				player.destParam2 = front.y;
			}
			const bool followThrough = player._pmode == PM_ATTACK && player.poe2.flurryStrike == 0 && player.AnimInfo.currentFrame >= player._pAFNum;
			dMonster[front.x][front.y] = targetStands && followThrough ? 1 : 0;

			const PLR_MODE modeBefore = player._pmode;
			const int frameBefore = player.AnimInfo.currentFrame;
			const uint8_t strikeBefore = player.poe2.flurryStrike;
			TestPlayerProcessAction(player);
			// DoAttack hits on this frame next tick.
			if (player._pmode == attackMode && player.AnimInfo.currentFrame == player._pAFNum - 1)
				swings.hits++;
			// Within a swing the frame goes up by one each tick; a new swing starts it again.
			if (player._pmode != attackMode || (modeBefore == attackMode && player.AnimInfo.currentFrame > frameBefore))
				continue;
			if (strikeBefore == 0 && player.poe2.flurryStrike == 1)
				swings.backhands++;
			else
				swings.starts.push_back(tick);
		}
		dMonster[front.x][front.y] = 0;
		return swings;
	}

	/** @brief Holds a spell's button for a number of game ticks, as HoldAttack holds an attack: the ticks each cast started on. */
	static HeldSwings HoldCast(Player &player, SpellID spell, int ticks)
	{
		HeldSwings casts;
		const Point target = Front() + Direction::South + Direction::South;
		for (int tick = 0; tick < ticks; tick++) {
			if (player.destAction == ACTION_NONE && player.CanChangeAction()) {
				player.queuedSpell.spellId = spell;
				player.queuedSpell.spellType = SpellType::Spell;
				player.destAction = ACTION_SPELL;
				player.destParam1 = target.x;
				player.destParam2 = target.y;
			}
			const PLR_MODE modeBefore = player._pmode;
			const int frameBefore = player.AnimInfo.currentFrame;
			TestPlayerProcessAction(player);
			player.poe2.tick();
			Missiles.clear();
			// DoSpell casts on this frame next tick.
			if (player._pmode == PM_SPELL && player.AnimInfo.currentFrame == player._pSFNum)
				casts.hits++;
			if (player._pmode != PM_SPELL || (modeBefore == PM_SPELL && player.AnimInfo.currentFrame > frameBefore))
				continue;
			casts.starts.push_back(tick);
		}
		return casts;
	}

	/**
	 * @brief Holds a weapon skill's tray button, as RepeatHeldTraySlot does before the hero's action each tick: the
	 * ticks each use started on, and in hits the missiles of the type it fired.
	 */
	static HeldSwings HoldSkill(Player &player, WeaponSkill skill, int ticks, MissileID missile)
	{
		HeldSwings uses;
		for (int tick = 0; tick < ticks; tick++) {
			const PLR_MODE modeBefore = player._pmode;
			const int frameBefore = player.AnimInfo.currentFrame;
			if (player.destAction == ACTION_NONE && player.CanChangeAction())
				TryUseWeaponSkill(player, skill);
			if (player._pmode == PM_SPELL && (modeBefore != PM_SPELL || player.AnimInfo.currentFrame < frameBefore))
				uses.starts.push_back(tick);
			TestPlayerProcessAction(player);
			player.poe2.tick();
			for (const Missile &fired : Missiles) {
				if (fired._mitype == missile)
					uses.hits++;
			}
			Missiles.clear();
		}
		return uses;
	}
};

TEST_F(Poe2SwingSpeedTest, HeldAttackWaitsForTheLastFrameInPoe2Only)
{
	Player &player = *MyPlayer;
	Equip(player, "Shortsword");
	ASSERT_EQ(player._pAFrames, 16);
	ASSERT_EQ(player._pAFNum, 9);

	// POE2: the next swing starts on the last frame, a Shortsword's 1 / 1.35 seconds on: 14.8 ticks, so 14 or 15.
	const HeldSwings poe2 = HoldAttack(player, WeaponSkill::None, 1500, false);
	EXPECT_EQ(poe2.starts.front(), 0);
	EXPECT_NEAR(poe2.averageCycle(), 20 / 1.35, 0.02);
	for (size_t i = 1; i < poe2.starts.size(); i++)
		EXPECT_TRUE(poe2.starts[i] - poe2.starts[i - 1] == 14 || poe2.starts[i] - poe2.starts[i - 1] == 15);
	// Every swing reaches its hit frame once.
	EXPECT_NEAR(poe2.hits, static_cast<int>(poe2.starts.size()), 1);

	// Diablo: the next swing cuts the follow-through short, the tick after the hit (0.45 s), whatever the base.
	gbIsPoe2 = false;
	player._pmode = PM_STAND;
	player.destAction = ACTION_NONE;
	const HeldSwings diablo = HoldAttack(player, WeaponSkill::None, 80, false);
	EXPECT_EQ(diablo.cycle(), 9);
}

TEST_F(Poe2SwingSpeedTest, WeaponBasesSetTheAttackTime)
{
	Player &player = *MyPlayer;
	// The hit lands as far through the time as through the animation: 8 of the sword's 15 frame steps.
	EXPECT_EQ(ActionTicksToActionFrame(15, 8, 16), 8);
	EXPECT_EQ(ActionTicksToActionFrame(30, 8, 16), 16);

	const struct {
		std::string_view base;
		double aps;
	} weapons[] = { { "Shortsword", 1.35 }, { "Longsword", 1.15 }, { "Sledge", 0.95 } };
	for (const auto &[base, aps] : weapons) {
		SCOPED_TRACE(base);
		Equip(player, base);
		EXPECT_EQ(WeaponAttacksPerSecond(player), static_cast<int>((aps * 100) + 0.5));
		const HeldSwings swings = HoldAttack(player, WeaponSkill::None, 2000, false);
		EXPECT_NEAR(swings.averageCycle() / 20, 1 / aps, 0.002);
		EXPECT_NEAR(UseSeconds(player, DefaultSkillSlot(player)), 1 / aps, 0.0001);
	}

	// The weapon's own increased attack speed raises its attacks per second, as POE2 shows it; the rest adds to the
	// hero's increased attack speed: 1.35 x 1.1 (1.485, which the weapon keeps to hundredths: 1.49) x 1.2.
	Equip(player, "Shortsword");
	Item &sword = player.InvBody[INVLOC_HAND_LEFT];
	ApplyModEffect(sword, ModEffect::AttackSpeed, 10, 0);
	EXPECT_EQ(ItemBaseAttacksPerSecond(sword), 135);
	EXPECT_EQ(ItemAttacksPerSecond(sword), 149);
	CalcPlrInv(player, false);
	player.poe2.attackSpeedPercent += 20;
	EXPECT_EQ(WeaponAttacksPerSecond(player), 149);
	EXPECT_EQ(IncreasedAttackSpeedPercent(player, DefaultSkillSlot(player)), 20);
	EXPECT_NEAR(UseSeconds(player, DefaultSkillSlot(player)), 1 / (1.49 * 1.2), 0.0001);

	// A crossbow's free Bow Shot: 0.9 a second.
	Equip(player, "Light Crossbow");
	EXPECT_NEAR(UseSeconds(player, DefaultSkillSlot(player)), 1 / 0.9, 0.0001);

	// Diablo's own bases take their kind's: The Butcher's Cleaver's two-handed axe, a short sword, a dagger.
	for (const auto &[base, aps] : { std::pair<std::string_view, int> { "Cleaver", DefaultTwoHandAxeAps }, { "Short Sword", DefaultOneHandSwordAps }, { "Dagger", DefaultDaggerAps } }) {
		Item item {};
		InitializeItem(item, FindBase(base));
		EXPECT_EQ(ItemBaseAttacksPerSecond(item), aps) << base;
	}
}

TEST_F(Poe2SwingSpeedTest, AttackSpeedAndSkillSpeedChangeTheTime)
{
	Player &player = *MyPlayer;
	Equip(player, "Shortsword");

	// 20% increased attack speed: 1 / (1.35 x 1.2).
	player.poe2.attackSpeedPercent = 20;
	const HeldSwings faster = HoldAttack(player, WeaponSkill::None, 2000, false);
	EXPECT_NEAR(faster.averageCycle() / 20, 1 / (1.35 * 1.2), 0.002);
	player.poe2.attackSpeedPercent = 0;

	// A skill at 80% attack speed: 1 / (1.35 x 0.8).
	SetSkillSpeed("Cleave", 80, 0);
	EXPECT_EQ(SkillAttackSpeedPercent(SkillSlot::ForAttack(WeaponSkill::Cleave)), 80);
	player._pmode = PM_STAND;
	player.destAction = ACTION_NONE;
	const HeldSwings slower = HoldAttack(player, WeaponSkill::Cleave, 2000, false);
	EXPECT_NEAR(slower.averageCycle() / 20, 1 / (1.35 * 0.8), 0.002);
	EXPECT_NEAR(UseSeconds(player, SkillSlot::ForAttack(WeaponSkill::Cleave)), 1 / (1.35 * 0.8), 0.0001);
}

TEST_F(Poe2SwingSpeedTest, HeldDoubleSwingStrikesTwice)
{
	Player &player = *MyPlayer;
	Equip(player, "Shortsword");
	// Each strike is a step with its own row (the strike's and "Flurry:backhand"); before, the use took one attack
	// time, which the strikes split by their animation frames.
	SetSkillSpeed("Flurry", 240, 0);
	SetSkillSpeed("Flurry:backhand", 200, 0);
	ASSERT_EQ(SkillAttackSpeedPercent(SkillSlot::ForAttack(WeaponSkill::Flurry)), 240);
	ASSERT_EQ(SkillAttackSpeedPercent(SkillSlot::ForAttack(WeaponSkill::Flurry), SkillStep::Backhand), 200);
	// One use is the strike, 1 / (1.35 x 2.4), then the backhand, 1 / (1.35 x 2). The backhand plays before the next
	// one starts, and both strikes reach the hit frame.
	const HeldSwings swings = HoldAttack(player, WeaponSkill::Flurry, 2000, true);
	ASSERT_GE(swings.starts.size(), 5U);
	EXPECT_NEAR(swings.averageCycle() / 20, (1 / (1.35 * 2.4)) + (1 / (1.35 * 2.0)), 0.002);
	EXPECT_NEAR(UseSeconds(player, SkillSlot::ForAttack(WeaponSkill::Flurry)), swings.averageCycle() / 20, 0.002);
	EXPECT_NEAR(swings.backhands, static_cast<int>(swings.starts.size()), 1);
	EXPECT_NEAR(swings.hits, static_cast<int>(swings.starts.size()) + swings.backhands, 1);

	// With nothing left standing in front, it's one strike: the strike's time alone.
	player._pmode = PM_STAND;
	player.destAction = ACTION_NONE;
	const HeldSwings alone = HoldAttack(player, WeaponSkill::Flurry, 2000, false);
	EXPECT_NEAR(alone.averageCycle() / 20, 1 / (1.35 * 2.4), 0.002);
	EXPECT_EQ(alone.backhands, 0);
}

TEST_F(Poe2SwingSpeedTest, FistsOfThunderFinisherIsSlower)
{
	Player &player = *MyPlayer;
	player._pClass = HeroClass::Monk;
	Equip(player, "Ash Staff");
	SetSkillSpeed("FistsOfThunder", 140, 0);
	SetSkillSpeed("FistsOfThunder:finisher", 91, 0);
	const SkillSlot fists = SkillSlot::ForAttack(WeaponSkill::FistsOfThunder);
	const double aps = WeaponAttacksPerSecond(player) / 100.0;

	// The strike that brings the Combo to a multiple of three bursts with lightning: Tempest Flurry's third strike,
	// 140% x 0.65.
	player.poe2.combo = 1;
	EXPECT_FALSE(FistsOfThunderNextHitBursts(player));
	player.poe2.combo = 2;
	EXPECT_TRUE(FistsOfThunderNextHitBursts(player));
	EXPECT_NEAR(ActionSeconds(SkillUseTime(player, fists, false)), 1 / (aps * 1.4), 0.0001);
	EXPECT_NEAR(ActionSeconds(SkillUseTime(player, fists, false, SkillStep::Finisher)), 1 / (aps * 0.91), 0.0001);

	// The swings take them: the Combo stays where it is with nothing to hit, so every swing is the same step.
	for (const auto &[combo, speed] : { std::pair<uint8_t, double> { 0, 1.4 }, { 2, 0.91 } }) {
		SCOPED_TRACE(static_cast<int>(combo));
		player.poe2.combo = combo;
		player.poe2.comboTicks = 255;
		player._pmode = PM_STAND;
		player.destAction = ACTION_NONE;
		const HeldSwings swings = HoldAttack(player, WeaponSkill::FistsOfThunder, 1000, false);
		ASSERT_GE(swings.starts.size(), 5U);
		EXPECT_NEAR(swings.averageCycle() / 20, 1 / (aps * speed), 0.003);
	}
	// The Skills panel shows the average over the three strikes.
	EXPECT_NEAR(UseSeconds(player, fists), ((2 / (aps * 1.4)) + (1 / (aps * 0.91))) / 3, 0.001);
}

TEST_F(Poe2SwingSpeedTest, FixedAttackTimeIgnoresTheWeapon)
{
	Player &player = *MyPlayer;
	// POE2's shield skills take their own attack time whatever the weapon, and increased attack speed shortens it.
	SkillSpeedRow bash;
	bash.attackMs = 550;
	SetSkillSpeed("ShieldBash", bash);
	SetSkillSpeed("Default ShieldBash", bash);
	const SkillSlot shieldBash = SkillSlot::ForAttack(WeaponSkill::ShieldBash);
	for (const std::string_view base : { "Shortsword", "Sledge" }) {
		SCOPED_TRACE(base);
		Equip(player, base);
		EXPECT_FALSE(IsCastTimed(shieldBash, false));
		EXPECT_NEAR(UseSeconds(player, shieldBash), 0.55, 0.0001);
		player.poe2.attackSpeedPercent = 25;
		EXPECT_NEAR(UseSeconds(player, shieldBash), 0.55 / 1.25, 0.0001);
		player.poe2.attackSpeedPercent = 0;
	}

	// A shield alone: the free Shield Bash, held, swings every 0.55 seconds, 11 ticks.
	Equip(player, "Wooden Tower Shield");
	ASSERT_EQ(DefaultAttackFor(player), DefaultAttack::ShieldBash);
	EXPECT_NEAR(UseSeconds(player, DefaultSkillSlot(player)), 0.55, 0.0001);
	const HeldSwings swings = HoldAttack(player, WeaponSkill::None, 400, false);
	ASSERT_GE(swings.starts.size(), 5U);
	EXPECT_EQ(swings.cycle(), 11);
}

TEST_F(Poe2SwingSpeedTest, AddedTimeComesAfterTheSpeed)
{
	Player &player = *MyPlayer;
	Equip(player, "Shortsword");
	// Leap Slam: the attack time, then 650 ms, then up to 500 ms more for a full-length leap.
	SkillSpeedRow leap;
	leap.addedMs = 650;
	leap.distanceMs = 500;
	SetSkillSpeed("LeapSlam", leap);
	const SkillSlot leapSlam = SkillSlot::ForAttack(WeaponSkill::LeapSlam);
	EXPECT_NEAR(UseSeconds(player, leapSlam), (1 / 1.35) + 0.65, 0.0001);
	EXPECT_NEAR(ActionSeconds(SkillUseTime(player, leapSlam, false, SkillStep::Main, 100)), (1 / 1.35) + 0.65 + 0.5, 0.0001);
	EXPECT_NEAR(ActionSeconds(SkillUseTime(player, leapSlam, false, SkillStep::Main, 40)), (1 / 1.35) + 0.65 + 0.2, 0.0001);
	// Speed doesn't shorten the added time.
	player.poe2.attackSpeedPercent = 20;
	EXPECT_NEAR(UseSeconds(player, leapSlam), (1 / (1.35 * 1.2)) + 0.65, 0.0001);
	player.poe2.attackSpeedPercent = 0;

	// Landing after a full-length dash of 10 ticks, the slam takes the rest: it still strikes DashStrikeWindupTicks in.
	player.poe2.dashDistancePercent = 100;
	BeginSkillAnimation(player, WeaponSkill::LeapSlam, player_graphic::Attack, Front(), /*quickStrike=*/true, /*dashTicks=*/10);
	int toHit = 0;
	int ticks = 0;
	while (!player.AnimInfo.isLastFrame() && ticks < 100) {
		player.AnimInfo.processAnimation();
		ticks++;
		if (player.AnimInfo.currentFrame == player.poe2.castFrame)
			toHit = ticks;
	}
	EXPECT_EQ(toHit, DashStrikeWindupTicks);
	EXPECT_NEAR(ticks, (((1 / 1.35) + 0.65 + 0.5) * 20) - 10, 1);
	player.poe2.castSkill = WeaponSkill::None;
	player._pmode = PM_STAND;

	// A spell's added time comes after cast speed: Comet's second.
	SkillSpeedRow meteor;
	meteor.castMs = 1000;
	meteor.addedMs = 1000;
	SetSkillSpeed("Meteor", meteor);
	player.poe2.castSpeedPercent = 25;
	EXPECT_NEAR(UseSeconds(player, SkillSlot::ForAttack(WeaponSkill::Meteor)), 0.8 + 1.0, 0.0001);
}

TEST_F(Poe2SwingSpeedTest, StaffBoltIsACast)
{
	Player &player = *MyPlayer;
	Equip(player, "Focused Pilgrim Staff");
	ASSERT_EQ(DefaultAttackFor(player), DefaultAttack::StaffBolt);
	SkillSpeedRow bolt;
	bolt.castMs = 900;
	SetSkillSpeed("Default StaffBolt", bolt);
	const SkillSlot staffBolt = DefaultSkillSlot(player);
	EXPECT_TRUE(IsCastTimed(staffBolt, false));
	// 0.9 seconds, which cast speed shortens and attack speed doesn't.
	EXPECT_NEAR(UseSeconds(player, staffBolt), 0.9, 0.0001);
	player.poe2.attackSpeedPercent = 50;
	EXPECT_NEAR(UseSeconds(player, staffBolt), 0.9, 0.0001);
	player.poe2.castSpeedPercent = 50;
	EXPECT_NEAR(UseSeconds(player, staffBolt), 0.6, 0.0001);
	// Held, the bolts fly that often.
	const HeldSwings bolts = HoldAttack(player, WeaponSkill::None, 400, false, /*ranged=*/true);
	ASSERT_GE(bolts.starts.size(), 5U);
	EXPECT_EQ(bolts.cycle(), 12);
}

TEST_F(Poe2SwingSpeedTest, SceptreCastsTheStaffBolt)
{
	// POE2's sceptres are caster foci: no Mace Smash and no mace skills; their default attack is the staff bolt, a
	// 0.9-second cast, and a click fires it.
	Player &player = *MyPlayer;
	Equip(player, "Quick Bone Sceptre");
	const Item &sceptre = player.InvBody[INVLOC_HAND_LEFT];
	ASSERT_EQ(sceptre._itype, ItemType::Mace);
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::StaffBolt);
	EXPECT_TRUE(FiresStaffBolt(player));
	EXPECT_TRUE(AttacksAtRange(player));
	EXPECT_EQ(WeaponSkillForItem(sceptre), WeaponSkill::None);
	for (const WeaponSkill skill : { WeaponSkill::GroundSlam, WeaponSkill::Smite, WeaponSkill::LeapSlam })
		EXPECT_FALSE(WeaponAllowsSkill(player, skill)) << WeaponSkillName(skill);
	// No attacks per second of its own: the bolt is timed by its cast.
	EXPECT_EQ(ItemBaseAttacksPerSecond(sceptre), 0);
	EXPECT_EQ(WeaponAttacksPerSecond(player), UnarmedAps);
	const SkillSlot staffBolt = DefaultSkillSlot(player);
	EXPECT_NEAR(UseSeconds(player, staffBolt), 0.9, 0.0001);
	player.poe2.castSpeedPercent = 50;
	EXPECT_NEAR(UseSeconds(player, staffBolt), 0.6, 0.0001);
	const HeldSwings bolts = HoldAttack(player, WeaponSkill::None, 400, false, /*ranged=*/true);
	ASSERT_GE(bolts.starts.size(), 5U);
	EXPECT_EQ(bolts.cycle(), 12);
	// A cast, like POE2's staff Firebolt: cast supports apply to it, attack supports don't.
	EXPECT_NE(SkillTags(staffBolt) & ST_Spell, 0U);
	EXPECT_EQ(SkillTags(staffBolt) & ST_Attack, 0U);

	// Held with a sword, the sword is the weapon: its strike, its skill and its speed.
	Item &sword = player.InvBody[INVLOC_HAND_RIGHT];
	sword = {};
	SetupAllItems(player, sword, FindBase("Shortsword"), AdvanceRndSeed(), 30, 1, /*onlygood=*/false, /*pregen=*/false, 0, /*forceNotUnique=*/true);
	sword._iStatFlag = true;
	CalcPlrInv(player, false);
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::SwordStrike);
	EXPECT_FALSE(AttacksAtRange(player));
	EXPECT_EQ(WeaponSkillFor(player), WeaponSkill::Cleave);
	EXPECT_EQ(WeaponAttacksPerSecond(player), 135);
	sword = {};

	// Diablo has no sceptres: a mace there smashes.
	gbIsPoe2 = false;
	EXPECT_EQ(DefaultAttackFor(player), DefaultAttack::MaceSmash);
	EXPECT_FALSE(AttacksAtRange(player));
	EXPECT_EQ(WeaponSkillForItem(sceptre), WeaponSkill::GroundSlam);
	gbIsPoe2 = true;
}

TEST_F(Poe2SwingSpeedTest, UseTimeIsTheHeldCycle)
{
	Player &player = *MyPlayer;
	Equip(player, "Shortsword");
	const SkillSlot skills[] = {
		DefaultSkillSlot(player),
		SkillSlot::ForAttack(WeaponSkill::Cleave),
		SkillSlot::ForAttack(WeaponSkill::Flurry),
		SkillSlot::ForAttack(WeaponSkill::Frenzy),
	};
	// Faster and slower than the plain swing.
	for (const int16_t speed : { int16_t { 0 }, int16_t { 20 }, int16_t { -20 } }) {
		for (const SkillSlot slot : skills) {
			SCOPED_TRACE(testing::Message() << "skill " << static_cast<int>(slot.id) << ", attack speed " << speed);
			player.poe2.attackSpeedPercent = speed;
			player._pmode = PM_STAND;
			player.destAction = ACTION_NONE;
			const HeldSwings swings = HoldAttack(player, slot.attack(), 2000, true);
			ASSERT_GE(swings.starts.size(), 10U);
			EXPECT_NEAR(UseSeconds(player, slot), swings.averageCycle() / 20, 0.002);
		}
	}

	// POE2's speeds (skill_speed.tsv): Cleave 75%; Double Swing's two strikes 240% each; Frenzy 140%. The old rows only
	// kept Double Swing (75% a use) and Frenzy (115%) at their old frame speeds.
	player.poe2.attackSpeedPercent = 0;
	EXPECT_NEAR(UseSeconds(player, DefaultSkillSlot(player)), 1 / 1.35, 0.0001);
	EXPECT_NEAR(UseSeconds(player, SkillSlot::ForAttack(WeaponSkill::Cleave)), 1 / (1.35 * 0.75), 0.0001);
	EXPECT_NEAR(UseSeconds(player, SkillSlot::ForAttack(WeaponSkill::Flurry)), 2 / (1.35 * 2.4), 0.0001);
	EXPECT_NEAR(UseSeconds(player, SkillSlot::ForAttack(WeaponSkill::Frenzy)), 1 / (1.35 * 1.4), 0.0001);
}

TEST_F(Poe2SwingSpeedTest, HeldSpellTakesItsCastTime)
{
	Player &player = *MyPlayer;
	Equip(player, "Shortsword");
	player._pMemSpells |= GetSpellBitmask(SpellID::Firebolt);
	player._pSplLvl[static_cast<size_t>(SpellID::Firebolt)] = 1;
	player._pMana = Fixed26_6::fromInt(100000);
	player._pManaBase = player._pMana;
	ASSERT_EQ(player._pSFrames, 20);
	ASSERT_EQ(player._pSFNum, 14);
	const SkillSlot firebolt = SkillSlot::ForSpell(SpellID::Firebolt);

	// Firebolt's own row: POE2's 0.9 seconds.
	EXPECT_NEAR(UseSeconds(player, firebolt), 0.9, 0.0001);

	// A row without a cast time, as for a spell without a row: what a held cast took before, its animation up to the
	// cast frame (14 ticks for a Warrior).
	SetSkillSpeed("Spell Firebolt", 100, 0);
	const HeldSwings plain = HoldCast(player, SpellID::Firebolt, 600);
	ASSERT_GE(plain.starts.size(), 10U);
	EXPECT_EQ(plain.cycle(), 14);
	EXPECT_NEAR(plain.hits, static_cast<int>(plain.starts.size()), 1);
	EXPECT_NEAR(UseSeconds(player, firebolt), 0.7, 0.0001);

	// A cast time of 1.05 seconds, then 25% increased cast speed: 1.05 / 1.25.
	SetSkillSpeed("Spell Firebolt", 100, 1050);
	player._pmode = PM_STAND;
	player.destAction = ACTION_NONE;
	const HeldSwings slow = HoldCast(player, SpellID::Firebolt, 600);
	EXPECT_EQ(slow.cycle(), 21);
	EXPECT_NEAR(UseSeconds(player, firebolt), 1.05, 0.0001);
	player.poe2.castSpeedPercent = 25;
	player._pmode = PM_STAND;
	player.destAction = ACTION_NONE;
	const HeldSwings quick = HoldCast(player, SpellID::Firebolt, 2000);
	EXPECT_NEAR(quick.averageCycle() / 20, 1.05 / 1.25, 0.002);
	EXPECT_NEAR(UseSeconds(player, firebolt), quick.averageCycle() / 20, 0.002);

	// Diablo: a held cast starts again on the cast frame.
	gbIsPoe2 = false;
	player._pmode = PM_STAND;
	player.destAction = ACTION_NONE;
	player.poe2.castSpeedPercent = 0;
	player.poe2.passiveCastFrames = 0;
	const HeldSwings diablo = HoldCast(player, SpellID::Firebolt, 300);
	EXPECT_EQ(diablo.cycle(), 14);
}

TEST_F(Poe2SwingSpeedTest, EverySkillSpeedRowNamesASkill)
{
	// Each key is a weapon skill's, a default attack's or a spell's (or one's step): a typo would quietly leave a skill
	// at 100%.
	EXPECT_TRUE(UnknownSkillSpeedKeys().empty());
	for (const std::string &key : UnknownSkillSpeedKeys())
		ADD_FAILURE() << key;
	EXPECT_TRUE(IsKnownSkillSpeedKey("Flurry:backhand"));
	EXPECT_TRUE(IsKnownSkillSpeedKey("Spell Fireball"));
	EXPECT_TRUE(IsKnownSkillSpeedKey("Default StaffBolt"));
	EXPECT_FALSE(IsKnownSkillSpeedKey("Cleave:backhand"));
	EXPECT_FALSE(IsKnownSkillSpeedKey("Flury"));
	EXPECT_FALSE(IsKnownSkillSpeedKey("Spell Firebolts"));
}

TEST_F(Poe2SwingSpeedTest, EveryActiveSkillHasARow)
{
	// The crossbow skills the bow change retired, kept only for saved gems, need no row.
	const WeaponSkill retired[] = { WeaponSkill::Impale, WeaponSkill::SpikeTrap, WeaponSkill::Sentry, WeaponSkill::SiegeVolley,
		WeaponSkill::RapidFire };
	for (int i = 1; i <= static_cast<int>(WeaponSkill::Last); i++) {
		const auto skill = static_cast<WeaponSkill>(i);
		if (std::find(std::begin(retired), std::end(retired), skill) != std::end(retired))
			continue;
		EXPECT_NE(FindSkillSpeedRow(SkillSlot::ForAttack(skill)), nullptr) << SkillSpeedKey(SkillSlot::ForAttack(skill));
	}
	for (const DefaultAttack attack : { DefaultAttack::Punch, DefaultAttack::SwordStrike, DefaultAttack::DaggerStab, DefaultAttack::AxeChop,
	         DefaultAttack::MaceSmash, DefaultAttack::StaffStrike, DefaultAttack::StaffBolt, DefaultAttack::BowShot, DefaultAttack::ShieldBash }) {
		const SkillSlot slot = SkillSlot::ForDefault(static_cast<uint8_t>(attack));
		EXPECT_NE(FindSkillSpeedRow(slot), nullptr) << SkillSpeedKey(slot);
	}
	// Every spell a gem teaches.
	int spells = 0;
	for (const ItemData &data : AllItemsList) {
		if (data.iMiscId != IMISC_SKILLGEM_SPELL || data.iSpell == SpellID::Null)
			continue;
		spells++;
		const SkillSlot slot = SkillSlot::ForSpell(data.iSpell);
		EXPECT_NE(FindSkillSpeedRow(slot), nullptr) << SkillSpeedKey(slot);
		EXPECT_TRUE(IsCastTimed(slot, true)) << SkillSpeedKey(slot);
		EXPECT_GT(SkillCastTimeMs(slot), 0) << SkillSpeedKey(slot);
	}
	EXPECT_GE(spells, 30);
}

TEST_F(Poe2SwingSpeedTest, SkillSpeedsAreThePoe2Research)
{
	// A few of the research's numbers (research/attack-speed/skill-speeds.tsv) as the game reads them.
	const auto attack = [](WeaponSkill skill) { return SkillSlot::ForAttack(skill); };
	EXPECT_EQ(SkillAttackSpeedPercent(attack(WeaponSkill::Cleave)), 75);
	// The crossbow skills that became bow skills: Fire Arrow and Explosive Arrow.
	EXPECT_EQ(SkillAttackSpeedPercent(attack(WeaponSkill::ClusterBolt)), 100);
	EXPECT_NE(FindSkillSpeedRow(attack(WeaponSkill::ClusterBolt)), nullptr);
	EXPECT_EQ(SkillAttackSpeedPercent(attack(WeaponSkill::ExplosiveBolt)), 70);
	EXPECT_EQ(SkillAttackSpeedPercent(attack(WeaponSkill::Shockwave)), 40);
	EXPECT_EQ(SkillAttackSpeedPercent(attack(WeaponSkill::WaveOfLight)), 120);
	EXPECT_EQ(SkillAttackSpeedPercent(attack(WeaponSkill::FistsOfThunder)), 140);
	EXPECT_EQ(SkillAttackSpeedPercent(attack(WeaponSkill::FistsOfThunder), SkillStep::Finisher), 91);
	EXPECT_EQ(SkillAttackSpeedPercent(attack(WeaponSkill::Flurry), SkillStep::Backhand), 240);
	EXPECT_EQ(SkillCastTimeMs(attack(WeaponSkill::IceBolt)), 800);
	EXPECT_EQ(SkillCastTimeMs(SkillSlot::ForSpell(SpellID::Fireball)), 1200);
	EXPECT_EQ(SkillCastTimeMs(SkillSlot::ForDefault(static_cast<uint8_t>(DefaultAttack::StaffBolt))), 900);
	// The shield skills' fixed attack times.
	EXPECT_EQ(FindSkillSpeedRow(attack(WeaponSkill::ShieldBash))->attackMs, 550);
	EXPECT_EQ(FindSkillSpeedRow(SkillSlot::ForDefault(static_cast<uint8_t>(DefaultAttack::ShieldBash)))->attackMs, 550);
	EXPECT_EQ(FindSkillSpeedRow(attack(WeaponSkill::ShieldCharge))->attackMs, 600);
	EXPECT_EQ(FindSkillSpeedRow(attack(WeaponSkill::BlessedShield))->attackMs, 800);
	EXPECT_EQ(FindSkillSpeedRow(attack(WeaponSkill::ShieldWall))->attackMs, 1250);
	EXPECT_FALSE(IsCastTimed(attack(WeaponSkill::ShieldWall), true));
	// The flat added times.
	EXPECT_EQ(FindSkillSpeedRow(attack(WeaponSkill::LeapSlam))->addedMs, 650);
	EXPECT_EQ(FindSkillSpeedRow(attack(WeaponSkill::LeapSlam))->distanceMs, 500);
	EXPECT_EQ(FindSkillSpeedRow(attack(WeaponSkill::EscapeShot))->addedMs, 700);
	EXPECT_EQ(FindSkillSpeedRow(attack(WeaponSkill::Whirlwind))->addedMs, 1000);
	EXPECT_EQ(FindSkillSpeedRow(attack(WeaponSkill::FallingSword))->addedMs, 650);
	EXPECT_EQ(FindSkillSpeedRow(attack(WeaponSkill::Meteor))->addedMs, 1000);

	// Unarmed: POE2's 1.65 a second, and Punch at 100% of it.
	Player &player = *MyPlayer;
	player.InvBody[INVLOC_HAND_LEFT] = {};
	CalcPlrInv(player, false);
	EXPECT_EQ(WeaponAttacksPerSecond(player), 165);
	ASSERT_EQ(DefaultAttackFor(player), DefaultAttack::Punch);
	EXPECT_NEAR(UseSeconds(player, DefaultSkillSlot(player)), 1 / 1.65, 0.0001);
}

TEST_F(Poe2SwingSpeedTest, HellfireDartsFiresOneEmberACast)
{
	// POE2's 0.4 seconds is per ember: a held cast conjures one every 8 ticks, where a cast fired five in a stream.
	Player &player = *MyPlayer;
	Equip(player, "Shortsword");
	leveltype = DTYPE_CATHEDRAL;
	const WeaponSkill darts = WeaponSkill::EmberFusillade;
	ASSERT_EQ(PerHitBurst(darts), EmberFusilladeEmbers);
	EXPECT_NEAR(UseSeconds(player, SkillSlot::ForAttack(darts)), 0.4, 0.0001);
	const HeldSwings held = HoldSkill(player, darts, 200, MissileID::Firebolt);
	EXPECT_EQ(held.starts.front(), 0);
	EXPECT_EQ(held.cycle(), 8);
	EXPECT_NEAR(held.hits, static_cast<int>(held.starts.size()), 1);
	EXPECT_EQ(player.poe2.volleyShotsLeft, 0);
	// Each ember hits as one of the old five did; the cast costs a fifth of the five's mana (16), rounded.
	EXPECT_EQ(PerHitDamagePercent(player, darts), 100);
	EXPECT_EQ(WeaponSkillManaCost(darts), 3);

	// 50% increased cast speed: 0.4 / 1.5, 5.3 ticks.
	player.poe2.castSpeedPercent = 50;
	player._pmode = PM_STAND;
	player.destAction = ACTION_NONE;
	const HeldSwings faster = HoldSkill(player, darts, 400, MissileID::Firebolt);
	EXPECT_NEAR(faster.averageCycle() / 20, 0.4 / 1.5, 0.003);
}

TEST_F(Poe2SwingSpeedTest, HeldPerHitSkillsStrikeAtThePerHitTime)
{
	Player &player = *MyPlayer;
	leveltype = DTYPE_CATHEDRAL;

	// Zeal: one strike a use at POE2's 240% (the research's 60% a use of four strikes), a quarter of the mana.
	Equip(player, "Shortsword");
	const WeaponSkill zeal = WeaponSkill::Zeal;
	EXPECT_EQ(PerHitBurst(zeal), 4);
	EXPECT_EQ(SkillAttackSpeedPercent(SkillSlot::ForAttack(zeal)), 240);
	EXPECT_NEAR(UseSeconds(player, SkillSlot::ForAttack(zeal)), 1 / (1.35 * 2.4), 0.0001);
	const HeldSwings strikes = HoldSkill(player, zeal, 2000, MissileID::Null);
	EXPECT_EQ(strikes.starts.front(), 0);
	EXPECT_NEAR(strikes.averageCycle() / 20, 1 / (1.35 * 2.4), 0.002);
	EXPECT_EQ(PerHitDamagePercent(player, zeal), 100);
	EXPECT_EQ(WeaponSkillManaCost(zeal), 1);

	// Blade Fury with a Shiv: one dagger a use at 550% of 1.5 a second would be 0.12 seconds; no action is quicker
	// than 4 ticks, so each dagger hits for what the 0.2 seconds lost (165%).
	Equip(player, "Shiv");
	const WeaponSkill fury = WeaponSkill::BladeFury;
	EXPECT_EQ(PerHitBurst(fury), 8);
	EXPECT_NEAR(UseSeconds(player, SkillSlot::ForAttack(fury)), 0.2, 0.0001);
	EXPECT_EQ(PerHitDamagePercent(player, fury), 165);
	const HeldSwings daggers = HoldSkill(player, fury, 200, MissileID::Arrow);
	EXPECT_EQ(daggers.cycle(), 4);
	EXPECT_NEAR(daggers.hits, static_cast<int>(daggers.starts.size()), 1);
	EXPECT_EQ(player.poe2.volleyShotsLeft, 0);
	EXPECT_EQ(WeaponSkillManaCost(fury), 2);

	// Seven-Sided Strike: 350% a strike (the research's 50% for a use of up to seven), a seventh of the mana.
	EXPECT_EQ(PerHitBurst(WeaponSkill::SevenSidedStrike), 7);
	EXPECT_EQ(SkillAttackSpeedPercent(SkillSlot::ForAttack(WeaponSkill::SevenSidedStrike)), 350);
	EXPECT_EQ(WeaponSkillManaCost(WeaponSkill::SevenSidedStrike), 1);

	// The count of hits wraps at the end of a set of any burst.
	player.poe2.perHitCount = 839;
	EXPECT_EQ(TakePerHitIndex(player), 839);
	EXPECT_EQ(TakePerHitIndex(player), 0);

	// Outside POE2 Mashup a use is the whole burst again.
	gbIsPoe2 = false;
	EXPECT_EQ(PerHitBurst(zeal), 0);
	EXPECT_EQ(PerHitDamagePercent(player, fury), 100);
}

TEST_F(Poe2SwingSpeedTest, DiabloKeepsItsFrames)
{
	// Outside POE2 Mashup nothing is loaded and nothing is timed: the 16-frame swing is cut short after the hit.
	Player &player = *MyPlayer;
	Equip(player, "Shortsword");
	gbIsPoe2 = false;
	LoadSkillSpeedData();
	EXPECT_EQ(FindSkillSpeedRow(SkillSlot::ForAttack(WeaponSkill::Cleave)), nullptr);
	player._pmode = PM_STAND;
	player.destAction = ACTION_NONE;
	const HeldSwings diablo = HoldAttack(player, WeaponSkill::None, 80, false);
	EXPECT_EQ(diablo.cycle(), 9);
}

} // namespace
} // namespace devilution
