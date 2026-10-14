#include "panels/stash_tabs_panel.hpp"

#include <algorithm>
#include <string>

#ifdef USE_SDL3
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#else
#ifdef USE_SDL1
#include "utils/sdl2_to_1_2_backports.h"
#endif
#endif

#include "DiabloUI/text_input.hpp"
#include "control/control.hpp"
#include "controls/wasd_movement.hpp"
#include "cursor.h"
#include "game_mode.hpp"
#include "inv.h"
#include "panels/poe2_widgets.hpp"
#include "player.h"
#include "plrmsg.h"
#include "poe2/stash_tabs.hpp"
#include "qol/stash.h"
#include "utils/display.h"
#include "utils/language.h"
#include "utils/sdl_compat.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

using namespace poe2ui;

enum class Popup : uint8_t {
	None,
	List,
	Settings,
};

Popup OpenPopup = Popup::None;
unsigned SettingsPage = 0;
unsigned ListScroll = 0;
bool EditingName = false;

char NameBuffer[MaxStashTabNameLength + 1];
TextInputCursorState NameCursor;
TextInputState NameInput {
	TextInputState::Options {
	    .value = NameBuffer,
	    .cursor = &NameCursor,
	    .maxLength = MaxStashTabNameLength,
	}
};

constexpr int RowHeight = 18;
constexpr int ListTop = 26;
constexpr int ListRows = 14;
constexpr Size SwatchSize { 26, 18 };
constexpr int SwatchGap = 4;
constexpr Size ButtonSize { 90, 20 };

Rectangle LabelRect()
{
	return { GetPanelPosition(UiPanels::Stash, { 126, 0 }), { 69, 14 } };
}

/** @brief Sorts the open tab. */
Rectangle SortRect()
{
	return { GetPanelPosition(UiPanels::Stash, { 199, 0 }), { 40, 14 } };
}

Rectangle PopupRect()
{
	return { GetPanelPosition(UiPanels::Stash, { 10, 36 }), { 300, 310 } };
}

unsigned ListEntries()
{
	return StashTabs.count() + (StashTabs.count() < MaxStashTabs ? 1 : 0);
}

Rectangle ListRow(unsigned visibleRow)
{
	const Rectangle popup = PopupRect();
	return { popup.position + Displacement { 6, ListTop + (static_cast<int>(visibleRow) * RowHeight) }, { popup.size.width - 12, RowHeight - 2 } };
}

Rectangle NameField()
{
	return { PopupRect().position + Displacement { 70, 28 }, { 216, 18 } };
}

Rectangle SwatchRect(uint8_t color)
{
	const int column = color % StashTabColumns;
	const int row = color / StashTabColumns;
	return { PopupRect().position + Displacement { 70 + (column * (SwatchSize.width + SwatchGap)), 56 + (row * (SwatchSize.height + SwatchGap)) }, SwatchSize };
}

Rectangle AffinityBox(size_t index)
{
	const int column = static_cast<int>(index % 2);
	const int row = static_cast<int>(index / 2);
	return { PopupRect().position + Displacement { 14 + (column * 140), 168 + (row * 24) }, { 130, 20 } };
}

Rectangle ListButton()
{
	const Rectangle popup = PopupRect();
	return { popup.position + Displacement { 14, popup.size.height - ButtonSize.height - 12 }, ButtonSize };
}

Rectangle DoneButton()
{
	const Rectangle popup = PopupRect();
	return { popup.position + Displacement { popup.size.width - ButtonSize.width - 14, popup.size.height - ButtonSize.height - 12 }, ButtonSize };
}

UiFlags TextColorOn(uint8_t color)
{
	return IsLightStashTabColor(color) ? UiFlags::ColorBlack : UiFlags::ColorWhite;
}

/** @brief Cuts the text with ".." so it fits the width. */
std::string FitText(std::string text, int width)
{
	if (GetLineWidth(text) <= width)
		return text;
	while (!text.empty() && GetLineWidth(StrCat(text, "..")) > width)
		text.pop_back();
	return StrCat(text, "..");
}

void StartEditingName()
{
	if (EditingName)
		return;
	EditingName = true;
	NameInput.assign(SettingsPage < StashTabs.count() ? StashTabs.tabs[SettingsPage].name : std::string {});
	SDLC_StartTextInput(ghMainWnd);
}

void StopEditingName()
{
	if (!EditingName)
		return;
	EditingName = false;
	SDLC_StopTextInput(ghMainWnd);
}

void OpenList()
{
	StopEditingName();
	OpenPopup = Popup::List;
	const unsigned page = Stash.GetPage();
	if (page < ListScroll || page >= ListScroll + ListRows)
		ListScroll = page >= ListRows / 2 ? page - (ListRows / 2) : 0;
	ClearWasdMovementKeys();
}

void OpenSettings(unsigned page)
{
	StopEditingName();
	OpenPopup = Popup::Settings;
	SettingsPage = std::min(page, StashTabs.count() - 1);
	ClearWasdMovementKeys();
}

void ScrollList(int rows)
{
	const int maxScroll = std::max(static_cast<int>(ListEntries()) - ListRows, 0);
	ListScroll = static_cast<unsigned>(std::clamp(static_cast<int>(ListScroll) + rows, 0, maxScroll));
}

void DrawButton(const Surface &out, const Rectangle &rect, std::string_view text)
{
	FillRectangle(out, rect, rect.contains(MousePosition) ? RowHoverColor : RowColor);
	DrawFrame(out, rect, BorderColor);
	DrawString(out, text, rect, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhite });
}

void DrawList(const Surface &out)
{
	const Rectangle popup = PopupRect();
	DrawPanel(out, popup);
	DrawString(out, _("Stash tabs"), { popup.position + Displacement { 0, 4 }, { popup.size.width, 18 } },
	    { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhitegold });

	for (unsigned row = 0; row < ListRows; row++) {
		const unsigned entry = ListScroll + row;
		if (entry >= ListEntries())
			break;
		const Rectangle rect = ListRow(row);
		const bool hovered = rect.contains(MousePosition);
		if (entry >= StashTabs.count()) {
			FillRectangle(out, rect, hovered ? RowHoverColor : RowColor);
			DrawString(out, _("+ New tab"), rect, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorGold });
			continue;
		}
		const StashTab &tab = StashTabs.tabs[entry];
		FillRectangle(out, rect, entry == Stash.GetPage() ? RowHoverColor : (hovered ? HoverColor : RowColor));
		const Rectangle swatch { rect.position + Displacement { 4, 2 }, { 14, rect.size.height - 4 } };
		FillRectangle(out, swatch, StashTabPaletteColor(tab.color));
		DrawFrame(out, swatch, InnerBorderColor);
		const UiFlags nameColor = hovered && entry != Stash.GetPage() ? UiFlags::ColorBlack : UiFlags::ColorWhite;
		DrawString(out, FitText(StashTabs.displayName(entry), 120), { rect.position + Displacement { 24, 0 }, { 124, rect.size.height } },
		    { .flags = UiFlags::VerticalCenter | nameColor });
		const std::string affinities = StashTabs.affinityText(entry);
		if (!affinities.empty()) {
			DrawString(out, FitText(affinities, 130), { rect.position + Displacement { 150, 0 }, { rect.size.width - 154, rect.size.height } },
			    { .flags = UiFlags::AlignRight | UiFlags::VerticalCenter | UiFlags::ColorGold });
		}
	}

	DrawString(out, _("Click: open   Right-click: settings"), { popup.position + Displacement { 0, popup.size.height - 22 }, { popup.size.width, 16 } },
	    { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorButtonpushed });
}

void DrawSettings(const Surface &out)
{
	const Rectangle popup = PopupRect();
	DrawPanel(out, popup);
	DrawString(out, StrCat(_("Tab settings: "), StashTabs.displayName(SettingsPage)), { popup.position + Displacement { 0, 4 }, { popup.size.width, 18 } },
	    { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhitegold });

	const StashTab &tab = StashTabs.tabs[SettingsPage];
	const Rectangle field = NameField();
	DrawString(out, _("Name"), { { popup.position.x + 14, field.position.y }, { 56, field.size.height } }, { .flags = UiFlags::VerticalCenter | UiFlags::ColorWhite });
	FillRectangle(out, field, RowColor);
	DrawFrame(out, field, EditingName ? HoverColor : BorderColor);
	const Rectangle fieldText { field.position + Displacement { 6, 0 }, { field.size.width - 12, field.size.height } };
	if (EditingName) {
		DrawString(out, NameInput.value(), fieldText,
		    { .flags = UiFlags::VerticalCenter | UiFlags::ColorWhite, .cursorPosition = static_cast<int>(NameCursor.position) });
	} else {
		const bool unnamed = tab.name.empty();
		DrawString(out, unnamed ? std::string(_("Click to name this tab")) : tab.name, fieldText,
		    { .flags = UiFlags::VerticalCenter | (unnamed ? UiFlags::ColorButtonpushed : UiFlags::ColorWhite) });
	}

	DrawString(out, _("Colour"), { { popup.position.x + 14, SwatchRect(0).position.y }, { 56, SwatchSize.height } }, { .flags = UiFlags::VerticalCenter | UiFlags::ColorWhite });
	for (uint8_t color = 0; color < StashTabColorCount; color++) {
		const Rectangle swatch = SwatchRect(color);
		FillRectangle(out, swatch, StashTabPaletteColor(color));
		if (color == tab.color) {
			DrawFrame(out, swatch, PAL16_GRAY);
			DrawFrame(out, { swatch.position + Displacement { 1, 1 }, { swatch.size.width - 2, swatch.size.height - 2 } }, PAL16_GRAY + 15);
		} else if (swatch.contains(MousePosition)) {
			DrawFrame(out, swatch, HoverColor);
		}
	}

	DrawString(out, _("Affinities"), { popup.position + Displacement { 14, 128 }, { 120, 16 } }, { .flags = UiFlags::VerticalCenter | UiFlags::ColorWhite });
	DrawString(out, _("Ctrl+click sends these items to this tab"), { popup.position + Displacement { 14, 146 }, { popup.size.width - 28, 16 } },
	    { .flags = UiFlags::VerticalCenter | UiFlags::ColorButtonpushed });
	for (size_t i = 0; i < std::size(AllStashAffinities); i++) {
		const StashAffinity affinity = AllStashAffinities[i];
		const Rectangle rect = AffinityBox(i);
		const Rectangle box { rect.position + Displacement { 0, 3 }, { 14, 14 } };
		FillRectangle(out, box, SocketColor);
		DrawFrame(out, box, rect.contains(MousePosition) ? HoverColor : BorderColor);
		if (StashTabs.hasAffinity(SettingsPage, affinity))
			FillRectangle(out, { box.position + Displacement { 3, 3 }, { 8, 8 } }, PAL16_YELLOW + 2);
		DrawString(out, StashAffinityName(affinity), { rect.position + Displacement { 20, 0 }, { rect.size.width - 20, rect.size.height } },
		    { .flags = UiFlags::VerticalCenter | UiFlags::ColorWhite });
	}

	DrawButton(out, ListButton(), _("All tabs"));
	DrawButton(out, DoneButton(), _("Done"));
}

void CommitName()
{
	StashTabs.rename(SettingsPage, NameInput.value());
	Stash.dirty = true;
}

bool HandleListClick(bool rightClick)
{
	for (unsigned row = 0; row < ListRows; row++) {
		const unsigned entry = ListScroll + row;
		if (entry >= ListEntries())
			break;
		if (!ListRow(row).contains(MousePosition))
			continue;
		if (entry >= StashTabs.count()) {
			if (StashTabs.addTab()) {
				Stash.dirty = true;
				ScrollList(1);
			}
			return true;
		}
		if (rightClick) {
			OpenSettings(entry);
		} else {
			Stash.SetPage(entry);
			OpenPopup = Popup::None;
		}
		return true;
	}
	return true;
}

bool HandleSettingsClick()
{
	if (NameField().contains(MousePosition)) {
		StartEditingName();
		return true;
	}
	if (EditingName) {
		CommitName();
		StopEditingName();
	}
	for (uint8_t color = 0; color < StashTabColorCount; color++) {
		if (SwatchRect(color).contains(MousePosition)) {
			StashTabs.setColor(SettingsPage, color);
			Stash.dirty = true;
			return true;
		}
	}
	for (size_t i = 0; i < std::size(AllStashAffinities); i++) {
		if (AffinityBox(i).contains(MousePosition)) {
			const StashAffinity affinity = AllStashAffinities[i];
			StashTabs.setAffinity(SettingsPage, affinity, !StashTabs.hasAffinity(SettingsPage, affinity));
			Stash.dirty = true;
			return true;
		}
	}
	if (ListButton().contains(MousePosition)) {
		OpenList();
		return true;
	}
	if (DoneButton().contains(MousePosition))
		OpenPopup = Popup::None;
	return true;
}

bool HandlePopupEvent(const SDL_Event &event)
{
	if (EditingName && HandleTextInputEvent(event, NameInput)) {
		CommitName();
		return true;
	}

	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
		switch (SDLC_EventKey(event)) {
		case SDLK_ESCAPE:
			if (EditingName)
				StopEditingName();
			else
				CloseStashTabPopups();
			break;
		case SDLK_RETURN:
		case SDLK_KP_ENTER:
			if (EditingName) {
				CommitName();
				StopEditingName();
			}
			break;
		default:
			break;
		}
		return true;
	case SDL_EVENT_KEY_UP:
#ifndef USE_SDL1
	case SDL_EVENT_TEXT_INPUT:
#endif
		return true;
	case SDL_EVENT_MOUSE_BUTTON_DOWN: {
#ifdef USE_SDL1
		if (event.button.button == SDL_BUTTON_WHEELUP || event.button.button == SDL_BUTTON_WHEELDOWN) {
			if (OpenPopup == Popup::List)
				ScrollList(event.button.button == SDL_BUTTON_WHEELUP ? -3 : 3);
			return true;
		}
#endif
		if (event.button.button != SDL_BUTTON_LEFT && event.button.button != SDL_BUTTON_RIGHT)
			return true;
		if (!PopupRect().contains(MousePosition)) {
			if (EditingName)
				CommitName();
			CloseStashTabPopups();
			return true;
		}
		if (OpenPopup == Popup::List)
			return HandleListClick(event.button.button == SDL_BUTTON_RIGHT);
		return HandleSettingsClick();
	}
	case SDL_EVENT_MOUSE_BUTTON_UP:
		return true;
#ifndef USE_SDL1
	case SDL_EVENT_MOUSE_WHEEL:
		if (OpenPopup == Popup::List)
			ScrollList(SDLC_EventWheelIntY(event) > 0 ? -3 : 3);
		return true;
#endif
	default:
		return false;
	}
}

} // namespace

bool IsStashTabPopupOpen()
{
	return OpenPopup != Popup::None;
}

void CloseStashTabPopups()
{
	StopEditingName();
	OpenPopup = Popup::None;
}

void DrawStashTabUi(const Surface &out)
{
	if (StashTabs.count() == 0)
		return;
	const unsigned page = std::min(Stash.GetPage(), StashTabs.count() - 1);
	const StashTab &tab = StashTabs.tabs[page];
	const Rectangle label = LabelRect();
	FillRectangle(out, label, StashTabPaletteColor(tab.color));
	DrawFrame(out, label, label.contains(MousePosition) && OpenPopup == Popup::None ? HoverColor : BorderColor);
	DrawString(out, FitText(StashTabs.displayName(page), label.size.width - 6), label,
	    { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | TextColorOn(tab.color) });
	const Rectangle sort = SortRect();
	const bool sortHovered = sort.contains(MousePosition) && OpenPopup == Popup::None;
	FillRectangle(out, sort, sortHovered ? RowHoverColor : RowColor);
	DrawFrame(out, sort, sortHovered ? HoverColor : BorderColor);
	DrawString(out, _("Sort"), sort, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhite });

	if (OpenPopup == Popup::List)
		DrawList(out);
	else if (OpenPopup == Popup::Settings)
		DrawSettings(out);
}

bool StashTabsHandleEvent(const SDL_Event &event)
{
	if (!gbIsPoe2 || !IsStashOpen) {
		if (OpenPopup != Popup::None)
			CloseStashTabPopups();
		return false;
	}
	if (OpenPopup != Popup::None)
		return HandlePopupEvent(event);
	if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN || IsWithdrawGoldOpen)
		return false;

	const bool rightClick = event.button.button == SDL_BUTTON_RIGHT;
	if (event.button.button != SDL_BUTTON_LEFT && !rightClick)
		return false;
	if (!rightClick && SortRect().contains(MousePosition) && MyPlayer->HoldItem.isEmpty()) {
		if (!SortStashPage(Stash.GetPage()))
			EventPlrMsg(_("No room to sort this tab."), UiFlags::ColorWhite);
		return true;
	}
	if (LabelRect().contains(MousePosition) && MyPlayer->HoldItem.isEmpty()) {
		if (rightClick)
			OpenSettings(Stash.GetPage());
		else
			OpenList();
		return true;
	}

	// Ctrl+right-click moves items between inventory and stash, like Ctrl+left-click.
	if (!rightClick || (SDL_GetModState() & SDL_KMOD_CTRL) == 0 || !MyPlayer->HoldItem.isEmpty())
		return false;
	if (invflag && GetRightPanel().contains(MousePosition) && pcursinvitem != -1) {
		TransferItemToStash(*MyPlayer, pcursinvitem);
		return true;
	}
	if (GetLeftPanel().contains(MousePosition) && pcursstashitem != StashStruct::EmptyCell) {
		TransferItemToInventory(*MyPlayer, pcursstashitem);
		return true;
	}
	return false;
}

} // namespace devilution
