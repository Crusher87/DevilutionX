#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/lighting_defs.hpp"
#include "game_mode.hpp"
#include "levels/dun_tile_data.hpp"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/player_data.hpp"
#include "poe2/skill_details.hpp"
#include "poe2/weapon_skills.hpp"
#include "tables/playerdat.hpp"

namespace devilution {

extern void TestPlayerProcessAction(Player &player);

namespace {

/** @brief The ticks on which a held attack started a swing, and how many backhands Double Swing added. */
struct HeldSwings {
	std::vector<int> starts;
	int backhands = 0;

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
		UnloadModArchives();
		LoadMissileData();
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
};

TEST_F(Poe2SwingSpeedTest, HeldAttackWaitsForTheLastFrameInPoe2Only)
{
	Player &player = *MyPlayer;
	ASSERT_EQ(player._pAFrames, 16);
	ASSERT_EQ(player._pAFNum, 9);

	// POE2: the next swing starts on the last frame, 15 ticks on (0.75 s; POE2's Shortsword takes 0.74 s).
	const HeldSwings poe2 = HoldAttack(player, WeaponSkill::None, 80, false);
	EXPECT_EQ(poe2.starts.front(), 0);
	EXPECT_EQ(poe2.cycle(), 15);

	// Diablo: the next swing cuts the follow-through short, the tick after the hit (0.45 s).
	gbIsPoe2 = false;
	player._pmode = PM_STAND;
	player.destAction = ACTION_NONE;
	const HeldSwings diablo = HoldAttack(player, WeaponSkill::None, 80, false);
	EXPECT_EQ(diablo.cycle(), 9);
}

TEST_F(Poe2SwingSpeedTest, HeldDoubleSwingStrikesTwice)
{
	Player &player = *MyPlayer;
	// Each Double Swing's backhand plays before the next one starts: 12 ticks for the first strike and 8 for the
	// backhand, which skips FlurryBackhandSkippedFrames more.
	const HeldSwings swings = HoldAttack(player, WeaponSkill::Flurry, 100, true);
	ASSERT_GE(swings.starts.size(), 5U);
	EXPECT_EQ(swings.cycle(), 20);
	EXPECT_EQ(swings.backhands, static_cast<int>(swings.starts.size()));

	// With nothing left standing in front, it's one strike.
	player._pmode = PM_STAND;
	player.destAction = ACTION_NONE;
	const HeldSwings alone = HoldAttack(player, WeaponSkill::Flurry, 60, false);
	EXPECT_EQ(alone.cycle(), 12);
	EXPECT_EQ(alone.backhands, 0);
}

TEST_F(Poe2SwingSpeedTest, UseTimeIsTheHeldCycle)
{
	Player &player = *MyPlayer;
	const struct {
		WeaponSkill skill;
		SkillSlot slot;
	} skills[] = {
		{ WeaponSkill::None, SkillSlot::ForDefault(0) },
		{ WeaponSkill::Cleave, SkillSlot::ForAttack(WeaponSkill::Cleave) },
		{ WeaponSkill::Flurry, SkillSlot::ForAttack(WeaponSkill::Flurry) },
		{ WeaponSkill::Frenzy, SkillSlot::ForAttack(WeaponSkill::Frenzy) },
	};
	// Faster and slower than the plain swing: passives' attack speed and a Heavy base, which adds frames.
	for (const int8_t passiveFrames : { int8_t { 0 }, int8_t { 2 }, int8_t { -2 } }) {
		for (const auto &[skill, slot] : skills) {
			SCOPED_TRACE(testing::Message() << "skill " << static_cast<int>(skill) << ", passive frames " << static_cast<int>(passiveFrames));
			player.poe2.passiveAttackFrames = passiveFrames;
			player._pmode = PM_STAND;
			player.destAction = ACTION_NONE;
			const HeldSwings swings = HoldAttack(player, skill, 120, true);
			ASSERT_NE(swings.cycle(), 0);
			EXPECT_DOUBLE_EQ(UseSeconds(player, slot), swings.cycle() / 20.0);
		}
	}

	player.poe2.passiveAttackFrames = 0;
	EXPECT_DOUBLE_EQ(UseSeconds(player, SkillSlot::ForDefault(0)), 0.75);
	EXPECT_DOUBLE_EQ(UseSeconds(player, SkillSlot::ForAttack(WeaponSkill::Cleave)), 0.75);
	EXPECT_DOUBLE_EQ(UseSeconds(player, SkillSlot::ForAttack(WeaponSkill::Flurry)), 1.0);
}

} // namespace
} // namespace devilution
