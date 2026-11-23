#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include "DiabloUI/diabloui.h"
#include "DiabloUI/ui_flags.hpp"
#include "DiabloUI/ui_item.h"
#include "engine/assets.hpp"
#include "engine/load_clx.hpp"
#include "engine/point.hpp"
#include "engine/sound.h"
#include "game_mode.hpp"
#include "poe2/hd.hpp"
#include "utils/language.h"
#include "utils/ui_fwd.h"

namespace devilution {
namespace {
int mainmenu_attract_time_out; // seconds
uint32_t dwAttractTicks;

std::vector<std::unique_ptr<UiItemBase>> vecMainMenuDialog;
std::vector<std::unique_ptr<UiListItem>> vecMenuItems;

_mainmenu_selections MainMenuResult;

/** @brief POE2 Mashup: the main menu's intro was playing at the last frame. */
bool IntroWasPlaying = false;

/** @brief POE2 Mashup: the intro has ended (or been skipped): the menu's music starts, and its wait for the attract mode. */
void IntroEnded()
{
	IntroWasPlaying = false;
	music_start(TMUSIC_INTRO);
	mainmenu_restart_repintro();
}

#if !defined(USE_SDL1) && !defined(USE_SDL3)
/**
 * @brief POE2 Mashup: until the menu shows, the intro takes the input: a click, Esc, Enter or Space (or a pad's
 * button) skips it to its end, and nothing else reaches the menu.
 */
bool IntroEvent(SDL_Event &event)
{
	if (!hd::MenuIntroTakesInput())
		return false;
	switch (event.type) {
	case SDL_KEYDOWN:
		switch (event.key.keysym.sym) {
		case SDLK_ESCAPE:
		case SDLK_RETURN:
		case SDLK_KP_ENTER:
		case SDLK_SPACE:
			hd::SkipMenuIntro();
			break;
		default:
			break;
		}
		return true;
	case SDL_MOUSEBUTTONDOWN:
	case SDL_CONTROLLERBUTTONDOWN:
	case SDL_FINGERDOWN:
		hd::SkipMenuIntro();
		return true;
	case SDL_KEYUP:
	case SDL_TEXTINPUT:
	case SDL_MOUSEBUTTONUP:
	case SDL_MOUSEMOTION:
	case SDL_MOUSEWHEEL:
	case SDL_CONTROLLERBUTTONUP:
	case SDL_CONTROLLERAXISMOTION:
	case SDL_JOYAXISMOTION:
	case SDL_JOYBUTTONDOWN:
	case SDL_JOYBUTTONUP:
	case SDL_JOYHATMOTION:
	case SDL_FINGERUP:
	case SDL_FINGERMOTION:
		return true;
	default:
		return false;
	}
}
#endif

void UiMainMenuSelect(size_t value)
{
	MainMenuResult = (_mainmenu_selections)vecMenuItems[value]->m_value;
}

#ifndef NOEXIT
void MainmenuEsc()
{
	const std::size_t last = vecMenuItems.size() - 1;
	if (SelectedItem == last) {
		UiMainMenuSelect(last);
	} else {
		SelectedItem = last;
	}
}
#endif

void MainmenuLoad(const char *name)
{
	vecMenuItems.push_back(std::make_unique<UiListItem>(_("Single Player"), MAINMENU_SINGLE_PLAYER));
	// POE2 Mashup: the Skill Lab takes Support's place, so the list still fits the screen.
	if (gbIsPoe2)
		vecMenuItems.push_back(std::make_unique<UiListItem>(_("Skill Lab"), MAINMENU_SKILL_LAB));
	vecMenuItems.push_back(std::make_unique<UiListItem>(_("Multi Player"), MAINMENU_MULTIPLAYER));
	vecMenuItems.push_back(std::make_unique<UiListItem>(_("Settings"), MAINMENU_SETTINGS));
	if (!gbIsPoe2)
		vecMenuItems.push_back(std::make_unique<UiListItem>(_("Support"), MAINMENU_SHOW_SUPPORT));
	vecMenuItems.push_back(std::make_unique<UiListItem>(_("Show Credits"), MAINMENU_SHOW_CREDITS));
#ifndef NOEXIT
	vecMenuItems.push_back(std::make_unique<UiListItem>(gbIsHellfire ? _("Exit Hellfire") : _("Exit Diablo"), MAINMENU_EXIT_DIABLO));
#endif

	if (!gbIsSpawn || gbIsHellfire) {
		ArtBackgroundWidescreen = LoadOptionalClx("ui_art\\mainmenuw.clx");
		LoadBackgroundArt("ui_art\\mainmenu");
	} else {
		LoadBackgroundArt("ui_art\\swmmenu");
	}

	UiAddBackground(&vecMainMenuDialog);
	UiAddLogo(&vecMainMenuDialog);

	const Point uiPosition = GetUIRectangle().position;

	if (gbIsSpawn && gbIsHellfire) {
		const SDL_Rect rect1 = { (Sint16)(uiPosition.x), (Sint16)(uiPosition.y + 145), 640, 30 };
		vecMainMenuDialog.push_back(std::make_unique<UiArtText>(_("Shareware").data(), rect1, UiFlags::FontSize30 | UiFlags::ColorUiSilver | UiFlags::AlignCenter, 8));
	}

	vecMainMenuDialog.push_back(std::make_unique<UiList>(vecMenuItems, vecMenuItems.size(), uiPosition.x + 64, (uiPosition.y + 192), 510, 43, UiFlags::FontSize42 | UiFlags::ColorUiGold | UiFlags::AlignCenter, 5));

	const SDL_Rect rect2 = { 17, (Sint16)(gnScreenHeight - 36), 605, 21 };
	vecMainMenuDialog.push_back(std::make_unique<UiArtText>(name, rect2, UiFlags::FontSize12 | UiFlags::ColorUiSilverDark));

#ifndef NOEXIT
	UiInitList(nullptr, UiMainMenuSelect, MainmenuEsc, vecMainMenuDialog, true);
#else
	UiInitList(nullptr, UiMainMenuSelect, nullptr, vecMainMenuDialog, true);
#endif
}

void MainmenuFree()
{
	// POE2 Mashup: leaving the main menu ends its intro.
	if (hd::MenuIntroPlaying()) {
		hd::FinishMenuIntro();
		IntroEnded();
	}
	ArtBackgroundWidescreen = std::nullopt;
	ArtBackground = std::nullopt;

	vecMainMenuDialog.clear();

	vecMenuItems.clear();
}

} // namespace

void mainmenu_restart_repintro()
{
	dwAttractTicks = SDL_GetTicks() + mainmenu_attract_time_out * 1000;
}

bool UiMainMenuDialog(const char *name, _mainmenu_selections *pdwResult, int attractTimeOut)
{
	MainMenuResult = MAINMENU_NONE;
	while (MainMenuResult == MAINMENU_NONE) {
		mainmenu_attract_time_out = attractTimeOut;
		MainmenuLoad(name);

		mainmenu_restart_repintro(); // for automatic starts

		// POE2 Mashup: the first time the main menu shows, its intro plays first.
		if (hd::MenuIntroWanted()) {
			hd::StartMenuIntro();
			IntroWasPlaying = hd::MenuIntroPlaying();
		}

		while (MainMenuResult == MAINMENU_NONE) {
			UiClearScreen();
#if !defined(USE_SDL1) && !defined(USE_SDL3)
			UiPollAndRender(IntroEvent);
#else
			UiPollAndRender();
#endif
			if (IntroWasPlaying && !hd::MenuIntroPlaying())
				IntroEnded();
			if (SDL_GetTicks() >= dwAttractTicks && (HaveIntro() || gbIsHellfire)) {
				MainMenuResult = MAINMENU_ATTRACT_MODE;
			}
		}

		MainmenuFree();
	}

	*pdwResult = MainMenuResult;
	return true;
}

} // namespace devilution
