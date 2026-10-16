#include "control.hpp"
#include "control_panel.hpp"
#include "controls/control_mode.hpp"
#include "cursor.h"
#include "engine/render/primitive_render.hpp"
#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "levels/trigs.h"
#include "options.h"
#include "panels/partypanel.hpp"
#include "panels/poe2_widgets.hpp"
#include "panels/skill_bar.hpp"
#include "poe2/endgame.hpp"
#include "poe2/monster_mods.hpp"
#include "qol/stash.h"
#include "qol/visual_store.h"
#include "qol/xpbar.h"
#include "towners.h"
#include "utils/algorithm/container.hpp"
#include "utils/format.hpp"
#include "utils/format_int.hpp"
#include "utils/log.hpp"
#include "utils/screen_reader.hpp"
#include "utils/str_cat.hpp"
#include "utils/str_split.hpp"

namespace devilution {

StringOrView InfoString;
StringOrView FloatingInfoString;
InfoLineStyle CurrentInfoLineStyle = InfoLineStyle::Default;
std::vector<InfoLineStyle> FloatingInfoStyles;

namespace {

void PrintInfo(const Surface &out)
{
	if (ChatFlag)
		return;
	// POE2 Mashup: a hovered item's name and details are in its tooltip instead.
	if (gbIsPoe2 && !FloatingInfoString.empty())
		return;

	const int space[] = { 18, 12, 6, 3, 0 };
	Rectangle infoBox = InfoBoxRect;

	SetPanelObjectPosition(UiPanels::Main, infoBox);

	const auto newLineCount = static_cast<int>(c_count(InfoString.str(), '\n'));
	const int spaceIndex = std::min(4, newLineCount);
	const int spacing = space[spaceIndex];
	const int lineHeight = 12 + spacing;

	// Adjusting the line height to add spacing between lines
	// will also add additional space beneath the last line
	// which throws off the vertical centering
	infoBox.position.y += spacing / 2;

	SpeakText(InfoString);

	DrawString(out, InfoString, infoBox,
	    {
	        .flags = InfoColor | UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::KerningFitSpacing,
	        .spacing = 2,
	        .lineHeight = lineHeight,
	    });
}

Rectangle GetFloatingInfoRect(const int lineHeight, const int textSpacing)
{
	// Calculate the width and height of the floating info box
	const std::string txt = std::string(FloatingInfoString);

	auto lines = SplitByChar(txt, '\n');
	const GameFontTables font = GameFont12;
	int maxW = 0;

	for (const auto &line : lines) {
		const int w = GetLineWidth(line, font, textSpacing, nullptr);
		maxW = std::max(maxW, w);
	}

	const auto lineCount = 1 + static_cast<int>(c_count(FloatingInfoString.str(), '\n'));
	const int totalH = lineCount * lineHeight;

	const Player &player = *InspectPlayer;

	// 1) Equipment (Rect position)
	if (pcursinvitem >= INVITEM_HEAD && pcursinvitem < INVITEM_INV_FIRST) {
		const int slot = pcursinvitem - INVITEM_HEAD;
		static constexpr Point equipLocal[] = {
			{ 133, 59 },
			{ 48, 205 },
			{ 249, 205 },
			{ 205, 60 },
			{ 17, 160 },
			{ 248, 160 },
			{ 133, 160 },
		};

		Point itemPosition = equipLocal[slot];
		auto &item = player.InvBody[slot];
		const Size frame = GetInvItemSize(item._iCurs + CURSOR_FIRSTITEM);

		if (slot == INVLOC_HAND_LEFT) {
			itemPosition.x += frame.width == InventorySlotSizeInPixels.width
			    ? InventorySlotSizeInPixels.width
			    : 0;
			itemPosition.y += frame.height == 3 * InventorySlotSizeInPixels.height
			    ? 0
			    : -InventorySlotSizeInPixels.height;
		} else if (slot == INVLOC_HAND_RIGHT) {
			itemPosition.x += frame.width == InventorySlotSizeInPixels.width
			    ? (InventorySlotSizeInPixels.width - 1)
			    : 1;
			itemPosition.y += frame.height == 3 * InventorySlotSizeInPixels.height
			    ? 0
			    : -InventorySlotSizeInPixels.height;
		}

		itemPosition.y++;                  // Align position to bottom left of the item graphic
		itemPosition.x += frame.width / 2; // Align position to center of the item graphic
		itemPosition.x -= maxW / 2;        // Align position to the center of the floating item info box

		const Point screen = GetPanelPosition(UiPanels::Inventory, itemPosition);

		return { { screen.x, screen.y }, { maxW, totalH } };
	}

	// 2) Inventory grid (Rect position)
	if (pcursinvitem >= INVITEM_INV_FIRST && pcursinvitem < INVITEM_INV_FIRST + InventoryGridCells) {
		const int itemIdx = pcursinvitem - INVITEM_INV_FIRST;

		for (int j = 0; j < InventoryGridCells; ++j) {
			if (player.InvGrid[j] > 0 && player.InvGrid[j] - 1 == itemIdx) {
				const Item &it = player.InvList[itemIdx];
				Point itemPosition = InvRect[j + SLOTXY_INV_FIRST].position;

				itemPosition.x += GetInventorySize(it).width * InventorySlotSizeInPixels.width / 2; // Align position to center of the item graphic
				itemPosition.x -= maxW / 2;                                                         // Align position to the center of the floating item info box

				const Point screen = GetPanelPosition(UiPanels::Inventory, itemPosition);

				return { { screen.x, screen.y }, { maxW, totalH } };
			}
		}
	}

	// 3) Belt (Rect position)
	if (pcursinvitem >= INVITEM_BELT_FIRST && pcursinvitem < INVITEM_BELT_FIRST + MaxBeltItems) {
		const int itemIdx = pcursinvitem - INVITEM_BELT_FIRST;
		for (int i = 0; i < MaxBeltItems; ++i) {
			if (player.SpdList[i].isEmpty())
				continue;
			if (i != itemIdx)
				continue;

			const Item &item = player.SpdList[i];
			Point itemPosition = InvRect[i + SLOTXY_BELT_FIRST].position;

			itemPosition.x += GetInventorySize(item).width * InventorySlotSizeInPixels.width / 2; // Align position to center of the item graphic
			itemPosition.x -= maxW / 2;                                                           // Align position to the center of the floating item info box

			const Point screen = GetMainPanel().position + Displacement { itemPosition.x, itemPosition.y };

			return { { screen.x, screen.y }, { maxW, totalH } };
		}
	}

	// 4) Stash (Rect position)
	if (pcursstashitem != StashStruct::EmptyCell) {
		for (auto slot : StashGridRange) {
			auto itemId = Stash.GetItemIdAtPosition(slot);
			if (itemId == StashStruct::EmptyCell)
				continue;
			if (itemId != pcursstashitem)
				continue;

			const Item &item = Stash.stashList[itemId];
			Point itemPosition = GetStashSlotCoord(slot);
			const Size itemGridSize = GetInventorySize(item);

			itemPosition.y += itemGridSize.height * (InventorySlotSizeInPixels.height + 1) - 1; // Align position to bottom left of the item graphic
			itemPosition.x += itemGridSize.width * InventorySlotSizeInPixels.width / 2;         // Align position to center of the item graphic
			itemPosition.x -= maxW / 2;                                                         // Align position to the center of the floating item info box

			return { { itemPosition.x, itemPosition.y }, { maxW, totalH } };
		}
	}

	// 5) Visual Store (Rect position)
	if (pcursstoreitem != -1) {
		const VisualStorePage &page = VisualStore.pages[VisualStore.currentPage];
		std::span<Item> allItems = GetVisualStoreItems();
		for (const auto &vsItem : page.items) {
			if (vsItem.index != pcursstoreitem)
				continue;

			const Item &item = allItems[vsItem.index];
			Point itemPosition = GetVisualStoreSlotCoord(vsItem.position);
			const Size itemGridSize = GetInventorySize(item);

			itemPosition.y += itemGridSize.height * (VisualStoreGridHeight + 1) - 1; // Align position to bottom left of the item graphic
			itemPosition.x += itemGridSize.width * VisualStoreGridWidth / 2;         // Align position to center of the item graphic
			itemPosition.x -= maxW / 2;                                              // Align position to the center of the floating item info box

			return { { itemPosition.x, itemPosition.y }, { maxW, totalH } };
		}
	}
	if (pcursstorebtn != -1) {
		return { GetVisualBtnCoord(pcursstorebtn).position, { maxW, totalH } };
	}

	return { { 0, 0 }, { 0, 0 } };
}

int GetHoverSpriteHeight()
{
	if (pcursinvitem >= INVITEM_HEAD && pcursinvitem < INVITEM_INV_FIRST) {
		auto &it = (*InspectPlayer).InvBody[pcursinvitem - INVITEM_HEAD];
		return GetInvItemSize(it._iCurs + CURSOR_FIRSTITEM).height + 1;
	}
	if (pcursinvitem >= INVITEM_INV_FIRST
	    && pcursinvitem < INVITEM_INV_FIRST + InventoryGridCells) {
		const int idx = pcursinvitem - INVITEM_INV_FIRST;
		auto &it = (*InspectPlayer).InvList[idx];
		return (GetInventorySize(it).height * (InventorySlotSizeInPixels.height + 1))
		    - InventorySlotSizeInPixels.height;
	}
	if (pcursinvitem >= INVITEM_BELT_FIRST
	    && pcursinvitem < INVITEM_BELT_FIRST + MaxBeltItems) {
		const int idx = pcursinvitem - INVITEM_BELT_FIRST;
		auto &it = (*InspectPlayer).SpdList[idx];
		return (GetInventorySize(it).height * (InventorySlotSizeInPixels.height + 1))
		    - InventorySlotSizeInPixels.height - 1;
	}
	if (pcursstashitem != StashStruct::EmptyCell) {
		auto &it = Stash.stashList[pcursstashitem];
		return GetInventorySize(it).height * (InventorySlotSizeInPixels.height + 1);
	}
	if (pcursstoreitem != -1) {
		std::span<Item> allItems = GetVisualStoreItems();
		auto &it = allItems[pcursstoreitem];
		return GetInventorySize(it).height * (INV_SLOT_SIZE_PX + 1);
	}
	return InventorySlotSizeInPixels.height;
}

int ClampAboveOrBelow(int anchorY, int spriteH, int boxH, int pad, int linePad)
{
	const int yAbove = anchorY - spriteH - boxH - pad;
	const int yBelow = anchorY + (linePad / 2) + pad;
	return (yAbove >= 0) ? yAbove : yBelow;
}

void PrintFloatingInfo(const Surface &out)
{
	if (ChatFlag)
		return;
	if (FloatingInfoString.empty())
		return;

	const int verticalSpacing = 3;
	const int lineHeight = 12 + verticalSpacing;
	const int textSpacing = 2;
	const int hPadding = 5;
	const int vPadding = 4;

	Rectangle floatingInfoBox = GetFloatingInfoRect(lineHeight, textSpacing);

	// Prevent the floating info box from going off-screen horizontally
	floatingInfoBox.position.x = std::clamp(floatingInfoBox.position.x, hPadding, GetScreenWidth() - (floatingInfoBox.size.width + hPadding));

	const int spriteH = GetHoverSpriteHeight();
	const int anchorY = floatingInfoBox.position.y;

	// Prevent the floating info box from going off-screen vertically
	floatingInfoBox.position.y = ClampAboveOrBelow(anchorY, spriteH, floatingInfoBox.size.height, vPadding, verticalSpacing);

	SpeakText(FloatingInfoString);

	for (int i = 0; i < 3; i++)
		DrawHalfTransparentRectTo(out, floatingInfoBox.position.x - hPadding, floatingInfoBox.position.y - vPadding, floatingInfoBox.size.width + (hPadding * 2), floatingInfoBox.size.height + (vPadding * 2));
	DrawHalfTransparentVerticalLine(out, { floatingInfoBox.position.x - hPadding - 1, floatingInfoBox.position.y - vPadding - 1 }, floatingInfoBox.size.height + (vPadding * 2) + 2, PAL16_GRAY + 10);
	DrawHalfTransparentVerticalLine(out, { floatingInfoBox.position.x + hPadding + floatingInfoBox.size.width, floatingInfoBox.position.y - vPadding - 1 }, floatingInfoBox.size.height + (vPadding * 2) + 2, PAL16_GRAY + 10);
	DrawHalfTransparentHorizontalLine(out, { floatingInfoBox.position.x - hPadding, floatingInfoBox.position.y - vPadding - 1 }, floatingInfoBox.size.width + (hPadding * 2), PAL16_GRAY + 10);
	DrawHalfTransparentHorizontalLine(out, { floatingInfoBox.position.x - hPadding, floatingInfoBox.position.y + vPadding + floatingInfoBox.size.height }, floatingInfoBox.size.width + (hPadding * 2), PAL16_GRAY + 10);

	DrawString(out, FloatingInfoString, floatingInfoBox,
	    {
	        .flags = InfoColor | UiFlags::AlignCenter | UiFlags::VerticalCenter,
	        .spacing = textSpacing,
	        .lineHeight = lineHeight,
	    });
}

/** @brief POE2 Mashup: the frame colour of an item tooltip, from the item's text colour (its rarity). */
uint8_t TooltipFrameColor(UiFlags color)
{
	if (HasAnyOf(color, UiFlags::ColorBlue))
		return PAL16_BLUE + 5;
	if (HasAnyOf(color, UiFlags::ColorYellow))
		return PAL16_YELLOW + 3;
	if (HasAnyOf(color, UiFlags::ColorWhitegold | UiFlags::ColorOrange))
		return PAL16_ORANGE + 4;
	if (HasAnyOf(color, UiFlags::ColorRed))
		return PAL16_RED + 6;
	return PAL16_GRAY + 6;
}

UiFlags TooltipLineColor(InfoLineStyle style)
{
	switch (style) {
	case InfoLineStyle::Mod:
		return UiFlags::ColorBlue;
	case InfoLineStyle::Corrupted:
		return UiFlags::ColorRed;
	case InfoLineStyle::Requirement:
		return UiFlags::ColorButtonpushed;
	case InfoLineStyle::Note:
		return UiFlags::ColorWhitegold;
	case InfoLineStyle::Better:
		return UiFlagsColorGreen;
	case InfoLineStyle::Worse:
		return UiFlags::ColorRed;
	default:
		return UiFlags::ColorWhite;
	}
}

/** @brief Groups of lines that get a separator between them. */
int TooltipGroup(InfoLineStyle style)
{
	switch (style) {
	case InfoLineStyle::Property:
	case InfoLineStyle::Default:
		return 0;
	case InfoLineStyle::Implicit:
		return 1;
	case InfoLineStyle::Mod:
	case InfoLineStyle::Corrupted:
		return 2;
	case InfoLineStyle::Better:
	case InfoLineStyle::Worse:
		return 4;
	default:
		return 3;
	}
}

void DrawTooltipSeparator(const Surface &out, Point center, int width, uint8_t color)
{
	DrawHorizontalLine(out, center - Displacement { width / 2, 0 }, width, color);
	// A small diamond in the middle, like POE2's item dividers.
	for (int y = -2; y <= 2; y++) {
		const int half = 2 - std::abs(y);
		DrawHorizontalLine(out, center + Displacement { -half, y }, (2 * half) + 1, color);
	}
}

/**
 * @brief POE2 Mashup: an item tooltip by the cursor, like POE2's: a header band in the rarity's colour with
 * the name, then the item's properties, implicits, modifiers and requirements split by dividers.
 */
void PrintPoe2ItemTooltip(const Surface &out)
{
	if (ChatFlag)
		return;
	const std::string text { FloatingInfoString.str() };
	std::vector<std::string_view> lines;
	for (const std::string_view line : SplitByChar(text, '\n'))
		lines.push_back(line);
	if (lines.empty())
		return;

	// Styles belong to the lines after the title, newest last.
	std::vector<InfoLineStyle> styles(lines.size() - 1, InfoLineStyle::Default);
	const size_t known = std::min(styles.size(), FloatingInfoStyles.size());
	std::copy(FloatingInfoStyles.end() - static_cast<ptrdiff_t>(known), FloatingInfoStyles.end(), styles.end() - static_cast<ptrdiff_t>(known));

	std::vector<std::string_view> titles { lines[0] };
	size_t first = 1;
	while (first < lines.size() && styles[first - 1] == InfoLineStyle::Subtitle) {
		titles.push_back(lines[first]);
		first++;
	}

	constexpr int LineHeight = 15;
	constexpr int TitleHeight = 18;
	constexpr int Padding = 12;
	constexpr int SeparatorHeight = 9;
	int width = 220;
	for (const std::string_view title : titles)
		width = std::max(width, GetLineWidth(title, GameFont12, 1) + (Padding * 2) + 24);
	int bodyHeight = 0;
	int previousGroup = -1;
	for (size_t i = first; i < lines.size(); i++) {
		width = std::max(width, GetLineWidth(lines[i], GameFont12, 1) + (Padding * 2));
		const int group = TooltipGroup(styles[i - 1]);
		if (previousGroup != -1 && group != previousGroup)
			bodyHeight += SeparatorHeight;
		previousGroup = group;
		bodyHeight += LineHeight;
	}
	width = std::min(width, out.w() - 8);
	const int headerHeight = 8 + (static_cast<int>(titles.size()) * TitleHeight);
	const int height = headerHeight + (bodyHeight > 0 ? bodyHeight + 12 : 0);

	// A ground item shows its picture in a small box on top of the tooltip.
	const Item *pictured = pcursitem != -1 && MyPlayer != nullptr && MyPlayer->HoldItem.isEmpty() ? &Items[pcursitem] : nullptr;
	const int pictureCursor = pictured != nullptr ? pictured->_iCurs + CURSOR_FIRSTITEM : 0;
	const Size pictureSize = pictured != nullptr ? GetInvItemSize(pictureCursor) : Size { 0, 0 };
	const int pictureHeight = pictured != nullptr ? pictureSize.height + 12 : 0;

	// Beside the cursor, on whichever side has more room, and inside the screen.
	const int gap = 24;
	int x = MousePosition.x > out.w() / 2 ? MousePosition.x - gap - width : MousePosition.x + gap;
	x = std::clamp(x, 4, std::max(out.w() - width - 4, 4));
	int y = std::clamp(MousePosition.y - ((height + pictureHeight) / 3), 4, std::max(out.h() - height - pictureHeight - 4, 4)) + pictureHeight;

	const uint8_t frame = TooltipFrameColor(InfoColor);
	if (pictured != nullptr) {
		const int boxWidth = pictureSize.width + 16;
		const Rectangle box { { x + ((width - boxWidth) / 2), y - pictureHeight + 1 }, { boxWidth, pictureHeight } };
		for (int i = 0; i < 3; i++)
			DrawHalfTransparentRectTo(out, box.position.x, box.position.y, box.size.width, box.size.height);
		poe2ui::DrawFrame(out, box, frame);
		// Ground items never get their usable flag worked out: red only if the hero really can't use it.
		Item shown = *pictured;
		shown._iStatFlag = MyPlayer->CanUseItem(shown);
		DrawItem(shown, out, { box.position.x + 8, box.position.y + 6 + pictureSize.height - 1 }, GetInvItemSprite(pictureCursor));
	}
	// Dark body, a little see-through like POE2's, with a frame in the rarity's colour.
	for (int i = 0; i < 3; i++)
		DrawHalfTransparentRectTo(out, x, y, width, height);
	poe2ui::DrawFrame(out, { { x, y }, { width, height } }, frame);

	// The header band: tinted by the rarity, with gold-capped ends.
	const Rectangle header { { x + 2, y + 2 }, { width - 4, headerHeight - 4 } };
	for (int row = 0; row < header.size.height; row++) {
		const uint8_t shade = static_cast<uint8_t>(std::min(15, 11 + (row * 4 / std::max(header.size.height, 1))));
		DrawHorizontalLine(out, header.position + Displacement { 0, row }, header.size.width, static_cast<uint8_t>((frame & 0xF0) + shade));
	}
	DrawHorizontalLine(out, { x + 2, y + headerHeight - 2 }, width - 4, frame);
	for (const int side : { x + 8, x + width - 9 }) {
		const Point gem { side, y + (headerHeight / 2) };
		for (int dy = -4; dy <= 4; dy++) {
			const int half = 4 - std::abs(dy);
			DrawHorizontalLine(out, gem + Displacement { -half, dy }, (2 * half) + 1, frame);
		}
	}
	int lineY = y + 5;
	for (size_t i = 0; i < titles.size(); i++) {
		const UiFlags color = i == 0 ? InfoColor : UiFlags::ColorWhite;
		DrawString(out, titles[i], { { x, lineY + 1 }, { width, TitleHeight } }, { .flags = UiFlags::ColorBlack | UiFlags::AlignCenter | UiFlags::VerticalCenter, .spacing = 1 });
		DrawString(out, titles[i], { { x, lineY }, { width, TitleHeight } }, { .flags = color | UiFlags::AlignCenter | UiFlags::VerticalCenter, .spacing = 1 });
		lineY += TitleHeight;
	}
	SpeakText(FloatingInfoString);

	lineY = y + headerHeight + 6;
	previousGroup = -1;
	for (size_t i = first; i < lines.size(); i++) {
		const InfoLineStyle style = styles[i - 1];
		const int group = TooltipGroup(style);
		if (previousGroup != -1 && group != previousGroup) {
			DrawTooltipSeparator(out, { x + (width / 2), lineY + 3 }, width - 40, frame);
			lineY += SeparatorHeight;
		}
		previousGroup = group;
		DrawString(out, lines[i], { { x, lineY }, { width, LineHeight } }, { .flags = TooltipLineColor(style) | UiFlags::AlignCenter, .spacing = 1 });
		lineY += LineHeight;
	}
}

} // namespace

void AddInfoBoxString(std::string_view str, bool floatingBox /*= false*/)
{
	StringOrView &infoString = floatingBox ? FloatingInfoString : InfoString;
	if (floatingBox)
		FloatingInfoStyles.push_back(CurrentInfoLineStyle);

	if (infoString.empty())
		infoString = str;
	else
		infoString = StrCat(infoString, "\n", str);
}

void AddInfoBoxString(std::string &&str, bool floatingBox /*= false*/)
{
	StringOrView &infoString = floatingBox ? FloatingInfoString : InfoString;
	if (floatingBox)
		FloatingInfoStyles.push_back(CurrentInfoLineStyle);

	if (infoString.empty())
		infoString = std::move(str);
	else
		infoString = StrCat(infoString, "\n", str);
}

void CheckPanelInfo()
{
	MainPanelFlag = false;
	InfoString = StringOrView {};
	FloatingInfoString = StringOrView {};

	const int totalButtons = IsChatAvailable() ? TotalMpMainPanelButtons : TotalSpMainPanelButtons;

	for (int i = 0; i < totalButtons; i++) {
		Rectangle button = MainPanelButtonRect[i];

		SetPanelObjectPosition(UiPanels::Main, button);

		if (button.contains(MousePosition)) {
			if (i != 7) {
				InfoString = _(PanBtnStr[i]);
			} else {
				if (MyPlayer->friendlyMode)
					InfoString = _("Player friendly");
				else
					InfoString = _("Player attack");
			}
			if (PanBtnHotKey[i] != nullptr) {
				AddInfoBoxString(FormatRuntime(_("Hotkey: {:s}"), _(PanBtnHotKey[i])));
			}
			InfoColor = UiFlags::ColorWhite;
			MainPanelFlag = true;
		}
	}

	Rectangle spellSelectButton = SpellButtonRect;

	SetPanelObjectPosition(UiPanels::Main, spellSelectButton);

	if (!SpellSelectFlag && spellSelectButton.contains(MousePosition)) {
		InfoString = _("Select current spell button");
		InfoColor = UiFlags::ColorWhite;
		MainPanelFlag = true;
		std::string_view speedbookKeyName = ControlMode == ControlTypes::Gamepad
		    ? GetOptions().Padmapper.InputNameForAction("DisplaySpells", true)
		    : GetOptions().Keymapper.KeyNameForAction("DisplaySpells");
		if (!speedbookKeyName.empty()) {
			AddInfoBoxString(FormatRuntime(_("Hotkey: '{:s}'"), speedbookKeyName));
		}
		const Player &myPlayer = *MyPlayer;
		const SpellID spellId = myPlayer._pRSpell;
		if (IsValidSpell(spellId)) {
			switch (myPlayer._pRSplType) {
			case SpellType::Skill:
				AddInfoBoxString(FormatRuntime(_("{:s} Skill"), pgettext("spell", GetSpellData(spellId).sNameText)));
				break;
			case SpellType::Spell: {
				AddInfoBoxString(FormatRuntime(_("{:s} Spell"), pgettext("spell", GetSpellData(spellId).sNameText)));
				const int spellLevel = myPlayer.GetSpellLevel(spellId);
				AddInfoBoxString(spellLevel == 0 ? _("Spell Level 0 - Unusable") : FormatRuntime(_("Spell Level {:d}"), spellLevel));
			} break;
			case SpellType::Scroll: {
				AddInfoBoxString(FormatRuntime(_("Scroll of {:s}"), pgettext("spell", GetSpellData(spellId).sNameText)));
				const int scrollCount = c_count_if(InventoryAndBeltPlayerItemsRange { myPlayer }, [spellId](const Item &item) {
					return item.isScrollOf(spellId);
				});
				AddInfoBoxString(FormatRuntime(ngettext("{:d} Scroll", "{:d} Scrolls", scrollCount), scrollCount));
			} break;
			case SpellType::Charges:
				AddInfoBoxString(FormatRuntime(_("Staff of {:s}"), pgettext("spell", GetSpellData(spellId).sNameText)));
				AddInfoBoxString(FormatRuntime(ngettext("{:d} Charge", "{:d} Charges", myPlayer.InvBody[INVLOC_HAND_LEFT]._iCharges), myPlayer.InvBody[INVLOC_HAND_LEFT]._iCharges));
				break;
			case SpellType::Invalid:
				break;
			}
		}
	}

	Rectangle belt = BeltRect;

	SetPanelObjectPosition(UiPanels::Main, belt);

	if (belt.contains(MousePosition))
		pcursinvitem = CheckInvHLight();

	if (CheckXPBarInfo())
		MainPanelFlag = true;
}

void DrawInfoBox(const Surface &out)
{
	DrawPanelBox(out, MakeSdlRect(InfoBoxRect.position.x, InfoBoxRect.position.y + PanelPaddingHeight, InfoBoxRect.size.width, InfoBoxRect.size.height), GetMainPanel().position + Displacement { InfoBoxRect.position.x, InfoBoxRect.position.y });
	const bool skillBarHovered = IsSkillBarHovered();
	if (!skillBarHovered && !MainPanelFlag && !trigflag && pcursinvitem == -1 && pcursstashitem == StashStruct::EmptyCell && pcursstoreitem == -1 && pcursstorebtn == -1 && !SpellSelectFlag && pcurs != CURSOR_HOURGLASS) {
		InfoString = StringOrView {};
		InfoColor = UiFlags::ColorWhite;
	}
	const Player &myPlayer = *MyPlayer;
	if (SpellSelectFlag || trigflag || pcurs == CURSOR_HOURGLASS) {
		InfoColor = UiFlags::ColorWhite;
	} else if (!myPlayer.HoldItem.isEmpty()) {
		if (myPlayer.HoldItem._itype == ItemType::Gold) {
			const int nGold = myPlayer.HoldItem._ivalue;
			InfoString = FormatRuntime(ngettext("{:s} gold piece", "{:s} gold pieces", nGold), FormatInteger(nGold));
		} else if (!myPlayer.CanUseItem(myPlayer.HoldItem)) {
			InfoString = _("Requirements not met");
		} else {
			InfoString = myPlayer.HoldItem.getName();
			InfoColor = myPlayer.HoldItem.getTextColor();
		}
	} else {
		if (pcursitem != -1) {
			GetItemStr(Items[pcursitem]);
			// POE2 Mashup: items on the ground get the full tooltip too.
			if (gbIsPoe2 && Items[pcursitem]._itype != ItemType::Gold) {
				FloatingInfoString = Items[pcursitem].getName();
				FloatingInfoStyles.clear();
				if (Items[pcursitem]._iIdentified)
					PrintItemDetails(Items[pcursitem]);
				else
					PrintItemDur(Items[pcursitem]);
				PrintItemComparison(Items[pcursitem]);
			} else if (gbIsPoe2) {
				FloatingInfoString = std::string(InfoString.str());
				FloatingInfoStyles.clear();
			}
		} else if (ObjectUnderCursor != nullptr)
			GetObjectStr(*ObjectUnderCursor);
		if (pcursmonst != -1) {
			if (leveltype != DTYPE_TOWN && gbIsPoe2) {
				// POE2 Mashup: the monster's name, life and modifiers are in the banner at the top of the screen.
			} else if (leveltype != DTYPE_TOWN) {
				const Monster &monster = Monsters[pcursmonst];
				InfoColor = UiFlags::ColorWhite;
				InfoString = monster.name();
				// POE2 Mashup: magic monsters show in blue and rares in yellow with their modifiers.
				const MonsterAffixes affixes = GetMonsterAffixes(monster);
				if (affixes.rarity == MonsterRarity::Rare) {
					InfoColor = UiFlags::ColorYellow;
					InfoString = RareMonsterName(monster);
					AddInfoBoxString(monster.name());
				} else if (affixes.rarity == MonsterRarity::Magic) {
					InfoColor = UiFlags::ColorBlue;
				}
				if (const std::optional<std::string> title = MapMonsterTitle(monster); title)
					AddInfoBoxString(*title);
				for (uint8_t mod = 0; mod < static_cast<uint8_t>(MonsterMod::Count); mod++) {
					if (affixes.has(static_cast<MonsterMod>(mod)))
						AddInfoBoxString(MonsterModName(static_cast<MonsterMod>(mod)));
				}
				if (monster.poe2.igniteTicks > 0)
					AddInfoBoxString(_("Ignited"));
				if (monster.poe2.shockTicks > 0)
					AddInfoBoxString(_("Shocked"));
				if (monster.poe2.freezeTicks > 0)
					AddInfoBoxString(_("Frozen"));
				else if (monster.poe2.chillTicks > 0)
					AddInfoBoxString(_("Chilled"));
				if (monster.poe2.heavyStunTicks > 0)
					AddInfoBoxString(_("Heavy Stunned"));
				if (monster.isUnique()) {
					InfoColor = UiFlags::ColorWhitegold;
					PrintUniqueHistory();
				} else {
					PrintMonstHistory(monster.type().type);
				}
			} else if (pcursitem == -1) {
				InfoString = std::string_view(Towners[pcursmonst].name);
			}
		}
		if (PlayerUnderCursor != nullptr) {
			InfoColor = UiFlags::ColorWhitegold;
			const auto &target = *PlayerUnderCursor;
			InfoString = std::string_view(target._pName);
			AddInfoBoxString(FormatRuntime(_("{:s}, Level: {:d}"), target.getClassName(), target.getCharacterLevel()));
			AddInfoBoxString(FormatRuntime(_("Hit Points {:d} of {:d}"), target._pHitPoints.whole(), target._pMaxHP.whole()));
		}
		if (PortraitIdUnderCursor != -1) {
			InfoColor = UiFlags::ColorWhitegold;
			auto &target = Players[PortraitIdUnderCursor];
			InfoString = std::string_view(target._pName);
			AddInfoBoxString(_("Right click to inspect"));
		}
	}
	if (skillBarHovered && myPlayer.HoldItem.isEmpty())
		SetSkillBarInfo();
	if (!InfoString.empty())
		PrintInfo(out);
}

void DrawFloatingInfoBox(const Surface &out)
{
	const bool groundItem = gbIsPoe2 && pcursitem != -1 && MyPlayer->HoldItem.isEmpty();
	if (pcursinvitem == -1 && pcursstashitem == StashStruct::EmptyCell && pcursstoreitem == -1 && pcursstorebtn == -1 && !groundItem) {
		FloatingInfoString = StringOrView {};
		InfoColor = UiFlags::ColorWhite;
	}

	if (!FloatingInfoString.empty()) {
		if (gbIsPoe2 && pcursstorebtn == -1)
			PrintPoe2ItemTooltip(out);
		else
			PrintFloatingInfo(out);
	}
	FloatingInfoStyles.clear();
}

} // namespace devilution
