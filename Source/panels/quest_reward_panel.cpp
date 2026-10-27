#include "panels/quest_reward_panel.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifdef USE_SDL1
#include "utils/sdl2_to_1_2_backports.h"
#endif

#include "control/control.hpp"
#include "diablo.h"
#include "diablo_msg.hpp"
#include "doom.h"
#include "engine/dx.h"
#include "game_mode.hpp"
#include "gmenu.h"
#include "help.h"
#include "inv.h"
#include "items.h"
#include "levels/gendung.h"
#include "minitext.h"
#include "msg.h"
#include "multi.h"
#include "panels/anvil_panel.hpp"
#include "panels/atlas_panel.hpp"
#include "panels/desecration_panel.hpp"
#include "panels/gemcutting_panel.hpp"
#include "panels/item_spawner.hpp"
#include "panels/passive_tree_panel.hpp"
#include "panels/poe2_widgets.hpp"
#include "panels/skill_bar.hpp"
#include "panels/stack_split_panel.hpp"
#include "panels/stash_tabs_panel.hpp"
#include "player.h"
#include "qol/stash.h"
#include "qol/visual_store.h"
#include "stores.h"
#include "utils/format.hpp"
#include "utils/language.h"
#include "utils/sdl_compat.h"

namespace devilution {

namespace {

using namespace poe2ui;

constexpr int Width = 380;
constexpr int Padding = 14;
constexpr int LineHeight = 18;
constexpr int ButtonHeight = 22;
/** Tooltip lines past this are left out, so a long unique still fits above the main panel. */
constexpr size_t MaxDetailLines = 16;

/** A reward, found again on the ground by its seed each time, in case it was picked up some other way. */
struct Offer {
	std::string towner;
	uint32_t seed;
	_item_indexes idx;
	uint16_t createInfo;
	/** The tile the hero was heading to when the popup opened: moving from it closes the popup. */
	Point spot;
	/** The item's tooltip below its name, worked out when the popup is first drawn. */
	std::optional<std::vector<std::pair<std::string, UiFlags>>> details;
};

std::vector<Offer> Pending;
std::optional<Offer> Open;

struct Layout {
	Rectangle panel;
	Rectangle title;
	Rectangle name;
	Rectangle details;
	Rectangle note;
	Rectangle putInBag;
	Rectangle leave;
};

int OfferedItemIndex(const Offer &offer)
{
	if (leveltype != DTYPE_TOWN)
		return -1;
	for (uint8_t i = 0; i < ActiveItemCount; i++) {
		const int ii = ActiveItems[i];
		const Item &item = Items[ii];
		if (!item.keyAttributesMatch(offer.seed, offer.idx, offer.createInfo) || !InDungeonBounds(item.position))
			continue;
		// Still lying where it landed, not picked up.
		if (dItem[item.position.x][item.position.y] == ii + 1)
			return ii;
	}
	return -1;
}

bool BagFull(const Player &player, const Item &item)
{
	return !CanFitItemInInventory(player, item) && !CanBePlacedOnBelt(player, item);
}

/** @brief As the item tooltip colours its lines. */
UiFlags DetailColor(InfoLineStyle style)
{
	switch (style) {
	case InfoLineStyle::Mod:
	case InfoLineStyle::Affix:
	case InfoLineStyle::AugmentedProperty:
		return UiFlags::ColorBlue;
	case InfoLineStyle::Corrupted:
	case InfoLineStyle::Worse:
		return UiFlags::ColorRed;
	case InfoLineStyle::Note:
		return UiFlags::ColorWhitegold;
	case InfoLineStyle::Flavour:
		return UiFlags::ColorGold;
	default:
		return UiFlags::ColorWhite;
	}
}

/** @brief The item's tooltip lines below its name, from the same code as the tooltip of an item under the mouse. */
std::vector<std::pair<std::string, UiFlags>> DetailLines(const Item &item)
{
	StringOrView savedFloating = std::move(FloatingInfoString);
	StringOrView savedInfo = std::move(InfoString);
	std::vector<InfoLineStyle> savedStyles = std::move(FloatingInfoStyles);
	const InfoLineStyle savedStyle = CurrentInfoLineStyle;
	FloatingInfoString = item.getName();
	InfoString = StringOrView {};
	FloatingInfoStyles.clear();
	if (item._iIdentified)
		PrintItemDetails(item);
	else
		PrintItemDur(item);
	const std::string text { FloatingInfoString.str() };
	const std::vector<InfoLineStyle> styles = std::move(FloatingInfoStyles);
	FloatingInfoString = std::move(savedFloating);
	InfoString = std::move(savedInfo);
	FloatingInfoStyles = std::move(savedStyles);
	CurrentInfoLineStyle = savedStyle;

	std::vector<std::pair<std::string, UiFlags>> lines;
	size_t start = text.find('\n');
	size_t index = 0;
	while (start != std::string::npos && lines.size() < MaxDetailLines) {
		const size_t end = text.find('\n', start + 1);
		std::string line = text.substr(start + 1, end == std::string::npos ? std::string::npos : end - start - 1);
		// An Alt view modifier line reads "side\tstat\ttier": the stat is enough here.
		if (const size_t tab = line.find('\t'); tab != std::string::npos) {
			const size_t next = line.find('\t', tab + 1);
			line = line.substr(tab + 1, next == std::string::npos ? std::string::npos : next - tab - 1);
		}
		if (!line.empty())
			lines.emplace_back(std::move(line), DetailColor(index < styles.size() ? styles[index] : InfoLineStyle::Default));
		index++;
		start = end;
	}
	return lines;
}

Layout ComputeLayout(Size screen, size_t detailCount, bool bagFull)
{
	const int detailsHeight = static_cast<int>(detailCount) * LineHeight;
	const int noteHeight = bagFull ? LineHeight : 0;
	const int height = Padding + 22 + 22 + (detailCount > 0 ? detailsHeight + 6 : 0) + noteHeight + 8 + ButtonHeight + Padding;
	const int width = std::min(Width, screen.width - 16);
	const int availableHeight = screen.height - GetMainPanel().size.height;
	Layout layout;
	layout.panel = { { (screen.width - width) / 2, std::max((availableHeight - height) / 2, 4) }, { width, height } };
	const int innerWidth = width - (2 * Padding);
	int y = layout.panel.position.y + Padding;
	layout.title = { { layout.panel.position.x + Padding, y }, { innerWidth, 20 } };
	y += 22;
	layout.name = { { layout.panel.position.x + Padding, y }, { innerWidth, 20 } };
	y += 22;
	layout.details = { { layout.panel.position.x + Padding, y }, { innerWidth, detailsHeight } };
	if (detailCount > 0)
		y += detailsHeight + 6;
	layout.note = { { layout.panel.position.x + Padding, y }, { innerWidth, noteHeight } };
	y += noteHeight + 8;
	const int buttonWidth = (innerWidth - Padding) / 2;
	layout.putInBag = { { layout.panel.position.x + Padding, y }, { buttonWidth, ButtonHeight } };
	layout.leave = { { layout.panel.position.x + width - Padding - buttonWidth, y }, { buttonWidth, ButtonHeight } };
	return layout;
}

/** @brief The open popup's layout and whether its bag is full; no layout once its item is gone. */
std::optional<std::pair<Layout, bool>> OpenLayout(Size screen)
{
	if (!Open || MyPlayer == nullptr)
		return std::nullopt;
	const int ii = OfferedItemIndex(*Open);
	if (ii < 0)
		return std::nullopt;
	const bool full = BagFull(*MyPlayer, Items[ii]);
	return std::make_pair(ComputeLayout(screen, Open->details ? Open->details->size() : 0, full), full);
}

std::optional<std::pair<Layout, bool>> ScreenLayout()
{
	const Surface out = GlobalBackBuffer();
	return OpenLayout({ out.w(), out.h() });
}

/** @brief Picks the ground item up the way a click with the inventory closed does, with the same net messages. */
void PutInBag()
{
	const int ii = Open ? OfferedItemIndex(*Open) : -1;
	if (ii >= 0 && MyPlayer != nullptr && !BagFull(*MyPlayer, Items[ii]))
		NetSendCmdGItem(true, CMD_REQUESTAGITEM, *MyPlayer, static_cast<uint8_t>(ii));
	CloseQuestReward();
}

void Act(QuestRewardInput input)
{
	const std::optional<std::pair<Layout, bool>> layout = ScreenLayout();
	if (!layout) {
		CloseQuestReward();
		return;
	}
	switch (QuestRewardActionFor(input, layout->second)) {
	case QuestRewardAction::PutInBag:
		PutInBag();
		break;
	case QuestRewardAction::LeaveOnGround:
		CloseQuestReward();
		break;
	default:
		break;
	}
}

void DrawButton(const Surface &out, const Rectangle &rect, std::string_view text, bool enabled)
{
	const bool hovered = enabled && rect.contains(MousePosition);
	FillRectangle(out, rect, !enabled ? SocketLockedColor : (hovered ? RowHoverColor : RowColor));
	DrawFrame(out, rect, !enabled ? SocketRimColor : (hovered ? HoverColor : InnerBorderColor));
	DrawString(out, text, rect, { .flags = (enabled ? UiFlags::ColorWhite : UiFlags::ColorUiSilverDark) | UiFlags::AlignCenter | UiFlags::VerticalCenter });
}

} // namespace

QuestRewardAction QuestRewardActionFor(QuestRewardInput input, bool bagFull)
{
	switch (input) {
	case QuestRewardInput::PutInBagButton:
	case QuestRewardInput::Enter:
		return bagFull ? QuestRewardAction::None : QuestRewardAction::PutInBag;
	case QuestRewardInput::LeaveButton:
	case QuestRewardInput::OutsidePanel:
	case QuestRewardInput::Escape:
	case QuestRewardInput::Moved:
		return QuestRewardAction::LeaveOnGround;
	default:
		return QuestRewardAction::None;
	}
}

void OfferQuestReward(std::string_view towner, int itemIndex)
{
	if (!gbIsPoe2 || gbIsMultiplayer || itemIndex < 0 || itemIndex >= MAXITEMS)
		return;
	const Item &item = Items[itemIndex];
	if (item.isEmpty())
		return;
	Offer offer {};
	offer.towner = std::string(towner);
	offer.seed = static_cast<uint32_t>(item._iSeed);
	offer.idx = item.IDidx;
	offer.createInfo = item._iCreateInfo;
	Pending.push_back(std::move(offer));
}

bool QuestRewardHeldBack()
{
	return qtextflag || IsPlayerInStore() || gmenu_is_active() || IsDiabloMsgAvailable() || DoomFlag || SpellSelectFlag || ChatFlag
	    || HelpFlag || DropGoldFlag || IsWithdrawGoldOpen || IsVisualStoreOpen || PauseMode != 0 || IsStackSplitOpen()
	    || IsDesecrationRevealOpen() || IsItemSpawnerOpen() || IsPassiveTreeOpen() || IsGemcuttingOpen() || IsAtlasOpen()
	    || IsAnvilOpen() || IsStashTabPopupOpen() || IsSkillTrayPopupOpen() || MyPlayer == nullptr || MyPlayer->hasNoLife();
}

void UpdateQuestRewardOffer()
{
	if (Open) {
		if (OfferedItemIndex(*Open) < 0 || MyPlayer == nullptr) {
			CloseQuestReward();
			return;
		}
		// Walking off leaves the reward on the ground, like "Leave on ground".
		if (MyPlayer->position.future != Open->spot) {
			CloseQuestReward();
			return;
		}
		// Something else came up on top, such as another towner's talk: the popup waits for it again.
		if (QuestRewardHeldBack()) {
			Pending.insert(Pending.begin(), std::move(*Open));
			Open = std::nullopt;
		}
		return;
	}
	// A reward already picked up, or left behind in town, needs no popup.
	std::erase_if(Pending, [](const Offer &offer) { return OfferedItemIndex(offer) < 0; });
	if (Pending.empty() || QuestRewardHeldBack())
		return;
	Open = std::move(Pending.front());
	Pending.erase(Pending.begin());
	Open->spot = MyPlayer->position.future;
}

bool IsQuestRewardOpen()
{
	return Open.has_value();
}

size_t PendingQuestRewardCount()
{
	return Pending.size();
}

void CloseQuestReward()
{
	Open = std::nullopt;
}

void ClearQuestRewards()
{
	Open = std::nullopt;
	Pending.clear();
}

bool QuestRewardHandleEvent(const SDL_Event &event)
{
	if (!Open)
		return false;
	switch (event.type) {
	case SDL_EVENT_KEY_DOWN: {
		const SDL_Keycode key = SDLC_EventKey(event);
		if (key == SDLK_ESCAPE) {
			Act(QuestRewardInput::Escape);
			return true;
		}
		if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
			Act(QuestRewardInput::Enter);
			return true;
		}
		// Every other key, like the movement keys, goes on to the game.
		return false;
	}
	case SDL_EVENT_MOUSE_BUTTON_DOWN: {
		const std::optional<std::pair<Layout, bool>> layout = ScreenLayout();
		if (!layout) {
			CloseQuestReward();
			return false;
		}
		const Point position { SDLC_EventButtonIntX(event), SDLC_EventButtonIntY(event) };
		if (!layout->first.panel.contains(position)) {
			// A click elsewhere closes the popup and goes on, so it can walk the hero off at once.
			Act(QuestRewardInput::OutsidePanel);
			return false;
		}
		if (event.button.button != SDL_BUTTON_LEFT)
			return true;
		if (layout->first.putInBag.contains(position))
			Act(QuestRewardInput::PutInBagButton);
		else if (layout->first.leave.contains(position))
			Act(QuestRewardInput::LeaveButton);
		else
			Act(QuestRewardInput::InsidePanel);
		return true;
	}
	default:
		return false;
	}
}

void DrawQuestReward(const Surface &out)
{
	std::optional<std::pair<Layout, bool>> found = OpenLayout({ out.w(), out.h() });
	if (!found) {
		CloseQuestReward();
		return;
	}
	const Item &item = Items[OfferedItemIndex(*Open)];
	if (!Open->details) {
		Open->details = DetailLines(item);
		found = OpenLayout({ out.w(), out.h() });
	}
	const Layout &layout = found->first;
	const bool bagFull = found->second;
	DrawPanel(out, layout.panel);
	DrawString(out, FormatRuntime(_("{:s} gives you:"), Open->towner), layout.title, { .flags = UiFlags::ColorUiGold | UiFlags::AlignCenter | UiFlags::VerticalCenter });
	DrawString(out, item.getName().str(), layout.name, { .flags = item.getTextColor() | UiFlags::AlignCenter | UiFlags::VerticalCenter });
	Rectangle line { layout.details.position, { layout.details.size.width, LineHeight } };
	for (const auto &[text, color] : *Open->details) {
		DrawString(out, text, line, { .flags = color | UiFlags::AlignCenter | UiFlags::VerticalCenter });
		line.position.y += LineHeight;
	}
	if (bagFull)
		DrawString(out, _("Bag is full"), layout.note, { .flags = UiFlags::ColorRed | UiFlags::AlignCenter | UiFlags::VerticalCenter });
	DrawButton(out, layout.putInBag, _("Put in bag"), !bagFull);
	DrawButton(out, layout.leave, _("Leave on ground"), true);
}

} // namespace devilution
