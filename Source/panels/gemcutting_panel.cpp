#include "panels/gemcutting_panel.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#else
#ifdef USE_SDL1
#include "utils/sdl2_to_1_2_backports.h"
#endif
#endif

#include "DiabloUI/text_input.hpp"
#include "control/control.hpp"
#include "controls/wasd_movement.hpp"
#include "cursor.h"
#include "diablo.h"
#include "effects.h"
#include "engine/dx.h"
#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "panels/item_spawner.hpp"
#include "panels/poe2_widgets.hpp"
#include "panels/skill_bar.hpp"
#include "panels/skills_panel.hpp"
#include "panels/spell_icons.hpp"
#include "player.h"
#include "plrmsg.h"
#include "poe2/default_attack.hpp"
#include "poe2/gem_levels.hpp"
#include "poe2/gemcutting.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/spirit.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/weapon_skills.hpp"
#include "utils/display.h"
#include "utils/format.hpp"
#include "utils/language.h"
#include "utils/sdl_compat.h"
#include "utils/str_case.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

using namespace poe2ui;

constexpr int Padding = 12;
constexpr int HeaderHeight = 40;
constexpr int FooterHeight = 36;
constexpr int TabWidth = 118;
constexpr int TabHeight = 24;
constexpr int TabGap = 3;
constexpr int TierLabelWidth = 34;
constexpr Size CellSize { 136, 68 };
constexpr int TierGap = 6;
constexpr int ScrollStep = 30;

constexpr uint8_t TabColor = PAL16_GRAY + 13;
constexpr uint8_t TabActiveColor = PAL16_YELLOW + 11;
constexpr uint8_t TierColor = PAL16_GRAY + 12;
constexpr uint8_t FieldColor = PAL16_GRAY + 14;

bool IsOpen;
/** Where the uncut gem being cut is; empty while just browsing. */
struct UncutLocation {
	bool fromBelt;
	int index;
	uint32_t seed;
};
std::optional<UncutLocation> Uncut;
GemCategory Category = GemCategory::All;
/** While cutting a support gem: the socketed skill whose supports are listed, like POE2's skill list; empty for all supports. */
std::optional<SkillSlot> SupportedSkill;
int Scroll = 0;

char QueryBuffer[48];
TextInputCursorState QueryCursor;
TextInputState QueryInput {
	TextInputState::Options {
	    .value = QueryBuffer,
	    .cursor = &QueryCursor,
	    .maxLength = sizeof(QueryBuffer) - 1,
	}
};

struct Cell {
	size_t gem;
	Rectangle rect;
};

struct TierBlock {
	int tier;
	Rectangle label;
};

struct CategoryTab {
	GemCategory category;
	Rectangle rect;
};

struct SkillTab {
	/** Empty for the "All gems" tab. */
	std::optional<SkillSlot> skill;
	Rectangle rect;
};

struct Layout {
	Rectangle panel;
	std::vector<CategoryTab> tabs;
	/** Shown in place of the category tabs while cutting a support gem. */
	std::vector<SkillTab> skillTabs;
	Rectangle grid;
	Rectangle search;
	std::vector<TierBlock> tiers;
	std::vector<Cell> cells;
	int contentHeight = 0;
};

const Item *UncutItem()
{
	if (!Uncut || MyPlayer == nullptr)
		return nullptr;
	const Player &player = *MyPlayer;
	const Item &item = Uncut->fromBelt ? player.SpdList[Uncut->index] : player.InvList[Uncut->index];
	if (!IsUncutGem(item) || item._iSeed != Uncut->seed)
		return nullptr;
	return &item;
}

bool IsCuttingSupport()
{
	const Item *uncut = UncutItem();
	return uncut != nullptr && UncutGemKind(*uncut) == UncutKind::Support;
}

/** @brief The socketed skills that take supports, each once, in Skills panel order, then the weapon's own attack. */
std::vector<SkillSlot> SupportableSkills(const Player &player)
{
	std::vector<SkillSlot> skills;
	for (const SkillSlot &skill : player.poe2.skillSlots) {
		if (!skill.isEmpty() && SkillTakesSupports(skill) && std::find(skills.begin(), skills.end(), skill) == skills.end())
			skills.push_back(skill);
	}
	// Curtis 2026-10-20: the weapon's own attack (Bow Shot and the like) takes supports too, in its own Skills panel row.
	if (const SkillSlot weaponAttack = DefaultSkillSlot(player); !weaponAttack.isEmpty() && SkillTakesSupports(weaponAttack))
		skills.push_back(weaponAttack);
	return skills;
}

/** @brief Whether the selected skill already has this support linked. */
/** @brief Whether the selected skill already has the support at this tier or higher. */
bool IsLinkedToSupportedSkill(const Player &player, SupportGem support, int tier)
{
	if (!SupportedSkill)
		return false;
	return player.poe2.supportTier(*SupportedSkill, support) >= std::max(tier, 1);
}

bool MatchesSupportedSkill(const GemEntry &gem)
{
	return !SupportedSkill || gem.support == SupportGem::None || SupportGemCanSupport(gem.support, *SupportedSkill);
}

bool MatchesQuery(const GemEntry &gem)
{
	const std::string query = AsciiStrToLower(QueryInput.value());
	if (query.empty())
		return true;
	std::string haystack = AsciiStrToLower(GemName(gem));
	for (size_t i = 0; i < static_cast<size_t>(GemCategory::All); i++) {
		const auto category = static_cast<GemCategory>(i);
		if (GemInCategory(gem, category))
			StrAppend(haystack, " ", AsciiStrToLower(GemCategoryName(category)));
	}
	if (gem.support != SupportGem::None)
		StrAppend(haystack, " support ", AsciiStrToLower(SupportGemDescription(gem.support, gem.tier)));
	else if (gem.skill.kind == SkillKind::Attack)
		StrAppend(haystack, " attack ", AsciiStrToLower(WeaponSkillDescription(gem.skill.attack())));
	else if (gem.skill.kind == SkillKind::Persistent)
		StrAppend(haystack, " spirit ", AsciiStrToLower(PersistentSkillDescription(gem.skill.persistent())));
	else
		StrAppend(haystack, " spell");
	size_t start = 0;
	while (start < query.size()) {
		size_t end = query.find(' ', start);
		if (end == std::string::npos)
			end = query.size();
		if (end > start && haystack.find(query.substr(start, end - start)) == std::string::npos)
			return false;
		start = end + 1;
	}
	return true;
}

Layout ComputeLayout(const Surface &out)
{
	Layout layout;
	const int availableHeight = out.h() - GetMainPanel().size.height;
	const int width = std::min(720, out.w() - 16);
	const int height = std::min(520, availableHeight - 8);
	layout.panel = { { (out.w() - width) / 2, std::max((availableHeight - height) / 2, 4) }, { width, height } };
	const Point origin = layout.panel.position;

	if (IsCuttingSupport() && MyPlayer != nullptr) {
		// Like POE2: pick one of your skills to see only the supports that work with it.
		int y = origin.y + HeaderHeight;
		for (const SkillSlot &skill : SupportableSkills(*MyPlayer)) {
			layout.skillTabs.push_back({ skill, { { origin.x + Padding, y }, { TabWidth, TabHeight } } });
			y += TabHeight + TabGap;
		}
		layout.skillTabs.push_back({ std::nullopt, { { origin.x + Padding, origin.y + height - FooterHeight - TabHeight }, { TabWidth, TabHeight } } });
	} else {
		// Only the tabs of what the uncut gem can become, e.g. no Support or Spirit tab for a skill gem (Curtis 2026-10-22).
		const Item *uncut = UncutItem();
		int y = origin.y + HeaderHeight;
		for (const GemCategory category : GemcuttingTabs(uncut != nullptr ? std::optional<UncutKind>(UncutGemKind(*uncut)) : std::nullopt)) {
			layout.tabs.push_back({ category, { { origin.x + Padding, y }, { TabWidth, TabHeight } } });
			y += TabHeight + TabGap;
		}
	}

	const int gridX = origin.x + Padding + TabWidth + 10;
	layout.grid = { { gridX, origin.y + HeaderHeight }, { origin.x + width - Padding - gridX, height - HeaderHeight - FooterHeight } };
	layout.search = { { origin.x + (width - 300) / 2, origin.y + height - FooterHeight + 6 }, { 300, 24 } };

	// Tier rows of gems, scrolled within the grid.
	const int perLine = std::max((layout.grid.size.width - TierLabelWidth - 6) / CellSize.width, 1);
	const std::vector<GemEntry> &catalog = GemCatalog();
	const Item *uncut = UncutItem();
	int y = layout.grid.position.y - Scroll;
	size_t i = 0;
	while (i < catalog.size()) {
		const int tier = GemTier(catalog[i].level);
		std::vector<size_t> gems;
		for (; i < catalog.size() && GemTier(catalog[i].level) == tier; i++) {
			// Cutting lists only the uncut gem's own kind: skill, Spirit or support.
			if (GemInCategory(catalog[i], Category) && MatchesQuery(catalog[i]) && MatchesSupportedSkill(catalog[i]) && (uncut == nullptr || UncutGemKind(*uncut) == GemUncutKind(catalog[i])))
				gems.push_back(i);
		}
		if (gems.empty())
			continue;
		const int lines = (static_cast<int>(gems.size()) + perLine - 1) / perLine;
		const int blockHeight = lines * CellSize.height;
		layout.tiers.push_back({ tier, { { layout.grid.position.x, y }, { TierLabelWidth, blockHeight } } });
		for (size_t n = 0; n < gems.size(); n++) {
			const int column = static_cast<int>(n) % perLine;
			const int line = static_cast<int>(n) / perLine;
			layout.cells.push_back({ gems[n], { { layout.grid.position.x + TierLabelWidth + 6 + column * CellSize.width, y + line * CellSize.height }, CellSize } });
		}
		y += blockHeight + TierGap;
	}
	layout.contentHeight = y + Scroll - layout.grid.position.y;
	return layout;
}

Layout CurrentLayout()
{
	return ComputeLayout(GlobalBackBuffer());
}

void ClampScroll(const Layout &layout)
{
	Scroll = std::clamp(Scroll, 0, std::max(layout.contentHeight - layout.grid.size.height, 0));
}

std::optional<size_t> CellAt(const Layout &layout, Point position)
{
	if (!layout.grid.contains(position))
		return std::nullopt;
	for (const Cell &cell : layout.cells) {
		if (cell.rect.contains(position))
			return cell.gem;
	}
	return std::nullopt;
}

/** @brief The level of the socketed gem an uncut gem would level up instead of cutting a new one: in a Skills panel row, or inside a meta gem. */
std::optional<int> SocketedGemLevel(const Player &player, const GemEntry &gem)
{
	if (gem.support != SupportGem::None)
		return std::nullopt;
	if (const std::optional<size_t> socket = FindSkillSlot(player, gem.skill); socket)
		return player.poe2.gemLevel(*socket);
	if (const auto meta = player.poe2.metaSocketOf(gem.skill); meta)
		return std::clamp<int>(player.poe2.metaSkillLevels[meta->first][meta->second], 1, MaxGemLevel);
	return std::nullopt;
}

void SetSocketedGemLevel(Player &player, const GemEntry &gem, int level)
{
	const auto value = static_cast<uint8_t>(std::clamp(level, 1, MaxGemLevel));
	if (const std::optional<size_t> socket = FindSkillSlot(player, gem.skill); socket)
		player.poe2.slotLevels[*socket] = value;
	else if (const auto meta = player.poe2.metaSocketOf(gem.skill); meta)
		player.poe2.metaSkillLevels[meta->first][meta->second] = value;
}

bool CanCut(const GemEntry &gem)
{
	const Item *uncut = UncutItem();
	return uncut != nullptr && CanCutGem(*uncut, gem);
}

const ItemData &GemData(const GemEntry &gem)
{
	return AllItemsList[gem.itemIndex];
}

/** @brief What the gem needs at a gem level: a skill gem's needs grow with its level, like POE2's; a support gem's don't. */
GemAttributeNeeds GemNeeds(const GemEntry &gem, int level)
{
	if (gem.support == SupportGem::None)
		return SkillGemAttributeNeeds(gem.skill, level);
	const ItemData &data = GemData(gem);
	return { data.iMinStr, data.iMinMag, data.iMinDex };
}

std::string RequirementsText(const GemEntry &gem, int level)
{
	const GemAttributeNeeds needs = GemNeeds(gem, level);
	std::string text;
	const auto add = [&text](int needed, std::string_view stat) {
		if (needed > 0)
			StrAppend(text, text.empty() ? "" : ", ", FormatRuntime(_("{:d} {:s}"), needed, stat));
	};
	add(needs.strength, _("Str"));
	add(needs.magic, _("Mag"));
	add(needs.dexterity, _("Dex"));
	return text;
}

bool MeetsRequirements(const Player &player, const GemEntry &gem, int level)
{
	return MeetsGemAttributeNeeds(player, GemNeeds(gem, level));
}

void DrawGemIcon(const Surface &out, const Rectangle &box, const GemEntry &gem, bool ready)
{
	if (gem.support == SupportGem::None) {
		DrawSkillIcon(out, { box.position.x, box.position.y + box.size.height - 1 }, gem.skill, ready);
		return;
	}
	Item item;
	if (!CreateSupportGemDisplayItem(item, gem.support, gem.tier))
		return;
	item._iStatFlag = ready;
	const int cursId = item._iCurs + CURSOR_FIRSTITEM;
	const Size size = GetInvItemSize(cursId);
	DrawItem(item, out, { box.position.x + (box.size.width - size.width) / 2, box.position.y + (box.size.height + size.height) / 2 - 1 }, GetInvItemSprite(cursId));
}

void DrawCell(const Surface &out, const Player &player, const Cell &cell, bool hovered)
{
	const GemEntry &gem = GemCatalog()[cell.gem];
	const Rectangle &rect = cell.rect;
	const bool dimmed = Uncut && !CanCut(gem);
	if (hovered)
		FillRectangle(out, { rect.position + Displacement { 2, 0 }, { rect.size.width - 4, rect.size.height - 2 } }, RowHoverColor);

	const Rectangle icon { { rect.position.x + (rect.size.width - 37) / 2, rect.position.y + 1 }, { 37, 38 } };
	DrawHalfTransparentRectTo(out, icon.position.x, icon.position.y, icon.size.width, icon.size.height);
	DrawGemIcon(out, icon, gem, !dimmed);
	if (hovered)
		DrawSmallSpellIconBorder(out, { icon.position.x, icon.position.y + icon.size.height - 1 });

	UiFlags nameColor = UiFlags::ColorWhitegold;
	if (gem.support != SupportGem::None)
		nameColor = SupportGemColor(gem.support) == GemColor::Strength ? UiFlags::ColorRed : SupportGemColor(gem.support) == GemColor::Dexterity ? UiFlags::ColorYellow
		                                                                                                                                         : UiFlags::ColorBlue;
	else if (gem.skill.kind == SkillKind::Spell)
		nameColor = UiFlags::ColorBlue;
	if (dimmed)
		nameColor = UiFlags::ColorButtonpushed;
	// Long names squeeze their letters together rather than run into the next cell.
	DrawString(out, GemName(gem), { rect.position + Displacement { 2, 40 }, { rect.size.width - 4, 14 } }, { .flags = nameColor | UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::KerningFitSpacing });
	const std::optional<int> socketedLevel = SocketedGemLevel(player, gem);
	std::string detail;
	if (socketedLevel)
		detail = FormatRuntime(_("Socketed, gem level {:d}"), *socketedLevel);
	else if (gem.support != SupportGem::None && IsLinkedToSupportedSkill(player, gem.support, gem.tier))
		detail = _("Already linked");
	else
		detail = FormatRuntime(_("Tier level {:d}"), gem.level);
	DrawString(out, detail, { rect.position + Displacement { 2, 54 }, { rect.size.width - 4, 13 } },
	    { .flags = UiFlags::FontSize12 | UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::KerningFitSpacing | (dimmed ? UiFlags::ColorButtonpushed : UiFlags::ColorWhite) });
}

void DrawGemTooltip(const Surface &out, const Player &player, const GemEntry &gem)
{
	std::vector<std::pair<std::string, UiFlags>> lines;
	if (gem.support != SupportGem::None) {
		lines.emplace_back(std::string(SupportGemDescription(gem.support, gem.tier)), UiFlags::ColorWhite);
		lines.emplace_back(FormatRuntime(_("{:s} support gem"), GemColorName(SupportGemColor(gem.support))), UiFlags::ColorWhitegold);
		if (SupportedSkill)
			lines.emplace_back(FormatRuntime(_("Works with {:s}"), SkillName(*SupportedSkill)), UiFlags::ColorWhite);
		else if (const std::string_view needs = SupportGemNeeds(gem.support); !needs.empty())
			lines.emplace_back(std::string(needs), UiFlags::ColorWhite);
	} else if (gem.skill.kind == SkillKind::Attack) {
		lines.emplace_back(std::string(WeaponSkillDescription(gem.skill.attack())), UiFlags::ColorWhite);
		if (WeaponSkillNeedsWeapon(gem.skill.attack()))
			lines.emplace_back(FormatRuntime(_("Needs a {:s}"), WeaponSkillWeapons(gem.skill.attack())), UiFlags::ColorWhite);
		lines.emplace_back(FormatRuntime(_("{:d} mana"), WeaponSkillManaCost(gem.skill.attack())), UiFlags::ColorWhite);
	} else if (gem.skill.kind == SkillKind::Persistent) {
		lines.emplace_back(std::string(PersistentSkillDescription(gem.skill.persistent())), UiFlags::ColorWhite);
		lines.emplace_back(SpiritCostText(gem.skill.persistent(), 1), UiFlags::ColorWhite);
	} else {
		lines.emplace_back(FormatRuntime(_("Grants the {:s} spell"), GemName(gem)), UiFlags::ColorWhite);
	}
	// The needs shown are for the level the gem would have: what the uncut gem cuts, else the socketed gem's.
	const Item *uncut = UncutItem();
	const std::optional<int> socketedLevel = SocketedGemLevel(player, gem);
	int needsLevel = socketedLevel.value_or(std::max<int>(gem.level, 1));
	if (uncut != nullptr && CanCutGem(*uncut, gem))
		needsLevel = std::max(GemLevelFromUncut(*uncut, player.getCharacterLevel()), socketedLevel.value_or(0));
	if (const std::string requirements = RequirementsText(gem, needsLevel); !requirements.empty()) {
		const bool meets = MeetsRequirements(player, gem, needsLevel);
		lines.emplace_back(gem.support == SupportGem::None ? FormatRuntime(_("Level {:d} requires {:s}"), needsLevel, requirements) : FormatRuntime(_("Requires {:s}"), requirements), meets ? UiFlags::ColorWhite : UiFlags::ColorRed);
		// Like POE2: cutting is allowed, but the skill can't be used until the hero has the attributes.
		if (!meets)
			lines.emplace_back(_("You can cut it, but can't use it until you meet this"), UiFlags::ColorRed);
	}

	if (uncut == nullptr) {
		lines.emplace_back(FormatRuntime(_("Use an uncut {:s} gem to cut or level this"), UncutKindName(GemUncutKind(gem))), UiFlags::ColorButtonpushed);
	} else if (CanCutGem(*uncut, gem)) {
		const int level = GemLevelFromUncut(*uncut, player.getCharacterLevel());
		if (const std::optional<int> current = SocketedGemLevel(player, gem); current) {
			if (level > *current) {
				lines.emplace_back(FormatRuntime(_("Click to raise your gem from level {:d} to {:d}"), *current, level), UiFlags::ColorWhitegold);
				lines.emplace_back(FormatRuntime(_("Now {:s}"), GemLevelText(gem.skill, *current)), UiFlags::ColorWhite);
				lines.emplace_back(FormatRuntime(_("Then {:s}"), GemLevelText(gem.skill, level)), UiFlags::ColorWhite);
			} else {
				lines.emplace_back(FormatRuntime(_("Your gem is already level {:d}"), *current), UiFlags::ColorRed);
			}
		} else if (gem.support == SupportGem::None) {
			lines.emplace_back(FormatRuntime(_("Click to cut a level {:d} gem"), level), UiFlags::ColorWhitegold);
			lines.emplace_back(GemLevelText(gem.skill, level), UiFlags::ColorWhite);
		} else {
			lines.emplace_back(_("Click to cut this gem"), UiFlags::ColorWhitegold);
		}
		if (gem.support == SupportGem::None && level < std::min(UncutGemLevel(*uncut), MaxGemLevel))
			lines.emplace_back(FormatRuntime(_("Gem level {:d} needs character level {:d}"), level + 1, CharacterLevelForGemLevel(level + 1)), UiFlags::ColorButtonpushed);
	} else if (UncutGemKind(*uncut) != GemUncutKind(gem)) {
		lines.emplace_back(FormatRuntime(_("Your uncut gem makes {:s} gems; this needs an uncut {:s} gem"), UncutKindName(UncutGemKind(*uncut)), UncutKindName(GemUncutKind(gem))), UiFlags::ColorRed);
	} else {
		lines.emplace_back(FormatRuntime(_("Needs an uncut {:s} gem of level {:d}"), UncutKindName(GemUncutKind(gem)), gem.level), UiFlags::ColorRed);
	}

	const Rectangle bounds { { 0, 0 }, { out.w(), out.h() } };
	DrawTooltip(out, bounds, MousePosition, std::string(GemName(gem)), gem.skill.kind == SkillKind::Spell ? UiFlags::ColorBlue : UiFlags::ColorWhitegold, lines);
}

void UseUpUncut(Player &player)
{
	if (Uncut->fromBelt)
		player.RemoveSpdBarItem(Uncut->index);
	else
		player.RemoveInvItem(Uncut->index);
}

void CutGem(const GemEntry &gem)
{
	Player &player = *MyPlayer;
	const Item *uncut = UncutItem();
	if (uncut == nullptr || !CanCutGem(*uncut, gem))
		return;
	const int level = GemLevelFromUncut(*uncut, player.getCharacterLevel());

	// POE2: an uncut gem raises a gem you already have to its level.
	if (const std::optional<int> socketedLevel = SocketedGemLevel(player, gem); socketedLevel) {
		const int current = *socketedLevel;
		if (level <= current) {
			EventPlrMsg(FormatRuntime(_("{:s} is already level {:d}; this uncut gem gives level {:d} at your character level."), GemName(gem), current, level), UiFlags::ColorRed);
			return;
		}
		const SfxID sound = ItemInvSnds[ItemCAnimTbl[uncut->_iCurs]];
		UseUpUncut(player);
		SetSocketedGemLevel(player, gem, level);
		PlaySFX(sound);
		// Like POE2, a higher level needs more attributes; say so, or the skill seems to stop for no reason.
		if (!MeetsRequirements(player, gem, level))
			EventPlrMsg(FormatRuntime(_("{:s} is now level {:d}, but needs {:s} to use."), GemName(gem), level, RequirementsText(gem, level)), UiFlags::ColorRed);
		else
			EventPlrMsg(FormatRuntime(_("{:s} is now level {:d}."), GemName(gem), level));
		TelemetryGemLevelChanged(player, gem.skill, current, level);
		CloseGemcutting();
		return;
	}

	// Curtis 2026-10-16: a support cut for the skill picked in the cutter goes straight into a free socket of that skill.
	if (gem.support != SupportGem::None && SupportedSkill) {
		const std::optional<size_t> slot = SupportedSkill->kind == SkillKind::Default ? std::optional<size_t>(DefaultAttackRow) : FindSkillSlot(player, *SupportedSkill);
		if (slot) {
			SupportGem replaced = SupportGem::None;
			const SupportSocketResult result = SocketSupportGem(player, *slot, std::nullopt, gem.support, replaced, gem.tier);
			if (result == SupportSocketResult::Socketed) {
				const SfxID sound = ItemInvSnds[ItemCAnimTbl[uncut->_iCurs]];
				UseUpUncut(player);
				PlaySFX(sound);
				EventPlrMsg(SupportSocketMessage(result, player, *slot, gem.support));
				return;
			}
		}
	}

	Item cut;
	if (!CreateGemItem(cut, gem, level))
		return;
	UseUpUncut(player);
	PlaySFX(ItemInvSnds[ItemCAnimTbl[cut._iCurs]]);
	if (!MeetsRequirements(player, gem, level))
		EventPlrMsg(FormatRuntime(_("You cut {:s}, but you need {:s} to use it."), GemName(gem), RequirementsText(gem, level)), UiFlags::ColorRed);
	else
		EventPlrMsg(gem.support != SupportGem::None ? FormatRuntime(_("You cut a {:s} gem."), GemName(gem)) : FormatRuntime(_("You cut a level {:d} {:s} gem."), level, GemName(gem)));
	if (player.HoldItem.isEmpty()) {
		player.HoldItem = std::move(cut);
		NewCursor(player.HoldItem);
	} else {
		GiveItemToPlayer(player, cut);
	}
	CloseGemcutting();
}

bool HandleClick(Point position)
{
	const Layout layout = CurrentLayout();
	if (!layout.panel.contains(position)) {
		CloseGemcutting();
		return true;
	}
	for (const CategoryTab &tab : layout.tabs) {
		if (tab.rect.contains(position)) {
			Category = tab.category;
			Scroll = 0;
			return true;
		}
	}
	for (const SkillTab &tab : layout.skillTabs) {
		if (tab.rect.contains(position)) {
			SupportedSkill = tab.skill;
			Scroll = 0;
			return true;
		}
	}
	if (const std::optional<size_t> gem = CellAt(layout, position); gem)
		CutGem(GemCatalog()[*gem]);
	return true;
}

void Open()
{
	if (!gbIsPoe2 || MyPlayer == nullptr)
		return;
	IsOpen = true;
	Scroll = 0;
	QueryInput.clear();
	// The panel takes the mouse button's release, so forget the click that opened it; otherwise the game
	// waits for a release that never comes and ignores clicks until the next right-click.
	sgbMouseDown = CLICK_NONE;
	LastPlayerAction = PlayerActionType::None;
	ReleaseAllTraySlots();
	ClearWasdMovementKeys();
	SDLC_StartTextInput(ghMainWnd);
}

} // namespace

bool IsGemcuttingOpen()
{
	return IsOpen;
}

namespace {

/** @brief The tab a skill gem opens on: the class's own line (Sorcerer elemental, Rogue bows, Monk staves), the Warrior's by weapon. */
GemCategory DefaultSkillCategory(const Player &player)
{
	switch (player._pClass) {
	case HeroClass::Sorcerer:
		return GemCategory::Elemental;
	case HeroClass::Rogue:
		return GemCategory::Bow;
	case HeroClass::Monk:
		return GemCategory::Staff;
	default:
		break;
	}
	for (const Item &item : { player.InvBody[INVLOC_HAND_LEFT], player.InvBody[INVLOC_HAND_RIGHT] }) {
		switch (item._itype) {
		case ItemType::Axe:
			return GemCategory::Axe;
		case ItemType::Sword:
			return GemCategory::Sword;
		case ItemType::Mace:
			return GemCategory::Mace;
		case ItemType::Bow:
			return GemCategory::Bow;
		case ItemType::Staff:
			return GemCategory::Staff;
		default:
			break;
		}
	}
	return GemCategory::Mace;
}

} // namespace

void OpenGemcutting(bool fromBelt, int index)
{
	const Player &player = *MyPlayer;
	const Item &item = fromBelt ? player.SpdList[index] : player.InvList[index];
	if (!IsUncutGem(item))
		return;
	Uncut = UncutLocation { fromBelt, index, item._iSeed };
	SupportedSkill = std::nullopt;
	switch (UncutGemKind(item)) {
	case UncutKind::Support: {
		Category = GemCategory::Support;
		// Start on the first socketed skill, so a support can't be wasted on one that does nothing for it.
		const std::vector<SkillSlot> skills = SupportableSkills(player);
		if (!skills.empty())
			SupportedSkill = skills.front();
		break;
	}
	case UncutKind::Spirit:
		Category = GemCategory::Spirit;
		break;
	default:
		Category = DefaultSkillCategory(player);
		break;
	}
	// Curtis 2026-10-19: the cut gem goes into the Skills panel, so that opens on the left in place of the stash or
	// character sheet (the stash's tab list would also take the cutter's clicks).
	OpenSkillsPanel();
	Open();
}

void OpenGemcuttingBrowser()
{
	Uncut = std::nullopt;
	SupportedSkill = std::nullopt;
	Open();
}

void CloseGemcutting()
{
	if (!IsOpen)
		return;
	IsOpen = false;
	Uncut = std::nullopt;
	ClearWasdMovementKeys();
	SDLC_StopTextInput(ghMainWnd);
}

bool GemcuttingHandleEvent(const SDL_Event &event)
{
	if (!IsOpen)
		return false;

	if (HandleTextInputEvent(event, QueryInput)) {
		Scroll = 0;
		return true;
	}

	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
		if (SDLC_EventKey(event) == SDLK_ESCAPE)
			CloseGemcutting();
		return true;
	case SDL_EVENT_KEY_UP:
#ifndef USE_SDL1
	case SDL_EVENT_TEXT_INPUT:
#endif
		return true;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (event.button.button == SDL_BUTTON_LEFT)
			return HandleClick(MousePosition);
#ifdef USE_SDL1
		if (event.button.button == SDL_BUTTON_WHEELUP)
			Scroll -= ScrollStep;
		else if (event.button.button == SDL_BUTTON_WHEELDOWN)
			Scroll += ScrollStep;
#endif
		return true;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		return true;
#ifndef USE_SDL1
	case SDL_EVENT_MOUSE_WHEEL:
		Scroll += SDLC_EventWheelIntY(event) > 0 ? -ScrollStep : ScrollStep;
		return true;
#endif
	default:
		return false;
	}
}

void DrawGemcutting(const Surface &out)
{
	if (!IsOpen || MyPlayer == nullptr)
		return;
	if (Uncut && UncutItem() == nullptr) {
		// The uncut gem was moved or used up.
		CloseGemcutting();
		return;
	}
	const Player &player = *MyPlayer;
	Layout layout = ComputeLayout(out);
	const int scroll = Scroll;
	ClampScroll(layout);
	if (scroll != Scroll)
		layout = ComputeLayout(out);

	DrawPanel(out, layout.panel);
	const Rectangle title { layout.panel.position + Displacement { Padding, 8 }, { layout.panel.size.width - (2 * Padding), 26 } };
	DrawString(out, _("Gemcutting"), title, { .flags = UiFlags::FontSize24 | UiFlags::ColorUiGold | UiFlags::AlignCenter | UiFlags::VerticalCenter });
	const Item *uncut = UncutItem();
	const std::string mode = uncut != nullptr ? FormatRuntime(_("Cutting a level {:d} uncut {:s} gem"), UncutGemLevel(*uncut), UncutKindName(UncutGemKind(*uncut)))
	                                          : std::string(_("Browsing all gems"));
	DrawString(out, mode, title, { .flags = UiFlags::ColorWhite | UiFlags::VerticalCenter });
	DrawString(out, _("Esc to close"), title, { .flags = UiFlags::AlignRight | UiFlags::ColorButtonpushed | UiFlags::VerticalCenter });

	for (const CategoryTab &tab : layout.tabs) {
		const bool active = Category == tab.category;
		FillRectangle(out, tab.rect, active ? TabActiveColor : (tab.rect.contains(MousePosition) ? RowHoverColor : TabColor));
		DrawFrame(out, tab.rect, active ? BorderColor : InnerBorderColor);
		DrawString(out, GemCategoryName(tab.category), { tab.rect.position + Displacement { 8, 0 }, { tab.rect.size.width - 12, tab.rect.size.height } },
		    { .flags = (active ? UiFlags::ColorWhitegold : UiFlags::ColorWhite) | UiFlags::VerticalCenter });
	}
	for (const SkillTab &tab : layout.skillTabs) {
		const bool active = SupportedSkill == tab.skill;
		FillRectangle(out, tab.rect, active ? TabActiveColor : (tab.rect.contains(MousePosition) ? RowHoverColor : TabColor));
		DrawFrame(out, tab.rect, active ? BorderColor : InnerBorderColor);
		const std::string_view name = tab.skill ? SkillName(*tab.skill) : _("All gems");
		UiFlags color = active ? UiFlags::ColorWhitegold : UiFlags::ColorWhite;
		if (!active && tab.skill && tab.skill->kind == SkillKind::Spell)
			color = UiFlags::ColorBlue;
		DrawString(out, name, { tab.rect.position + Displacement { 8, 0 }, { tab.rect.size.width - 12, tab.rect.size.height } }, { .flags = color | UiFlags::VerticalCenter });
	}
	// Only cutting a support lists skill tabs; the hint drew over the gem categories when cutting a skill gem (Curtis 2026-10-21).
	if (IsCuttingSupport() && std::none_of(layout.skillTabs.begin(), layout.skillTabs.end(), [](const SkillTab &tab) { return tab.skill && tab.skill->kind != SkillKind::Default; }))
		DrawString(out, WordWrapString(_("Socket a skill gem in the Skills panel (K) to see its supports."), TabWidth), { layout.panel.position + Displacement { Padding, HeaderHeight + TabHeight + TabGap }, { TabWidth, 120 } },
		    { .flags = UiFlags::ColorButtonpushed });

	FillRectangle(out, layout.grid, PanelColor);
	DrawFrame(out, layout.grid, InnerBorderColor);
	const Surface grid = out.subregion(layout.grid.position.x + 1, layout.grid.position.y + 1, layout.grid.size.width - 2, layout.grid.size.height - 2);
	const Displacement toGrid { -(layout.grid.position.x + 1), -(layout.grid.position.y + 1) };
	for (const TierBlock &block : layout.tiers) {
		const Rectangle label { block.label.position + toGrid, block.label.size };
		FillRectangle(grid, label, TierColor);
		DrawFrame(grid, label, InnerBorderColor);
		DrawString(grid, GemTierNumeral(block.tier), { label.position + Displacement { 0, 4 }, { label.size.width, 16 } }, { .flags = UiFlags::ColorUiGold | UiFlags::AlignCenter | UiFlags::VerticalCenter });
		DrawHorizontalLine(grid, label.position + Displacement { label.size.width, label.size.height + (TierGap / 2) - 1 }, layout.grid.size.width, InnerBorderColor);
	}
	const std::optional<size_t> hovered = CellAt(layout, MousePosition);
	for (const Cell &cell : layout.cells) {
		if (cell.rect.position.y + cell.rect.size.height < layout.grid.position.y || cell.rect.position.y > layout.grid.position.y + layout.grid.size.height)
			continue;
		DrawCell(grid, player, { cell.gem, { cell.rect.position + toGrid, cell.rect.size } }, hovered == cell.gem);
	}
	if (layout.cells.empty() && SupportedSkill)
		DrawString(grid, FormatRuntime(_("No supports for {:s} match."), SkillName(*SupportedSkill)), { { 0, 0 }, { layout.grid.size.width, 40 } }, { .flags = UiFlags::ColorButtonpushed | UiFlags::AlignCenter | UiFlags::VerticalCenter });
	else if (layout.cells.empty())
		DrawString(grid, _("No gems match."), { { 0, 0 }, { layout.grid.size.width, 40 } }, { .flags = UiFlags::ColorButtonpushed | UiFlags::AlignCenter | UiFlags::VerticalCenter });

	FillRectangle(out, layout.search, FieldColor);
	DrawFrame(out, layout.search, BorderColor);
	const Rectangle searchText { layout.search.position + Displacement { 8, 0 }, { layout.search.size.width - 16, layout.search.size.height } };
	if (QueryInput.empty())
		DrawString(out, _("Type keywords here..."), searchText, { .flags = UiFlags::VerticalCenter | UiFlags::ColorButtonpushed });
	DrawString(out, QueryInput.value(), searchText, { .flags = UiFlags::VerticalCenter | UiFlags::ColorWhite, .cursorPosition = static_cast<int>(QueryCursor.position) });

	if (hovered)
		DrawGemTooltip(out, player, GemCatalog()[*hovered]);
}

} // namespace devilution
