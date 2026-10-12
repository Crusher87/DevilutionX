#include <string>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "items.h"
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

} // namespace
} // namespace devilution
