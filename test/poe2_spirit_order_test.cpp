/**
 * @file poe2_spirit_order_test.cpp
 *
 * POE2 Mashup: Spirit skills switch on only with the Spirit for them, and when Spirit runs short the newest switched
 * on wait until there is enough again (Curtis 2026-10-30); a gem moved to another socket stays on; the minion skills'
 * details and the minion portraits.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <string_view>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "monster.h"
#include "player.h"
#include "poe2/hud_minions.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/skill_details.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/spirit.hpp"
#include "spells.h"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

constexpr PersistentSkill Ash = PersistentSkill::HeraldOfAsh;
constexpr PersistentSkill Thunder = PersistentSkill::HeraldOfThunder;
constexpr PersistentSkill Warriors = PersistentSkill::SkeletalWarriors;
constexpr PersistentSkill Golem = PersistentSkill::SummonGolem;

class Poe2SpiritOrderTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
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
		LoadPassiveTreeData();
		LoadMonsterData();
		ActiveMonsterCount = 0;

		Player &player = *MyPlayer;
		player._pClass = HeroClass::Sorcerer;
		player.setCharacterLevel(10);
		player._pBaseStr = player._pStrength = 50;
		player._pBaseMag = player._pMagic = 100;
		player._pBaseDex = player._pDexterity = 50;
		player._pBaseVit = player._pVitality = 25;
		player._pMaxHP = player._pHitPoints = Fixed26_6::fromInt(100);
		player._pMaxHPBase = player._pHPBase = Fixed26_6::fromInt(100);
	}

	void TearDown() override
	{
		ActiveMonsterCount = 0;
		leveltype = DTYPE_TOWN;
		gbIsPoe2 = false;
		UnloadModArchives();
	}
};

void Socket(Player &player, size_t slot, PersistentSkill skill)
{
	SocketSkill(player, slot, SkillSlot::ForPersistent(skill));
}

/** @brief A right-click on the gem's row while it is off. */
void SwitchOn(Player &player, size_t slot)
{
	ASSERT_FALSE(player.poe2.spiritGemOn(slot));
	EXPECT_TRUE(ToggleSpiritGem(player, slot));
}

bool HasLine(const std::vector<SkillDetailLine> &lines, std::string_view label)
{
	return std::any_of(lines.begin(), lines.end(), [&](const SkillDetailLine &line) { return line.label == label; });
}

TEST_F(Poe2SpiritOrderTest, SwitchingOnNeedsTheSpiritForIt)
{
	Player &player = *MyPlayer;
	player.poe2.spiritSkulls = 1;
	ASSERT_EQ(MaxSpirit(player), 30);
	Socket(player, 0, Ash);
	SwitchOn(player, 0);
	EXPECT_TRUE(player.poe2.spiritGemOn(0));
	EXPECT_EQ(ReservedSpirit(player), 30);

	// No Spirit left: the click is taken, but the gem stays off.
	Socket(player, 1, Thunder);
	EXPECT_TRUE(ToggleSpiritGem(player, 1));
	EXPECT_FALSE(player.poe2.spiritGemOn(1));
	EXPECT_FALSE(IsPersistentSkillActive(player, Thunder));
	EXPECT_EQ(ReservedSpirit(player), 30);

	// Skeletal Warriors' first two are free: they go on with no Spirit at all.
	Socket(player, 2, Warriors);
	SwitchOn(player, 2);
	EXPECT_TRUE(player.poe2.spiritGemOn(2));
	EXPECT_TRUE(IsPersistentSkillActive(player, Warriors));

	// Switching one off frees its Spirit for the other.
	EXPECT_TRUE(ToggleSpiritGem(player, 0));
	SwitchOn(player, 1);
	EXPECT_TRUE(IsPersistentSkillActive(player, Thunder));
	EXPECT_FALSE(IsPersistentSkillActive(player, Ash));
}

TEST_F(Poe2SpiritOrderTest, NewestWaitsFirstAndComesBackOn)
{
	Player &player = *MyPlayer;
	player.poe2.spiritSkulls = AllSpiritSkulls;
	ASSERT_GE(MaxSpirit(player), 60);
	// Ash goes on first though Thunder's socket comes before it.
	Socket(player, 0, Thunder);
	Socket(player, 3, Ash);
	Socket(player, 5, Warriors);
	SwitchOn(player, 3);
	SwitchOn(player, 0);
	SwitchOn(player, 5);
	EXPECT_EQ(ReservedSpirit(player), 60);

	// The Spirit drops to 30: the newest that reserves Spirit waits, the free skeletons don't.
	player.poe2.spiritSkulls = 1;
	EXPECT_TRUE(IsPersistentSkillActive(player, Ash));
	EXPECT_FALSE(IsPersistentSkillActive(player, Thunder));
	EXPECT_TRUE(IsSpiritSkillWaiting(player, Thunder));
	EXPECT_FALSE(IsSpiritSkillWaiting(player, Ash));
	EXPECT_TRUE(IsPersistentSkillActive(player, Warriors));
	EXPECT_EQ(ReservedSpirit(player), 30);
	EXPECT_TRUE(player.poe2.spiritGemOn(0)) << "It stays switched on, waiting";
	EXPECT_EQ(GetSkillUsability(player, SkillSlot::ForPersistent(Thunder)), SkillUsability::Spirit);

	// The per-tick check notes it, and again when it comes back on.
	ProcessPersistentSkills(player);
	EXPECT_EQ(player.poe2.spiritWaiting, 1U << 0);
	player.poe2.spiritSkulls = AllSpiritSkulls;
	EXPECT_TRUE(IsPersistentSkillActive(player, Thunder));
	EXPECT_FALSE(IsSpiritSkillWaiting(player, Thunder));
	ProcessPersistentSkills(player);
	EXPECT_EQ(player.poe2.spiritWaiting, 0U);
	EXPECT_EQ(ReservedSpirit(player), 60);
}

TEST_F(Poe2SpiritOrderTest, WaitingGemsDontGetAheadOfOlderOnes)
{
	Player &player = *MyPlayer;
	player.poe2.spiritSkulls = AllSpiritSkulls;
	Socket(player, 0, Ash);
	Socket(player, 1, Thunder);
	Socket(player, 2, PersistentSkill::HeraldOfFrost);
	SwitchOn(player, 2);
	SwitchOn(player, 1);
	SwitchOn(player, 0);
	// Room for one only: the oldest keeps running and the two newer wait, even one that would fit alone.
	player.poe2.spiritSkulls = 1;
	EXPECT_TRUE(IsPersistentSkillActive(player, PersistentSkill::HeraldOfFrost));
	EXPECT_TRUE(IsSpiritSkillWaiting(player, Thunder));
	EXPECT_TRUE(IsSpiritSkillWaiting(player, Ash));
	// Switching the oldest off lets the next one back on.
	EXPECT_TRUE(ToggleSpiritGem(player, 2));
	EXPECT_TRUE(IsPersistentSkillActive(player, Thunder));
	EXPECT_TRUE(IsSpiritSkillWaiting(player, Ash));
}

TEST_F(Poe2SpiritOrderTest, MovedGemStaysOn)
{
	Player &player = *MyPlayer;
	player.poe2.spiritSkulls = AllSpiritSkulls;
	Socket(player, 0, Golem);
	SwitchOn(player, 0);
	ASSERT_TRUE(IsPersistentSkillActive(player, Golem));
	// Lifted from its socket and put in another, as the Skills panel moves it.
	SocketSkill(player, 0, {});
	EXPECT_FALSE(IsPersistentSkillActive(player, Golem));
	Socket(player, 4, Golem);
	EXPECT_TRUE(player.poe2.spiritGemOn(4));
	EXPECT_TRUE(IsPersistentSkillActive(player, Golem));
	EXPECT_FALSE(player.poe2.spiritGemOn(0));
	// Another gem put where it was starts off.
	Socket(player, 0, Ash);
	EXPECT_FALSE(player.poe2.spiritGemOn(0));
}

TEST_F(Poe2SpiritOrderTest, GrantedSkillNeedsSpiritToo)
{
	Player &player = *MyPlayer;
	player.poe2.granted[0] = { SkillSlot::ForPersistent(Ash), 5, 0 };
	player.poe2.toggleGranted(0);
	ASSERT_TRUE(player.poe2.grantedOff(0));
	EXPECT_TRUE(ToggleGrantedSpiritSkill(player, 0));
	EXPECT_TRUE(player.poe2.grantedOff(0)) << "No Spirit: it stays off";

	player.poe2.spiritSkulls = 1;
	EXPECT_TRUE(ToggleGrantedSpiritSkill(player, 0));
	EXPECT_FALSE(player.poe2.grantedOff(0));
}

TEST_F(Poe2SpiritOrderTest, MinionDetailsShowWhatPoe2Shows)
{
	Player &player = *MyPlayer;
	player.poe2.spiritSkulls = AllSpiritSkulls;
	for (const PersistentSkill skill : { Golem, PersistentSkill::BloodGolem, Warriors, PersistentSkill::SkeletalMages }) {
		Socket(player, 0, skill);
		const std::vector<SkillDetailLine> lines = SkillDetails(player, 0);
		for (const std::string_view label : { "Each minion", "Life", "Damage per blow", "Attack time", "Resistances", "Reserves" })
			EXPECT_TRUE(HasLine(lines, label)) << PersistentSkillName(skill) << ": " << label;
	}
	// Skeleton gems say how many and how many at most.
	EXPECT_TRUE(HasLine(SkillDetails(player, 0), "Most minions"));

	// A Cleric doesn't fight.
	Socket(player, 0, PersistentSkill::SkeletalClerics);
	const std::vector<SkillDetailLine> clerics = SkillDetails(player, 0);
	EXPECT_TRUE(HasLine(clerics, "Life"));
	EXPECT_FALSE(HasLine(clerics, "Damage per blow"));
	EXPECT_FALSE(HasLine(clerics, "Attack time"));
}

TEST_F(Poe2SpiritOrderTest, SceptreSkillHasDetails)
{
	Player &player = *MyPlayer;
	player.poe2.granted[0] = { SkillSlot::ForPersistent(Warriors), 5, 0 };
	const std::vector<SkillDetailLine> lines = SkillDetails(player, GrantedRowMain);
	EXPECT_TRUE(HasLine(lines, "Granted by your sceptre"));
	EXPECT_TRUE(HasLine(lines, "Life"));
	EXPECT_TRUE(HasLine(lines, "Damage per blow"));
	EXPECT_TRUE(SkillDetails(player, GrantedRowOff).empty());
}

TEST_F(Poe2SpiritOrderTest, GolemHasAPortrait)
{
	Player &player = *MyPlayer;
	player.poe2.spiritSkulls = AllSpiritSkulls;
	leveltype = DTYPE_CATHEDRAL;
	Socket(player, 0, Golem);
	SwitchOn(player, 0);

	std::vector<MinionPortrait> portraits = MinionHudPortraits(player);
	ASSERT_FALSE(portraits.empty());
	EXPECT_EQ(portraits[0].icon, SkillSlot::ForPersistent(Golem));
	EXPECT_EQ(portraits[0].alive, 0);
	EXPECT_EQ(portraits[0].target, 1);
	EXPECT_EQ(portraits[0].pending, 1);

	LevelMonsterTypes[0].type = MT_GOLEM;
	Monster &golem = Monsters[3];
	golem = {};
	golem.levelType = 0;
	golem.flags = MFLAG_GOLEM;
	golem.goalVar3 = 0;
	golem.position.tile = { 20, 20 };
	golem.maxHitPoints = golem.hitPoints = Fixed26_6::fromInt(50);
	ActiveMonsters[0] = 3;
	ActiveMonsterCount = 1;
	portraits = MinionHudPortraits(player);
	ASSERT_FALSE(portraits.empty());
	EXPECT_EQ(portraits[0].alive, 1);
	EXPECT_EQ(portraits[0].pending, 0);

	// Switched off, its gem takes the golem's portrait with it; the Golem spell's own golem keeps one.
	EXPECT_TRUE(ToggleSpiritGem(player, 0));
	portraits = MinionHudPortraits(player);
	ASSERT_EQ(portraits.size(), 1U);
	EXPECT_EQ(portraits[0].icon, SkillSlot::ForSpell(SpellID::Golem));
	golem = {};
}

} // namespace
} // namespace devilution
