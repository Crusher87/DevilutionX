#include <array>
#include <string>

#include <gtest/gtest.h>

#include "DiabloUI/diabloui.h"
#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "loadsave.h"
#include "menu.h"
#include "missiles.h"
#include "pfile.h"
#include "player.h"
#include "poe2/charms.hpp"
#include "poe2/defenses.hpp"
#include "poe2/flasks.hpp"
#include "poe2/passive_tree.hpp"
#include "spells.h"
#include "tables/misdat.h"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"
#include "utils/file_util.h"
#include "utils/paths.h"
#include "utils/str_cat.hpp"

namespace devilution {
namespace {

class Poe2CharmsTest : public ::testing::Test {
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
		LoadMissileData();
		Missiles.clear();

		Player &player = *MyPlayer;
		player._pMaxHP = player._pMaxHPBase = Fixed26_6::fromInt(100);
		player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(100);
		player._pMaxMana = player._pMaxManaBase = Fixed26_6::fromInt(100);
		player._pMana = player._pManaBase = Fixed26_6::fromInt(100);
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		Missiles.clear();
		UnloadModArchives();
		LoadMissileData();
	}

public:
	static Item &PutInBelt(Player &player, size_t slot, std::string_view name)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == name) {
				Item &item = player.SpdList[slot];
				InitializeItem(item, static_cast<_item_indexes>(i));
				item._iCharges = item._iMaxCharges; // as if filled from kills
				return item;
			}
		}
		ADD_FAILURE() << name;
		return player.SpdList[slot];
	}
};

TEST_F(Poe2CharmsTest, CharmsStartEmptyAndAreBeltItems)
{
	Player &player = *MyPlayer;
	for (const std::string_view name : { "Bloodstone Charm", "Lapis Charm", "Iron Charm", "Ruby Charm", "Topaz Charm", "Amethyst Charm", "Stone Charm", "Golden Charm", "Sapphire Charm" }) {
		Item &charm = PutInBelt(player, 4, name);
		InitializeItem(charm, charm.IDidx);
		EXPECT_TRUE(IsCharm(charm)) << name;
		EXPECT_EQ(charm._iCharges, 0) << name;
		EXPECT_EQ(charm._iMaxCharges, MaxCharmCharges) << name;
		charm._iCharges = MaxCharmCharges;
		EXPECT_TRUE(charm.isUsable()) << name;
	}
}

TEST_F(Poe2CharmsTest, BloodstoneCharmCatchesLowLife)
{
	Player &player = *MyPlayer;
	Item &charm = PutInBelt(player, 4, "Bloodstone Charm");
	player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(50);
	ProcessCharms(player);
	EXPECT_EQ(player._pHitPoints.whole(), 50);
	EXPECT_EQ(charm._iCharges, MaxCharmCharges);

	player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(20);
	ProcessCharms(player);
	EXPECT_EQ(player._pHitPoints.whole(), 50);
	EXPECT_EQ(charm._iCharges, MaxCharmCharges - CharmChargesPerUse);

	// Waits out the cooldown before going off again.
	player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(20);
	ProcessCharms(player);
	EXPECT_EQ(player._pHitPoints.whole(), 20);
}

TEST_F(Poe2CharmsTest, LapisCharmCatchesLowMana)
{
	Player &player = *MyPlayer;
	Item &charm = PutInBelt(player, 5, "Lapis Charm");
	player._pMana = player._pManaBase = Fixed26_6::fromInt(10);
	ProcessCharms(player);
	EXPECT_EQ(player._pMana.whole(), 50);
	EXPECT_EQ(charm._iCharges, MaxCharmCharges - CharmChargesPerUse);
}

TEST_F(Poe2CharmsTest, IronCharmGrantsGuardAfterAHeavyHit)
{
	Player &player = *MyPlayer;
	Item &charm = PutInBelt(player, 6, "Iron Charm");
	EXPECT_EQ(ApplyGuard(player, Fixed26_6::fromInt(5)).whole(), 5);
	EXPECT_EQ(player.poe2.guardTicks, 0);

	EXPECT_EQ(ApplyGuard(player, Fixed26_6::fromInt(20)).whole(), 20);
	EXPECT_EQ(player.poe2.guardTicks, GuardTicks);
	EXPECT_EQ(charm._iCharges, MaxCharmCharges - CharmChargesPerUse);
	EXPECT_EQ(ApplyGuard(player, Fixed26_6::fromInt(10)).whole(), 6);
}

TEST_F(Poe2CharmsTest, KillsRefillCharmsButTownDoesNot)
{
	Player &player = *MyPlayer;
	Item &charm = PutInBelt(player, 4, "Bloodstone Charm");
	charm._iCharges = 0;
	GainFlaskCharges(player, 2);
	EXPECT_EQ(charm._iCharges, 2);
	RefillFlasks(player);
	EXPECT_EQ(charm._iCharges, 2);
	RefillCharms(player);
	EXPECT_EQ(charm._iCharges, MaxCharmCharges);
}

TEST_F(Poe2CharmsTest, TheTownWellRefillsCharms)
{
	Player &player = *MyPlayer;
	Item &charm = PutInBelt(player, 4, "Bloodstone Charm");
	charm._iCharges = 1;
	player.plrlevel = 0;
	player.plrIsOnSetLevel = false;
	player.position.tile = { 59, 70 };
	RefillCharmsAtWell(player);
	EXPECT_EQ(charm._iCharges, 1) << "Standing by the well doesn't refill: it's clicked";
	EXPECT_TRUE(IsTownWellTile({ 60, 70 }));
	ClickTownWell(player);
	EXPECT_EQ(charm._iCharges, MaxCharmCharges);
}

TEST_F(Poe2CharmsTest, RubyCharmRaisesFireResistanceWhenAFireHitLands)
{
	Player &player = *MyPlayer;
	Item &charm = PutInBelt(player, 4, "Ruby Charm");
	EXPECT_EQ(ResistanceAgainstHit(player, DamageType::Physical, 0), 0);
	EXPECT_EQ(ResistanceAgainstHit(player, DamageType::Lightning, 10), 10);
	EXPECT_EQ(charm._iCharges, MaxCharmCharges);

	EXPECT_EQ(ResistanceAgainstHit(player, DamageType::Fire, 10), 10 + ResistCharmBonus);
	EXPECT_EQ(charm._iCharges, MaxCharmCharges - CharmChargesPerUse);
	// The bonus lasts without spending more charges, and stops at the cap.
	EXPECT_EQ(ResistanceAgainstHit(player, DamageType::Fire, 70), MaxResistance);
	EXPECT_EQ(charm._iCharges, MaxCharmCharges - CharmChargesPerUse);
	// It doesn't hold up the life charm.
	EXPECT_EQ(player.poe2.charmCooldown, 0);

	for (int i = 0; i < ResistCharmTicks; i++)
		ProcessCharms(player);
	EXPECT_EQ(player.poe2.resistCharmTicks[0], 0);
}

TEST_F(Poe2CharmsTest, TopazAndAmethystCharmsAnswerTheirElements)
{
	Player &player = *MyPlayer;
	PutInBelt(player, 4, "Topaz Charm");
	PutInBelt(player, 5, "Amethyst Charm");
	EXPECT_EQ(ResistanceAgainstHit(player, DamageType::Fire, 0), 0);
	EXPECT_EQ(ResistanceAgainstHit(player, DamageType::Lightning, 0), ResistCharmBonus);
	// The Amethyst Charm answers chaos, as in POE2 (chaos damage plan); cold hits leave it alone.
	EXPECT_EQ(ResistanceAgainstHit(player, DamageType::Cold, 10), 10);
	EXPECT_EQ(ResistanceAgainstHit(player, DamageType::Chaos, -20), -20 + ResistCharmBonus);
	EXPECT_EQ(CharmDescription(CharmEffect::Magic), "A chaos hit lands: +25% chaos resistance for 4 seconds");
}

TEST_F(Poe2CharmsTest, SapphireCharmAnswersCold)
{
	Player &player = *MyPlayer;
	Item &charm = PutInBelt(player, 4, "Sapphire Charm");
	EXPECT_EQ(GetCharmEffect(charm), CharmEffect::Cold);
	EXPECT_EQ(charm._iMiscId, IMISC_CHARM_COLD);
	EXPECT_EQ(CharmDescription(CharmEffect::Cold), "A cold hit lands: +25% cold resistance for 4 seconds");
	// Fire, lightning and chaos hits leave it alone.
	EXPECT_EQ(ResistanceAgainstHit(player, DamageType::Fire, 0), 0);
	EXPECT_EQ(ResistanceAgainstHit(player, DamageType::Chaos, 0), 0);
	EXPECT_EQ(charm._iCharges, MaxCharmCharges);
	EXPECT_EQ(ResistanceAgainstHit(player, DamageType::Cold, 10), 10 + ResistCharmBonus);
	EXPECT_EQ(charm._iCharges, MaxCharmCharges - CharmChargesPerUse);
	EXPECT_GT(player.poe2.resistCharmTicks[3], 0);
	EXPECT_EQ(player.poe2.resistCharmTicks[2], 0);
	// The Lapis Charm is the mana charm, which was called Sapphire.
	Item &lapis = PutInBelt(player, 5, "Lapis Charm");
	EXPECT_EQ(GetCharmEffect(lapis), CharmEffect::Mana);
	for (int i = 0; i < ResistCharmTicks; i++)
		ProcessCharms(player);
	EXPECT_EQ(player.poe2.resistCharmTicks[3], 0);
}

TEST_F(Poe2CharmsTest, StoneCharmBreaksAStun)
{
	Player &player = *MyPlayer;
	EXPECT_FALSE(CharmPreventsStun(player));
	Item &charm = PutInBelt(player, 4, "Stone Charm");
	EXPECT_TRUE(CharmPreventsStun(player));
	EXPECT_EQ(charm._iCharges, MaxCharmCharges - CharmChargesPerUse);
	EXPECT_EQ(player.poe2.stunImmuneTicks, StunImmuneTicks);
	// Immune for a while without spending more charges.
	EXPECT_TRUE(CharmPreventsStun(player));
	EXPECT_EQ(charm._iCharges, MaxCharmCharges - CharmChargesPerUse);
	for (int i = 0; i < StunImmuneTicks; i++)
		ProcessCharms(player);
	EXPECT_TRUE(CharmPreventsStun(player));
	EXPECT_EQ(charm._iCharges, 0);
	player.poe2.stunImmuneTicks = 0;
	EXPECT_FALSE(CharmPreventsStun(player));
}

/**
 * @brief Puts a charm in the belt as the hero would (it starts empty), then fills it the way play does: from kills,
 * with the belt checked every tick in between.
 */
Item &FillFromKills(Player &player, size_t slot, std::string_view name)
{
	ProcessFlaskRecovery(player);
	Item &charm = Poe2CharmsTest::PutInBelt(player, slot, name);
	ProcessFlaskRecovery(player);
	EXPECT_EQ(charm._iCharges, 0) << name;
	for (int kill = 0; kill < 10; kill++) {
		GainFlaskCharges(player, 2);
		ProcessFlaskRecovery(player);
	}
	EXPECT_EQ(charm._iCharges, 20) << name;
	return charm;
}

TEST_F(Poe2CharmsTest, StoneCharmBreaksAStunInPlay)
{
	// Curtis 2026-10-28: Stone, Topaz and Bloodstone Charms in the belt, "charms 0" all run.
	Player &player = *MyPlayer;
	MyPlayerId = 0;
	Item &charm = FillFromKills(player, 4, "Stone Charm");
	// A hit for most of the hero's life stuns them nearly always: the charm breaks it.
	for (int i = 0; i < 20 && charm._iCharges > 0; i++) {
		SetRndSeed(i);
		StartPlrHit(player, Fixed26_6::fromInt(90), false);
	}
	EXPECT_EQ(charm._iCharges, 0);
	EXPECT_GT(player.poe2.stunImmuneTicks, 0);
	EXPECT_NE(player._pmode, PM_GOTHIT);
}

TEST_F(Poe2CharmsTest, TopazCharmAnswersALightningHitInPlay)
{
	Player &player = *MyPlayer;
	MyPlayerId = 0;
	Item &charm = FillFromKills(player, 4, "Topaz Charm");
	bool blocked = false;
	for (int i = 0; i < 20 && charm._iCharges > 0; i++) {
		SetRndSeed(i);
		player._pHitPoints = player._pHPBase = Fixed26_6::fromInt(100);
		PlayerMHit(player, nullptr, 0, 5, 5, MissileID::LightningBow, DamageType::Lightning, false, DeathReason::MonsterOrTrap, &blocked);
	}
	EXPECT_EQ(charm._iCharges, 0);
	EXPECT_GT(player.poe2.resistCharmTicks[1], 0);
}

TEST_F(Poe2CharmsTest, BloodstoneCharmCatchesLowLifeInPlay)
{
	Player &player = *MyPlayer;
	MyPlayerId = 0;
	Item &charm = FillFromKills(player, 4, "Bloodstone Charm");
	ApplyPlrDamage(DamageType::Physical, player, Fixed26_6::fromInt(77));
	ASSERT_EQ(player._pHitPoints.whole(), 23);
	ProcessCharms(player);
	EXPECT_EQ(charm._iCharges, 0);
	EXPECT_EQ(player._pHitPoints.whole(), 53);
}

TEST_F(Poe2CharmsTest, GoldenCharmSpendsAllItsCharges)
{
	EXPECT_EQ(CharmChargesPerUseFor(CharmEffect::Golden), MaxCharmCharges);
	EXPECT_EQ(CharmChargesPerUseFor(CharmEffect::Fire), CharmChargesPerUse);
}

TEST_F(Poe2CharmsTest, EveryCharmSurvivesASave)
{
	LoadCoreArchives();
	LoadGameArchives();
	if (!HaveMainData())
		GTEST_SKIP() << "MPQ assets (spawn.mpq or DIABDAT.MPQ) not found - skipping test";
	UnloadModArchives();
	LoadModArchives({ { "hf", "poe2" } });
	LoadItemData();
	LoadSpellData();
	LoadPlayerDataFiles();
	LoadMonsterData();
	LoadPassiveTreeData();
	paths::SetPrefPath(paths::BasePath());
	gbIsHellfireSaveGame = true;
	const std::string savePath = paths::BasePath() + "single_0.sv";
	RemoveFile(savePath.c_str());
	_uiheroinfo info {};
	info.heroclass = HeroClass::Sorcerer;
	CopyUtf8(info.name, "Charmer", sizeof(info.name));
	ASSERT_TRUE(pfile_ui_save_create(&info));
	gSaveNumber = 0;
	MyPlayerId = 0;
	pfile_read_player_from_save(0, *MyPlayer);
	Player &player = *MyPlayer;
	const std::string_view names[] = { "Sapphire Charm", "Lapis Charm", "Amethyst Charm", "Ruby Charm", "Topaz Charm", "Bloodstone Charm", "Iron Charm", "Stone Charm" };
	for (size_t slot = 0; slot < std::size(names); slot++)
		PutInBelt(player, slot, names[slot]);
	std::array<int, std::size(names)> indexes {};
	for (size_t slot = 0; slot < std::size(names); slot++)
		indexes[slot] = player.SpdList[slot].IDidx;
	pfile_write_hero(/*writeGameData=*/false);
	for (Item &item : player.SpdList)
		item.clear();
	pfile_read_player_from_save(0, player);
	for (size_t slot = 0; slot < std::size(names); slot++) {
		const Item &charm = player.SpdList[slot];
		EXPECT_EQ(charm.IDidx, indexes[slot]) << names[slot];
		EXPECT_EQ(charm._iName, names[slot]);
	}
	EXPECT_EQ(GetCharmEffect(player.SpdList[0]), CharmEffect::Cold);
	EXPECT_EQ(GetCharmEffect(player.SpdList[1]), CharmEffect::Mana);
	EXPECT_EQ(GetCharmEffect(player.SpdList[2]), CharmEffect::Magic);
	gbIsHellfireSaveGame = false;
	gbIsPoe2 = false;
	LoadPassiveTreeData();
	gbIsPoe2 = true;
	RemoveFile(savePath.c_str());
}

} // namespace
} // namespace devilution
