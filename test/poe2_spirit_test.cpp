#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "levels/gendung.h"
#include "monster.h"
#include "player.h"
#include "poe2/passive_tree.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/spirit.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/support_table.hpp"
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

/** @brief Sockets a Spirit gem and switches it on, as a right-click in the Skills panel does. */
void SocketOn(Player &player, size_t slot, PersistentSkill skill)
{
	SocketSkill(player, slot, SkillSlot::ForPersistent(skill));
	if (!player.poe2.spiritGemOn(slot))
		ToggleSpiritGem(player, slot);
}

TEST_F(Poe2SpiritTest, SceptreGoesWithAnyOneHandedWeapon)
{
	Player &player = *MyPlayer;
	InitializeItem(player.InvBody[INVLOC_HAND_LEFT], FindBase("Short Sword"));
	player.InvBody[INVLOC_HAND_LEFT]._iStatFlag = true;
	CalcPlrInv(player, false);
	const int swordMin = player._pIMinDam;
	const int swordMax = player._pIMaxDam;

	Item sceptre;
	InitializeItem(sceptre, FindBase("Rattling Sceptre"));
	sceptre._iStatFlag = true;
	ASSERT_TRUE(AutoEquip(player, sceptre, true, false)) << "The sceptre goes in the free hand";
	EXPECT_TRUE(ItemGrantsSpirit(player.InvBody[INVLOC_HAND_RIGHT]));
	CalcPlrInv(player, false);
	EXPECT_EQ(player._pIMinDam, swordMin) << "Its damage isn't added to the sword's";
	EXPECT_EQ(player._pIMaxDam, swordMax);
	EXPECT_GE(MaxSpirit(player), SceptreSpirit) << "It still grants Spirit";

	Item sword;
	InitializeItem(sword, FindBase("Short Sword"));
	sword._iStatFlag = true;
	player.InvBody[INVLOC_HAND_RIGHT].clear();
	EXPECT_FALSE(AutoEquip(player, sword, false, false)) << "Two swords don't pair; only a sceptre goes with any weapon";
	gbIsPoe2 = false;
	EXPECT_FALSE(AutoEquip(player, sceptre, false, false)) << "Diablo keeps one weapon per hero";
	gbIsPoe2 = true;
}

TEST_F(Poe2SpiritTest, SpiritGemsAreSkillGems)
{
	for (const PersistentSkill skill : { PersistentSkill::HeraldOfAsh, PersistentSkill::HeraldOfThunder, PersistentSkill::PurityOfElements, PersistentSkill::SummonGolem, PersistentSkill::SkeletalWarriors }) {
		Item gem;
		ASSERT_TRUE(CreateSkillGemItem(gem, SkillSlot::ForPersistent(skill))) << PersistentSkillName(skill);
		EXPECT_EQ(SkillForGem(gem).persistent(), skill);
		EXPECT_GT(SpiritReservation(skill), 0);
	}
}

TEST_F(Poe2SpiritTest, SkeletalWarriorsGrewWithLevelInOldSaves)
{
	EXPECT_EQ(SkeletalWarriorCount(1), 2);
	EXPECT_EQ(SkeletalWarriorCount(10), 3);
	EXPECT_EQ(SkeletalWarriorCount(25), 4);
	EXPECT_EQ(SkeletalWarriorCount(50), 4);
}

TEST_F(Poe2SpiritTest, MinionGemsReserveSpiritPerMinion)
{
	Player &player = *MyPlayer;
	constexpr PersistentSkill Warriors = PersistentSkill::SkeletalWarriors;
	player.poe2.spiritSkulls = 1;
	SocketOn(player, 0, Warriors);
	player.poe2.slotLevels[0] = 4;
	// POE2: the first 2 skeletons are free, so they need no Spirit at all.
	EXPECT_EQ(ChosenMinionCount(player, Warriors), 2);
	EXPECT_EQ(ActiveMinionCount(player, Warriors), 2);
	EXPECT_EQ(ReservedSpirit(player), 0);
	EXPECT_EQ(SpiritPerMinion(player, Warriors), 27);
	EXPECT_EQ(AffordableMinionCount(player, Warriors), 3);

	SetChosenMinionCount(player, Warriors, 3);
	EXPECT_EQ(ReservedSpirit(player), 27);
	EXPECT_EQ(AvailableSpirit(player), 3);
	EXPECT_EQ(GemSpiritReservation(player, Warriors), 27);

	// Short of Spirit, the gem keeps as many as fit.
	SetChosenMinionCount(player, Warriors, 4);
	EXPECT_EQ(ActiveMinionCount(player, Warriors), 3);
	EXPECT_EQ(ReservedSpirit(player), 27);
	EXPECT_TRUE(IsPersistentSkillActive(player, Warriors));

	// A higher gem level costs less per minion (and, like POE2, needs more Magic: 119 at level 20).
	player.poe2.slotLevels[0] = 20;
	player._pBaseMag = player._pMagic = 160;
	EXPECT_EQ(SpiritPerMinion(player, Warriors), 20);
	EXPECT_EQ(ActiveMinionCount(player, Warriors), 3);

	// None at all is a choice, not a lack of Spirit.
	SetChosenMinionCount(player, Warriors, 0);
	EXPECT_FALSE(IsPersistentSkillActive(player, Warriors));
	EXPECT_EQ(GetSkillUsability(player, SkillSlot::ForPersistent(Warriors)), SkillUsability::Ready);
	SetChosenMinionCount(player, Warriors, 99);
	EXPECT_EQ(ChosenMinionCount(player, Warriors), MaxMinionsPerGem);
}

TEST_F(Poe2SpiritTest, SkeletalMagesHaveNoFreeMinions)
{
	Player &player = *MyPlayer;
	constexpr PersistentSkill Mages = PersistentSkill::SkeletalMages;
	player.poe2.spiritSkulls = 1;
	SocketSkill(player, 0, SkillSlot::ForPersistent(Mages));
	player.poe2.slotLevels[0] = 4;
	EXPECT_EQ(ChosenMinionCount(player, Mages), 1);
	EXPECT_EQ(SpiritPerMinion(player, Mages), 47);
	// Its one minion needs 47 Spirit, so it doesn't go on with 30.
	EXPECT_TRUE(ToggleSpiritGem(player, 0));
	EXPECT_FALSE(player.poe2.spiritGemOn(0));
	player.poe2.spiritSkulls = AllSpiritSkulls;
	EXPECT_TRUE(ToggleSpiritGem(player, 0));
	EXPECT_EQ(ActiveMinionCount(player, Mages), 1);
	EXPECT_EQ(ReservedSpirit(player), 47);
	// With less Spirit later it waits, with none of its minions up.
	player.poe2.spiritSkulls = 1;
	EXPECT_FALSE(IsPersistentSkillActive(player, Mages));
	EXPECT_EQ(GetSkillUsability(player, SkillSlot::ForPersistent(Mages)), SkillUsability::Spirit);
}

TEST_F(Poe2SpiritTest, MinionsFightAtTheirGemsLevel)
{
	// A gem at the right level for the character fights at least at the character's level; one level lower, below it.
	for (int level = 2; level <= 50; level++) {
		const int gem = MaxGemLevelForCharacter(level);
		EXPECT_GE(MinionLevelForGemLevel(gem), level) << level;
		if (gem > 1)
			EXPECT_LT(MinionLevelForGemLevel(gem - 1), level) << level;
	}
	EXPECT_EQ(MinionLevelForGemLevel(MaxGemLevel), 50);
}

TEST_F(Poe2SpiritTest, YouWalkThroughYourOwnMinions)
{
	const dungeon_type oldLevelType = leveltype;
	leveltype = DTYPE_CATHEDRAL;
	Player &player = *MyPlayer;
	player.position.tile = { 10, 10 };
	Monster &minion = Monsters[5];
	minion.flags = MFLAG_GOLEM;
	minion.goalVar3 = 0;
	minion.hitPoints = Fixed26_6::fromInt(20);
	const Point tile { 11, 10 };
	dMonster[tile.x][tile.y] = 6;

	EXPECT_TRUE(PosOkPlayer(player, tile));
	dMonster[tile.x][tile.y] = -6; // walking into the tile
	EXPECT_TRUE(PosOkPlayer(player, tile));

	minion.goalVar3 = 1; // someone else's minion
	EXPECT_FALSE(PosOkPlayer(player, tile));
	minion.goalVar3 = 0;
	minion.flags = 0; // an enemy
	EXPECT_FALSE(PosOkPlayer(player, tile));

	minion.flags = MFLAG_GOLEM;
	gbIsPoe2 = false;
	EXPECT_FALSE(PosOkPlayer(player, tile));
	gbIsPoe2 = true;

	dMonster[tile.x][tile.y] = 0;
	minion = {};
	leveltype = oldLevelType;
}

TEST_F(Poe2SpiritTest, GemsReserveSpiritInSlotOrder)
{
	Player &player = *MyPlayer;
	EXPECT_EQ(MaxSpirit(player), 0);
	player.poe2.spiritSkulls = 1;
	EXPECT_EQ(MaxSpirit(player), 30);

	SocketOn(player, 0, PersistentSkill::HeraldOfAsh);
	SocketOn(player, 1, PersistentSkill::HeraldOfThunder);
	EXPECT_TRUE(IsPersistentSkillActive(player, PersistentSkill::HeraldOfAsh));
	// Without the Spirit for it, Thunder doesn't go on (Curtis 2026-10-30).
	EXPECT_FALSE(player.poe2.spiritGemOn(1));
	EXPECT_FALSE(IsPersistentSkillActive(player, PersistentSkill::HeraldOfThunder));
	EXPECT_EQ(ReservedSpirit(player), 30);

	// A sceptre's Spirit makes room for both.
	InitializeItem(player.InvBody[INVLOC_HAND_LEFT], FindBase("Rattling Sceptre"));
	player.InvBody[INVLOC_HAND_LEFT]._iStatFlag = true;
	EXPECT_TRUE(ItemGrantsSpirit(player.InvBody[INVLOC_HAND_LEFT]));
	EXPECT_EQ(MaxSpirit(player), 30 + SceptreSpirit);
	EXPECT_TRUE(ToggleSpiritGem(player, 1));
	EXPECT_TRUE(IsPersistentSkillActive(player, PersistentSkill::HeraldOfThunder));
	EXPECT_EQ(ReservedSpirit(player), 60);

	// Without the sceptre the newer one waits for Spirit again, still switched on.
	player.InvBody[INVLOC_HAND_LEFT].clear();
	EXPECT_TRUE(IsPersistentSkillActive(player, PersistentSkill::HeraldOfAsh));
	EXPECT_FALSE(IsPersistentSkillActive(player, PersistentSkill::HeraldOfThunder));
	EXPECT_TRUE(player.poe2.spiritGemOn(1));
	EXPECT_EQ(GetSkillUsability(player, SkillSlot::ForPersistent(PersistentSkill::HeraldOfThunder)), SkillUsability::Spirit);
}

TEST_F(Poe2SpiritTest, SpiritGemsStartOffUntilSwitchedOn)
{
	Player &player = *MyPlayer;
	player.poe2.spiritSkulls = 1;
	SocketSkill(player, 0, SkillSlot::ForPersistent(PersistentSkill::HeraldOfAsh));
	EXPECT_FALSE(IsPersistentSkillActive(player, PersistentSkill::HeraldOfAsh));
	EXPECT_EQ(ReservedSpirit(player), 0);
	EXPECT_TRUE(ToggleSpiritGem(player, 0));
	EXPECT_TRUE(IsPersistentSkillActive(player, PersistentSkill::HeraldOfAsh));
	EXPECT_EQ(ReservedSpirit(player), 30);
	// Socketing the same gem again keeps it on; another gem starts off.
	SocketSkill(player, 0, SkillSlot::ForPersistent(PersistentSkill::HeraldOfAsh));
	EXPECT_TRUE(player.poe2.spiritGemOn(0));
	SocketSkill(player, 0, SkillSlot::ForPersistent(PersistentSkill::HeraldOfThunder));
	EXPECT_FALSE(player.poe2.spiritGemOn(0));
	EXPECT_EQ(ReservedSpirit(player), 0);
}

TEST_F(Poe2SpiritTest, HeraldsTakeSupportsForTheirExplosions)
{
	Player &player = *MyPlayer;
	player.poe2.spiritSkulls = 3;
	const SkillSlot ash = SkillSlot::ForPersistent(PersistentSkill::HeraldOfAsh);
	const SkillSlot thunder = SkillSlot::ForPersistent(PersistentSkill::HeraldOfThunder);
	EXPECT_TRUE(SkillTakesSupports(ash));
	EXPECT_FALSE(SkillTakesSupports(SkillSlot::ForPersistent(PersistentSkill::PurityOfElements)));

	// Supports that change the explosion fit, by its element; supports for skills you use yourself don't.
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::IncreasedArea, ash));
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::ElementalFocus, ash));
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::LifeLeech, thunder));
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::FirePenetration, ash));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::FirePenetration, thunder));
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::LightningPenetration, thunder));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::SpellEcho, ash));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::BloodPrice, ash));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::Efficiency, ash));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::Momentum, ash));

	// Deadly Herald fits heralds only.
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::DeadlyHerald, ash));
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::DeadlyHerald, thunder));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::DeadlyHerald, SkillSlot::ForSpell(SpellID::Firebolt)));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::DeadlyHerald, SkillSlot::ForPersistent(PersistentSkill::SkeletalWarriors)));

	// It adds 20 Spirit to the herald's reservation; other supports add none.
	SocketOn(player, 0, PersistentSkill::HeraldOfAsh);
	EXPECT_EQ(ReservedSpirit(player), 30);
	SupportGem replaced;
	EXPECT_EQ(SocketSupportGem(player, 0, std::nullopt, SupportGem::ElementalFocus, replaced), SupportSocketResult::Socketed);
	EXPECT_EQ(ReservedSpirit(player), 30);
	EXPECT_EQ(SocketSupportGem(player, 0, std::nullopt, SupportGem::DeadlyHerald, replaced), SupportSocketResult::Socketed);
	EXPECT_EQ(ReservedSpirit(player), 50);
	EXPECT_EQ(GemSpiritCost(player, PersistentSkill::HeraldOfAsh), 50);

	// While the herald's explosion hits, its supports change the damage: 25% more elemental, then 30% more.
	Monster monster {};
	EXPECT_EQ(SupportHitDamagePercent(monster, DamageType::Fire), 100);
	{
		const SupportHitScope hit(player, ash);
		EXPECT_EQ(SupportHitDamagePercent(monster, DamageType::Fire), 125 * 130 / 100);
	}
	EXPECT_EQ(SupportHitDamagePercent(monster, DamageType::Fire), 100);
}

TEST_F(Poe2SpiritTest, SkullsGrantSpiritOnce)
{
	Player &player = *MyPlayer;
	Item skull;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (AllItemsList[i].iMiscId == IMISC_SPIRIT_SKULL && AllItemsList[i].iName == "Skull of the Archbishop")
			InitializeItem(skull, static_cast<_item_indexes>(i));
	}
	ASSERT_EQ(SpiritSkullIndex(skull), 2);
	EXPECT_TRUE(UseSpiritSkull(player, skull));
	EXPECT_EQ(MaxSpirit(player), 40);
	EXPECT_FALSE(UseSpiritSkull(player, skull));
	EXPECT_EQ(MaxSpirit(player), 40);
}

TEST_F(Poe2SpiritTest, PurityGrantsResistances)
{
	Player &player = *MyPlayer;
	InitializeItem(player.InvBody[INVLOC_HAND_LEFT], FindBase("Rattling Sceptre"));
	CalcPlrInv(player, false);
	const int fire = player._pFireResist;
	SocketOn(player, 0, PersistentSkill::PurityOfElements);
	ASSERT_TRUE(IsPersistentSkillActive(player, PersistentSkill::PurityOfElements));
	EXPECT_EQ(player._pFireResist, fire + PurityResistance);
	SocketSkill(player, 0, {});
	EXPECT_EQ(player._pFireResist, fire);
}

} // namespace
} // namespace devilution
