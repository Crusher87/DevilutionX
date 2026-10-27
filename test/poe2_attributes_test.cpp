#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/passive_tree.hpp"
#include "poe2/unique_effects.hpp"
#include "spells.h"
#include "tables/monstdat.h"
#include "storm/storm_net.hpp"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

/** Curtis 2026-10-23: no class has a ceiling on its attributes. */
class Poe2AttributesTest : public ::testing::Test {
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
		LoadPassiveTreeData();
		SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		LoadPassiveTreeData();
		UnloadModArchives();
	}

	/** A hero of the class at the level, strong enough for any base, with nothing equipped. */
	static void MakeHero(Player &player, HeroClass heroClass, int level)
	{
		player = {};
		player.poe2.clear();
		player._pClass = heroClass;
		player.setCharacterLevel(level);
		player._pBaseStr = 200;
		player._pBaseMag = 200;
		player._pBaseDex = 100;
		player._pBaseVit = 80;
		player._pMaxHPBase = player._pHPBase = Fixed26_6::fromInt(100);
		player._pMaxManaBase = player._pManaBase = Fixed26_6::fromInt(20);
	}

	static void Recalculate(Player &player)
	{
		// Keeps CalcPlrInv from loading art for the armour graphic.
		CalcPlrItemVals(player, false);
	}

	/** Puts on the first body armour of the type. */
	static void WearChest(Player &player, ItemType type)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].itype == type && AllItemsList[i].iLoc == ILOC_ARMOR) {
				Item &item = player.InvBody[INVLOC_CHEST];
				InitializeItem(item, static_cast<_item_indexes>(i));
				item._iAC = 10;
				item._iStatFlag = true;
				return;
			}
		}
		FAIL() << "no body armour of the type";
	}
};

constexpr HeroClass AllClasses[] = { HeroClass::Warrior, HeroClass::Rogue, HeroClass::Sorcerer, HeroClass::Monk, HeroClass::Bard, HeroClass::Barbarian };

TEST_F(Poe2AttributesTest, EveryClassCanRaiseEveryAttributeToTheHeroFileLimit)
{
	for (const HeroClass heroClass : { HeroClass::Warrior, HeroClass::Rogue, HeroClass::Sorcerer, HeroClass::Monk, HeroClass::Bard, HeroClass::Barbarian }) {
		Player &player = *MyPlayer;
		player._pClass = heroClass;
		player.setCharacterLevel(1);
		player._pBaseStr = player._pStrength = 10;
		player._pBaseMag = player._pMagic = 10;
		player._pBaseDex = player._pDexterity = 10;
		player._pBaseVit = player._pVitality = 10;
		for (const CharacterAttribute attribute : { CharacterAttribute::Strength, CharacterAttribute::Magic, CharacterAttribute::Dexterity, CharacterAttribute::Vitality })
			EXPECT_EQ(player.GetMaximumAttributeValue(attribute), 255);

		ModifyPlrStr(player, 300);
		ModifyPlrMag(player, 300);
		ModifyPlrDex(player, 300);
		ModifyPlrVit(player, 300);
		EXPECT_EQ(player._pBaseStr, 255);
		EXPECT_EQ(player._pBaseMag, 255);
		EXPECT_EQ(player._pBaseDex, 255);
		EXPECT_EQ(player._pBaseVit, 255);
	}
}

TEST_F(Poe2AttributesTest, ClassicModeKeepsTheClassCeilings)
{
	gbIsPoe2 = false;
	MyPlayer->_pClass = HeroClass::Sorcerer;
	EXPECT_EQ(MyPlayer->GetMaximumAttributeValue(CharacterAttribute::Strength), 45);
	gbIsPoe2 = true;
}

/** Pick 7 / decision 10A: Diablo's Strength (and Dexterity) x level damage is gone for every class but the Barbarian. */
TEST_F(Poe2AttributesTest, StrengthDamageDoesNotGrowWithLevel)
{
	Player &player = *MyPlayer;
	for (const HeroClass heroClass : AllClasses) {
		if (heroClass == HeroClass::Barbarian)
			continue; // on hold, see BarbarianKeepsHisLevelBonusesForNow
		for (const int level : { 1, 40 }) {
			MakeHero(player, heroClass, level);
			Recalculate(player);
			EXPECT_EQ(player._pDamageMod, 0) << static_cast<int>(heroClass) << " at " << level;
		}
	}

	gbIsPoe2 = false;
	MakeHero(player, HeroClass::Warrior, 40);
	Recalculate(player);
	EXPECT_EQ(player._pDamageMod, 40 * 200 / 100) << "Diablo keeps Strength x level / 100";
}

/** Decision 10A: the Warrior's class crit doesn't grow with level; every class crits at the base 5%. */
TEST_F(Poe2AttributesTest, WarriorCritIsFlatFive)
{
	Player &player = *MyPlayer;
	for (const HeroClass heroClass : AllClasses) {
		MakeHero(player, heroClass, 40);
		EXPECT_EQ(AttackCritChance(player), BaseAttackCritPercent) << static_cast<int>(heroClass);
	}

	gbIsPoe2 = false;
	MakeHero(player, HeroClass::Warrior, 40);
	EXPECT_EQ(AttackCritChance(player), 40) << "Diablo's Warrior crits at clvl %";
	MakeHero(player, HeroClass::Sorcerer, 40);
	EXPECT_EQ(AttackCritChance(player), 0);
}

/** Curtis 2026-10-23: the Barbarian is on hold, so he keeps Iron Skin, Natural Resistance and Rage's growth. */
TEST_F(Poe2AttributesTest, BarbarianKeepsHisLevelBonusesForNow)
{
	Player &player = *MyPlayer;
	MakeHero(player, HeroClass::Barbarian, 1);
	Recalculate(player);
	const int armour = player._pIAC;
	const int fire = player._pFireResist;

	MakeHero(player, HeroClass::Barbarian, 40);
	Recalculate(player);
	EXPECT_EQ(player._pIAC, armour + 40 / 4) << "Iron Skin";
	EXPECT_EQ(player._pFireResist, fire + 39) << "Natural Resistance";
	EXPECT_GT(player._pDamageMod, 0);

	const int strength = player._pStrength;
	player._pSpellFlags = SpellFlag::RageActive;
	Recalculate(player);
	EXPECT_EQ(player._pStrength, strength + 2 * 40) << "Rage still grows";
}

/** Decision 43A: the Monk's armour from level in light and medium body armour is gone. */
TEST_F(Poe2AttributesTest, MonkHasNoLevelArmour)
{
	Player &player = *MyPlayer;
	for (const ItemType type : { ItemType::LightArmor, ItemType::MediumArmor }) {
		MakeHero(player, HeroClass::Monk, 1);
		WearChest(player, type);
		Recalculate(player);
		const int armour = player._pIAC;
		MakeHero(player, HeroClass::Monk, 40);
		WearChest(player, type);
		Recalculate(player);
		EXPECT_EQ(player._pIAC, armour) << static_cast<int>(type);
	}

	gbIsPoe2 = false;
	MakeHero(player, HeroClass::Monk, 1);
	WearChest(player, ItemType::LightArmor);
	Recalculate(player);
	const int diabloArmour = player._pIAC;
	MakeHero(player, HeroClass::Monk, 40);
	WearChest(player, ItemType::LightArmor);
	Recalculate(player);
	EXPECT_EQ(player._pIAC, diabloArmour + 2 * 39) << "Diablo's Monk keeps +2 armour a level";
}

/** Pick 7 / audit N8: block doesn't gain +2 a character level (the monster-level term is step 4's). */
TEST_F(Poe2AttributesTest, BlockHasNoLevelTerm)
{
	Player &player = *MyPlayer;
	MakeHero(player, HeroClass::Warrior, 1);
	Recalculate(player);
	const int atOne = player.GetBlockChance();
	EXPECT_EQ(atOne, player.GetBlockChance(false));
	MakeHero(player, HeroClass::Warrior, 40);
	Recalculate(player);
	EXPECT_EQ(player.GetBlockChance(), atOne);

	gbIsPoe2 = false;
	EXPECT_EQ(player.GetBlockChance(), player.GetBlockChance(false) + 2 * 40) << "Diablo keeps the level term";
}

/** Plan step 14 (Curtis 2026-10-27): Diablo's sword and mace rule against Undead and Animals is off in POE2. */
TEST_F(Poe2AttributesTest, WeaponsDealTheSameToEveryMonsterClass)
{
	EXPECT_FALSE(Poe2WeaponVsMonsterClass);
	for (const ItemType hand : { ItemType::Sword, ItemType::Mace, ItemType::Staff, ItemType::None }) {
		for (const MonsterClass monsterClass : { MonsterClass::Undead, MonsterClass::Animal, MonsterClass::Demon })
			EXPECT_EQ(WeaponVsMonsterClassDamage(hand, monsterClass, 101), 101);
	}
	// Diablo mode keeps it.
	gbIsPoe2 = false;
	EXPECT_EQ(WeaponVsMonsterClassDamage(ItemType::Sword, MonsterClass::Undead, 101), 51);
	EXPECT_EQ(WeaponVsMonsterClassDamage(ItemType::Mace, MonsterClass::Undead, 101), 151);
	EXPECT_EQ(WeaponVsMonsterClassDamage(ItemType::Mace, MonsterClass::Animal, 101), 51);
	EXPECT_EQ(WeaponVsMonsterClassDamage(ItemType::Sword, MonsterClass::Animal, 101), 151);
	EXPECT_EQ(WeaponVsMonsterClassDamage(ItemType::Staff, MonsterClass::Undead, 101), 101);
	EXPECT_EQ(WeaponVsMonsterClassDamage(ItemType::Sword, MonsterClass::Demon, 101), 101);
	gbIsPoe2 = true;
}

} // namespace
} // namespace devilution
