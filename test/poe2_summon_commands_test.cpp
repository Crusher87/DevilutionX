#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

#include "DiabloUI/diabloui.h"
#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "init.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "loadsave.h"
#include "menu.h"
#include "missiles.h"
#include "monster.h"
#include "pfile.h"
#include "player.h"
#include "poe2/item_search.hpp"
#include "poe2/minion_gems.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/skill_details.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/spirit.hpp"
#include "poe2/spirit_skills.hpp"
#include "poe2/summon_commands.hpp"
#include "poe2/support_table.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"
#include "tables/spelldat.h"
#include "utils/file_util.h"
#include "utils/paths.h"
#include "utils/utf8.hpp"

namespace devilution {
namespace {

using SC = SummonCommand;

class Poe2SummonCommandsTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		*MyPlayer = {};
		MyPlayer->poe2.clear();
		MyPlayer->_pStrength = 200;
		MyPlayer->_pMagic = 200;
		MyPlayer->_pDexterity = 200;
		// Every Spirit skull broken: 100 Spirit, and every gem switched on.
		MyPlayer->poe2.spiritSkulls = 7;
		MyPlayer->poe2.spiritGemsOn = 0xFFFF;
		// Spirit enough for the dearer minions and two gems at once.
		MyPlayer->poe2.itemStats[static_cast<size_t>(Poe2ItemStat::Spirit)] = 300;
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
		for (size_t i = 0; i < MinionGemTable().size(); i++)
			LevelMonsterTypes[i + 1].type = MinionGemTable()[i].type;
		LevelMonsterTypes[GolemLevelType()].type = MT_GOLEM;
		leveltype = DTYPE_CATHEDRAL;
		MyPlayer->setCharacterLevel(30);
		MyPlayer->position.tile = { 40, 40 };
		MyPlayer->_pMaxHP = MyPlayer->_pHitPoints = Fixed26_6::fromInt(1000);
		MyPlayer->_pMaxHPBase = MyPlayer->_pHPBase = Fixed26_6::fromInt(1000);
		ActiveMonsterCount = 0;
		Missiles.clear();
		ClearMinionGems();
		ClearSummonCommands();
		// Damage to monsters goes out as a network message.
		SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
	}

	void TearDown() override
	{
		ActiveMonsterCount = 0;
		Missiles.clear();
		ClearMinionGems();
		ClearSpiritSkills();
		ClearSummonCommands();
		gbIsPoe2 = false;
		leveltype = DTYPE_TOWN;
		UnloadModArchives();
	}

	static size_t GolemLevelType()
	{
		return MinionGemTable().size() + 1;
	}

	static void Socket(PersistentSkill skill, int gemLevel = 1, size_t slot = 0)
	{
		MyPlayer->poe2.skillSlots[slot] = SkillSlot::ForPersistent(skill);
		MyPlayer->poe2.slotLevels[slot] = static_cast<uint8_t>(gemLevel);
		ASSERT_TRUE(IsPersistentSkillActive(*MyPlayer, skill)) << PersistentSkillName(skill);
	}

	static Monster &Place(size_t slot, Point tile, int levelType, int life)
	{
		Monster &monster = Monsters[slot];
		monster = {};
		monster.levelType = levelType;
		monster.uniqueType = UniqueMonsterType::None;
		monster.talkMsg = TEXT_NONE;
		monster.position.tile = tile;
		monster.position.future = tile;
		monster.position.old = tile;
		monster.maxHitPoints = monster.hitPoints = Fixed26_6::fromInt(life);
		monster.minDamage = 10;
		monster.maxDamage = 20;
		monster.mode = MonsterMode::Stand;
		ActiveMonsters[ActiveMonsterCount] = static_cast<unsigned>(slot);
		ActiveMonsterCount++;
		return monster;
	}

	static Monster &PlaceEnemy(size_t slot, Point tile, int life = 1000)
	{
		Monster &enemy = Place(slot, tile, 0, life);
		// A plain monster: the first seed that rolls neither magic nor rare.
		for (uint32_t seed = 0; seed < 5000; seed++) {
			enemy.rndItemSeed = seed;
			if (GetMonsterAffixes(enemy).rarity == MonsterRarity::Normal)
				break;
		}
		return enemy;
	}

	static void MakeRarity(Monster &monster, MonsterRarity rarity)
	{
		for (uint32_t seed = 0; seed < 20000; seed++) {
			monster.rndItemSeed = seed;
			if (GetMonsterAffixes(monster).rarity == rarity)
				return;
		}
		FAIL() << "no seed rolls that rarity";
	}

	/** @brief One of the hero's minions of the gem. */
	static Monster &PlaceMinion(size_t slot, Point tile, PersistentSkill gem, int life = 100)
	{
		int levelType = 0;
		for (size_t i = 0; i < MinionGemTable().size(); i++) {
			if (MinionGemTable()[i].skill == gem)
				levelType = static_cast<int>(i) + 1;
		}
		if (IsAnyOf(gem, PersistentSkill::SummonGolem, PersistentSkill::BloodGolem, PersistentSkill::FireGolem))
			levelType = static_cast<int>(GolemLevelType());
		Monster &minion = Place(slot, tile, levelType, life);
		minion.flags |= MFLAG_GOLEM;
		minion.goalVar3 = static_cast<int8_t>(MyPlayerId);
		return minion;
	}

	static SummonCommandRequest Request(PersistentSkill gem, Point target, int monster = -1)
	{
		return { gem, target, static_cast<int16_t>(monster) };
	}

	static void Tick(int ticks)
	{
		for (int i = 0; i < ticks; i++) {
			ProcessSummonCommands(*MyPlayer);
			for (size_t m = 0; m < ActiveMonsterCount; m++)
				TickCommandMonster(Monsters[ActiveMonsters[m]]);
		}
	}
};

constexpr std::array<std::pair<PersistentSkill, std::string_view>, 13> Expected { {
	{ PersistentSkill::SkeletalMages, "Flame Volley" },
	{ PersistentSkill::SkeletalSnipers, "Poison Arrow" },
	{ PersistentSkill::SkeletalFrostMages, "Frost Shell" },
	{ PersistentSkill::SkeletalStormMages, "Thunder Call" },
	{ PersistentSkill::SkeletalReavers, "Bone Frenzy" },
	{ PersistentSkill::SkeletalBrutes, "Grave Howl" },
	{ PersistentSkill::WolfPack, "Pack Rush" },
	{ PersistentSkill::SummonGolem, "Quake Stomp" },
	{ PersistentSkill::BloodGolem, "Blood Gift" },
	{ PersistentSkill::FireGolem, "Golem's Blaze" },
	{ PersistentSkill::Guardian, "Fire Sweep" },
	{ PersistentSkill::ShadowWarrior, "Switch" },
	{ PersistentSkill::Revive, "Last Rites" },
} };

TEST_F(Poe2SummonCommandsTest, EachSummonGemGrantsItsCommand)
{
	ASSERT_EQ(AllSummonCommands().size(), Expected.size());
	for (const auto &[gem, name] : Expected) {
		const SummonCommand command = CommandForGem(gem);
		ASSERT_NE(command, SC::None) << PersistentSkillName(gem);
		EXPECT_EQ(SummonCommandName(command), name);
		EXPECT_EQ(CommandGem(command), gem);
		EXPECT_FALSE(SummonCommandDescription(command).empty());
		// Short, so the tooltip stays one line.
		EXPECT_LE(SummonCommandDescription(command).size(), 50U) << name;
	}
	// The plan's gems with no Command.
	for (const PersistentSkill gem : { PersistentSkill::SkeletalWarriors, PersistentSkill::SkeletalClerics, PersistentSkill::RagingSpirits, PersistentSkill::LingeringIllusion, PersistentSkill::HeraldOfAsh })
		EXPECT_EQ(CommandForGem(gem), SC::None) << PersistentSkillName(gem);
	EXPECT_EQ(PersistentSkillName(PersistentSkill::RagingSpirits), "Burning Souls");
	EXPECT_EQ(PersistentSkillName(PersistentSkill::LingeringIllusion), "Phantom Double");
	EXPECT_EQ(PersistentSkillName(PersistentSkill::SkeletalClerics), "Bone Priests");
}

TEST_F(Poe2SummonCommandsTest, RenamedCommandsClashWithNoSkill)
{
	// The five the plan renamed, away from existing skills' names.
	for (const std::string_view old : { "Frozen Armor", "Berserk", "War Cry", "Blood Offering", "Holy Fire" }) {
		for (const SummonCommand command : AllSummonCommands())
			EXPECT_NE(SummonCommandName(command), old);
	}
	for (const SummonCommand command : AllSummonCommands()) {
		const std::string_view name = SummonCommandName(command);
		for (int skill = 1; skill <= static_cast<int>(PersistentSkill::Last); skill++)
			EXPECT_NE(PersistentSkillName(static_cast<PersistentSkill>(skill)), name);
		for (int skill = 0; skill <= static_cast<int>(WeaponSkill::Last); skill++)
			EXPECT_NE(WeaponSkillName(static_cast<WeaponSkill>(skill)), name);
		for (int spell = 1; spell <= static_cast<int>(SpellID::LAST); spell++)
			EXPECT_NE(GetSpellData(static_cast<SpellID>(spell)).sNameText, name);
	}
}

TEST_F(Poe2SummonCommandsTest, CooldownsAndTagsFollowTheNumbers)
{
	const std::array<std::pair<SummonCommand, int>, 13> seconds { {
		{ SC::FlameVolley, 5 },
		{ SC::PoisonArrow, 6 },
		{ SC::FrostShell, 12 },
		{ SC::ThunderCall, 5 },
		{ SC::BoneFrenzy, 4 },
		{ SC::GraveHowl, 8 },
		{ SC::PackRush, 6 },
		{ SC::QuakeStomp, 8 },
		{ SC::BloodGift, 10 },
		{ SC::GolemsBlaze, 10 },
		{ SC::FireSweep, 6 },
		{ SC::Switch, 5 },
		{ SC::LastRites, 8 },
	} };
	for (const auto &[command, cooldown] : seconds) {
		EXPECT_EQ(SummonCommandCooldownTicks(command), cooldown * 20) << SummonCommandName(command);
		const uint32_t tags = SummonCommandTags(command);
		EXPECT_NE(tags & ST_Minion, 0U);
		EXPECT_NE(tags & ST_Command, 0U);
		EXPECT_NE(SummonCommandTagText(command).find("Minion, Command"), std::string::npos);
	}
	EXPECT_NE(SummonCommandTags(SC::FlameVolley) & ST_Fire, 0U);
	EXPECT_NE(SummonCommandTags(SC::GolemsBlaze) & ST_Fire, 0U);
	EXPECT_NE(SummonCommandTags(SC::FireSweep) & ST_Fire, 0U);
	EXPECT_NE(SummonCommandTags(SC::PoisonArrow) & ST_Chaos, 0U);
	EXPECT_NE(SummonCommandTags(SC::FrostShell) & ST_Frost, 0U);
	EXPECT_NE(SummonCommandTags(SC::ThunderCall) & ST_Lightning, 0U);
	for (const SummonCommand command : { SC::PackRush, SC::QuakeStomp, SC::LastRites })
		EXPECT_NE(SummonCommandTags(command) & ST_Physical, 0U) << SummonCommandName(command);
	EXPECT_EQ(SummonCommandTagText(SC::PoisonArrow), "Minion, Command, Chaos");
	EXPECT_EQ(SummonCommandTagText(SC::FrostShell), "Minion, Command, Cold");
}

TEST_F(Poe2SummonCommandsTest, NeedsItsMinionsCostsNoManaAndCoolsDown)
{
	Socket(PersistentSkill::SkeletalMages);
	PlaceEnemy(1, { 45, 40 });
	const SummonCommandRequest request = Request(PersistentSkill::SkeletalMages, { 45, 40 }, 1);
	EXPECT_EQ(CheckSummonCommand(*MyPlayer, request), CommandResult::NoMinions);
	EXPECT_EQ(CheckSummonCommand(*MyPlayer, Request(PersistentSkill::SkeletalWarriors, { 45, 40 })), CommandResult::NoCommand);

	PlaceMinion(0, { 41, 40 }, PersistentSkill::SkeletalMages);
	EXPECT_EQ(CheckSummonCommand(*MyPlayer, request), CommandResult::Used);
	leveltype = DTYPE_TOWN;
	EXPECT_EQ(CheckSummonCommand(*MyPlayer, request), CommandResult::Town);
	leveltype = DTYPE_CATHEDRAL;
	MyPlayer->poe2.spiritGemsOn = 0;
	EXPECT_EQ(CheckSummonCommand(*MyPlayer, request), CommandResult::Off);
	MyPlayer->poe2.spiritGemsOn = 0xFFFF;

	MyPlayer->_pMana = MyPlayer->_pManaBase = Fixed26_6::fromInt(50);
	EXPECT_EQ(IssueSummonCommand(*MyPlayer, request), CommandResult::Used);
	EXPECT_EQ(MyPlayer->_pMana, Fixed26_6::fromInt(50));
	EXPECT_EQ(SummonCommandCooldownLeft(*MyPlayer, SC::FlameVolley), 100);
	EXPECT_EQ(CheckSummonCommand(*MyPlayer, request), CommandResult::Cooldown);
	EXPECT_EQ(IssueSummonCommand(*MyPlayer, request), CommandResult::Cooldown);
	Tick(100);
	EXPECT_EQ(SummonCommandCooldownLeft(*MyPlayer, SC::FlameVolley), 0);
	EXPECT_EQ(CheckSummonCommand(*MyPlayer, request), CommandResult::Used);
	EXPECT_FALSE(CommandResultText(CommandResult::NoMinions).empty());
	EXPECT_TRUE(CommandResultText(CommandResult::Used).empty());
}

TEST_F(Poe2SummonCommandsTest, FlameVolleyFiresEveryMageInSight)
{
	Socket(PersistentSkill::SkeletalMages);
	Monster &near = PlaceMinion(0, { 41, 40 }, PersistentSkill::SkeletalMages);
	Monster &far = PlaceMinion(2, { 70, 40 }, PersistentSkill::SkeletalMages);
	PlaceEnemy(1, { 46, 40 });
	ASSERT_EQ(IssueSummonCommand(*MyPlayer, Request(PersistentSkill::SkeletalMages, { 46, 40 }, 1)), CommandResult::Used);
	EXPECT_EQ(near.mode, MonsterMode::RangedAttack);
	EXPECT_EQ(near.poe2.commandAction, static_cast<uint8_t>(SC::FlameVolley));
	EXPECT_EQ(Point(near.poe2.commandAimX, near.poe2.commandAimY), Point(46, 40));
	// Out of sight of the target: it doesn't fire.
	EXPECT_EQ(far.mode, MonsterMode::Stand);
	EXPECT_EQ(far.poe2.commandAction, 0);
	// Its bolt is half again its normal one.
	const MinionGemData &gem = *FindMinionGem(PersistentSkill::SkeletalMages);
	EXPECT_NEAR(MinionShotAverage(near, gem.shotPercent * FlameVolleyPercent / 100), MinionShotAverage(near, gem.shotPercent) * 3 / 2, 1);
}

TEST_F(Poe2SummonCommandsTest, PoisonArrowBurstsIntoAChaosCloud)
{
	Socket(PersistentSkill::SkeletalSnipers);
	Monster &near = PlaceMinion(0, { 42, 40 }, PersistentSkill::SkeletalSnipers);
	Monster &farther = PlaceMinion(2, { 40, 42 }, PersistentSkill::SkeletalSnipers);
	Monster &enemy = PlaceEnemy(1, { 47, 40 });
	Monster &outside = PlaceEnemy(3, { 47, 44 });
	ASSERT_EQ(IssueSummonCommand(*MyPlayer, Request(PersistentSkill::SkeletalSnipers, { 47, 40 }, 1)), CommandResult::Used);
	// One archer, the nearest.
	EXPECT_EQ(near.poe2.commandAction, static_cast<uint8_t>(SC::PoisonArrow));
	EXPECT_EQ(farther.poe2.commandAction, 0);

	Missile arrow {};
	arrow._miAnimType = MissileGraphicID::None;
	OnCommandShotFired(near, static_cast<uint8_t>(SC::PoisonArrow), { 47, 40 }, arrow);
	EXPECT_EQ(arrow.poe2.commandShot, static_cast<uint8_t>(SC::PoisonArrow));
	arrow._misource = 0;
	arrow._midam = 100;
	arrow.position.tile = { 47, 40 };
	OnCommandShotLanded(arrow);
	EXPECT_EQ(CommandGroundCount(*MyPlayer, SC::PoisonArrow), 1);
	// Once a second, the strongest poison only: 20% of the arrow's hit a second, for 2 seconds.
	Tick(20);
	EXPECT_GT(enemy.poe2.poisonTicks, 0);
	EXPECT_EQ(enemy.poe2.poisonDamagePerTick * PoisonCloudPoisonTicks, 100 * 64 * 40 / 100);
	EXPECT_EQ(outside.poe2.poisonTicks, 0);
	Tick(PoisonCloudTicks);
	EXPECT_EQ(CommandGroundCount(*MyPlayer, SC::PoisonArrow), 0);
}

TEST_F(Poe2SummonCommandsTest, FrostShellWrapsMinionsNearTheTarget)
{
	Socket(PersistentSkill::SkeletalFrostMages);
	Monster &lich = PlaceMinion(0, { 41, 40 }, PersistentSkill::SkeletalFrostMages);
	Monster &near = PlaceMinion(2, { 44, 40 }, PersistentSkill::SkeletalFrostMages);
	Monster &far = PlaceMinion(3, { 52, 40 }, PersistentSkill::SkeletalFrostMages);
	Monster &enemy = PlaceEnemy(1, { 45, 40 });
	ASSERT_EQ(IssueSummonCommand(*MyPlayer, Request(PersistentSkill::SkeletalFrostMages, { 44, 40 })), CommandResult::Used);
	EXPECT_EQ(lich.mode, MonsterMode::RangedAttack);
	EXPECT_EQ(lich.poe2.commandAction, static_cast<uint8_t>(SC::FrostShell));
	RunCommandAction(lich);
	EXPECT_EQ(near.poe2.commandShellTicks, FrostShellTicks);
	EXPECT_EQ(lich.poe2.commandShellTicks, FrostShellTicks);
	EXPECT_EQ(far.poe2.commandShellTicks, 0);
	EXPECT_EQ(CommandShellDamagePercent(near), 70);
	EXPECT_EQ(CommandShellDamagePercent(far), 100);
	// The ice bites back at what hits a shelled minion.
	const Fixed26_6 life = enemy.hitPoints;
	OnShelledMinionHit(enemy, near);
	EXPECT_LT(enemy.hitPoints, life);
	const Fixed26_6 after = enemy.hitPoints;
	OnShelledMinionHit(enemy, far);
	EXPECT_EQ(enemy.hitPoints, after);
	// It lasts 6 seconds.
	Tick(FrostShellTicks);
	EXPECT_EQ(near.poe2.commandShellTicks, 0);
}

TEST_F(Poe2SummonCommandsTest, ACommandWhoseMinionIsCutOffStillGoesOff)
{
	Socket(PersistentSkill::SkeletalFrostMages);
	Monster &lich = PlaceMinion(0, { 41, 40 }, PersistentSkill::SkeletalFrostMages);
	ASSERT_EQ(IssueSummonCommand(*MyPlayer, Request(PersistentSkill::SkeletalFrostMages, { 41, 40 })), CommandResult::Used);
	// Hit mid-cast: its action frame never comes.
	lich.mode = MonsterMode::HitRecovery;
	lich.poe2.commandAction = 0;
	Tick(CommandFallbackTicks - 1);
	EXPECT_EQ(lich.poe2.commandShellTicks, 0);
	Tick(1);
	EXPECT_GT(lich.poe2.commandShellTicks, 0);
}

TEST_F(Poe2SummonCommandsTest, ThunderCallStrikesFallenSkeletonsBones)
{
	Socket(PersistentSkill::SkeletalStormMages);
	Monster &lich = PlaceMinion(0, { 40, 42 }, PersistentSkill::SkeletalStormMages);
	Monster &enemy = PlaceEnemy(1, { 46, 41 });
	Monster &spared = PlaceEnemy(3, { 52, 52 });
	EXPECT_EQ(CheckSummonCommand(*MyPlayer, Request(PersistentSkill::SkeletalStormMages, { 46, 40 })), CommandResult::NoBones);

	// A skeleton falls there; a non-skeleton leaves nothing.
	Monster &fallen = PlaceMinion(2, { 46, 40 }, PersistentSkill::SkeletalWarriors);
	OnCommandMinionDied(*MyPlayer, fallen);
	ActiveMonsterCount--;
	ASSERT_EQ(CheckSummonCommand(*MyPlayer, Request(PersistentSkill::SkeletalStormMages, { 46, 40 })), CommandResult::Used);
	// Too far from the target: no strike.
	EXPECT_EQ(CheckSummonCommand(*MyPlayer, Request(PersistentSkill::SkeletalStormMages, { 46, 50 })), CommandResult::NoBones);

	ASSERT_EQ(IssueSummonCommand(*MyPlayer, Request(PersistentSkill::SkeletalStormMages, { 46, 40 })), CommandResult::Used);
	const Fixed26_6 life = enemy.hitPoints;
	RunCommandAction(lich);
	EXPECT_LT(enemy.hitPoints, life);
	EXPECT_EQ(spared.hitPoints, spared.maxHitPoints);
	EXPECT_EQ(CommandGroundCount(*MyPlayer, SC::ThunderCall), 1);
	// Shocked ground: 20% more damage taken while standing on it.
	Tick(1);
	EXPECT_GT(enemy.poe2.shockTicks, 0);
	EXPECT_EQ(spared.poe2.shockTicks, 0);
	Tick(ShockedGroundTicks);
	EXPECT_EQ(CommandGroundCount(*MyPlayer, SC::ThunderCall), 0);
}

TEST_F(Poe2SummonCommandsTest, ThunderCallStrikesAtMostSix)
{
	Socket(PersistentSkill::SkeletalStormMages);
	Monster &lich = PlaceMinion(0, { 40, 42 }, PersistentSkill::SkeletalStormMages);
	Monster &fallen = PlaceMinion(1, { 46, 40 }, PersistentSkill::SkeletalWarriors);
	for (int i = 0; i < 9; i++) {
		fallen.position.tile = { 44 + (i % 3), 39 + (i / 3) };
		OnCommandMinionDied(*MyPlayer, fallen);
	}
	ActiveMonsterCount = 1;
	ASSERT_EQ(IssueSummonCommand(*MyPlayer, Request(PersistentSkill::SkeletalStormMages, { 45, 40 })), CommandResult::Used);
	RunCommandAction(lich);
	EXPECT_EQ(CommandGroundCount(*MyPlayer, SC::ThunderCall), ThunderCallMaxStrikes);
}

TEST_F(Poe2SummonCommandsTest, BoneFrenzyHitsHarderFasterAndCostsLife)
{
	Socket(PersistentSkill::SkeletalReavers);
	Monster &captain = PlaceMinion(0, { 41, 40 }, PersistentSkill::SkeletalReavers, 100);
	const int32_t blow = MinionBlowRaw(captain);
	ASSERT_EQ(IssueSummonCommand(*MyPlayer, Request(PersistentSkill::SkeletalReavers, { 45, 40 })), CommandResult::Used);
	EXPECT_EQ(captain.poe2.commandFrenzyTicks, BoneFrenzyTicks);
	EXPECT_EQ(CommandMinionDamagePercent(captain), 130);
	EXPECT_NEAR(MinionBlowRaw(captain), blow * 13 / 10, 2);
	// 30% faster attacks: three extra turns in ten while it swings.
	captain.mode = MonsterMode::MeleeAttack;
	int extra = 0;
	for (int i = 0; i < 10; i++)
		extra += CommandExtraTick(captain) ? 1 : 0;
	EXPECT_EQ(extra, 3);
	// 2% of its life a second.
	Tick(20);
	EXPECT_EQ(captain.hitPoints, Fixed26_6::fromInt(98));
	Tick(BoneFrenzyTicks);
	EXPECT_EQ(captain.poe2.commandFrenzyTicks, 0);
	EXPECT_EQ(CommandMinionDamagePercent(captain), 100);
	EXPECT_GE(captain.hitPoints, Fixed26_6::fromInt(92));
}

TEST_F(Poe2SummonCommandsTest, GraveHowlWeakensBySlidingScale)
{
	Socket(PersistentSkill::SkeletalBrutes);
	Monster &brute = PlaceMinion(0, { 40, 42 }, PersistentSkill::SkeletalBrutes);
	Monster &normal = PlaceEnemy(1, { 42, 42 });
	Monster &magic = PlaceEnemy(2, { 41, 44 });
	MakeRarity(magic, MonsterRarity::Magic);
	Monster &rare = PlaceEnemy(3, { 43, 43 });
	MakeRarity(rare, MonsterRarity::Rare);
	Monster &boss = PlaceEnemy(4, { 40, 45 });
	boss.uniqueType = UniqueMonsterType::Garbud;
	Monster &far = PlaceEnemy(5, { 50, 42 });
	const int blow = ScaleMonsterBlow(normal, 6400);
	ASSERT_EQ(IssueSummonCommand(*MyPlayer, Request(PersistentSkill::SkeletalBrutes, { 42, 42 })), CommandResult::Used);
	EXPECT_EQ(brute.poe2.commandAction, static_cast<uint8_t>(SC::GraveHowl));
	RunCommandAction(brute);
	EXPECT_EQ(CommandWeakenedDamagePercent(normal), 80);
	EXPECT_EQ(CommandWeakenedDamagePercent(magic), 80);
	EXPECT_EQ(CommandWeakenedDamagePercent(rare), 85);
	EXPECT_EQ(CommandWeakenedDamagePercent(boss), 90);
	EXPECT_EQ(CommandWeakenedDamagePercent(far), 100);
	EXPECT_EQ(ScaleMonsterBlow(normal, 6400), blow * 80 / 100);
	Tick(GraveHowlTicks);
	EXPECT_EQ(CommandWeakenedDamagePercent(normal), 100);
	EXPECT_EQ(CommandWeakenedDamagePercent(boss), 100);
}

TEST_F(Poe2SummonCommandsTest, SlidingScaleMatchesTheTable)
{
	EXPECT_EQ(CommandTierScales[static_cast<size_t>(CommandTier::Normal)].staggerTicks, 20);
	EXPECT_EQ(CommandTierScales[static_cast<size_t>(CommandTier::Magic)].staggerTicks, 15);
	EXPECT_EQ(CommandTierScales[static_cast<size_t>(CommandTier::Rare)].staggerTicks, 10);
	EXPECT_EQ(CommandTierScales[static_cast<size_t>(CommandTier::Boss)].staggerTicks, 5);
	EXPECT_TRUE(CommandTierScales[static_cast<size_t>(CommandTier::Normal)].interrupts);
	EXPECT_TRUE(CommandTierScales[static_cast<size_t>(CommandTier::Magic)].interrupts);
	EXPECT_FALSE(CommandTierScales[static_cast<size_t>(CommandTier::Rare)].interrupts);
	EXPECT_FALSE(CommandTierScales[static_cast<size_t>(CommandTier::Boss)].interrupts);
	EXPECT_EQ(CommandTierScales[static_cast<size_t>(CommandTier::Rare)].weakenPercent, 15);
	EXPECT_EQ(CommandTierScales[static_cast<size_t>(CommandTier::Boss)].weakenPercent, 10);
}

TEST_F(Poe2SummonCommandsTest, QuakeStompStaggersByRarityAndBossesShrugItOff)
{
	Socket(PersistentSkill::SummonGolem);
	Monster &golem = PlaceMinion(0, { 40, 42 }, PersistentSkill::SummonGolem, 500);
	Monster &normal = PlaceEnemy(1, { 41, 43 });
	Monster &rare = PlaceEnemy(2, { 42, 42 });
	MakeRarity(rare, MonsterRarity::Rare);
	Monster &boss = PlaceEnemy(3, { 40, 44 });
	boss.uniqueType = UniqueMonsterType::Garbud;
	Monster &far = PlaceEnemy(4, { 45, 42 });
	ASSERT_EQ(FindGolemForPlayer(*MyPlayer), &golem);
	ASSERT_EQ(IssueSummonCommand(*MyPlayer, Request(PersistentSkill::SummonGolem, { 41, 43 })), CommandResult::Used);
	EXPECT_EQ(golem.poe2.commandAction, static_cast<uint8_t>(SC::QuakeStomp));
	RunCommandAction(golem);
	// 100% of its hit on everything within 3 tiles, bosses too.
	const int32_t hit = MinionBlowRaw(golem);
	EXPECT_LT(normal.hitPoints, normal.maxHitPoints);
	EXPECT_LT(boss.hitPoints, boss.maxHitPoints);
	EXPECT_LE(boss.maxHitPoints.raw() - boss.hitPoints.raw(), hit * 2);
	EXPECT_EQ(far.hitPoints, far.maxHitPoints);
	EXPECT_EQ(normal.poe2.commandStaggerTicks, 20);
	EXPECT_TRUE(normal.poe2.commandStaggerHard);
	EXPECT_EQ(rare.poe2.commandStaggerTicks, 10);
	EXPECT_FALSE(rare.poe2.commandStaggerHard);
	EXPECT_EQ(boss.poe2.commandStaggerTicks, 5);
	EXPECT_EQ(boss.poe2.commandStaggerImmuneTicks, CommandStaggerImmuneTicks);
	EXPECT_EQ(far.poe2.commandStaggerTicks, 0);
	// A staggered monster can't act; a rare finishes the swing it's in.
	EXPECT_TRUE(CommandStaggerHolds(normal));
	rare.mode = MonsterMode::MeleeAttack;
	EXPECT_FALSE(CommandStaggerHolds(rare));
	rare.mode = MonsterMode::Stand;
	EXPECT_TRUE(CommandStaggerHolds(rare));

	// For 10 seconds the boss ignores Command staggers, but still takes the stomp.
	Tick(SummonCommandCooldownTicks(SC::QuakeStomp));
	EXPECT_EQ(boss.poe2.commandStaggerTicks, 0);
	const Fixed26_6 life = boss.hitPoints;
	ASSERT_EQ(IssueSummonCommand(*MyPlayer, Request(PersistentSkill::SummonGolem, { 41, 43 })), CommandResult::Used);
	RunCommandAction(golem);
	EXPECT_LT(boss.hitPoints, life);
	EXPECT_EQ(boss.poe2.commandStaggerTicks, 0);
	EXPECT_EQ(normal.poe2.commandStaggerTicks, 20);
	Tick(CommandStaggerImmuneTicks);
	EXPECT_TRUE(CommandStagger(boss));
}

TEST_F(Poe2SummonCommandsTest, BloodGiftHealsCappedAndNotFromAHurtGolem)
{
	Socket(PersistentSkill::BloodGolem);
	Monster &golem = PlaceMinion(0, { 41, 40 }, PersistentSkill::BloodGolem, 1000);
	Player &player = *MyPlayer;
	player._pMaxHP = player._pMaxHPBase = Fixed26_6::fromInt(100);
	player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(50);
	ASSERT_EQ(IssueSummonCommand(player, Request(PersistentSkill::BloodGolem, { 41, 40 })), CommandResult::Used);
	RunCommandAction(golem);
	// It gives 15% of its life; the hero takes at most 10% of theirs.
	EXPECT_EQ(golem.hitPoints, Fixed26_6::fromInt(850));
	EXPECT_EQ(player._pHitPoints, Fixed26_6::fromInt(60));

	Tick(SummonCommandCooldownTicks(SC::BloodGift));
	golem.hitPoints = Fixed26_6::fromInt(290);
	EXPECT_EQ(CheckSummonCommand(player, Request(PersistentSkill::BloodGolem, { 41, 40 })), CommandResult::GolemHurt);
	golem.hitPoints = Fixed26_6::fromInt(300);
	EXPECT_EQ(CheckSummonCommand(player, Request(PersistentSkill::BloodGolem, { 41, 40 })), CommandResult::Used);
}

TEST_F(Poe2SummonCommandsTest, GolemsBlazeBurnsInTheIgniteSlot)
{
	Socket(PersistentSkill::FireGolem);
	Monster &golem = PlaceMinion(0, { 40, 42 }, PersistentSkill::FireGolem, 500);
	Monster &near = PlaceEnemy(1, { 41, 43 });
	Monster &far = PlaceEnemy(2, { 44, 42 });
	ASSERT_EQ(IssueSummonCommand(*MyPlayer, Request(PersistentSkill::FireGolem, { 41, 43 })), CommandResult::Used);
	RunCommandAction(golem);
	EXPECT_EQ(MyPlayer->poe2.commandState.blazeTicks, GolemsBlazeTicks);
	Tick(1);
	EXPECT_GT(near.poe2.igniteTicks, 0);
	EXPECT_EQ(far.poe2.igniteTicks, 0);
	// 60% of its hit a second, as fire.
	EXPECT_NEAR(near.poe2.igniteDamagePerTick * 20, MinionBlowRaw(golem) * GolemsBlazePercentPerSecond / 100, 20);
	// A stronger burn already there stays.
	near.poe2.igniteDamagePerTick = 100000;
	Tick(20);
	EXPECT_EQ(near.poe2.igniteDamagePerTick, 100000);
	Tick(GolemsBlazeTicks);
	EXPECT_EQ(MyPlayer->poe2.commandState.blazeTicks, 0);
}

TEST_F(Poe2SummonCommandsTest, FireSweepBreathesAnArc)
{
	Socket(PersistentSkill::Guardian);
	EXPECT_EQ(CheckSummonCommand(*MyPlayer, Request(PersistentSkill::Guardian, { 44, 40 })), CommandResult::NoMinions);
	Missile &guardian = Missiles.emplace_back();
	guardian._mitype = MissileID::Guardian;
	guardian._miAnimType = MissileGraphicID::None;
	guardian._misource = 0;
	guardian._micaster = TARGET_MONSTERS;
	guardian._mispllvl = 10;
	guardian.position.tile = { 40, 40 };
	Monster &ahead = PlaceEnemy(1, { 43, 40 });
	Monster &aside = PlaceEnemy(2, { 43, 42 });
	Monster &behind = PlaceEnemy(3, { 37, 40 });
	Monster &beyond = PlaceEnemy(4, { 46, 40 });
	Monster &boss = PlaceEnemy(5, { 42, 40 });
	boss.uniqueType = UniqueMonsterType::Garbud;
	ASSERT_EQ(IssueSummonCommand(*MyPlayer, Request(PersistentSkill::Guardian, { 43, 40 }, 1)), CommandResult::Used);
	EXPECT_LT(ahead.hitPoints, ahead.maxHitPoints);
	EXPECT_LT(aside.hitPoints, aside.maxHitPoints);
	EXPECT_LT(boss.hitPoints, boss.maxHitPoints);
	EXPECT_EQ(behind.hitPoints, behind.maxHitPoints);
	EXPECT_EQ(beyond.hitPoints, beyond.maxHitPoints);
}

TEST_F(Poe2SummonCommandsTest, SwitchSwapsWithTheShadow)
{
	Socket(PersistentSkill::ShadowWarrior);
	const SummonCommandRequest request = Request(PersistentSkill::ShadowWarrior, { 44, 40 });
	EXPECT_EQ(CheckSummonCommand(*MyPlayer, request), CommandResult::NoMinions);
	OnShadowAppeared(*MyPlayer, { 43, 41 });
	ASSERT_EQ(CheckSummonCommand(*MyPlayer, request), CommandResult::Used);
	ASSERT_EQ(IssueSummonCommand(*MyPlayer, request), CommandResult::Used);
	EXPECT_EQ(Point(MyPlayer->position.tile), Point(43, 41));
	// The shadow takes the old spot.
	EXPECT_EQ(Point(MyPlayer->poe2.commandState.shadowX, MyPlayer->poe2.commandState.shadowY), Point(40, 40));
	// The shadow fades after a while.
	Tick(ShadowStandTicks);
	EXPECT_EQ(MyPlayer->poe2.commandState.shadowTicks, 0);
}

TEST_F(Poe2SummonCommandsTest, LastRitesRushesTheTargetHittingHarder)
{
	Socket(PersistentSkill::Revive);
	Monster &revived = Place(0, { 41, 40 }, 0, 100);
	revived.flags |= MFLAG_GOLEM;
	revived.goalVar3 = static_cast<int8_t>(MyPlayerId);
	revived.poe2.revived = true;
	Monster &enemy = PlaceEnemy(1, { 46, 40 });
	ASSERT_EQ(IssueSummonCommand(*MyPlayer, Request(PersistentSkill::Revive, { 46, 40 }, 1)), CommandResult::Used);
	EXPECT_EQ(revived.poe2.commandRitesTicks, LastRitesTicks);
	EXPECT_EQ(revived.enemy, static_cast<int>(enemy.getId()));
	EXPECT_EQ(CommandMinionDamagePercent(revived), 140);
	// 20% faster on its way: an extra step in five.
	revived.mode = MonsterMode::MoveNorthwards;
	int extra = 0;
	for (int i = 0; i < 10; i++)
		extra += CommandExtraTick(revived) ? 1 : 0;
	EXPECT_EQ(extra, 2);
	Tick(LastRitesTicks);
	EXPECT_EQ(CommandMinionDamagePercent(revived), 100);
}

TEST_F(Poe2SummonCommandsTest, PackRushLeapsEveryWolfInReach)
{
	Socket(PersistentSkill::WolfPack);
	Monster &near = PlaceMinion(0, { 41, 40 }, PersistentSkill::WolfPack);
	Monster &far = PlaceMinion(2, { 40, 55 }, PersistentSkill::WolfPack);
	EXPECT_EQ(CheckSummonCommand(*MyPlayer, Request(PersistentSkill::WolfPack, { 46, 40 })), CommandResult::NoTarget);
	Monster &enemy = PlaceEnemy(1, { 46, 40 });
	ASSERT_EQ(IssueSummonCommand(*MyPlayer, Request(PersistentSkill::WolfPack, { 46, 40 }, 1)), CommandResult::Used);
	EXPECT_LE(near.position.tile.WalkingDistance(enemy.position.tile), 1);
	EXPECT_EQ(Point(far.position.tile), Point(40, 55));
	EXPECT_EQ(near.poe2.commandAction, static_cast<uint8_t>(SC::PackRush));
	const Fixed26_6 life = enemy.hitPoints;
	RunCommandAction(near);
	EXPECT_LT(enemy.hitPoints, life);
	EXPECT_NEAR(life.raw() - enemy.hitPoints.raw(), MinionBlowRaw(near) * PackRushPercent / 100, 64);
}

TEST_F(Poe2SummonCommandsTest, CommandsComeFromSocketedGemsForTheTray)
{
	Player &player = *MyPlayer;
	Socket(PersistentSkill::SkeletalMages, 1, 2);
	Socket(PersistentSkill::SkeletalWarriors, 1, 3);
	Socket(PersistentSkill::Guardian, 1, 4);
	EXPECT_EQ(CommandSockets(player), (std::vector<size_t> { 2, 4 }));
	EXPECT_EQ(SocketCommand(player, 2), SC::FlameVolley);
	EXPECT_EQ(SocketCommand(player, 3), SC::None);
	// Nothing goes on the tray by itself.
	for (const TrayBinding &binding : player.poe2.tray)
		EXPECT_FALSE(binding.isCommand());

	EXPECT_TRUE(TrayBindingFits(TrayBinding::ForCommand(2), TraySlot::Key1));
	EXPECT_FALSE(TrayBindingFits(TrayBinding::ForCommand(NumSkillSlots), TraySlot::Key1));
	BindTraySlot(player, TraySlot::Key1, TrayBinding::ForCommand(2));
	EXPECT_EQ(player.poe2.tray[static_cast<size_t>(TraySlot::Key1)], TrayBinding::ForCommand(2));
	// One slot each, like a gem.
	BindTraySlot(player, TraySlot::Key2, TrayBinding::ForCommand(2));
	EXPECT_EQ(player.poe2.tray[static_cast<size_t>(TraySlot::Key2)], TrayBinding::ForCommand(2));
	EXPECT_NE(player.poe2.tray[static_cast<size_t>(TraySlot::Key1)], TrayBinding::ForCommand(2));
	// It goes with its gem.
	SocketSkill(player, 2, SkillSlot::ForPersistent(PersistentSkill::SkeletalWarriors));
	EXPECT_NE(player.poe2.tray[static_cast<size_t>(TraySlot::Key2)], TrayBinding::ForCommand(2));
	// Another minion gem with a Command keeps the slot, now its Command.
	BindTraySlot(player, TraySlot::Key3, TrayBinding::ForCommand(4));
	SocketSkill(player, 4, SkillSlot::ForPersistent(PersistentSkill::SkeletalSnipers));
	EXPECT_EQ(player.poe2.tray[static_cast<size_t>(TraySlot::Key3)], TrayBinding::ForCommand(4));
	EXPECT_EQ(SocketCommand(player, 4), SC::PoisonArrow);
}

TEST_F(Poe2SummonCommandsTest, CommandIsSearchableAndInTheDetails)
{
	// The tooltip lines the search reads are only written with a screen.
	HeadlessMode = false;
	for (const auto &[gem, name] : Expected) {
		Item item;
		ASSERT_TRUE(CreateSkillGemItem(item, SkillSlot::ForPersistent(gem), 1)) << name;
		const std::string text = ItemSearchText(item);
		EXPECT_TRUE(ItemSearchTextMatches(text, ParseItemSearch("command"))) << name;
		std::string lower { name };
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		EXPECT_NE(text.find(lower), std::string::npos) << name;
	}
	Item warriors;
	ASSERT_TRUE(CreateSkillGemItem(warriors, SkillSlot::ForPersistent(PersistentSkill::SkeletalWarriors), 1));
	EXPECT_FALSE(ItemSearchTextMatches(ItemSearchText(warriors), ParseItemSearch("command:")));

	Socket(PersistentSkill::SkeletalMages);
	bool found = false;
	for (const SkillDetailLine &line : SkillDetails(*MyPlayer, 0))
		found = found || line.label == "Command: Flame Volley";
	EXPECT_TRUE(found);
	HeadlessMode = true;
}

/** The minion a gem's Command is sized from, at a gem level: a skeleton, the golem or a revived zombie. */
Monster &BalanceMinion(PersistentSkill gem, int gemLevel)
{
	Monster &minion = Monsters[0];
	minion = {};
	minion.uniqueType = UniqueMonsterType::None;
	minion.flags = MFLAG_GOLEM;
	minion.goalVar3 = 0;
	minion.position.tile = { 41, 40 };
	minion.maxHitPoints = minion.hitPoints = Fixed26_6::fromInt(500);
	MinionRiseStats stats {};
	if (IsAnyOf(gem, PersistentSkill::SummonGolem, PersistentSkill::FireGolem)) {
		minion.levelType = static_cast<int>(MinionGemTable().size() + 1);
		stats = GolemRiseStats(*MyPlayer, gemLevel);
	} else if (gem == PersistentSkill::Revive) {
		minion.levelType = 0;
		minion.poe2.revived = true;
		minion.minDamage = minion.data().minDamage;
		minion.maxDamage = minion.data().maxDamage;
		return minion;
	} else {
		for (size_t i = 0; i < MinionGemTable().size(); i++) {
			if (MinionGemTable()[i].skill == gem)
				minion.levelType = static_cast<int>(i) + 1;
		}
		stats = SkeletonMinionRiseStats(*MyPlayer, FindMinionGem(gem)->type, gemLevel);
	}
	minion.minDamage = static_cast<uint8_t>(stats.minDamage);
	minion.maxDamage = static_cast<uint8_t>(stats.maxDamage);
	return minion;
}

TEST_F(Poe2SummonCommandsTest, CommandsAddAtMostAQuarterOverTheirCooldown)
{
	// The numbers file's check: over one cooldown, a damaging Command adds at most 25% of what its summon deals
	// normally in that time, at gems 1 and 20. The ones over are reported to the balance thread, not lowered here;
	// the test pins them so a change to either side shows.
	const std::array<SummonCommand, 4> reported { SC::PoisonArrow, SC::ThunderCall, SC::BoneFrenzy, SC::FireSweep };
	// Flame Volley is just over at gem 1 only, where the bolt is a few points and the 150% rounds up (25.1%).
	const auto isReported = [&](SummonCommand command, int gemLevel) {
		return std::find(reported.begin(), reported.end(), command) != reported.end() || (command == SC::FlameVolley && gemLevel == 1);
	};
	for (const int gemLevel : { 1, 20 }) {
		for (const SummonCommand command : AllSummonCommands()) {
			const PersistentSkill gem = CommandGem(command);
			MyPlayer->poe2.skillSlots[0] = SkillSlot::ForPersistent(gem);
			MyPlayer->poe2.slotLevels[0] = static_cast<uint8_t>(gemLevel);
			if (IsAnyOf(command, SC::FrostShell, SC::GraveHowl, SC::BloodGift, SC::Switch)) {
				// No damage: a defence or a move.
				const Monster &minion = BalanceMinion(gem == PersistentSkill::BloodGolem || gem == PersistentSkill::ShadowWarrior ? PersistentSkill::SkeletalWarriors : gem, gemLevel);
				EXPECT_EQ(CommandDamageOverCooldown(command, minion, gemLevel).added, 0) << SummonCommandName(command);
				continue;
			}
			const Monster &minion = BalanceMinion(gem == PersistentSkill::Guardian ? PersistentSkill::SkeletalWarriors : gem, gemLevel);
			const CommandDamageShare share = CommandDamageOverCooldown(command, minion, gemLevel);
			ASSERT_GT(share.normal, 0) << SummonCommandName(command);
			ASSERT_GT(share.added, 0) << SummonCommandName(command);
			const bool withinQuarter = share.added * 100 <= share.normal * 25;
			const int64_t permille = share.added * 1000 / share.normal;
			std::printf("[balance] gem %2d %-14s +%lld.%lld%%\n", gemLevel, std::string(SummonCommandName(command)).c_str(), static_cast<long long>(permille / 10), static_cast<long long>(permille % 10));
			if (isReported(command, gemLevel))
				EXPECT_FALSE(withinQuarter) << SummonCommandName(command) << " at gem " << gemLevel << " is within it now: take it off the reported list";
			else
				EXPECT_TRUE(withinQuarter) << SummonCommandName(command) << " at gem " << gemLevel;
		}
	}
}

class Poe2SummonCommandsSaveTest : public Poe2SummonCommandsTest {
public:
	void SetUp() override
	{
		Poe2SummonCommandsTest::SetUp();
		paths::SetPrefPath(paths::BasePath());
	}
};

TEST_F(Poe2SummonCommandsSaveTest, TrayCommandSurvivesASaveOnlyWithItsGem)
{
	LoadCoreArchives();
	LoadGameArchives();
	if (!HaveMainData())
		GTEST_SKIP() << "MPQ assets (spawn.mpq or DIABDAT.MPQ) not found - skipping test";
	UnloadModArchives();
	LoadModArchives({ { "hf", "poe2" } });
	LoadItemData();
	LoadSpellData();
	const std::string savePath = paths::BasePath() + "single_0.sv";
	RemoveFile(savePath.c_str());
	_uiheroinfo info {};
	info.heroclass = HeroClass::Sorcerer;
	CopyUtf8(info.name, "Commander", sizeof(info.name));
	ASSERT_TRUE(pfile_ui_save_create(&info));
	gSaveNumber = 0;
	Player &player = *MyPlayer;
	pfile_read_player_from_save(0, player);
	LoadPoe2HeroData(player);

	player.poe2.skillSlots[2] = SkillSlot::ForPersistent(PersistentSkill::SkeletalMages);
	player.poe2.skillSlots[3] = SkillSlot::ForPersistent(PersistentSkill::WolfPack);
	player.poe2.tray[static_cast<size_t>(TraySlot::Key1)] = TrayBinding::ForCommand(2);
	player.poe2.tray[static_cast<size_t>(TraySlot::Key2)] = TrayBinding::ForCommand(3);
	// A Command whose socket holds no minion gem (an edited save) is dropped.
	player.poe2.tray[static_cast<size_t>(TraySlot::Key3)] = TrayBinding::ForCommand(5);
	pfile_write_hero(/*writeGameData=*/false);

	pfile_read_player_from_save(0, player);
	LoadPoe2HeroData(player);
	EXPECT_EQ(player.poe2.tray[static_cast<size_t>(TraySlot::Key1)], TrayBinding::ForCommand(2));
	EXPECT_EQ(player.poe2.tray[static_cast<size_t>(TraySlot::Key2)], TrayBinding::ForCommand(3));
	EXPECT_FALSE(player.poe2.tray[static_cast<size_t>(TraySlot::Key3)].isCommand());
	// Nothing about the Commands themselves is saved: the cooldowns start clear.
	EXPECT_EQ(SummonCommandCooldownLeft(player, SC::FlameVolley), 0);
	RemoveFile(savePath.c_str());
}

} // namespace
} // namespace devilution
