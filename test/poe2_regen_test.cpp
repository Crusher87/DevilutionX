#include <gtest/gtest.h>

#include <string>

#include "DiabloUI/diabloui.h"
#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "init.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "loadsave.h"
#include "menu.h"
#include "pack.h"
#include "pfile.h"
#include "player.h"
#include "poe2/passive_tree.hpp"
#include "poe2/regen.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"
#include "utils/file_util.h"
#include "utils/paths.h"
#include "utils/utf8.hpp"

namespace devilution {
namespace {

class Poe2RegenTest : public ::testing::Test {
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
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		UnloadModArchives();
	}

	static void RunSeconds(Player &player, int seconds)
	{
		for (int i = 0; i < seconds * 20; i++)
			ApplyRegen(player);
	}

	/** @brief A hero with no gear or tree at a level, whose saved bases are what levelling up gave them (Diablo's rules). */
	static Player &NakedHero(HeroClass heroClass, int level)
	{
		Player &player = *MyPlayer;
		player = {};
		player.poe2.clear();
		player._pClass = heroClass;
		player.setCharacterLevel(static_cast<uint8_t>(level));
		const ClassAttributes &attributes = player.getClassAttributes();
		player._pBaseStr = player._pStrength = attributes.baseStr;
		player._pBaseMag = player._pMagic = attributes.baseMag;
		player._pBaseDex = player._pDexterity = attributes.baseDex;
		player._pBaseVit = player._pVitality = attributes.baseVit;
		player._pMaxHPBase = player._pHPBase = player.calculateBaseLife();
		player._pMaxManaBase = player._pManaBase = player.calculateBaseMana();
		CalcPlrInv(player, false);
		return player;
	}
};

struct ClassPool {
	HeroClass heroClass;
	int atLevel1;
	int atLevel50;
};

// POE2's naked mana pools: 30 + 4 a level + 2 a starting Magic point, counting at least 10 Magic.
constexpr ClassPool ManaPools[] = {
	{ HeroClass::Warrior, 54, 250 },
	{ HeroClass::Rogue, 64, 260 },
	{ HeroClass::Sorcerer, 104, 300 },
	{ HeroClass::Monk, 64, 260 },
	{ HeroClass::Bard, 74, 270 },
	{ HeroClass::Barbarian, 54, 250 },
};

TEST_F(Poe2RegenTest, ManaRegeneratesFourPercentPerSecond)
{
	Player &player = *MyPlayer;
	player._pMaxMana = Fixed26_6::fromInt(100);
	player._pMaxManaBase = Fixed26_6::fromInt(100);
	player._pMana = Fixed26_6::fromInt(0);
	player._pManaBase = Fixed26_6::fromInt(0);

	RunSeconds(player, 5);
	EXPECT_EQ(player._pMana.whole(), 20);
	EXPECT_EQ(player._pManaBase.whole(), 20);

	RunSeconds(player, 60);
	EXPECT_EQ(player._pMana, player._pMaxMana);
}

TEST_F(Poe2RegenTest, EveryClassGetsPoe2ManaPool)
{
	for (const ClassPool &pool : ManaPools) {
		Player &player = NakedHero(pool.heroClass, 1);
		EXPECT_EQ(player._pMaxMana, Fixed26_6::fromInt(pool.atLevel1)) << static_cast<int>(pool.heroClass);
		EXPECT_EQ(player._pMana, player._pMaxMana);

		NakedHero(pool.heroClass, 50);
		EXPECT_EQ(player._pMaxMana, Fixed26_6::fromInt(pool.atLevel50)) << static_cast<int>(pool.heroClass);
		// The saved base stays on Diablo's rules, so the hero file and multiplayer checks don't change.
		EXPECT_EQ(player._pMaxManaBase, player.calculateBaseMana());

		// Diablo keeps its own pools.
		gbIsPoe2 = false;
		CalcPlrInv(player, false);
		EXPECT_EQ(player._pMaxMana, player.calculateBaseMana()) << static_cast<int>(pool.heroClass);
		gbIsPoe2 = true;
	}

	// Magic on gear and the tree gives 2 mana a point for every class.
	EXPECT_EQ(ManaFromItemMagic(NakedHero(HeroClass::Warrior, 1), 10), 20);
	EXPECT_EQ(ManaFromItemMagic(NakedHero(HeroClass::Sorcerer, 1), 10), 20);
	gbIsPoe2 = false;
	EXPECT_EQ(ManaFromItemMagic(NakedHero(HeroClass::Warrior, 1), 10), 10);
	gbIsPoe2 = true;
}

TEST_F(Poe2RegenTest, ShrineLossesStayInTheSavedMana)
{
	Player &player = NakedHero(HeroClass::Sorcerer, 10);
	const Fixed26_6 pool = player._pMaxMana;
	player._pMaxManaBase -= Fixed26_6::fromInt(7);
	player._pManaBase = player._pMaxManaBase;
	CalcPlrInv(player, false);
	EXPECT_EQ(player._pMaxMana, pool - Fixed26_6::fromInt(7));
	EXPECT_EQ(player._pMana, player._pMaxMana);
}

TEST_F(Poe2RegenTest, LifeGainsTwelvePerLevel)
{
	for (const ClassPool &pool : ManaPools) {
		Player &player = NakedHero(pool.heroClass, 1);
		const Fixed26_6 atLevel1 = player._pMaxHP;
		EXPECT_EQ(atLevel1, player.calculateBaseLife());

		NakedHero(pool.heroClass, 2);
		EXPECT_EQ(player._pMaxHP, atLevel1 + Fixed26_6::fromInt(12)) << static_cast<int>(pool.heroClass);

		NakedHero(pool.heroClass, 50);
		EXPECT_EQ(player._pMaxHP, atLevel1 + Fixed26_6::fromInt(12 * 49)) << static_cast<int>(pool.heroClass);
		// Current life rises with it like any bonus, and the saved base keeps Diablo's gain.
		EXPECT_EQ(player._pHitPoints, player._pMaxHP);
		EXPECT_EQ(player._pMaxHPBase, player.calculateBaseLife());

		gbIsPoe2 = false;
		CalcPlrInv(player, false);
		EXPECT_EQ(player._pMaxHP, player.calculateBaseLife()) << static_cast<int>(pool.heroClass);
		gbIsPoe2 = true;
	}

	// A wounded hero stays wounded by the same amount.
	Player &player = NakedHero(HeroClass::Warrior, 30);
	player._pHPBase -= Fixed26_6::fromInt(25);
	CalcPlrInv(player, false);
	EXPECT_EQ(player._pHitPoints, player._pMaxHP - Fixed26_6::fromInt(25));
}

TEST_F(Poe2RegenTest, LifeCanPass2000)
{
	Player &player = NakedHero(HeroClass::Warrior, 50);
	player._pMaxHPBase = player._pHPBase = Fixed26_6::fromInt(2500);
	player._pMaxManaBase = player._pManaBase = Fixed26_6::fromInt(2500);
	CalcPlrInv(player, false);
	EXPECT_EQ(player._pMaxHP, Fixed26_6::fromInt(2500) + Poe2LevelLife(player));
	EXPECT_EQ(player._pHitPoints, player._pMaxHP);
	EXPECT_EQ(player._pMaxMana, Fixed26_6::fromInt(2500) + Poe2LevelMana(player));
	EXPECT_GT(player._pMaxMana.whole(), 2000);

	// Diablo still caps both at 2,000.
	gbIsPoe2 = false;
	CalcPlrInv(player, false);
	EXPECT_EQ(player._pMaxHP.whole(), 2000);
	EXPECT_EQ(player._pMaxMana.whole(), 2000);
	gbIsPoe2 = true;
}

TEST_F(Poe2RegenTest, SorcererPaysFull)
{
	for (const HeroClass heroClass : { HeroClass::Sorcerer, HeroClass::Warrior, HeroClass::Monk }) {
		Player &player = NakedHero(heroClass, 1);
		SocketSkill(player, 0, SkillSlot::ForSpell(SpellID::Firebolt), 1);
		SocketSkill(player, 1, SkillSlot::ForSpell(SpellID::ChargedBolt), 1);
		SocketSkill(player, 2, SkillSlot::ForSpell(SpellID::ManaShield), 1);
		SocketSkill(player, 3, SkillSlot::ForSpell(SpellID::Healing), 1);
		SocketSkill(player, 4, SkillSlot::ForSpell(SpellID::RuneOfFire), 1);
		// Every class pays the same: no class discount, and no Diablo minimum (Charged Bolt's 6, Mana Shield's 33).
		// Firebolt: 4 x120% for its speed (half of its damage_pct 140), 4.8, to the nearest point.
		EXPECT_EQ(GetManaAmount(player, SpellID::Firebolt), Fixed26_6::fromInt(5)) << static_cast<int>(heroClass);
		EXPECT_EQ(GetManaAmount(player, SpellID::ChargedBolt), Fixed26_6::fromInt(4)) << static_cast<int>(heroClass);
		EXPECT_EQ(GetManaAmount(player, SpellID::ManaShield), Fixed26_6::fromInt(15)) << static_cast<int>(heroClass);
		EXPECT_EQ(GetManaAmount(player, SpellID::Healing), Fixed26_6::fromInt(8)) << static_cast<int>(heroClass);
		// A rune costs its own base, not the whole base pool.
		EXPECT_EQ(GetManaAmount(player, SpellID::RuneOfFire), Fixed26_6::fromInt(8)) << static_cast<int>(heroClass);

		// Damage spells get 5x dearer by gem level 20, Healing 3x like other utility gems.
		SocketSkill(player, 0, SkillSlot::ForSpell(SpellID::Firebolt), 20);
		SocketSkill(player, 3, SkillSlot::ForSpell(SpellID::Healing), 20);
		EXPECT_EQ(GetManaAmount(player, SpellID::Firebolt), Fixed26_6::fromInt(24)) << static_cast<int>(heroClass);
		EXPECT_EQ(GetManaAmount(player, SpellID::Healing), Fixed26_6::fromInt(24)) << static_cast<int>(heroClass);
	}

	// Healing no longer gets dearer with the character's level.
	Player &player = NakedHero(HeroClass::Warrior, 50);
	SocketSkill(player, 3, SkillSlot::ForSpell(SpellID::Healing), 1);
	EXPECT_EQ(GetManaAmount(player, SpellID::Healing), Fixed26_6::fromInt(8));

	// Diablo keeps the Sorcerer's discount.
	gbIsPoe2 = false;
	NakedHero(HeroClass::Sorcerer, 1);
	player._pSplLvl[static_cast<int8_t>(SpellID::Firebolt)] = 1;
	const Fixed26_6 sorcerer = GetManaAmount(player, SpellID::Firebolt);
	NakedHero(HeroClass::Warrior, 1);
	player._pSplLvl[static_cast<int8_t>(SpellID::Firebolt)] = 1;
	EXPECT_LT(sorcerer, GetManaAmount(player, SpellID::Firebolt));
	gbIsPoe2 = true;
}

TEST_F(Poe2RegenTest, SorcererStarterBoltsCostFour)
{
	// Curtis 2026-10-23: Firebolt, Ice Bolt, Holy Bolt and Charged Bolt all cost 4 mana at gem level 1. Since 2026-10-27
	// a skill costs more by half of what its speed adds to its damage: Firebolt (140%) 4.8, so 5; Holy Bolt (117%) 4.36
	// and Charged Bolt (109%) 4.2 stay at 4.
	for (const HeroClass heroClass : { HeroClass::Sorcerer, HeroClass::Warrior }) {
		Player &player = NakedHero(heroClass, 1);
		SocketSkill(player, 0, SkillSlot::ForSpell(SpellID::Firebolt), 1);
		SocketSkill(player, 1, SkillSlot::ForSpell(SpellID::HolyBolt), 1);
		SocketSkill(player, 2, SkillSlot::ForSpell(SpellID::ChargedBolt), 1);
		SocketSkill(player, 3, SkillSlot::ForAttack(WeaponSkill::IceBolt), 1);
		EXPECT_EQ(GetManaAmount(player, SpellID::Firebolt), Fixed26_6::fromInt(5)) << static_cast<int>(heroClass);
		EXPECT_EQ(GetManaAmount(player, SpellID::HolyBolt), Fixed26_6::fromInt(4)) << static_cast<int>(heroClass);
		EXPECT_EQ(GetManaAmount(player, SpellID::ChargedBolt), Fixed26_6::fromInt(4)) << static_cast<int>(heroClass);
		EXPECT_EQ(WeaponSkillManaCost(player, WeaponSkill::IceBolt), 4) << static_cast<int>(heroClass);
	}
}

TEST_F(Poe2RegenTest, BarbarianCountsTenMagicForMana)
{
	// Curtis 2026-10-23: the Barbarian starts with no Magic but gets the Warrior's mana pool (both count as 10 Magic).
	ASSERT_EQ(NakedHero(HeroClass::Barbarian, 1)._pBaseMag, 0);
	ASSERT_EQ(NakedHero(HeroClass::Warrior, 1)._pBaseMag, Poe2MinManaMagic);
	for (const int level : { 1, 2, 10, 25, 50 }) {
		const Fixed26_6 warrior = NakedHero(HeroClass::Warrior, level)._pMaxMana;
		const Fixed26_6 barbarian = NakedHero(HeroClass::Barbarian, level)._pMaxMana;
		EXPECT_EQ(barbarian, warrior) << level;
		EXPECT_EQ(barbarian, Fixed26_6::fromInt(Poe2BaseMana + (Poe2ManaPerLevel * level) + (Poe2ManaPerMagic * Poe2MinManaMagic))) << level;
	}
	// At the same level and the same Magic, the two pools match too.
	for (const int level : { 1, 25, 50 }) {
		Player &player = NakedHero(HeroClass::Warrior, level);
		player._pBaseMag = player._pMagic = 30;
		player._pMaxManaBase = player._pManaBase = player.calculateBaseMana();
		CalcPlrInv(player, false);
		const Fixed26_6 warrior = player._pMaxMana;
		NakedHero(HeroClass::Barbarian, level);
		player._pBaseMag = player._pMagic = 30;
		player._pMaxManaBase = player._pManaBase = player.calculateBaseMana();
		CalcPlrInv(player, false);
		EXPECT_EQ(player._pMaxMana, warrior) << level;
	}
	// Magic from gear still adds 2 a point, as for the Warrior.
	EXPECT_EQ(ManaFromItemMagic(NakedHero(HeroClass::Barbarian, 1), 10), 20);
}

TEST_F(Poe2RegenTest, MultiplayerValidationStillPasses)
{
	Players.resize(2);
	MyPlayer = &Players[0];
	Player &player = NakedHero(HeroClass::Warrior, 30);
	// Away from the level being shown, so unpacking loads no graphics.
	currlevel = 0;
	player.plrlevel = 1;
	player.position.tile = { 50, 50 };
	ASSERT_GT(player._pMaxHP, player._pMaxHPBase);
	ASSERT_GT(player._pMaxMana, player._pMaxManaBase);

	PlayerNetPack packed;
	PackNetPlayer(packed, player);
	Players[1] = {};
	EXPECT_TRUE(UnPackNetPlayer(packed, Players[1]));
	EXPECT_EQ(Players[1]._pMaxHP, player._pMaxHP);
	EXPECT_EQ(Players[1]._pMaxMana, player._pMaxMana);
}

TEST_F(Poe2RegenTest, IncreasedManaRegenScalesTheRate)
{
	Player &player = *MyPlayer;
	player._pMaxMana = Fixed26_6::fromInt(100);
	player._pMana = Fixed26_6::fromInt(0);
	player.poe2.manaRegenBonus = 50;

	RunSeconds(player, 5);
	EXPECT_EQ(player._pMana.whole(), 30);
}

TEST_F(Poe2RegenTest, LifeOnlyRegeneratesFromGear)
{
	Player &player = *MyPlayer;
	player._pMaxHP = Fixed26_6::fromInt(100);
	player._pHitPoints = Fixed26_6::fromInt(50);
	player._pHPBase = Fixed26_6::fromInt(50);

	RunSeconds(player, 5);
	EXPECT_EQ(player._pHitPoints.whole(), 50);

	player.poe2.lifeRegen = 3;
	RunSeconds(player, 5);
	EXPECT_EQ(player._pHitPoints.whole(), 65);
	EXPECT_EQ(player._pHPBase.whole(), 65);

	RunSeconds(player, 60);
	EXPECT_EQ(player._pHitPoints, player._pMaxHP);
}

TEST_F(Poe2RegenTest, NothingRegeneratesOutsidePoe2OrWhenDead)
{
	Player &player = *MyPlayer;
	player._pMaxMana = Fixed26_6::fromInt(100);
	player._pMana = Fixed26_6::fromInt(0);
	player.poe2.lifeRegen = 10;
	player._pMaxHP = Fixed26_6::fromInt(100);
	player._pHitPoints = Fixed26_6::fromInt(0);

	leveltype = DTYPE_CATHEDRAL;
	RunSeconds(player, 5);
	EXPECT_EQ(player._pHitPoints.whole(), 0);
	leveltype = DTYPE_TOWN;

	player._pHitPoints = Fixed26_6::fromInt(50);
	gbIsPoe2 = false;
	RunSeconds(player, 5);
	EXPECT_EQ(player._pMana.whole(), 0);
	EXPECT_EQ(player._pHitPoints.whole(), 50);
}

TEST_F(Poe2RegenTest, RegenAffixesRollAndShowOnItems)
{
	int lifeAffixes = 0;
	int manaAffixes = 0;
	for (const PLStruct &suffix : ItemSuffixes) {
		if (suffix.power.type == IPL_LIFEREGEN) lifeAffixes++;
		if (suffix.power.type == IPL_MANAREGEN) manaAffixes++;
	}
	EXPECT_EQ(lifeAffixes, 4);
	EXPECT_EQ(manaAffixes, 3);

	Item ring {};
	ring._iPLLifeRegen = 5;
	ring._iPLManaRegen = 30;
	EXPECT_EQ(PrintItemPower(IPL_LIFEREGEN, ring).str(), "regenerate 5 life per second");
	EXPECT_EQ(PrintItemPower(IPL_MANAREGEN, ring).str(), "30% increased mana regeneration");
}

class Poe2LifeManaSaveTest : public Poe2RegenTest {
public:
	void SetUp() override
	{
		Poe2RegenTest::SetUp();
		MyPlayerId = 0;
		gbVanilla = false;
		gbIsHellfireSaveGame = true;
		LoadMonsterData();
		LoadPassiveTreeData();
		paths::SetPrefPath(paths::BasePath());
	}

	void TearDown() override
	{
		gbIsHellfireSaveGame = false;
		Poe2RegenTest::TearDown();
		LoadPassiveTreeData();
	}
};

TEST_F(Poe2LifeManaSaveTest, LoadingTwiceGivesTheSameHero)
{
	LoadCoreArchives();
	LoadGameArchives();
	if (!HaveMainData())
		GTEST_SKIP() << "MPQ assets (spawn.mpq or DIABDAT.MPQ) not found - skipping test";
	UnloadModArchives();
	LoadModArchives({ { "hf", "poe2" } });
	LoadItemData();
	LoadSpellData();
	LoadPassiveTreeData();
	const std::string savePath = paths::BasePath() + "single_0.sv";
	RemoveFile(savePath.c_str());
	_uiheroinfo info {};
	info.heroclass = HeroClass::Warrior;
	CopyUtf8(info.name, "Twice", sizeof(info.name));
	ASSERT_TRUE(pfile_ui_save_create(&info));
	gSaveNumber = 0;
	pfile_read_player_from_save(0, *MyPlayer);
	LoadPoe2HeroData(*MyPlayer);

	// A level 30 hero, a little wounded and short of mana, as levelling up left the saved bases.
	Player &player = *MyPlayer;
	player.setCharacterLevel(30);
	player._pMaxHPBase = player.calculateBaseLife();
	player._pHPBase = player._pMaxHPBase - Fixed26_6::fromInt(10);
	player._pMaxManaBase = player.calculateBaseMana();
	player._pManaBase = player._pMaxManaBase - Fixed26_6::fromInt(5);
	CalcPlrInv(player, false);
	const Fixed26_6 maxLife = player._pMaxHP;
	const Fixed26_6 life = player._pHitPoints;
	const Fixed26_6 maxMana = player._pMaxMana;
	const Fixed26_6 mana = player._pMana;
	ASSERT_GT(maxLife, player._pMaxHPBase);
	pfile_write_hero(/*writeGameData=*/false);

	// Nothing is stored for POE2's life and mana, so loading and saving again never adds it twice.
	for (int load = 0; load < 2; load++) {
		pfile_read_player_from_save(0, player);
		LoadPoe2HeroData(player);
		CalcPlrInv(player, false);
		EXPECT_EQ(player._pMaxHPBase, player.calculateBaseLife()) << load;
		EXPECT_EQ(player._pMaxManaBase, player.calculateBaseMana()) << load;
		EXPECT_EQ(player._pMaxHP, maxLife) << load;
		EXPECT_EQ(player._pHitPoints, life) << load;
		EXPECT_EQ(player._pMaxMana, maxMana) << load;
		EXPECT_EQ(player._pMana, mana) << load;
		pfile_write_hero(/*writeGameData=*/false);
	}
	RemoveFile(savePath.c_str());
}

} // namespace
} // namespace devilution
