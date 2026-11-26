#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_surface.h>
#else
#include <SDL.h>
#endif

#include <function_ref.hpp>

#include "DiabloUI/ui_item.h"
#include "engine/clx_sprite.hpp"
#include "engine/load_pcx.hpp" // IWYU pragma: export
#include "player.h"
#include "utils/display.h"

namespace devilution {

extern std::size_t SelectedItem;

bool IsTextInputActive();

enum _artFocus : uint8_t {
	FOCUS_SMALL,
	FOCUS_MED,
	FOCUS_BIG,
};

enum _mainmenu_selections : uint8_t {
	MAINMENU_NONE,
	MAINMENU_SINGLE_PLAYER,
	MAINMENU_MULTIPLAYER,
	MAINMENU_SHOW_SUPPORT,
	MAINMENU_SETTINGS,
	MAINMENU_SHOW_CREDITS,
	MAINMENU_EXIT_DIABLO,
	MAINMENU_ATTRACT_MODE,
	/** POE2 Mashup: the Skill Lab (poe2/skill_lab.hpp). */
	MAINMENU_SKILL_LAB,
};

enum _selhero_selections : uint8_t {
	SELHERO_NEW_DUNGEON,
	SELHERO_CONTINUE,
	SELHERO_CONNECT,
	SELHERO_PREVIOUS,
};

struct _uidefaultstats {
	uint16_t strength;
	uint16_t magic;
	uint16_t dexterity;
	uint16_t vitality;
};

struct _uiheroinfo {
	uint32_t saveNumber;
	char name[16];
	uint8_t level;
	HeroClass heroclass;
	uint8_t herorank;
	uint16_t strength;
	uint16_t magic;
	uint16_t dexterity;
	uint16_t vitality;
	bool hassaved;
	bool spawned;
	/** POE2 Mashup: a Hardcore league hero. */
	bool hardcore;
};

extern OptionalOwnedClxSpriteList ArtLogo;
extern OptionalOwnedClxSpriteList DifficultyIndicator;
extern std::array<OptionalOwnedClxSpriteList, 3> ArtFocus;
extern OptionalOwnedClxSpriteList ArtBackgroundWidescreen;
extern OptionalOwnedClxSpriteList ArtBackground;
extern OptionalOwnedClxSpriteList ArtCursor;

extern bool (*gfnHeroInfo)(bool (*fninfofunc)(_uiheroinfo *));

inline SDL_Surface *DiabloUiSurface()
{
	return PalSurface;
}

void UiDestroy();
void UiTitleDialog();
void UnloadUiGFX();
void UiInitialize();
bool UiValidPlayerName(std::string_view name); /* check */
void UiSelHeroMultDialog(bool (*fninfo)(bool (*fninfofunc)(_uiheroinfo *)), bool (*fncreate)(_uiheroinfo *), bool (*fnremove)(_uiheroinfo *), void (*fnstats)(HeroClass, _uidefaultstats *), _selhero_selections *dlgresult, uint32_t *saveNumber);
void UiSelHeroSingDialog(bool (*fninfo)(bool (*fninfofunc)(_uiheroinfo *)), bool (*fncreate)(_uiheroinfo *), bool (*fnremove)(_uiheroinfo *), void (*fnstats)(HeroClass, _uidefaultstats *), _selhero_selections *dlgresult, uint32_t *saveNumber, _difficulty *difficulty);
bool UiCreditsDialog();
bool UiSupportDialog();
bool UiMainMenuDialog(const char *name, _mainmenu_selections *pdwResult, int attractTimeOut);
bool UiProgressDialog(int (*fnfunc)());
bool UiSelectGame(GameData *gameData, int *playerId);
bool UiSelectProvider(GameData *gameData);
void UiFadeIn();
void UiHandleEvents(SDL_Event *event);
bool UiItemMouseEvents(SDL_Event *event, const std::vector<UiItemBase *> &items);
bool UiItemMouseEvents(SDL_Event *event, const std::vector<std::unique_ptr<UiItemBase>> &items);
Sint16 GetCenterOffset(Sint16 w, Sint16 bw = 0);
void DrawMouse();
void UiLoadDefaultPalette();
bool UiLoadBlackBackground();
void LoadBackgroundArt(const char *pszFile, int frames = 1);
/**
 * @brief The main menu's background (ui_art\mainmenu and its wide sides; ui_art\swmmenu for `spawn`), or the HD
 * pack's own in its place when it has it (hd::HasMenuBackground).
 */
void LoadMainMenuBackground(bool spawn = false);
void UiAddBackground(std::vector<std::unique_ptr<UiItemBase>> *vecDialog);
void UiAddLogo(std::vector<std::unique_ptr<UiItemBase>> *vecDialog, int y = GetUIRectangle().position.y);
/**
 * @brief POE2 Mashup: how far a screen with the menu logo moves its title and contents down, clear of the intro's
 * subtitle and fire lines under the logo (hd::MenuSubtitleShown): 0 without them, and never more than the screen has
 * under its 640 x 480 (so nothing runs off the bottom). Each item of a screen rendered (or clicked) while the logo shows
 * is moved by it once, its background too (the screens' frames are drawn in it); the logo and dialogs over a screen stay
 * put (UiItemBase::KeepPlace).
 */
int UiSubtitleDrop();
/** @brief POE2 Mashup: moves the items not yet moved down by UiSubtitleDrop. */
void UiMoveUnderSubtitle(const std::vector<UiItemBase *> &items);
void UiMoveUnderSubtitle(const std::vector<std::unique_ptr<UiItemBase>> &items);
/** @brief POE2 Mashup: these items stay where they are (the main menu's, which the subtitle was made to fit). */
void UiKeepPlaces(const std::vector<std::unique_ptr<UiItemBase>> &items);
void UiFocusNavigationSelect();
void UiFocusNavigationEsc();
void UiFocusNavigationYesNo();

void UiInitList(void (*fnFocus)(size_t value), void (*fnSelect)(size_t value), void (*fnEsc)(), const std::vector<std::unique_ptr<UiItemBase>> &items, bool wraps = false, void (*fnFullscreen)() = nullptr, bool (*fnYesNo)() = nullptr, size_t selectedItem = 0);
void UiRenderListItems();
void UiInitList_clear();

void UiClearScreen();
void UiPollAndRender(std::optional<tl::function_ref<bool(SDL_Event &)>> eventHandler = std::nullopt);
void UiRenderItem(const UiItemBase &item);
void UiRenderItems(const std::vector<UiItemBase *> &items);
void UiRenderItems(const std::vector<std::unique_ptr<UiItemBase>> &items);
ClxSprite UiGetHeroDialogSprite(size_t heroClassIndex);

void mainmenu_restart_repintro();
} // namespace devilution
