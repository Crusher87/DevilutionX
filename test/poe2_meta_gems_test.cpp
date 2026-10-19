#include <algorithm>
#include <optional>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "missiles.h"
#include "player.h"
#include "poe2/meta_gems.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/spirit.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/support_table.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

constexpr size_t MetaRow = 2;
const SkillSlot Firebolt = SkillSlot::ForSpell(SpellID::Firebolt);
const SkillSlot Fireball = SkillSlot::ForSpell(SpellID::Fireball);
const SkillSlot CastOnDodge = SkillSlot::ForPersistent(PersistentSkill::CastOnDodge);

class Poe2MetaGemsTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		*MyPlayer = {};
		MyPlayer->poe2.clear();
		MyPlayer->_pStrength = 200;
		MyPlayer->_pMagic = 200;
		MyPlayer->_pDexterity = 200;
		// Every Spirit skull broken: 100 Spirit, enough for one meta gem.
		MyPlayer->poe2.spiritSkulls = 7;
		MyPlayer->poe2.spiritGemsOn = 0xFFFF;
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
		leveltype = DTYPE_CATHEDRAL;
		MyPlayer->setCharacterLevel(30);
		MyPlayer->position.tile = { 40, 40 };
		MyPlayer->_pMaxMana = MyPlayer->_pMana = Fixed26_6::fromInt(1000);
		MyPlayer->_pMaxManaBase = MyPlayer->_pManaBase = Fixed26_6::fromInt(1000);
		MyPlayer->_pMaxHP = MyPlayer->_pHitPoints = Fixed26_6::fromInt(1000);
		MyPlayer->_pMaxHPBase = MyPlayer->_pHPBase = Fixed26_6::fromInt(1000);
	}

	void TearDown() override
	{
		Missiles.clear();
		ClearSupportEffects();
		gbIsPoe2 = false;
		leveltype = DTYPE_TOWN;
		UnloadModArchives();
	}

	static MetaSocketResult Inside(size_t socket, SkillSlot skill, int level = 1, SkillSlot *replaced = nullptr)
	{
		SkillSlot out;
		int outLevel = 1;
		const MetaSocketResult result = SocketSkillInMeta(*MyPlayer, MetaRow, socket, skill, level, out, outLevel);
		if (replaced != nullptr)
			*replaced = out;
		return result;
	}
};

TEST_F(Poe2MetaGemsTest, SpellsGoInsideAndComeOut)
{
	Poe2PlayerData &data = MyPlayer->poe2;
	data.skillSlots[MetaRow] = CastOnDodge;
	EXPECT_TRUE(SkillTakesSupports(CastOnDodge));
	EXPECT_EQ(SpiritReservation(PersistentSkill::CastOnDodge), 100);
	EXPECT_EQ(Inside(0, Firebolt, 3), MetaSocketResult::Socketed);
	EXPECT_EQ(Inside(1, Fireball), MetaSocketResult::Socketed);
	EXPECT_EQ(MetaSkillCount(data, MetaRow), 2);
	EXPECT_EQ(MetaMaxEnergy(data, MetaRow), 2 * MetaEnergyPerSpell);
	// A spell inside casts at its own gem's level.
	EXPECT_EQ(data.skillLevel(Firebolt), 3);
	EXPECT_GE(MyPlayer->GetSpellLevel(SpellID::Firebolt), 3);

	// A spell is in one socket of the Skills panel only.
	EXPECT_EQ(Inside(1, Firebolt), MetaSocketResult::AlreadySocketed);
	data.skillSlots[0] = SkillSlot::ForSpell(SpellID::ChainLightning);
	EXPECT_EQ(Inside(1, SkillSlot::ForSpell(SpellID::ChainLightning)), MetaSocketResult::AlreadySocketed);
	// Movement spells, minions and attacks can't go inside.
	EXPECT_EQ(Inside(1, SkillSlot::ForSpell(SpellID::Teleport)), MetaSocketResult::NotTriggerable);
	EXPECT_EQ(Inside(1, SkillSlot::ForSpell(SpellID::Golem)), MetaSocketResult::NotTriggerable);
	EXPECT_EQ(Inside(1, SkillSlot::ForAttack(WeaponSkill::Cleave)), MetaSocketResult::NotTriggerable);
	// The third socket opens with a Jeweller's Orb, as for supports.
	EXPECT_EQ(Inside(2, SkillSlot::ForSpell(SpellID::ChargedBolt)), MetaSocketResult::Locked);
	SkillSlot replaced;
	int replacedLevel = 0;
	EXPECT_EQ(SocketSkillInMeta(*MyPlayer, 0, 0, SkillSlot::ForSpell(SpellID::ChargedBolt), 1, replaced, replacedLevel), MetaSocketResult::NotMeta);

	// Another spell in a taken socket hands the old one back.
	EXPECT_EQ(Inside(1, SkillSlot::ForSpell(SpellID::ChargedBolt), 1, &replaced), MetaSocketResult::Replaced);
	EXPECT_EQ(replaced, Fireball);
	EXPECT_EQ(data.skillLevel(Fireball), 0);

	// Taking a spell out shortens the bar and keeps what Energy fits.
	data.supportState.metaEnergy[MetaRow] = 150 * 100;
	int level = 0;
	EXPECT_EQ(UnsocketMetaSkill(data, MetaRow, 0, &level), Firebolt);
	EXPECT_EQ(level, 3);
	EXPECT_EQ(MetaEnergy(*MyPlayer, MetaRow), MetaEnergyPerSpell);
}

TEST_F(Poe2MetaGemsTest, SupportsInTheRowWorkOnTheSpellsInside)
{
	Poe2PlayerData &data = MyPlayer->poe2;
	data.skillSlots[MetaRow] = CastOnDodge;
	SupportGem replaced;
	// With no spell inside, nothing fits.
	EXPECT_EQ(SocketSupportGem(*MyPlayer, MetaRow, 1, SupportGem::IncreasedArea, replaced), SupportSocketResult::NotApplicable);
	ASSERT_EQ(Inside(0, Fireball), MetaSocketResult::Socketed);
	EXPECT_EQ(SocketSupportGem(*MyPlayer, MetaRow, 0, SupportGem::IncreasedArea, replaced), SupportSocketResult::SpellInSocket);
	EXPECT_EQ(FindSlotForSupport(*MyPlayer, SupportGem::IncreasedArea), MetaRow);
	ASSERT_EQ(SocketSupportGem(*MyPlayer, MetaRow, std::nullopt, SupportGem::IncreasedArea, replaced), SupportSocketResult::Socketed);
	EXPECT_EQ(data.supportsAt(MetaRow)[1], SupportGem::IncreasedArea);
	EXPECT_TRUE(data.hasSupport(Fireball, SupportGem::IncreasedArea));
	EXPECT_GT(SupportAreaRadius(*MyPlayer, Fireball, 10), 10);
	// Both sockets are taken now.
	EXPECT_EQ(FindSlotForSupport(*MyPlayer, SupportGem::Pierce), std::nullopt);

	// Supports that act when you use a skill yourself don't fit triggered spells (POE2).
	EXPECT_TRUE(SupportGemCanSupport(SupportGem::SpellEcho, Fireball));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::SpellEcho, CastOnDodge));
	// The old trigger supports fit nothing now.
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::CastOnDodge, Fireball));
	EXPECT_FALSE(SupportGemCanSupport(SupportGem::CastWhenHit, Firebolt));
}

TEST_F(Poe2MetaGemsTest, DodgingCastsTheSpellsInsideForTheirMana)
{
	Player &player = *MyPlayer;
	player.poe2.skillSlots[MetaRow] = CastOnDodge;
	ASSERT_EQ(Inside(0, Firebolt), MetaSocketResult::Socketed);
	ASSERT_TRUE(IsPersistentSkillActive(player, PersistentSkill::CastOnDodge));
	const int cost = SkillManaCost(player, Firebolt);
	ASSERT_GT(cost, 0);

	OnMetaDodge(player, 2);
	EXPECT_EQ(MetaEnergy(player, MetaRow), 2 * CastOnDodgeEnergyPerTile);
	EXPECT_TRUE(Missiles.empty());
	int rolls = 1;
	while (Missiles.empty() && rolls < 20) {
		OnMetaDodge(player, 2);
		rolls++;
	}
	// 34 Energy a roll of 2 tiles fills an 80 Energy bar on the third.
	EXPECT_EQ(rolls, 3);
	EXPECT_FALSE(Missiles.empty());
	EXPECT_EQ(MetaEnergy(player, MetaRow), 0);
	EXPECT_EQ(player._pMana.whole(), 1000 - cost);

	// Without the mana the spell doesn't go off, and the Energy still empties.
	Missiles.clear();
	player._pMana = player._pManaBase = Fixed26_6::fromInt(0);
	for (int i = 0; i < 3; i++)
		OnMetaDodge(player, 2);
	EXPECT_TRUE(Missiles.empty());
	EXPECT_EQ(MetaEnergy(player, MetaRow), 0);

	// Switched off, it gains no Energy.
	player.poe2.spiritGemsOn = 0;
	OnMetaDodge(player, 2);
	EXPECT_EQ(MetaEnergy(player, MetaRow), 0);
}

TEST_F(Poe2MetaGemsTest, HigherGemLevelsGainMoreEnergy)
{
	Player &player = *MyPlayer;
	player.poe2.skillSlots[MetaRow] = CastOnDodge;
	player.poe2.slotLevels[MetaRow] = 20;
	ASSERT_EQ(Inside(0, Firebolt), MetaSocketResult::Socketed);
	OnMetaDodge(player, 1);
	EXPECT_EQ(MetaEnergy(player, MetaRow), CastOnDodgeEnergyPerTile * (100 + (19 * MetaEnergyPercentPerLevel)) / 100);
}

TEST_F(Poe2MetaGemsTest, TakingHitsFillsCastWhenDamageTaken)
{
	Player &player = *MyPlayer;
	player.poe2.skillSlots[MetaRow] = SkillSlot::ForPersistent(PersistentSkill::CastWhenDamageTaken);
	ASSERT_EQ(Inside(0, Firebolt), MetaSocketResult::Socketed);
	OnMetaDamageTaken(player, { 43, 40 });
	OnMetaDamageTaken(player, { 43, 40 });
	EXPECT_EQ(MetaEnergy(player, MetaRow), 2 * CastWhenDamageTakenEnergy);
	EXPECT_TRUE(Missiles.empty());
	OnMetaDamageTaken(player, { 43, 40 });
	EXPECT_FALSE(Missiles.empty());
	EXPECT_EQ(MetaEnergy(player, MetaRow), 0);
}

TEST_F(Poe2MetaGemsTest, ChannellingFillsCastWhileChannellingAndStoppingDrainsIt)
{
	Player &player = *MyPlayer;
	player.poe2.skillSlots[MetaRow] = SkillSlot::ForPersistent(PersistentSkill::CastWhileChannelling);
	ASSERT_EQ(Inside(0, Fireball), MetaSocketResult::Socketed);
	player.poe2.volleyShotsLeft = 5;
	player.poe2.volleyTargetX = 43;
	player.poe2.volleyTargetY = 40;
	int ticks = 0;
	while (Missiles.empty() && ticks < 100) {
		ProcessMetaGems(player);
		ticks++;
	}
	// 60 Energy a second fills 80 in about 1.3 seconds.
	EXPECT_NEAR(ticks, 27, 1);
	EXPECT_FALSE(Missiles.empty());

	for (int i = 0; i < 20; i++)
		ProcessMetaGems(player);
	EXPECT_EQ(MetaEnergy(player, MetaRow), 60);
	player.poe2.volleyShotsLeft = 0;
	for (int i = 0; i < 20; i++)
		ProcessMetaGems(player);
	EXPECT_EQ(MetaEnergy(player, MetaRow), 35);
}

TEST_F(Poe2MetaGemsTest, OldTriggerSupportsBecomeMetaGems)
{
	Poe2PlayerData &data = MyPlayer->poe2;
	data.skillSlots[0] = Firebolt;
	data.slotSupports[0][1] = SupportGem::CastOnDodge;
	data.skillSlots[1] = Fireball;
	data.slotSupports[1][0] = SupportGem::CastWhenHit;
	data.slotSupports[1][1] = SupportGem::CastOnDodge;
	MigrateTriggerSupports(*MyPlayer);

	EXPECT_EQ(data.slotSupports[0][1], SupportGem::None);
	EXPECT_EQ(data.slotSupports[1][0], SupportGem::None);
	EXPECT_EQ(data.slotSupports[1][1], SupportGem::None);
	// One gem each, in the free rows, switched off so they take no Spirit the hero was using.
	EXPECT_EQ(data.skillSlots[2], CastOnDodge);
	EXPECT_EQ(data.skillSlots[3], SkillSlot::ForPersistent(PersistentSkill::CastWhenDamageTaken));
	EXPECT_TRUE(data.skillSlots[4].isEmpty());
	EXPECT_FALSE(data.spiritGemOn(2));
	EXPECT_FALSE(data.spiritGemOn(3));
	// The spells stay where they were.
	EXPECT_EQ(data.skillSlots[0], Firebolt);
	EXPECT_EQ(data.skillSlots[1], Fireball);

	// A second load changes nothing.
	MigrateTriggerSupports(*MyPlayer);
	EXPECT_EQ(std::count(data.skillSlots.begin(), data.skillSlots.end(), CastOnDodge), 1);
}

} // namespace
} // namespace devilution
