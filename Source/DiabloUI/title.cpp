#include <memory>
#include <optional>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include "DiabloUI/diabloui.h"
#include "DiabloUI/ui_flags.hpp"
#include "DiabloUI/ui_item.h"
#include "controls/input.h"
#include "controls/menu_controls.h"
#include "discord/discord.h"
#include "engine/clx_sprite.hpp"
#include "engine/load_clx.hpp"
#include "engine/load_pcx.hpp"
#include "engine/point.hpp"
#include "poe2/hd.hpp"
#include "utils/algorithm/container.hpp"
#include "utils/language.h"
#include "utils/sdl_compat.h"
#include "utils/sdl_geometry.h"
#include "utils/ui_fwd.h"

namespace devilution {
namespace {

OptionalOwnedClxSpriteList DiabloTitleLogo;

std::vector<std::unique_ptr<UiItemBase>> vecTitleScreen;

void TitleLoad()
{
	// POE2 Mashup: with the HD pack's Diablo logo, Hellfire's title screen is Diablo's (its background, with the HD logo
	// and its fire) in place of hf_logo1, which is a whole picture with Hellfire's logo in it.
	if (!hd::ShowsHdLogo("ui_art/logo"))
		ArtBackgroundWidescreen = LoadOptionalClx("ui_art\\hf_titlew.clx");
	if (ArtBackgroundWidescreen.has_value()) {
		LoadBackgroundArt("ui_art\\hf_logo1", 16);
	} else {
		LoadBackgroundArt("ui_art\\title");
		DiabloTitleLogo = LoadPcxSpriteList("ui_art\\logo", /*numFrames=*/15, /*transparentColor=*/250);
		// POE2 Mashup: the HD pack's sharp logo (hd\\ui_art\\logo\\d0_fNN.png) goes over the UI in its place, and none of
		// the old one (its fire too) shows round it. Without the art, the old one shows as it was.
		if (DiabloTitleLogo.has_value())
			hd::RegisterUiPicture("ui_art\\logo", *DiabloTitleLogo, hd::UiPictureLayer::Over);
	}
}

void TitleFree()
{
	ArtBackground = std::nullopt;
	ArtBackgroundWidescreen = std::nullopt;
	hd::ForgetUiPicture(DiabloTitleLogo);
	DiabloTitleLogo = std::nullopt;

	vecTitleScreen.clear();
}

} // namespace

void UiTitleDialog()
{
	TitleLoad();
	const Point uiPosition = GetUIRectangle().position;
	if (ArtBackgroundWidescreen.has_value()) {
		const SDL_Rect rect = MakeSdlRect(0, uiPosition.y, 0, 0);
		if (ArtBackgroundWidescreen)
			vecTitleScreen.push_back(std::make_unique<UiImageClx>((*ArtBackgroundWidescreen)[0], rect, UiFlags::AlignCenter));
		vecTitleScreen.push_back(std::make_unique<UiImageAnimatedClx>(*ArtBackground, rect, UiFlags::AlignCenter));
	} else {
		UiAddBackground(&vecTitleScreen);

		vecTitleScreen.push_back(std::make_unique<UiImageAnimatedClx>(
		    *DiabloTitleLogo, MakeSdlRect(0, uiPosition.y + 182, 0, 0), UiFlags::AlignCenter));

		const SDL_Rect rect = MakeSdlRect(uiPosition.x, uiPosition.y + 410, 640, 26);
		vecTitleScreen.push_back(std::make_unique<UiArtText>(_("Copyright © 1996-2001 Blizzard Entertainment").data(), rect, UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiSilver));
	}

	bool endMenu = false;
	const Uint32 timeOut = SDL_GetTicks() + 7000;

	SDL_Event event;
	while (!endMenu && SDL_GetTicks() < timeOut) {
		UiRenderItems(vecTitleScreen);
		UiFadeIn();

		discord_manager::UpdateMenu();

		while (PollEvent(&event)) {
			if (c_any_of(GetMenuActions(event), [](MenuAction menuAction) { return menuAction != MenuAction_NONE; })) {
				endMenu = true;
				break;
			}
			switch (event.type) {
			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP:
				endMenu = true;
				break;
			}
			UiHandleEvents(&event);
		}
	}

	TitleFree();
}

} // namespace devilution
