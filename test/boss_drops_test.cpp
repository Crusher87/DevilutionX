/**
 * Na-Krul's drops away from his own floor: the admin monster spawner can put him on floor 1, where the great sword,
 * war staff and long war bow he drops are above the floor's drop level. Making them used to roll forever (the game froze).
 */
#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "player.h"
#include "spells.h"

namespace devilution {
namespace {

class BossDropsTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		*MyPlayer = {};
		gbIsSpawn = false;
		gbIsMultiplayer = false;
		gbIsHellfire = true;
		gbIsPoe2 = false;
		setlevel = false;
		UnloadModArchives();
		LoadModArchives({ { "hf" } });
		LoadItemData();
		LoadSpellData();
		currlevel = 1;
		leveltype = DTYPE_CATHEDRAL;
		ActiveItemCount = 0;
		for (int i = 0; i < MAXITEMS; i++)
			ActiveItems[i] = i;
		for (int x = 0; x < MAXDUNX; x++)
			for (int y = 0; y < MAXDUNY; y++)
				dItem[x][y] = 0;
	}

	void TearDown() override
	{
		ActiveItemCount = 0;
		currlevel = 0;
		leveltype = DTYPE_TOWN;
		UnloadModArchives();
	}
};

TEST_F(BossDropsTest, NaKrulsWeaponsDropOnFloorOne)
{
	SetRndSeed(7);
	CreateMagicWeapon({ 40, 40 }, ItemType::Sword, ICURS_GREAT_SWORD, false, false);
	CreateMagicWeapon({ 40, 40 }, ItemType::Staff, ICURS_WAR_STAFF, false, false);
	CreateMagicWeapon({ 40, 40 }, ItemType::Bow, ICURS_LONG_WAR_BOW, false, false);
	ASSERT_EQ(ActiveItemCount, 3);
	EXPECT_EQ(Items[ActiveItems[0]]._iCurs, ICURS_GREAT_SWORD);
	EXPECT_EQ(Items[ActiveItems[1]]._iCurs, ICURS_WAR_STAFF);
	EXPECT_EQ(Items[ActiveItems[2]]._iCurs, ICURS_LONG_WAR_BOW);
	// Still magic, as on his own floor.
	EXPECT_NE(Items[ActiveItems[0]]._iMagical, ITEM_QUALITY_NORMAL);
}

TEST_F(BossDropsTest, AnItemThatCantDropIsSkippedNotRolledForever)
{
	// No sword has the amulet picture.
	CreateMagicWeapon({ 40, 40 }, ItemType::Sword, ICURS_AMULET, false, false);
	EXPECT_EQ(ActiveItemCount, 0);
}

} // namespace
} // namespace devilution
