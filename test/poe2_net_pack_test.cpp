/**
 * @file poe2_net_pack_test.cpp
 *
 * Build spec step 9: items sent between players carry their POE2 crafting state (sockets, runes, soulstones, a Zod
 * Rune's mark, essence marks, a hidden desecrated modifier, enchantments), so both sides have the same item.
 */
#include <algorithm>
#include <cstring>
#include <string_view>

#include <gtest/gtest.h>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "cursor.h"
#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "msg.h"
#include "pack.h"
#include "player.h"
#include "poe2/craft_state.hpp"
#include "poe2/crafting_items.hpp"
#include "poe2/currency.hpp"
#include "poe2/desecration.hpp"
#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/stash_tabs.hpp"
#include "qol/stash.h"
#include "spells.h"
#include "tables/playerdat.hpp"
#include "utils/endian_swap.hpp"
#include "utils/utf8.hpp"

namespace devilution {
namespace {

class Poe2NetPackTest : public ::testing::Test {
public:
	void SetUp() override
	{
		LoadCoreArchives();
		LoadGameArchives();
		haveMainData_ = HaveMainData();
		Players.resize(2);
		MyPlayer = &Players[0];
		MyPlayerId = 0;
		Players[0] = {};
		Players[1] = {};
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
		LoadMonsterData();
		LoadPassiveTreeData();
		LoadModifierData();
		MyPlayer->setCharacterLevel(60);
	}

	void TearDown() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		MyPlayer->poe2.clear();
		gbIsPoe2 = false;
		gbIsMultiplayer = false;
		Stash = {};
		StashTabs.reset();
		LoadModifierData();
		UnloadModArchives();
	}

	bool haveMainData_ = false;

	static _item_indexes FindBase(std::string_view name)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == name && AllItemsList[i].dropRate > 0)
				return static_cast<_item_indexes>(i);
		}
		return IDI_GOLD;
	}

	static Item MakeItem(std::string_view baseName, uint32_t seed)
	{
		Item item = {};
		SetLootRarity(item, ITEM_QUALITY_NORMAL);
		SetPoe2ItemLevel(item, 75);
		SetRndSeed(seed);
		SetupAllItems(*MyPlayer, item, FindBase(baseName), AdvanceRndSeed(), 30, 1, /*onlygood=*/true, /*pregen=*/false, 0, /*forceNotUnique=*/true);
		item._iIdentified = true;
		EXPECT_FALSE(item.isEmpty()) << baseName;
		return item;
	}

	static bool Orb(CurrencyOrb orb, Item &item, uint8_t variant = 0)
	{
		return ApplyCurrencyOrb(orb, *MyPlayer, item, variant);
	}

	static bool Essence(EssenceKind kind, Item &item, EssenceTier tier = EssenceTier::Normal)
	{
		return Orb(CurrencyOrb::Essence, item, EssenceVariant(kind, tier));
	}

	/** Body armour with everything crafting keeps: essence lines, Sin, Hatred and Lies, two runes, one raised by Zod. */
	static Item CraftedPlate()
	{
		Item plate = MakeItem("Full Plate", 11);
		EXPECT_TRUE(Orb(CurrencyOrb::Transmutation, plate));
		EXPECT_TRUE(Essence(EssenceKind::Body, plate, EssenceTier::Greater));
		EXPECT_TRUE(Essence(EssenceKind::Sin, plate));
		EXPECT_TRUE(Essence(EssenceKind::Hatred, plate));
		EXPECT_TRUE(Essence(EssenceKind::Lies, plate));
		EXPECT_TRUE(Orb(CurrencyOrb::Artificer, plate));
		EXPECT_TRUE(Orb(CurrencyOrb::Artificer, plate));
		EXPECT_TRUE(Orb(CurrencyOrb::Augment, plate, static_cast<uint8_t>(AugmentId::LesserOhm)));
		EXPECT_TRUE(Orb(CurrencyOrb::Augment, plate, static_cast<uint8_t>(AugmentId::GreaterAmn)));
		EXPECT_TRUE(Orb(CurrencyOrb::Augment, plate, static_cast<uint8_t>(AugmentId::Zod)));
		return plate;
	}

	/** A corrupted helmet the Essence of Destruction gave two enchantments, with a soulstone in its socket. */
	static Item CraftedHelmet()
	{
		Item helmet = MakeItem("Iron Cap", 21);
		EXPECT_TRUE(Orb(CurrencyOrb::Alchemy, helmet));
		EXPECT_TRUE(Orb(CurrencyOrb::Artificer, helmet));
		EXPECT_TRUE(Orb(CurrencyOrb::Augment, helmet, static_cast<uint8_t>(AugmentId::WitchfireSoulstone)));
		EXPECT_TRUE(Essence(EssenceKind::Destruction, helmet));
		// Corruption has other outcomes; the item's seed decides, so try copies until one gets its enchantments.
		for (int attempt = 0; attempt < 50; attempt++) {
			Item copy = helmet;
			SetRndSeed(1000 + attempt);
			if (Orb(CurrencyOrb::Vaal, copy) && SecondEnchantId(copy._iPoe2Craft) != 0)
				return copy;
		}
		ADD_FAILURE() << "no corruption gave the helmet two enchantments";
		return helmet;
	}

	/** The item sent as another player's gear would be (PackNetItem), and as the other side rebuilds it. */
	static Item SentAndRebuilt(const Item &item)
	{
		ItemNetPack packed {};
		PackNetItem(item, packed);
		Item rebuilt;
		EXPECT_TRUE(UnPackNetItem(*MyPlayer, packed, rebuilt));
		return rebuilt;
	}

	static void ExpectSameItem(const Item &sent, const Item &got)
	{
		EXPECT_EQ(got._iPoe2Craft, sent._iPoe2Craft) << sent._iIName;
		EXPECT_STREQ(got._iIName, sent._iIName);
		EXPECT_EQ(got._iMagical, sent._iMagical);
		EXPECT_EQ(got._iAC, sent._iAC);
		EXPECT_EQ(got._iMinDam, sent._iMinDam);
		EXPECT_EQ(got._iMaxDam, sent._iMaxDam);
		EXPECT_EQ(got._iPLAC, sent._iPLAC);
		EXPECT_EQ(got._iPLHP, sent._iPLHP);
		EXPECT_EQ(got._iPLMana, sent._iPLMana);
		EXPECT_EQ(got._iPLFR, sent._iPLFR);
		EXPECT_EQ(got._iPLLR, sent._iPLLR);
		EXPECT_EQ(got._iPLMR, sent._iPLMR);
		EXPECT_EQ(got._iMinStr, sent._iMinStr);
		EXPECT_TRUE(got._iPoe2Stats == sent._iPoe2Stats) << sent._iIName;
		EXPECT_EQ(got._iPoe2Mods.size(), sent._iPoe2Mods.size());
	}
};

TEST_F(Poe2NetPackTest, AFullyCraftedItemCrossesTheNetworkWhole)
{
	const Item plate = CraftedPlate();
	ASSERT_EQ(plate._iPoe2Craft.sockets, 2);
	ASSERT_EQ(SocketAugment(plate._iPoe2Craft, 0), AugmentId::Ohm) << "Zod raised the Lesser Ohm Rune";
	ASSERT_TRUE(SocketRaised(plate._iPoe2Craft, 0));
	ASSERT_EQ(SocketAugment(plate._iPoe2Craft, 1), AugmentId::GreaterAmn);
	ASSERT_TRUE(HasHatredMark(plate._iPoe2Craft));
	ASSERT_TRUE(SinNotable(plate._iPoe2Craft).has_value());
	ASSERT_TRUE(HasUnrevealedMod(plate));

	const Item helmet = CraftedHelmet();
	ASSERT_NE(SecondEnchantId(helmet._iPoe2Craft), 0);
	ASSERT_NE(CraftEnchantId(helmet._iPoe2Craft), 0);
	// A shield waiting for its corruption keeps the Essence of Destruction's mark.
	Item shield = MakeItem("Wooden Tower Shield", 41);
	ASSERT_TRUE(Orb(CurrencyOrb::Alchemy, shield));
	ASSERT_TRUE(Essence(EssenceKind::Destruction, shield));
	ASSERT_TRUE(HasDestructionMark(shield._iPoe2Craft));

	for (const Item &item : { plate, helmet, shield }) {
		const Item rebuilt = SentAndRebuilt(item);
		ExpectSameItem(item, rebuilt);
		EXPECT_EQ(EssenceMarkText(rebuilt), EssenceMarkText(item));
		EXPECT_EQ(AugmentSocketText(rebuilt), AugmentSocketText(item));
	}
	const Item plateCopy = SentAndRebuilt(plate);
	EXPECT_TRUE(HasUnrevealedMod(plateCopy));
	EXPECT_EQ(SinNotable(plateCopy._iPoe2Craft), SinNotable(plate._iPoe2Craft));
	EXPECT_EQ(SecondEnchantRoll(SentAndRebuilt(helmet)._iPoe2Craft), SecondEnchantRoll(helmet._iPoe2Craft));
	EXPECT_TRUE(HasDestructionMark(SentAndRebuilt(shield)._iPoe2Craft));

	// The other messages that carry items (a drop, a pick-up, a changed slot) use the same bytes.
	TItem drop {};
	drop.wIndx = static_cast<_item_indexes>(Swap16LE(plate.IDidx));
	drop.wCI = Swap16LE(plate._iCreateInfo);
	drop.dwSeed = Swap32LE(plate._iSeed);
	PrepareItemForNetwork(plate, drop);
	Item dropped;
	RecreateItem(*MyPlayer, drop, dropped);
	ExpectSameItem(plate, dropped);
	// Without those bytes the other side had only the seed's item: no sockets, no runes, no marks.
	TItem seedOnly = drop;
	std::memset(seedOnly.craft, 0, sizeof(seedOnly.craft));
	std::memset(seedOnly.craftExtension, 0, sizeof(seedOnly.craftExtension));
	Item bare;
	RecreateItem(*MyPlayer, seedOnly, bare);
	EXPECT_EQ(bare._iPoe2Craft.sockets, 0);
	EXPECT_FALSE(HasHatredMark(bare._iPoe2Craft));

	// And a hero wearing them has the same stats on both sides.
	Player &hero = *MyPlayer;
	hero._pBaseStr = hero._pStrength = 200;
	hero._pBaseDex = hero._pDexterity = 200;
	hero._pBaseMag = hero._pMagic = 200;
	hero.InvBody[INVLOC_CHEST] = plate;
	hero.InvBody[INVLOC_HEAD] = helmet;
	for (Item &item : hero.InvBody)
		item._iStatFlag = !item.isEmpty();
	CalcPlrItemVals(hero, false);
	const auto stats = hero.poe2.itemStats;
	const int armour = hero._pIAC;
	const std::optional<PassiveNodeId> notable = WornItemNotable(hero);
	ASSERT_TRUE(notable.has_value());
	hero.InvBody[INVLOC_CHEST] = SentAndRebuilt(plate);
	hero.InvBody[INVLOC_HEAD] = SentAndRebuilt(helmet);
	for (Item &item : hero.InvBody)
		item._iStatFlag = !item.isEmpty();
	CalcPlrItemVals(hero, false);
	EXPECT_TRUE(hero.poe2.itemStats == stats);
	EXPECT_EQ(hero._pIAC, armour);
	EXPECT_EQ(WornItemNotable(hero), notable);
}

TEST_F(Poe2NetPackTest, AnItemWithoutCraftingPacksAsBefore)
{
	const Item plain = MakeItem("Full Plate", 31);
	ASSERT_TRUE(plain._iPoe2Craft.isEmpty());
	ASSERT_NE(plain.dwBuff & CF_POE2, 0U);

	ItemNetPack packed {};
	std::memset(&packed, 0xAB, sizeof(packed));
	PackNetItem(plain, packed);
	// Diablo's fields as before, and nothing but zeros after them.
	EXPECT_EQ(Swap16LE(packed.item.wIndx), plain.IDidx);
	EXPECT_EQ(Swap16LE(packed.item.wCI), plain._iCreateInfo);
	EXPECT_EQ(Swap32LE(packed.item.dwSeed), plain._iSeed);
	EXPECT_EQ(packed.item.bId, 1);
	EXPECT_EQ(packed.item.bDur, plain._iDurability);
	EXPECT_EQ(packed.item.bMDur, plain._iMaxDur);
	EXPECT_EQ(Swap32LE(packed.item.dwBuff), plain.dwBuff);
	EXPECT_TRUE(std::all_of(std::begin(packed.item.craft), std::end(packed.item.craft), [](uint8_t b) { return b == 0; }));
	EXPECT_TRUE(std::all_of(std::begin(packed.item.craftExtension), std::end(packed.item.craftExtension), [](uint8_t b) { return b == 0; }));

	Item rebuilt;
	ASSERT_TRUE(UnPackNetItem(*MyPlayer, packed, rebuilt));
	ExpectSameItem(plain, rebuilt);
	EXPECT_TRUE(rebuilt._iPoe2Craft.isEmpty());

	// Bytes that don't check out give no crafting, so the item is what its seed rolls.
	ItemNetPack garbled = packed;
	const Item crafted = CraftedPlate();
	TItem craftedMessage {};
	PrepareItemForNetwork(crafted, craftedMessage);
	std::memcpy(garbled.item.craft, craftedMessage.craft, sizeof(garbled.item.craft));
	garbled.item.craft[5] ^= 0x5A;
	EXPECT_TRUE(NetItemCraft(garbled.item).isEmpty());

	// And an item that isn't a POE2 one never reads them.
	TItem vanilla = craftedMessage;
	vanilla.dwBuff = Swap32LE(Swap32LE(vanilla.dwBuff) & ~CF_POE2);
	EXPECT_TRUE(NetItemCraft(vanilla).isEmpty());
	EXPECT_FALSE(NetItemCraft(craftedMessage).isEmpty());
}

TEST_F(Poe2NetPackTest, AHeroWithCraftedGearJoinsWithTheSameStats)
{
	if (!haveMainData_)
		GTEST_SKIP() << "MPQ assets (spawn.mpq or DIABDAT.MPQ) not found - skipping test";
	gbIsMultiplayer = true;
	Player &hero = *MyPlayer;
	// CreatePlayer loads and frees the cursor pictures itself, so they load only after it.
	CreatePlayer(hero, HeroClass::Warrior);
	InitCursor();
	hero.setCharacterLevel(60);
	CopyUtf8(hero._pName, "Crafter", sizeof(hero._pName));
	hero._pBaseStr = 200;
	hero._pBaseDex = 200;
	hero._pBaseMag = 200;
	// Only the gear matters here: an empty bag and belt.
	for (Item &item : hero.SpdList)
		item.clear();
	for (Item &item : hero.InvList)
		item.clear();
	hero._pNumInv = 0;
	std::fill(std::begin(hero.InvGrid), std::end(hero.InvGrid), 0);
	hero.InvBody[INVLOC_CHEST] = CraftedPlate();
	hero.InvBody[INVLOC_HEAD] = CraftedHelmet();
	CalcPlrInv(hero, false);

	PlayerNetPack packed {};
	PackNetPlayer(packed, hero);
	Player &other = Players[1];
	ASSERT_TRUE(UnPackNetPlayer(packed, other)) << "the joining hero's stats match what the other side rebuilds";
	EXPECT_EQ(other.InvBody[INVLOC_CHEST]._iPoe2Craft, hero.InvBody[INVLOC_CHEST]._iPoe2Craft);
	EXPECT_EQ(other.InvBody[INVLOC_HEAD]._iPoe2Craft, hero.InvBody[INVLOC_HEAD]._iPoe2Craft);
	EXPECT_EQ(other._pIAC, hero._pIAC);
	EXPECT_EQ(other._pMaxHP, hero._pMaxHP);
	FreeCursor();
}

} // namespace
} // namespace devilution
