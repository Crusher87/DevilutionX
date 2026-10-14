#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/jewels.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/passive_tree.hpp"
#include "spells.h"
#include "tables/playerdat.hpp"

namespace devilution {
namespace {

class Poe2JewelsTest : public ::testing::Test {
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
		LoadModifierData();

		Player &player = *MyPlayer;
		player._pClass = HeroClass::Warrior;
		player.setCharacterLevel(10);
		player._pBaseStr = player._pStrength = 30;
		player._pBaseMag = player._pMagic = 10;
		player._pBaseDex = player._pDexterity = 20;
		player._pBaseVit = player._pVitality = 25;
		player._pMaxHPBase = player._pHPBase = Fixed26_6::fromInt(100);
		player._pMaxManaBase = player._pManaBase = Fixed26_6::fromInt(20);
		// Matches the unarmed light armor graphic so recalculating stats loads no art.
		player._pgfxnum = 0;
	}

	void TearDown() override
	{
		gbIsPoe2 = false;
		LoadPassiveTreeData();
		LoadModifierData();
		UnloadModArchives();
	}

	static _item_indexes FindBase(std::string_view name)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == name && AllItemsList[i].iMiscId == IMISC_JEWEL)
				return static_cast<_item_indexes>(i);
		}
		ADD_FAILURE() << name;
		return IDI_GOLD;
	}

	static Item MakeJewel(std::string_view name, uint32_t seed, item_quality rarity, int itemLevel)
	{
		Item item = {};
		SetLootRarity(item, rarity);
		SetPoe2ItemLevel(item, itemLevel);
		SetRndSeed(seed);
		SetupAllItems(*MyPlayer, item, FindBase(name), AdvanceRndSeed(), 30, 1, /*onlygood=*/true, /*pregen=*/false, 0, /*forceNotUnique=*/true);
		item._iIdentified = true;
		return item;
	}

	static std::vector<Poe2ItemMod> Mods(const Item &item)
	{
		std::vector<Poe2ItemMod> mods;
		for (const Poe2ItemMod &mod : item._iPoe2Mods) {
			if (mod.family != 0)
				mods.push_back(mod);
		}
		return mods;
	}

	static PassiveNodeId FirstSocket()
	{
		for (const PassiveNode &node : PassiveTree()) {
			if (node.kind == PassiveKind::JewelSocket)
				return node.id;
		}
		ADD_FAILURE() << "no jewel socket";
		return 0;
	}
};

TEST_F(Poe2JewelsTest, JewelsRollModifiersOfTheirColour)
{
	const std::vector<std::pair<std::string_view, ModSlot>> colours = { { "Ruby", ModSlot::Ruby }, { "Emerald", ModSlot::Emerald }, { "Sapphire", ModSlot::Sapphire } };
	for (const auto &[name, slot] : colours) {
		std::set<std::string> seen;
		for (uint32_t seed = 1; seed <= 60; seed++) {
			const Item rare = MakeJewel(name, seed, ITEM_QUALITY_RARE, 50);
			ASSERT_TRUE(IsJewel(rare));
			ASSERT_EQ(ItemModSlot(rare), slot);
			const std::vector<Poe2ItemMod> mods = Mods(rare);
			EXPECT_GE(mods.size(), 3U) << name;
			EXPECT_LE(mods.size(), 4U) << name;
			int prefixes = 0;
			for (const Poe2ItemMod &mod : mods) {
				const ModFamily &family = *ItemModFamily(mod);
				EXPECT_TRUE((family.slots & ((1U << static_cast<int>(slot)) | (1U << static_cast<int>(ModSlot::Jewel)))) != 0) << family.id;
				prefixes += family.prefix ? 1 : 0;
				seen.insert(family.id);
			}
			EXPECT_LE(prefixes, MaxRareJewelModsPerSide);
			EXPECT_LE(static_cast<int>(mods.size()) - prefixes, MaxRareJewelModsPerSide);

			const Item magic = MakeJewel(name, seed, ITEM_QUALITY_MAGIC, 50);
			EXPECT_GE(Mods(magic).size(), 1U);
			EXPECT_LE(Mods(magic).size(), 2U);
		}
		// Each colour rolls its own modifiers and never another colour's.
		std::string_view own = "sapphire_";
		if (slot == ModSlot::Ruby)
			own = "ruby_";
		else if (slot == ModSlot::Emerald)
			own = "emerald_";
		bool hasOwn = false;
		for (const std::string &id : seen) {
			hasOwn = hasOwn || id.rfind(own, 0) == 0;
			for (const std::string_view other : { "ruby_", "emerald_", "sapphire_" }) {
				if (other != own)
					EXPECT_NE(id.rfind(other, 0), 0U) << name << " rolled " << id;
			}
		}
		EXPECT_TRUE(hasOwn) << name;
	}
}

TEST_F(Poe2JewelsTest, SocketedJewelsAddTheirStats)
{
	Player &player = *MyPlayer;
	CalcPlrInv(player, false);
	const int life = player._pMaxHP.whole();

	Item jewel = MakeJewel("Ruby", 7, ITEM_QUALITY_MAGIC, 30);
	jewel._iPoe2Mods = {};
	jewel._iPoe2Stats = {};
	const std::optional<uint8_t> family = FindModifierFamily("jewel_maximum_life_pct");
	ASSERT_TRUE(family.has_value());
	jewel._iPoe2Mods[0] = { static_cast<uint8_t>(*family + 1), 1, { 10, 0 } };
	ApplyItemMod(jewel, jewel._iPoe2Mods[0]);

	const PassiveNodeId socket = FirstSocket();
	// The socket has to be allocated first.
	Item held = jewel;
	EXPECT_FALSE(SocketJewel(player, socket, held));
	EXPECT_FALSE(FreeJewelSocket(player).has_value());
	player.poe2.passives.set(socket);
	EXPECT_EQ(FreeJewelSocket(player), socket);
	ASSERT_TRUE(SocketJewel(player, socket, held));
	EXPECT_TRUE(held.isEmpty());
	EXPECT_FALSE(FreeJewelSocket(player).has_value());
	ASSERT_EQ(ActiveJewels(player).size(), 1U);
	EXPECT_GT(player._pMaxHP.whole(), life);
	EXPECT_FALSE(CanRefundPassive(player, socket));

	// Something that isn't a jewel doesn't go in.
	Item other = {};
	EXPECT_FALSE(SocketJewel(player, socket, other));

	const std::optional<Item> taken = TakeJewel(player, socket);
	ASSERT_TRUE(taken.has_value());
	EXPECT_EQ(taken->_iSeed, jewel._iSeed);
	EXPECT_EQ(SocketedJewel(player, socket), nullptr);
	EXPECT_EQ(player._pMaxHP.whole(), life);
}

TEST_F(Poe2JewelsTest, JewelsDoNothingInMultiplayer)
{
	Player &player = *MyPlayer;
	const PassiveNodeId socket = FirstSocket();
	player.poe2.passives.set(socket);
	Item held = MakeJewel("Sapphire", 3, ITEM_QUALITY_RARE, 30);
	ASSERT_TRUE(SocketJewel(player, socket, held));
	gbIsMultiplayer = true;
	EXPECT_TRUE(ActiveJewels(player).empty());
	gbIsMultiplayer = false;
}

} // namespace
} // namespace devilution
