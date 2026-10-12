/**
 * @file admin_menu.cpp
 *
 * POE2 Mashup admin tools: an in-game menu for quickly testing new content.
 */
#include "admin_menu.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include "automap.h"
#include "engine/backbuffer_state.hpp"
#include "engine/random.hpp"
#include "gamemenu.h"
#include "gmenu.h"
#include "inv.h"
#include "levels/gendung.h"
#include "msg.h"
#include "player.h"
#include "plrmsg.h"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/str_cat.hpp"

#ifdef _DEBUG
#include "debug.h"
#endif

namespace devilution {

namespace {

constexpr int MinItemLevel = 1;
constexpr int MaxItemLevel = 60;
constexpr int GoldPerClick = 10000;
/** How many seeds to try before giving up on rolling the wanted quality. */
constexpr int MaxItemRolls = 20000;

void AdminMain(bool bActivate);
void AdminItemsOpen(bool bActivate);
void AdminCharacterOpen(bool bActivate);
void AdminWorldOpen(bool bActivate);
void AdminItemBase(bool bActivate);
void AdminItemQuality(bool bActivate);
void AdminItemLevel(bool bActivate);
void AdminItemSpawn(bool bActivate);
void AdminLevelUp(bool bActivate);
void AdminGold(bool bActivate);
void AdminMaxStats(bool bActivate);
void AdminRestore(bool bActivate);
void AdminGodMode(bool bActivate);
void AdminWarpTarget(bool bActivate);
void AdminWarp(bool bActivate);
void AdminRevealMap(bool bActivate);
void AdminBackToGameMenu(bool bActivate);

TMenuItem AdminMainMenu[] = {
	// clang-format off
	// dwFlags,      pszStr,                 fnMenu
	{ GMENU_ENABLED, N_("Spawn Items"),      &AdminItemsOpen      },
	{ GMENU_ENABLED, N_("Character"),        &AdminCharacterOpen  },
	{ GMENU_ENABLED, N_("World"),            &AdminWorldOpen      },
	{ GMENU_ENABLED, N_("Previous Menu"),    &AdminBackToGameMenu },
	{ GMENU_ENABLED, nullptr,                nullptr              },
	// clang-format on
};

TMenuItem AdminItemsMenu[] = {
	// clang-format off
	// dwFlags,                     pszStr,              fnMenu
	{ GMENU_ENABLED | GMENU_SLIDER, N_("Base"),          &AdminItemBase    },
	{ GMENU_ENABLED | GMENU_SLIDER, nullptr,             &AdminItemQuality },
	{ GMENU_ENABLED | GMENU_SLIDER, nullptr,             &AdminItemLevel   },
	{ GMENU_ENABLED,                nullptr,             &AdminItemSpawn   },
	{ GMENU_ENABLED,                N_("Previous Menu"), &AdminMain        },
	{ GMENU_ENABLED,                nullptr,             nullptr           },
	// clang-format on
};

TMenuItem AdminCharacterMenu[] = {
	// clang-format off
	// dwFlags,      pszStr,                     fnMenu
	{ GMENU_ENABLED, N_("Level Up"),             &AdminLevelUp  },
	{ GMENU_ENABLED, N_("Give 10000 Gold"),      &AdminGold     },
	{ GMENU_ENABLED, N_("Max Attributes"),       &AdminMaxStats },
	{ GMENU_ENABLED, N_("Restore Life/Mana"),    &AdminRestore  },
	{ GMENU_ENABLED, nullptr,                    &AdminGodMode  },
	{ GMENU_ENABLED, N_("Previous Menu"),        &AdminMain     },
	{ GMENU_ENABLED, nullptr,                    nullptr        },
	// clang-format on
};

TMenuItem AdminWorldMenu[] = {
	// clang-format off
	// dwFlags,                     pszStr,              fnMenu
	{ GMENU_ENABLED | GMENU_SLIDER, nullptr,             &AdminWarpTarget },
	{ GMENU_ENABLED,                N_("Warp There"),    &AdminWarp       },
	{ GMENU_ENABLED,                N_("Reveal Map"),    &AdminRevealMap  },
	{ GMENU_ENABLED,                N_("Previous Menu"), &AdminMain       },
	{ GMENU_ENABLED,                nullptr,             nullptr          },
	// clang-format on
};

const std::array<const char *, 4> QualityNames = {
	N_("Normal"),
	N_("Magic"),
	N_("Rare"),
	N_("Unique"),
};
const std::array<item_quality, 4> QualityOrder = {
	ITEM_QUALITY_NORMAL,
	ITEM_QUALITY_MAGIC,
	ITEM_QUALITY_RARE,
	ITEM_QUALITY_UNIQUE,
};

std::vector<_item_indexes> ItemBases;
size_t SelectedBase = 0;
size_t SelectedQuality = 2; // Rare
int SelectedItemLevel = 30;
int WarpTarget = 1;

std::string QualityLabel;
std::string LevelLabel;
std::string SpawnLabel;
std::string GodModeLabel;
std::string WarpLabel;

int MaxDungeonLevel()
{
	return gbIsHellfire ? 24 : 16;
}

void CollectItemBases()
{
	ItemBases.clear();
	std::vector<std::string_view> seenNames;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		const ItemData &data = AllItemsList[i];
		if (!IsItemAvailable(static_cast<int>(i)))
			continue;
		const bool isSupportGem = SupportGemForMiscId(data.iMiscId) != SupportGem::None;
		if (!isSupportGem && IsAnyOf(data.iLoc, ILOC_UNEQUIPABLE, ILOC_BELT, ILOC_INVALID))
			continue;
		if (data.iMiscId == IMISC_UNIQUE)
			continue;
		// Skip copies like the class starting weapons that share a name with a regular base.
		if (std::find(seenNames.begin(), seenNames.end(), data.iName) != seenNames.end())
			continue;
		seenNames.emplace_back(data.iName);
		ItemBases.push_back(static_cast<_item_indexes>(i));
	}
	SelectedBase = std::min(SelectedBase, ItemBases.empty() ? 0 : ItemBases.size() - 1);
}

void UpdateItemsMenu()
{
	TMenuItem &baseItem = AdminItemsMenu[0];
	gmenu_slider_steps(&baseItem, std::max<int>(static_cast<int>(ItemBases.size()) - 1, 2));
	gmenu_slider_set(&baseItem, 0, std::max<int>(static_cast<int>(ItemBases.size()) - 1, 1), static_cast<int>(SelectedBase));

	TMenuItem &qualityItem = AdminItemsMenu[1];
	QualityLabel = std::string(_(QualityNames[SelectedQuality]));
	qualityItem.pszStr = QualityLabel.c_str();
	gmenu_slider_steps(&qualityItem, static_cast<int>(QualityNames.size()) - 1);
	gmenu_slider_set(&qualityItem, 0, static_cast<int>(QualityNames.size()) - 1, static_cast<int>(SelectedQuality));

	TMenuItem &levelItem = AdminItemsMenu[2];
	LevelLabel = StrCat(_("Level"), " ", SelectedItemLevel);
	levelItem.pszStr = LevelLabel.c_str();
	gmenu_slider_steps(&levelItem, MaxItemLevel - MinItemLevel);
	gmenu_slider_set(&levelItem, MinItemLevel, MaxItemLevel, SelectedItemLevel);

	if (ItemBases.empty()) {
		SpawnLabel = _("No items available");
		AdminItemsMenu[3].setEnabled(false);
	} else {
		SpawnLabel = StrCat(_("Spawn"), " ", AllItemsList[ItemBases[SelectedBase]].iName);
		AdminItemsMenu[3].setEnabled(true);
	}
	AdminItemsMenu[3].pszStr = SpawnLabel.c_str();
}

void UpdateCharacterMenu()
{
	const Player &myPlayer = *MyPlayer;
	AdminCharacterMenu[0].setEnabled(!myPlayer.isMaxCharacterLevel());
#ifdef _DEBUG
	GodModeLabel = DebugGodMode ? _("God Mode: On") : _("God Mode: Off");
	AdminCharacterMenu[4].setEnabled(true);
#else
	GodModeLabel = _("God Mode: Debug Only");
	AdminCharacterMenu[4].setEnabled(false);
#endif
	AdminCharacterMenu[4].pszStr = GodModeLabel.c_str();
}

void UpdateWorldMenu()
{
	TMenuItem &targetItem = AdminWorldMenu[0];
	WarpTarget = std::clamp(WarpTarget, 0, MaxDungeonLevel());
	WarpLabel = WarpTarget == 0 ? std::string(_("Town")) : StrCat(_("Level"), " ", WarpTarget);
	targetItem.pszStr = WarpLabel.c_str();
	gmenu_slider_steps(&targetItem, MaxDungeonLevel());
	gmenu_slider_set(&targetItem, 0, MaxDungeonLevel(), WarpTarget);
}

void AdminMain(bool /*bActivate*/)
{
	gmenu_set_items(AdminMainMenu, nullptr);
}

void AdminBackToGameMenu(bool /*bActivate*/)
{
	gamemenu_on();
}

void AdminItemsOpen(bool /*bActivate*/)
{
	CollectItemBases();
	UpdateItemsMenu();
	gmenu_set_items(AdminItemsMenu, nullptr);
}

void AdminCharacterOpen(bool /*bActivate*/)
{
	UpdateCharacterMenu();
	gmenu_set_items(AdminCharacterMenu, nullptr);
}

void AdminWorldOpen(bool /*bActivate*/)
{
	UpdateWorldMenu();
	gmenu_set_items(AdminWorldMenu, nullptr);
}

void AdminItemBase(bool bActivate)
{
	if (ItemBases.empty())
		return;
	const int last = static_cast<int>(ItemBases.size()) - 1;
	if (bActivate)
		SelectedBase = (SelectedBase + 1) % ItemBases.size();
	else
		SelectedBase = static_cast<size_t>(std::clamp(gmenu_slider_get(&AdminItemsMenu[0], 0, last), 0, last));
	UpdateItemsMenu();
}

void AdminItemQuality(bool bActivate)
{
	const int last = static_cast<int>(QualityNames.size()) - 1;
	if (bActivate)
		SelectedQuality = (SelectedQuality + 1) % QualityNames.size();
	else
		SelectedQuality = static_cast<size_t>(std::clamp(gmenu_slider_get(&AdminItemsMenu[1], 0, last), 0, last));
	UpdateItemsMenu();
}

void AdminItemLevel(bool bActivate)
{
	if (bActivate)
		SelectedItemLevel = SelectedItemLevel >= MaxItemLevel ? MinItemLevel : SelectedItemLevel + 10;
	else
		SelectedItemLevel = gmenu_slider_get(&AdminItemsMenu[2], MinItemLevel, MaxItemLevel);
	SelectedItemLevel = std::clamp(SelectedItemLevel, MinItemLevel, MaxItemLevel);
	UpdateItemsMenu();
}

void AdminItemSpawn(bool /*bActivate*/)
{
	if (ItemBases.empty())
		return;

	if (ActiveItemCount >= MAXITEMS) {
		EventPlrMsg(_("Too many items on the ground. Pick some up first."), UiFlags::ColorRed);
		return;
	}

	const _item_indexes baseIndex = ItemBases[SelectedBase];
	const item_quality quality = QualityOrder[SelectedQuality];
	Item item;
	if (!AdminGenerateItem(*MyPlayer, item, baseIndex, quality, SelectedItemLevel, SDL_GetTicks())) {
		EventPlrMsg(StrCat(_("Couldn't roll that item. Try another level or quality.")), UiFlags::ColorRed);
		return;
	}

	const int ii = AllocateItem();
	Items[ii] = std::move(item);
	Items[ii]._iIdentified = true;
	GetSuperItemSpace(MyPlayer->position.tile, ii);
	NetSendCmdPItem(false, CMD_SPAWNITEM, Items[ii].position, Items[ii]);
	EventPlrMsg(StrCat(_("Spawned"), " ", Items[ii].getName()));
}

void AdminLevelUp(bool /*bActivate*/)
{
	Player &myPlayer = *MyPlayer;
	if (myPlayer.isMaxCharacterLevel())
		return;
	myPlayer._pExperience = myPlayer.getNextExperienceThreshold();
	NextPlrLevel(myPlayer);
	RedrawEverything();
	EventPlrMsg(StrCat(_("Character level"), " ", myPlayer.getCharacterLevel()));
	UpdateCharacterMenu();
}

void AdminGold(bool /*bActivate*/)
{
	const int added = AdminGiveGold(*MyPlayer, GoldPerClick);
	if (added == 0)
		EventPlrMsg(_("No room in the inventory for more gold."), UiFlags::ColorRed);
	else
		EventPlrMsg(StrCat(_("Added gold:"), " ", added));
}

void AdminMaxStats(bool /*bActivate*/)
{
	Player &myPlayer = *MyPlayer;
	ModifyPlrStr(myPlayer, myPlayer.GetMaximumAttributeValue(CharacterAttribute::Strength) - myPlayer._pBaseStr);
	ModifyPlrMag(myPlayer, myPlayer.GetMaximumAttributeValue(CharacterAttribute::Magic) - myPlayer._pBaseMag);
	ModifyPlrDex(myPlayer, myPlayer.GetMaximumAttributeValue(CharacterAttribute::Dexterity) - myPlayer._pBaseDex);
	ModifyPlrVit(myPlayer, myPlayer.GetMaximumAttributeValue(CharacterAttribute::Vitality) - myPlayer._pBaseVit);
	EventPlrMsg(_("Attributes set to the class maximum."));
}

void AdminRestore(bool /*bActivate*/)
{
	Player &myPlayer = *MyPlayer;
	myPlayer.RestoreFullLife();
	myPlayer.RestoreFullMana();
	RedrawComponent(PanelDrawComponent::Health);
	RedrawComponent(PanelDrawComponent::Mana);
	EventPlrMsg(_("Life and mana restored."));
}

void AdminGodMode(bool /*bActivate*/)
{
#ifdef _DEBUG
	DebugGodMode = !DebugGodMode;
#endif
	UpdateCharacterMenu();
}

void AdminWarpTarget(bool bActivate)
{
	if (bActivate)
		WarpTarget = (WarpTarget + 1) % (MaxDungeonLevel() + 1);
	else
		WarpTarget = gmenu_slider_get(&AdminWorldMenu[0], 0, MaxDungeonLevel());
	UpdateWorldMenu();
}

void AdminWarp(bool /*bActivate*/)
{
	Player &myPlayer = *MyPlayer;
	if (!setlevel && myPlayer.isOnLevel(WarpTarget)) {
		EventPlrMsg(_("You are already there."));
		return;
	}
	gamemenu_off();
	// Level 21 is the Hellfire crypt, which is entered from town.
	StartNewLvl(myPlayer, WarpTarget != 21 ? WM_DIABNEXTLVL : WM_DIABTOWNWARP, WarpTarget);
}

void AdminRevealMap(bool /*bActivate*/)
{
	for (int x = 0; x < DMAXX; x++) {
		for (int y = 0; y < DMAXY; y++) {
			UpdateAutomapExplorer({ x, y }, MAP_EXP_SHRINE);
		}
	}
	EventPlrMsg(_("Map revealed. Press Tab to view it."));
}

} // namespace

bool AdminGenerateItem(const Player &player, Item &item, _item_indexes baseIndex, item_quality quality, int level, uint32_t firstSeed)
{
	if (SupportGemForMiscId(AllItemsList[baseIndex].iMiscId) != SupportGem::None) {
		// Gems have no quality or level.
		InitializeItem(item, baseIndex);
		item._iSeed = static_cast<int32_t>(firstSeed >> 1);
		SetupItem(item);
		return true;
	}

	// Rolling items reseeds the game's random number generator, so put it back afterwards.
	const uint32_t rngState = GetLCGEngineState();

	const bool onlyGood = quality != ITEM_QUALITY_NORMAL;
	const int uper = quality == ITEM_QUALITY_UNIQUE ? 15 : 1;
	bool found = false;
	uint32_t seed = firstSeed;
	for (int i = 0; i < MaxItemRolls && !found; i++) {
		// A simple LCG step gives a fresh seed each try without touching the game's generator.
		seed = seed * 1103515245U + 12345U;
		Item candidate {};
		SetupAllItems(player, candidate, baseIndex, seed >> 1, level, uper, onlyGood, false, 0, quality != ITEM_QUALITY_UNIQUE);
		if (candidate.isEmpty() || candidate._iMagical != quality)
			continue;
		SetupItem(candidate);
		item = std::move(candidate);
		found = true;
	}

	SetRndSeed(rngState);
	return found;
}

int AdminGiveGold(Player &player, int amount)
{
	if (amount <= 0)
		return 0;

	int goldToAdd = amount;
	const int goldAmountBefore = player._pGold;
	for (int8_t &itemIndex : player.InvGrid) {
		if (itemIndex < 0)
			continue;

		Item &item = player.InvList[itemIndex != 0 ? itemIndex - 1 : player._pNumInv];

		if (itemIndex != 0) {
			if ((!item.isGold() && !item.isEmpty()) || (item.isGold() && item._ivalue == GOLD_MAX_LIMIT))
				continue;
		} else {
			if (item.isEmpty()) {
				MakeGoldStack(item, 0);
				player._pNumInv++;
				itemIndex = player._pNumInv;
			}
		}

		const int goldThatCanBeAdded = GOLD_MAX_LIMIT - item._ivalue;
		if (goldThatCanBeAdded >= goldToAdd) {
			item._ivalue += goldToAdd;
			player._pGold += goldToAdd;
			break;
		}

		item._ivalue += goldThatCanBeAdded;
		goldToAdd -= goldThatCanBeAdded;
		player._pGold += goldThatCanBeAdded;
	}

	CalcPlrInv(player, true);
	return player._pGold - goldAmountBefore;
}

void AdminMenuOpen(bool bActivate)
{
	AdminMain(bActivate);
}

} // namespace devilution
