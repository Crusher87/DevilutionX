#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/passive_tree.hpp"
#include "poe2/tears.hpp"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

/** Curtis 2026-10-29: the Fountain of Tears moves one point between two stats the hero picks. */
class Poe2TearsTest : public ::testing::Test {
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

		Player &player = *MyPlayer;
		player._pClass = HeroClass::Warrior;
		player.setCharacterLevel(10);
		const ClassAttributes &base = player.getClassAttributes();
		player._pBaseStr = player._pStrength = base.baseStr + 5;
		player._pBaseMag = player._pMagic = base.baseMag;
		player._pBaseDex = player._pDexterity = base.baseDex + 2;
		player._pBaseVit = player._pVitality = base.baseVit;
		player._pMaxHPBase = player._pHPBase = Fixed26_6::fromInt(100);
		player._pMaxManaBase = player._pManaBase = Fixed26_6::fromInt(20);
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		LoadPassiveTreeData();
		UnloadModArchives();
	}
};

TEST_F(Poe2TearsTest, MovesOnePointBetweenThePickedStats)
{
	Player &player = *MyPlayer;
	const int str = player._pBaseStr;
	const int mag = player._pBaseMag;
	const int dex = player._pBaseDex;
	const int vit = player._pBaseVit;

	ASSERT_TRUE(TearsTrade(player, CharacterAttribute::Strength, CharacterAttribute::Vitality));
	EXPECT_EQ(player._pBaseStr, str - 1);
	EXPECT_EQ(player._pBaseVit, vit + 1);
	EXPECT_EQ(player._pBaseMag, mag);
	EXPECT_EQ(player._pBaseDex, dex);
}

TEST_F(Poe2TearsTest, NeverTakesAStatBelowItsStartingValue)
{
	Player &player = *MyPlayer;
	EXPECT_FALSE(TearsCanGive(player, CharacterAttribute::Magic));
	EXPECT_FALSE(TearsTrade(player, CharacterAttribute::Magic, CharacterAttribute::Strength));
	EXPECT_EQ(player._pBaseMag, player.getClassAttributes().baseMag);

	for (int i = 0; i < 2; i++)
		ASSERT_TRUE(TearsTrade(player, CharacterAttribute::Dexterity, CharacterAttribute::Magic));
	EXPECT_FALSE(TearsCanGive(player, CharacterAttribute::Dexterity));
}

TEST_F(Poe2TearsTest, AStatCannotTradeWithItself)
{
	Player &player = *MyPlayer;
	const int str = player._pBaseStr;
	EXPECT_FALSE(TearsTrade(player, CharacterAttribute::Strength, CharacterAttribute::Strength));
	EXPECT_EQ(player._pBaseStr, str);
}

} // namespace
} // namespace devilution
