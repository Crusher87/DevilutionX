#include "panels/atlas_panel.hpp"

#include <algorithm>
#include <array>
#include <string>

#ifdef USE_SDL1
#include "utils/sdl2_to_1_2_backports.h"
#endif

#include "control/control.hpp"
#include "controls/wasd_movement.hpp"
#include "diablo.h"
#include "engine/dx.h"
#include "game_mode.hpp"
#include "panels/poe2_widgets.hpp"
#include "panels/skill_bar.hpp"
#include "player.h"
#include "poe2/endgame.hpp"
#include "utils/format.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/sdl_compat.h"

namespace devilution {

namespace {

using namespace poe2ui;

constexpr int Padding = 14;
constexpr int HeaderHeight = 70;
constexpr int RowHeight = 40;
constexpr int RowGap = 4;
constexpr int Columns = 2;
constexpr int ColumnWidth = 360;
constexpr int FooterHeight = 26;

bool IsOpen;

struct Layout {
	Rectangle panel;
	std::array<Rectangle, AtlasNodeCount> rows;
};

Layout ComputeLayout(const Surface &out)
{
	Layout layout;
	const int rowsPerColumn = static_cast<int>((AtlasNodeCount + Columns - 1) / Columns);
	const int width = std::min((Columns * ColumnWidth) + ((Columns + 1) * Padding), out.w() - 8);
	const int columnWidth = (width - ((Columns + 1) * Padding)) / Columns;
	const int height = HeaderHeight + (rowsPerColumn * (RowHeight + RowGap)) + FooterHeight + Padding;
	const int availableHeight = out.h() - GetMainPanel().size.height;
	layout.panel = { { (out.w() - width) / 2, std::max((availableHeight - height) / 2, 4) }, { width, height } };
	for (size_t i = 0; i < AtlasNodeCount; i++) {
		const int column = static_cast<int>(i) / rowsPerColumn;
		const int row = static_cast<int>(i) % rowsPerColumn;
		layout.rows[i] = { layout.panel.position + Displacement { Padding + (column * (columnWidth + Padding)), HeaderHeight + (row * (RowHeight + RowGap)) }, { columnWidth, RowHeight } };
	}
	return layout;
}

} // namespace

bool IsAtlasOpen()
{
	return IsOpen;
}

void ToggleAtlas()
{
	if (IsOpen) {
		CloseAtlas();
		return;
	}
	if (!gbIsPoe2 || MyPlayer == nullptr)
		return;
	IsOpen = true;
	sgbMouseDown = CLICK_NONE;
	LastPlayerAction = PlayerActionType::None;
	ReleaseAllTraySlots();
	ClearWasdMovementKeys();
}

void CloseAtlas()
{
	if (!IsOpen)
		return;
	IsOpen = false;
	ClearWasdMovementKeys();
}

bool AtlasHandleEvent(const SDL_Event &event)
{
	if (!IsOpen)
		return false;
	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
		if (IsAnyOf(SDLC_EventKey(event), SDLK_ESCAPE, SDLK_U))
			CloseAtlas();
		return true;
	case SDL_EVENT_MOUSE_BUTTON_DOWN: {
		const Layout layout = ComputeLayout(GlobalBackBuffer());
		Player &player = *MyPlayer;
		for (size_t i = 0; i < AtlasNodeCount; i++) {
			if (!layout.rows[i].contains(MousePosition))
				continue;
			// Left-click spends a point, right-click takes it back.
			if (event.button.button == SDL_BUTTON_LEFT)
				AllocateAtlasNode(player, static_cast<AtlasNode>(i));
			else if (event.button.button == SDL_BUTTON_RIGHT)
				RefundAtlasNode(player, static_cast<AtlasNode>(i));
			return true;
		}
		if (!layout.panel.contains(MousePosition))
			CloseAtlas();
		return true;
	}
	case SDL_EVENT_KEY_UP:
	case SDL_EVENT_MOUSE_BUTTON_UP:
#ifndef USE_SDL1
	case SDL_EVENT_MOUSE_WHEEL:
#endif
		return true;
	default:
		return false;
	}
}

void DrawAtlas(const Surface &out)
{
	if (!IsOpen || MyPlayer == nullptr)
		return;
	const Player &player = *MyPlayer;
	const Layout layout = ComputeLayout(out);
	DrawPanel(out, layout.panel);
	const Rectangle title { layout.panel.position + Displacement { Padding, 8 }, { layout.panel.size.width - (2 * Padding), 26 } };
	DrawString(out, _("Atlas Passives"), title, { .flags = UiFlags::FontSize24 | UiFlags::ColorUiGold | UiFlags::AlignCenter | UiFlags::VerticalCenter });
	const int earned = AtlasPointsEarned(player);
	const int spent = AtlasPointsSpent(player);
	const Rectangle points { title.position + Displacement { 0, 30 }, { title.size.width, 20 } };
	DrawString(out, FormatRuntime(_("Points: {:d} of {:d} left   Maps completed: {:d}   Pinnacle bosses: {:d}"), earned - spent, earned, player.poe2.mapsCompleted, player.poe2.atlas.pinnaclesDefeated), points,
	    { .flags = UiFlags::ColorWhite | UiFlags::AlignCenter | UiFlags::VerticalCenter });

	for (size_t i = 0; i < AtlasNodeCount; i++) {
		const auto node = static_cast<AtlasNode>(i);
		const AtlasNodeData &data = GetAtlasNode(node);
		const Rectangle &row = layout.rows[i];
		const int rank = AtlasRank(player, node);
		const bool hovered = row.contains(MousePosition);
		FillRectangle(out, row, hovered ? RowHoverColor : RowColor);
		DrawFrame(out, row, rank > 0 ? HoverColor : InnerBorderColor);
		const bool keystone = data.maxRank == 1;
		const UiFlags nameColor = keystone ? UiFlags::ColorOrange : rank > 0 ? UiFlags::ColorUiGold
		                                                                     : UiFlags::ColorWhite;
		const Rectangle name { row.position + Displacement { 6, 3 }, { row.size.width - 12, 16 } };
		DrawString(out, data.name, name, { .flags = nameColor | UiFlags::VerticalCenter });
		DrawString(out, FormatRuntime("{:d}/{:d}", rank, data.maxRank), name, { .flags = UiFlags::ColorWhite | UiFlags::AlignRight | UiFlags::VerticalCenter });
		const Rectangle effect { name.position + Displacement { 0, 18 }, { name.size.width, 16 } };
		const std::string text = data.tradeoff.empty() ? std::string(data.effect) : FormatRuntime("{:s}. {:s}", data.effect, data.tradeoff);
		DrawString(out, text, effect, { .flags = UiFlags::ColorBlue | UiFlags::FontSize12 | UiFlags::VerticalCenter });
	}
	const Rectangle footer { { layout.panel.position.x + Padding, layout.panel.position.y + layout.panel.size.height - FooterHeight - 4 }, { layout.panel.size.width - (2 * Padding), FooterHeight } };
	DrawString(out, _("Left-click spends a point, right-click takes it back. Each completed map gives a point; pinnacle bosses give two."), footer,
	    { .flags = UiFlags::ColorWhite | UiFlags::FontSize12 | UiFlags::AlignCenter | UiFlags::VerticalCenter });
}

} // namespace devilution
