#include "panels/passive_tree_panel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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
#include "tables/playerdat.hpp"
#include "utils/format.hpp"
#include "utils/language.h"
#include "utils/sdl_compat.h"

namespace devilution {

namespace {

constexpr int Padding = 12;
constexpr int HeaderHeight = 36;
constexpr int FooterHeight = 34;

/** Tree units from the centre to the edge of the view at zoom 1: the seal and the class names around it. */
constexpr float TreeExtent = 410;
constexpr float SealRadius = 372;
constexpr float HexagramRadius = 360;
constexpr float SanctumRadius = 82;
constexpr float ClassNameRadius = 392;

constexpr float MinZoom = 1.0F;
constexpr float MaxZoom = 4.0F;
constexpr float OpenZoom = 1.75F;
constexpr float ZoomStep = 1.25F;
/** Screen pixels the view moves per arrow key press. */
constexpr int PanStep = 48;
/** Mouse travel before a press on empty space counts as a drag. */
constexpr int DragThreshold = 3;

constexpr uint8_t PanelColor = PAL16_GRAY + 15;
constexpr uint8_t BorderColor = PAL16_YELLOW + 8;
constexpr uint8_t ViewColor = PAL16_RED + 15;
constexpr uint8_t SealColor = PAL16_RED + 11;
constexpr uint8_t HexagramColor = PAL16_RED + 12;
constexpr uint8_t SanctumColor = PAL16_GRAY + 14;
constexpr uint8_t SanctumRimColor = PAL16_YELLOW + 6;
constexpr uint8_t LinkColor = PAL16_GRAY + 11;
constexpr uint8_t LinkReachableColor = PAL16_GRAY + 6;
constexpr uint8_t LinkAllocatedColor = PAL16_YELLOW + 4;
constexpr uint8_t NodeColor = PAL16_GRAY + 9;
constexpr uint8_t NodeReachableColor = PAL16_GRAY + 2;
constexpr uint8_t NodeAllocatedColor = PAL16_YELLOW + 2;
constexpr uint8_t NotableRimColor = PAL16_ORANGE + 4;
constexpr uint8_t ShrineRimColor = PAL16_BLUE + 4;
constexpr uint8_t StartColor = PAL16_RED + 6;
constexpr uint8_t OtherStartColor = PAL16_RED + 11;
constexpr uint8_t HoverColor = PAL16_BEIGE;
constexpr uint8_t TooltipColor = PAL16_GRAY + 14;

bool IsOpen;
/** @brief How far the view is zoomed in, and the tree point at its centre. */
float Zoom = OpenZoom;
float ViewX;
float ViewY;
/** @brief A press on empty space that may become a drag of the view. */
bool Pressed;
bool Dragging;
Point DragStart;
float DragViewX;
float DragViewY;

struct Layout {
	Rectangle panel;
	Rectangle view;
	Rectangle footer;
};

Layout ComputeLayout(const Surface &out)
{
	Layout layout;
	const int availableHeight = out.h() - GetMainPanel().size.height;
	const int height = std::max(availableHeight - 16, 240);
	const int width = std::min(out.w() - 32, 1200);
	layout.panel = { { (out.w() - width) / 2, std::max((availableHeight - height) / 2, 4) }, { width, height } };
	layout.view = { layout.panel.position + Displacement { Padding, HeaderHeight }, { width - (2 * Padding), height - HeaderHeight - FooterHeight } };
	layout.footer = { { layout.panel.position.x + Padding, layout.panel.position.y + height - FooterHeight }, { width - (2 * Padding), FooterHeight - 2 } };
	return layout;
}

Layout CurrentLayout()
{
	return ComputeLayout(GlobalBackBuffer());
}

/** @brief Screen pixels per tree unit. */
float Scale(const Layout &layout)
{
	return static_cast<float>(std::min(layout.view.size.width, layout.view.size.height)) / 2.0F / TreeExtent * Zoom;
}

/** @brief Where a tree point is inside the view. */
Point ViewPoint(const Layout &layout, float x, float y)
{
	const float scale = Scale(layout);
	return { static_cast<int>(std::lround((layout.view.size.width / 2.0F) + ((x - ViewX) * scale))),
		static_cast<int>(std::lround((layout.view.size.height / 2.0F) + ((y - ViewY) * scale))) };
}

Point NodeViewPoint(const Layout &layout, const PassiveNode &node)
{
	return ViewPoint(layout, static_cast<float>(node.x), static_cast<float>(node.y));
}

void ClampView()
{
	ViewX = std::clamp(ViewX, -TreeExtent, TreeExtent);
	ViewY = std::clamp(ViewY, -TreeExtent, TreeExtent);
}

/** @brief Opens on the player's start, pulled towards the sanctum so the ascendancies are in sight. */
void CenterOnStart()
{
	Zoom = OpenZoom;
	ViewX = 0;
	ViewY = 0;
	const std::optional<PassiveNodeId> start = PassiveStartNode(MyPlayer->_pClass);
	if (start) {
		const PassiveNode &node = PassiveTree()[*start];
		ViewX = node.x * 0.6F;
		ViewY = node.y * 0.6F;
	}
}

void Pan(const Layout &layout, int dx, int dy)
{
	const float scale = Scale(layout);
	ViewX += dx / scale;
	ViewY += dy / scale;
	ClampView();
}

/** @brief Zooms while keeping the tree point under the anchor (a screen point) where it is. */
void ZoomAt(const Layout &layout, float factor, Point anchor)
{
	const float oldScale = Scale(layout);
	const Point center = layout.view.Center();
	const float anchorX = ViewX + ((anchor.x - center.x) / oldScale);
	const float anchorY = ViewY + ((anchor.y - center.y) / oldScale);
	Zoom = std::clamp(Zoom * factor, MinZoom, MaxZoom);
	const float newScale = Scale(layout);
	ViewX = anchorX - ((anchor.x - center.x) / newScale);
	ViewY = anchorY - ((anchor.y - center.y) / newScale);
	ClampView();
}

bool IsNodeShown(const Player &player, const PassiveNode &node)
{
	// The sanctum only holds the ascendancies of the player's own class.
	return !IsAscendancyNode(node) || node.startClass == player._pClass;
}

/** @brief Node radius in tree units. */
float NodeSize(const PassiveNode &node)
{
	switch (node.kind) {
	case PassiveKind::Start:
		return 13;
	case PassiveKind::Shrine:
		return 11;
	case PassiveKind::Ascendancy:
		return 10;
	case PassiveKind::Notable:
		return 9;
	case PassiveKind::AscendancyNotable:
		return 8;
	default:
		return 6;
	}
}

int NodeRadius(const Layout &layout, const PassiveNode &node)
{
	return std::max(static_cast<int>(std::lround(NodeSize(node) * Scale(layout))), 2);
}

std::optional<PassiveNodeId> NodeAt(const Layout &layout, Point position)
{
	if (!layout.view.contains(position))
		return std::nullopt;
	const Point local = position - Displacement { layout.view.position.x, layout.view.position.y };
	std::optional<PassiveNodeId> closest;
	int closestDistance = 0;
	for (const PassiveNode &node : PassiveTree()) {
		if (!IsNodeShown(*MyPlayer, node))
			continue;
		const Point center = NodeViewPoint(layout, node);
		const int radius = NodeRadius(layout, node) + 2;
		const int dx = local.x - center.x;
		const int dy = local.y - center.y;
		const int distance = (dx * dx) + (dy * dy);
		if (distance <= radius * radius && (!closest || distance < closestDistance)) {
			closest = node.id;
			closestDistance = distance;
		}
	}
	return closest;
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

bool IsFarOutside(const Surface &out, Point p)
{
	constexpr int Margin = 4096;
	return p.x < -Margin || p.y < -Margin || p.x > out.w() + Margin || p.y > out.h() + Margin;
}

void DrawLine(const Surface &out, Point from, Point to, uint8_t color, int thickness)
{
	if (IsFarOutside(out, from) || IsFarOutside(out, to))
		return;
	// Both ends off the same side: nothing to draw.
	if ((from.x < 0 && to.x < 0) || (from.y < 0 && to.y < 0) || (from.x >= out.w() && to.x >= out.w()) || (from.y >= out.h() && to.y >= out.h()))
		return;
	const int dx = std::abs(to.x - from.x);
	const int dy = -std::abs(to.y - from.y);
	const int sx = from.x < to.x ? 1 : -1;
	const int sy = from.y < to.y ? 1 : -1;
	int error = dx + dy;
	Point p = from;
	while (true) {
		for (int t = 0; t < thickness; t++) {
			// Thicken across the line's main direction.
			out.SetPixel(dx >= -dy ? p + Displacement { 0, t - (thickness / 2) } : p + Displacement { t - (thickness / 2), 0 }, color);
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

void FillDiamond(const Surface &out, Point center, int radius, uint8_t color)
{
	for (int y = -radius; y <= radius; y++) {
		const int width = radius - std::abs(y);
		DrawHorizontalLine(out, center + Displacement { -width, y }, (2 * width) + 1, color);
	}
}

void DrawRing(const Surface &out, const Layout &layout, float radius, uint8_t color, int thickness)
{
	constexpr int Segments = 120;
	Point previous = ViewPoint(layout, radius, 0);
	for (int i = 1; i <= Segments; i++) {
		const float angle = static_cast<float>(i) * 2.0F * 3.14159265F / Segments;
		const Point next = ViewPoint(layout, radius * std::cos(angle), radius * std::sin(angle));
		DrawLine(out, previous, next, color, thickness);
		previous = next;
	}
}

Point SealPoint(const Layout &layout, float radius, float degrees)
{
	const float angle = degrees * 3.14159265F / 180.0F;
	return ViewPoint(layout, radius * std::cos(angle), radius * std::sin(angle));
}

/** @brief The Horadric seal behind the tree: its ring, a hexagram joining the class starts, and the sanctum. */
void DrawSeal(const Surface &out, const Layout &layout)
{
	const float scale = Scale(layout);
	const int thickness = std::max(static_cast<int>(std::lround(scale * 2)), 1);
	DrawRing(out, layout, SealRadius, SealColor, thickness);
	for (const std::array<float, 3> &triangle : { std::array<float, 3> { -90, 30, 150 }, std::array<float, 3> { -30, 90, 210 } }) {
		for (size_t i = 0; i < triangle.size(); i++) {
			DrawLine(out, SealPoint(layout, HexagramRadius, triangle[i]), SealPoint(layout, HexagramRadius, triangle[(i + 1) % triangle.size()]), HexagramColor, thickness);
		}
	}
	FillCircle(out, ViewPoint(layout, 0, 0), static_cast<int>(std::lround(SanctumRadius * scale)), SanctumColor);
	DrawRing(out, layout, SanctumRadius, SanctumRimColor, thickness);
}

void DrawClassNames(const Surface &out, const Layout &layout, const Player &player)
{
	for (const PassiveNode &node : PassiveTree()) {
		if (node.kind != PassiveKind::Start || !node.startClass)
			continue;
		const float length = std::hypot(static_cast<float>(node.x), static_cast<float>(node.y));
		if (length <= 0)
			continue;
		const Point center = ViewPoint(layout, node.x * ClassNameRadius / length, node.y * ClassNameRadius / length);
		const Rectangle label { center + Displacement { -80, -9 }, { 160, 18 } };
		const bool mine = *node.startClass == player._pClass;
		DrawString(out, GetPlayerDataForClass(*node.startClass).className, label,
		    { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | (mine ? UiFlags::ColorUiGold : UiFlags::ColorUiSilverDark) });
	}
}

uint8_t NodeFillColor(const Player &player, const PassiveNode &node)
{
	if (node.kind == PassiveKind::Start)
		return node.startClass == player._pClass ? StartColor : OtherStartColor;
	if (IsPassiveAllocated(player, node.id))
		return NodeAllocatedColor;
	if (CanAllocatePassive(player, node.id))
		return NodeReachableColor;
	return NodeColor;
}

void DrawNodes(const Surface &out, const Layout &layout, const Player &player, std::optional<PassiveNodeId> hovered)
{
	const std::vector<PassiveNode> &tree = PassiveTree();
	const float scale = Scale(layout);
	const int thickness = std::max(static_cast<int>(std::lround(scale * 1.5F)), 1);
	for (const PassiveNode &node : tree) {
		if (!IsNodeShown(player, node))
			continue;
		for (const PassiveNodeId link : node.links) {
			if (link < node.id || !IsNodeShown(player, tree[link]))
				continue; // Each link is drawn once.
			const bool from = IsPassiveAllocated(player, node.id);
			const bool to = IsPassiveAllocated(player, link);
			const uint8_t color = from && to ? LinkAllocatedColor : (from || to ? LinkReachableColor : LinkColor);
			DrawLine(out, NodeViewPoint(layout, node), NodeViewPoint(layout, tree[link]), color, from && to ? thickness + 1 : thickness);
		}
	}

	for (const PassiveNode &node : tree) {
		if (!IsNodeShown(player, node))
			continue;
		const Point center = NodeViewPoint(layout, node);
		const int radius = NodeRadius(layout, node);
		if (center.x < -radius - 2 || center.y < -radius - 2 || center.x > out.w() + radius + 2 || center.y > out.h() + radius + 2)
			continue;
		const uint8_t color = NodeFillColor(player, node);
		const bool diamond = node.kind == PassiveKind::Shrine;
		const auto fill = [&](int size, uint8_t fillColor) {
			if (diamond)
				FillDiamond(out, center, size, fillColor);
			else
				FillCircle(out, center, size, fillColor);
		};
		if (hovered == node.id)
			fill(radius + 2, HoverColor);
		else if (node.kind == PassiveKind::Shrine)
			fill(radius + 1, ShrineRimColor);
		else if (node.kind != PassiveKind::Small && node.kind != PassiveKind::Start)
			fill(radius + 1, NotableRimColor);
		fill(radius, color);
		// Ascendancy names, once the sanctum is big enough to hold them.
		if (node.kind == PassiveKind::Ascendancy && scale >= 1.2F) {
			const Rectangle label { center + Displacement { -80, radius + 2 }, { 160, 18 } };
			DrawString(out, node.name, label, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorUiGold });
		}
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
	else if (node.kind == PassiveKind::Ascendancy && !IsPassiveAllocated(player, node.id) && !CanAllocatePassive(player, node.id))
		hint = ChosenAscendancy(player) ? _("You already chose an ascendancy") : FormatRuntime(_("Reach level {:d} to ascend"), AscendancyLevel);
	else if (node.kind == PassiveKind::Ascendancy && !IsPassiveAllocated(player, node.id))
		hint = _("Click to ascend (free; refund to change)");
	else if (IsPassiveAllocated(player, node.id))
		hint = CanRefundPassive(player, node.id) ? _("Right-click to refund") : _("Other nodes depend on this one");
	else if (CanAllocatePassive(player, node.id))
		hint = _("Click to allocate");
	else
		hint = (node.kind == PassiveKind::AscendancyNotable ? AscendancyPointsLeft(player) : PassivePointsLeft(player)) <= 0 ? _("No points left") : _("Not linked to your tree yet");

	constexpr int LineHeight = 16;
	const int width = 250;
	const bool hasKindLine = node.kind == PassiveKind::Shrine || IsAscendancyNode(node);
	const int height = 30 + (static_cast<int>(lines.size() + (hasKindLine ? 1 : 0)) * LineHeight) + LineHeight;
	Point position = MousePosition + Displacement { 16, 8 };
	position.x = std::min(position.x, layout.panel.position.x + layout.panel.size.width - width - 4);
	position.y = std::min(position.y, layout.panel.position.y + layout.panel.size.height - height - 4);
	const Rectangle box { position, { width, height } };
	FillRectangle(out, box, TooltipColor);
	DrawFrame(out, box, node.kind == PassiveKind::Small ? BorderColor : (node.kind == PassiveKind::Shrine ? ShrineRimColor : NotableRimColor));

	Rectangle line { position + Displacement { 8, 6 }, { width - 16, 20 } };
	DrawString(out, PassiveNodeName(node), line, { .flags = (node.kind == PassiveKind::Small ? UiFlags::ColorWhite : UiFlags::ColorUiGold) | UiFlags::VerticalCenter });
	line.position.y += 22;
	line.size.height = LineHeight;
	if (hasKindLine) {
		DrawString(out, node.kind == PassiveKind::Shrine ? _("Shrine: a boon with a price") : _("Ascendancy"), line, { .flags = UiFlags::ColorWhitegold | UiFlags::VerticalCenter });
		line.position.y += LineHeight;
	}
	for (const auto &[stat, value] : node.stats) {
		DrawString(out, PassiveStatText(stat, value), line, { .flags = (value < 0 ? UiFlags::ColorRed : UiFlags::ColorBlue) | UiFlags::VerticalCenter });
		line.position.y += LineHeight;
	}
	DrawString(out, hint, line, { .flags = UiFlags::ColorButtonpushed | UiFlags::VerticalCenter });
}

std::string PointsText(const Player &player)
{
	const int left = PassivePointsLeft(player);
	if (AscendancyPointsTotal(player) > 0)
		return FormatRuntime(_("{:d} passive points, {:d} ascendancy points left"), left, AscendancyPointsLeft(player));
	return FormatRuntime(_("{:d} points left ({:d} of {:d} spent)"), left, PassivePointsSpent(player), PassivePointsTotal(player));
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
	Pressed = false;
	Dragging = false;
	CenterOnStart();
	ClearWasdMovementKeys();
}

void ClosePassiveTree()
{
	IsOpen = false;
	Pressed = false;
	Dragging = false;
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
	const Layout layout = CurrentLayout();
	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
		switch (SDLC_EventKey(event)) {
		case SDLK_ESCAPE:
		case SDLK_P:
			ClosePassiveTree();
			break;
		case SDLK_LEFT:
		case SDLK_A:
			Pan(layout, -PanStep, 0);
			break;
		case SDLK_RIGHT:
		case SDLK_D:
			Pan(layout, PanStep, 0);
			break;
		case SDLK_UP:
		case SDLK_W:
			Pan(layout, 0, -PanStep);
			break;
		case SDLK_DOWN:
		case SDLK_S:
			Pan(layout, 0, PanStep);
			break;
		case SDLK_EQUALS:
		case SDLK_PLUS:
		case SDLK_KP_PLUS:
			ZoomAt(layout, ZoomStep, layout.view.Center());
			break;
		case SDLK_MINUS:
		case SDLK_KP_MINUS:
			ZoomAt(layout, 1 / ZoomStep, layout.view.Center());
			break;
		case SDLK_HOME:
			CenterOnStart();
			break;
		default:
			break;
		}
		return true;
	case SDL_EVENT_KEY_UP:
		return true;
#ifndef USE_SDL1
	case SDL_EVENT_MOUSE_WHEEL: {
		const int steps = SDLC_EventWheelIntY(event);
		if (steps != 0)
			ZoomAt(layout, steps > 0 ? ZoomStep : 1 / ZoomStep, layout.view.contains(MousePosition) ? MousePosition : layout.view.Center());
		return true;
	}
#endif
	case SDL_EVENT_MOUSE_BUTTON_DOWN: {
#ifdef USE_SDL1
		// SDL1 reports the wheel as buttons.
		if (event.button.button == SDL_BUTTON_WHEELUP || event.button.button == SDL_BUTTON_WHEELDOWN) {
			ZoomAt(layout, event.button.button == SDL_BUTTON_WHEELUP ? ZoomStep : 1 / ZoomStep, layout.view.contains(MousePosition) ? MousePosition : layout.view.Center());
			return true;
		}
#endif
		const std::optional<PassiveNodeId> node = NodeAt(layout, MousePosition);
		if (event.button.button == SDL_BUTTON_LEFT) {
			if (node) {
				AllocatePassive(player, *node);
			} else if (layout.view.contains(MousePosition)) {
				Pressed = true;
				Dragging = false;
				DragStart = MousePosition;
				DragViewX = ViewX;
				DragViewY = ViewY;
			}
		} else if (node && event.button.button == SDL_BUTTON_RIGHT) {
			RefundPassive(player, *node);
		}
		return true;
	}
	case SDL_EVENT_MOUSE_BUTTON_UP:
		if (event.button.button == SDL_BUTTON_LEFT) {
			Pressed = false;
			Dragging = false;
		}
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

	// The view follows the mouse while it's dragged; mouse motion itself is left to the game.
	if (Pressed) {
		const Displacement moved = MousePosition - DragStart;
		if (std::abs(moved.deltaX) + std::abs(moved.deltaY) > DragThreshold)
			Dragging = true;
		if (Dragging) {
			const float scale = Scale(layout);
			ViewX = DragViewX - (moved.deltaX / scale);
			ViewY = DragViewY - (moved.deltaY / scale);
			ClampView();
		}
	}

	FillRectangle(out, layout.panel, PanelColor);
	DrawFrame(out, layout.panel, BorderColor);

	const Rectangle title { layout.panel.position + Displacement { Padding, 6 }, { layout.panel.size.width - (2 * Padding), 26 } };
	DrawString(out, _("Passive Tree"), title, { .flags = UiFlags::FontSize24 | UiFlags::ColorUiGold | UiFlags::VerticalCenter });
	const bool pointsLeft = PassivePointsLeft(player) > 0 || AscendancyPointsLeft(player) > 0;
	DrawString(out, PointsText(player), title, { .flags = UiFlags::AlignRight | UiFlags::VerticalCenter | (pointsLeft ? UiFlags::ColorWhitegold : UiFlags::ColorWhite) });

	const Surface view = out.subregion(layout.view.position.x, layout.view.position.y, layout.view.size.width, layout.view.size.height);
	FillRect(view, 0, 0, view.w(), view.h(), ViewColor);
	DrawSeal(view, layout);
	DrawClassNames(view, layout, player);
	const std::optional<PassiveNodeId> hovered = Dragging ? std::nullopt : NodeAt(layout, MousePosition);
	DrawNodes(view, layout, player, hovered);
	DrawFrame(out, layout.view, BorderColor);

	const std::string help = WordWrapString(_("Click a linked node to allocate it, right-click to refund. Drag or use the arrow keys to move, the wheel or +/- to zoom, Home to recenter. P or Esc closes."), layout.footer.size.width);
	DrawString(out, help, layout.footer, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorButtonpushed, .lineHeight = 14 });

	if (hovered)
		DrawTooltip(out, layout, player, PassiveTree()[*hovered]);
}

} // namespace devilution
