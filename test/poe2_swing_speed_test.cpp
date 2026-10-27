#include <cstdint>
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
	 */
	static HeldSwings HoldAttack(Player &player, WeaponSkill skill, int ticks, bool targetStands)
	{
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
				player.destAction = ACTION_ATTACK;
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
			if (player._pmode == PM_ATTACK && player.AnimInfo.currentFrame == player._pAFNum - 1)
				swings.hits++;
			// Within a swing the frame goes up by one each tick; a new swing starts it again.
			if (player._pmode != PM_ATTACK || (modeBefore == PM_ATTACK && player.AnimInfo.currentFrame > frameBefore))
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
	} weapons[] = { { "Shortsword", 1.55 }, { "Corroded Longsword", 1.30 }, { "Felled Greatclub", 1.10 } };
	for (const auto &[base, aps] : weapons) {
		SCOPED_TRACE(base);
		Equip(player, base);
		EXPECT_EQ(WeaponAttacksPerSecond(player), static_cast<int>((aps * 100) + 0.5));
		const HeldSwings swings = HoldAttack(player, WeaponSkill::None, 2000, false);
		EXPECT_NEAR(swings.averageCycle() / 20, 1 / aps, 0.002);
		EXPECT_NEAR(UseSeconds(player, DefaultSkillSlot(player)), 1 / aps, 0.0001);
	}

	// The weapon's own increased attack speed raises its attacks per second, as POE2 shows it; the rest adds to the
	// hero's increased attack speed: 1.55 x 1.1 (1.705, which the weapon keeps to hundredths: 1.71) x 1.2.
	Equip(player, "Shortsword");
	Item &sword = player.InvBody[INVLOC_HAND_LEFT];
	ApplyModEffect(sword, ModEffect::AttackSpeed, 10, 0);
	EXPECT_EQ(ItemBaseAttacksPerSecond(sword), 155);
	EXPECT_EQ(ItemAttacksPerSecond(sword), 171);
	CalcPlrInv(player, false);
	player.poe2.attackSpeedPercent += 20;
	EXPECT_EQ(WeaponAttacksPerSecond(player), 171);
	EXPECT_EQ(IncreasedAttackSpeedPercent(player, DefaultSkillSlot(player)), 20);
	EXPECT_NEAR(UseSeconds(player, DefaultSkillSlot(player)), 1 / (1.71 * 1.2), 0.0001);

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
	ASSERT_EQ(SkillAttackSpeedPercent(SkillSlot::ForAttack(WeaponSkill::Flurry)), 75);
	// One use, both strikes, takes the skill's attack time: 1 / (1.35 x 0.75). The backhand plays before the next one
	// starts, and both strikes reach the hit frame.
	const HeldSwings swings = HoldAttack(player, WeaponSkill::Flurry, 2000, true);
	ASSERT_GE(swings.starts.size(), 5U);
	EXPECT_NEAR(swings.averageCycle() / 20, 1 / (1.35 * 0.75), 0.002);
	EXPECT_NEAR(swings.backhands, static_cast<int>(swings.starts.size()), 1);
	EXPECT_NEAR(swings.hits, static_cast<int>(swings.starts.size()) + swings.backhands, 1);

	// With nothing left standing in front, it's one strike: its share of the time, 15 of the 26 frame steps.
	player._pmode = PM_STAND;
	player.destAction = ACTION_NONE;
	const HeldSwings alone = HoldAttack(player, WeaponSkill::Flurry, 2000, false);
	EXPECT_NEAR(alone.averageCycle() / 20, 15.0 / 26 / (1.35 * 0.75), 0.002);
	EXPECT_EQ(alone.backhands, 0);
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

	player.poe2.attackSpeedPercent = 0;
	EXPECT_NEAR(UseSeconds(player, DefaultSkillSlot(player)), 1 / 1.35, 0.0001);
	EXPECT_NEAR(UseSeconds(player, SkillSlot::ForAttack(WeaponSkill::Cleave)), 1 / 1.35, 0.0001);
	EXPECT_NEAR(UseSeconds(player, SkillSlot::ForAttack(WeaponSkill::Flurry)), 1 / (1.35 * 0.75), 0.0001);
	EXPECT_NEAR(UseSeconds(player, SkillSlot::ForAttack(WeaponSkill::Frenzy)), 1 / (1.35 * 1.15), 0.0001);
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

	// No row: what a held cast took before, its animation up to the cast frame (14 ticks for a Warrior).
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

} // namespace
} // namespace devilution
