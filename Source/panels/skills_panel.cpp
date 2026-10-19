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
#include "poe2/currency.hpp"
#include "poe2/default_attack.hpp"
#include "poe2/skill_details.hpp"
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
/** The socket whose details pane is open, POE2's skill details pop-out. */
std::optional<size_t> DetailsSlot;
constexpr int DetailsWidth = 340;
constexpr int DetailsLineHeight = 16;
/** The frame of a Spirit gem that is switched on. */
constexpr uint8_t SpiritOnColor = PAL16_ORANGE + 2;

struct Hit {
	enum class Kind : uint8_t {
		None,
		Skill,
		Socket,
		GemcuttingButton,
		PageTab,
		/** The rest of a skill's row: opens its details. */
		Row,
		/** A minion gem's [-] and [+], POE2's Set Minion Count. */
		CountMinus,
		CountPlus,
	} kind
	    = Kind::None;
	size_t slot = 0;
	size_t socket = 0;
};

Rectangle PanelRect()
{
	// One row taller than the other left panels, for the weapon's default attack; up a little if the screen is short.
	const Rectangle left = GetLeftPanel();
	const int height = left.size.height + RowHeight + RowGap;
	const int top = std::max(0, std::min(left.position.y, GetMainPanel().position.y - height));
	return { { left.position.x, top }, { left.size.width, height } };
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
	// The default attack's row is first; the page of gem sockets follows.
	const int row = slot == DefaultAttackRow ? 0 : static_cast<int>(slot % RowsPerPage) + 1;
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

/** @brief A minion gem's [-] or [+], at the right of the row's first line. */
Rectangle CountButtonRect(size_t slot, bool plus)
{
	const Rectangle row = RowRect(slot);
	return { row.position + Displacement { row.size.width - (plus ? 18 : 50), 1 }, { 14, 15 } };
}

bool RowHasMinionCount(const Player &player, size_t slot)
{
	return slot != DefaultAttackRow && IsMinionCountSkill(RowSkill(player, slot).persistent());
}

/** @brief The rows shown: the default attack's, then the current page of gem sockets. */
std::array<size_t, RowsPerPage + 1> VisibleRows()
{
	std::array<size_t, RowsPerPage + 1> rows {};
	rows[0] = DefaultAttackRow;
	for (size_t i = 0; i < RowsPerPage; i++)
		rows[i + 1] = (Page * RowsPerPage) + i;
	return rows;
}

Hit HitAt(Point position)
{
	if (GemcuttingButtonRect().contains(position))
		return { Hit::Kind::GemcuttingButton };
	for (size_t page = 0; page < PageCount; page++) {
		if (PageTabRect(page).contains(position))
			return { Hit::Kind::PageTab, page };
	}
	for (size_t slot : VisibleRows()) {
		if (!RowRect(slot).contains(position))
			continue;
		if (IconRect(slot).contains(position))
			return { Hit::Kind::Skill, slot };
		if (MyPlayer != nullptr && RowHasMinionCount(*MyPlayer, slot)) {
			if (CountButtonRect(slot, false).contains(position))
				return { Hit::Kind::CountMinus, slot };
			if (CountButtonRect(slot, true).contains(position))
				return { Hit::Kind::CountPlus, slot };
		}
		for (size_t socket = 0; socket < MaxSupportSockets; socket++) {
			if (SocketRect(slot, socket).contains(position))
				return { Hit::Kind::Socket, slot, socket };
		}
		return { Hit::Kind::Row, slot };
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
	if (IsMinionCountSkill(skill.persistent()))
		return FormatRuntime(_("Spirit {:d}"), GemSpiritReservation(player, skill.persistent()));
	if (skill.kind == SkillKind::Persistent)
		return FormatRuntime(_("Spirit {:d}"), GemSpiritCost(player, skill.persistent()));
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
}

void DrawMinionCount(const Surface &out, const Player &player, size_t slot, const Hit &hover)
{
	const PersistentSkill skill = RowSkill(player, slot).persistent();
	const int chosen = ChosenMinionCount(player, skill);
	const int active = ActiveMinionCount(player, skill);
	const int affordable = AffordableMinionCount(player, skill);
	for (const bool plus : { false, true }) {
		const Rectangle button = CountButtonRect(slot, plus);
		const bool usable = plus ? chosen < affordable : chosen > 0;
		const bool hovered = hover.kind == (plus ? Hit::Kind::CountPlus : Hit::Kind::CountMinus) && hover.slot == slot;
		FillRectangle(out, button, hovered && usable ? RowHoverColor : RowColor);
		DrawFrame(out, button, usable ? BorderColor : InnerBorderColor);
		DrawString(out, plus ? "+" : "-", button, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | (usable ? UiFlags::ColorWhitegold : UiFlags::ColorButtonpushed) });
	}
	const Rectangle number { CountButtonRect(slot, false).position + Displacement { 14, 0 }, { 18, 15 } };
	// Red when Spirit runs short of the count chosen, as the gem then keeps fewer.
	DrawString(out, StrCat(std::min(active, chosen)), number, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | (active < chosen ? UiFlags::ColorRed : UiFlags::ColorWhite) });
}

void DrawRow(const Surface &out, const Player &player, size_t slot, const Hit &hover)
{
	const Rectangle row = RowRect(slot);
	const SkillSlot skill = RowSkill(player, slot);
	const bool isDefault = slot == DefaultAttackRow;
	FillRectangle(out, row, hover.kind != Hit::Kind::None && hover.slot == slot ? RowHoverColor : RowColor);
	DrawFrame(out, row, isDefault ? BorderColor : InnerBorderColor);

	const Rectangle icon = IconRect(slot);
	DrawHalfTransparentRectTo(out, icon.position.x, icon.position.y, icon.size.width, icon.size.height);
	const Point iconBottomLeft { icon.position.x, icon.position.y + icon.size.height - 1 };
	if (isDefault) {
		// The word may be a little wider than the icon; it can spill into the margins.
		const Rectangle label { icon.position - Displacement { 4, 0 }, { icon.size.width + 8, icon.size.height } };
		DrawString(out, DefaultAttackLabel(DefaultAttackFor(player)), label, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhitegold | UiFlags::FontSize12, .spacing = 0 });
	} else {
		const bool spiritOff = skill.kind == SkillKind::Persistent && !player.poe2.spiritGemOn(slot);
		DrawSkillIcon(out, iconBottomLeft, skill, GetSkillUsability(player, skill) == SkillUsability::Ready && !spiritOff);
		// A Spirit gem that is switched on wears a bright frame.
		if (skill.kind == SkillKind::Persistent && !spiritOff) {
			DrawFrame(out, icon, SpiritOnColor);
			DrawFrame(out, { icon.position + Displacement { 1, 1 }, { icon.size.width - 2, icon.size.height - 2 } }, SpiritOnColor);
		}
	}
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
	// A minion gem shows its count there instead; its level and Spirit are in the count's tooltip and the details.
	if (!RowHasMinionCount(player, slot)) {
		std::string right = isDefault ? std::string(_("No mana")) : StrCat(SkillLevelText(player, skill), "   ", SkillCostText(player, skill));
		// A long name leaves room for the cost alone.
		if (!isDefault && GetLineWidth(SkillName(skill)) + GetLineWidth(right) + 8 > title.size.width)
			right = SkillCostText(player, skill);
		DrawString(out, right, title, { .flags = UiFlags::AlignRight | UiFlags::ColorWhite | UiFlags::VerticalCenter });
	}
	// The arrow that opens the skill's details, as in POE2's Skills panel.
	const bool showing = DetailsSlot == slot;
	const Rectangle arrow { row.position + Displacement { row.size.width - 52, 20 }, { 48, 24 } };
	DrawString(out, showing ? _("Details <") : _("Details >"), arrow,
	    { .flags = UiFlags::AlignRight | UiFlags::VerticalCenter | (showing || (hover.kind == Hit::Kind::Row && hover.slot == slot) ? UiFlags::ColorWhitegold : UiFlags::ColorButtonpushed) });

	if (!SkillTakesSupports(skill)) {
		const bool on = skill.kind == SkillKind::Persistent && player.poe2.spiritGemOn(slot);
		// Stops short of the Details arrow.
		DrawString(out, on ? _("On: right-click to turn off") : _("Off: right-click to turn on"), { row.position + Displacement { 48, 20 }, { arrow.position.x - row.position.x - 52, 24 } },
		    { .flags = (on ? UiFlags::ColorWhite : UiFlags::ColorButtonpushed) | UiFlags::VerticalCenter });
		return;
	}

	const size_t open = SupportSocketCount(player, slot);
	const SupportGem held = SupportGemForItem(player.HoldItem);
	for (size_t socket = 0; socket < MaxSupportSockets; socket++) {
		const Rectangle rect = SocketRect(slot, socket);
		const Point center = rect.Center();
		const SupportGem gem = player.poe2.supportsAt(slot)[socket];
		const bool hovered = hover.kind == Hit::Kind::Socket && hover.slot == slot && hover.socket == socket;
		if (socket >= open) {
			FillCircle(out, center, SocketSize / 2 - 1, SocketLockedColor);
			continue;
		}
		uint8_t rim = SocketRimColor;
		if (gem != SupportGem::None && !SupportGemCanSupport(gem, skill))
			rim = SocketLockedColor; // kept from another weapon's attack; it rests until a weapon it fits
		else if (gem != SupportGem::None)
			rim = GemPaletteColor(SupportGemColor(gem));
		else if (held != SupportGem::None && SupportGemCanSupport(held, skill))
			rim = GemPaletteColor(SupportGemColor(held));
		FillCircle(out, center, SocketSize / 2, hovered ? HoverColor : rim);
		FillCircle(out, center, SocketSize / 2 - 2, SocketColor);
		if (gem != SupportGem::None)
			DrawSupportGemIcon(out, rect, gem);
	}

	if (RowHasMinionCount(player, slot))
		DrawMinionCount(out, player, slot, hover);
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
	if (hover.kind == Hit::Kind::PageTab || hover.kind == Hit::Kind::Row)
		return;
	const SkillSlot skill = RowSkill(player, hover.slot);
	if (IsAnyOf(hover.kind, Hit::Kind::CountMinus, Hit::Kind::CountPlus)) {
		const PersistentSkill persistent = skill.persistent();
		const int chosen = ChosenMinionCount(player, persistent);
		const int active = ActiveMinionCount(player, persistent);
		const int perMinion = SpiritPerMinion(player, persistent);
		if (const int free = FreeMinions(persistent); free > 0)
			lines.emplace_back(FormatRuntime(_("The first {:d} reserve no Spirit; each one more reserves {:d}."), free, perMinion), UiFlags::ColorWhite);
		else
			lines.emplace_back(FormatRuntime(_("Each one reserves {:d} Spirit."), perMinion), UiFlags::ColorWhite);
		lines.emplace_back(FormatRuntime(_("Gem level {:d}: minions fight at level {:d}."), player.poe2.skillLevel(persistent), MinionLevelForGemLevel(player.poe2.skillLevel(persistent))), UiFlags::ColorWhite);
		lines.emplace_back(FormatRuntime(_("Reserving {:d} Spirit; {:d} of {:d} left."), GemSpiritReservation(player, persistent), AvailableSpirit(player), MaxSpirit(player)), UiFlags::ColorWhite);
		if (active < chosen)
			lines.emplace_back(FormatRuntime(_("Not enough Spirit for {:d}: raising {:d}."), chosen, active), UiFlags::ColorRed);
		lines.emplace_back(FormatRuntime(_("Fallen minions rise again after {:d} seconds."), SkeletonReviveTicks / 20), UiFlags::ColorWhite);
		lines.emplace_back(hover.kind == Hit::Kind::CountPlus ? _("Click for one more") : _("Click for one fewer"), UiFlags::ColorButtonpushed);
		DrawTooltip(out, bounds, MousePosition, FormatRuntime(_("{:s}: {:d} minions"), SkillName(skill), chosen), UiFlags::ColorWhitegold, lines);
		return;
	}
	const std::string key = hover.slot == DefaultAttackRow ? std::string {} : SkillSlotKeyName(hover.slot);
	if (hover.kind == Hit::Kind::Skill && hover.slot == DefaultAttackRow) {
		lines.emplace_back(_("Your weapon's own attack: it costs no mana and changes with the weapon."), UiFlags::ColorWhite);
		lines.emplace_back(_("Supports socketed here stay when you change weapons; any that don't fit the new attack rest until they do."), UiFlags::ColorWhite);
		lines.emplace_back(_("Put it on the skill tray by clicking a tray slot."), UiFlags::ColorButtonpushed);
		DrawTooltip(out, bounds, MousePosition, std::string(SkillName(skill)), UiFlags::ColorWhitegold, lines);
		return;
	}
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
		if (skill.kind == SkillKind::Persistent) {
			if (player.poe2.spiritGemOn(hover.slot))
				lines.emplace_back(_("On. Right-click to switch it off"), UiFlags::ColorWhite);
			else
				lines.emplace_back(_("Off: it reserves no Spirit. Right-click to switch it on"), UiFlags::ColorRed);
		}
		lines.emplace_back(_("Click to take the gem out"), UiFlags::ColorButtonpushed);
		DrawTooltip(out, bounds, MousePosition, key.empty() ? std::string(SkillName(skill)) : FormatRuntime("{:s} [{:s}]", SkillName(skill), key), UiFlags::ColorWhitegold, lines);
		return;
	}

	const size_t open = SupportSocketCount(player, hover.slot);
	const SupportGem gem = player.poe2.supportsAt(hover.slot)[hover.socket];
	const SupportGem held = SupportGemForItem(player.HoldItem);
	if (hover.socket >= open) {
		lines.emplace_back(JewellerSocketHint(hover.socket), UiFlags::ColorWhite);
		DrawTooltip(out, bounds, MousePosition, std::string(_("Locked socket")), UiFlags::ColorButtonpushed, lines);
		return;
	}
	if (gem != SupportGem::None) {
		const GemColor color = SupportGemColor(gem);
		const int tier = player.poe2.socketTier(hover.slot, hover.socket);
		lines.emplace_back(std::string(SupportGemDescription(gem, tier)), UiFlags::ColorWhite);
		lines.emplace_back(FormatRuntime(_("{:s} support"), GemColorName(color)), GemTextColor(color));
		if (!SupportGemCanSupport(gem, skill))
			lines.emplace_back(FormatRuntime(_("Resting: it doesn't fit {:s}"), SkillName(skill)), UiFlags::ColorRed);
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

Rectangle DetailsRect(const std::vector<SkillDetailLine> &lines)
{
	const Rectangle panel = PanelRect();
	int height = 40;
	for (const SkillDetailLine &line : lines) {
		if (line.kind == SkillDetailLine::Kind::Heading)
			height += 24;
		else if (line.kind == SkillDetailLine::Kind::Value)
			height += DetailsLineHeight;
		else
			height += DetailsLineHeight * static_cast<int>(std::count(line.label.begin(), line.label.end(), '\n') + 1);
	}
	height = std::max(height + 8, panel.size.height);
	// A tall pane moves up so it ends above the main panel; the top of the screen is the limit.
	const int bottom = GetMainPanel().position.y - 4;
	const int top = std::max(0, std::min(panel.position.y, bottom - height));
	return { { panel.position.x + panel.size.width + 4, top }, { DetailsWidth, std::min(height, bottom - top) } };
}

/** @brief The lines with long text wrapped to the pane. */
std::vector<SkillDetailLine> WrappedDetails(const Player &player, size_t slot)
{
	std::vector<SkillDetailLine> lines = SkillDetails(player, slot);
	for (SkillDetailLine &line : lines) {
		if (line.kind != SkillDetailLine::Kind::Value && line.kind != SkillDetailLine::Kind::Heading)
			line.label = WordWrapString(line.label, DetailsWidth - 20);
	}
	return lines;
}

void DrawDetailsPane(const Surface &out, const Player &player)
{
	if (!DetailsSlot || *DetailsSlot > DefaultAttackRow || RowSkill(player, *DetailsSlot).isEmpty()) {
		DetailsSlot = std::nullopt;
		return;
	}
	const size_t slot = *DetailsSlot;
	const SkillSlot skill = RowSkill(player, slot);
	const std::vector<SkillDetailLine> lines = WrappedDetails(player, slot);
	const Rectangle pane = DetailsRect(lines);
	DrawPanel(out, pane);

	const int left = pane.position.x + 10;
	const int width = pane.size.width - 20;
	int y = pane.position.y + 8;
	DrawString(out, SkillName(skill), { { left, y }, { width, 24 } }, { .flags = UiFlags::FontSize24 | UiFlags::ColorUiGold | UiFlags::VerticalCenter });
	y += 30;
	const int bottom = GetMainPanel().position.y - 4;
	for (const SkillDetailLine &line : lines) {
		if (y + DetailsLineHeight > bottom)
			break;
		switch (line.kind) {
		case SkillDetailLine::Kind::Heading:
			y += 4;
			DrawHorizontalLine(out, { left, y }, width, InnerBorderColor);
			DrawString(out, line.label, { { left, y + 3 }, { width, 16 } }, { .flags = UiFlags::ColorWhitegold | UiFlags::VerticalCenter });
			y += 20;
			break;
		case SkillDetailLine::Kind::Value: {
			const Rectangle rect { { left, y }, { width, DetailsLineHeight } };
			DrawString(out, line.label, rect, { .flags = UiFlags::ColorWhite | UiFlags::VerticalCenter });
			if (!line.value.empty())
				DrawString(out, line.value, rect, { .flags = UiFlags::AlignRight | UiFlags::ColorWhitegold | UiFlags::VerticalCenter });
			y += DetailsLineHeight;
			break;
		}
		default: {
			const int count = static_cast<int>(std::count(line.label.begin(), line.label.end(), '\n') + 1);
			const UiFlags color = line.kind == SkillDetailLine::Kind::Good ? UiFlags::ColorBlue
			    : line.kind == SkillDetailLine::Kind::Note               ? UiFlags::ColorButtonpushed
			                                                             : UiFlags::ColorWhite;
			DrawString(out, line.label, { { left, y }, { width, DetailsLineHeight * count } }, { .flags = color, .lineHeight = DetailsLineHeight });
			y += DetailsLineHeight * count;
			break;
		}
		}
	}
}

void HoldGem(Player &player, Item &&item)
{
	PlaySFX(ItemInvSnds[ItemCAnimTbl[item._iCurs]]);
	player.HoldItem = std::move(item);
	NewCursor(player.HoldItem);
}

void ChangeMinionCount(Player &player, PersistentSkill skill, bool more)
{
	const int affordable = AffordableMinionCount(player, skill);
	// A count Spirit no longer covers drops to what the gem keeps first.
	const int chosen = std::min(ChosenMinionCount(player, skill), affordable);
	if (more && chosen >= affordable) {
		EventPlrMsg(chosen >= MaxMinionsPerGem ? FormatRuntime(_("{:s} keeps at most {:d} minions."), SkillName(SkillSlot::ForPersistent(skill)), MaxMinionsPerGem)
		                                      : FormatRuntime(_("Not enough Spirit: another one needs {:d}."), SpiritPerMinion(player, skill)),
		    UiFlags::ColorRed);
		return;
	}
	if (!more && chosen <= 0 && ChosenMinionCount(player, skill) <= 0)
		return;
	SetChosenMinionCount(player, skill, more ? chosen + 1 : std::max(chosen - 1, 0));
	PlaySFX(SfxID::MenuMove);
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
	DetailsSlot = std::nullopt;
}

bool SkillsPanelContains(Point position)
{
	if (!IsOpen)
		return false;
	if (PanelRect().contains(position))
		return true;
	if (!DetailsSlot || MyPlayer == nullptr)
		return false;
	return DetailsRect(WrappedDetails(*MyPlayer, *DetailsSlot)).contains(position);
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
	for (const size_t slot : VisibleRows())
		DrawRow(out, player, slot, hover);
	DrawDetailsPane(out, player);
	if (hover.kind != Hit::Kind::None)
		DrawHoverTooltip(out, player, hover);
}

bool HandleSkillsPanelRightClick(Point mousePosition)
{
	if (!IsOpen || MyPlayer == nullptr || !PanelRect().contains(mousePosition))
		return false;
	const Hit hit = HitAt(mousePosition);
	if (IsNoneOf(hit.kind, Hit::Kind::Skill, Hit::Kind::Row) || hit.slot >= NumSkillSlots)
		return false;
	Player &player = *MyPlayer;
	if (!ToggleSpiritGem(player, hit.slot))
		return false;
	PlaySFX(player.poe2.spiritGemOn(hit.slot) ? SfxID::SpellManaShield : SfxID::SpellEnd);
	return true;
}

bool UseJewellerOnSkillsPanel(Player &player, Point mousePosition)
{
	if (!IsOpen || !PanelRect().contains(mousePosition))
		return false;
	const Hit hit = HitAt(mousePosition);
	if (IsNoneOf(hit.kind, Hit::Kind::Row, Hit::Kind::Skill, Hit::Kind::Socket))
		return false;
	return ApplyJewellerOrb(CurrencyOrbForMiscId(player._pOilType), player, hit.slot);
}

std::string JewellerSocketHint(size_t socket)
{
	switch (socket) {
	case 2:
		return std::string(_("Use a Lesser Jeweller's Orb on this skill to open it"));
	case 3:
		return std::string(_("Use a Greater Jeweller's Orb on this skill to open it"));
	default:
		return std::string(_("Use a Perfect Jeweller's Orb on this skill to open it"));
	}
}

void HandleSkillsPanelClick(Point mousePosition)
{
	Player &player = *MyPlayer;
	if (!PanelRect().contains(mousePosition))
		return;
	const Hit hit = HitAt(mousePosition);
	switch (hit.kind) {
	case Hit::Kind::Row:
		if (DetailsSlot == hit.slot || RowSkill(player, hit.slot).isEmpty())
			DetailsSlot = std::nullopt;
		else
			DetailsSlot = hit.slot;
		break;
	case Hit::Kind::GemcuttingButton:
		OpenGemcuttingBrowser();
		break;
	case Hit::Kind::PageTab:
		Page = hit.slot;
		break;
	case Hit::Kind::Skill:
		if (hit.slot != DefaultAttackRow)
			ClickSkillSlot(player, hit.slot, /*canTakeOut=*/true);
		break;
	case Hit::Kind::Socket:
		if (hit.socket < SupportSocketCount(player, hit.slot) && SkillTakesSupports(RowSkill(player, hit.slot)))
			ClickSocket(player, hit.slot, hit.socket);
		break;
	case Hit::Kind::CountMinus:
	case Hit::Kind::CountPlus:
		ChangeMinionCount(player, RowSkill(player, hit.slot).persistent(), hit.kind == Hit::Kind::CountPlus);
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
