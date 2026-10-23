#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

/** Curtis 2026-10-23: no class has a ceiling on its attributes. */
class Poe2AttributesTest : public ::testing::Test {
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
		SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
	}
};

TEST_F(Poe2AttributesTest, EveryClassCanRaiseEveryAttributeToTheHeroFileLimit)
{
	for (const HeroClass heroClass : { HeroClass::Warrior, HeroClass::Rogue, HeroClass::Sorcerer, HeroClass::Monk, HeroClass::Bard, HeroClass::Barbarian }) {
		Player &player = *MyPlayer;
		player._pClass = heroClass;
		player.setCharacterLevel(1);
		player._pBaseStr = player._pStrength = 10;
		player._pBaseMag = player._pMagic = 10;
		player._pBaseDex = player._pDexterity = 10;
		player._pBaseVit = player._pVitality = 10;
		for (const CharacterAttribute attribute : { CharacterAttribute::Strength, CharacterAttribute::Magic, CharacterAttribute::Dexterity, CharacterAttribute::Vitality })
			EXPECT_EQ(player.GetMaximumAttributeValue(attribute), 255);

		ModifyPlrStr(player, 300);
		ModifyPlrMag(player, 300);
		ModifyPlrDex(player, 300);
		ModifyPlrVit(player, 300);
		EXPECT_EQ(player._pBaseStr, 255);
		EXPECT_EQ(player._pBaseMag, 255);
		EXPECT_EQ(player._pBaseDex, 255);
		EXPECT_EQ(player._pBaseVit, 255);
	}
}

TEST_F(Poe2AttributesTest, ClassicModeKeepsTheClassCeilings)
{
	gbIsPoe2 = false;
	MyPlayer->_pClass = HeroClass::Sorcerer;
	EXPECT_EQ(MyPlayer->GetMaximumAttributeValue(CharacterAttribute::Strength), 45);
	gbIsPoe2 = true;
}

} // namespace
} // namespace devilution
