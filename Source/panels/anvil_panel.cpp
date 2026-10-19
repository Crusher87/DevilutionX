#include "panels/anvil_panel.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#ifdef USE_SDL1
#include "utils/sdl2_to_1_2_backports.h"
#endif

#include "control/control.hpp"
#include "controls/wasd_movement.hpp"
#include "diablo.h"
#include "effects.h"
#include "engine/dx.h"
#include "game_mode.hpp"
#include "inv.h"
#include "panels/poe2_widgets.hpp"
#include "panels/skill_bar.hpp"
#include "player.h"
#include "poe2/bases.hpp"
#include "poe2/economy.hpp"
#include "utils/format.hpp"
#include "utils/language.h"
#include "utils/sdl_compat.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

using namespace poe2ui;

constexpr int Width = 560;
constexpr int Padding = 14;
constexpr int HeaderHeight = 108;
/** The list keeps this height however few items it holds, so the panel doesn't jump as items are salvaged. */
constexpr int ListRows = 6;
constexpr int FooterHeight = 64;
constexpr int RowHeight = 40;
constexpr int RowGap = 4;
constexpr int ScrollStep = RowHeight + RowGap;
constexpr Size ButtonSize { 120, 28 };

constexpr uint8_t SelectedColor = PAL16_YELLOW + 13;
constexpr uint8_t ButtonColor = PAL16_RED + 12;
constexpr uint8_t ButtonHoverColor = PAL16_RED + 9;

bool IsOpen;
int Scroll = 0;
/** The item picked to salvage, found again by its seed so a changed inventory never salvages the wrong one. */
struct Pick {
	int invIndex;
	uint32_t seed;
};
std::optional<Pick> Picked;

struct Row {
	int invIndex;
	Rectangle rect;
};

struct Layout {
	Rectangle panel;
	Rectangle list;
	std::vector<Row> rows;
	Rectangle button;
	int contentHeight = 0;
};

bool GivesSomething(const Item &item)
{
	const std::optional<SalvageYield> yield = GetSalvageYield(item);
	return yield && (yield->full > 0 || yield->socketShards > 0);
}

/** @brief The inventory items worth salvaging: those with quality or sockets. */
std::vector<int> SalvageableItems(const Player &player)
{
	std::vector<int> items;
	for (int i = 0; i < player._pNumInv; i++) {
		if (GivesSomething(player.InvList[i]))
			items.push_back(i);
	}
	return items;
}

std::optional<int> PickedIndex()
{
	if (!Picked || MyPlayer == nullptr)
		return std::nullopt;
	const Player &player = *MyPlayer;
	if (Picked->invIndex >= player._pNumInv)
		return std::nullopt;
	const Item &item = player.InvList[Picked->invIndex];
	if (static_cast<uint32_t>(item._iSeed) != Picked->seed || !GivesSomething(item))
		return std::nullopt;
	return Picked->invIndex;
}

Layout ComputeLayout(const Surface &out, const Player &player)
{
	Layout layout;
	const int availableHeight = out.h() - GetMainPanel().size.height;
	const int width = std::min(Width, out.w() - 16);
	const std::vector<int> items = SalvageableItems(player);
	layout.contentHeight = std::max(static_cast<int>(items.size()) * ScrollStep - RowGap, 0);
	const int wanted = HeaderHeight + (ListRows * ScrollStep) - RowGap + 2 + FooterHeight;
	const int height = std::min(wanted, availableHeight - 8);
	layout.panel = { { (out.w() - width) / 2, std::max((availableHeight - height) / 2, 4) }, { width, height } };
	const Point origin = layout.panel.position;
	layout.list = { origin + Displacement { Padding, HeaderHeight }, { width - (2 * Padding), height - HeaderHeight - FooterHeight } };
	Scroll = std::clamp(Scroll, 0, std::max(layout.contentHeight - layout.list.size.height + 2, 0));
	int y = layout.list.position.y + 1 - Scroll;
	for (const int invIndex : items) {
		layout.rows.push_back({ invIndex, { { layout.list.position.x + 1, y }, { layout.list.size.width - 2, RowHeight } } });
		y += ScrollStep;
	}
	layout.button = { origin + Displacement { width - Padding - ButtonSize.width, height - FooterHeight + ((FooterHeight - ButtonSize.height) / 2) }, ButtonSize };
	return layout;
}

std::string ItemDetails(const Item &item)
{
	const int quality = ItemQuality(item);
	const int sockets = item._iPoe2Craft.sockets;
	std::string text = quality > 0 ? FormatRuntime(_("{:d}% quality"), quality) : std::string(_("No quality"));
	if (sockets > 0)
		StrAppend(text, ", ", sockets, " ", sockets == 1 ? _("socket") : _("sockets"));
	return text;
}

void Salvage(int invIndex)
{
	Player &player = *MyPlayer;
	if (SalvageInvItem(player, invIndex)) {
		PlaySFX(SfxID::ItemAnvil);
		CalcPlrInv(player, true);
	}
	Picked = std::nullopt;
}

bool HandleClick(Point position)
{
	if (MyPlayer == nullptr)
		return true;
	const Layout layout = ComputeLayout(GlobalBackBuffer(), *MyPlayer);
	if (const std::optional<int> picked = PickedIndex(); picked && layout.button.contains(position)) {
		Salvage(*picked);
		return true;
	}
	if (!layout.list.contains(position))
		return true;
	for (const Row &row : layout.rows) {
		if (row.rect.contains(position)) {
			Picked = Pick { row.invIndex, static_cast<uint32_t>(MyPlayer->InvList[row.invIndex]._iSeed) };
			return true;
		}
	}
	return true;
}

} // namespace

bool IsAnvilOpen()
{
	return IsOpen;
}

void OpenAnvil()
{
	if (!gbIsPoe2 || MyPlayer == nullptr)
		return;
	IsOpen = true;
	Scroll = 0;
	Picked = std::nullopt;
	// The panel takes the mouse button's release, like the gemcutting panel.
	sgbMouseDown = CLICK_NONE;
	LastPlayerAction = PlayerActionType::None;
	ReleaseAllTraySlots();
	ClearWasdMovementKeys();
}

void CloseAnvil()
{
	if (!IsOpen)
		return;
	IsOpen = false;
	Picked = std::nullopt;
	ClearWasdMovementKeys();
}

bool AnvilHandleEvent(const SDL_Event &event)
{
	if (!IsOpen)
		return false;
	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
		if (SDLC_EventKey(event) == SDLK_ESCAPE)
			CloseAnvil();
		return true;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (event.button.button == SDL_BUTTON_LEFT)
			return HandleClick(MousePosition);
#ifdef USE_SDL1
		if (event.button.button == SDL_BUTTON_WHEELUP)
			Scroll -= ScrollStep;
		else if (event.button.button == SDL_BUTTON_WHEELDOWN)
			Scroll += ScrollStep;
#endif
		return true;
	case SDL_EVENT_KEY_UP:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		return true;
#ifndef USE_SDL1
	case SDL_EVENT_MOUSE_WHEEL:
		Scroll += SDLC_EventWheelIntY(event) > 0 ? -ScrollStep : ScrollStep;
		return true;
#endif
	default:
		return false;
	}
}

void DrawAnvil(const Surface &out)
{
	if (!IsOpen || MyPlayer == nullptr)
		return;
	const Player &player = *MyPlayer;
	const Layout layout = ComputeLayout(out, player);
	DrawPanel(out, layout.panel);
	const Rectangle title { layout.panel.position + Displacement { Padding, 8 }, { layout.panel.size.width - (2 * Padding), 26 } };
	DrawString(out, _("Anvil"), title, { .flags = UiFlags::FontSize24 | UiFlags::ColorUiGold | UiFlags::AlignCenter | UiFlags::VerticalCenter });
	DrawString(out, _("Esc to close"), title, { .flags = UiFlags::AlignRight | UiFlags::ColorButtonpushed | UiFlags::VerticalCenter });
	const Rectangle intro { title.position + Displacement { 0, 32 }, { title.size.width, HeaderHeight - 44 } };
	DrawString(out, WordWrapString(_("Break down weapons, armour, shields, flasks and charms from your inventory. Each 5% quality gives one matching quality currency, rounded up, and each socket gives an Artificer's Shard. Runes in the sockets are lost."), intro.size.width),
	    intro, { .flags = UiFlags::FontSize12 | UiFlags::ColorWhite, .lineHeight = 15 });

	FillRectangle(out, layout.list, PanelColor);
	DrawFrame(out, layout.list, InnerBorderColor);
	const std::optional<int> picked = PickedIndex();
	const Surface list = out.subregion(layout.list.position.x + 1, layout.list.position.y + 1, layout.list.size.width - 2, layout.list.size.height - 2);
	const Displacement toList { -(layout.list.position.x + 1), -(layout.list.position.y + 1) };
	for (const Row &row : layout.rows) {
		const Item &item = player.InvList[row.invIndex];
		const std::optional<SalvageYield> yield = GetSalvageYield(item);
		const bool hovered = layout.list.contains(MousePosition) && row.rect.contains(MousePosition);
		const Rectangle rect { row.rect.position + toList, row.rect.size };
		FillRectangle(list, rect, picked == row.invIndex ? SelectedColor : (hovered ? RowHoverColor : RowColor));
		DrawFrame(list, rect, picked == row.invIndex || hovered ? HoverColor : InnerBorderColor);
		const Rectangle top { rect.position + Displacement { 8, 3 }, { rect.size.width - 16, 18 } };
		DrawString(list, item.getName().str(), top, { .flags = item.getTextColor() | UiFlags::VerticalCenter });
		DrawString(list, ItemDetails(item), top, { .flags = UiFlags::FontSize12 | UiFlags::ColorButtonpushed | UiFlags::AlignRight | UiFlags::VerticalCenter });
		const Rectangle bottom { top.position + Displacement { 0, 18 }, top.size };
		DrawString(list, StrCat(_("Gives "), SalvageYieldText(*yield)), bottom, { .flags = UiFlags::FontSize12 | UiFlags::ColorWhitegold | UiFlags::VerticalCenter });
	}
	if (layout.rows.empty())
		DrawString(list, _("Nothing to salvage. Items need quality or sockets."), { { 0, 0 }, { layout.list.size.width - 2, 40 } },
		    { .flags = UiFlags::ColorButtonpushed | UiFlags::AlignCenter | UiFlags::VerticalCenter });

	const Rectangle footer { { layout.panel.position.x + Padding, layout.button.position.y - 4 }, { layout.button.position.x - layout.panel.position.x - (2 * Padding), ButtonSize.height + 8 } };
	if (!picked) {
		if (!layout.rows.empty())
			DrawString(out, _("Pick an item to salvage."), footer, { .flags = UiFlags::ColorButtonpushed | UiFlags::VerticalCenter });
		return;
	}
	// Asking first: salvaging destroys the item for good.
	const Item &item = player.InvList[*picked];
	DrawString(out, WordWrapString(FormatRuntime(_("Salvage {:s} for {:s}? The item is destroyed."), item.getName().str(), SalvageYieldText(*GetSalvageYield(item))), footer.size.width), footer,
	    { .flags = UiFlags::FontSize12 | UiFlags::ColorWhite | UiFlags::VerticalCenter, .lineHeight = 15 });
	const bool hovered = layout.button.contains(MousePosition);
	FillRectangle(out, layout.button, hovered ? ButtonHoverColor : ButtonColor);
	DrawFrame(out, layout.button, hovered ? HoverColor : BorderColor);
	DrawString(out, _("Salvage"), layout.button, { .flags = UiFlags::ColorWhitegold | UiFlags::AlignCenter | UiFlags::VerticalCenter });
}

} // namespace devilution
