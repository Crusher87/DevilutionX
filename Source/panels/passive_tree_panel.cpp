#include "panels/passive_tree_panel.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "control/control.hpp"
#include "controls/wasd_movement.hpp"
#include "cursor.h"
#include "engine/dx.h"
#include "engine/palette.h"
#include "engine/point.hpp"
#include "engine/rectangle.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "multi.h"
#include "player.h"
#include "plrmsg.h"
#include "poe2/jewels.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/weapon_swap.hpp"
#include "sound_effect_enums.h"
#include "tables/playerdat.hpp"
#include "utils/format.hpp"
#include "utils/language.h"
#include "utils/sdl_compat.h"

namespace devilution {

namespace {

/** @brief Rounds to the nearest whole number; the Amiga toolchain has no std::lround. */
int RoundToInt(float value)
{
	return static_cast<int>(value < 0 ? value - 0.5F : value + 0.5F);
}

constexpr int Padding = 12;
constexpr int HeaderHeight = 36;
constexpr int FooterHeight = 34;

/** Tree units from the centre to the edge of the view at zoom 1: the seal and the class names around it. */
constexpr float TreeExtent = 462;
constexpr float SealRadius = 436;
constexpr float SanctumRadius = 84;
constexpr float ClassNameRadius = 450;

constexpr float MinZoom = 1.0F;
constexpr float MaxZoom = 4.0F;
constexpr float OpenZoom = 1.75F;
constexpr float ZoomStep = 1.25F;
/** Screen pixels the view moves per arrow key press. */
constexpr int PanStep = 48;
/** Mouse travel before a press on empty space counts as a drag. */
constexpr int DragThreshold = 3;

constexpr uint8_t PanelColor = 0; // black: palette entry 255 is white
constexpr uint8_t BorderColor = PAL16_YELLOW + 8;
constexpr uint8_t ViewColor = PAL16_RED + 15;
constexpr uint8_t SealColor = PAL16_RED + 11;
constexpr uint8_t SanctumColor = PAL16_GRAY + 14;
constexpr uint8_t SanctumRimColor = PAL16_YELLOW + 6;
constexpr uint8_t LinkColor = PAL16_GRAY + 11;
constexpr uint8_t LinkReachableColor = PAL16_GRAY + 6;
constexpr uint8_t LinkAllocatedColor = PAL16_YELLOW + 4;
constexpr uint8_t NodeColor = PAL16_GRAY + 9;
constexpr uint8_t NodeReachableColor = PAL16_GRAY + 2;
constexpr uint8_t NodeAllocatedColor = PAL16_YELLOW + 2;
constexpr uint8_t NotableRimColor = PAL16_ORANGE + 4;
constexpr uint8_t NotableInnerRimColor = PAL16_GRAY + 15;
constexpr uint8_t ShrineRimColor = PAL16_BLUE + 4;
constexpr uint8_t AttributeRimColor = PAL16_BEIGE + 6;
constexpr uint8_t SocketRimColor = PAL16_GRAY + 4;
constexpr uint8_t SocketHoleColor = 0;
constexpr uint8_t SocketGemColor = PAL16_BLUE + 6;
constexpr uint8_t TitleBarColor = PAL16_GRAY + 13;
constexpr uint8_t StartColor = PAL16_RED + 6;
constexpr uint8_t OtherStartColor = PAL16_RED + 11;
constexpr uint8_t HoverColor = PAL16_BEIGE;
constexpr uint8_t TooltipColor = PAL16_GRAY + 14;

bool IsOpen;
/** @brief Which points clicks spend: the main tree's or a weapon set's. */
PassivePool Pool = PassivePool::Main;
/** @brief An "any Attribute" node waiting for the player to pick its attribute. */
std::optional<PassiveNodeId> PendingAttributeNode;
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
	return { RoundToInt((layout.view.size.width / 2.0F) + ((x - ViewX) * scale)),
		RoundToInt((layout.view.size.height / 2.0F) + ((y - ViewY) * scale)) };
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
	return IsPassiveNodeVisible(player, node);
}

/** @brief Node radius in tree units. */
/** @brief The palette entry closest to the colour; Diablo's fixed ramps have no green, so Emeralds look it up. */
uint8_t NearestPaletteColor(int red, int green, int blue)
{
	uint8_t best = 0;
	int bestDistance = INT_MAX;
	for (size_t i = 0; i < logical_palette.size(); i++) {
		const SDL_Color &color = logical_palette[i];
		const int dr = color.r - red;
		const int dg = color.g - green;
		const int db = color.b - blue;
		const int distance = (dr * dr) + (dg * dg) + (db * db);
		if (distance < bestDistance) {
			bestDistance = distance;
			best = static_cast<uint8_t>(i);
		}
	}
	return best;
}

/** @brief The colour a socketed jewel glints with. */
uint8_t JewelColor(const Item &jewel)
{
	switch (ItemModSlot(jewel).value_or(ModSlot::Jewel)) {
	case ModSlot::Ruby:
		return PAL16_RED + 4;
	case ModSlot::Emerald:
		return NearestPaletteColor(40, 170, 60);
	default:
		return PAL16_BLUE + 4;
	}
}

float NodeSize(const PassiveNode &node)
{
	switch (node.kind) {
	case PassiveKind::Start:
		return 13;
	case PassiveKind::Shrine:
		return 11;
	case PassiveKind::Ascendancy:
		return 11;
	case PassiveKind::Notable:
		return 10;
	case PassiveKind::JewelSocket:
		return 9;
	case PassiveKind::AscendancyNotable:
		return 8;
	default:
		return 5;
	}
}

int NodeRadius(const Layout &layout, const PassiveNode &node)
{
	return std::max(RoundToInt(NodeSize(node) * Scale(layout)), 2);
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

/** @brief The seal behind the tree: its ring and the sanctum in the middle, which says when the ascendancy unlocks. */
void DrawSeal(const Surface &out, const Layout &layout, const Player &player)
{
	const float scale = Scale(layout);
	const int thickness = std::max(RoundToInt(scale * 2), 1);
	DrawRing(out, layout, SealRadius, SealColor, thickness);
	FillCircle(out, ViewPoint(layout, 0, 0), RoundToInt(SanctumRadius * scale), SanctumColor);
	DrawRing(out, layout, SanctumRadius, SanctumRimColor, thickness);
	if (player.getCharacterLevel() < AscendancyLevel && scale >= 0.9F) {
		const Point center = ViewPoint(layout, 0, 0);
		const Rectangle label { center + Displacement { -RoundToInt(SanctumRadius * scale), -20 }, { 2 * RoundToInt(SanctumRadius * scale), 40 } };
		const std::string text = WordWrapString(FormatRuntime(_("Ascendancy unlocks at level {:d}"), AscendancyLevel), label.size.width - 8);
		DrawString(out, text, label, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorUiSilverDark, .lineHeight = 14 });
	}
}

void DrawClassNames(const Surface &out, const Layout &layout, const Player &player)
{
	for (const PassiveNode &node : PassiveTree()) {
		if (node.kind != PassiveKind::Start || !node.startClass)
			continue;
		const float length = std::sqrt((static_cast<float>(node.x) * static_cast<float>(node.x)) + (static_cast<float>(node.y) * static_cast<float>(node.y)));
		if (length <= 0)
			continue;
		const Point center = ViewPoint(layout, node.x * ClassNameRadius / length, node.y * ClassNameRadius / length);
		const Rectangle label { center + Displacement { -80, -9 }, { 160, 18 } };
		const bool mine = *node.startClass == PassiveTreeClass(player._pClass);
		DrawString(out, GetPlayerDataForClass(*node.startClass).className, label,
		    { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | (mine ? UiFlags::ColorUiGold : UiFlags::ColorUiSilverDark) });
	}
}

/** @brief Weapon set nodes are red for set I and green for set II, like POE2. */
uint8_t SetColor(PassivePool pool)
{
	static const uint8_t SetIIColor = NearestPaletteColor(70, 190, 80);
	return pool == PassivePool::SetI ? PAL16_RED + 3 : SetIIColor;
}

/** @brief The pool's tree as the panel shows it: the main view shows both sets' nodes too, so nothing is hidden. */
bool IsShownAllocated(const Player &player, PassiveNodeId node)
{
	if (Pool != PassivePool::Main)
		return IsPassiveAllocatedIn(player, node, Pool);
	return IsPassiveAllocatedIn(player, node, PassivePool::Main) || IsPassiveInPool(player, node, PassivePool::SetI) || IsPassiveInPool(player, node, PassivePool::SetII);
}

uint8_t NodeFillColor(const Player &player, const PassiveNode &node)
{
	if (node.kind == PassiveKind::Start)
		return node.startClass == PassiveTreeClass(player._pClass) ? StartColor : OtherStartColor;
	if (IsPassiveInPool(player, node.id, PassivePool::Main))
		return NodeAllocatedColor;
	for (const PassivePool set : { PassivePool::SetI, PassivePool::SetII }) {
		if ((Pool == PassivePool::Main || Pool == set) && IsPassiveInPool(player, node.id, set))
			return SetColor(set);
	}
	if (CanAllocatePassive(player, node.id, Pool))
		return NodeReachableColor;
	return NodeColor;
}

void DrawNodes(const Surface &out, const Layout &layout, const Player &player, std::optional<PassiveNodeId> hovered)
{
	const std::vector<PassiveNode> &tree = PassiveTree();
	const float scale = Scale(layout);
	const int thickness = std::max(RoundToInt(scale * 1.5F), 1);
	for (const PassiveNode &node : tree) {
		if (!IsNodeShown(player, node))
			continue;
		for (const PassiveNodeId link : node.links) {
			if (link < node.id || !IsNodeShown(player, tree[link]))
				continue; // Each link is drawn once.
			const bool from = IsShownAllocated(player, node.id);
			const bool to = IsShownAllocated(player, link);
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
		const int rim = std::max(RoundToInt(scale * 1.5F), 1);
		if (hovered == node.id)
			fill(radius + rim + 1, HoverColor);
		switch (node.kind) {
		case PassiveKind::Shrine:
			fill(radius + rim, ShrineRimColor);
			fill(radius, color);
			break;
		case PassiveKind::Notable:
		case PassiveKind::Ascendancy:
		case PassiveKind::AscendancyNotable:
			// Notables wear a gold frame round a dark band, so they stand out from the small nodes.
			fill(radius + rim, NotableRimColor);
			fill(radius, NotableInnerRimColor);
			fill(std::max(radius - rim, 1), color);
			break;
		case PassiveKind::JewelSocket:
			// An empty socket: a rim round a dark hole, with a gem glinting in it once allocated.
			fill(radius + rim, color);
			fill(radius, SocketRimColor);
			if (const Item *jewel = SocketedJewel(player, node.id); jewel != nullptr)
				fill(std::max(radius - (2 * rim), 1), JewelColor(*jewel));
			else
				fill(std::max(radius - (2 * rim), 1), IsPassiveAllocated(player, node.id) ? SocketGemColor : SocketHoleColor);
			break;
		case PassiveKind::Attribute:
			fill(radius + rim, AttributeRimColor);
			fill(radius, color);
			break;
		default:
			fill(radius, color);
			break;
		}
		// Ascendancy names, once the sanctum is big enough to hold them.
		if (node.kind == PassiveKind::Ascendancy && scale >= 1.2F) {
			const Rectangle label { center + Displacement { -80, radius + 2 }, { 160, 18 } };
			DrawString(out, node.name, label, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorUiGold });
		}
	}
}

/** @brief Puts the held jewel in the socket, picking up the jewel that was there. */
void PlaceHeldJewel(Player &player, PassiveNodeId node)
{
	if (!IsPassiveAllocated(player, node)) {
		EventPlrMsg(_("Allocate the Jewel Socket first."), UiFlags::ColorRed);
		return;
	}
	const int cursor = player.HoldItem._iCurs;
	if (!SocketJewel(player, node, player.HoldItem))
		return;
	PlaySFX(ItemInvSnds[ItemCAnimTbl[cursor]]);
	if (player.HoldItem.isEmpty())
		NewCursor(CURSOR_HAND);
	else
		NewCursor(player.HoldItem);
}

/** @brief Takes the jewel out of the socket into the inventory, or the hand when the inventory is full. */
void TakeOutJewel(Player &player, PassiveNodeId node)
{
	const Item *socketed = SocketedJewel(player, node);
	if (socketed == nullptr)
		return;
	if (!CanFitItemInInventory(player, *socketed) && !player.HoldItem.isEmpty()) {
		EventPlrMsg(_("No room in your inventory for the jewel."), UiFlags::ColorRed);
		return;
	}
	std::optional<Item> jewel = TakeJewel(player, node);
	if (!jewel)
		return;
	PlaySFX(ItemInvSnds[ItemCAnimTbl[jewel->_iCurs]]);
	if (AutoPlaceItemInInventory(player, *jewel))
		return;
	player.HoldItem = *jewel;
	NewCursor(player.HoldItem);
}

std::string_view NodeKindText(const PassiveNode &node)
{
	switch (node.kind) {
	case PassiveKind::Notable:
		return _("Notable Passive");
	case PassiveKind::Shrine:
		return _("Shrine: a boon with a price");
	case PassiveKind::Ascendancy:
		return _("Ascendancy");
	case PassiveKind::AscendancyNotable:
		return _("Ascendancy Passive");
	case PassiveKind::Attribute:
		return _("Pick the attribute when you allocate it");
	case PassiveKind::JewelSocket:
		return _("Jewel Socket");
	case PassiveKind::Start:
		return _("Class start");
	default:
		return _("Passive");
	}
}

uint8_t NodeFrameColor(const PassiveNode &node)
{
	switch (node.kind) {
	case PassiveKind::Small:
		return BorderColor;
	case PassiveKind::Attribute:
		return AttributeRimColor;
	case PassiveKind::Shrine:
		return ShrineRimColor;
	case PassiveKind::JewelSocket:
		return SocketGemColor;
	default:
		return NotableRimColor;
	}
}

std::string NodeHint(const Player &player, const PassiveNode &node)
{
	if (node.kind == PassiveKind::Start)
		return node.startClass == PassiveTreeClass(player._pClass) ? std::string(_("Your starting point")) : std::string(_("Another class's starting point"));
	if (node.kind == PassiveKind::Ascendancy && !IsPassiveAllocated(player, node.id) && !CanAllocatePassive(player, node.id))
		return ChosenAscendancy(player) ? std::string(_("You already chose an ascendancy")) : FormatRuntime(_("Reach level {:d} to ascend"), AscendancyLevel);
	if (node.kind == PassiveKind::Ascendancy && !IsPassiveAllocated(player, node.id))
		return std::string(_("Click to ascend (free; refund to change)"));
	if (node.kind == PassiveKind::JewelSocket && IsPassiveAllocated(player, node.id)) {
		if (SocketedJewel(player, node.id) != nullptr)
			return std::string(_("Right-click to take the jewel out"));
		if (IsJewel(player.HoldItem))
			return std::string(_("Click to socket the jewel"));
		if (CanRefundPassive(player, node.id))
			return std::string(_("Click with a jewel to socket it; right-click to refund"));
	}
	if (Pool != PassivePool::Main) {
		if (IsPassiveInPool(player, node.id, PassivePool::Main))
			return std::string(_("Allocated in the main tree"));
		if (IsPassiveInPool(player, node.id, Pool))
			return CanRefundPassive(player, node.id, Pool) ? std::string(_("Right-click to refund")) : std::string(_("Other nodes depend on this one"));
		if (!CanTakeWeaponSetPoints(node))
			return std::string(_("Weapon set points can't go here"));
		if (CanAllocatePassive(player, node.id, Pool))
			return node.kind == PassiveKind::Attribute ? std::string(_("Click to pick an attribute")) : std::string(_("Click to allocate with a weapon set point"));
		return WeaponSetPointsLeft(player, Pool) <= 0 ? std::string(_("No weapon set points left")) : std::string(_("Not linked to this set's tree yet"));
	}
	if (IsPassiveInPool(player, node.id, PassivePool::Main))
		return CanRefundPassive(player, node.id) ? std::string(_("Right-click to refund")) : std::string(_("Other nodes depend on this one"));
	for (const PassivePool set : { PassivePool::SetI, PassivePool::SetII }) {
		if (IsPassiveInPool(player, node.id, set) && !CanAllocatePassive(player, node.id))
			return FormatRuntime(_("Allocated in weapon set {:s}"), WeaponSetName(set == PassivePool::SetI ? 0 : 1));
	}
	if (CanAllocatePassive(player, node.id))
		return node.kind == PassiveKind::Attribute ? std::string(_("Click to pick an attribute")) : std::string(_("Click to allocate"));
	return (node.kind == PassiveKind::AscendancyNotable ? AscendancyPointsLeft(player) : PassivePointsLeft(player)) <= 0 ? std::string(_("No points left")) : std::string(_("Not linked to your tree yet"));
}

/** @brief A big POE2-style popup: a framed title bar with the node's name, its kind, then its stats in large type. */
void DrawTooltip(const Surface &out, const Layout &layout, const Player &player, const PassiveNode &node)
{
	struct Line {
		std::string text;
		UiFlags color;
	};
	std::vector<Line> lines;
	for (const auto &[stat, value] : PassiveNodeStats(player, node))
		lines.push_back({ PassiveStatText(stat, value), value < 0 ? UiFlags::ColorRed : UiFlags::ColorBlue });
	if (node.kind == PassiveKind::JewelSocket) {
		if (const Item *jewel = SocketedJewel(player, node.id); jewel != nullptr) {
			lines.push_back({ std::string(jewel->_iIName), jewel->_iMagical == ITEM_QUALITY_RARE ? UiFlags::ColorYellow : UiFlags::ColorBlue });
			for (const Poe2ItemMod &mod : jewel->_iPoe2Mods) {
				for (const std::string &text : ItemModText(mod))
					lines.push_back({ text, UiFlags::ColorBlue });
			}
		} else {
			lines.push_back({ std::string(_("Holds a Ruby, Emerald or Sapphire")), UiFlags::ColorWhite });
		}
	}
	const std::string title = PassiveNodeName(node);
	const std::string_view kind = NodeKindText(node);
	const std::string hint = NodeHint(player, node);

	constexpr int Pad = 12;
	constexpr int TitleHeight = 34;
	constexpr int KindHeight = 18;
	constexpr int StatHeight = 28;
	constexpr int HintHeight = 22;
	int width = std::max({ GetLineWidth(title, GameFont24) + (2 * Pad), GetLineWidth(kind) + (2 * Pad), GetLineWidth(hint) + (2 * Pad), 300 });
	for (const Line &line : lines)
		width = std::max(width, GetLineWidth(line.text, GameFont24) + (2 * Pad));
	width = std::min(width, layout.panel.size.width - 8);
	const int height = TitleHeight + KindHeight + 6 + (static_cast<int>(lines.size()) * StatHeight) + HintHeight + Pad;

	Point position = MousePosition + Displacement { 18, 12 };
	position.x = std::clamp(position.x, layout.panel.position.x + 4, layout.panel.position.x + layout.panel.size.width - width - 4);
	position.y = std::clamp(position.y, layout.panel.position.y + 4, layout.panel.position.y + layout.panel.size.height - height - 4);
	const Rectangle box { position, { width, height } };
	const uint8_t frame = NodeFrameColor(node);
	FillRectangle(out, box, TooltipColor);
	const Rectangle titleBar { position, { width, TitleHeight } };
	FillRectangle(out, titleBar, TitleBarColor);
	DrawFrame(out, titleBar, frame);
	DrawFrame(out, box, frame);
	DrawFrame(out, { position + Displacement { 1, 1 }, { width - 2, height - 2 } }, frame);
	const bool gold = node.kind != PassiveKind::Small && node.kind != PassiveKind::Attribute;
	DrawString(out, title, titleBar, { .flags = UiFlags::FontSize24 | UiFlags::AlignCenter | UiFlags::VerticalCenter | (gold ? UiFlags::ColorUiGold : UiFlags::ColorUiSilver) });

	Rectangle line { position + Displacement { Pad, TitleHeight + 4 }, { width - (2 * Pad), KindHeight } };
	DrawString(out, kind, line, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhitegold });
	line.position.y += KindHeight + 6;
	line.size.height = StatHeight;
	for (const Line &stat : lines) {
		DrawString(out, stat.text, line, { .flags = UiFlags::FontSize24 | UiFlags::AlignCenter | UiFlags::VerticalCenter | stat.color });
		line.position.y += StatHeight;
	}
	line.size.height = HintHeight;
	DrawString(out, hint, line, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorButtonpushed });
}

/** @brief Where the attribute picker's buttons are, next to the node waiting for a pick. */
std::array<Rectangle, static_cast<size_t>(PassiveAttribute::Count)> AttributeButtons(const Layout &layout, const PassiveNode &node)
{
	constexpr Size ButtonSize { 120, 30 };
	constexpr int Gap = 4;
	const Point center = NodeViewPoint(layout, node) + Displacement { layout.view.position.x, layout.view.position.y };
	const int width = ButtonSize.width;
	const int height = (static_cast<int>(PassiveAttribute::Count) * (ButtonSize.height + Gap)) - Gap;
	Point origin { center.x + 20, center.y - (height / 2) };
	origin.x = std::clamp(origin.x, layout.view.position.x + 4, layout.view.position.x + layout.view.size.width - width - 4);
	origin.y = std::clamp(origin.y, layout.view.position.y + 4, layout.view.position.y + layout.view.size.height - height - 4);
	std::array<Rectangle, static_cast<size_t>(PassiveAttribute::Count)> buttons;
	for (size_t i = 0; i < buttons.size(); i++)
		buttons[i] = { origin + Displacement { 0, static_cast<int>(i) * (ButtonSize.height + Gap) }, ButtonSize };
	return buttons;
}

void DrawAttributePicker(const Surface &out, const Layout &layout, const PassiveNode &node)
{
	const auto buttons = AttributeButtons(layout, node);
	const Rectangle frame { buttons.front().position - Displacement { 4, 4 }, { buttons.front().size.width + 8, buttons.back().position.y + buttons.back().size.height - buttons.front().position.y + 8 } };
	FillRectangle(out, frame, TooltipColor);
	DrawFrame(out, frame, AttributeRimColor);
	for (size_t i = 0; i < buttons.size(); i++) {
		const bool hovered = buttons[i].contains(MousePosition);
		FillRectangle(out, buttons[i], hovered ? TitleBarColor : PanelColor);
		DrawFrame(out, buttons[i], hovered ? HoverColor : AttributeRimColor);
		DrawString(out, FormatRuntime(_("+5 {:s}"), PassiveAttributeName(static_cast<PassiveAttribute>(i))), buttons[i],
		    { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | (hovered ? UiFlags::ColorWhitegold : UiFlags::ColorWhite) });
	}
}

std::string PointsText(const Player &player)
{
	if (Pool != PassivePool::Main) {
		const int total = WeaponSetPointsTotal(player);
		if (total == 0)
			return std::string(_("No weapon set points yet: read Books of Specialisation from quest bosses"));
		return FormatRuntime(_("{:d} weapon set points left ({:d} of {:d} spent)"), WeaponSetPointsLeft(player, Pool), WeaponSetPointsSpent(player, Pool), total);
	}
	const int left = PassivePointsLeft(player);
	if (AscendancyPointsTotal(player) > 0)
		return FormatRuntime(_("{:d} passive points, {:d} ascendancy points left"), left, AscendancyPointsLeft(player));
	return FormatRuntime(_("{:d} points left ({:d} of {:d} spent)"), left, PassivePointsSpent(player), PassivePointsTotal(player));
}

constexpr std::array<PassivePool, 3> Pools { PassivePool::Main, PassivePool::SetI, PassivePool::SetII };

/** @brief The Main / Weapon Set I / Weapon Set II tabs in the title bar. */
std::array<Rectangle, 3> PoolTabs(const Layout &layout)
{
	constexpr Size TabSize { 120, 24 };
	constexpr int Gap = 6;
	const int width = (3 * TabSize.width) + (2 * Gap);
	const Point origin { layout.panel.position.x + ((layout.panel.size.width - width) / 2), layout.panel.position.y + 7 };
	std::array<Rectangle, 3> tabs;
	for (size_t i = 0; i < tabs.size(); i++)
		tabs[i] = { origin + Displacement { static_cast<int>(i) * (TabSize.width + Gap), 0 }, TabSize };
	return tabs;
}

std::string_view PoolName(PassivePool pool)
{
	switch (pool) {
	case PassivePool::SetI:
		return _("Weapon Set I");
	case PassivePool::SetII:
		return _("Weapon Set II");
	default:
		return _("Main Tree");
	}
}

void DrawPoolTabs(const Surface &out, const Layout &layout, const Player &player)
{
	const auto tabs = PoolTabs(layout);
	for (size_t i = 0; i < tabs.size(); i++) {
		const PassivePool pool = Pools[i];
		const bool selected = pool == Pool;
		const uint8_t accent = pool == PassivePool::Main ? BorderColor : SetColor(pool);
		FillRectangle(out, tabs[i], selected ? TitleBarColor : PanelColor);
		DrawFrame(out, tabs[i], selected ? accent : PAL16_GRAY + 8);
		// The set that is out gets a mark, since only its nodes count right now.
		std::string label(PoolName(pool));
		if (pool != PassivePool::Main && static_cast<uint8_t>(static_cast<size_t>(pool) - 1) == player.poe2.weaponSet)
			label += " *";
		DrawString(out, label, tabs[i], { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | (selected ? UiFlags::ColorWhitegold : UiFlags::ColorUiSilverDark) });
	}
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
	Pool = PassivePool::Main;
	PendingAttributeNode = std::nullopt;
	Pressed = false;
	Dragging = false;
	CenterOnStart();
	ClearWasdMovementKeys();
}

void ClosePassiveTree()
{
	IsOpen = false;
	PendingAttributeNode = std::nullopt;
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
			if (PendingAttributeNode && SDLC_EventKey(event) == SDLK_ESCAPE)
				PendingAttributeNode = std::nullopt;
			else
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
		case SDLK_TAB:
			Pool = Pools[(static_cast<size_t>(Pool) + 1) % Pools.size()];
			PendingAttributeNode = std::nullopt;
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
		if (PendingAttributeNode) {
			// The attribute picker takes the click: a button allocates the node with that attribute, anything else cancels.
			const auto buttons = AttributeButtons(layout, PassiveTree()[*PendingAttributeNode]);
			if (event.button.button == SDL_BUTTON_LEFT) {
				for (size_t i = 0; i < buttons.size(); i++) {
					if (buttons[i].contains(MousePosition))
						AllocatePassive(player, *PendingAttributeNode, static_cast<PassiveAttribute>(i), Pool);
				}
			}
			PendingAttributeNode = std::nullopt;
			return true;
		}
		if (event.button.button == SDL_BUTTON_LEFT) {
			const auto tabs = PoolTabs(layout);
			for (size_t i = 0; i < tabs.size(); i++) {
				if (tabs[i].contains(MousePosition)) {
					Pool = Pools[i];
					return true;
				}
			}
		}
		const std::optional<PassiveNodeId> node = NodeAt(layout, MousePosition);
		if (event.button.button == SDL_BUTTON_LEFT) {
			if (node && PassiveNeedsAttributeChoice(*node) && CanAllocatePassive(player, *node, Pool)) {
				PendingAttributeNode = node;
			} else if (node && Pool == PassivePool::Main && PassiveTree()[*node].kind == PassiveKind::JewelSocket && IsJewel(player.HoldItem)) {
				PlaceHeldJewel(player, *node);
			} else if (node) {
				AllocatePassive(player, *node, PassiveAttribute::Strength, Pool);
			} else if (layout.view.contains(MousePosition)) {
				Pressed = true;
				Dragging = false;
				DragStart = MousePosition;
				DragViewX = ViewX;
				DragViewY = ViewY;
			}
		} else if (node && event.button.button == SDL_BUTTON_RIGHT) {
			if (Pool == PassivePool::Main && SocketedJewel(player, *node) != nullptr)
				TakeOutJewel(player, *node);
			else
				RefundPassive(player, *node, Pool);
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
	const bool pointsLeft = Pool == PassivePool::Main ? PassivePointsLeft(player) > 0 || AscendancyPointsLeft(player) > 0 : WeaponSetPointsLeft(player, Pool) > 0;
	DrawPoolTabs(out, layout, player);
	DrawString(out, PointsText(player), title, { .flags = UiFlags::AlignRight | UiFlags::VerticalCenter | (pointsLeft ? UiFlags::ColorWhitegold : UiFlags::ColorWhite) });

	const Surface view = out.subregion(layout.view.position.x, layout.view.position.y, layout.view.size.width, layout.view.size.height);
	FillRect(view, 0, 0, view.w(), view.h(), ViewColor);
	DrawSeal(view, layout, player);
	DrawClassNames(view, layout, player);
	const std::optional<PassiveNodeId> hovered = Dragging || PendingAttributeNode ? std::nullopt : NodeAt(layout, MousePosition);
	DrawNodes(view, layout, player, hovered);
	DrawFrame(out, layout.view, BorderColor);

	const std::string help = WordWrapString(_("Click a linked node to allocate it, right-click to refund. Tab or the tabs above switch between the main tree and the weapon sets (X swaps sets in game). Drag or arrow keys move, wheel or +/- zooms, Home recenters. P or Esc closes."), layout.footer.size.width);
	DrawString(out, help, layout.footer, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorButtonpushed, .lineHeight = 14 });

	if (PendingAttributeNode)
		DrawAttributePicker(out, layout, PassiveTree()[*PendingAttributeNode]);
	else if (hovered)
		DrawTooltip(out, layout, player, PassiveTree()[*hovered]);
}

} // namespace devilution
