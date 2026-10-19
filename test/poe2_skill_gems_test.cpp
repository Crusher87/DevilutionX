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
	// Skill gems plus 49 Spirit gems.
	EXPECT_EQ(gems, 161);
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

	// Cleave follows Boneshatter: 100%, 142%, 191%, 244%, 312% at levels 1, 5, 10, 15, 20.
	const SkillSlot cleave = SkillSlot::ForAttack(WeaponSkill::Cleave);
	EXPECT_EQ(GemDamagePercent(cleave, 1), 100);
	EXPECT_EQ(GemDamagePercent(cleave, 5), 142);
	EXPECT_EQ(GemDamagePercent(cleave, 10), 191);
	EXPECT_EQ(GemDamagePercent(cleave, 15), 244);
	EXPECT_EQ(GemDamagePercent(cleave, 20), 312);
	EXPECT_EQ(GemManaPercent(cleave, 20), 500);
}

TEST_F(Poe2SkillGemsTest, EachGemHasItsOwnCurve)
{
	const SkillSlot leapSlam = SkillSlot::ForAttack(WeaponSkill::LeapSlam);
	const SkillSlot barrage = SkillSlot::ForAttack(WeaponSkill::Barrage);
	EXPECT_EQ(GemDamagePercent(leapSlam, 20), 408);
	EXPECT_EQ(GemDamagePercent(barrage, 20), 280);
	// Steps are uneven like POE2's: Boneshatter's level 9 adds little, level 16 a lot.
	const SkillSlot cleave = SkillSlot::ForAttack(WeaponSkill::Cleave);
	EXPECT_EQ(GemDamagePercent(cleave, 8), 174);
	EXPECT_EQ(GemDamagePercent(cleave, 9), 182);
	EXPECT_EQ(GemDamagePercent(cleave, 16), 257);
	EXPECT_EQ(GemStepText(cleave, 15, 16), "[+5% damage, +8% mana]");
	for (int level = 2; level <= MaxGemLevel; level++)
		EXPECT_GT(GemDamagePercent(leapSlam, level), GemDamagePercent(leapSlam, level - 1)) << level;

	// Spark gains projectiles at POE2 Spark's levels; Fireball relies on Diablo's own spell level growth.
	const SkillSlot spark = SkillSlot::ForAttack(WeaponSkill::Spark);
	EXPECT_EQ(GemExtraProjectiles(spark, 1), 0);
	EXPECT_EQ(GemExtraProjectiles(spark, 2), 1);
	EXPECT_EQ(GemExtraProjectiles(spark, 20), 5);
	EXPECT_NE(GemStepText(spark, 6, 7).find("+1 projectile, big step"), std::string::npos) << GemStepText(spark, 6, 7);
	EXPECT_EQ(GemDamagePercent(SkillSlot::ForSpell(SpellID::Fireball), 20), 100);
	EXPECT_EQ(GemDamagePercent(SkillSlot::ForSpell(SpellID::Lightning), 20), 312);
	EXPECT_EQ(BattleCryDamagePercent(1), 50);
	EXPECT_EQ(BattleCryDamagePercent(20), 100);
}

TEST_F(Poe2SkillGemsTest, GemTextShowsTheNumbers)
{
	const SkillSlot cleave = SkillSlot::ForAttack(WeaponSkill::Cleave);
	EXPECT_EQ(GemLevelText(cleave, 5), "level 5: 142% weapon damage, 6 mana");
	EXPECT_NE(GemCurveTextByName("cleave").find("level 20: 312% weapon damage, 20 mana (character level 50)"), std::string::npos) << GemCurveTextByName("cleave");
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
	SocketSkill(player, 0, SkillSlot::ForAttack(WeaponSkill::LeapSlam), 1);
	EXPECT_EQ(WeaponSkillManaCost(player, WeaponSkill::LeapSlam), WeaponSkillManaCost(WeaponSkill::LeapSlam));
	SocketSkill(player, 0, SkillSlot::ForAttack(WeaponSkill::LeapSlam), 20);
	EXPECT_EQ(WeaponSkillManaCost(player, WeaponSkill::LeapSlam), WeaponSkillManaCost(WeaponSkill::LeapSlam) * 5);
}

TEST_F(Poe2SkillGemsTest, UsabilityChecksStatsWeaponAndMana)
{
	Player &player = *MyPlayer;
	const SkillSlot slam = SkillSlot::ForAttack(WeaponSkill::GroundSlam);
	player._pStrength = 10;
	player._pMana = Fixed26_6::fromInt(50);
	EXPECT_EQ(GetSkillUsability(player, slam), SkillUsability::Requirements);

	player._pStrength = 60;
	EXPECT_EQ(GetSkillUsability(player, slam), SkillUsability::Weapon);

	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Sledge");
	EXPECT_EQ(GetSkillUsability(player, slam), SkillUsability::Ready);

	player._pMana = Fixed26_6::fromInt(1);
	EXPECT_EQ(GetSkillUsability(player, slam), SkillUsability::Mana);
	EXPECT_EQ(GetSkillUsability(player, {}), SkillUsability::Empty);
}

TEST_F(Poe2SkillGemsTest, WeaponsAllowMatchingSkills)
{
	const Item dagger = MakeBase("Shiv");
	EXPECT_TRUE(ItemAllowsWeaponSkill(dagger, WeaponSkill::Flurry));
	EXPECT_TRUE(ItemAllowsWeaponSkill(dagger, WeaponSkill::Cleave));
	EXPECT_FALSE(ItemAllowsWeaponSkill(dagger, WeaponSkill::Barrage));

	const Item axe = MakeBase("Woodsman's Axe");
	EXPECT_TRUE(ItemAllowsWeaponSkill(axe, WeaponSkill::Cleave));
	EXPECT_FALSE(ItemAllowsWeaponSkill(axe, WeaponSkill::Flurry));

	EXPECT_TRUE(ItemAllowsWeaponSkill(MakeBase("Yew Bow"), WeaponSkill::Barrage));
	EXPECT_TRUE(ItemAllowsWeaponSkill(MakeBase("Yew Bow"), WeaponSkill::RainOfArrows));
	EXPECT_TRUE(ItemAllowsWeaponSkill(MakeBase("Sledge"), WeaponSkill::LeapSlam));
	EXPECT_TRUE(ItemAllowsWeaponSkill(axe, WeaponSkill::LeapSlam));
	EXPECT_FALSE(ItemAllowsWeaponSkill(MakeBase("Yew Bow"), WeaponSkill::LeapSlam));
	EXPECT_TRUE(IsRangedWeaponSkill(WeaponSkill::RainOfArrows));
	EXPECT_FALSE(ItemAllowsWeaponSkill(MakeBase("Hide Buckler"), WeaponSkill::Cleave));
}

TEST_F(Poe2SkillGemsTest, ClassSkillsNeedTheirGear)
{
	EXPECT_TRUE(ItemAllowsWeaponSkill(MakeBase("Hide Buckler"), WeaponSkill::ShieldCharge));
	EXPECT_FALSE(ItemAllowsWeaponSkill(MakeBase("Woodsman's Axe"), WeaponSkill::ShieldCharge));
	EXPECT_TRUE(ItemAllowsWeaponSkill(MakeBase("Yew Bow"), WeaponSkill::LightningArrow));
	EXPECT_TRUE(ItemAllowsWeaponSkill(MakeBase("Yew Bow"), WeaponSkill::EscapeShot));
	EXPECT_TRUE(IsRangedWeaponSkill(WeaponSkill::LightningArrow));
	EXPECT_FALSE(IsRangedWeaponSkill(WeaponSkill::EscapeShot));

	// Battle Cry and the fire and lightning skills work with any gear, even bare hands.
	Player &player = *MyPlayer;
	for (const WeaponSkill skill : { WeaponSkill::BattleCry, WeaponSkill::EmberFusillade, WeaponSkill::Spark }) {
		EXPECT_FALSE(WeaponSkillNeedsWeapon(skill)) << WeaponSkillName(skill);
		EXPECT_TRUE(WeaponAllowsSkill(player, skill)) << WeaponSkillName(skill);
		EXPECT_FALSE(ItemAllowsWeaponSkill(MakeBase("Yew Bow"), skill)) << WeaponSkillName(skill);
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

TEST_F(Poe2SkillGemsTest, TrayStartsWithDiabloMouseButtons)
{
	const Player &player = *MyPlayer;
	EXPECT_EQ(TrayOf(player, TraySlot::LeftMouse).action, TrayAction::MoveAndAttack);
	EXPECT_EQ(TrayOf(player, TraySlot::RightMouse).action, TrayAction::ReadiedSpell);
	EXPECT_EQ(TrayOf(player, TraySlot::MiddleMouse).action, TrayAction::WeaponAttack) << "the free weapon attack";
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
	EXPECT_EQ(TrayOf(player, TraySlot::LeftMouse).action, TrayAction::MoveAndAttack);
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

	// A key can't take a mouse action, so swapping with the left button leaves the key empty.
	BindTraySlot(player, TraySlot::LeftMouse, TrayBinding::ForGem(0));
	EXPECT_EQ(TrayOf(player, TraySlot::LeftMouse), TrayBinding::ForGem(0));
	EXPECT_EQ(TrayOf(player, TraySlot::Key2).action, TrayAction::None);
	BindTraySlot(player, TraySlot::Key3, { TrayAction::Interact, 0 });
	EXPECT_EQ(TrayOf(player, TraySlot::Key3).action, TrayAction::None);
	// The weapon attack fits any slot.
	BindTraySlot(player, TraySlot::Key3, { TrayAction::WeaponAttack, 0 });
	EXPECT_EQ(TrayOf(player, TraySlot::Key3).action, TrayAction::WeaponAttack);

	// The right button can take an action; clearing puts a slot back to its default.
	BindTraySlot(player, TraySlot::RightMouse, { TrayAction::Interact, 0 });
	EXPECT_EQ(TrayOf(player, TraySlot::RightMouse).action, TrayAction::Interact);
	ClearTraySlot(player, TraySlot::RightMouse);
	ClearTraySlot(player, TraySlot::LeftMouse);
	EXPECT_EQ(TrayOf(player, TraySlot::RightMouse).action, TrayAction::ReadiedSpell);
	EXPECT_EQ(TrayOf(player, TraySlot::LeftMouse).action, TrayAction::MoveAndAttack);
}

TEST_F(Poe2SkillGemsTest, TakingAGemOutClearsItsTraySlot)
{
	Player &player = *MyPlayer;
	SocketSkill(player, 2, SkillSlot::ForSpell(SpellID::Firebolt));
	BindTraySlot(player, TraySlot::LeftMouse, TrayBinding::ForGem(2));
	BindTraySlot(player, TraySlot::Key4, TrayBinding::ForGem(2));
	EXPECT_EQ(TrayOf(player, TraySlot::LeftMouse).action, TrayAction::MoveAndAttack);

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
