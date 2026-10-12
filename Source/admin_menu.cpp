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
#include "panels/item_spawner.hpp"
#include "player.h"
#include "plrmsg.h"
#include "poe2/currency.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/support_gems.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/str_cat.hpp"

#ifdef _DEBUG
#include "debug.h"
#endif

namespace devilution {

namespace {

constexpr int GoldPerClick = 10000;

void AdminMain(bool bActivate);
void AdminItemsOpen(bool bActivate);
void AdminCharacterOpen(bool bActivate);
void AdminWorldOpen(bool bActivate);
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

int WarpTarget = 1;

std::string GodModeLabel;
std::string WarpLabel;

int MaxDungeonLevel()
{
	return gbIsHellfire ? 24 : 16;
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
	gamemenu_off();
	OpenItemSpawner();
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
	const int miscId = AllItemsList[baseIndex].iMiscId;
	if (SupportGemForMiscId(miscId) != SupportGem::None || CurrencyOrbForMiscId(miscId) != CurrencyOrb::None || !SkillForGemData(AllItemsList[baseIndex]).isEmpty()) {
		// Gems and orbs have no quality or level.
		InitializeItem(item, baseIndex);
		item._iSeed = static_cast<int32_t>(firstSeed >> 1);
		SetupItem(item);
		return true;
	}

	return RollItemOfQuality(player, item, baseIndex, quality, level, firstSeed);
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
