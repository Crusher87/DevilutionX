#include "panels/stack_split_panel.hpp"

#include <algorithm>
#include <optional>

#ifdef USE_SDL1
#include "utils/sdl2_to_1_2_backports.h"
#endif

#include "controls/control_mode.hpp"
#include "cursor.h"
#include "effects.h"
#include "engine/dx.h"
#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "msg.h"
#include "panels/poe2_widgets.hpp"
#include "player.h"
#include "poe2/stacking.hpp"
#include "poe2/stash_layouts.hpp"
#include "qol/stash.h"
#include "utils/format_int.hpp"
#include "utils/language.h"
#include "utils/sdl_compat.h"

namespace devilution {

namespace {

using namespace poe2ui;

constexpr Size PanelSize { 180, 70 };
constexpr Size KnobSize { 8, 14 };
constexpr Size OkSize { 64, 18 };

/** The stack being split, found again by its seed when the amount is taken, in case it moved. */
struct Split {
	StackSplitSource source;
	uint16_t index;
	int idx;
	uint32_t seed;
	int max;
	int amount;
	/** Where it was clicked; the slider sits above it. */
	Point anchor;
	bool dragging = false;
	/** Digits typed since the amount last changed another way. */
	bool typing = false;
	int typed = 0;
};
std::optional<Split> Open;

struct Layout {
	Rectangle panel;
	Rectangle amount;
	/** Where a click or a drag sets the amount. */
	Rectangle track;
	Rectangle ok;
};

Item *ItemAt(StackSplitSource source, uint16_t index)
{
	if (MyPlayer == nullptr)
		return nullptr;
	if (source == StackSplitSource::Inventory)
		return index < MyPlayer->_pNumInv ? &MyPlayer->InvList[index] : nullptr;
	return index < Stash.stashList.size() ? &Stash.stashList[index] : nullptr;
}

Item *StackItem()
{
	if (!Open)
		return nullptr;
	Item *item = ItemAt(Open->source, Open->index);
	if (item == nullptr || item->isEmpty() || item->IDidx != Open->idx || static_cast<uint32_t>(item->_iSeed) != Open->seed)
		return nullptr;
	return item;
}

/** A Currency tab slot holds more than an inventory stack. */
int CountOf(StackSplitSource source, const Item &item)
{
	return source == StackSplitSource::Stash ? StoredStackCount(item) : StackCount(item);
}

Layout ComputeLayout(Size screen)
{
	Point position = Open->anchor + Displacement { -PanelSize.width / 2, -PanelSize.height - 16 };
	// Below the stack when there is no room above it.
	if (position.y < 4)
		position.y = Open->anchor.y + 16;
	position.x = std::clamp(position.x, 4, std::max(screen.width - PanelSize.width - 4, 4));
	position.y = std::clamp(position.y, 4, std::max(screen.height - PanelSize.height - 4, 4));
	Layout layout;
	layout.panel = { position, PanelSize };
	layout.amount = { position + Displacement { 8, 6 }, { PanelSize.width - 16, 18 } };
	layout.track = { position + Displacement { 14, 26 }, { PanelSize.width - 28, KnobSize.height } };
	layout.ok = { position + Displacement { (PanelSize.width - OkSize.width) / 2, PanelSize.height - OkSize.height - 8 }, OkSize };
	return layout;
}

Layout ScreenLayout()
{
	const Surface out = GlobalBackBuffer();
	return ComputeLayout({ out.w(), out.h() });
}

int KnobX(const Rectangle &track)
{
	if (Open->max <= 1)
		return track.position.x;
	return track.position.x + ((Open->amount - 1) * (track.size.width - KnobSize.width)) / (Open->max - 1);
}

void SetAmountAt(const Rectangle &track, int x)
{
	const int span = track.size.width - KnobSize.width;
	const int offset = std::clamp(x - track.position.x - (KnobSize.width / 2), 0, span);
	Open->typing = false;
	SetStackSplitAmount(1 + (((offset * (Open->max - 1)) + (span / 2)) / span));
}

void Step(int delta)
{
	Open->typing = false;
	SetStackSplitAmount(Open->amount + delta);
}

void HandleKey(SDL_Keycode key)
{
	switch (key) {
	case SDLK_ESCAPE:
		CloseStackSplit();
		return;
	case SDLK_RETURN:
	case SDLK_KP_ENTER:
		ConfirmStackSplit();
		return;
	case SDLK_LEFT:
	case SDLK_DOWN:
		Step(-1);
		return;
	case SDLK_RIGHT:
	case SDLK_UP:
		Step(1);
		return;
	case SDLK_HOME:
		Step(-Open->max);
		return;
	case SDLK_END:
		Step(Open->max);
		return;
	case SDLK_BACKSPACE:
		if (Open->typing) {
			Open->typed /= 10;
			SetStackSplitAmount(Open->typed);
		}
		return;
	default:
		break;
	}
	// Typing a number sets it, as in POE2's box beside the slider.
	if (key >= SDLK_0 && key <= SDLK_9) {
		const int digit = static_cast<int>(key - SDLK_0);
		Open->typed = Open->typing ? std::min((Open->typed * 10) + digit, 9999) : digit;
		Open->typing = true;
		SetStackSplitAmount(Open->typed);
	}
}

} // namespace

bool IsStackSplitOpen()
{
	return Open.has_value();
}

bool OpenStackSplit(StackSplitSource source, uint16_t index)
{
	if (!gbIsPoe2 || MyPlayer == nullptr || !MyPlayer->HoldItem.isEmpty())
		return false;
	// A gamepad's clicks don't pass through the slider, so it keeps taking half.
	if (ControlMode != ControlTypes::KeyboardAndMouse || ControlDevice != ControlTypes::KeyboardAndMouse)
		return false;
	const Item *item = ItemAt(source, index);
	if (item == nullptr || !IsStackable(*item))
		return false;
	const int count = CountOf(source, *item);
	if (count < 2)
		return false;
	Split split {};
	split.source = source;
	split.index = index;
	split.idx = item->IDidx;
	split.seed = static_cast<uint32_t>(item->_iSeed);
	// At most an inventory stack goes on the cursor, and at least one stays behind.
	split.max = std::min(count - 1, CurrencyStackMax);
	split.amount = std::clamp(count / 2, 1, split.max);
	split.anchor = MousePosition;
	Open = split;
	return true;
}

void CloseStackSplit()
{
	Open = std::nullopt;
}

int StackSplitAmount()
{
	return Open ? Open->amount : 0;
}

void SetStackSplitAmount(int amount)
{
	if (Open)
		Open->amount = std::clamp(amount, 1, Open->max);
}

void ConfirmStackSplit()
{
	Item *stack = StackItem();
	if (stack == nullptr || !MyPlayer->HoldItem.isEmpty()) {
		CloseStackSplit();
		return;
	}
	Player &player = *MyPlayer;
	const StackSplitSource source = Open->source;
	const uint16_t index = Open->index;
	const int amount = Open->amount;
	CloseStackSplit();
	if (source == StackSplitSource::Inventory) {
		const std::optional<Item> part = SplitStack(*stack, amount);
		if (!part)
			return;
		player.HoldItem = *part;
		NetSyncInvItem(player, index);
		CalcPlrInv(player, true);
	} else {
		player.HoldItem = TakeFromStash(index, amount);
		if (player.HoldItem.isEmpty())
			return;
	}
	player.HoldItem._iStatFlag = player.CanUseItem(player.HoldItem);
	PlaySFX(SfxID::GrabItem);
	NewCursor(player.HoldItem);
}

bool StackSplitHandleEvent(const SDL_Event &event)
{
	if (!Open)
		return false;
	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
		HandleKey(SDLC_EventKey(event));
		return true;
	case SDL_EVENT_MOUSE_BUTTON_DOWN: {
#ifdef USE_SDL1
		if (event.button.button == SDL_BUTTON_WHEELUP || event.button.button == SDL_BUTTON_WHEELDOWN) {
			Step(event.button.button == SDL_BUTTON_WHEELUP ? 1 : -1);
			return true;
		}
#endif
		if (event.button.button != SDL_BUTTON_LEFT) {
			CloseStackSplit();
			return true;
		}
		const Point position { SDLC_EventButtonIntX(event), SDLC_EventButtonIntY(event) };
		const Layout layout = ScreenLayout();
		if (layout.ok.contains(position)) {
			ConfirmStackSplit();
		} else if (layout.track.contains(position)) {
			Open->dragging = true;
			SetAmountAt(layout.track, position.x);
		} else if (!layout.panel.contains(position)) {
			CloseStackSplit();
		}
		return true;
	}
	case SDL_EVENT_MOUSE_MOTION:
		// The game still sees the mouse move.
		if (Open->dragging)
			SetAmountAt(ScreenLayout().track, SDLC_EventMotionIntX(event));
		return false;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		Open->dragging = false;
		return false;
#ifndef USE_SDL1
	case SDL_EVENT_MOUSE_WHEEL:
		Step(SDLC_EventWheelIntY(event) > 0 ? 1 : -1);
		return true;
#endif
	default:
		return false;
	}
}

void DrawStackSplit(const Surface &out)
{
	if (!Open)
		return;
	const bool shown = Open->source == StackSplitSource::Inventory ? invflag : IsStashOpen;
	if (!shown || StackItem() == nullptr || ControlDevice != ControlTypes::KeyboardAndMouse) {
		CloseStackSplit();
		return;
	}
	const Layout layout = ComputeLayout({ out.w(), out.h() });
	DrawPanel(out, layout.panel);
	DrawString(out, FormatInteger(Open->amount), layout.amount, { .flags = UiFlags::ColorWhite | UiFlags::AlignCenter | UiFlags::VerticalCenter });

	const Rectangle &track = layout.track;
	const int knobX = KnobX(track);
	const int barY = track.position.y + (track.size.height / 2) - 2;
	FillRectangle(out, { { track.position.x, barY }, { track.size.width, 4 } }, SocketRimColor);
	FillRectangle(out, { { track.position.x, barY }, { knobX - track.position.x + (KnobSize.width / 2), 4 } }, BorderColor);
	const Rectangle knob { { knobX, track.position.y }, KnobSize };
	FillRectangle(out, knob, Open->dragging || knob.contains(MousePosition) ? HoverColor : InnerBorderColor);
	DrawFrame(out, knob, BorderColor);

	const bool okHovered = layout.ok.contains(MousePosition);
	FillRectangle(out, layout.ok, okHovered ? RowHoverColor : RowColor);
	DrawFrame(out, layout.ok, okHovered ? HoverColor : InnerBorderColor);
	DrawString(out, _("OK"), layout.ok, { .flags = UiFlags::ColorWhite | UiFlags::AlignCenter | UiFlags::VerticalCenter });
}

} // namespace devilution
