#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/passive_tree.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/spirit.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2SpiritTest : public ::testing::Test {
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

		Player &player = *MyPlayer;
		player._pClass = HeroClass::Sorcerer;
		player.setCharacterLevel(10);
		player._pBaseStr = player._pStrength = 50;
		player._pBaseMag = player._pMagic = 100;
		player._pBaseDex = player._pDexterity = 50;
		player._pBaseVit = player._pVitality = 25;
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		UnloadModArchives();
	}

	static _item_indexes FindBase(std::string_view name)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == name)
				return static_cast<_item_indexes>(i);
		}
		return IDI_GOLD;
	}
};

TEST_F(Poe2SpiritTest, SpiritGemsAreSkillGems)
{
	for (const PersistentSkill skill : { PersistentSkill::HeraldOfAsh, PersistentSkill::HeraldOfThunder, PersistentSkill::PurityOfElements, PersistentSkill::SummonGolem, PersistentSkill::SkeletalWarriors }) {
		Item gem;
		ASSERT_TRUE(CreateSkillGemItem(gem, SkillSlot::ForPersistent(skill))) << PersistentSkillName(skill);
		EXPECT_EQ(SkillForGem(gem).persistent(), skill);
		EXPECT_GT(SpiritReservation(skill), 0);
	}
}

TEST_F(Poe2SpiritTest, SkeletalWarriorsGrowWithLevel)
{
	EXPECT_EQ(SkeletalWarriorCount(1), 2);
	EXPECT_EQ(SkeletalWarriorCount(10), 3);
	EXPECT_EQ(SkeletalWarriorCount(25), 4);
	EXPECT_EQ(SkeletalWarriorCount(50), 4);
	// Fits in the Spirit every character has, unlike the golem.
	EXPECT_LE(SpiritReservation(PersistentSkill::SkeletalWarriors), BaseSpirit);
}

TEST_F(Poe2SpiritTest, GemsReserveSpiritInSlotOrder)
{
	Player &player = *MyPlayer;
	EXPECT_EQ(MaxSpirit(player), BaseSpirit);

	SocketSkill(player, 0, SkillSlot::ForPersistent(PersistentSkill::HeraldOfAsh));
	SocketSkill(player, 1, SkillSlot::ForPersistent(PersistentSkill::HeraldOfThunder));
	EXPECT_TRUE(IsPersistentSkillActive(player, PersistentSkill::HeraldOfAsh));
	EXPECT_FALSE(IsPersistentSkillActive(player, PersistentSkill::HeraldOfThunder));
	EXPECT_EQ(GetSkillUsability(player, SkillSlot::ForPersistent(PersistentSkill::HeraldOfThunder)), SkillUsability::Spirit);
	EXPECT_EQ(ReservedSpirit(player), 30);

	// A sceptre's Spirit makes room for both.
	InitializeItem(player.InvBody[INVLOC_HAND_LEFT], FindBase("Rattling Sceptre"));
	player.InvBody[INVLOC_HAND_LEFT]._iStatFlag = true;
	EXPECT_TRUE(ItemGrantsSpirit(player.InvBody[INVLOC_HAND_LEFT]));
	EXPECT_EQ(MaxSpirit(player), BaseSpirit + SceptreSpirit);
	EXPECT_TRUE(IsPersistentSkillActive(player, PersistentSkill::HeraldOfThunder));
	EXPECT_EQ(ReservedSpirit(player), 60);
}

TEST_F(Poe2SpiritTest, PurityGrantsResistances)
{
	Player &player = *MyPlayer;
	InitializeItem(player.InvBody[INVLOC_HAND_LEFT], FindBase("Rattling Sceptre"));
	CalcPlrInv(player, false);
	const int fire = player._pFireResist;
	SocketSkill(player, 0, SkillSlot::ForPersistent(PersistentSkill::PurityOfElements));
	ASSERT_TRUE(IsPersistentSkillActive(player, PersistentSkill::PurityOfElements));
	EXPECT_EQ(player._pFireResist, fire + PurityResistance);
	SocketSkill(player, 0, {});
	EXPECT_EQ(player._pFireResist, fire);
}

} // namespace
} // namespace devilution
