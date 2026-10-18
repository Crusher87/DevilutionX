#include <string>

#include <gtest/gtest.h>

#include "DiabloUI/diabloui.h"
#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "init.hpp"
#include "items.h"
#include "loadsave.h"
#include "menu.h"
#include "pfile.h"
#include "player.h"
#include "poe2/hardcore.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/spirit.hpp"
#include "poe2/weapon_swap.hpp"
#include "spells.h"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"
#include "utils/file_util.h"
#include "utils/paths.h"

namespace devilution {

namespace {

bool HardcoreListed;

class Poe2WeaponSwapTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayerId = 0;
		MyPlayer = &Players[0];
		*MyPlayer = {};
		gbVanilla = false;
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		gbIsHellfireSaveGame = true;
		gbIsPoe2 = true;
		UnloadModArchives();
		LoadModArchives({ { "hf", "poe2" } });
		LoadItemData();
		LoadSpellData();
		LoadPlayerDataFiles();
		LoadMonsterData();
		LoadPassiveTreeData();
		paths::SetPrefPath(paths::BasePath());
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		gbIsHellfire = false;
		gbIsHellfireSaveGame = false;
		LoadPassiveTreeData();
		UnloadModArchives();
	}

	static Item Sword()
	{
		Item item;
		InitializeItem(item, IDI_WARRIOR);
		return item;
	}
};

TEST_F(Poe2WeaponSwapTest, SwapExchangesTheHandsAndTheSet)
{
	// Another player, so the swap isn't sent over the network, which the tests don't start.
	Players.resize(2);
	MyPlayer = &Players[0];
	Player &player = Players[1];
	player = {};
	player._pClass = HeroClass::Warrior;
	player._pmode = PM_STAND;
	player._pHitPoints = player._pMaxHP = Fixed26_6::fromInt(100);
	player.swapHands[0] = Sword();
	ASSERT_TRUE(player.InvBody[INVLOC_HAND_LEFT].isEmpty());

	ASSERT_TRUE(SwapWeapons(player));
	EXPECT_EQ(player.poe2.weaponSet, 1);
	EXPECT_EQ(player.InvBody[INVLOC_HAND_LEFT].IDidx, IDI_WARRIOR);
	EXPECT_TRUE(InactiveHand(player, INVLOC_HAND_LEFT).isEmpty());

	ASSERT_TRUE(SwapWeapons(player));
	EXPECT_EQ(player.poe2.weaponSet, 0);
	EXPECT_TRUE(player.InvBody[INVLOC_HAND_LEFT].isEmpty());
	EXPECT_EQ(InactiveHand(player, INVLOC_HAND_LEFT).IDidx, IDI_WARRIOR);
}

TEST_F(Poe2WeaponSwapTest, WeaponSetsAndLeagueSurviveASave)
{
	LoadCoreArchives();
	LoadGameArchives();
	if (!HaveMainData())
		GTEST_SKIP() << "MPQ assets (spawn.mpq or DIABDAT.MPQ) not found - skipping test";
	UnloadModArchives();
	LoadModArchives({ { "hf", "poe2" } });
	LoadItemData();
	LoadPassiveTreeData();
	const std::string savePath = paths::BasePath() + "single_0.sv";
	RemoveFile(savePath.c_str());
	_uiheroinfo info {};
	info.heroclass = HeroClass::Warrior;
	info.hardcore = true;
	CopyUtf8(info.name, "Swapper", sizeof(info.name));
	ASSERT_TRUE(pfile_ui_save_create(&info));
	gSaveNumber = 0;
	pfile_read_player_from_save(0, *MyPlayer);
	LoadPoe2HeroData(*MyPlayer);

	Player &player = *MyPlayer;
	EXPECT_TRUE(player.poe2.hardcore);
	EXPECT_TRUE(IsHardcoreHero(player));
	player.setCharacterLevel(10);
	player.poe2.specBooks = 0b101;
	player.poe2.weaponSet = 1;
	const PassiveNodeId start = *PassiveStartNode(HeroClass::Warrior);
	const PassiveNodeId first = PassiveTree()[start].links.front();
	ASSERT_TRUE(AllocatePassive(player, first, PassiveAttribute::Dexterity, PassivePool::SetII));
	player.swapHands[1] = Sword();
	// The bigger tree's last jewel socket is saved too.
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (AllItemsList[i].iMiscId == IMISC_JEWEL) {
			InitializeItem(player.jewels[MaxJewelSockets - 1], static_cast<_item_indexes>(i));
			break;
		}
	}
	const int jewelIndex = player.jewels[MaxJewelSockets - 1].IDidx;
	ASSERT_GE(jewelIndex, 0);
	SetChosenMinionCount(player, PersistentSkill::SkeletalWarriors, 5);
	pfile_write_hero(/*writeGameData=*/false);

	player.poe2.clear();
	player.swapHands[1].clear();
	player.jewels[MaxJewelSockets - 1].clear();
	LoadPoe2HeroData(player);
	EXPECT_EQ(MaxJewelSockets, 12U);
	EXPECT_EQ(player.jewels[MaxJewelSockets - 1].IDidx, jewelIndex);
	EXPECT_TRUE(player.poe2.hardcore);
	EXPECT_EQ(player.poe2.specBooks, 0b101);
	EXPECT_EQ(player.poe2.weaponSet, 1);
	EXPECT_TRUE(IsPassiveInPool(player, first, PassivePool::SetII));
	EXPECT_FALSE(IsPassiveInPool(player, first, PassivePool::Main));
	EXPECT_EQ(player.swapHands[1].IDidx, IDI_WARRIOR);
	EXPECT_TRUE(player.swapHands[0].isEmpty());
	EXPECT_EQ(ChosenMinionCount(player, PersistentSkill::SkeletalWarriors), 5);
	EXPECT_EQ(ChosenMinionCount(player, PersistentSkill::SkeletalMages), StartingMinionCount(PersistentSkill::SkeletalMages));

	// The hero list reads the league on its own.
	HardcoreListed = false;
	pfile_ui_set_hero_infos([](_uiheroinfo *hero) {
		if (std::string_view(hero->name) == "Swapper")
			HardcoreListed = hero->hardcore;
		return true;
	});
	EXPECT_TRUE(HardcoreListed);
	RemoveFile(savePath.c_str());
}

} // namespace
} // namespace devilution
