/**
 * POE2 Mashup, build spec step 5: the runes' numbers, bonded lines, the Zod Rune, the Runes of Legend, rune drops,
 * Cain's fusing, stacks of 10 and what loading does to an item saved before the runes took their own numbers.
 */
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "cursor.h"
#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "msg.h"
#include "player.h"
#include "poe2/area_levels.hpp"
#include "poe2/craft_state.hpp"
#include "poe2/crafting_items.hpp"
#include "poe2/currency.hpp"
#include "poe2/effect_primitives.hpp"
#include "poe2/loot.hpp"
#include "poe2/mix.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/rune_fusing.hpp"
#include "poe2/stacking.hpp"
#include "poe2/stash_layouts.hpp"
#include "qol/stash.h"
#include "quests.h"
#include "spells.h"
#include "storm/storm_net.hpp"
#include "tables/playerdat.hpp"
#include "utils/str_cat.hpp"

namespace devilution {
namespace {

class Poe2RunesTest : public ::testing::Test {
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
		LoadModifierData();
		LoadAreaLevels();
		MyPlayer->setCharacterLevel(60);
		Stash = {};
		Quests[Q_DIABLO]._qactive = QUEST_NOTAVAIL;
	}

	void TearDown() override
	{
		Stash = {};
		gbIsPoe2 = false;
		LoadModifierData();
		LoadAreaLevels();
		UnloadModArchives();
	}

	/** Fusing puts the rune in the bag, and placing an item there needs the cursor graphics from the game's MPQ. */
	static void SetUpTestSuite()
	{
		LoadCoreArchives();
		LoadGameArchives();
		haveCursors_ = HaveMainData();
		if (haveCursors_)
			InitCursor();
	}

	static bool haveCursors_;

	static _item_indexes FindBase(std::string_view name)
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iName == name && AllItemsList[i].dropRate > 0)
				return static_cast<_item_indexes>(i);
		}
		return IDI_GOLD;
	}

	static _item_indexes AugmentBase()
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			if (AllItemsList[i].iMiscId == IMISC_AUGMENT && AllItemsList[i].dropRate > 0)
				return static_cast<_item_indexes>(i);
		}
		return IDI_GOLD;
	}

	static Item MakeItem(std::string_view baseName, uint32_t seed = 7)
	{
		Item item = {};
		SetLootRarity(item, ITEM_QUALITY_NORMAL);
		SetPoe2ItemLevel(item, 60);
		SetRndSeed(seed);
		SetupAllItems(*MyPlayer, item, FindBase(baseName), AdvanceRndSeed(), 30, 1, /*onlygood=*/false, /*pregen=*/false, 0, /*forceNotUnique=*/true);
		item._iIdentified = true;
		item._iStatFlag = true;
		return item;
	}

	/** The item with its sockets, and an augment in them in turn. */
	static Item Socketed(Item item, int sockets, std::initializer_list<AugmentId> augments)
	{
		for (int i = 0; i < sockets; i++)
			EXPECT_TRUE(ApplyCurrencyOrb(CurrencyOrb::Artificer, *MyPlayer, item));
		for (const AugmentId augment : augments)
			EXPECT_TRUE(Insert(item, augment)) << GetAugment(augment).name;
		return item;
	}

	static bool Insert(Item &item, AugmentId augment)
	{
		return ApplyCurrencyOrb(CurrencyOrb::Augment, *MyPlayer, item, static_cast<uint8_t>(augment));
	}

	static Item Rune(AugmentId id, int count = 1)
	{
		Item item = {};
		InitializeItem(item, AugmentBase());
		SetCraftingItemVariant(item, static_cast<uint8_t>(id));
		FinishCraftingItem(item);
		item._iIdentified = true;
		if (count > 1)
			SetStackCount(item, count);
		return item;
	}

	static Item Rebuilt(const Item &item)
	{
		Item rebuilt;
		RecreateItem(*MyPlayer, rebuilt, item.IDidx, item._iCreateInfo, item._iSeed, item._ivalue, item.dwBuff, item._iPoe2Craft);
		return rebuilt;
	}

	static int Stat(const Item &item, Poe2ItemStat stat)
	{
		return item._iPoe2Stats[static_cast<size_t>(stat)];
	}

	static int BagCount(AugmentId id)
	{
		int count = 0;
		for (int i = 0; i < MyPlayer->_pNumInv; i++) {
			const Item &item = MyPlayer->InvList[i];
			if (item._iMiscId == IMISC_AUGMENT && GetAugmentId(item) == id)
				count += StackCount(item);
		}
		return count;
	}
};

bool Poe2RunesTest::haveCursors_ = false;

TEST_F(Poe2RunesTest, StrengthsHaveTheirLevelsAndDrop)
{
	for (int family = 0; family < static_cast<int>(RuneFamily::Count); family++) {
		for (int strength = 0; strength < 4; strength++) {
			const AugmentId id = RuneOf(static_cast<RuneFamily>(family), static_cast<RuneStrength>(strength));
			if (id == AugmentId::None || id == AugmentId::Zod || id == AugmentId::VenomRune)
				continue;
			const Augment &rune = GetAugment(id);
			constexpr int Levels[] = { 1, 15, 30, 50 };
			EXPECT_EQ(rune.level, Levels[strength]) << rune.name;
			EXPECT_EQ(rune.dropLevel, Levels[strength]) << rune.name;
			EXPECT_TRUE(rune.drops) << rune.name;
			EXPECT_GT(rune.dropWeight, 0) << rune.name;
			EXPECT_FALSE(rune.weaponText.empty() && rune.staffText.empty() && rune.armourText.empty()) << rune.name;
		}
	}
	EXPECT_EQ(GetAugment(AugmentId::Zod).dropAct, 3);
	for (int i = 0; i < RunesOfLegendCount; i++) {
		const Augment &legend = GetAugment(static_cast<AugmentId>(static_cast<int>(FirstRuneOfLegend) + i));
		EXPECT_EQ(legend.level, 50) << legend.name;
		EXPECT_TRUE(legend.bossOnly) << legend.name;
		EXPECT_EQ(legend.dropAct, 3) << legend.name;
	}
	// The soulstones wait for step 6.
	EXPECT_FALSE(GetAugment(AugmentId::ButcherSoulstone).drops);
}

TEST_F(Poe2RunesTest, ARuneDoesWhatItsKindOfItemSays)
{
	const Item axe = MakeItem("Splitting Greataxe");
	const Item staff = MakeItem("Gelid Staff");
	const Item plate = MakeItem("Full Plate");
	ASSERT_EQ(AugmentKindOf(axe), AugmentKind::Weapon);
	ASSERT_EQ(AugmentKindOf(staff), AugmentKind::Staff);
	ASSERT_EQ(AugmentKindOf(plate), AugmentKind::Armour);
	const Augment &ral = GetAugment(AugmentId::LesserRal);

	const Item axeRal = Socketed(axe, 1, { AugmentId::LesserRal });
	EXPECT_EQ(axeRal._iFMinDam - axe._iFMinDam, ral.weaponValues[0]);
	EXPECT_EQ(axeRal._iFMaxDam - axe._iFMaxDam, ral.weaponValues[1]);
	// The tooltip has only this kind's line.
	ASSERT_EQ(AugmentSocketText(axeRal).size(), 1u);
	EXPECT_EQ(AugmentSocketText(axeRal)[0], StrCat(ral.name, ": ", ral.weaponText));

	const Item plateRal = Socketed(plate, 1, { AugmentId::LesserRal });
	EXPECT_EQ(plateRal._iPLFR - plate._iPLFR, ral.armourValues[0]);
	EXPECT_EQ(AugmentSocketText(plateRal)[0], StrCat(ral.name, ": ", ral.armourText));

	const Item staffRal = Socketed(staff, 1, { AugmentId::LesserRal });
	EXPECT_EQ(Stat(staffRal, Poe2ItemStat::FireAsExtra) - Stat(staff, Poe2ItemStat::FireAsExtra), ral.staffValues[0]);
	EXPECT_EQ(AugmentSocketText(staffRal)[0], StrCat(ral.name, ": ", ral.staffText));

	// Ith does nothing in a staff, so it won't go in one.
	Item staffSocket = Socketed(staff, 1, {});
	EXPECT_FALSE(Insert(staffSocket, AugmentId::Ith));
	EXPECT_EQ(staffSocket._iPoe2Craft.augments[0], 0);
}

TEST_F(Poe2RunesTest, ARuneNeedsItsLevel)
{
	MyPlayer->setCharacterLevel(14);
	Item axe = Socketed(MakeItem("Splitting Greataxe"), 1, {});
	EXPECT_FALSE(Insert(axe, AugmentId::Ral));
	MyPlayer->setCharacterLevel(15);
	EXPECT_TRUE(Insert(axe, AugmentId::Ral));
	EXPECT_EQ(SocketAugment(axe._iPoe2Craft, 0), AugmentId::Ral);
}

TEST_F(Poe2RunesTest, BondedLinesNeedBothSocketsToMatch)
{
	const Item bare = MakeItem("Splitting Greataxe");
	Item axe = Socketed(bare, 2, { AugmentId::LesserRal, AugmentId::LesserThul });
	EXPECT_FALSE(BondedRuneFamily(axe).has_value());
	EXPECT_EQ(Stat(axe, Poe2ItemStat::IgniteMagnitude), 0);
	// Both runes' bonded lines show, greyed.
	std::vector<SocketLine> lines = AugmentSocketLines(axe);
	ASSERT_EQ(lines.size(), 4u);
	EXPECT_FALSE(lines[0].inactive);
	EXPECT_TRUE(lines[2].inactive);
	EXPECT_EQ(lines[2].text, "Bonded: 15% stronger ignites (match both sockets)");
	EXPECT_TRUE(lines[3].inactive);

	// A second Lesser Ral replaces the Thul, which is destroyed, and the two bond.
	ASSERT_TRUE(Insert(axe, AugmentId::LesserRal));
	ASSERT_EQ(BondedRuneFamily(axe), RuneFamily::Ral);
	EXPECT_EQ(Stat(axe, Poe2ItemStat::IgniteMagnitude), 15);
	EXPECT_EQ(Stat(Rebuilt(axe), Poe2ItemStat::IgniteMagnitude), 15) << "The bonded line comes back from the seed";
	lines = AugmentSocketLines(axe);
	ASSERT_EQ(lines.size(), 3u);
	EXPECT_EQ(lines[2].text, "Bonded: 15% stronger ignites");
	EXPECT_FALSE(lines[2].inactive);
	// The fire of both sockets, and no more.
	EXPECT_EQ(axe._iFMinDam - bare._iFMinDam, 2 * GetAugment(AugmentId::LesserRal).weaponValues[0]);

	// Strengths must match too.
	ASSERT_TRUE(Insert(axe, AugmentId::Ral));
	EXPECT_FALSE(BondedRuneFamily(axe).has_value());

	// Armour bonds give life and mana; a body armour has two sockets.
	const Item plate = MakeItem("Full Plate");
	const Item bonded = Socketed(plate, 2, { AugmentId::LesserOhm, AugmentId::LesserOhm });
	EXPECT_EQ(bonded._iPLHP.raw() - plate._iPLHP.raw(), Fixed10_6::fromInt(10).raw());
	EXPECT_EQ(bonded._iPLMana.raw() - plate._iPLMana.raw(), Fixed10_6::fromInt(10).raw());

	// A one-socket item has no bonded line.
	const Item cap = Socketed(MakeItem("Iron Cap"), 1, { AugmentId::LesserRal });
	EXPECT_EQ(AugmentSocketLines(cap).size(), 1u);
}

TEST_F(Poe2RunesTest, ZodRaisesARuneOnceAndNotPastTheHeroLevel)
{
	MyPlayer->setCharacterLevel(10);
	Item axe = Socketed(MakeItem("Splitting Greataxe"), 2, { AugmentId::LesserRal });
	EXPECT_FALSE(Insert(axe, AugmentId::Zod)) << "Ral Rune needs level 15";
	EXPECT_EQ(SocketAugment(axe._iPoe2Craft, 0), AugmentId::LesserRal);

	MyPlayer->setCharacterLevel(20);
	ASSERT_TRUE(Insert(axe, AugmentId::Zod));
	EXPECT_EQ(SocketAugment(axe._iPoe2Craft, 0), AugmentId::Ral);
	EXPECT_TRUE(SocketRaised(axe._iPoe2Craft, 0));
	EXPECT_EQ(axe._iPoe2Craft.augments[1], 0) << "Zod raises, it doesn't take a socket";
	EXPECT_NE(AugmentSocketText(axe)[0].find("(raised)"), std::string::npos);
	const Augment &ral = GetAugment(AugmentId::Ral);
	EXPECT_EQ(Rebuilt(axe)._iFMinDam, axe._iFMinDam);
	EXPECT_EQ(axe._iFMinDam - MakeItem("Splitting Greataxe")._iFMinDam, ral.weaponValues[0]);

	// Once per rune.
	MyPlayer->setCharacterLevel(60);
	EXPECT_FALSE(Insert(axe, AugmentId::Zod));
	EXPECT_EQ(SocketAugment(axe._iPoe2Craft, 0), AugmentId::Ral);

	// The mark is saved, and the item's values load from the seed the same.
	const auto bytes = PackCraftState(axe._iPoe2Craft);
	EXPECT_FALSE(SavedBeforeRuneNumbers(bytes));
	const Poe2Craft loaded = UnpackCraftState(bytes);
	EXPECT_EQ(loaded, axe._iPoe2Craft);
	EXPECT_EQ(loaded.raised, 1);

	// A rune put in its place can be raised again.
	ASSERT_TRUE(Insert(axe, AugmentId::LesserThul));
	ASSERT_TRUE(Insert(axe, AugmentId::LesserOrt));
	EXPECT_EQ(SocketAugment(axe._iPoe2Craft, 1), AugmentId::LesserOrt);
	EXPECT_TRUE(SocketRaised(axe._iPoe2Craft, 0));
	EXPECT_FALSE(SocketRaised(axe._iPoe2Craft, 1));
	ASSERT_TRUE(Insert(axe, AugmentId::Zod));
	EXPECT_EQ(SocketAugment(axe._iPoe2Craft, 1), AugmentId::Ort);
	EXPECT_EQ(axe._iPoe2Craft.raised, 3);
	EXPECT_EQ(UnpackCraftState(PackCraftState(axe._iPoe2Craft)), axe._iPoe2Craft);
	// A Perfect rune goes no higher, and a full socket's replacement isn't raised.
	EXPECT_EQ(NextRuneStrength(AugmentId::PerfectRal), AugmentId::None);
	EXPECT_EQ(NextRuneStrength(AugmentId::LesserShael), AugmentId::None);
}

TEST_F(Poe2RunesTest, RunesOfLegendCountOnceAndOnePlusLevelsPerWeapon)
{
	Item axe = Socketed(MakeItem("Splitting Greataxe"), 2, { AugmentId::TalicsRune });
	EXPECT_FALSE(Insert(axe, AugmentId::ZoltunKullesRune)) << "One augment that adds skill levels per weapon";
	EXPECT_FALSE(Insert(axe, AugmentId::TalicsRune)) << "The same legend twice in one item";
	EXPECT_EQ(Stat(axe, Poe2ItemStat::MeleeSkillLevels), 0) << "It counts on the hero, not the item";

	// Horazon's and Jered Cain's go only in helmets.
	Item plate = Socketed(MakeItem("Full Plate"), 1, {});
	EXPECT_FALSE(Insert(plate, AugmentId::HorazonsRune));
	EXPECT_TRUE(Insert(plate, AugmentId::KorlicsRune));
	Item cap = Socketed(MakeItem("Iron Cap"), 1, { AugmentId::KorlicsRune });
	Item helm = Socketed(MakeItem("Iron Cap", 9), 1, { AugmentId::HorazonsRune });

	// Two Korlic's count once.
	std::vector<Item> worn = { axe, plate, cap, helm };
	for (Item &item : worn)
		item._iStatFlag = true;
	Item standIn;
	UniqueRuleTotals rules {};
	ASSERT_TRUE(ApplyWornRunesOfLegend(worn, standIn, rules));
	EXPECT_EQ(Stat(standIn, Poe2ItemStat::MovementSpeed), 8);
	EXPECT_EQ(Stat(standIn, Poe2ItemStat::MeleeSkillLevels), 1);
	EXPECT_EQ(rules[static_cast<size_t>(UniqueRule::HitDamageFromMana)], 6);

	// The legends need level 50.
	MyPlayer->setCharacterLevel(49);
	Item low = Socketed(MakeItem("Full Plate"), 1, {});
	EXPECT_FALSE(Insert(low, AugmentId::KorlicsRune));
	// Bosses only: an ordinary drop never is one.
	for (uint32_t roll = 0; roll < 2000; roll++)
		EXPECT_FALSE(IsRuneOfLegend(RollAugmentDrop(Mix(roll), 80, PastStoryAct)));
	std::vector<bool> seen(RunesOfLegendCount);
	for (uint32_t roll = 0; roll < RunesOfLegendCount; roll++)
		seen[static_cast<size_t>(RollRuneOfLegend(roll)) - static_cast<size_t>(FirstRuneOfLegend)] = true;
	EXPECT_TRUE(std::all_of(seen.begin(), seen.end(), [](bool b) { return b; }));
}

TEST_F(Poe2RunesTest, DropsFollowAreaLevelAndAct)
{
	EXPECT_EQ(ActForFloor(1), 1);
	EXPECT_EQ(ActForFloor(9), 2);
	EXPECT_EQ(ActForFloor(21), 3);
	EXPECT_EQ(ActForFloor(13), 3);
	const auto counts = [](int areaLevel, int act) {
		std::map<AugmentId, int> seen;
		for (uint32_t roll = 0; roll < 5000; roll++)
			seen[RollAugmentDrop(Mix(roll + 1), areaLevel, act)]++;
		return seen;
	};
	const auto strength = [](AugmentId id) { return GetRuneKind(id) ? static_cast<int>(GetRuneKind(id)->strength) : -1; };
	// Act 1: Lesser runes only, and no Zod.
	for (const auto &[id, count] : counts(10, 1)) {
		EXPECT_NE(id, AugmentId::None);
		EXPECT_NE(id, AugmentId::Zod);
		EXPECT_LE(GetAugment(id).dropLevel, 10) << GetAugment(id).name;
		if (GetRuneKind(id))
			EXPECT_EQ(strength(id), 0) << GetAugment(id).name;
	}
	// Act 3 at area level 32: Greater runes and Zod, no Perfect.
	const std::map<AugmentId, int> act3 = counts(32, 3);
	EXPECT_GT(act3.count(AugmentId::Zod), 0u);
	EXPECT_GT(act3.count(AugmentId::GreaterRal), 0u);
	EXPECT_EQ(act3.count(AugmentId::PerfectRal), 0u);
	// Lesser runes stay the commonest.
	EXPECT_GT(act3.at(AugmentId::LesserRal), act3.at(AugmentId::GreaterRal));
	// Past the story, Perfect runes.
	EXPECT_GT(counts(60, PastStoryAct).count(AugmentId::PerfectRal), 0u);
}

TEST_F(Poe2RunesTest, RunesStackToTen)
{
	Item a = Rune(AugmentId::LesserRal, 8);
	Item b = Rune(AugmentId::LesserRal, 8);
	EXPECT_EQ(StackMax(a), RuneStackMax);
	EXPECT_FALSE(MergeStack(a, b));
	EXPECT_EQ(StackCount(a), 10);
	EXPECT_EQ(StackCount(b), 6);
	// A stack saved when runes stacked to 40 keeps its count, and takes no more.
	Item old = Rune(AugmentId::LesserRal);
	old._iCharges = 35;
	EXPECT_EQ(StackCount(old), 35);
	EXPECT_EQ(StackRoom(old), 0);
	Item one = Rune(AugmentId::LesserRal);
	EXPECT_TRUE(MergeStack(one, old) == false);
	EXPECT_EQ(StackCount(one) + StackCount(old), 36);
}

TEST_F(Poe2RunesTest, CainFusesRunes)
{
	if (!haveCursors_)
		GTEST_SKIP() << "needs the cursor graphics from spawn.mpq or DIABDAT.MPQ";
	// The bag's changes go out as commands.
	SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
	Player &player = *MyPlayer;
	ASSERT_TRUE(AutoPlaceItemInInventory(player, Rune(AugmentId::LesserRal, 4)));
	const std::optional<RuneFusion> fusion = GetRuneFusion(RuneFamily::Ral, RuneStrength::Lesser);
	ASSERT_TRUE(fusion.has_value());
	EXPECT_EQ(fusion->output, AugmentId::Ral);
	EXPECT_EQ(RuneFusionText(*fusion), "3 Lesser Ral Runes into a Ral Rune");
	ASSERT_EQ(RuneFusionsToOffer(player).size(), 1u);
	ASSERT_TRUE(FuseRunes(player, RuneFamily::Ral, RuneStrength::Lesser, 1234));
	EXPECT_EQ(BagCount(AugmentId::LesserRal), 1);
	EXPECT_EQ(BagCount(AugmentId::Ral), 1);
	EXPECT_FALSE(FuseRunes(player, RuneFamily::Ral, RuneStrength::Lesser, 1235)) << "One left";
	EXPECT_TRUE(RuneFusionsToOffer(player).empty());
	// The new rune is made with the command's seed.
	for (int i = 0; i < player._pNumInv; i++) {
		if (GetAugmentId(player.InvList[i]) == AugmentId::Ral)
			EXPECT_EQ(player.InvList[i]._iSeed, 1234 >> 1);
	}
	EXPECT_FALSE(CainFusingLine(1234).empty());

	// The stash makes up what the bag lacks.
	ASSERT_TRUE(AutoPlaceItemInInventory(player, Rune(AugmentId::LesserThul)));
	Item stored = Rune(AugmentId::LesserThul);
	SetStoredStackCount(stored, 4);
	Stash.stashList.push_back(stored);
	EXPECT_EQ(OwnedRuneCount(player, AugmentId::LesserThul), 5);
	ASSERT_TRUE(FuseRunes(player, RuneFamily::Thul, RuneStrength::Lesser, 99));
	EXPECT_EQ(OwnedRuneCount(player, AugmentId::LesserThul), 2);
	EXPECT_EQ(BagCount(AugmentId::LesserThul), 0);
	EXPECT_EQ(BagCount(AugmentId::Thul), 1);

	// Five Greater and gold make a Perfect, once Diablo is dead.
	ASSERT_TRUE(AutoPlaceItemInInventory(player, Rune(AugmentId::GreaterOrt, 5)));
	const std::optional<RuneFusion> perfect = GetRuneFusion(RuneFamily::Ort, RuneStrength::Greater);
	ASSERT_TRUE(perfect.has_value());
	EXPECT_EQ(perfect->count, 5);
	EXPECT_EQ(perfect->gold, PerfectFusionGold);
	Stash.gold = PerfectFusionGold;
	EXPECT_FALSE(FuseRunes(player, RuneFamily::Ort, RuneStrength::Greater, 5)) << "Diablo lives";
	Quests[Q_DIABLO]._qactive = QUEST_DONE;
	Stash.gold = PerfectFusionGold - 1;
	EXPECT_FALSE(FuseRunes(player, RuneFamily::Ort, RuneStrength::Greater, 5)) << "Not enough gold";
	Stash.gold = PerfectFusionGold + 10;
	ASSERT_TRUE(FuseRunes(player, RuneFamily::Ort, RuneStrength::Greater, 5));
	EXPECT_EQ(BagCount(AugmentId::GreaterOrt), 0);
	EXPECT_EQ(BagCount(AugmentId::PerfectOrt), 1);
	EXPECT_EQ(Stash.gold, 10);

	// No fusing where a strength is missing.
	EXPECT_FALSE(GetRuneFusion(RuneFamily::Ith, RuneStrength::Lesser).has_value());
	EXPECT_FALSE(GetRuneFusion(RuneFamily::Shael, RuneStrength::Lesser).has_value());
	EXPECT_FALSE(GetRuneFusion(RuneFamily::Ral, RuneStrength::Perfect).has_value());
	// The command carries the family, the strength and the seed.
	EXPECT_EQ(sizeof(TCmdFuseRunes), 7u);
}

TEST_F(Poe2RunesTest, AnItemSavedBeforeTheRunesNumbersGetsThem)
{
	// A Lesser Ral Rune saved in step 4 had the Ember Rune's numbers; the save holds today's id, packed narrow.
	const Item bare = MakeItem("Splitting Greataxe");
	Item then = Socketed(bare, 1, { AugmentId::EmberRune });
	EXPECT_EQ(then._iFMaxDam - bare._iFMaxDam, 9);
	Poe2Craft savedCraft = then._iPoe2Craft;
	savedCraft.augments[0] = static_cast<uint8_t>(AugmentId::LesserRal);
	// Today it packs with the marker of the runes' own numbers, so it won't move again; a legacy id still packs narrow,
	// as every save of before did.
	EXPECT_FALSE(SavedBeforeRuneNumbers(PackCraftState(savedCraft)));
	EXPECT_TRUE(SavedBeforeRuneNumbers(PackCraftState(then._iPoe2Craft)));

	Item loaded = then;
	loaded._iPoe2Craft.augments[0] = static_cast<uint8_t>(AugmentId::LesserRal);
	loaded._iPLToHit += 3; // something else changed it after it dropped, which stays
	const Poe2Craft saved = loaded._iPoe2Craft;
	ASSERT_TRUE(MigrateRuneNumbers(loaded, saved));
	const Item now = Rebuilt(loaded);
	EXPECT_EQ(loaded._iFMinDam, now._iFMinDam);
	EXPECT_EQ(loaded._iFMaxDam, now._iFMaxDam);
	EXPECT_EQ(loaded._iPLToHit, now._iPLToHit + 3);
	// Nothing moves for an id whose numbers didn't change.
	Item soul = Socketed(MakeItem("Full Plate"), 1, { AugmentId::LordOfThePit });
	EXPECT_FALSE(MigrateRuneNumbers(soul, soul._iPoe2Craft));
}

TEST_F(Poe2RunesTest, TheGearStatsReachTheirPrimitives)
{
	Player &player = *MyPlayer;
	const auto set = [&player](Poe2ItemStat stat, int value) { player.poe2.itemStats[static_cast<size_t>(stat)] = static_cast<int16_t>(value); };
	EXPECT_EQ(IgniteMagnitudePercent(player), 100);
	set(Poe2ItemStat::IgniteMagnitude, 15);
	EXPECT_EQ(IgniteMagnitudePercent(player), 115);
	set(Poe2ItemStat::ShockMagnitude, 15);
	EXPECT_EQ(ShockMagnitudeBonus(player), 15);
	set(Poe2ItemStat::ManaCostAsLife, 140);
	EXPECT_EQ(GearManaCostAsLifePercent(player), 100);
	set(Poe2ItemStat::LifeRegenRate, 20);
	EXPECT_EQ(GearLifeRegenPerTenThousand(player), 20);
	set(Poe2ItemStat::AttackCritChance, 5);
	EXPECT_EQ(GearAttackCritTenths(player), 5);
	set(Poe2ItemStat::MovementSpeed, 8);
	EXPECT_EQ(GearMovementSpeedPercent(player), 8);
	gbIsPoe2 = false;
	EXPECT_EQ(IgniteMagnitudePercent(player), 100);
	EXPECT_EQ(GearMovementSpeedPercent(player), 0);
	gbIsPoe2 = true;
}

} // namespace
} // namespace devilution
