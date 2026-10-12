#include <string>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
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
	// 34 skill gems plus 4 Spirit gems.
	EXPECT_EQ(gems, 38);
}

TEST_F(Poe2SkillGemsTest, SpellGemGrantsTheSpellWhileSocketed)
{
	Player &player = *MyPlayer;
	player.setCharacterLevel(9);
	EXPECT_EQ(player.GetSpellLevel(SpellID::Fireball), 0);

	SocketSkill(player, 2, SkillSlot::ForSpell(SpellID::Fireball));
	EXPECT_EQ(player.GetSpellLevel(SpellID::Fireball), GemSpellLevel(9));
	EXPECT_EQ(GemSpellLevel(9), 3);

	// A higher level learned from books wins.
	player._pSplLvl[static_cast<size_t>(SpellID::Fireball)] = 7;
	EXPECT_EQ(player.GetSpellLevel(SpellID::Fireball), 7);

	EXPECT_EQ(SocketSkill(player, 2, {}), SkillSlot::ForSpell(SpellID::Fireball));
	player._pSplLvl[static_cast<size_t>(SpellID::Fireball)] = 0;
	EXPECT_EQ(player.GetSpellLevel(SpellID::Fireball), 0);
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

	player.InvBody[INVLOC_HAND_LEFT] = MakeBase("Maul");
	EXPECT_EQ(GetSkillUsability(player, slam), SkillUsability::Ready);

	player._pMana = Fixed26_6::fromInt(1);
	EXPECT_EQ(GetSkillUsability(player, slam), SkillUsability::Mana);
	EXPECT_EQ(GetSkillUsability(player, {}), SkillUsability::Empty);
}

TEST_F(Poe2SkillGemsTest, WeaponsAllowMatchingSkills)
{
	const Item dagger = MakeBase("Dagger");
	EXPECT_TRUE(ItemAllowsWeaponSkill(dagger, WeaponSkill::Flurry));
	EXPECT_TRUE(ItemAllowsWeaponSkill(dagger, WeaponSkill::Cleave));
	EXPECT_FALSE(ItemAllowsWeaponSkill(dagger, WeaponSkill::Barrage));

	const Item axe = MakeBase("Battle Axe");
	EXPECT_TRUE(ItemAllowsWeaponSkill(axe, WeaponSkill::Cleave));
	EXPECT_FALSE(ItemAllowsWeaponSkill(axe, WeaponSkill::Flurry));

	EXPECT_TRUE(ItemAllowsWeaponSkill(MakeBase("Long Bow"), WeaponSkill::Barrage));
	EXPECT_TRUE(ItemAllowsWeaponSkill(MakeBase("Long Bow"), WeaponSkill::RainOfArrows));
	EXPECT_TRUE(ItemAllowsWeaponSkill(MakeBase("Maul"), WeaponSkill::LeapSlam));
	EXPECT_TRUE(ItemAllowsWeaponSkill(axe, WeaponSkill::LeapSlam));
	EXPECT_FALSE(ItemAllowsWeaponSkill(MakeBase("Long Bow"), WeaponSkill::LeapSlam));
	EXPECT_TRUE(IsRangedWeaponSkill(WeaponSkill::RainOfArrows));
	EXPECT_FALSE(ItemAllowsWeaponSkill(MakeBase("Buckler"), WeaponSkill::Cleave));
}

TEST_F(Poe2SkillGemsTest, ClassSkillsNeedTheirGear)
{
	EXPECT_TRUE(ItemAllowsWeaponSkill(MakeBase("Buckler"), WeaponSkill::ShieldCharge));
	EXPECT_FALSE(ItemAllowsWeaponSkill(MakeBase("Battle Axe"), WeaponSkill::ShieldCharge));
	EXPECT_TRUE(ItemAllowsWeaponSkill(MakeBase("Long Bow"), WeaponSkill::LightningArrow));
	EXPECT_TRUE(ItemAllowsWeaponSkill(MakeBase("Long Bow"), WeaponSkill::EscapeShot));
	EXPECT_TRUE(IsRangedWeaponSkill(WeaponSkill::LightningArrow));
	EXPECT_FALSE(IsRangedWeaponSkill(WeaponSkill::EscapeShot));

	// Battle Cry and the fire and lightning skills work with any gear, even bare hands.
	Player &player = *MyPlayer;
	for (const WeaponSkill skill : { WeaponSkill::BattleCry, WeaponSkill::EmberFusillade, WeaponSkill::Spark }) {
		EXPECT_FALSE(WeaponSkillNeedsWeapon(skill)) << WeaponSkillName(skill);
		EXPECT_TRUE(WeaponAllowsSkill(player, skill)) << WeaponSkillName(skill);
		EXPECT_FALSE(ItemAllowsWeaponSkill(MakeBase("Long Bow"), skill)) << WeaponSkillName(skill);
	}
	EXPECT_FALSE(WeaponAllowsSkill(player, WeaponSkill::ShieldCharge));
	player.InvBody[INVLOC_HAND_RIGHT] = MakeBase("Buckler");
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
	}
}

} // namespace
} // namespace devilution
