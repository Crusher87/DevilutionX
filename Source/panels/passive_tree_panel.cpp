#include "panels/passive_tree_panel.hpp"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "control/control.hpp"
#include "controls/wasd_movement.hpp"
#include "engine/dx.h"
#include "engine/palette.h"
#include "engine/point.hpp"
#include "engine/rectangle.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "game_mode.hpp"
#include "multi.h"
#include "player.h"
#include "plrmsg.h"
#include "poe2/passive_tree.hpp"
#include "utils/format.hpp"
#include "utils/language.h"
#include "utils/sdl_compat.h"

namespace devilution {

namespace {

constexpr int Padding = 12;
constexpr int HeaderHeight = 36;
constexpr int FooterHeight = 22;
/** Tree coordinates run from about -100 to 100; this leaves room for the outermost nodes. */
constexpr int TreeExtent = 110;

constexpr uint8_t PanelColor = PAL16_GRAY + 15;
constexpr uint8_t BorderColor = PAL16_YELLOW + 8;
constexpr uint8_t LinkColor = PAL16_GRAY + 11;
constexpr uint8_t LinkReachableColor = PAL16_GRAY + 6;
constexpr uint8_t LinkAllocatedColor = PAL16_YELLOW + 4;
constexpr uint8_t NodeColor = PAL16_GRAY + 9;
constexpr uint8_t NodeReachableColor = PAL16_GRAY + 2;
constexpr uint8_t NodeAllocatedColor = PAL16_YELLOW + 2;
constexpr uint8_t NotableRimColor = PAL16_ORANGE + 4;
constexpr uint8_t StartColor = PAL16_RED + 6;
constexpr uint8_t HoverColor = PAL16_BEIGE;
constexpr uint8_t TooltipColor = PAL16_GRAY + 14;

bool IsOpen;

struct Layout {
	Rectangle panel;
	Rectangle tree;
	Rectangle footer;
};

Layout ComputeLayout(const Surface &out)
{
	Layout layout;
	const int availableHeight = out.h() - GetMainPanel().size.height;
	const int height = std::min(560, availableHeight - 16);
	const int width = std::min(std::max(height, 520), out.w() - 32);
	layout.panel = { { (out.w() - width) / 2, std::max((availableHeight - height) / 2, 4) }, { width, height } };
	const int treeSize = std::min(width - (2 * Padding), height - HeaderHeight - FooterHeight - Padding);
	layout.tree = { { layout.panel.position.x + ((width - treeSize) / 2), layout.panel.position.y + HeaderHeight }, { treeSize, treeSize } };
	layout.footer = { { layout.panel.position.x + Padding, layout.panel.position.y + height - FooterHeight }, { width - (2 * Padding), FooterHeight - 4 } };
	return layout;
}

Layout CurrentLayout()
{
	return ComputeLayout(GlobalBackBuffer());
}

Point NodePosition(const Layout &layout, const PassiveNode &node)
{
	const Point center = layout.tree.Center();
	const int half = layout.tree.size.width / 2;
	return center + Displacement { node.x * half / TreeExtent, node.y * half / TreeExtent };
}

int NodeRadius(const Layout &layout, const PassiveNode &node)
{
	const int base = std::max(layout.tree.size.width / 90, 3);
	switch (node.kind) {
	case PassiveKind::Start:
		return base + 4;
	case PassiveKind::Notable:
		return base + 3;
	default:
		return base;
	}
}

std::optional<uint8_t> NodeAt(const Layout &layout, Point position)
{
	for (const PassiveNode &node : PassiveTree()) {
		const Point center = NodePosition(layout, node);
		const int radius = NodeRadius(layout, node) + 2;
		const int dx = position.x - center.x;
		const int dy = position.y - center.y;
		if (dx * dx + dy * dy <= radius * radius)
			return node.id;
	}
	return std::nullopt;
}

void FillRectangle(const Surface &out, const Rectangle &rect, uint8_t color)
{
	FillRect(out, rect.position.x, rect.position.y, rect.size.width, rect.size.height, color);
}

void DrawFrame(const Surface &out, const Rectangle &rect, uint8_t color)
{
	DrawHorizontalLine(out, rect.position, rect.size.width, color);
	DrawHorizontalLine(out, rect.position + Displacement { 0, rect.size.height - 1 }, rect.size.width, color);
	DrawVerticalLine(out, rect.position, rect.size.height, color);
	DrawVerticalLine(out, rect.position + Displacement { rect.size.width - 1, 0 }, rect.size.height, color);
}

void DrawLine(const Surface &out, Point from, Point to, uint8_t color, int thickness)
{
	const int dx = std::abs(to.x - from.x);
	const int dy = -std::abs(to.y - from.y);
	const int sx = from.x < to.x ? 1 : -1;
	const int sy = from.y < to.y ? 1 : -1;
	int error = dx + dy;
	Point p = from;
	while (true) {
		for (int t = 0; t < thickness; t++) {
			// Thicken across the line's main direction.
			out.SetPixel(dx >= -dy ? p + Displacement { 0, t } : p + Displacement { t, 0 }, color);
		}
		if (p == to)
			break;
		const int e2 = 2 * error;
		if (e2 >= dy) {
			error += dy;
			p.x += sx;
		}
		if (e2 <= dx) {
			error += dx;
			p.y += sy;
		}
	}
}

void FillCircle(const Surface &out, Point center, int radius, uint8_t color)
{
	for (int y = -radius; y <= radius; y++) {
		int width = 0;
		while ((width + 1) * (width + 1) + y * y <= radius * radius)
			width++;
		DrawHorizontalLine(out, center + Displacement { -width, y }, (2 * width) + 1, color);
	}
}

void DrawTooltip(const Surface &out, const Layout &layout, const Player &player, const PassiveNode &node)
{
	std::vector<std::string> lines;
	for (const auto &[stat, value] : node.stats)
		lines.push_back(PassiveStatText(stat, value));
	std::string hint;
	if (node.kind == PassiveKind::Start)
		hint = node.startClass == player._pClass ? _("Your starting point") : _("Another class's starting point");
	else if (IsPassiveAllocated(player, node.id))
		hint = CanRefundPassive(player, node.id) ? _("Right-click to refund") : _("Other nodes depend on this one");
	else if (CanAllocatePassive(player, node.id))
		hint = _("Click to allocate");
	else
		hint = PassivePointsLeft(player) <= 0 ? _("No points left") : _("Not linked to your tree yet");

	constexpr int LineHeight = 16;
	const int width = 240;
	const int height = 30 + (static_cast<int>(lines.size()) * LineHeight) + LineHeight;
	Point position = MousePosition + Displacement { 16, 8 };
	position.x = std::min(position.x, layout.panel.position.x + layout.panel.size.width - width - 4);
	position.y = std::min(position.y, layout.panel.position.y + layout.panel.size.height - height - 4);
	const Rectangle box { position, { width, height } };
	FillRectangle(out, box, TooltipColor);
	DrawFrame(out, box, node.kind == PassiveKind::Notable ? NotableRimColor : BorderColor);

	Rectangle line { position + Displacement { 8, 6 }, { width - 16, 20 } };
	DrawString(out, PassiveNodeName(node), line, { .flags = (node.kind == PassiveKind::Small ? UiFlags::ColorWhite : UiFlags::ColorUiGold) | UiFlags::VerticalCenter });
	line.position.y += 22;
	line.size.height = LineHeight;
	for (const std::string &text : lines) {
		DrawString(out, text, line, { .flags = UiFlags::ColorBlue | UiFlags::VerticalCenter });
		line.position.y += LineHeight;
	}
	DrawString(out, hint, line, { .flags = UiFlags::ColorButtonpushed | UiFlags::VerticalCenter });
}

} // namespace

bool IsPassiveTreeOpen()
{
	return IsOpen;
}

void OpenPassiveTree()
{
	if (IsOpen || !gbIsPoe2)
		return;
	if (gbIsMultiplayer) {
		EventPlrMsg(_("The passive tree isn't available in multiplayer yet."), UiFlags::ColorRed);
		return;
	}
	if (PassiveTree().empty()) {
		EventPlrMsg(_("The passive tree couldn't be loaded."), UiFlags::ColorRed);
		return;
	}
	IsOpen = true;
	ClearWasdMovementKeys();
}

void ClosePassiveTree()
{
	IsOpen = false;
}

void TogglePassiveTree()
{
	if (IsOpen)
		ClosePassiveTree();
	else
		OpenPassiveTree();
}

bool PassiveTreeHandleEvent(const SDL_Event &event)
{
	if (!IsOpen)
		return false;

	Player &player = *MyPlayer;
	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
		switch (SDLC_EventKey(event)) {
		case SDLK_ESCAPE:
		case SDLK_P:
			ClosePassiveTree();
			break;
		default:
			break;
		}
		return true;
	case SDL_EVENT_KEY_UP:
		return true;
	case SDL_EVENT_MOUSE_BUTTON_DOWN: {
		const std::optional<uint8_t> node = NodeAt(CurrentLayout(), MousePosition);
		if (node && event.button.button == SDL_BUTTON_LEFT)
			AllocatePassive(player, *node);
		else if (node && event.button.button == SDL_BUTTON_RIGHT)
			RefundPassive(player, *node);
		return true;
	}
	case SDL_EVENT_MOUSE_BUTTON_UP:
		return true;
	default:
		return false;
	}
}

void DrawPassiveTree(const Surface &out)
{
	if (!IsOpen)
		return;
	const Player &player = *MyPlayer;
	const Layout layout = ComputeLayout(out);

	FillRectangle(out, layout.panel, PanelColor);
	DrawFrame(out, layout.panel, BorderColor);

	const Rectangle title { layout.panel.position + Displacement { Padding, 6 }, { layout.panel.size.width - (2 * Padding), 26 } };
	DrawString(out, _("Passive Tree"), title, { .flags = UiFlags::FontSize24 | UiFlags::ColorUiGold | UiFlags::VerticalCenter });
	DrawString(out, FormatRuntime(_("{:d} points left  ({:d} of {:d} spent)"), PassivePointsLeft(player), PassivePointsSpent(player), PassivePointsTotal(player)), title,
	    { .flags = UiFlags::AlignRight | UiFlags::VerticalCenter | (PassivePointsLeft(player) > 0 ? UiFlags::ColorWhitegold : UiFlags::ColorWhite) });

	const std::vector<PassiveNode> &tree = PassiveTree();
	const int thickness = std::max(layout.tree.size.width / 250, 1);
	for (const PassiveNode &node : tree) {
		for (const uint8_t link : node.links) {
			if (link < node.id)
				continue; // Each link is drawn once.
			const bool from = IsPassiveAllocated(player, node.id);
			const bool to = IsPassiveAllocated(player, link);
			const uint8_t color = from && to ? LinkAllocatedColor : (from || to ? LinkReachableColor : LinkColor);
			DrawLine(out, NodePosition(layout, node), NodePosition(layout, tree[link]), color, from && to ? thickness + 1 : thickness);
		}
	}

	const std::optional<uint8_t> hovered = NodeAt(layout, MousePosition);
	for (const PassiveNode &node : tree) {
		const Point center = NodePosition(layout, node);
		const int radius = NodeRadius(layout, node);
		uint8_t color = NodeColor;
		if (node.kind == PassiveKind::Start)
			color = node.startClass == player._pClass ? StartColor : NodeColor;
		else if (IsPassiveAllocated(player, node.id))
			color = NodeAllocatedColor;
		else if (CanAllocatePassive(player, node.id))
			color = NodeReachableColor;

		if (hovered == node.id)
			FillCircle(out, center, radius + 2, HoverColor);
		else if (node.kind == PassiveKind::Notable)
			FillCircle(out, center, radius + 1, NotableRimColor);
		FillCircle(out, center, radius, color);
	}

	DrawString(out, _("Click a linked node to allocate it, right-click to refund. Each level gives a point. P or Esc closes."), layout.footer,
	    { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorButtonpushed });

	if (hovered)
		DrawTooltip(out, layout, player, tree[*hovered]);
}

} // namespace devilution
