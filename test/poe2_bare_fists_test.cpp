#include <cmath>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/default_attack.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/passive_tree.hpp"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

/** A Monk's bare fists (Curtis 2026-10-22: 156 damage in 82 punches; decided 2026-10-19: they hit like a plain staff of the attack gem's level). */
class Poe2BareFistsTest : public ::testing::Test {
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
		LoadModifierData();
		LoadMissileData();
		LoadMonsterData();
		LevelMonsterTypes[0].type = MT_NZOMBIE;
		Missiles.clear();
		leveltype = DTYPE_CATHEDRAL;
		// Damage to monsters goes out as a network message.
		SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);

		Player &player = *MyPlayer;
		player._pClass = HeroClass::Monk;
		player.setCharacterLevel(1);
		player._pBaseStr = player._pStrength = 25;
		player._pBaseMag = player._pMagic = 15;
		player._pBaseDex = player._pDexterity = 25;
		player._pBaseVit = player._pVitality = 20;
		player.position.tile = { 40, 40 };
		player._pdir = Direction::East;
	}

	void TearDown() override
	{
		ActiveMonsterCount = 0;
		Missiles.clear();
		leveltype = DTYPE_TOWN;
		gbIsPoe2 = false;
		LoadModifierData();
		UnloadModArchives();
	}

	static _item_indexes FindBase(std::string_view name)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == name && AllItemsList[i].dropRate > 0)
				return static_cast<_item_indexes>(i);
		}
		ADD_FAILURE() << name;
		return IDI_GOLD;
	}

	/** @brief A normal monster turned to stone, so every hit lands. */
	static Monster &PlaceMonster(size_t slot, Point tile)
	{
		Monster &monster = Monsters[slot];
		monster = {};
		monster.levelType = 0;
		monster.uniqueType = UniqueMonsterType::None;
		monster.talkMsg = TEXT_NONE;
		for (uint32_t seed = 1; seed < 100000; seed++) {
			monster.rndItemSeed = seed;
			if (GetMonsterAffixes(monster).rarity == MonsterRarity::Normal)
				break;
		}
		monster.mode = MonsterMode::Petrified;
		monster.position.tile = tile;
		monster.maxHitPoints = monster.hitPoints = Fixed26_6::fromInt(1000);
		ActiveMonsters[ActiveMonsterCount] = static_cast<unsigned>(slot);
		ActiveMonsterCount++;
		return monster;
	}
};

TEST_F(Poe2BareFistsTest, FistsHitLikeAPlainHeavyStaffOfTheGemLevel)
{
	const ItemData &first = AllItemsList[FindBase("Heavy Ash Staff")];
	const ItemData &last = AllItemsList[FindBase("Heavy Ascendant Staff")];
	const auto share = [](int damage) { return static_cast<int>(std::round(static_cast<double>(damage) * BareFistStaffPercent / 100)); };
	// A level 1 gem needs character level 1: the first heavy staff. Level 20 is past the last one.
	EXPECT_EQ(BareFistDamage(1), std::make_pair(share(first.iMinDam), share(first.iMaxDam)));
	EXPECT_EQ(BareFistDamage(MaxGemLevel), std::make_pair(share(last.iMinDam), share(last.iMaxDam)));
	for (int gem = 1; gem < MaxGemLevel; gem++) {
		const auto [min, max] = BareFistDamage(gem);
		const auto [nextMin, nextMax] = BareFistDamage(gem + 1);
		EXPECT_GE(min, 1) << gem;
		EXPECT_LE(min, max) << gem;
		EXPECT_LE(min, nextMin) << gem;
		EXPECT_LE(max, nextMax) << gem;
	}
	EXPECT_LT(BareFistDamage(1).second, BareFistDamage(MaxGemLevel).second);
}

TEST_F(Poe2BareFistsTest, OnlyAMonkWithNoWeaponFightsBareHanded)
{
	Player &player = *MyPlayer;
	EXPECT_TRUE(FightsWithBareFists(player));
	Item &hand = player.InvBody[INVLOC_HAND_LEFT];
	InitializeItem(hand, FindBase("Heavy Ash Staff"));
	hand._iStatFlag = true;
	EXPECT_FALSE(FightsWithBareFists(player)) << "A staff hits with its own damage";
	hand._iStatFlag = false;
	EXPECT_TRUE(FightsWithBareFists(player)) << "A staff the hero can't use deals none";
	hand.clear();
	player._pClass = HeroClass::Warrior;
	EXPECT_FALSE(FightsWithBareFists(player));
	player._pClass = HeroClass::Monk;
	gbIsPoe2 = false;
	EXPECT_FALSE(FightsWithBareFists(player)) << "Diablo's Monk keeps his own fists";
	gbIsPoe2 = true;
}

TEST_F(Poe2BareFistsTest, TheGemLevelIsTheAttackGemsLevel)
{
	Player &player = *MyPlayer;
	const SkillSlot punch = DefaultSkillSlot(player);
	ASSERT_EQ(static_cast<DefaultAttack>(punch.id), DefaultAttack::Punch);
	EXPECT_EQ(BareFistGemLevel(player, punch), 1) << "No gems";

	const SkillSlot wave = SkillSlot::ForAttack(WeaponSkill::WaveOfLight);
	player.poe2.skillSlots[0] = wave;
	player.poe2.slotLevels[0] = 3;
	player.poe2.skillSlots[1] = SkillSlot::ForAttack(WeaponSkill::FistsOfThunder);
	player.poe2.slotLevels[1] = 7;
	player.poe2.skillSlots[2] = SkillSlot::ForSpell(SpellID::Firebolt);
	player.poe2.slotLevels[2] = 12;
	EXPECT_EQ(BareFistGemLevel(player, wave), 3) << "A skill punches at its own gem's level";
	EXPECT_EQ(BareFistGemLevel(player, punch), 7) << "The default attack has no gem: the best attack gem, not a spell gem";
	EXPECT_EQ(AttackWeaponDamage(player, wave), BareFistDamage(3));

	// The panel shows the default attack's fists, in place of Diablo's level/2 to level.
	player.setCharacterLevel(20);
	CalcPlrInv(player, false);
	EXPECT_EQ(std::make_pair(player._pIMinDam, player._pIMaxDam), BareFistDamage(7));
}

TEST_F(Poe2BareFistsTest, PunchesAreNotRoundedAway)
{
	Player &player = *MyPlayer;
	Monster &punched = PlaceMonster(0, { 41, 40 });
	ASSERT_TRUE(SkillHitMonster(player, punched, 100));
	const int fists = punched.maxHitPoints.raw() - punched.hitPoints.raw();

	// A weapon that hits for 1: the default attack's 85% used to round it down to 0.
	player._pClass = HeroClass::Warrior;
	player._pIMinDam = 1;
	player._pIMaxDam = 1;
	Monster &tapped = PlaceMonster(1, { 39, 40 });
	ASSERT_TRUE(SkillHitMonster(player, tapped, 100));
	const int tap = tapped.maxHitPoints.raw() - tapped.hitPoints.raw();
	EXPECT_GT(tap, 0);
	EXPECT_GT(fists, tap) << "A Monk's fists hit like a staff";
}

} // namespace
} // namespace devilution
