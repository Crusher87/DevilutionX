#include "panels/stash_tabs_panel.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_timer.h>
#else
#ifdef USE_SDL1
#include "utils/sdl2_to_1_2_backports.h"
#endif
#endif

#include "DiabloUI/text_input.hpp"
#include "control/control.hpp"
#include "controls/wasd_movement.hpp"
#include "cursor.h"
#include "diablo.h"
#include "effects.h"
#include "engine/clx_sprite.hpp"
#include "engine/light_tables.hpp"
#include "engine/render/clx_render.hpp"
#include "engine/trn.hpp"
#include "lighting.h"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "inv.h"
#include "items.h"
#include "panels/poe2_widgets.hpp"
#include "panels/quest_log.hpp"
#include "panels/stack_split_panel.hpp"
#include "player.h"
#include "plrmsg.h"
#include "poe2/crafting_items.hpp"
#include "poe2/item_search.hpp"
#include "poe2/stacking.hpp"
#include "poe2/stash_layouts.hpp"
#include "poe2/stash_tabs.hpp"
#include "poe2/waystones.hpp"
#include "qol/stash.h"
#include "utils/display.h"
#include "utils/language.h"
#include "utils/sdl_compat.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

using namespace poe2ui;

// ---------------------------------------------------------------------------
// The drawer with the list of tabs, and the popup with a tab's settings
// ---------------------------------------------------------------------------

enum class Popup : uint8_t {
	None,
	Settings,
};

Popup OpenPopup = Popup::None;
/**
 * The list of tabs is a drawer beside the stash (Curtis 2026-10-16): it slides out to the right and stays open while
 * tabs are clicked, so the stash can be used with it open.
 */
bool DrawerOpen = false;
uint32_t DrawerOpenedAt = 0;
constexpr uint32_t DrawerSlideMs = 140;
constexpr int DrawerWidth = 230;
unsigned SettingsPage = 0;
unsigned ListScroll = 0;
bool EditingName = false;

/** A tab being dragged to another place in the order, from the strip or the drawer (Curtis 2026-10-17). */
struct TabDrag {
	unsigned page;
	Point start;
	/** A drag from the drawer: the game never saw its mouse down, so it doesn't see the mouse up either. */
	bool fromDrawer = false;
	bool moving = false;
};
std::optional<TabDrag> Drag;
/** The mouse moves this far with the button down before a click on a tab becomes a drag. */
constexpr int DragThreshold = 5;
uint32_t DragScrolledAt = 0;
constexpr uint32_t DragScrollMs = 250;

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
constexpr int ListRows = 15;
constexpr Size SwatchSize { 26, 18 };
constexpr int SwatchGap = 4;
constexpr Size ButtonSize { 90, 20 };
/** The kinds a tab can be made, in the settings popup. */
constexpr StashTabKind KindChoices[] = { StashTabKind::Blank, StashTabKind::Currency, StashTabKind::Gems, StashTabKind::Maps, StashTabKind::Uniques };

/** @brief A rectangle at a position in the stash panel. */
Rectangle At(int x, int y, int width, int height)
{
	return { GetPanelPosition(UiPanels::Stash, { x, y }), { width, height } };
}

/** The search box, in a strip under the grid (Curtis 2026-10-19). */
constexpr int SearchStripHeight = 26;
Rectangle StashSearchRect()
{
	return At(8, 354, 304, 20);
}

Rectangle PopupRect()
{
	return At(10, 8, 300, 338);
}

/** @brief The kind a new tab gets: blank, else a premade kind the stash has none of; none once it has them all. */
std::optional<StashTabKind> NewTabKind()
{
	if (StashTabs.count() >= MaxStashTabs)
		return std::nullopt;
	if (StashTabs.canHaveAnother(StashTabKind::Blank))
		return StashTabKind::Blank;
	for (const StashTabKind kind : PremadeStashTabKinds) {
		if (StashTabs.canHaveAnother(kind))
			return kind;
	}
	return std::nullopt;
}

unsigned ListEntries()
{
	return StashTabs.count() + (NewTabKind() ? 1 : 0);
}

/** @brief Why the tab can't become the kind, or empty when it can. */
std::string KindRefusal(unsigned page, StashTabKind kind)
{
	if (StashTabs.kind(page) == kind || StashTabs.canHaveAnother(kind))
		return {};
	if (kind == StashTabKind::Blank)
		return std::string(_("A stash has two blank tabs at most."));
	return StrCat(_("You already have a "), StashTabKindName(kind), _(" tab."));
}

/** @brief The drawer, against the stash panel's right edge, kept on the screen. */
Rectangle DrawerRect()
{
	Rectangle rect = At(320, 8, DrawerWidth, 338);
	rect.position.x = std::min(rect.position.x, static_cast<int>(gnScreenWidth) - DrawerWidth);
	return rect;
}

Rectangle ListRow(unsigned visibleRow)
{
	const Rectangle popup = DrawerRect();
	return { popup.position + Displacement { 6, ListTop + (static_cast<int>(visibleRow) * RowHeight) }, { popup.size.width - 12, RowHeight - 2 } };
}

/** @brief Where in the drawer's list a dragged tab would go: before this page, or the tab count for the end. */
unsigned DrawerInsertionAt(Point position)
{
	const int offset = std::max(position.y - ListRow(0).position.y + (RowHeight / 2), 0);
	const unsigned row = std::min(static_cast<unsigned>(offset / RowHeight), static_cast<unsigned>(ListRows));
	return std::min(ListScroll + row, StashTabs.count());
}

Rectangle NameField()
{
	return { PopupRect().position + Displacement { 70, 28 }, { 216, 18 } };
}

Rectangle SwatchRect(uint8_t color)
{
	const int column = color % StashTabColumns;
	const int row = color / StashTabColumns;
	return { PopupRect().position + Displacement { 84 + (column * (SwatchSize.width + SwatchGap)), 56 + (row * (SwatchSize.height + SwatchGap)) }, SwatchSize };
}

Rectangle KindRect(size_t index)
{
	// Two rows of three, under the "Kind" label.
	const int column = static_cast<int>(index % 3);
	const int row = static_cast<int>(index / 3);
	return { PopupRect().position + Displacement { 14 + (column * 92), 146 + (row * 20) }, { 88, 18 } };
}

Rectangle AffinityBox(size_t index)
{
	const int column = static_cast<int>(index % 2);
	const int row = static_cast<int>(index / 2);
	return { PopupRect().position + Displacement { 14 + (column * 140), 240 + (row * 22) }, { 130, 20 } };
}

Rectangle ListButton()
{
	const Rectangle popup = PopupRect();
	return { popup.position + Displacement { 14, popup.size.height - ButtonSize.height - 10 }, ButtonSize };
}

Rectangle DoneButton()
{
	const Rectangle popup = PopupRect();
	return { popup.position + Displacement { popup.size.width - ButtonSize.width - 14, popup.size.height - ButtonSize.height - 10 }, ButtonSize };
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

void ToggleDrawer()
{
	DrawerOpen = !DrawerOpen;
	if (!DrawerOpen)
		return;
	DrawerOpenedAt = SDL_GetTicks();
	const unsigned page = Stash.GetPage();
	if (page < ListScroll || page >= ListScroll + ListRows)
		ListScroll = page >= ListRows / 2 ? page - (ListRows / 2) : 0;
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

void DrawButton(const Surface &out, const Rectangle &rect, std::string_view text, bool selected = false, bool enabled = true)
{
	const bool hovered = enabled && rect.contains(MousePosition) && OpenPopup == Popup::None;
	FillRectangle(out, rect, selected ? RowHoverColor : (hovered ? RowHoverColor : RowColor));
	DrawFrame(out, rect, selected ? HoverColor : (hovered ? HoverColor : BorderColor));
	UiFlags color = UiFlags::ColorWhite;
	if (!enabled)
		color = UiFlags::ColorButtonpushed;
	else if (selected)
		color = UiFlags::ColorWhitegold;
	DrawString(out, FitText(std::string(text), rect.size.width - 4), rect, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | color });
}

void DrawList(const Surface &out)
{
	const Rectangle popup = DrawerRect();
	// Slides out from behind the stash's edge; the rows appear once it's open.
	const uint32_t elapsed = SDL_GetTicks() - DrawerOpenedAt;
	if (elapsed < DrawerSlideMs) {
		const int width = std::max(8, static_cast<int>((popup.size.width * elapsed) / DrawerSlideMs));
		DrawPanel(out, { popup.position, { width, popup.size.height } });
		return;
	}
	DrawPanel(out, popup);
	DrawString(out, _("Stash tabs"), { popup.position + Displacement { 0, 4 }, { popup.size.width, 18 } },
	    { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhitegold });

	for (unsigned row = 0; row < ListRows; row++) {
		const unsigned entry = ListScroll + row;
		if (entry >= ListEntries())
			break;
		const Rectangle rect = ListRow(row);
		// While a tab is dragged only the marker shows where it goes.
		const bool hovered = rect.contains(MousePosition) && OpenPopup == Popup::None && !(Drag && Drag->moving);
		if (entry >= StashTabs.count()) {
			FillRectangle(out, rect, hovered ? RowHoverColor : RowColor);
			const StashTabKind kind = NewTabKind().value_or(StashTabKind::Blank);
			DrawString(out, kind == StashTabKind::Blank ? std::string(_("+ New tab")) : StrCat(_("+ New "), StashTabKindName(kind), _(" tab")), rect,
			    { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorGold });
			continue;
		}
		const StashTab &tab = StashTabs.tabs[entry];
		FillRectangle(out, rect, entry == Stash.GetPage() ? RowHoverColor : (hovered ? HoverColor : RowColor));
		const Rectangle swatch { rect.position + Displacement { 4, 2 }, { 14, rect.size.height - 4 } };
		FillRectangle(out, swatch, StashTabPaletteColor(tab.color));
		DrawFrame(out, swatch, InnerBorderColor);
		const UiFlags nameColor = hovered && entry != Stash.GetPage() ? UiFlags::ColorBlack : UiFlags::ColorWhite;
		DrawString(out, FitText(StashTabs.displayName(entry), 100), { rect.position + Displacement { 24, 0 }, { 104, rect.size.height } },
		    { .flags = UiFlags::VerticalCenter | nameColor });
		const std::string what = tab.isPremade() ? std::string(StashTabKindName(tab.kind)) : StashTabs.affinityText(entry);
		if (!what.empty() && what != StashTabs.displayName(entry)) {
			DrawString(out, FitText(what, rect.size.width - 134), { rect.position + Displacement { 130, 0 }, { rect.size.width - 134, rect.size.height } },
			    { .flags = UiFlags::AlignRight | UiFlags::VerticalCenter | UiFlags::ColorGold });
		}
	}

	if (Drag && Drag->moving && popup.contains(MousePosition)) {
		const unsigned insertion = DrawerInsertionAt(MousePosition);
		if (insertion >= ListScroll && insertion <= ListScroll + ListRows) {
			const Rectangle row = ListRow(0);
			const int y = row.position.y + (static_cast<int>(insertion - ListScroll) * RowHeight) - 2;
			FillRectangle(out, { { row.position.x, y }, { row.size.width, 2 } }, HoverColor);
		}
	}

	DrawString(out, _("Drag a tab to move it"), { popup.position + Displacement { 0, popup.size.height - 38 }, { popup.size.width, 16 } },
	    { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorButtonpushed });
	DrawString(out, _("Right-click: settings"), { popup.position + Displacement { 0, popup.size.height - 22 }, { popup.size.width, 16 } },
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

	DrawString(out, _("Colour"), { { popup.position.x + 14, SwatchRect(0).position.y }, { 68, SwatchSize.height } }, { .flags = UiFlags::VerticalCenter | UiFlags::ColorWhite });
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

	DrawString(out, _("Kind"), { popup.position + Displacement { 14, 128 }, { 120, 16 } }, { .flags = UiFlags::VerticalCenter | UiFlags::ColorWhite });
	const bool empty = IsStashTabEmpty(SettingsPage);
	for (size_t i = 0; i < std::size(KindChoices); i++) {
		const Rectangle rect = KindRect(i);
		const bool selected = tab.kind == KindChoices[i];
		const bool allowed = empty && KindRefusal(SettingsPage, KindChoices[i]).empty();
		FillRectangle(out, rect, selected ? RowHoverColor : RowColor);
		DrawFrame(out, rect, selected ? HoverColor : (rect.contains(MousePosition) && allowed ? HoverColor : BorderColor));
		DrawString(out, FitText(std::string(StashTabKindName(KindChoices[i])), rect.size.width - 4), rect,
		    { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | (selected ? UiFlags::ColorWhitegold : (allowed ? UiFlags::ColorWhite : UiFlags::ColorButtonpushed)) });
	}
	DrawString(out, empty ? _("One of each premade tab, two blank tabs") : _("Empty the tab to change its kind"),
	    { popup.position + Displacement { 14, 186 }, { popup.size.width - 28, 16 } }, { .flags = UiFlags::VerticalCenter | UiFlags::ColorButtonpushed });

	if (tab.isPremade()) {
		DrawString(out, _("Affinities"), { popup.position + Displacement { 14, 205 }, { 120, 16 } }, { .flags = UiFlags::VerticalCenter | UiFlags::ColorWhite });
		DrawString(out, StrCat(_("This tab only takes "), StashTabKindName(tab.kind), "."), { popup.position + Displacement { 14, 221 }, { popup.size.width - 28, 16 } },
		    { .flags = UiFlags::VerticalCenter | UiFlags::ColorButtonpushed });
	} else {
		DrawString(out, _("Affinities"), { popup.position + Displacement { 14, 205 }, { 120, 16 } }, { .flags = UiFlags::VerticalCenter | UiFlags::ColorWhite });
		DrawString(out, _("Tick some to take only those"), { popup.position + Displacement { 14, 221 }, { popup.size.width - 28, 16 } },
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
	}

	const auto popupButton = [&out](const Rectangle &rect, std::string_view text) {
		FillRectangle(out, rect, rect.contains(MousePosition) ? RowHoverColor : RowColor);
		DrawFrame(out, rect, BorderColor);
		DrawString(out, text, rect, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhite });
	};
	popupButton(ListButton(), _("All tabs"));
	popupButton(DoneButton(), _("Done"));
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
			const std::optional<StashTabKind> kind = NewTabKind();
			if (kind && StashTabs.addTab(*kind)) {
				Stash.dirty = true;
				ScrollList(1);
			}
			return true;
		}
		if (rightClick) {
			OpenSettings(entry);
		} else {
			Stash.SetPage(entry);
			if (MyPlayer->HoldItem.isEmpty())
				Drag = TabDrag { .page = entry, .start = MousePosition, .fromDrawer = true };
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
	for (size_t i = 0; i < std::size(KindChoices); i++) {
		if (!KindRect(i).contains(MousePosition) || StashTabs.kind(SettingsPage) == KindChoices[i])
			continue;
		if (!IsStashTabEmpty(SettingsPage)) {
			EventPlrMsg(_("Empty the tab to change its kind."), UiFlags::ColorWhite);
			return true;
		}
		const std::string refusal = KindRefusal(SettingsPage, KindChoices[i]);
		if (!refusal.empty()) {
			EventPlrMsg(refusal, UiFlags::ColorWhite);
			return true;
		}
		StashTabs.setKind(SettingsPage, KindChoices[i]);
		Stash.dirty = true;
		return true;
	}
	if (!StashTabs.tabs[SettingsPage].isPremade()) {
		for (size_t i = 0; i < std::size(AllStashAffinities); i++) {
			if (AffinityBox(i).contains(MousePosition)) {
				const StashAffinity affinity = AllStashAffinities[i];
				StashTabs.setAffinity(SettingsPage, affinity, !StashTabs.hasAffinity(SettingsPage, affinity));
				Stash.dirty = true;
				return true;
			}
		}
	}
	if (ListButton().contains(MousePosition)) {
		OpenPopup = Popup::None;
		if (!DrawerOpen)
			ToggleDrawer();
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
				OpenPopup = Popup::None;
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
		if (event.button.button == SDL_BUTTON_WHEELUP || event.button.button == SDL_BUTTON_WHEELDOWN)
			return true;
#endif
		if (event.button.button != SDL_BUTTON_LEFT && event.button.button != SDL_BUTTON_RIGHT)
			return true;
		if (!PopupRect().contains(MousePosition)) {
			if (EditingName) {
				CommitName();
				StopEditingName();
			}
			OpenPopup = Popup::None;
			return true;
		}
		return HandleSettingsClick();
	}
	case SDL_EVENT_MOUSE_BUTTON_UP:
		return true;
#ifndef USE_SDL1
	case SDL_EVENT_MOUSE_WHEEL:
		return true;
#endif
	default:
		return false;
	}
}

// ---------------------------------------------------------------------------
// The frame and the tab strip
// ---------------------------------------------------------------------------

constexpr int StripY = 4;
constexpr int StripHeight = 20;
constexpr int TabsLeft = 22;
constexpr int TabsRight = 266;
constexpr int HeaderY = 27;
constexpr int CellPitch = INV_SLOT_SIZE_PX + 1;
constexpr Size CellSize { INV_SLOT_SIZE_PX, INV_SLOT_SIZE_PX };

unsigned TabScroll = 0;
unsigned LastShownPage = static_cast<unsigned>(-1);

Rectangle StripLeftArrow()
{
	return At(6, StripY, 14, StripHeight);
}

Rectangle StripRightArrow()
{
	return At(268, StripY, 14, StripHeight);
}

Rectangle StripListButton()
{
	return At(284, StripY, 30, StripHeight);
}

int TabWidth(unsigned page)
{
	return std::clamp(GetLineWidth(StashTabs.displayName(page)) + 14, 28, 84);
}

struct VisibleTab {
	unsigned page;
	Rectangle rect;
};

std::vector<VisibleTab> VisibleTabs()
{
	std::vector<VisibleTab> visible;
	int x = TabsLeft;
	for (unsigned page = std::min(TabScroll, StashTabs.count()); page < StashTabs.count(); page++) {
		const int width = TabWidth(page);
		if (x + width > TabsRight && !visible.empty())
			break;
		visible.push_back({ page, At(x, StripY, width, StripHeight) });
		x += width + 2;
	}
	return visible;
}

/** @brief Where in the strip a dragged tab would go: before this page, or after the last tab shown. */
unsigned StripInsertionAt(Point position)
{
	const std::vector<VisibleTab> visible = VisibleTabs();
	for (const VisibleTab &tab : visible) {
		if (position.x < tab.rect.position.x + (tab.rect.size.width / 2))
			return tab.page;
	}
	return visible.empty() ? StashTabs.count() : visible.back().page + 1;
}

bool CanScrollTabsRight()
{
	const std::vector<VisibleTab> visible = VisibleTabs();
	return !visible.empty() && visible.back().page + 1 < StashTabs.count();
}

void EnsureTabVisible(unsigned page)
{
	if (page < TabScroll) {
		TabScroll = page;
		return;
	}
	while (TabScroll < page) {
		const std::vector<VisibleTab> visible = VisibleTabs();
		if (std::any_of(visible.begin(), visible.end(), [page](const VisibleTab &tab) { return tab.page == page; }))
			return;
		TabScroll++;
	}
}

void DrawArrow(const Surface &out, const Rectangle &rect, bool left, bool enabled)
{
	const bool hovered = enabled && rect.contains(MousePosition) && OpenPopup == Popup::None;
	FillRectangle(out, rect, hovered ? RowHoverColor : RowColor);
	DrawFrame(out, rect, hovered ? HoverColor : BorderColor);
	const uint8_t color = enabled ? (hovered ? HoverColor : InnerBorderColor) : SocketRimColor;
	const Point middle = rect.position + Displacement { rect.size.width / 2, rect.size.height / 2 };
	for (int i = 0; i < 5; i++) {
		const int x = left ? middle.x - 2 + i : middle.x + 2 - i;
		DrawVerticalLine(out, { x, middle.y - i }, (2 * i) + 1, color);
	}
}

void DrawTabStrip(const Surface &out)
{
	const unsigned current = Stash.GetPage();
	if (current != LastShownPage) {
		EnsureTabVisible(current);
		LastShownPage = current;
	}
	DrawArrow(out, StripLeftArrow(), true, TabScroll > 0);
	DrawArrow(out, StripRightArrow(), false, CanScrollTabsRight());
	DrawButton(out, StripListButton(), "...");

	for (const VisibleTab &visible : VisibleTabs()) {
		const StashTab &tab = StashTabs.tabs[visible.page];
		const bool active = visible.page == current;
		Rectangle rect = visible.rect;
		if (active)
			rect.size.height += 3; // joins the panel below
		FillRectangle(out, rect, StashTabPaletteColor(tab.color));
		const bool hovered = visible.rect.contains(MousePosition) && OpenPopup == Popup::None;
		if (active) {
			DrawHorizontalLine(out, rect.position, rect.size.width, HoverColor);
			DrawVerticalLine(out, rect.position, rect.size.height, HoverColor);
			DrawVerticalLine(out, rect.position + Displacement { rect.size.width - 1, 0 }, rect.size.height, HoverColor);
		} else {
			DrawFrame(out, rect, hovered ? HoverColor : InnerBorderColor);
			// Inactive tabs look dimmer: a dark band along their bottom.
			DrawHorizontalLine(out, rect.position + Displacement { 1, rect.size.height - 2 }, rect.size.width - 2, PanelColor);
		}
		DrawString(out, FitText(StashTabs.displayName(visible.page), visible.rect.size.width - 6), visible.rect,
		    { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | TextColorOn(tab.color) });
	}

	if (!Drag || !Drag->moving)
		return;
	// Holding a dragged tab over an arrow scrolls the strip.
	if (SDL_GetTicks() - DragScrolledAt >= DragScrollMs) {
		if (StripLeftArrow().contains(MousePosition) && TabScroll > 0) {
			TabScroll--;
			DragScrolledAt = SDL_GetTicks();
		} else if (StripRightArrow().contains(MousePosition) && CanScrollTabsRight()) {
			TabScroll++;
			DragScrolledAt = SDL_GetTicks();
		}
	}
	if (!At(0, 0, 320, HeaderY).contains(MousePosition))
		return;
	const std::vector<VisibleTab> visible = VisibleTabs();
	if (visible.empty())
		return;
	const unsigned insertion = StripInsertionAt(MousePosition);
	const auto before = std::find_if(visible.begin(), visible.end(), [insertion](const VisibleTab &tab) { return tab.page == insertion; });
	const int x = before != visible.end() ? before->rect.position.x - 2 : visible.back().rect.position.x + visible.back().rect.size.width;
	FillRectangle(out, { { x, visible.front().rect.position.y - 1 }, { 2, StripHeight + 2 } }, HoverColor);
}

/** @brief The dragged tab's name under the cursor. */
void DrawDraggedTab(const Surface &out)
{
	if (!Drag || !Drag->moving || Drag->page >= StashTabs.count())
		return;
	const StashTab &tab = StashTabs.tabs[Drag->page];
	const Rectangle rect { MousePosition + Displacement { 8, 6 }, { TabWidth(Drag->page), StripHeight } };
	FillRectangle(out, rect, StashTabPaletteColor(tab.color));
	DrawFrame(out, rect, HoverColor);
	DrawString(out, FitText(StashTabs.displayName(Drag->page), rect.size.width - 6), rect,
	    { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | TextColorOn(tab.color) });
}

/** @brief Puts the tab at the insertion place, the open tab staying open. */
void DropTab(unsigned page, unsigned insertion)
{
	const unsigned to = insertion > page ? insertion - 1 : insertion;
	if (to == page || to >= StashTabs.count())
		return;
	MoveStashTab(page, to);
	EnsureTabVisible(Stash.GetPage());
	LastShownPage = Stash.GetPage();
}

/** @brief Starts, follows and ends dragging a tab; on the drop, whether the stash took the event. */
std::optional<bool> HandleTabDragEvent(const SDL_Event &event)
{
	if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT && MyPlayer->HoldItem.isEmpty() && !Drag) {
		const Point position { SDLC_EventButtonIntX(event), SDLC_EventButtonIntY(event) };
		if (!DrawerOpen || !DrawerRect().contains(position)) {
			for (const VisibleTab &visible : VisibleTabs()) {
				if (visible.rect.contains(position))
					Drag = TabDrag { .page = visible.page, .start = position };
			}
		}
		// The click itself still opens the tab.
		return std::nullopt;
	}
	if (event.type == SDL_EVENT_MOUSE_MOTION) {
		if (Drag && !Drag->moving) {
			const Point position { SDLC_EventMotionIntX(event), SDLC_EventMotionIntY(event) };
			if (std::abs(position.x - Drag->start.x) + std::abs(position.y - Drag->start.y) > DragThreshold)
				Drag->moving = true;
		}
		return std::nullopt;
	}
	if (event.type != SDL_EVENT_MOUSE_BUTTON_UP || event.button.button != SDL_BUTTON_LEFT || !Drag)
		return std::nullopt;
	const TabDrag drag = *Drag;
	Drag = std::nullopt;
	if (drag.moving) {
		const Point position { SDLC_EventButtonIntX(event), SDLC_EventButtonIntY(event) };
		if (DrawerOpen && DrawerRect().contains(position))
			DropTab(drag.page, DrawerInsertionAt(position));
		else if (At(0, 0, 320, HeaderY).contains(position))
			DropTab(drag.page, StripInsertionAt(position));
	}
	// The game lets go of the mouse button it saw go down.
	return drag.fromDrawer;
}

// ---------------------------------------------------------------------------
// Tab views: what a tab shows, built the same way for drawing, hovering and clicking
// ---------------------------------------------------------------------------

struct SlotView {
	Rectangle rect;
	StashStruct::StashCell index = StashStruct::EmptyCell;
	/** The currency slot or collection entry an empty slot stands for, shown dimmed; -1 for none. */
	int fixedSlot = -1;
};

struct ButtonView {
	Rectangle rect;
	std::string text;
	bool selected = false;
	std::function<void()> onClick {};
	/** A second, smaller line under the text. */
	std::string detail {};
};

struct LabelView {
	Rectangle rect;
	std::string text;
	UiFlags flags = UiFlags::ColorWhitegold;
};

struct ScrollView {
	Rectangle track;
	int first = 0;
	int visible = 0;
	int total = 0;
};

struct TabView {
	std::vector<SlotView> slots;
	std::vector<ButtonView> buttons;
	std::vector<LabelView> labels;
	std::optional<ScrollView> scroll;
	/** Wheel scrolling: rows to move by, and where the wheel scrolls. */
	std::function<void(int)> onScroll;
	/** Collection slots and currency slots are big pictures of their entry. */
	bool collection = false;
};

/** UI choices on the premade tabs; not saved. */
enum class CurrencyPage : uint8_t {
	General,
	Essences,
	Omens,
};
CurrencyPage OpenCurrencyPage = CurrencyPage::General;
GemFilter OpenGemFilter = GemFilter::All;
int GemScroll = 0;
int OpenMapTier = 1;
int OpenMapPage = 0;
UniqueCategory OpenUniqueCategory = UniqueCategory::Weapons;
int UniqueScroll = 0;
/** Whether the Uniques tab hides the uniques not in the collection yet (Curtis 2026-10-16). */
bool UniquesCollectedOnly = false;

/** @brief The button that opens the tab's settings, at the right end of a row. */
void AddSettingsButton(TabView &view, int y, int x = 268)
{
	view.buttons.push_back({ .rect = At(x, y, 44, 18), .text = std::string(_("Edit")), .onClick = [] { OpenSettings(Stash.GetPage()); } });
}

/** Adds a section's slots in rows of ten from y; returns the y below them. */
int AddCurrencyRows(TabView &view, unsigned page, CurrencySection section, int y, bool label = true)
{
	const CurrencySectionRange range = GetCurrencySection(section);
	unsigned used = range.count;
	if (section != CurrencySection::Other) {
		used = 0;
		while (used < range.count && GetCurrencySlotKind(range.first + used).miscId != IMISC_INVALID)
			used++;
	}
	if (label) {
		view.labels.push_back({ At(15, y, 290, 12), std::string(CurrencySectionName(section)) });
		y += 13;
	}
	for (unsigned i = 0; i < used; i++) {
		const unsigned slot = range.first + i;
		const int column = static_cast<int>(i % 10);
		const int row = static_cast<int>(i / 10);
		view.slots.push_back({ At(15 + (column * CellPitch), y + (row * CellPitch), CellSize.width, CellSize.height), StashTabSlotItem(page, slot),
		    section == CurrencySection::Other ? -1 : static_cast<int>(slot) });
	}
	const int rows = static_cast<int>((used + 9) / 10);
	return y + (rows * CellPitch) + 3;
}

void BuildCurrencyView(TabView &view, unsigned page)
{
	constexpr std::pair<CurrencyPage, const char *> Pages[] = {
		{ CurrencyPage::General, N_("General") },
		{ CurrencyPage::Essences, N_("Essences") },
		{ CurrencyPage::Omens, N_("Omens") },
	};
	for (size_t i = 0; i < std::size(Pages); i++) {
		const CurrencyPage currencyPage = Pages[i].first;
		view.buttons.push_back({ .rect = At(8 + (static_cast<int>(i) * 74), HeaderY, 72, 18), .text = std::string(_(Pages[i].second)),
		    .selected = OpenCurrencyPage == currencyPage, .onClick = [currencyPage] { OpenCurrencyPage = currencyPage; } });
	}
	AddSettingsButton(view, HeaderY);

	switch (OpenCurrencyPage) {
	case CurrencyPage::General: {
		int y = 48;
		y = AddCurrencyRows(view, page, CurrencySection::Orbs, y);
		y = AddCurrencyRows(view, page, CurrencySection::Catalysts, y);
		y = AddCurrencyRows(view, page, CurrencySection::Augments, y);
		AddCurrencyRows(view, page, CurrencySection::Other, y);
		break;
	}
	case CurrencyPage::Essences: {
		// Rows are tiers, columns are kinds, in two blocks of six kinds.
		const CurrencySectionRange range = GetCurrencySection(CurrencySection::Essences);
		constexpr const char *TierNames[] = { N_("Lesser"), N_("Normal"), N_("Greater"), N_("Perfect") };
		constexpr unsigned Tiers = std::size(TierNames);
		const unsigned kinds = static_cast<unsigned>(EssenceKind::Count) - 1;
		for (unsigned block = 0; block * 6 < kinds; block++) {
			const int top = 50 + (static_cast<int>(block) * 134);
			view.labels.push_back({ At(15, top, 290, 12), block == 0 ? std::string(_("Essences")) : std::string(_("Essences, continued")) });
			for (unsigned tier = 0; tier < Tiers; tier++) {
				const int y = top + 14 + (static_cast<int>(tier) * CellPitch);
				view.labels.push_back({ At(12, y, 62, CellSize.height), std::string(_(TierNames[tier])), UiFlags::ColorWhite | UiFlags::VerticalCenter });
				for (unsigned column = 0; column < 6 && (block * 6) + column < kinds; column++) {
					const unsigned kind = (block * 6) + column;
					const unsigned slot = range.first + (kind * Tiers) + tier;
					view.slots.push_back({ At(78 + (static_cast<int>(column) * CellPitch), y, CellSize.width, CellSize.height), StashTabSlotItem(page, slot), static_cast<int>(slot) });
				}
			}
		}
		break;
	}
	case CurrencyPage::Omens:
		AddCurrencyRows(view, page, CurrencySection::Omens, 50);
		break;
	}
	view.labels.push_back({ At(10, 326, 300, 14), std::string(_("Up to 5000 a slot. A click takes 20.")), UiFlags::ColorButtonpushed | UiFlags::AlignCenter });
}

/** Lines of the Gems tab: a heading, or a row of up to ten gems. */
struct GemLine {
	std::string heading;
	std::vector<uint16_t> gems;
};

constexpr int GemAreaTop = 50;
constexpr int GemAreaBottom = 318;
constexpr int GemHeadingHeight = 15;

void BuildGemsView(TabView &view, unsigned page)
{
	// Each filter button is as wide as its name.
	int x = 8;
	for (uint8_t i = 0; i < static_cast<uint8_t>(GemFilter::Count); i++) {
		const auto filter = static_cast<GemFilter>(i);
		const std::string name { GemFilterName(filter) };
		const int width = std::clamp(GetLineWidth(name) + 6, 30, 80);
		view.buttons.push_back({ .rect = At(x, HeaderY, width, 18), .text = name, .selected = OpenGemFilter == filter,
		    .onClick = [filter] { OpenGemFilter = filter; GemScroll = 0; } });
		x += width + 2;
	}
	AddSettingsButton(view, HeaderY);

	const std::vector<uint16_t> all = StashTabItems(page);
	std::vector<uint16_t> gems;
	for (const uint16_t index : all) {
		if (GemMatchesFilter(Stash.stashList[index], OpenGemFilter))
			gems.push_back(index);
	}
	std::sort(gems.begin(), gems.end(), [](uint16_t a, uint16_t b) { return GemListsBefore(Stash.stashList[a], Stash.stashList[b]); });

	std::vector<GemLine> lines;
	std::optional<GemSection> section;
	for (const uint16_t index : gems) {
		const GemSection gemSection = GetGemSection(Stash.stashList[index]);
		if (!section || !(*section == gemSection)) {
			section = gemSection;
			lines.push_back({ GemSectionName(gemSection), {} });
			lines.push_back({});
		} else if (lines.back().gems.size() >= 10) {
			lines.push_back({});
		}
		lines.back().gems.push_back(index);
	}

	const auto lineHeight = [](const GemLine &line) { return line.heading.empty() ? CellPitch : GemHeadingHeight; };
	// The most lines that fit from the last one up gives the furthest scroll.
	int maxScroll = static_cast<int>(lines.size());
	for (int height = 0; maxScroll > 0;) {
		height += lineHeight(lines[static_cast<size_t>(maxScroll - 1)]);
		if (height > GemAreaBottom - GemAreaTop)
			break;
		maxScroll--;
	}
	GemScroll = std::clamp(GemScroll, 0, maxScroll);

	int y = GemAreaTop;
	int shown = 0;
	for (size_t i = static_cast<size_t>(GemScroll); i < lines.size(); i++) {
		const GemLine &line = lines[i];
		if (y + lineHeight(line) > GemAreaBottom)
			break;
		if (!line.heading.empty()) {
			view.labels.push_back({ At(12, y + 1, 286, 12), line.heading });
		} else {
			for (size_t column = 0; column < line.gems.size(); column++)
				view.slots.push_back({ At(12 + (static_cast<int>(column) * CellPitch), y, CellSize.width, CellSize.height), line.gems[column] });
		}
		y += lineHeight(line);
		shown++;
	}
	if (lines.empty())
		view.labels.push_back({ At(12, 120, 296, 14), std::string(_("Gems you put here are listed by level.")), UiFlags::ColorButtonpushed | UiFlags::AlignCenter });
	view.scroll = ScrollView { At(304, GemAreaTop, 8, GemAreaBottom - GemAreaTop), GemScroll, shown, static_cast<int>(lines.size()) };
	view.onScroll = [maxScroll](int rows) { GemScroll = std::clamp(GemScroll + rows, 0, maxScroll); };
	view.labels.push_back({ At(12, 324, 140, 14), StrCat(all.size(), " / ", GemTabSlots), UiFlags::ColorWhite });
	view.labels.push_back({ At(150, 324, 160, 14), std::string(_("Ctrl+click: to inventory")), UiFlags::ColorButtonpushed | UiFlags::AlignRight });
}

std::string_view TierNumeral(int tier)
{
	constexpr std::string_view Numerals[] = { "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII", "XIII", "XIV", "XV", "XVI" };
	return Numerals[std::clamp(tier, 1, 16) - 1];
}

constexpr int MapsPerPage = 80;

void BuildMapsView(TabView &view, unsigned page)
{
	const std::vector<uint16_t> all = StashTabItems(page);
	std::array<int, MaxWaystoneTier + 1> counts {};
	for (const uint16_t index : all)
		counts[static_cast<size_t>(MapTabTier(Stash.stashList[index]))]++;

	// Tiers I to XVI, then the tablets, fragments and keys. A button is as wide as its label needs, so none is cut off.
	int x = 8;
	for (int i = 0; i <= MaxWaystoneTier; i++) {
		const int tier = i < MaxWaystoneTier ? i + 1 : MapTabOtherTier;
		const int row = i / 9;
		if (i % 9 == 0)
			x = 8;
		std::string text = tier == MapTabOtherTier ? std::string(_("Other")) : std::string(TierNumeral(tier));
		const int width = std::max(32, GetLineWidth(text) + 8);
		const Rectangle rect = At(x, 26 + (row * 26), width, 24);
		x += width + 2;
		view.buttons.push_back({ .rect = rect,
		    .text = std::move(text),
		    .selected = OpenMapTier == tier,
		    .onClick = [tier] { OpenMapTier = tier; OpenMapPage = 0; },
		    .detail = StrCat(counts[static_cast<size_t>(tier)]) });
	}

	std::vector<uint16_t> maps;
	for (const uint16_t index : all) {
		if (MapTabTier(Stash.stashList[index]) == OpenMapTier)
			maps.push_back(index);
	}
	std::sort(maps.begin(), maps.end(), [](uint16_t a, uint16_t b) { return MapListsBefore(Stash.stashList[a], Stash.stashList[b]); });
	const int pages = std::max(1, static_cast<int>((maps.size() + MapsPerPage - 1) / MapsPerPage));
	OpenMapPage = std::clamp(OpenMapPage, 0, pages - 1);
	for (int i = 0; i < pages && i < 10; i++) {
		view.buttons.push_back({ .rect = At(8 + (i * 24), 80, 22, 16), .text = StrCat(i + 1), .selected = OpenMapPage == i, .onClick = [i] { OpenMapPage = i; } });
	}
	AddSettingsButton(view, 79);

	for (int i = 0; i < MapsPerPage; i++) {
		const size_t at = static_cast<size_t>((OpenMapPage * MapsPerPage) + i);
		const int column = i % 10;
		const int row = i / 10;
		view.slots.push_back({ At(15 + (column * CellPitch), 100 + (row * CellPitch), CellSize.width, CellSize.height),
		    at < maps.size() ? maps[at] : StashStruct::EmptyCell });
	}
	view.labels.push_back({ At(12, 334, 140, 12), StrCat(all.size(), " / ", MapTabSlots), UiFlags::ColorWhite });
}

constexpr int UniqueColumns = 9;
constexpr Size UniqueSlotSize { 30, 45 };
constexpr int UniqueAreaTop = 96;
constexpr int UniqueRows = 5;
constexpr int UniqueAreaLeft = 10;
constexpr int UniqueAreaWidth = 292;
constexpr int UniqueAreaHeight = UniqueRows * (UniqueSlotSize.height + 2);

/** @brief The biggest picture among a category's uniques, which the large pictures make room for. */
Size LargestUniquePicture(UniqueCategory category)
{
	if (HeadlessMode)
		return { 2 * INV_SLOT_SIZE_PX, 3 * INV_SLOT_SIZE_PX };
	Size largest { INV_SLOT_SIZE_PX, INV_SLOT_SIZE_PX };
	for (const UniqueCollectionEntry &entry : UniqueCollection()) {
		if (!entry.listed || entry.category != category || entry.cursor < 0)
			continue;
		const Size size = GetInvItemSize(entry.cursor + static_cast<int>(CURSOR_FIRSTITEM));
		largest.width = std::max(largest.width, size.width);
		largest.height = std::max(largest.height, size.height);
	}
	return largest;
}

struct UniqueGrid {
	Size slot;
	int columns;
	int rows;
	int gap;
	int left;
};

/** @brief Small pictures are half size in a fixed grid; large ones are full size, in as many columns and rows as fit. */
UniqueGrid UniqueGridFor(unsigned page)
{
	if (page >= StashTabs.count() || !StashTabs.tabs[page].bigPictures)
		return { UniqueSlotSize, UniqueColumns, UniqueRows, 2, UniqueAreaLeft };
	const Size picture = LargestUniquePicture(OpenUniqueCategory);
	const Size slot { picture.width + 1, picture.height + 1 };
	constexpr int Gap = 1;
	const int columns = std::max(UniqueAreaWidth / (slot.width + Gap), 1);
	const int rows = std::max(UniqueAreaHeight / (slot.height + Gap), 1);
	const int used = (columns * (slot.width + Gap)) - Gap;
	return { slot, columns, rows, Gap, UniqueAreaLeft + ((UniqueAreaWidth - used) / 2) };
}

void BuildUniquesView(TabView &view, unsigned page)
{
	view.collection = true;
	const std::vector<UniqueCollectionEntry> &collection = UniqueCollection();
	std::array<int, static_cast<size_t>(UniqueCategory::Count)> total {};
	std::array<int, static_cast<size_t>(UniqueCategory::Count)> collected {};
	int allTotal = 0;
	int allCollected = 0;
	std::vector<unsigned> shown;
	for (unsigned slot = 0; slot < collection.size() && slot < PremadeTabCapacity(StashTabKind::Uniques); slot++) {
		const UniqueCollectionEntry &entry = collection[slot];
		if (!entry.listed)
			continue;
		const auto category = static_cast<size_t>(entry.category);
		const bool have = StashTabSlotItem(page, slot) != StashStruct::EmptyCell;
		total[category]++;
		allTotal++;
		if (have) {
			collected[category]++;
			allCollected++;
		}
		if (entry.category == OpenUniqueCategory && (have || !UniquesCollectedOnly))
			shown.push_back(slot);
	}
	for (uint8_t i = 0; i < static_cast<uint8_t>(UniqueCategory::Count); i++) {
		const auto category = static_cast<UniqueCategory>(i);
		const int percent = total[i] > 0 ? (collected[i] * 100) / total[i] : 0;
		view.buttons.push_back({ .rect = At(8 + ((i % 4) * 76), 26 + ((i / 4) * 26), 74, 24),
		    .text = std::string(UniqueCategoryName(category)),
		    .selected = OpenUniqueCategory == category,
		    .onClick = [category] { OpenUniqueCategory = category; UniqueScroll = 0; },
		    .detail = StrCat(percent, "%") });
	}
	// The count sits under the grid, where the other premade tabs show theirs.
	view.labels.push_back({ At(12, 334, 140, 12), StrCat(_("Have"), " ", allCollected, " / ", allTotal), UiFlags::ColorWhite });
	view.buttons.push_back({ .rect = At(10, 77, 124, 18), .text = std::string(_("Collected only")), .selected = UniquesCollectedOnly,
	    .onClick = [] { UniquesCollectedOnly = !UniquesCollectedOnly; UniqueScroll = 0; } });
	// Full-size pictures, remembered with the tab (Curtis 2026-10-17).
	view.buttons.push_back({ .rect = At(138, 77, 126, 18), .text = std::string(_("Large pictures")), .selected = StashTabs.tabs[page].bigPictures,
	    .onClick = [page] {
		    StashTabs.tabs[page].bigPictures = !StashTabs.tabs[page].bigPictures;
		    Stash.dirty = true;
		    UniqueScroll = 0;
	    } });
	AddSettingsButton(view, 77);

	const UniqueGrid grid = UniqueGridFor(page);
	const int rows = static_cast<int>((shown.size() + grid.columns - 1) / grid.columns);
	const int maxScroll = std::max(rows - grid.rows, 0);
	UniqueScroll = std::clamp(UniqueScroll, 0, maxScroll);
	for (int row = 0; row < grid.rows; row++) {
		for (int column = 0; column < grid.columns; column++) {
			const size_t at = static_cast<size_t>(((UniqueScroll + row) * grid.columns) + column);
			if (at >= shown.size())
				break;
			const unsigned slot = shown[at];
			view.slots.push_back({ At(grid.left + (column * (grid.slot.width + grid.gap)), UniqueAreaTop + (row * (grid.slot.height + grid.gap)), grid.slot.width, grid.slot.height),
			    StashTabSlotItem(page, slot), static_cast<int>(slot) });
		}
	}
	if (shown.empty())
		view.labels.push_back({ At(12, 150, 296, 14), std::string(UniquesCollectedOnly ? _("None of this kind collected yet.") : _("No uniques of this kind.")), UiFlags::ColorButtonpushed | UiFlags::AlignCenter });
	view.scroll = ScrollView { At(302, UniqueAreaTop, 8, UniqueAreaHeight), UniqueScroll, std::min(rows, grid.rows), rows };
	view.onScroll = [maxScroll](int delta) { UniqueScroll = std::clamp(UniqueScroll + delta, 0, maxScroll); };
}

void BuildBlankView(TabView &view, unsigned page)
{
	// A main tab (no affinities) takes anything; it says nothing about it.
	if (const std::string affinities = StashTabs.affinityText(page); !affinities.empty())
		view.labels.push_back({ At(10, HeaderY + 2, 190, 14), StrCat(_("Takes: "), affinities), UiFlags::ColorButtonpushed });
	AddSettingsButton(view, HeaderY, 220);
	view.buttons.push_back({ .rect = At(266, HeaderY, 46, 18), .text = std::string(_("Sort")), .onClick = [] {
		                        if (!MyPlayer->HoldItem.isEmpty())
			                        return;
		                        if (!SortStashPage(Stash.GetPage()))
			                        EventPlrMsg(_("No room to sort this tab."), UiFlags::ColorWhite);
	                        } });
}

TabView BuildTabView()
{
	TabView view;
	if (StashTabs.count() == 0)
		return view;
	const unsigned page = std::min(Stash.GetPage(), StashTabs.count() - 1);
	switch (StashTabs.kind(page)) {
	case StashTabKind::Currency:
		BuildCurrencyView(view, page);
		break;
	case StashTabKind::Gems:
		BuildGemsView(view, page);
		break;
	case StashTabKind::Maps:
		BuildMapsView(view, page);
		break;
	case StashTabKind::Uniques:
		BuildUniquesView(view, page);
		break;
	default:
		BuildBlankView(view, page);
		break;
	}
	return view;
}

/** @brief Draws an item picture centred in a box, at half size when it doesn't fit. */
void DrawPicture(const Surface &out, int cursor, const Rectangle &box, const uint8_t *trn, std::optional<uint8_t> outline)
{
	if (cursor < 0)
		return;
	const int cursId = cursor + static_cast<int>(CURSOR_FIRSTITEM);
	ClxSprite sprite = GetInvItemSprite(cursId);
	// Half-size sprites exist only for pictures bigger than one cell; a 1x1 picture is always drawn whole.
	const bool biggerThanACell = sprite.width() > INV_SLOT_SIZE_PX || sprite.height() > INV_SLOT_SIZE_PX;
	if (biggerThanACell && (sprite.width() > box.size.width || sprite.height() > box.size.height)) {
		CreateHalfSizeItemSprites();
		sprite = GetHalfSizeItemSprite(cursor);
	}
	const Point position { box.position.x + ((box.size.width - static_cast<int>(sprite.width())) / 2),
		box.position.y + ((box.size.height + static_cast<int>(sprite.height())) / 2) - 1 };
	if (outline)
		ClxDrawOutline(out, *outline, position, sprite);
	if (trn != nullptr)
		ClxDrawTRN(out, position, sprite, trn);
	else
		ClxDraw(out, position, sprite);
}

const uint8_t *DimTrn()
{
	return LightTables[std::min<size_t>(7, NumLightingLevels - 1)].data();
}

/** @brief Grey and dark: a unique not in the collection yet, so it can't be mistaken for one that is. */
const uint8_t *MissingTrn()
{
	static std::array<uint8_t, 256> trn;
	const std::array<uint8_t, 256> &dark = LightTables[std::min<size_t>(9, NumLightingLevels - 1)];
	for (size_t i = 0; i < trn.size(); i++)
		trn[i] = dark[StoneTable[i]];
	return trn.data();
}

/** A unique in the collection has a gold frame. */
constexpr uint8_t CollectedFrameColor = PAL16_YELLOW + 2;

int SlotCursor(const SlotView &slot, bool collection)
{
	if (slot.fixedSlot < 0)
		return -1;
	if (collection) {
		const std::vector<UniqueCollectionEntry> &entries = UniqueCollection();
		return static_cast<size_t>(slot.fixedSlot) < entries.size() ? entries[static_cast<size_t>(slot.fixedSlot)].cursor : -1;
	}
	Item sample;
	return MakeCurrencySlotItem(static_cast<unsigned>(slot.fixedSlot), sample) ? sample._iCurs : -1;
}

std::string SlotName(const SlotView &slot, bool collection)
{
	if (slot.fixedSlot < 0)
		return {};
	if (collection) {
		const std::vector<UniqueCollectionEntry> &entries = UniqueCollection();
		return static_cast<size_t>(slot.fixedSlot) < entries.size() ? entries[static_cast<size_t>(slot.fixedSlot)].name : std::string {};
	}
	Item sample;
	return MakeCurrencySlotItem(static_cast<unsigned>(slot.fixedSlot), sample) ? std::string(sample._iIName) : std::string {};
}

void DrawSlot(const Surface &out, const SlotView &slot, bool collection)
{
	const bool hovered = slot.rect.contains(MousePosition) && OpenPopup == Popup::None;
	const bool empty = slot.index == StashStruct::EmptyCell || slot.index >= Stash.stashList.size();
	FillRectangle(out, slot.rect, SocketColor);
	uint8_t frame = SocketRimColor;
	if (hovered)
		frame = HoverColor;
	else if (collection)
		frame = empty ? SocketRimColor : CollectedFrameColor;
	DrawFrame(out, slot.rect, frame);
	if (empty) {
		if (!HeadlessMode)
			DrawPicture(out, SlotCursor(slot, collection), slot.rect, collection ? MissingTrn() : DimTrn(), std::nullopt);
		return;
	}
	const Item &item = Stash.stashList[slot.index];
	if (!collection)
		InvDrawSlotBack(out, slot.rect.position + Displacement { 0, slot.rect.size.height - 1 }, slot.rect.size, SlotBackQuality(item));
	std::optional<uint8_t> outline;
	if (pcursstashitem == slot.index)
		outline = GetOutlineColor(item, true);
	DrawPicture(out, item._iCurs, slot.rect, item._iStatFlag ? nullptr : GetInfravisionTRN(), outline);
	if (const int count = StoredStackCount(item); IsStackable(item) && count > 1) {
		DrawString(out, StrCat(count), { slot.rect.position + Displacement { 2, 1 }, { slot.rect.size.width, 12 } },
		    { .flags = UiFlags::ColorWhite | UiFlags::Outlined });
	}
	if (StashSearch.searching())
		DrawItemSearchMark(out, slot.rect, StashSearch.matches(item));
}

void DrawScroll(const Surface &out, const ScrollView &scroll)
{
	if (scroll.total <= scroll.visible || scroll.total <= 0)
		return;
	FillRectangle(out, scroll.track, RowColor);
	DrawFrame(out, scroll.track, SocketRimColor);
	const int height = std::max(scroll.track.size.height * scroll.visible / scroll.total, 8);
	const int maxFirst = std::max(scroll.total - scroll.visible, 1);
	const int top = (scroll.track.size.height - height) * std::min(scroll.first, maxFirst) / maxFirst;
	FillRectangle(out, { scroll.track.position + Displacement { 1, top }, { scroll.track.size.width - 2, height } }, InnerBorderColor);
}

void DrawTabView(const Surface &out, const TabView &view)
{
	for (const LabelView &label : view.labels)
		DrawString(out, label.text, label.rect, { .flags = label.flags });
	for (const ButtonView &button : view.buttons) {
		if (button.detail.empty()) {
			DrawButton(out, button.rect, button.text, button.selected);
			continue;
		}
		DrawButton(out, button.rect, "", button.selected);
		const UiFlags color = button.selected ? UiFlags::ColorWhitegold : UiFlags::ColorWhite;
		DrawString(out, FitText(button.text, button.rect.size.width - 4), { button.rect.position + Displacement { 0, 1 }, { button.rect.size.width, 11 } }, { .flags = UiFlags::AlignCenter | color });
		DrawString(out, button.detail, { button.rect.position + Displacement { 0, 12 }, { button.rect.size.width, 11 } },
		    { .flags = UiFlags::AlignCenter | UiFlags::ColorButtonpushed });
	}
	for (const SlotView &slot : view.slots)
		DrawSlot(out, slot, view.collection);
	if (view.scroll)
		DrawScroll(out, *view.scroll);
}

/** @brief The name of an empty slot under the mouse, for a tooltip. */
void DrawEmptySlotTooltip(const Surface &out, const TabView &view)
{
	if (OpenPopup != Popup::None || !MyPlayer->HoldItem.isEmpty())
		return;
	for (const SlotView &slot : view.slots) {
		if (!slot.rect.contains(MousePosition) || slot.index != StashStruct::EmptyCell)
			continue;
		const std::string name = SlotName(slot, view.collection);
		if (name.empty())
			return;
		DrawTooltip(out, { { 0, 0 }, { out.w(), out.h() } }, MousePosition, name, view.collection ? UiFlags::ColorWhitegold : UiFlags::ColorWhite,
		    { { std::string(view.collection ? _("Not collected yet") : _("None stored")), UiFlags::ColorButtonpushed } });
		return;
	}
}

bool HandleStripClick()
{
	if (StripLeftArrow().contains(MousePosition)) {
		if (TabScroll > 0)
			TabScroll--;
		return true;
	}
	if (StripRightArrow().contains(MousePosition)) {
		if (CanScrollTabsRight())
			TabScroll++;
		return true;
	}
	if (StripListButton().contains(MousePosition)) {
		ToggleDrawer();
		return true;
	}
	for (const VisibleTab &visible : VisibleTabs()) {
		if (visible.rect.contains(MousePosition)) {
			Stash.SetPage(visible.page);
			LastShownPage = visible.page;
			return true;
		}
	}
	return At(0, 0, 320, HeaderY).contains(MousePosition);
}

bool HandlePremadeSlotClick(const TabView &view, bool isShiftHeld, bool isCtrlHeld)
{
	Player &player = *MyPlayer;
	const unsigned page = Stash.GetPage();
	const auto slot = std::find_if(view.slots.begin(), view.slots.end(), [](const SlotView &s) { return s.rect.contains(MousePosition); });
	if (!player.HoldItem.isEmpty()) {
		if (const std::string_view refusal = StashTabRefusal(page, player.HoldItem); !refusal.empty()) {
			EventPlrMsg(refusal, UiFlags::ColorWhite);
			return true;
		}
		const int before = StoredStackCount(player.HoldItem);
		const int sound = player.HoldItem._iCurs;
		StoreInPremadeTab(page, player.HoldItem);
		if (!player.HoldItem.isEmpty() && StoredStackCount(player.HoldItem) == before) {
			EventPlrMsg(_("This tab has no room for that."), UiFlags::ColorWhite);
			return true;
		}
		if (!player.HoldItem.isEmpty())
			SetStackCount(player.HoldItem, StoredStackCount(player.HoldItem));
		PlaySFX(ItemInvSnds[ItemCAnimTbl[sound]]);
		NewCursor(player.HoldItem);
		return true;
	}
	if (slot == view.slots.end() || slot->index == StashStruct::EmptyCell || slot->index >= Stash.stashList.size())
		return true;
	// Shift-click on a currency stack opens a slider to pick how many to take (Curtis 2026-10-22).
	if (isShiftHeld && OpenStackSplit(StackSplitSource::Stash, slot->index))
		return true;
	if (isCtrlHeld) {
		TransferItemToInventory(player, slot->index);
		return true;
	}
	player.HoldItem = TakeFromStash(slot->index);
	player.HoldItem._iStatFlag = player.CanUseItem(player.HoldItem);
	PlaySFX(SfxID::GrabItem);
	NewCursor(player.HoldItem);
	return true;
}

} // namespace

bool IsStashTabPopupOpen()
{
	return OpenPopup != Popup::None;
}

bool StashDrawerContains(Point position)
{
	return gbIsPoe2 && IsStashOpen && DrawerOpen && DrawerRect().contains(position);
}

void CloseStashTabPopups()
{
	StopEditingName();
	OpenPopup = Popup::None;
	Drag = std::nullopt;
	// The drawer stays as it was, so it is open again next time the stash is (Curtis 2026-10-18).
}

void DrawPoe2StashFrame(const Surface &out)
{
	const Rectangle panel = At(0, 0, 320, 352 + SearchStripHeight);
	DrawPanel(out, panel);
	StashSearch.draw(out, StashSearchRect());
	// The tab's colour as a thin band under the strip, so the open tab reads at a glance.
	if (StashTabs.count() > 0) {
		const unsigned page = std::min(Stash.GetPage(), StashTabs.count() - 1);
		FillRectangle(out, At(4, StripY + StripHeight, 312, 2), StashTabPaletteColor(StashTabs.tabs[page].color));
	}
	DrawTabStrip(out);
	if (StashTabs.kind(Stash.GetPage()) == StashTabKind::Blank)
		DrawTabView(out, BuildTabView());
}

void DrawPoe2StashGridBack(const Surface &out)
{
	// The same stone grid as the hero's bag (Curtis 2026-10-17).
	if (gbIsPoe2) {
		DrawInventoryGridBackground(out, GetStashSlotCoord({ 0, 0 }), StashGridSize);
		return;
	}
	FillRectangle(out, { GetStashSlotCoord({ 0, 0 }) - Displacement { 1, 1 }, { (StashGridSize.width * CellPitch) + 1, (StashGridSize.height * CellPitch) + 1 } }, SocketRimColor);
	for (const Point slot : StashGridRange)
		FillRectangle(out, { GetStashSlotCoord(slot), CellSize }, SocketColor);
}

void DrawStashTabUi(const Surface &out)
{
	if (StashTabs.count() == 0)
		return;
	if (StashTabs.kind(Stash.GetPage()) != StashTabKind::Blank) {
		const TabView view = BuildTabView();
		DrawTabView(out, view);
	}

	if (DrawerOpen)
		DrawList(out);
	if (OpenPopup == Popup::Settings)
		DrawSettings(out);
	DrawDraggedTab(out);
}

void DrawStashTabTooltip(const Surface &out)
{
	// Drawn with the item info box, above the tab list and the HUD, which would otherwise cover the right of it.
	if (!gbIsPoe2 || !IsStashOpen || CharFlag || QuestLogIsOpen || StashTabs.count() == 0 || StashTabs.kind(Stash.GetPage()) == StashTabKind::Blank)
		return;
	if (DrawerOpen && DrawerRect().contains(MousePosition))
		return;
	DrawEmptySlotTooltip(out, BuildTabView());
}

StashStruct::StashCell PremadeTabItemUnderCursor(Point mousePosition)
{
	if (StashTabs.kind(Stash.GetPage()) == StashTabKind::Blank)
		return StashStruct::EmptyCell;
	const TabView view = BuildTabView();
	for (const SlotView &slot : view.slots) {
		if (slot.rect.contains(mousePosition))
			return slot.index;
	}
	return StashStruct::EmptyCell;
}

bool HandlePoe2StashClick(Point mousePosition, bool isShiftHeld, bool isCtrlHeld)
{
	if (OpenPopup != Popup::None)
		return true;
	const Point saved = MousePosition;
	MousePosition = mousePosition;
	bool handled = HandleStripClick();
	if (!handled) {
		const TabView view = BuildTabView();
		for (const ButtonView &button : view.buttons) {
			if (button.rect.contains(MousePosition) && button.onClick) {
				button.onClick();
				handled = true;
				break;
			}
		}
		if (!handled && StashTabs.kind(Stash.GetPage()) != StashTabKind::Blank)
			handled = HandlePremadeSlotClick(view, isShiftHeld, isCtrlHeld);
	}
	MousePosition = saved;
	return handled;
}

bool StashTabsHandleEvent(const SDL_Event &event)
{
	if (!gbIsPoe2 || !IsStashOpen) {
		if (OpenPopup != Popup::None || Drag)
			CloseStashTabPopups();
		StashSearch.unfocus();
		return false;
	}
	if (OpenPopup != Popup::None)
		return HandlePopupEvent(event);
	if (const std::optional<bool> dropped = HandleTabDragEvent(event))
		return *dropped;
	if (StashSearch.handleEvent(event, StashSearchRect()))
		return true;

	// The drawer takes clicks and the wheel over it; everything else goes to the stash as usual, so Escape closes
	// the whole stash, drawer and all (Curtis 2026-10-18).
	if (DrawerOpen) {
		if (DrawerRect().contains(MousePosition)) {
#ifdef USE_SDL1
			if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && (event.button.button == SDL_BUTTON_WHEELUP || event.button.button == SDL_BUTTON_WHEELDOWN)) {
				ScrollList(event.button.button == SDL_BUTTON_WHEELUP ? -3 : 3);
				return true;
			}
#else
			if (event.type == SDL_EVENT_MOUSE_WHEEL) {
				ScrollList(SDLC_EventWheelIntY(event) > 0 ? -3 : 3);
				return true;
			}
#endif
			if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
				if (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)
					HandleListClick(event.button.button == SDL_BUTTON_RIGHT);
				return true;
			}
			if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
				return true;
		}
	}

#ifndef USE_SDL1
	if (event.type == SDL_EVENT_MOUSE_WHEEL && GetLeftPanel().contains(MousePosition)) {
		const int rows = SDLC_EventWheelIntY(event) > 0 ? -1 : 1;
		if (At(0, 0, 320, HeaderY).contains(MousePosition)) {
			if (rows < 0 && TabScroll > 0)
				TabScroll--;
			else if (rows > 0 && CanScrollTabsRight())
				TabScroll++;
			return true;
		}
		const TabView view = BuildTabView();
		if (view.onScroll) {
			view.onScroll(rows);
			return true;
		}
		return false;
	}
#endif
	if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN)
		return false;

#ifdef USE_SDL1
	if ((event.button.button == SDL_BUTTON_WHEELUP || event.button.button == SDL_BUTTON_WHEELDOWN) && GetLeftPanel().contains(MousePosition)) {
		const TabView view = BuildTabView();
		if (view.onScroll)
			view.onScroll(event.button.button == SDL_BUTTON_WHEELUP ? -1 : 1);
		return true;
	}
#endif

	const bool rightClick = event.button.button == SDL_BUTTON_RIGHT;
	if (event.button.button != SDL_BUTTON_LEFT && !rightClick)
		return false;
	if (rightClick && MyPlayer->HoldItem.isEmpty()) {
		for (const VisibleTab &visible : VisibleTabs()) {
			if (visible.rect.contains(MousePosition)) {
				OpenSettings(visible.page);
				return true;
			}
		}
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
