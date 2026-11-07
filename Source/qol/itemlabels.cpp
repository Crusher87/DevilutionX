#include "itemlabels.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "control/control.hpp"
#include "cursor.h"
#include "engine/point.hpp"
#include "engine/render/clx_render.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/ui_scale.hpp"
#include "game_mode.hpp"
#include "gmenu.h"
#include "inv.h"
#include "options.h"
#include "poe2/loot_filter.hpp"
#include "qol/stash.h"
#include "stores.h"
#include "utils/algorithm/container.hpp"
#include "utils/format.hpp"
#include "utils/format_int.hpp"
#include "utils/language.h"

namespace devilution {

namespace {

struct ItemLabel {
	int id, width;
	Point pos;
	StringOrView text;
};

std::vector<ItemLabel> labelQueue;

bool highlightKeyPressed = false;
bool isLabelHighlighted = false;
std::array<std::optional<int>, ITEMTYPES> labelCenterOffsets;

const int BorderX = 4; // minimal horizontal space between labels
const int BorderY = 2; // minimal vertical space between labels
const int MarginX = 2; // horizontal margins between text and edges of the label

// Vertical space between the text and the edges of the label.
int TextMarginTop() { return IsSmallFontTall() ? 1 : -1; }
int TextMarginBottom() { return IsSmallFontTall() ? 1 : 3; }

// The total height of the label box.
int LabelHeight() { return (IsSmallFontTall() ? 16 : 11) + TextMarginBottom() + TextMarginTop(); }

/**
 * @brief The set of used X coordinates for a certain Y coordinate.
 */
class UsedX {
public:
	[[nodiscard]] bool contains(int val) const
	{
		return c_find(data_, val) != data_.end();
	}

	void insert(int val)
	{
		if (!contains(val))
			data_.push_back(val);
	}

	void clear()
	{
		data_.clear();
	}

private:
	std::vector<int> data_;
};

} // namespace

void ToggleItemLabelHighlight()
{
	GetOptions().Gameplay.showItemLabels.SetValue(!*GetOptions().Gameplay.showItemLabels);
}

void HighlightKeyPressed(bool pressed)
{
	highlightKeyPressed = pressed;
}

bool IsItemLabelHighlighted()
{
	return isLabelHighlighted;
}

void ResetItemlabelHighlighted()
{
	isLabelHighlighted = false;
}

bool IsHighlightingLabelsEnabled()
{
	return !IsPlayerInStore() && highlightKeyPressed != *GetOptions().Gameplay.showItemLabels;
}

void AddItemToLabelQueue(int id, Point position)
{
	if (!IsHighlightingLabelsEnabled())
		return;
	Item &item = Items[id];
	if (IsItemHiddenByLootFilter(item))
		return;
	// POE2 Mashup: no names through closed doors or in rooms not yet seen (Curtis 2026-11-07).
	if (gbIsPoe2 && !HeroHasSeenGroundItem(item))
		return;
	// POE2 Mashup: gold has no label; picking it up shows the amount over the hero.
	if (gbIsPoe2 && item._itype == ItemType::Gold)
		return;

	StringOrView textOnGround;
	if (item._itype == ItemType::Gold) {
		textOnGround = FormatRuntime(_("{:s} gold"), FormatInteger(item._ivalue));
	} else {
		textOnGround = item.getName();
	}

	int nameWidth = GetLineWidth(textOnGround);
	nameWidth += MarginX * 2;
	const int index = ItemCAnimTbl[item._iCurs];
	if (!labelCenterOffsets[index]) {
		const auto [xBegin, xEnd] = ClxMeasureSolidHorizontalBounds((*item.AnimInfo.sprites)[item.AnimInfo.currentFrame]);
		labelCenterOffsets[index].emplace((xBegin + xEnd) / 2);
	}

	position.x += *labelCenterOffsets[index];
	position.y -= TILE_HEIGHT;
	if (*GetOptions().Graphics.zoom) {
		position *= 2;
	}
	// POE2 Mashup: the labels are UI, drawn over the full-size world.
	position = WorldToUi(position);
	position.x -= nameWidth / 2;
	position.y -= LabelHeight();
	labelQueue.push_back(ItemLabel { id, nameWidth, position, std::move(textOnGround) });
}

bool IsMouseOverGameArea()
{
	return !IsCursorOverUi(MousePosition);
}

void DrawItemNameLabels(const Surface &out)
{
	const Surface clippedOut = out.subregionY(0, gnViewportHeight);
	isLabelHighlighted = false;
	if (labelQueue.empty())
		return;
	UsedX usedX;
	const int labelHeight = LabelHeight();
	const int labelMarginTop = TextMarginTop();

	// The queue fills in screen draw order, which reshuffles as the view scrolls by half tiles, so the overlap pass
	// below nudged different labels each frame and they jumped about while walking (Curtis 2026-11-06). Placing them
	// in a fixed order, front of the floor first by map position, keeps each label where it was as the view moves.
	std::stable_sort(labelQueue.begin(), labelQueue.end(), [](const ItemLabel &a, const ItemLabel &b) {
		const Point pa = Items[a.id].position;
		const Point pb = Items[b.id].position;
		if (pa.x + pa.y != pb.x + pb.y)
			return pa.x + pa.y > pb.x + pb.y;
		if (pa.x != pb.x)
			return pa.x < pb.x;
		return a.id < b.id;
	});

	for (unsigned i = 0; i < labelQueue.size(); ++i) {
		usedX.clear();

		bool canShow;
		do {
			canShow = true;
			for (unsigned j = 0; j < i; ++j) {
				ItemLabel &a = labelQueue[i];
				const ItemLabel &b = labelQueue[j];
				// POE2 Mashup: room for the loot filter's borders, drawn round the outside of the labels.
				if (std::abs(b.pos.y - a.pos.y) < labelHeight + BorderY + (gbIsPoe2 ? 4 : 0)) {
					const int widthA = a.width + BorderX + (MarginX * 2);
					const int widthB = b.width + BorderX + (MarginX * 2);
					int newpos = b.pos.x;
					if (b.pos.x >= a.pos.x && b.pos.x - a.pos.x < widthA) {
						newpos -= widthA;
						if (usedX.contains(newpos))
							newpos = b.pos.x + widthB;
					} else if (b.pos.x < a.pos.x && a.pos.x - b.pos.x < widthB) {
						newpos += widthB;
						if (usedX.contains(newpos))
							newpos = b.pos.x - widthA;
					} else
						continue;
					canShow = false;
					a.pos.x = newpos;
					usedX.insert(newpos);
				}
			}
		} while (!canShow);
	}

	for (const ItemLabel &label : labelQueue) {
		const Item &item = Items[label.id];

		if (MousePosition.x >= label.pos.x && MousePosition.x < label.pos.x + label.width
		    && MousePosition.y >= label.pos.y && MousePosition.y < label.pos.y + labelHeight) {
			// POE2 Mashup: a skill cast from a key leaves LastPlayerAction set until the next mouse release, which hid
			// ground labels from the mouse; only a held mouse button should (Curtis 2026-10-29).
			if (!gmenu_is_active()
			    && PauseMode == 0
			    && !MyPlayerIsDead
			    && !IsPlayerInStore()
			    && IsMouseOverGameArea()
			    && (gbIsPoe2 ? sgbMouseDown == CLICK_NONE : LastPlayerAction == PlayerActionType::None)) {
				isLabelHighlighted = true;
				cursPosition = item.position;
				pcursitem = label.id;
			}
		}
		// POE2 Mashup: with the loot filter on, the label takes the item's NeverSink colours (Curtis 2026-11-05).
		const LootStyle *style = LootFilterStyle(item);
		if (pcursitem == label.id && !IsPlayerInStore())
			FillRect(clippedOut, label.pos.x, label.pos.y, label.width, labelHeight, PAL8_BLUE + 6);
		else if (style != nullptr && style->background)
			FillRect(clippedOut, label.pos.x, label.pos.y, label.width, labelHeight, NearestLootPaletteColour(*style->background));
		else
			DrawHalfTransparentRectTo(clippedOut, label.pos.x, label.pos.y, label.width, labelHeight);
		if (style != nullptr && style->border) {
			const uint8_t border = NearestLootPaletteColour(*style->border);
			const int thickness = style->big ? 2 : 1;
			for (int i = 0; i < thickness; i++) {
				const int x = label.pos.x - 1 - i;
				const int y = label.pos.y - 1 - i;
				const int w = label.width + 2 + (2 * i);
				const int h = labelHeight + 2 + (2 * i);
				DrawHorizontalLine(clippedOut, { x, y }, w, border);
				DrawHorizontalLine(clippedOut, { x, y + h - 1 }, w, border);
				DrawVerticalLine(clippedOut, { x, y }, h, border);
				DrawVerticalLine(clippedOut, { x + w - 1, y }, h, border);
			}
		}
		DrawString(clippedOut, label.text, { { label.pos.x + MarginX, label.pos.y + labelMarginTop }, { label.width, labelHeight } },
		    { .flags = style != nullptr ? style->text : item.getTextColor() });
	}
	labelQueue.clear();
}

} // namespace devilution
