/**
 * @file poe2_granted_bolt_test.cpp
 *
 * The caster weapons rework's granted level and spell-based Staff Bolt (caster-weapons-plan-v2.md 6.6, 6.8 and 6.10,
 * step S13): what a left click does with each pair of hands, the level a weapon grants its skill at, and the Staff Bolt
 * of a new staff or wand, a spell rolled from Magic and that level.
 */
#include <algorithm>
#include <string_view>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/bases.hpp"
#include "poe2/default_attack.hpp"
#include "poe2/gem_levels.hpp"
#include "poe2/item_stats.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/spell_base.hpp"
#include "poe2/weapon_kinds.hpp"
#include "spells.h"
#include "tables/itemdat.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2GrantedBoltTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(2);
		MyPlayer = &Players[0];
		MyPlayerId = 0;
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
		LoadModifierData();
		for (Player &player : Players)
			Reset(player, HeroClass::Sorcerer);
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		LoadModifierData();
		UnloadModArchives();
		Players.resize(1);
	}

	static void Reset(Player &player, HeroClass heroClass)
	{
		player = {};
		player.poe2.clear();
		player._pClass = heroClass;
		player.setCharacterLevel(85);
		player._pBaseStr = player._pStrength = 200;
		player._pBaseMag = player._pMagic = 200;
		player._pBaseDex = player._pDexterity = 200;
		player._pBaseVit = player._pVitality = 50;
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

	/** @brief Puts a normal item of the base in a hand (empty name: nothing), at a POE2 item level when one is given. */
	static Item &Hold(Player &player, inv_body_loc hand, std::string_view name, int itemLevel = 0)
	{
		Item &item = player.InvBody[hand];
		item = {};
		if (name.empty())
			return item;
		SetLootRarity(item, ITEM_QUALITY_NORMAL);
		SetRndSeed(1);
		SetupAllItems(player, item, Row(name), AdvanceRndSeed(), 30, 1, /*onlygood=*/false, /*pregen=*/false, 0, /*forceNotUnique=*/true);
		if (itemLevel > 0)
			SetPoe2ItemLevel(item, itemLevel);
		item._iStatFlag = true;
		return item;
	}

	static DefaultAttack Click(HeroClass heroClass, std::string_view left, std::string_view right = {})
	{
		Player &player = *MyPlayer;
		Reset(player, heroClass);
		Hold(player, INVLOC_HAND_LEFT, left);
		Hold(player, INVLOC_HAND_RIGHT, right);
		CalcPlrInv(player, false);
		return DefaultAttackFor(player);
	}
};

TEST_F(Poe2GrantedBoltTest, GrantedGemLevelFollowsPoe2sCharacterLevels)
{
	EXPECT_EQ(GrantedGemLevel(0), 1);
	EXPECT_EQ(GrantedGemLevel(1), 1);
	EXPECT_EQ(GrantedGemLevel(2), 1);
	EXPECT_EQ(GrantedGemLevel(3), 2);
	EXPECT_EQ(GrantedGemLevel(11), 4);
	EXPECT_EQ(GrantedGemLevel(33), 9);
	EXPECT_EQ(GrantedGemLevel(65), 15);
	EXPECT_EQ(GrantedGemLevel(82), 18);
	EXPECT_EQ(GrantedGemLevel(100), 20);
}

TEST_F(Poe2GrantedBoltTest, LeftClickForEachPairOfHands)
{
	for (const HeroClass heroClass : { HeroClass::Warrior, HeroClass::Sorcerer, HeroClass::Monk }) {
		// A new staff or wand casts its Staff Bolt, for a Monk too.
		EXPECT_EQ(Click(heroClass, "Ashen Staff"), DefaultAttack::StaffBolt);
		EXPECT_EQ(Click(heroClass, "Spriggan Staff"), DefaultAttack::StaffBolt);
		// The Gelid Staff casts its own skill since S14 (poe2_granted_skills_test).
		EXPECT_EQ(Click(heroClass, "Gelid Staff"), DefaultAttack::Granted);
		EXPECT_EQ(Click(heroClass, "Voltaic Staff"), DefaultAttack::StaffBolt);
		EXPECT_EQ(Click(heroClass, "Withered Wand"), DefaultAttack::StaffBolt);
		EXPECT_EQ(Click(heroClass, "Withered Wand", "Wooden Tower Shield"), DefaultAttack::StaffBolt);
		// A wand beside a sceptre is the main weapon, whichever hand either is in.
		EXPECT_EQ(Click(heroClass, "Withered Wand", "Fine Rattling Sceptre"), DefaultAttack::StaffBolt);
		EXPECT_EQ(Click(heroClass, "Fine Rattling Sceptre", "Withered Wand"), DefaultAttack::StaffBolt);
		// A new sceptre has no attack: alone, paired or beside a shield the hero punches.
		EXPECT_EQ(Click(heroClass, "Fine Rattling Sceptre"), DefaultAttack::Punch);
		EXPECT_EQ(Click(heroClass, "Fine Rattling Sceptre", "Lupine Sceptre"), DefaultAttack::Punch);
		EXPECT_EQ(Click(heroClass, "Fine Rattling Sceptre", "Wooden Tower Shield"), DefaultAttack::Punch);
		// Beside a martial one-hander, that weapon's attack.
		EXPECT_EQ(Click(heroClass, "Fine Rattling Sceptre", "Shortsword"), DefaultAttack::SwordStrike);
		// A quarterstaff strikes for every class, old and new ones alike.
		EXPECT_EQ(Click(heroClass, "Fine Long Quarterstaff"), DefaultAttack::StaffStrike);
		EXPECT_EQ(Click(heroClass, "Fine Wrapped Quarterstaff"), DefaultAttack::StaffStrike);
		// The staves and sceptres from before the rework keep their attack.
		EXPECT_EQ(Click(heroClass, "Focused Pilgrim Staff"), heroClass == HeroClass::Monk ? DefaultAttack::StaffStrike : DefaultAttack::StaffBolt);
		EXPECT_EQ(Click(heroClass, "Quick Bone Sceptre"), DefaultAttack::StaffBolt);
	}
	// A Monk with only a sceptre punches with his fists' damage.
	Click(HeroClass::Monk, "Fine Rattling Sceptre");
	EXPECT_TRUE(FightsWithBareFists(*MyPlayer));
}

TEST_F(Poe2GrantedBoltTest, OutsidePoe2ClicksAreAsBefore)
{
	gbIsPoe2 = false;
	UnloadModArchives();
	LoadModArchives({ { "hf" } });
	LoadItemData();
	LoadModifierData();
	EXPECT_EQ(Click(HeroClass::Sorcerer, "Short Staff"), DefaultAttack::StaffBolt);
	EXPECT_EQ(Click(HeroClass::Monk, "Short Staff"), DefaultAttack::StaffStrike);
	EXPECT_EQ(Click(HeroClass::Warrior, "Mace"), DefaultAttack::MaceSmash);
	EXPECT_EQ(MyPlayer->poe2.granted[0].baseLevel, 0);
	EXPECT_FALSE(FiresGrantedBolt(*MyPlayer));
}

TEST_F(Poe2GrantedBoltTest, AHeldCasterWeaponGrantsAtItsItemLevel)
{
	Player &player = *MyPlayer;
	const SkillSlot bolt = SkillSlot::ForDefault(static_cast<uint8_t>(DefaultAttack::StaffBolt));
	Hold(player, INVLOC_HAND_LEFT, "Ashen Staff", 33);
	CalcPlrInv(player, false);
	EXPECT_EQ(player.poe2.granted[0].skill, bolt);
	EXPECT_EQ(player.poe2.granted[0].baseLevel, 9);
	EXPECT_EQ(player.poe2.granted[0].hand, 0);
	EXPECT_EQ(player.poe2.granted[1].baseLevel, 0);
	EXPECT_EQ(player.poe2.grantedLevel(bolt), 9);
	EXPECT_EQ(player.poe2.effectiveLevel(bolt), 9);
	EXPECT_EQ(GrantedBoltLevel(player), 9);
	EXPECT_TRUE(FiresGrantedBolt(player));

	// Gear's spell and fire levels raise it; cold levels don't.
	player.poe2.itemStats[static_cast<size_t>(Poe2ItemStat::SpellSkillLevels)] = 2;
	EXPECT_EQ(GrantedBoltLevel(player), 11);
	player.poe2.itemStats[static_cast<size_t>(Poe2ItemStat::FireSkillLevels)] = 1;
	player.poe2.itemStats[static_cast<size_t>(Poe2ItemStat::ColdSkillLevels)] = 3;
	EXPECT_EQ(GrantedBoltLevel(player), 12);
	EXPECT_EQ(player.poe2.grantedLevel(bolt), 12);
	player.poe2.itemStats[static_cast<size_t>(Poe2ItemStat::SpellSkillLevels)] = 30;
	EXPECT_EQ(GrantedBoltLevel(player), MaxGemLevel);

	// Without an item level the base's drop level counts.
	Hold(player, INVLOC_HAND_LEFT, "Spriggan Staff");
	SetPoe2ItemLevel(player.InvBody[INVLOC_HAND_LEFT], 0);
	CalcPlrInv(player, false);
	EXPECT_EQ(player.poe2.granted[0].baseLevel, GrantedGemLevel(11));

	// A wand whose own skill comes later still has its bolt's level, from the hand it's in; a sceptre beside it gets
	// its own entry.
	Hold(player, INVLOC_HAND_LEFT, "Fine Rattling Sceptre", 1);
	Hold(player, INVLOC_HAND_RIGHT, "Withered Wand", 65);
	CalcPlrInv(player, false);
	EXPECT_EQ(player.poe2.granted[0].hand, 0);
	EXPECT_EQ(player.poe2.granted[0].baseLevel, 1);
	EXPECT_EQ(player.poe2.granted[1].hand, 1);
	EXPECT_EQ(player.poe2.granted[1].baseLevel, 15);
	player.poe2.itemStats.fill(0);
	EXPECT_EQ(GrantedBoltLevel(player), 15);

	// One the hero can't use grants nothing.
	Hold(player, INVLOC_HAND_RIGHT, "Dueling Wand", 65);
	CalcPlrInv(player, false);
	EXPECT_EQ(player.poe2.granted[1].baseLevel, 15);
	player._pBaseMag = player._pMagic = 0;
	CalcPlrInv(player, false);
	ASSERT_FALSE(player.InvBody[INVLOC_HAND_RIGHT]._iStatFlag);
	EXPECT_EQ(player.poe2.granted[1].baseLevel, 0);

	// Martial weapons and the older staves grant nothing.
	Reset(player, HeroClass::Sorcerer);
	Hold(player, INVLOC_HAND_LEFT, "Focused Pilgrim Staff", 40);
	CalcPlrInv(player, false);
	EXPECT_EQ(player.poe2.granted[0].baseLevel, 0);
	EXPECT_FALSE(FiresGrantedBolt(player));
	EXPECT_EQ(GrantedBoltLevel(player), 0);
}

TEST_F(Poe2GrantedBoltTest, ASocketedGemBeatsAGrant)
{
	Player &player = *MyPlayer;
	const SkillSlot skill = SkillSlot::ForAttack(WeaponSkill::IceBolt);
	EXPECT_EQ(player.poe2.effectiveLevel(skill), 0);
	player.poe2.granted[0] = { skill, 7, 0 };
	EXPECT_EQ(player.poe2.skillLevel(skill), 0);
	EXPECT_EQ(player.poe2.grantedLevel(skill), 7);
	EXPECT_EQ(player.poe2.effectiveLevel(skill), 7);
	// Gem levels on gear add once.
	player.poe2.itemStats[static_cast<size_t>(Poe2ItemStat::ProjectileSkillLevels)] = 1;
	player.poe2.itemStats[static_cast<size_t>(Poe2ItemStat::MeleeSkillLevels)] = 1;
	const int bonus = ItemSkillLevelBonus(player.poe2, skill);
	EXPECT_EQ(player.poe2.effectiveLevel(skill), 7 + bonus);
	// Socketed at level 2, the gem's level counts even though the grant's is higher.
	player.poe2.skillSlots[0] = skill;
	player.poe2.slotLevels[0] = 2;
	EXPECT_EQ(player.poe2.effectiveLevel(skill), 2 + bonus);
	// An empty slot never matches an empty grant.
	player.poe2.granted[1] = { SkillSlot {}, 5, 1 };
	EXPECT_EQ(player.poe2.grantedLevel(SkillSlot {}), 0);
}

TEST_F(Poe2GrantedBoltTest, GrantedBoltIsASpell)
{
	Player &player = *MyPlayer;
	for (const int itemLevel : { 1, 33, 82 }) {
		Hold(player, INVLOC_HAND_LEFT, "Ashen Staff", itemLevel);
		CalcPlrInv(player, false);
		const int level = GrantedBoltLevel(player);
		ASSERT_EQ(level, GrantedGemLevel(itemLevel));
		const int percent = GrantedBoltPercent(player, level);
		// 61% of the spell base at level 1, along the Fireball curve to 312% at 20.
		EXPECT_EQ(percent, GrantedStaffBoltPercent * FireballCurvePercent(GrantedCurveEndAt20, level) / 100) << itemLevel;
		const int low = SpellBaseMin(player, level) * percent / 100;
		const int high = SpellBaseMax(player, level) * percent / 100;
		SetRndSeed(5);
		int seenLow = high;
		int seenHigh = low;
		for (int i = 0; i < 500; i++) {
			const int damage = RollGrantedBoltDamage(player);
			EXPECT_GE(damage, low);
			EXPECT_LE(damage, high);
			seenLow = std::min(seenLow, damage);
			seenHigh = std::max(seenHigh, damage);
		}
		EXPECT_EQ(seenLow, low) << itemLevel;
		EXPECT_EQ(seenHigh, high) << itemLevel;
	}
	EXPECT_EQ(GrantedBoltPercent(player, 1), GrantedStaffBoltPercent);
	EXPECT_EQ(FireballCurvePercent(GrantedCurveEndAt20, 20), GrantedCurveEndAt20);

	// Magic raises it as it does a spell; the staff's own damage is nothing.
	Hold(player, INVLOC_HAND_LEFT, "Ashen Staff", 1);
	CalcPlrInv(player, false);
	EXPECT_EQ(player._pIMinDam, 1);
	const int before = SpellBaseMin(player, 1);
	player._pMagic += 80;
	EXPECT_EQ(SpellBaseMin(player, 1), before + 10);
	// Even with no Magic at all the bolt hits for something.
	player._pMagic = 0;
	SetRndSeed(1);
	EXPECT_GE(RollGrantedBoltDamage(player), 1);
}

TEST_F(Poe2GrantedBoltTest, LegacyStavesKeepTheirBolt)
{
	Player &player = *MyPlayer;
	Hold(player, INVLOC_HAND_LEFT, "Focused Pilgrim Staff", 20);
	CalcPlrInv(player, false);
	ASSERT_TRUE(FiresStaffBolt(player));
	EXPECT_FALSE(FiresGrantedBolt(player));
	// Its bolt is rolled from the staff's damage as before.
	SetRndSeed(3);
	const int legacy = RollStaffBoltDamage(player);
	const int base = player._pIMinDam;
	EXPECT_GE(legacy, (base * StaffBoltDamagePercent / 100) + (player._pMagic * StaffBoltMagicPercent / 100));
}

TEST_F(Poe2GrantedBoltTest, AnotherHeroInMultiplayerRollsTheSameBolt)
{
	// Everything the bolt needs is on the hero's items and attributes, so every machine works out the same numbers for
	// a hero that isn't its own.
	gbIsMultiplayer = true;
	for (Player &player : Players) {
		Hold(player, INVLOC_HAND_LEFT, "Spriggan Staff", 40);
		CalcPlrInv(player, false);
	}
	const Player &mine = Players[0];
	const Player &theirs = Players[1];
	EXPECT_EQ(GrantedBoltLevel(theirs), GrantedBoltLevel(mine));
	EXPECT_EQ(GrantedBoltLevel(theirs), GrantedGemLevel(40));
	EXPECT_TRUE(FiresGrantedBolt(theirs));
	EXPECT_EQ(GrantedBoltPercent(theirs, GrantedBoltLevel(theirs)), GrantedBoltPercent(mine, GrantedBoltLevel(mine)));
	SetRndSeed(9);
	const int a = RollGrantedBoltDamage(mine);
	SetRndSeed(9);
	EXPECT_EQ(RollGrantedBoltDamage(theirs), a);
	gbIsMultiplayer = false;
}

} // namespace
} // namespace devilution
