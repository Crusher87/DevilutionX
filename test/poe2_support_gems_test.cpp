#include <string>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/support_gems.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2SupportGemsTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
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
};

TEST_F(Poe2SupportGemsTest, EveryGemHasAnItem)
{
	for (uint8_t i = 1; i < static_cast<uint8_t>(SupportGem::Count); i++) {
		const auto gem = static_cast<SupportGem>(i);
		Item item;
		ASSERT_TRUE(CreateSupportGemItem(item, gem)) << "no item for gem " << static_cast<int>(i);
		EXPECT_EQ(SupportGemForItem(item), gem);
		EXPECT_FALSE(SupportGemName(gem).empty());
		EXPECT_FALSE(SupportGemDescription(gem).empty());

		// Gems are stored in the hero file by index and seed, so they must rebuild the same.
		Item recreated;
		RecreateItem(*MyPlayer, recreated, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff);
		EXPECT_EQ(SupportGemForItem(recreated), gem);
		EXPECT_EQ(std::string(recreated._iName), std::string(item._iName));
	}
}

TEST_F(Poe2SupportGemsTest, LinkingRules)
{
	Poe2PlayerData data;
	SupportGem replaced;

	EXPECT_EQ(LinkSupportGem(data, SpellID::Invalid, SupportGem::Empower, replaced), SupportLinkResult::NoReadiedSpell);
	EXPECT_EQ(LinkSupportGem(data, SpellID::Healing, SupportGem::MultipleProjectiles, replaced), SupportLinkResult::NotApplicable);

	EXPECT_EQ(LinkSupportGem(data, SpellID::Firebolt, SupportGem::MultipleProjectiles, replaced), SupportLinkResult::Linked);
	EXPECT_EQ(LinkSupportGem(data, SpellID::Firebolt, SupportGem::MultipleProjectiles, replaced), SupportLinkResult::AlreadyLinked);
	EXPECT_EQ(LinkSupportGem(data, SpellID::Firebolt, SupportGem::Empower, replaced), SupportLinkResult::Linked);
	EXPECT_TRUE(data.hasSupport(SpellID::Firebolt, SupportGem::Empower));
	EXPECT_FALSE(data.hasSupport(SpellID::Fireball, SupportGem::Empower));

	// A third gem swaps out the oldest one.
	EXPECT_EQ(LinkSupportGem(data, SpellID::Firebolt, SupportGem::FasterCasting, replaced), SupportLinkResult::Replaced);
	EXPECT_EQ(replaced, SupportGem::MultipleProjectiles);
	EXPECT_FALSE(data.hasSupport(SpellID::Firebolt, SupportGem::MultipleProjectiles));
	EXPECT_TRUE(data.hasSupport(SpellID::Firebolt, SupportGem::Empower));
	EXPECT_TRUE(data.hasSupport(SpellID::Firebolt, SupportGem::FasterCasting));
}

TEST_F(Poe2SupportGemsTest, SupportsChangeTheSpell)
{
	Player &player = *MyPlayer;
	player = {};
	player._pClass = HeroClass::Sorcerer;
	player._pSplLvl[static_cast<int8_t>(SpellID::Firebolt)] = 5;
	player._pSplLvl[static_cast<int8_t>(SpellID::Healing)] = 5;

	const Fixed26_6 baseMana = GetManaAmount(player, SpellID::Firebolt);
	EXPECT_EQ(SupportExtraProjectiles(player, SpellID::Firebolt), 0);
	EXPECT_EQ(SupportSpellLevelBonus(player, SpellID::Firebolt), 0);
	EXPECT_EQ(SupportCastSkippedFrames(player, SpellID::Firebolt), 0);

	SupportGem replaced;
	LinkSupportGem(player.poe2, SpellID::Firebolt, SupportGem::MultipleProjectiles, replaced);
	LinkSupportGem(player.poe2, SpellID::Firebolt, SupportGem::Empower, replaced);
	EXPECT_EQ(SupportExtraProjectiles(player, SpellID::Firebolt), 2);
	EXPECT_EQ(SupportSpellLevelBonus(player, SpellID::Firebolt), 2);
	EXPECT_EQ(SupportManaCostPercent(player, SpellID::Firebolt), 195);
	EXPECT_EQ(GetManaAmount(player, SpellID::Firebolt).raw(), baseMana.raw() * 195 / 100);

	// Supports belong to one spell only.
	EXPECT_EQ(SupportManaCostPercent(player, SpellID::Healing), 100);

	LinkSupportGem(player.poe2, SpellID::Healing, SupportGem::Efficiency, replaced);
	LinkSupportGem(player.poe2, SpellID::Healing, SupportGem::FasterCasting, replaced);
	EXPECT_EQ(SupportManaCostPercent(player, SpellID::Healing), 72);
	EXPECT_EQ(SupportCastSkippedFrames(player, SpellID::Healing), 3);
	EXPECT_EQ(SupportExtraProjectiles(player, SpellID::Healing), 0);
}

TEST_F(Poe2SupportGemsTest, ProjectileSupportsOnlyFitBolts)
{
	for (const SupportGem gem : { SupportGem::Pierce, SupportGem::Fork, SupportGem::Chain, SupportGem::ConcentratedEffect, SupportGem::FasterProjectiles }) {
		EXPECT_TRUE(SupportGemCanSupport(gem, SpellID::Firebolt)) << SupportGemName(gem);
		EXPECT_TRUE(SupportGemCanSupport(gem, SpellID::Fireball)) << SupportGemName(gem);
		EXPECT_FALSE(SupportGemCanSupport(gem, SpellID::Healing)) << SupportGemName(gem);
		EXPECT_FALSE(SupportGemCanSupport(gem, SpellID::Lightning)) << SupportGemName(gem);
	}
}

TEST_F(Poe2SupportGemsTest, SupportsShapeTheProjectile)
{
	Player &player = *MyPlayer;
	player = {};
	SupportGem replaced;
	LinkSupportGem(player.poe2, SpellID::Firebolt, SupportGem::Pierce, replaced);
	LinkSupportGem(player.poe2, SpellID::Firebolt, SupportGem::FasterProjectiles, replaced);

	Missile missile {};
	missile.position.velocity = { 100, -50 };
	ApplySupportsToMissile(missile, player, SpellID::Firebolt);
	EXPECT_EQ(missile.poe2.spell, SpellID::Firebolt);
	EXPECT_EQ(missile.poe2.piercesLeft, PierceCount);
	EXPECT_EQ(missile.poe2.chainsLeft, 0);
	EXPECT_EQ(missile.position.velocity, (Displacement { 160, -80 }));

	// Spells without supports leave their missiles alone.
	Missile plain {};
	plain.position.velocity = { 100, -50 };
	ApplySupportsToMissile(plain, player, SpellID::Fireball);
	EXPECT_EQ(plain.poe2.spell, SpellID::Invalid);
	EXPECT_EQ(plain.position.velocity, (Displacement { 100, -50 }));
}

TEST_F(Poe2SupportGemsTest, PiercingSkipsTheMonsterItHit)
{
	Player &player = *MyPlayer;
	player = {};
	SupportGem replaced;
	LinkSupportGem(player.poe2, SpellID::Firebolt, SupportGem::Pierce, replaced);
	LinkSupportGem(player.poe2, SpellID::Firebolt, SupportGem::ConcentratedEffect, replaced);

	Missile missile {};
	ApplySupportsToMissile(missile, player, SpellID::Firebolt);
	int minDamage = 10;
	int maxDamage = 20;
	ScaleSupportedMissileDamage(missile, minDamage, maxDamage);
	EXPECT_EQ(minDamage, 14);
	EXPECT_EQ(maxDamage, 28);

	const Monster &first = Monsters[3];
	const Monster &second = Monsters[4];
	EXPECT_FALSE(SupportedMissileIgnoresMonster(missile, 3));
	EXPECT_TRUE(OnSupportedMissileHit(missile, first));
	EXPECT_TRUE(SupportedMissileIgnoresMonster(missile, 3));
	EXPECT_TRUE(OnSupportedMissileHit(missile, second));
	// Out of pierces, the third hit stops the projectile.
	EXPECT_FALSE(OnSupportedMissileHit(missile, first));
}

} // namespace
} // namespace devilution
