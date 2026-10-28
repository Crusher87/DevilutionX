#include <string>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/gem_levels.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/weapon_skills.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2SkillGemsTest : public ::testing::Test {
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

	static Item MakeBase(std::string_view name)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == name && AllItemsList[i].dropRate > 0) {
				Item item;
				InitializeItem(item, static_cast<_item_indexes>(i));
				return item;
			}
		}
		return {};
	}
};

TEST_F(Poe2SkillGemsTest, EveryGemGrantsASkillAndRoundTrips)
{
	int gems = 0;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		const SkillSlot skill = SkillForGemData(AllItemsList[i]);
		if (skill.isEmpty())
			continue;
		gems++;
		EXPECT_FALSE(SkillName(skill).empty()) << AllItemsList[i].iName;
		Item gem;
		ASSERT_TRUE(CreateSkillGemItem(gem, skill)) << AllItemsList[i].iName;
		EXPECT_EQ(SkillForGem(gem), skill) << AllItemsList[i].iName;
		if (skill.kind == SkillKind::Spell) {
			EXPECT_EQ(AllItemsList[i].iMinMag, GetSpellData(skill.spell()).minInt) << AllItemsList[i].iName;
		}
	}
	// Skill gems plus 69 Spirit gems.
	EXPECT_EQ(gems, 181);
}

TEST_F(Poe2SkillGemsTest, SpellGemGrantsTheSpellWhileSocketed)
{
	Player &player = *MyPlayer;
	player.setCharacterLevel(9);
	EXPECT_EQ(player.GetSpellLevel(SpellID::Fireball), 0);

	// The spell is cast at the gem's level.
	SocketSkill(player, 2, SkillSlot::ForSpell(SpellID::Fireball), 3);
	EXPECT_EQ(player.GetSpellLevel(SpellID::Fireball), 3);
	EXPECT_EQ(player.poe2.skillLevel(SpellID::Fireball), 3);

	// A higher level learned from books wins.
	player._pSplLvl[static_cast<size_t>(SpellID::Fireball)] = 7;
	EXPECT_EQ(player.GetSpellLevel(SpellID::Fireball), 7);

	EXPECT_EQ(SocketSkill(player, 2, {}), SkillSlot::ForSpell(SpellID::Fireball));
	player._pSplLvl[static_cast<size_t>(SpellID::Fireball)] = 0;
	EXPECT_EQ(player.GetSpellLevel(SpellID::Fireball), 0);
}

TEST_F(Poe2SkillGemsTest, GemLevelsFollowPoe2)
{
	// POE2's character level table scaled to Diablo's level 50 cap.
	EXPECT_EQ(CharacterLevelForGemLevel(1), 1);
	EXPECT_EQ(CharacterLevelForGemLevel(10), 20);
	EXPECT_EQ(CharacterLevelForGemLevel(20), 50);
	EXPECT_EQ(MaxGemLevelForCharacter(1), 1);
	EXPECT_EQ(MaxGemLevelForCharacter(19), 9);
	EXPECT_EQ(MaxGemLevelForCharacter(50), 20);

	// Cleave follows Boneshatter: 100%, 142%, 191%, 244%, 312% at levels 1, 5, 10, 15, 20. A use hits for that times
	// its damage for its speed (Cleave swings at 75%, so 134%).
	const SkillSlot cleave = SkillSlot::ForAttack(WeaponSkill::Cleave);
	EXPECT_EQ(GemCurveDamagePercent(cleave, 1), 100);
	EXPECT_EQ(GemCurveDamagePercent(cleave, 5), 142);
	EXPECT_EQ(GemCurveDamagePercent(cleave, 10), 191);
	EXPECT_EQ(GemCurveDamagePercent(cleave, 15), 244);
	EXPECT_EQ(GemCurveDamagePercent(cleave, 20), 312);
	EXPECT_EQ(GemDamagePercent(cleave, 1), 134);
	EXPECT_EQ(GemDamagePercent(cleave, 20), 312 * 134 / 100);
	// Weapon-tab attacks get 4x dearer by level 20 (Elemental and Occult damage skills 5x).
	EXPECT_EQ(GemManaPercent(cleave, 20), 400);
	EXPECT_EQ(GemManaPercent(SkillSlot::ForAttack(WeaponSkill::Spark), 20), 500);
}

TEST_F(Poe2SkillGemsTest, EachGemHasItsOwnCurve)
{
	const SkillSlot leapSlam = SkillSlot::ForAttack(WeaponSkill::LeapSlam);
	const SkillSlot barrage = SkillSlot::ForAttack(WeaponSkill::Barrage);
	EXPECT_EQ(GemCurveDamagePercent(leapSlam, 20), 408);
	EXPECT_EQ(GemCurveDamagePercent(barrage, 20), 280);
	// Steps are uneven like POE2's: Boneshatter's level 9 adds little, level 16 a lot.
	const SkillSlot cleave = SkillSlot::ForAttack(WeaponSkill::Cleave);
	EXPECT_EQ(GemCurveDamagePercent(cleave, 8), 174);
	EXPECT_EQ(GemCurveDamagePercent(cleave, 9), 182);
	EXPECT_EQ(GemCurveDamagePercent(cleave, 16), 257);
	EXPECT_EQ(GemStepText(cleave, 15, 16), "[+5% damage, +7% mana]");
	for (int level = 2; level <= MaxGemLevel; level++)
		EXPECT_GT(GemDamagePercent(leapSlam, level), GemDamagePercent(leapSlam, level - 1)) << level;

	// Spark gains projectiles at POE2 Spark's levels; Diablo's spells all grow along Fireball's steps to 312%.
	const SkillSlot spark = SkillSlot::ForAttack(WeaponSkill::Spark);
	EXPECT_EQ(GemExtraProjectiles(spark, 1), 0);
	EXPECT_EQ(GemExtraProjectiles(spark, 2), 1);
	EXPECT_EQ(GemExtraProjectiles(spark, 20), 5);
	EXPECT_NE(GemStepText(spark, 6, 7).find("+1 projectile, big step"), std::string::npos) << GemStepText(spark, 6, 7);
	EXPECT_EQ(GemCurveDamagePercent(SkillSlot::ForSpell(SpellID::Fireball), 20), 312);
	EXPECT_EQ(GemCurveDamagePercent(SkillSlot::ForSpell(SpellID::Lightning), 20), 312);
	EXPECT_EQ(BattleCryDamagePercent(1), 50);
	EXPECT_EQ(BattleCryDamagePercent(20), 100);
}

TEST_F(Poe2SkillGemsTest, GemTextShowsTheNumbers)
{
	const SkillSlot cleave = SkillSlot::ForAttack(WeaponSkill::Cleave);
	// The gem's curve times Cleave's damage for its speed (134%); its mana x117% for that speed (half as much).
	EXPECT_EQ(GemLevelText(cleave, 5), "level 5: 190% weapon damage, 5 mana");
	EXPECT_NE(GemCurveTextByName("cleave").find("level 20: 418% weapon damage, 14 mana (character level 50)"), std::string::npos) << GemCurveTextByName("cleave");
	EXPECT_EQ(GemCurveTextByName("nothing"), "No skill gem is called nothing.");
}

TEST_F(Poe2SkillGemsTest, GemItemsKeepTheirLevel)
{
	Item gem;
	ASSERT_TRUE(CreateSkillGemItem(gem, SkillSlot::ForAttack(WeaponSkill::Cleave), 7));
	EXPECT_EQ(SkillGemItemLevel(gem), 7);

	Item rebuilt;
	RecreateItem(*MyPlayer, rebuilt, static_cast<_item_indexes>(gem.IDidx), gem._iCreateInfo, gem._iSeed, gem._ivalue, gem.dwBuff);
	EXPECT_EQ(SkillGemItemLevel(rebuilt), 7);

	// Gems from before gem levels are level 1.
	gem.dwBuff &= ~CF_POE2_GEM_LEVEL;
	EXPECT_EQ(SkillGemItemLevel(gem), 1);
}

TEST_F(Poe2SkillGemsTest, AttackGemLevelRaisesManaCost)
{
	Player &player = *MyPlayer;
	// Leap Slam's listed cost, x151% for its speed (damage_pct 201, mana half as much): 5 -> 7.55, to 8.
	SocketSkill(player, 0, SkillSlot::ForAttack(WeaponSkill::LeapSlam), 1);
	EXPECT_EQ(WeaponSkillManaCost(player, WeaponSkill::LeapSlam), (WeaponSkillManaCost(WeaponSkill::LeapSlam) * 151 + 50) / 100);
	SocketSkill(player, 0, SkillSlot::ForAttack(WeaponSkill::LeapSlam), 20);
	EXPECT_EQ(WeaponSkillManaCost(player, WeaponSkill::LeapSlam), (WeaponSkillManaCost(WeaponSkill::LeapSlam) * 4 * 151 + 50) / 100);
}

TEST_F(Poe2SkillGemsTest, UsabilityChecksStatsWeaponAndMana)
{
	Player &player = *MyPlayer;
	const SkillSlot slam = SkillSlot::ForAttack(WeaponSkill::GroundSlam);
	player.poe2.skillSlots[0] = slam;
	player.poe2.slotLevels[0] = 5;
	player._pStrength = 10;
	player._pMana = Fixed26_6::fromInt(50);
	EXPECT_EQ(GetSkillUsability(player, slam), SkillUsability::Requirements);

	player._pStrength = 60;
	EXPECT_EQ(GetSkillUsability(player, slam), SkillUsability::Weapon);

	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Felled Greatclub");
	EXPECT_EQ(GetSkillUsability(player, slam), SkillUsability::Ready);

	player._pMana = Fixed26_6::fromInt(1);
	EXPECT_EQ(GetSkillUsability(player, slam), SkillUsability::Mana);
	EXPECT_EQ(GetSkillUsability(player, {}), SkillUsability::Empty);
}

TEST_F(Poe2SkillGemsTest, GemNeedsGrowWithLevelLikePoe2)
{
	// Path of Building 2's formula x 0.75, for a single-attribute gem.
	EXPECT_EQ(GemAttributeRequirement(1, 100), 0);
	EXPECT_EQ(GemAttributeRequirement(2, 100), 8);
	EXPECT_EQ(GemAttributeRequirement(3, 100), 12);
	EXPECT_EQ(GemAttributeRequirement(10, 100), 50);
	EXPECT_EQ(GemAttributeRequirement(13, 100), 70);
	EXPECT_EQ(GemAttributeRequirement(20, 100), 119);
	EXPECT_EQ(GemAttributeRequirement(20, 0), 0);
	// Hybrid gems: 60/40 and 50/50 shares, and anything under 8 counts as none.
	EXPECT_EQ(GemAttributeRequirement(3, 60), 9);
	EXPECT_EQ(GemAttributeRequirement(3, 40), 0);
	EXPECT_EQ(GemAttributeRequirement(3, 50), 8);
	EXPECT_EQ(GemAttributeRequirement(20, 60), 76);
	EXPECT_EQ(GemAttributeRequirement(20, 40), 54);
	EXPECT_EQ(GemAttributeRequirement(20, 50), 65);

	const GemAttributeNeeds cleave = SkillGemAttributeNeeds(SkillSlot::ForAttack(WeaponSkill::Cleave), 3);
	EXPECT_EQ(cleave.strength, 12);
	EXPECT_EQ(cleave.magic, 0);
	EXPECT_EQ(cleave.dexterity, 0);
	// Whirling Staff is half Strength, half Dexterity.
	const GemAttributeNeeds whirl = SkillGemAttributeNeeds(SkillSlot::ForAttack(WeaponSkill::WhirlingStaff), 20);
	EXPECT_EQ(whirl.strength, 65);
	EXPECT_EQ(whirl.dexterity, 65);

	// Every class can level its own gems to 20 under Diablo's attribute caps: a Bard's 120 covers a single-attribute
	// gem, a Monk's Magic 80 and a Sorcerer's Dexterity 85 cover a 50/50 one, a Rogue's Strength 55 a 40% share.
	EXPECT_LE(GemAttributeRequirement(20, 100), 120);
	EXPECT_LE(GemAttributeRequirement(20, 50), 80);
	EXPECT_LE(GemAttributeRequirement(20, 40), 55);

	Player &player = *MyPlayer;
	Item gem;
	ASSERT_TRUE(CreateSkillGemItem(gem, SkillSlot::ForAttack(WeaponSkill::Cleave), 10));
	EXPECT_EQ(gem._iMinStr, 50);
	player._pStrength = 49;
	EXPECT_FALSE(player.CanUseItem(gem));
	player._pStrength = 50;
	EXPECT_TRUE(player.CanUseItem(gem));
}

TEST_F(Poe2SkillGemsTest, WeaponsAllowMatchingSkills)
{
	const Item dagger = MakeBase("Glass Shank");
	EXPECT_TRUE(ItemAllowsWeaponSkill(dagger, WeaponSkill::Flurry));
	EXPECT_TRUE(ItemAllowsWeaponSkill(dagger, WeaponSkill::Cleave));
	EXPECT_FALSE(ItemAllowsWeaponSkill(dagger, WeaponSkill::Barrage));

	const Item axe = MakeBase("Splitting Greataxe");
	EXPECT_TRUE(ItemAllowsWeaponSkill(axe, WeaponSkill::Cleave));
	EXPECT_FALSE(ItemAllowsWeaponSkill(axe, WeaponSkill::Flurry));

	EXPECT_TRUE(ItemAllowsWeaponSkill(MakeBase("Recurve Bow"), WeaponSkill::Barrage));
	EXPECT_TRUE(ItemAllowsWeaponSkill(MakeBase("Recurve Bow"), WeaponSkill::RainOfArrows));
	EXPECT_TRUE(ItemAllowsWeaponSkill(MakeBase("Felled Greatclub"), WeaponSkill::LeapSlam));
	EXPECT_TRUE(ItemAllowsWeaponSkill(axe, WeaponSkill::LeapSlam));
	EXPECT_FALSE(ItemAllowsWeaponSkill(MakeBase("Recurve Bow"), WeaponSkill::LeapSlam));
	EXPECT_TRUE(IsRangedWeaponSkill(WeaponSkill::RainOfArrows));
	EXPECT_FALSE(ItemAllowsWeaponSkill(MakeBase("Hide Buckler"), WeaponSkill::Cleave));
}

TEST_F(Poe2SkillGemsTest, ClassSkillsNeedTheirGear)
{
	EXPECT_TRUE(ItemAllowsWeaponSkill(MakeBase("Hide Buckler"), WeaponSkill::ShieldCharge));
	EXPECT_FALSE(ItemAllowsWeaponSkill(MakeBase("Splitting Greataxe"), WeaponSkill::ShieldCharge));
	EXPECT_TRUE(ItemAllowsWeaponSkill(MakeBase("Recurve Bow"), WeaponSkill::LightningArrow));
	EXPECT_TRUE(ItemAllowsWeaponSkill(MakeBase("Recurve Bow"), WeaponSkill::EscapeShot));
	EXPECT_TRUE(IsRangedWeaponSkill(WeaponSkill::LightningArrow));
	EXPECT_FALSE(IsRangedWeaponSkill(WeaponSkill::EscapeShot));

	// Battle Cry and the fire and lightning skills work with any gear, even bare hands.
	Player &player = *MyPlayer;
	for (const WeaponSkill skill : { WeaponSkill::BattleCry, WeaponSkill::EmberFusillade, WeaponSkill::Spark }) {
		EXPECT_FALSE(WeaponSkillNeedsWeapon(skill)) << WeaponSkillName(skill);
		EXPECT_TRUE(WeaponAllowsSkill(player, skill)) << WeaponSkillName(skill);
		EXPECT_FALSE(ItemAllowsWeaponSkill(MakeBase("Recurve Bow"), skill)) << WeaponSkillName(skill);
	}
	EXPECT_FALSE(WeaponAllowsSkill(player, WeaponSkill::ShieldCharge));
	player.InvBody[INVLOC_HAND_RIGHT] = MakeBase("Hide Buckler");
	EXPECT_TRUE(WeaponAllowsSkill(player, WeaponSkill::ShieldCharge));

	// Every skill has a name, a description and a gem with a stat requirement.
	for (uint8_t i = 1; i <= static_cast<uint8_t>(WeaponSkill::Last); i++) {
		const auto skill = static_cast<WeaponSkill>(i);
		EXPECT_FALSE(WeaponSkillName(skill).empty());
		EXPECT_FALSE(WeaponSkillDescription(skill).empty()) << WeaponSkillName(skill);
		EXPECT_GT(WeaponSkillManaCost(skill), 0) << WeaponSkillName(skill);
		const std::optional<size_t> gem = SkillGemItemIndex(SkillSlot::ForAttack(skill));
		ASSERT_TRUE(gem.has_value()) << WeaponSkillName(skill);
		const ItemData &data = AllItemsList[*gem];
		EXPECT_GT(data.iMinStr + data.iMinMag + data.iMinDex, 0) << WeaponSkillName(skill);
	}
}

TEST_F(Poe2SkillGemsTest, SlotsAndStarterSkills)
{
	Player &player = *MyPlayer;
	EXPECT_EQ(FirstFreeSkillSlot(player), 0U);

	const std::pair<HeroClass, SkillSlot> starters[] = {
		{ HeroClass::Warrior, SkillSlot::ForAttack(WeaponSkill::Cleave) },
		{ HeroClass::Rogue, SkillSlot::ForAttack(WeaponSkill::Barrage) },
		{ HeroClass::Sorcerer, SkillSlot::ForSpell(SpellID::Firebolt) },
	};
	for (const auto &[heroClass, skill] : starters) {
		player.poe2.clear();
		player._pClass = heroClass;
		GrantStarterSkill(player);
		EXPECT_EQ(player.poe2.skillSlots[0], skill);
		GrantStarterSkill(player);
		EXPECT_EQ(player.poe2.skillSlots[1], SkillSlot {}) << "granted twice";
		EXPECT_EQ(FirstFreeSkillSlot(player), 1U);
		EXPECT_EQ(FindSkillSlot(player, skill), 0U);
		// The starter skill goes on the first tray key.
		EXPECT_EQ(player.poe2.tray[static_cast<size_t>(TraySlot::Key1)], TrayBinding::ForGem(0));
	}
}

TrayBinding TrayOf(const Player &player, TraySlot slot)
{
	return player.poe2.tray[static_cast<size_t>(slot)];
}

TEST_F(Poe2SkillGemsTest, TrayStartsWithTheWeaponAttackOnTheMouse)
{
	const Player &player = *MyPlayer;
	EXPECT_EQ(TrayOf(player, TraySlot::LeftMouse), (TrayBinding { TrayAction::WeaponAttack, 0 })) << "weapon set I's free attack";
	EXPECT_EQ(TrayOf(player, TraySlot::MiddleMouse), (TrayBinding { TrayAction::WeaponAttack, 0 }));
	EXPECT_EQ(TrayOf(player, TraySlot::RightMouse).action, TrayAction::None) << "an empty right button casts the readied spell";
	for (size_t key = 0; key < NumTrayKeys; key++)
		EXPECT_EQ(TrayOf(player, TrayKeySlot(key)).action, TrayAction::None);
}

TEST_F(Poe2SkillGemsTest, SocketedSkillsFillTheTrayKeys)
{
	Player &player = *MyPlayer;
	for (size_t socket = 0; socket < 7; socket++)
		SocketSkill(player, socket, SkillSlot::ForSpell(static_cast<SpellID>(static_cast<int>(SpellID::Firebolt) + static_cast<int>(socket))));
	SocketSkill(player, 7, SkillSlot::ForPersistent(PersistentSkill::HeraldOfAsh));
	for (size_t socket = 0; socket < 8; socket++)
		AutoBindSocket(player, socket);

	for (size_t key = 0; key < NumTrayKeys; key++)
		EXPECT_EQ(TrayOf(player, TrayKeySlot(key)), TrayBinding::ForGem(key));
	// The middle button keeps the free weapon attack; a seventh skill is bound by hand.
	EXPECT_EQ(TrayOf(player, TraySlot::MiddleMouse).action, TrayAction::WeaponAttack);
	EXPECT_EQ(TraySlotForSocket(player, 6), std::nullopt);
	// Spirit gems are active while socketed and need no slot.
	EXPECT_EQ(TraySlotForSocket(player, 7), std::nullopt);
	// The mouse buttons keep their actions.
	EXPECT_EQ(TrayOf(player, TraySlot::LeftMouse).action, TrayAction::WeaponAttack);
	EXPECT_EQ(AutoBindSocket(player, 0), TraySlot::Key1);
}

TEST_F(Poe2SkillGemsTest, BindingAGemOnAnotherSlotSwapsThem)
{
	Player &player = *MyPlayer;
	SocketSkill(player, 0, SkillSlot::ForSpell(SpellID::Firebolt));
	SocketSkill(player, 1, SkillSlot::ForSpell(SpellID::Fireball));
	BindTraySlot(player, TraySlot::Key1, TrayBinding::ForGem(0));
	BindTraySlot(player, TraySlot::Key2, TrayBinding::ForGem(1));

	// Like POE2, picking a skill that's on another slot swaps the two.
	BindTraySlot(player, TraySlot::Key1, TrayBinding::ForGem(1));
	EXPECT_EQ(TrayOf(player, TraySlot::Key1), TrayBinding::ForGem(1));
	EXPECT_EQ(TrayOf(player, TraySlot::Key2), TrayBinding::ForGem(0));
	EXPECT_EQ(TraySkill(player, TraySlot::Key2), SkillSlot::ForSpell(SpellID::Firebolt));

	// Swapping with the left button gives the key the left button's weapon attack.
	BindTraySlot(player, TraySlot::LeftMouse, TrayBinding::ForGem(0));
	EXPECT_EQ(TrayOf(player, TraySlot::LeftMouse), TrayBinding::ForGem(0));
	EXPECT_EQ(TrayOf(player, TraySlot::Key2).action, TrayAction::WeaponAttack);
	// The old mouse actions don't fit any slot now.
	BindTraySlot(player, TraySlot::Key3, { TrayAction::Interact, 0 });
	EXPECT_EQ(TrayOf(player, TraySlot::Key3).action, TrayAction::None);
	// The weapon attack fits any slot, for either weapon set.
	BindTraySlot(player, TraySlot::Key3, { TrayAction::WeaponAttack, 1 });
	EXPECT_EQ(TrayOf(player, TraySlot::Key3), (TrayBinding { TrayAction::WeaponAttack, 1 }));

	// Clearing puts a slot back to its default.
	BindTraySlot(player, TraySlot::RightMouse, { TrayAction::WeaponAttack, 1 });
	EXPECT_EQ(TrayOf(player, TraySlot::RightMouse), (TrayBinding { TrayAction::WeaponAttack, 1 }));
	ClearTraySlot(player, TraySlot::RightMouse);
	ClearTraySlot(player, TraySlot::LeftMouse);
	EXPECT_EQ(TrayOf(player, TraySlot::RightMouse).action, TrayAction::None);
	EXPECT_EQ(TrayOf(player, TraySlot::LeftMouse), (TrayBinding { TrayAction::WeaponAttack, 0 }));
}

TEST_F(Poe2SkillGemsTest, AWeaponAttackOnTwoKeysClearsFromOne)
{
	Player &player = *MyPlayer;
	// The weapon attack can sit on any number of slots (Curtis 2026-10-23 had it on Q and E).
	BindTraySlot(player, TraySlot::Key1, { TrayAction::WeaponAttack, 0 });
	BindTraySlot(player, TraySlot::Key2, { TrayAction::WeaponAttack, 0 });
	EXPECT_EQ(TrayOf(player, TraySlot::Key1), (TrayBinding { TrayAction::WeaponAttack, 0 }));
	EXPECT_EQ(TrayOf(player, TraySlot::Key2), (TrayBinding { TrayAction::WeaponAttack, 0 }));

	// The tray popup's empty choice binds nothing, which clears just that slot.
	BindTraySlot(player, TraySlot::Key2, {});
	EXPECT_EQ(TrayOf(player, TraySlot::Key2).action, TrayAction::None);
	EXPECT_EQ(TrayOf(player, TraySlot::Key1), (TrayBinding { TrayAction::WeaponAttack, 0 }));
	ClearTraySlot(player, TraySlot::Key1);
	EXPECT_EQ(TrayOf(player, TraySlot::Key1).action, TrayAction::None);
}

TEST_F(Poe2SkillGemsTest, TakingAGemOutClearsItsTraySlot)
{
	Player &player = *MyPlayer;
	SocketSkill(player, 2, SkillSlot::ForSpell(SpellID::Firebolt));
	BindTraySlot(player, TraySlot::LeftMouse, TrayBinding::ForGem(2));
	BindTraySlot(player, TraySlot::Key4, TrayBinding::ForGem(2));
	EXPECT_EQ(TrayOf(player, TraySlot::LeftMouse).action, TrayAction::WeaponAttack);

	SocketSkill(player, 2, {});
	EXPECT_EQ(TrayOf(player, TraySlot::Key4).action, TrayAction::None);
	EXPECT_EQ(TraySkill(player, TraySlot::Key4), SkillSlot {});
}

TEST_F(Poe2SkillGemsTest, OldSkillBarMovesToTheTrayKeys)
{
	Player &player = *MyPlayer;
	SocketSkill(player, 0, SkillSlot::ForSpell(SpellID::Firebolt));
	SocketSkill(player, 4, SkillSlot::ForAttack(WeaponSkill::Cleave));
	SocketSkill(player, 5, SkillSlot::ForSpell(SpellID::Fireball));
	SocketSkill(player, 3, SkillSlot::ForPersistent(PersistentSkill::HeraldOfAsh));
	player.poe2.tray = {};
	for (size_t slot = 0; slot < NumTraySlots; slot++)
		player.poe2.tray[slot] = DefaultTrayBinding(static_cast<TraySlot>(slot));

	MigrateOldSkillBar(player);
	// E stays E, G stays G, and V's skill moves to Q.
	EXPECT_EQ(TrayOf(player, TraySlot::Key2), TrayBinding::ForGem(0));
	EXPECT_EQ(TrayOf(player, TraySlot::Key6), TrayBinding::ForGem(4));
	EXPECT_EQ(TrayOf(player, TraySlot::Key1), TrayBinding::ForGem(5));
	EXPECT_EQ(TrayOf(player, TraySlot::Key5).action, TrayAction::None);
}

} // namespace
} // namespace devilution
