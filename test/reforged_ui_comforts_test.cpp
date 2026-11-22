/**
 * @file reforged_ui_comforts_test.cpp
 *
 * Sanctuary Reforged's share of POE2 Mashup's comfort features, filled with Diablo's own facts: the monster banner,
 * the newer item tooltip with Shift to compare, rarity-coloured inventory slots, right-click to put an item on,
 * held bow shots along the cursor with WASD, and camera shake on Diablo's own big moments.
 */
#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "ui_test.hpp"

#include "control/control.hpp"
#include "controls/wasd_movement.hpp"
#include "diablo.h"
#include "engine/random.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/surface.hpp"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "inv.h"
#include "items.h"
#include "monster.h"
#include "msg.h"
#include "multi.h"
#include "panels/monster_banner.hpp"
#include "player.h"
#include "poe2/aim.hpp"
#include "poe2/camera_shake.hpp"
#include "storm/storm_net.hpp"
#include "tables/monstdat.h"
#include "track.h"
#include "utils/sdl_compat.h"
#include "utils/str_cat.hpp"

namespace devilution {
namespace {

class ReforgedUiComfortsTest : public UITest {
protected:
	void SetUp() override
	{
		UITest::SetUp();
		if (IsSkipped())
			return;
		gbIsPoe2 = false;
		gbIsReforged = true;
		// The tooltip is only written when something can draw it, and the hero is looking at their own bag.
		HeadlessMode = false;
		InspectPlayer = MyPlayer;
		StripPlayer();
		CompareTooltips.clear();
		SDL_SetModState(KMOD_NONE);
	}

	void TearDown() override
	{
		SDL_SetModState(KMOD_NONE);
		CompareTooltips.clear();
		FloatingInfoString = StringOrView {};
		InfoString = StringOrView {};
		FloatingInfoStyles.clear();
		gbIsReforged = false;
		gbIsPoe2 = false;
		HeadlessMode = true;
		UITest::TearDown();
	}

	static Item MakeItem(_item_indexes index)
	{
		Item item {};
		InitializeItem(item, index);
		item._iIdentified = true;
		item._iStatFlag = true;
		return item;
	}

	/** @brief The first plain body armour in the item table. */
	static Item MakeBodyArmour()
	{
		for (size_t i = 0; i < AllItemsList.size(); i++) {
			const ItemData &data = AllItemsList[i];
			if (data.iLoc == ILOC_ARMOR && data.iClass == ICLASS_ARMOR && data.iMinStr == 0 && data.iMinAC > 0)
				return MakeItem(static_cast<_item_indexes>(i));
		}
		return {};
	}

	/** @brief The hovered item's tooltip text, as the inventory hover writes it. */
	static std::string Tooltip(const Item &item)
	{
		FloatingInfoString = item.getName();
		InfoString = item.getName();
		FloatingInfoStyles.clear();
		CompareTooltips.clear();
		if (item._iIdentified)
			PrintItemDetails(item);
		else
			PrintItemDur(item);
		return std::string(FloatingInfoString.str());
	}

	/** @brief Lines that only POE2 Mashup's items have. */
	static void ExpectNoPoe2Lines(const std::string &text)
	{
		for (const std::string_view poe2Only : { "Item Level", "Quality", "implicit", "Corrupted", "Attacks per Second",
		         "Handed", "Prefix", "Suffix", "(T", "Armour", "Evasion", "Energy Shield", "Spirit", "Critical",
		         "Socket", "Rare", "Desecrated", "Grants Skill", "Accuracy" }) {
			EXPECT_EQ(text.find(poe2Only), std::string::npos) << "\"" << poe2Only << "\" in:\n"
			                                                  << text;
		}
	}

	/** @brief Puts the item in the bag and points the mouse at it, as a hover would; returns its inventory index. */
	int PlaceAndPointAt(const Item &item)
	{
		Item copy = item;
		EXPECT_TRUE(AutoPlaceItemInInventory(*MyPlayer, copy));
		const int iv = MyPlayer->_pNumInv - 1;
		for (int cell = 0; cell < InventoryGridCells; cell++) {
			if (std::abs(MyPlayer->InvGrid[cell]) == iv + 1) {
				MousePosition = GetRightPanel().position + Displacement { InvRect[SLOTXY_INV_FIRST + cell].position.x + 5, InvRect[SLOTXY_INV_FIRST + cell].position.y + 5 };
				break;
			}
		}
		return iv;
	}
};

// --- 1. The monster banner -------------------------------------------------------------------------------------

class ReforgedBannerTest : public ReforgedUiComfortsTest {
protected:
	void SetUp() override
	{
		ReforgedUiComfortsTest::SetUp();
		if (IsSkipped())
			return;
		LoadMonsterData();
		for (int &kills : MonsterKillCounts)
			kills = 0;
	}

	void TearDown() override
	{
		for (int &kills : MonsterKillCounts)
			kills = 0;
		ReforgedUiComfortsTest::TearDown();
	}

	static Monster MakeMonster(_monster_id type, UniqueMonsterType unique, uint16_t resistance)
	{
		LevelMonsterTypes[0].type = type;
		Monster monster {};
		monster.levelType = 0;
		monster.uniqueType = unique;
		monster.leader = Monster::NoLeader;
		monster.resistance = resistance;
		monster.maxHitPoints = Fixed26_6::fromInt(100);
		monster.hitPoints = monster.maxHitPoints;
		return monster;
	}
};

TEST_F(ReforgedBannerTest, AUniqueShowsItsNameKindTypeAndDiablosWordsForItsResistances)
{
	const Monster butcher = MakeMonster(MT_CLEAVER, UniqueMonsterType::Butcher, RESIST_FIRE);
	const MonsterBannerText text = ReforgedMonsterBannerText(butcher);
	EXPECT_EQ(text.name, butcher.name());
	EXPECT_EQ(text.nameColor, UiFlags::ColorWhitegold);
	ASSERT_EQ(text.lines.size(), 2U);
	EXPECT_NE(text.lines[0].find("Type: Demon"), std::string::npos) << text.lines[0];
	EXPECT_NE(text.lines[0].find(MonstersData[MT_CLEAVER].name), std::string::npos) << text.lines[0];
	// Diablo never names a unique's elements, only that it has some.
	EXPECT_EQ(text.lines[1], "Some Magic Resistances   No Immunities");
	for (const std::string &line : text.lines) {
		EXPECT_EQ(line.find('%'), std::string::npos) << line;
		EXPECT_EQ(line.find("Armour"), std::string::npos) << line;
		EXPECT_EQ(line.find("Level"), std::string::npos) << line;
	}
}

TEST_F(ReforgedBannerTest, AKindsResistancesShowAfterFifteenKillsAsDiabloWordsThem)
{
	const Monster zombie = MakeMonster(MT_NZOMBIE, UniqueMonsterType::None, IMMUNE_MAGIC | RESIST_FIRE);
	MonsterKillCounts[MT_NZOMBIE] = 3;
	MonsterBannerText text = ReforgedMonsterBannerText(zombie);
	EXPECT_EQ(text.nameColor, UiFlags::ColorWhite);
	ASSERT_EQ(text.lines.size(), 1U);
	EXPECT_EQ(text.lines[0], "Type: Undead  Kills: 3");

	MonsterKillCounts[MT_NZOMBIE] = 20;
	text = ReforgedMonsterBannerText(zombie);
	ASSERT_EQ(text.lines.size(), 2U);
	EXPECT_EQ(text.lines[1], "Resists: Fire   Immune: Magic");

	const Monster plain = MakeMonster(MT_NZOMBIE, UniqueMonsterType::None, 0);
	text = ReforgedMonsterBannerText(plain);
	ASSERT_EQ(text.lines.size(), 2U);
	EXPECT_EQ(text.lines[1], "No magic resistance");
}

TEST_F(ReforgedBannerTest, APacksMinionIsBlue)
{
	Monster minion = MakeMonster(MT_NZOMBIE, UniqueMonsterType::None, 0);
	minion.leader = 1;
	EXPECT_EQ(ReforgedMonsterBannerText(minion).nameColor, UiFlags::ColorBlue);
}

// --- 2. The newer item tooltip and Shift to compare -------------------------------------------------------------

TEST_F(ReforgedUiComfortsTest, TooltipsOfDiablosItemsHaveDiablosLinesOnly)
{
	Item sword = MakeItem(IDI_WARRIOR);
	const std::string swordText = Tooltip(sword);
	EXPECT_NE(swordText.find(StrCat("damage: ", sword._iMinDam, "-", sword._iMaxDam, "  Dur: ")), std::string::npos) << swordText;
	ExpectNoPoe2Lines(swordText);

	const Item shield = MakeItem(IDI_WARRSHLD);
	const std::string shieldText = Tooltip(shield);
	EXPECT_NE(shieldText.find(StrCat("armor: ", shield._iAC)), std::string::npos) << shieldText;
	ExpectNoPoe2Lines(shieldText);

	// A magic sword shows Diablo's affix text, without tiers even with Alt held.
	Item magic = MakeItem(IDI_WARRIOR);
	magic._iMagical = ITEM_QUALITY_MAGIC;
	magic._iPrePower = IPL_TOHIT;
	magic._iPLToHit = 10;
	magic._iPreTier = 3;
	SDL_SetModState(KMOD_ALT);
	const std::string magicText = Tooltip(magic);
	EXPECT_NE(magicText.find(std::string(PrintItemPower(IPL_TOHIT, magic).str())), std::string::npos) << magicText;
	ExpectNoPoe2Lines(magicText);
	SDL_SetModState(KMOD_NONE);

	// An unidentified one hides it, as in Diablo.
	magic._iIdentified = false;
	const std::string hiddenText = Tooltip(magic);
	EXPECT_NE(hiddenText.find("Not Identified"), std::string::npos) << hiddenText;
	EXPECT_EQ(hiddenText.find(std::string(PrintItemPower(IPL_TOHIT, magic).str())), std::string::npos) << hiddenText;

	const Item potion = MakeItem(IDI_HEAL);
	ExpectNoPoe2Lines(Tooltip(potion));
}

TEST_F(ReforgedUiComfortsTest, AUniquesPowersAreInItsTooltipInDiablosWords)
{
	ASSERT_FALSE(UniqueItems.empty());
	Item unique = MakeItem(IDI_CLEAVER);
	unique._iMagical = ITEM_QUALITY_UNIQUE;
	unique._iUid = 0;
	ShowUniqueItemInfoBox = false;
	const std::string text = Tooltip(unique);
	int powers = 0;
	for (const auto &power : UniqueItems[0].powers) {
		if (power.type == IPL_INVALID)
			break;
		powers++;
		EXPECT_NE(text.find(std::string(PrintItemPower(power.type, unique).str())), std::string::npos) << text;
	}
	EXPECT_GT(powers, 0);
	EXPECT_EQ(text.find("unique item"), std::string::npos) << text;
	// The powers are in the tooltip, so Diablo's separate unique window stays shut.
	EXPECT_FALSE(ShowUniqueItemInfoBox);
	ExpectNoPoe2Lines(text);
}

TEST_F(ReforgedUiComfortsTest, ShiftComparesWithTheWornItemOfTheSameSlotInDiablosNumbers)
{
	Player &player = *MyPlayer;
	Item worn = MakeItem(IDI_WARRIOR);
	worn._iMinDam = 2;
	worn._iMaxDam = 6;
	player.InvBody[INVLOC_HAND_LEFT] = worn;
	player.InvBody[INVLOC_HAND_RIGHT] = MakeItem(IDI_WARRSHLD);
	Item better = MakeItem(IDI_WARRCLUB);
	better._iMinDam = 5;
	better._iMaxDam = 8;

	// Without Shift, nothing is compared.
	Tooltip(better);
	PrintItemComparison(better);
	EXPECT_TRUE(CompareTooltips.empty());

	SDL_SetModState(KMOD_SHIFT);
	std::string text = Tooltip(better);
	PrintItemComparison(better);
	text = std::string(FloatingInfoString.str());
	ASSERT_EQ(CompareTooltips.size(), 1U);
	// The weapon in hand, not the shield.
	EXPECT_EQ(std::string(CompareTooltips[0].text.str()).rfind(worn.getName(), 0), 0U) << CompareTooltips[0].text.str();
	EXPECT_NE(text.find("+3 to minimum Damage"), std::string::npos) << text;
	EXPECT_NE(text.find("+2 to maximum Damage"), std::string::npos) << text;
	ExpectNoPoe2Lines(text);

	// A shield is compared with the shield.
	Item shield = MakeItem(IDI_WARRSHLD);
	shield._iAC = player.InvBody[INVLOC_HAND_RIGHT]._iAC + 4;
	Tooltip(shield);
	PrintItemComparison(shield);
	text = std::string(FloatingInfoString.str());
	ASSERT_EQ(CompareTooltips.size(), 1U);
	EXPECT_NE(text.find("+4 Armor Class"), std::string::npos) << text;
	EXPECT_EQ(text.find("Damage"), std::string::npos) << text;

	// Without the mod, plain Diablo has no comparison.
	gbIsReforged = false;
	Tooltip(shield);
	PrintItemComparison(shield);
	EXPECT_TRUE(CompareTooltips.empty());
}

// --- 3. Rarity-coloured inventory slots ---------------------------------------------------------------------------

uint8_t SlotPixelAfterTint(item_quality quality)
{
	OwnedSurface surface { 40, 40 };
	constexpr uint8_t Grey = PAL16_GRAY + 5;
	FillRect(surface, 0, 0, 40, 40, Grey);
	InvDrawSlotBack(surface, { 4, 31 }, { 28, 28 }, quality);
	return surface[{ 10, 20 }];
}

TEST_F(ReforgedUiComfortsTest, SlotsAreColouredByDiablosRarities)
{
	EXPECT_EQ(SlotPixelAfterTint(ITEM_QUALITY_NORMAL), PAL16_GRAY + 5);
	EXPECT_EQ(SlotPixelAfterTint(ITEM_QUALITY_MAGIC), PAL16_BLUE + 6);
	EXPECT_EQ(SlotPixelAfterTint(ITEM_QUALITY_UNIQUE), PAL16_YELLOW + 6);

	// POE2 Mashup keeps its orange uniques; plain Diablo keeps its beige normal items.
	gbIsReforged = false;
	gbIsPoe2 = true;
	EXPECT_EQ(SlotPixelAfterTint(ITEM_QUALITY_UNIQUE), PAL16_ORANGE + 6);
	gbIsPoe2 = false;
	EXPECT_EQ(SlotPixelAfterTint(ITEM_QUALITY_NORMAL), PAL16_BEIGE + 6);
}

} // namespace
} // namespace devilution
