#include "panels/skills_panel.hpp"

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "control/control.hpp"
#include "cursor.h"
#include "effects.h"
#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "panels/gemcutting_panel.hpp"
#include "panels/poe2_widgets.hpp"
#include "panels/quest_log.hpp"
#include "panels/skill_bar.hpp"
#include "panels/spell_icons.hpp"
#include "player.h"
#include "plrmsg.h"
#include "poe2/skill_gems.hpp"
#include "poe2/spirit.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/weapon_skills.hpp"
#include "qol/stash.h"
#include "qol/visual_store.h"
#include "utils/format.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

using namespace poe2ui;

constexpr int HeaderHeight = 52;
constexpr int RowHeight = 48;
constexpr int RowGap = 2;
/** @brief The 12 sockets show 6 to a page. */
constexpr size_t RowsPerPage = 6;
constexpr size_t PageCount = NumSkillSlots / RowsPerPage;
constexpr Size IconSize { 37, 38 };
constexpr int SocketSize = 28;
constexpr int SocketGap = 4;

bool IsOpen;
size_t Page;

struct Hit {
	enum class Kind : uint8_t {
		None,
		Skill,
		Socket,
		GemcuttingButton,
		PageTab,
	} kind
	    = Kind::None;
	size_t slot = 0;
	size_t socket = 0;
};

Rectangle PanelRect()
{
	return GetLeftPanel();
}

Rectangle GemcuttingButtonRect()
{
	const Rectangle panel = PanelRect();
	return { panel.position + Displacement { panel.size.width - 104, 8 }, { 96, 20 } };
}

Rectangle PageTabRect(size_t page)
{
	const Rectangle panel = PanelRect();
	return { panel.position + Displacement { 84 + (static_cast<int>(page) * 58), 8 }, { 54, 20 } };
}

Rectangle RowRect(size_t slot)
{
	const Rectangle panel = PanelRect();
	const int row = static_cast<int>(slot % RowsPerPage);
	return { panel.position + Displacement { 6, HeaderHeight + (row * (RowHeight + RowGap)) }, { panel.size.width - 12, RowHeight } };
}

Rectangle IconRect(size_t slot)
{
	const Rectangle row = RowRect(slot);
	return { row.position + Displacement { 4, (RowHeight - IconSize.height) / 2 }, IconSize };
}

Rectangle SocketRect(size_t slot, size_t socket)
{
	const Rectangle row = RowRect(slot);
	return { row.position + Displacement { 48 + (static_cast<int>(socket) * (SocketSize + SocketGap)), RowHeight - SocketSize - 3 }, { SocketSize, SocketSize } };
}

Hit HitAt(Point position)
{
	if (GemcuttingButtonRect().contains(position))
		return { Hit::Kind::GemcuttingButton };
	for (size_t page = 0; page < PageCount; page++) {
		if (PageTabRect(page).contains(position))
			return { Hit::Kind::PageTab, page };
	}
	for (size_t slot = Page * RowsPerPage; slot < (Page + 1) * RowsPerPage; slot++) {
		if (!RowRect(slot).contains(position))
			continue;
		if (IconRect(slot).contains(position))
			return { Hit::Kind::Skill, slot };
		for (size_t socket = 0; socket < MaxSupportSockets; socket++) {
			if (SocketRect(slot, socket).contains(position))
				return { Hit::Kind::Socket, slot, socket };
		}
		return {};
	}
	return {};
}

const Item &SupportGemItem(SupportGem gem)
{
	static std::array<Item, static_cast<size_t>(SupportGem::Count)> items;
	static std::array<size_t, static_cast<size_t>(SupportGem::Count)> itemCounts {};
	const auto index = static_cast<size_t>(gem);
	// The item table can be reloaded when mods change, so remake the item when it is.
	if (itemCounts[index] != AllItemsList.size()) {
		items[index] = {};
		CreateSupportGemItem(items[index], gem);
		items[index]._iStatFlag = true;
		itemCounts[index] = AllItemsList.size();
	}
	return items[index];
}

std::string SkillLevelText(const Player &player, SkillSlot skill)
{
	if (skill.kind == SkillKind::Spell)
		return FormatRuntime(_("Level {:d}"), player.GetSpellLevel(skill.spell()));
	return FormatRuntime(_("Level {:d}"), player.poe2.skillLevel(skill));
}

std::string SkillCostText(const Player &player, SkillSlot skill)
{
	if (skill.kind == SkillKind::Persistent)
		return FormatRuntime(_("Spirit {:d}"), SpiritReservation(skill.persistent()));
	return FormatRuntime(_("Mana {:d}"), SkillManaCost(player, skill));
}

void DrawHeader(const Surface &out, const Player &player, const Hit &hover)
{
	const Rectangle panel = PanelRect();
	DrawString(out, _("Skills"), { panel.position + Displacement { 10, 8 }, { 70, 20 } }, { .flags = UiFlags::FontSize24 | UiFlags::ColorUiGold | UiFlags::VerticalCenter });
	for (size_t page = 0; page < PageCount; page++) {
		const Rectangle tab = PageTabRect(page);
		const bool current = page == Page;
		FillRectangle(out, tab, current || (hover.kind == Hit::Kind::PageTab && hover.slot == page) ? RowHoverColor : RowColor);
		DrawFrame(out, tab, current ? BorderColor : InnerBorderColor);
		const int first = static_cast<int>(page * RowsPerPage) + 1;
		DrawString(out, FormatRuntime("{:d}-{:d}", first, first + static_cast<int>(RowsPerPage) - 1), tab,
		    { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | (current ? UiFlags::ColorWhitegold : UiFlags::ColorWhite) });
	}

	const Rectangle button = GemcuttingButtonRect();
	FillRectangle(out, button, hover.kind == Hit::Kind::GemcuttingButton ? RowHoverColor : RowColor);
	DrawFrame(out, button, BorderColor);
	DrawString(out, _("Gemcutting"), button, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhitegold });

	Rectangle line { panel.position + Displacement { 10, 32 }, { panel.size.width - 20, 16 } };
	DrawString(out, _("Support capacity:"), line, { .flags = UiFlags::ColorWhite | UiFlags::VerticalCenter });
	int x = 128;
	for (const GemColor color : { GemColor::Strength, GemColor::Dexterity, GemColor::Magic }) {
		const int used = SupportsUsed(player.poe2, color);
		const int capacity = SupportCapacity(player, color);
		const Rectangle cell { panel.position + Displacement { x, 32 }, { 60, 16 } };
		DrawString(out, FormatRuntime("{:s} {:d}/{:d}", GemColorName(color), used, capacity), cell,
		    { .flags = (used >= capacity ? UiFlags::ColorWhitegold : GemTextColor(color)) | UiFlags::VerticalCenter });
		x += 62;
	}
}

void DrawRow(const Surface &out, const Player &player, size_t slot, const Hit &hover)
{
	const Rectangle row = RowRect(slot);
	const SkillSlot skill = player.poe2.skillSlots[slot];
	FillRectangle(out, row, hover.kind != Hit::Kind::None && hover.slot == slot ? RowHoverColor : RowColor);
	DrawFrame(out, row, InnerBorderColor);

	const Rectangle icon = IconRect(slot);
	DrawHalfTransparentRectTo(out, icon.position.x, icon.position.y, icon.size.width, icon.size.height);
	const Point iconBottomLeft { icon.position.x, icon.position.y + icon.size.height - 1 };
	DrawSkillIcon(out, iconBottomLeft, skill, GetSkillUsability(player, skill) == SkillUsability::Ready);
	if (hover.kind == Hit::Kind::Skill && hover.slot == slot)
		DrawSmallSpellIconBorder(out, iconBottomLeft);
	const std::string key = SkillSlotKeyName(slot);
	if (!key.empty() && key.length() <= 3)
		DrawString(out, key, { icon.position + Displacement { 2, 1 }, { icon.size.width - 4, 12 } }, { .flags = UiFlags::ColorWhite | UiFlags::Outlined });

	const Rectangle title { row.position + Displacement { 48, 2 }, { row.size.width - 52, 14 } };
	if (skill.isEmpty()) {
		DrawString(out, _("Empty: drop a skill gem here"), title, { .flags = UiFlags::ColorButtonpushed | UiFlags::VerticalCenter });
		return;
	}
	const SkillUsability usability = GetSkillUsability(player, skill);
	const UiFlags nameColor = IsAnyOf(usability, SkillUsability::Requirements, SkillUsability::Spirit) ? UiFlags::ColorRed
	    : skill.kind == SkillKind::Spell                                                               ? UiFlags::ColorBlue
	                                                                                                   : UiFlags::ColorWhitegold;
	DrawString(out, SkillName(skill), title, { .flags = nameColor | UiFlags::VerticalCenter });
	DrawString(out, StrCat(SkillLevelText(player, skill), "   ", SkillCostText(player, skill)), title, { .flags = UiFlags::AlignRight | UiFlags::ColorWhite | UiFlags::VerticalCenter });

	if (!SkillTakesSupports(skill)) {
		DrawString(out, _("Spirit gems take no supports"), { row.position + Displacement { 48, 20 }, { row.size.width - 52, 24 } },
		    { .flags = UiFlags::ColorButtonpushed | UiFlags::VerticalCenter });
		return;
	}

	const size_t open = SupportSocketCount(player);
	const SupportGem held = SupportGemForItem(player.HoldItem);
	for (size_t socket = 0; socket < MaxSupportSockets; socket++) {
		const Rectangle rect = SocketRect(slot, socket);
		const Point center = rect.Center();
		const SupportGem gem = player.poe2.slotSupports[slot][socket];
		const bool hovered = hover.kind == Hit::Kind::Socket && hover.slot == slot && hover.socket == socket;
		if (socket >= open) {
			FillCircle(out, center, SocketSize / 2 - 1, SocketLockedColor);
			continue;
		}
		uint8_t rim = SocketRimColor;
		if (gem != SupportGem::None)
			rim = GemPaletteColor(SupportGemColor(gem));
		else if (held != SupportGem::None && SupportGemCanSupport(held, skill))
			rim = GemPaletteColor(SupportGemColor(held));
		FillCircle(out, center, SocketSize / 2, hovered ? HoverColor : rim);
		FillCircle(out, center, SocketSize / 2 - 2, SocketColor);
		if (gem != SupportGem::None)
			DrawSupportGemIcon(out, rect, gem);
	}
}

void DrawHoverTooltip(const Surface &out, const Player &player, const Hit &hover)
{
	const Rectangle bounds { { 0, 0 }, { out.w(), out.h() } };
	std::vector<std::pair<std::string, UiFlags>> lines;
	if (hover.kind == Hit::Kind::GemcuttingButton) {
		lines.emplace_back(_("Browse every skill and support gem.\nUse an uncut gem to cut one."), UiFlags::ColorWhite);
		DrawTooltip(out, bounds, MousePosition, std::string(_("Gemcutting")), UiFlags::ColorUiGold, lines);
		return;
	}
	if (hover.kind == Hit::Kind::PageTab)
		return;
	const SkillSlot skill = player.poe2.skillSlots[hover.slot];
	const std::string key = SkillSlotKeyName(hover.slot);
	if (hover.kind == Hit::Kind::Skill) {
		if (skill.isEmpty()) {
			lines.emplace_back(_("Click with a skill gem to socket it."), UiFlags::ColorWhite);
			DrawTooltip(out, bounds, MousePosition, FormatRuntime(_("Empty skill socket {:d}"), static_cast<int>(hover.slot) + 1), UiFlags::ColorWhite, lines);
			return;
		}
		if (skill.kind == SkillKind::Attack)
			lines.emplace_back(std::string(WeaponSkillDescription(skill.attack())), UiFlags::ColorWhite);
		else if (skill.kind == SkillKind::Persistent)
			lines.emplace_back(std::string(PersistentSkillDescription(skill.persistent())), UiFlags::ColorWhite);
		else
			lines.emplace_back(FormatRuntime(_("Spell level {:d}"), player.GetSpellLevel(skill.spell())), UiFlags::ColorWhite);
		if (skill.kind == SkillKind::Attack && WeaponSkillNeedsWeapon(skill.attack()))
			lines.emplace_back(FormatRuntime(_("Needs a {:s}"), WeaponSkillWeapons(skill.attack())),
			    WeaponAllowsSkill(player, skill.attack()) ? UiFlags::ColorWhite : UiFlags::ColorRed);
		if (GetSkillUsability(player, skill) == SkillUsability::Requirements)
			lines.emplace_back(MissingSkillRequirements(player, skill), UiFlags::ColorRed);
		if (skill.kind != SkillKind::Persistent && key.empty())
			lines.emplace_back(_("Not on the skill tray: click a tray slot to put it there"), UiFlags::ColorWhite);
		lines.emplace_back(_("Click to take the gem out"), UiFlags::ColorButtonpushed);
		DrawTooltip(out, bounds, MousePosition, key.empty() ? std::string(SkillName(skill)) : FormatRuntime("{:s} [{:s}]", SkillName(skill), key), UiFlags::ColorWhitegold, lines);
		return;
	}

	const size_t open = SupportSocketCount(player);
	const SupportGem gem = player.poe2.slotSupports[hover.slot][hover.socket];
	const SupportGem held = SupportGemForItem(player.HoldItem);
	if (hover.socket >= open) {
		lines.emplace_back(FormatRuntime(_("Opens at character level {:d}"), static_cast<int>((hover.socket - 1) * 10)), UiFlags::ColorWhite);
		DrawTooltip(out, bounds, MousePosition, std::string(_("Locked socket")), UiFlags::ColorButtonpushed, lines);
		return;
	}
	if (gem != SupportGem::None) {
		const GemColor color = SupportGemColor(gem);
		const int tier = player.poe2.socketTier(hover.slot, hover.socket);
		lines.emplace_back(std::string(SupportGemDescription(gem, tier)), UiFlags::ColorWhite);
		lines.emplace_back(FormatRuntime(_("{:s} support"), GemColorName(color)), GemTextColor(color));
		lines.emplace_back(held != SupportGem::None ? _("Click to swap in the held gem") : _("Click to take it out"), UiFlags::ColorButtonpushed);
		DrawTooltip(out, bounds, MousePosition, SupportGemTierName(gem, tier), GemTextColor(color), lines);
		return;
	}
	if (skill.isEmpty() || !SkillTakesSupports(skill))
		return;
	if (held != SupportGem::None) {
		const bool fits = SupportGemCanSupport(held, skill);
		const std::string heldName = SupportGemTierName(held, SupportGemItemTier(player.HoldItem));
		lines.emplace_back(fits ? FormatRuntime(_("Click to socket {:s}"), heldName) : FormatRuntime(_("{:s} can't support {:s}"), heldName, SkillName(skill)),
		    fits ? UiFlags::ColorWhite : UiFlags::ColorRed);
	} else {
		lines.emplace_back(_("Click with a support gem to socket it.\nSupports change how the skill works."), UiFlags::ColorWhite);
	}
	DrawTooltip(out, bounds, MousePosition, std::string(_("Empty support socket")), UiFlags::ColorWhite, lines);
}

void HoldGem(Player &player, Item &&item)
{
	PlaySFX(ItemInvSnds[ItemCAnimTbl[item._iCurs]]);
	player.HoldItem = std::move(item);
	NewCursor(player.HoldItem);
}

void ClickSocket(Player &player, size_t slot, size_t socket)
{
	if (!player.HoldItem.isEmpty()) {
		if (SupportGemForItem(player.HoldItem) == SupportGem::None) {
			EventPlrMsg(_("Only support gems go in support sockets."), UiFlags::ColorRed);
			return;
		}
		SocketHeldSupportGem(player, slot, socket);
		return;
	}
	int tier = 1;
	const SupportGem gem = UnsocketSupportGem(player.poe2, slot, socket, &tier);
	Item item;
	if (gem != SupportGem::None && CreateSupportGemItem(item, gem, tier))
		HoldGem(player, std::move(item));
}

} // namespace

bool IsSkillsPanelOpen()
{
	return IsOpen;
}

void OpenSkillsPanel()
{
	if (!gbIsPoe2 || MyPlayer == nullptr)
		return;
	CloseCharPanel();
	QuestLogIsOpen = false;
	CloseGoldWithdraw();
	CloseStash();
	CloseVisualStore();
	IsOpen = true;
}

void CloseSkillsPanel()
{
	IsOpen = false;
}

void ToggleSkillsPanel()
{
	if (IsOpen)
		CloseSkillsPanel();
	else
		OpenSkillsPanel();
}

void DrawSkillsPanel(const Surface &out)
{
	if (!IsOpen || MyPlayer == nullptr)
		return;
	const Player &player = *MyPlayer;
	const Hit hover = PanelRect().contains(MousePosition) ? HitAt(MousePosition) : Hit {};
	DrawPanel(out, PanelRect());
	DrawHeader(out, player, hover);
	for (size_t slot = Page * RowsPerPage; slot < (Page + 1) * RowsPerPage; slot++)
		DrawRow(out, player, slot, hover);
	if (hover.kind != Hit::Kind::None)
		DrawHoverTooltip(out, player, hover);
}

void HandleSkillsPanelClick(Point mousePosition)
{
	Player &player = *MyPlayer;
	const Hit hit = HitAt(mousePosition);
	switch (hit.kind) {
	case Hit::Kind::GemcuttingButton:
		OpenGemcuttingBrowser();
		break;
	case Hit::Kind::PageTab:
		Page = hit.slot;
		break;
	case Hit::Kind::Skill:
		ClickSkillSlot(player, hit.slot, /*canTakeOut=*/true);
		break;
	case Hit::Kind::Socket:
		if (hit.socket < SupportSocketCount(player) && SkillTakesSupports(player.poe2.skillSlots[hit.slot]))
			ClickSocket(player, hit.slot, hit.socket);
		break;
	default:
		break;
	}
}

uint8_t GemPaletteColor(GemColor color)
{
	switch (color) {
	case GemColor::Strength:
		return PAL16_RED + 5;
	case GemColor::Dexterity:
		return PAL16_YELLOW + 4;
	default:
		return PAL16_BLUE + 5;
	}
}

UiFlags GemTextColor(GemColor color)
{
	switch (color) {
	case GemColor::Strength:
		return UiFlags::ColorRed;
	case GemColor::Dexterity:
		return UiFlags::ColorYellow;
	default:
		return UiFlags::ColorBlue;
	}
}

void DrawSupportGemIcon(const Surface &out, const Rectangle &socket, SupportGem gem)
{
	const Item &item = SupportGemItem(gem);
	if (item.isEmpty())
		return;
	const int cursId = item._iCurs + CURSOR_FIRSTITEM;
	const Size size = GetInvItemSize(cursId);
	const Point bottomLeft { socket.position.x + ((socket.size.width - size.width) / 2), socket.position.y + ((socket.size.height + size.height) / 2) - 1 };
	DrawItem(item, out, bottomLeft, GetInvItemSprite(cursId));
}

} // namespace devilution
