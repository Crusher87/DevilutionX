#include "panels/item_spawner.hpp"

#include <algorithm>
#include <array>
#include <optional>

#ifdef USE_SDL3
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_timer.h>
#else
#ifdef USE_SDL1
#include "utils/sdl2_to_1_2_backports.h"
#endif
#endif

#include "DiabloUI/text_input.hpp"
#include "control/control.hpp"
#include "controls/wasd_movement.hpp"
#include "cursor.h"
#include "engine/dx.h"
#include "engine/random.hpp"
#include "engine/render/clx_render.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "inv.h"
#include "msg.h"
#include "multi.h"
#include "player.h"
#include "plrmsg.h"
#include "poe2/belt_uniques.hpp"
#include "poe2/crafting_items.hpp"
#include "poe2/currency.hpp"
#include "poe2/gemcutting.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/uniques.hpp"
#include "poe2/waystones.hpp"
#include "poe2/weapon_skills.hpp"
#include "utils/display.h"
#include "utils/format.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/sdl_compat.h"
#include "utils/str_case.hpp"
#include "utils/str_cat.hpp"
#include "utils/utf8.hpp"

namespace devilution {

namespace {

constexpr int MinLevel = 1;
constexpr int MaxLevel = 60;
constexpr int LevelStep = 5;
constexpr int BatchCount = 5;
/** Tries at rolling a specific unique; each try has about a 1 in 7 chance. */
constexpr int MaxUniqueRolls = 2000;

constexpr int Padding = 12;
constexpr int RowHeight = 18;
constexpr int ChipHeight = 20;

constexpr uint8_t PanelColor = PAL16_GRAY + 15;
constexpr uint8_t BorderColor = PAL16_YELLOW + 8;
constexpr uint8_t FieldColor = PAL16_GRAY + 14;
constexpr uint8_t ChipColor = PAL16_GRAY + 12;
constexpr uint8_t ChipActiveColor = PAL16_YELLOW + 11;
constexpr uint8_t RowHoverColor = PAL16_GRAY + 13;
constexpr uint8_t RowSelectedColor = PAL16_YELLOW + 13;
constexpr uint8_t DividerColor = PAL16_GRAY + 11;

const std::array<item_quality, 4> Qualities = { ITEM_QUALITY_NORMAL, ITEM_QUALITY_MAGIC, ITEM_QUALITY_RARE, ITEM_QUALITY_UNIQUE };

bool IsOpen;
char QueryBuffer[64];
TextInputCursorState QueryCursor;
TextInputState QueryInput {
	TextInputState::Options {
	    .value = QueryBuffer,
	    .cursor = &QueryCursor,
	    .maxLength = sizeof(QueryBuffer) - 1,
	}
};

std::vector<SpawnerEntry> Catalog;
std::vector<size_t> Results;
std::string LastQuery;
SpawnerCategory Category = SpawnerCategory::All;
SpawnerCategory LastCategory = SpawnerCategory::Count;
size_t Selected = 0;
size_t ScrollTop = 0;
size_t QualityIndex = 2; // Rare
int Level = 30;

struct Layout {
	Rectangle panel;
	Rectangle search;
	std::array<Rectangle, static_cast<size_t>(SpawnerCategory::Count)> categories;
	std::array<Rectangle, Qualities.size()> qualities;
	Rectangle levelMinus;
	Rectangle levelValue;
	Rectangle levelPlus;
	Rectangle list;
	Rectangle preview;
	Rectangle footer;

	[[nodiscard]] size_t visibleRows() const
	{
		return static_cast<size_t>(std::max(list.size.height / RowHeight, 1));
	}
};

std::string_view QualityName(item_quality quality)
{
	switch (quality) {
	case ITEM_QUALITY_NORMAL:
		return _("Normal");
	case ITEM_QUALITY_MAGIC:
		return _("Magic");
	case ITEM_QUALITY_RARE:
		return _("Rare");
	default:
		return _("Unique");
	}
}

std::string_view TypeLabel(const ItemData &data)
{
	switch (data.itype) {
	case ItemType::Sword:
		return _("Sword");
	case ItemType::Axe:
		return _("Axe");
	case ItemType::Bow:
		return _("Bow");
	case ItemType::Mace:
		return _("Mace");
	case ItemType::Shield:
		return _("Shield");
	case ItemType::LightArmor:
		return _("Light Armor");
	case ItemType::Helm:
		return _("Helm");
	case ItemType::MediumArmor:
		return _("Medium Armor");
	case ItemType::HeavyArmor:
		return _("Heavy Armor");
	case ItemType::Staff:
		return _("Staff");
	case ItemType::Ring:
		return _("Ring");
	case ItemType::Amulet:
		return _("Amulet");
	default:
		return {};
	}
}

std::string Requirements(const ItemData &data)
{
	std::string result;
	const auto add = [&result](std::string_view stat, int value) {
		if (value > 0)
			StrAppend(result, result.empty() ? "" : ", ", stat, " ", value);
	};
	add(_("Str"), data.iMinStr);
	add(_("Mag"), data.iMinMag);
	add(_("Dex"), data.iMinDex);
	return result;
}

std::string JoinDetail(std::string_view first, std::string_view second)
{
	if (first.empty())
		return std::string(second);
	if (second.empty())
		return std::string(first);
	return StrCat(first, " - ", second);
}

/** @brief Base items that spawn a unique, preferring ones that can drop. */
std::optional<_item_indexes> UniqueBase(const UniqueItem &unique)
{
	std::optional<_item_indexes> found;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		const ItemData &data = AllItemsList[i];
		if (data.iItemId != unique.UIItemId || !IsItemAvailable(static_cast<int>(i)))
			continue;
		if (data.dropRate > 0)
			return static_cast<_item_indexes>(i);
		if (!found)
			found = static_cast<_item_indexes>(i);
	}
	return found;
}

bool IsSpawnableMisc(const ItemData &data)
{
	// Skill and support gems don't drop (uncut gems do), but they're handy for testing.
	const bool isGem = !SkillForGemData(data).isEmpty() || SupportGemForMiscId(data.iMiscId) != SupportGem::None;
	return (data.dropRate > 0 || isGem) && data.iClass == ICLASS_MISC && IsAnyOf(data.iLoc, ILOC_UNEQUIPABLE, ILOC_BELT) && data.iMiscId != IMISC_UNIQUE;
}

/** @brief Rolls the given unique the way a drop of its base would, so it survives saving and loading. */
bool RollUnique(const Player &player, Item &item, int uid, uint32_t firstSeed)
{
	const UniqueItem &unique = UniqueItems[uid];
	const std::optional<_item_indexes> base = UniqueBase(unique);
	if (!base)
		return false;

	// With the 15% unique chance a drop's item level is 4 above the monster level.
	const int level = std::max(unique.UIMinLvl - 4, 1);
	std::vector<int> validUniques;
	for (size_t i = 0; i < UniqueItems.size(); i++) {
		if (UniqueItems[i].UIItemId == unique.UIItemId && level + 4 >= UniqueItems[i].UIMinLvl)
			validUniques.push_back(static_cast<int>(i));
	}
	const auto it = std::find(validUniques.begin(), validUniques.end(), uid);
	if (it == validUniques.end())
		return false;
	const int uidOffset = static_cast<int>(validUniques.end() - it) - 1;

	const uint32_t rngState = GetLCGEngineState();
	bool found = false;
	uint32_t seed = firstSeed;
	for (int i = 0; i < MaxUniqueRolls && !found; i++) {
		seed = (seed * 1103515245U) + 12345U;
		Item candidate {};
		SetupAllItems(player, candidate, *base, seed >> 1, level, 15, true, false, uidOffset);
		if (candidate._iMagical != ITEM_QUALITY_UNIQUE || candidate._iUid != uid)
			continue;
		candidate.dwBuff |= (uidOffset << 1) & CF_UIDOFFSET;
		SetupItem(candidate);
		item = std::move(candidate);
		found = true;
	}
	SetRndSeed(rngState);
	return found;
}

void RefreshResults()
{
	const std::string_view query = QueryInput.value();
	if (query == LastQuery && Category == LastCategory)
		return;
	LastQuery = std::string(query);
	LastCategory = Category;
	Results = FilterItemSpawnerCatalog(Catalog, query, Category);
	Selected = 0;
	ScrollTop = 0;
}

Layout ComputeLayout(const Surface &out)
{
	Layout layout;
	const int availableHeight = out.h() - GetMainPanel().size.height;
	const int width = std::min(680, out.w() - 32);
	const int height = std::min(500, availableHeight - 24);
	layout.panel = { { (out.w() - width) / 2, std::max((availableHeight - height) / 2, 4) }, { width, height } };

	const int innerX = layout.panel.position.x + Padding;
	const int innerWidth = width - (2 * Padding);
	int y = layout.panel.position.y + 40;

	layout.search = { { innerX, y }, { innerWidth, 24 } };
	y += 32;

	int x = innerX;
	for (size_t i = 0; i < layout.categories.size(); i++) {
		const int chipWidth = GetLineWidth(SpawnerCategoryName(static_cast<SpawnerCategory>(i))) + 16;
		layout.categories[i] = { { x, y }, { chipWidth, ChipHeight } };
		x += chipWidth + 4;
	}
	y += ChipHeight + 6;

	x = innerX + GetLineWidth(_("Quality")) + 10;
	for (size_t i = 0; i < Qualities.size(); i++) {
		const int chipWidth = GetLineWidth(QualityName(Qualities[i])) + 16;
		layout.qualities[i] = { { x, y }, { chipWidth, ChipHeight } };
		x += chipWidth + 4;
	}
	x += 16 + GetLineWidth(_("Level")) + 10;
	layout.levelMinus = { { x, y }, { ChipHeight, ChipHeight } };
	layout.levelValue = { { x + ChipHeight, y }, { 32, ChipHeight } };
	layout.levelPlus = { { x + ChipHeight + 32, y }, { ChipHeight, ChipHeight } };
	y += ChipHeight + 10;

	const int footerHeight = 20;
	const int listHeight = layout.panel.position.y + height - footerHeight - 8 - y;
	const int listWidth = (innerWidth * 3) / 5;
	layout.list = { { innerX, y }, { listWidth, listHeight } };
	layout.preview = { { innerX + listWidth + Padding, y }, { innerWidth - listWidth - Padding, listHeight } };
	layout.footer = { { innerX, layout.panel.position.y + height - footerHeight - 4 }, { innerWidth, footerHeight } };
	return layout;
}

Layout CurrentLayout()
{
	return ComputeLayout(GlobalBackBuffer());
}

void KeepSelectionVisible(const Layout &layout)
{
	const size_t rows = layout.visibleRows();
	if (Selected < ScrollTop)
		ScrollTop = Selected;
	else if (Selected >= ScrollTop + rows)
		ScrollTop = Selected - rows + 1;
}

void MoveSelection(int delta)
{
	if (Results.empty())
		return;
	const int last = static_cast<int>(Results.size()) - 1;
	Selected = static_cast<size_t>(std::clamp(static_cast<int>(Selected) + delta, 0, last));
	KeepSelectionVisible(CurrentLayout());
}

void Scroll(int rows)
{
	const Layout layout = CurrentLayout();
	const int maxTop = std::max(static_cast<int>(Results.size()) - static_cast<int>(layout.visibleRows()), 0);
	ScrollTop = static_cast<size_t>(std::clamp(static_cast<int>(ScrollTop) + rows, 0, maxTop));
}

void CycleCategory(int delta)
{
	const int count = static_cast<int>(SpawnerCategory::Count);
	Category = static_cast<SpawnerCategory>((static_cast<int>(Category) + delta + count) % count);
	RefreshResults();
}

void SpawnSelected(int count)
{
	if (Selected >= Results.size())
		return;
	const SpawnerEntry &entry = Catalog[Results[Selected]];
	Player &player = *MyPlayer;
	const item_quality quality = Qualities[QualityIndex];
	int spawned = 0;
	for (int i = 0; i < count; i++) {
		Item item;
		if (!CreateSpawnerItem(player, entry, quality, Level, SDL_GetTicks() + (i * 7919), item))
			break;
		GiveItemToPlayer(player, item);
		spawned++;
	}
	if (spawned == 0) {
		EventPlrMsg(FormatRuntime(_("Couldn't roll a {:s} {:s}. Try another quality or level."), QualityName(quality), entry.name), UiFlags::ColorRed);
		return;
	}
	PlaySFX(SfxID::GrabItem);
	if (spawned == 1)
		EventPlrMsg(FormatRuntime(_("Spawned {:s}"), entry.name));
	else
		EventPlrMsg(FormatRuntime(_("Spawned {:d}x {:s}"), spawned, entry.name));
}

bool HandleClick(Point position, bool shift)
{
	const Layout layout = CurrentLayout();
	if (!layout.panel.contains(position)) {
		CloseItemSpawner();
		return true;
	}
	for (size_t i = 0; i < layout.categories.size(); i++) {
		if (layout.categories[i].contains(position)) {
			Category = static_cast<SpawnerCategory>(i);
			RefreshResults();
			return true;
		}
	}
	for (size_t i = 0; i < layout.qualities.size(); i++) {
		if (layout.qualities[i].contains(position)) {
			QualityIndex = i;
			return true;
		}
	}
	if (layout.levelMinus.contains(position)) {
		Level = std::max(Level - LevelStep, MinLevel);
		return true;
	}
	if (layout.levelPlus.contains(position)) {
		Level = std::min(Level + LevelStep, MaxLevel);
		return true;
	}
	if (layout.list.contains(position)) {
		const size_t row = ScrollTop + static_cast<size_t>((position.y - layout.list.position.y) / RowHeight);
		if (row < Results.size()) {
			Selected = row;
			SpawnSelected(shift ? BatchCount : 1);
		}
		return true;
	}
	if (layout.preview.contains(position)) {
		SpawnSelected(shift ? BatchCount : 1);
	}
	return true;
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

void DrawChip(const Surface &out, const Rectangle &rect, std::string_view text, bool active, bool hovered)
{
	FillRectangle(out, rect, active ? ChipActiveColor : (hovered ? RowHoverColor : ChipColor));
	DrawString(out, text, rect, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | (active ? UiFlags::ColorWhitegold : UiFlags::ColorWhite) });
}

UiFlags EntryColor(const SpawnerEntry &entry)
{
	switch (entry.category) {
	case SpawnerCategory::Uniques:
		return UiFlags::ColorWhitegold;
	case SpawnerCategory::Gems:
		return UiFlags::ColorBlue;
	case SpawnerCategory::Orbs:
		return UiFlags::ColorOrange;
	case SpawnerCategory::Jewelry:
		return UiFlags::ColorYellow;
	default:
		return UiFlags::ColorWhite;
	}
}

void DrawPreview(const Surface &out, const Rectangle &rect)
{
	FillRectangle(out, rect, FieldColor);
	if (Selected >= Results.size()) {
		DrawString(out, _("No items match."), rect, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhite });
		return;
	}
	const SpawnerEntry &entry = Catalog[Results[Selected]];

	const int cursId = static_cast<int>(entry.cursor) + CURSOR_FIRSTITEM;
	const Size spriteSize = GetInvItemSize(cursId);
	const int spriteAreaHeight = std::min(rect.size.height / 2, 100);
	const Point spriteBottomLeft {
		rect.position.x + ((rect.size.width - spriteSize.width) / 2),
		rect.position.y + 10 + ((spriteAreaHeight + spriteSize.height) / 2)
	};
	ClxDraw(out, spriteBottomLeft, GetInvItemSprite(cursId));

	int y = rect.position.y + spriteAreaHeight + 20;
	const int textX = rect.position.x + 8;
	const int textWidth = rect.size.width - 16;
	DrawString(out, entry.name, { { textX, y }, { textWidth, 16 } }, { .flags = UiFlags::AlignCenter | EntryColor(entry) });
	y += 20;
	if (!entry.detail.empty()) {
		DrawString(out, entry.detail, { { textX, y }, { textWidth, 32 } }, { .flags = UiFlags::AlignCenter | UiFlags::ColorWhite });
		y += 32;
	}
	if (entry.rollsQuality) {
		DrawString(out, FormatRuntime(_("{:s}, item level {:d}"), QualityName(Qualities[QualityIndex]), Level), { { textX, y }, { textWidth, 16 } },
		    { .flags = UiFlags::AlignCenter | UiFlags::ColorWhite });
		y += 20;
	}
	DrawString(out, _("Click or Enter: spawn\nShift: spawn 5"), { { textX, rect.position.y + rect.size.height - 40 }, { textWidth, 36 } },
	    { .flags = UiFlags::AlignCenter | UiFlags::ColorWhite, .lineHeight = 16 });
}

} // namespace

std::string_view SpawnerCategoryName(SpawnerCategory category)
{
	switch (category) {
	case SpawnerCategory::All:
		return _("All");
	case SpawnerCategory::Weapons:
		return _("Weapons");
	case SpawnerCategory::Armor:
		return _("Armor");
	case SpawnerCategory::Jewelry:
		return _("Jewelry");
	case SpawnerCategory::Uniques:
		return _("Uniques");
	case SpawnerCategory::Gems:
		return _("Gems");
	case SpawnerCategory::Orbs:
		return _("Orbs");
	case SpawnerCategory::Misc:
		return _("Potions & Misc");
	default:
		return {};
	}
}

std::vector<SpawnerEntry> BuildItemSpawnerCatalog()
{
	std::vector<SpawnerEntry> catalog;
	std::vector<std::string_view> seenNames;
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		const ItemData &data = AllItemsList[i];
		if (!IsItemAvailable(static_cast<int>(i)) || data.iName.empty())
			continue;
		// Skip copies like the class starting weapons that share a name with a regular base.
		if (std::find(seenNames.begin(), seenNames.end(), data.iName) != seenNames.end())
			continue;

		// Essences, omens, augments and catalysts are listed one kind at a time below.
		if (IsAnyOf(data.iMiscId, IMISC_ESSENCE, IMISC_OMEN, IMISC_AUGMENT, IMISC_ORB_CATALYST)) {
			Item probe;
			InitializeItem(probe, static_cast<_item_indexes>(i));
			const auto addVariant = [&](uint8_t variant) {
				SetCraftingItemVariant(probe, variant);
				FinishCraftingItem(probe);
				catalog.push_back(SpawnerEntry { .name = std::string(probe._iIName), .detail = std::string(_("Currency")), .category = SpawnerCategory::Orbs, .baseIndex = static_cast<_item_indexes>(i), .cursor = data.iCurs, .variant = variant });
			};
			switch (data.iMiscId) {
			case IMISC_ESSENCE:
				for (uint8_t kind = 1; kind < static_cast<uint8_t>(EssenceKind::Count); kind++) {
					for (uint8_t tier = 0; tier < static_cast<uint8_t>(EssenceTier::Count); tier++)
						addVariant(EssenceVariant(static_cast<EssenceKind>(kind), static_cast<EssenceTier>(tier)));
				}
				break;
			case IMISC_OMEN:
				for (uint8_t omen = 1; omen < static_cast<uint8_t>(OmenId::Count); omen++)
					addVariant(omen);
				break;
			case IMISC_AUGMENT:
				for (uint8_t augment = 1; augment < static_cast<uint8_t>(AugmentId::Count); augment++)
					addVariant(augment);
				break;
			default:
				for (uint8_t catalyst = 1; catalyst < static_cast<uint8_t>(Catalyst::Count); catalyst++)
					addVariant(catalyst);
				break;
			}
			continue;
		}
		SpawnerEntry entry { .name = std::string(data.iName), .detail = {}, .category = SpawnerCategory::Misc, .baseIndex = static_cast<_item_indexes>(i), .uniqueId = -1, .cursor = data.iCurs };
		const SkillSlot skill = SkillForGemData(data);
		if (!skill.isEmpty()) {
			entry.category = SpawnerCategory::Gems;
			const std::string_view kind = skill.kind == SkillKind::Spell ? _("Spell gem") : skill.kind == SkillKind::Persistent ? _("Spirit gem")
			                                                                                                                    : _("Skill gem");
			std::string weapon;
			if (skill.kind == SkillKind::Attack && WeaponSkillNeedsWeapon(skill.attack()))
				weapon = std::string(WeaponSkillWeapons(skill.attack()));
			entry.detail = JoinDetail(JoinDetail(kind, weapon), Requirements(data));
		} else if (const SupportGem gem = SupportGemForMiscId(data.iMiscId); gem != SupportGem::None) {
			entry.category = SpawnerCategory::Gems;
			entry.detail = std::string(_("Support gem"));
		} else if (CurrencyOrbForMiscId(data.iMiscId) != CurrencyOrb::None) {
			entry.category = SpawnerCategory::Orbs;
			entry.detail = std::string(_("Currency"));
		} else if (data.iClass == ICLASS_WEAPON) {
			entry.category = SpawnerCategory::Weapons;
			entry.rollsQuality = true;
		} else if (data.iClass == ICLASS_ARMOR) {
			entry.category = SpawnerCategory::Armor;
			entry.rollsQuality = true;
		} else if (IsAnyOf(data.iLoc, ILOC_RING, ILOC_AMULET)) {
			entry.category = SpawnerCategory::Jewelry;
			entry.rollsQuality = true;
		} else if (!IsSpawnableMisc(data)) {
			continue;
		}
		if (entry.rollsQuality)
			entry.detail = JoinDetail(TypeLabel(data), Requirements(data));

		seenNames.emplace_back(data.iName);
		catalog.push_back(std::move(entry));
	}

	for (size_t uid = 0; uid < UniqueItems.size(); uid++) {
		const UniqueItem &unique = UniqueItems[uid];
		// The bundle's uniques have a base of their own rather than a Diablo base type.
		const Poe2Unique *poe2Unique = Poe2UniqueFor(static_cast<int>(uid));
		const std::optional<_item_indexes> base = poe2Unique != nullptr ? std::optional(static_cast<_item_indexes>(poe2Unique->baseItem)) : UniqueBase(unique);
		if (unique.UIName.empty() || !base || AllItemsList[*base].iMiscId == IMISC_UNIQUE)
			continue;
		const ItemData &data = AllItemsList[*base];
		catalog.push_back(SpawnerEntry {
		    .name = unique.UIName,
		    .detail = JoinDetail(data.iName, StrCat(_("Level"), " ", static_cast<int>(unique.UIMinLvl))),
		    .category = SpawnerCategory::Uniques,
		    .baseIndex = *base,
		    .uniqueId = static_cast<int>(uid),
		    .cursor = unique.UICurs != ICURS_DEFAULT ? unique.UICurs : data.iCurs,
		});
	}

	for (const BeltUnique &unique : BeltUniques()) {
		const std::optional<int> base = BeltUniqueBase(unique);
		if (!base)
			continue;
		const ItemData &data = AllItemsList[*base];
		catalog.push_back(SpawnerEntry {
		    .name = std::string(unique.name),
		    .detail = JoinDetail(unique.kind == BeltUniqueKind::Charm ? data.iName : (unique.kind == BeltUniqueKind::LifeFlask ? _("Life Flask") : _("Mana Flask")), StrCat(_("Level"), " ", unique.dropLevel)),
		    .category = SpawnerCategory::Uniques,
		    .baseIndex = static_cast<_item_indexes>(*base),
		    .cursor = data.iCurs,
		    .beltUnique = static_cast<uint8_t>(unique.id),
		});
	}

	std::stable_sort(catalog.begin(), catalog.end(), [](const SpawnerEntry &a, const SpawnerEntry &b) {
		if (a.category != b.category)
			return a.category < b.category;
		return a.name < b.name;
	});
	return catalog;
}

std::vector<size_t> FilterItemSpawnerCatalog(const std::vector<SpawnerEntry> &catalog, std::string_view query, SpawnerCategory category)
{
	std::vector<std::string> words;
	const std::string lowerQuery = AsciiStrToLower(query);
	size_t start = 0;
	while (start < lowerQuery.size()) {
		const size_t end = std::min(lowerQuery.find(' ', start), lowerQuery.size());
		if (end > start)
			words.emplace_back(lowerQuery.substr(start, end - start));
		start = end + 1;
	}

	std::vector<size_t> results;
	for (size_t i = 0; i < catalog.size(); i++) {
		const SpawnerEntry &entry = catalog[i];
		if (category != SpawnerCategory::All && entry.category != category)
			continue;
		const std::string haystack = AsciiStrToLower(StrCat(entry.name, " ", entry.detail, " ", SpawnerCategoryName(entry.category)));
		const bool matches = std::all_of(words.begin(), words.end(), [&haystack](const std::string &word) {
			return haystack.find(word) != std::string::npos;
		});
		if (matches)
			results.push_back(i);
	}
	return results;
}

bool CreateSpawnerItem(const Player &player, const SpawnerEntry &entry, item_quality quality, int level, uint32_t seed, Item &item)
{
	if (entry.variant != 0) {
		InitializeItem(item, entry.baseIndex);
		item._iSeed = static_cast<int32_t>(seed >> 1);
		item.dwBuff |= CF_POE2;
		SetCraftingItemVariant(item, entry.variant);
		FinishCraftingItem(item);
		SetupItem(item);
		return !item.isEmpty();
	}
	if (entry.beltUnique != 0) {
		InitializeItem(item, entry.baseIndex);
		item._iSeed = static_cast<int32_t>(seed >> 1);
		item.dwBuff |= CF_POE2;
		MakeBeltUnique(item, static_cast<BeltUniqueId>(entry.beltUnique));
		SetupItem(item);
		return !item.isEmpty();
	}
	if (entry.uniqueId >= 0 && Poe2UniqueFor(entry.uniqueId) != nullptr) {
		if (!CreatePoe2Unique(player, item, entry.uniqueId, seed >> 1))
			return false;
		SetupItem(item);
		return true;
	}
	if (entry.uniqueId >= 0)
		return RollUnique(player, item, entry.uniqueId, seed);
	if (entry.rollsQuality)
		return RollItemOfQuality(player, item, entry.baseIndex, quality, std::clamp(level, MinLevel, MaxLevel), seed);

	// Gems, orbs and potions have no quality; only uncut gems use the level.
	InitializeItem(item, entry.baseIndex);
	item._iSeed = static_cast<int32_t>(seed >> 1);
	SetupItem(item);
	if (IsUncutGem(item) || IsWaystone(item))
		item._iCreateInfo = static_cast<uint16_t>((item._iCreateInfo & ~CF_LEVEL) | std::clamp(level, MinLevel, static_cast<int>(CF_LEVEL)));
	// A waystone's tier follows the level: tier 16 at level 90.
	if (IsWaystone(item)) {
		item._iPoe2Craft = {};
		FinishWaystone(item);
	}
	return !item.isEmpty();
}

/** @brief Puts the item in the inventory, or on the ground at the player's feet when it doesn't fit. */
void GiveItemToPlayer(Player &player, Item &item)
{
	item._iIdentified = true;
	if (AutoPlaceItemInInventory(player, item, true)) {
		CalcPlrInv(player, true);
		return;
	}
	if (ActiveItemCount >= MAXITEMS)
		return;
	const int ii = AllocateItem();
	Items[ii] = item;
	GetSuperItemSpace(player.position.tile, ii);
	NetSendCmdPItem(false, CMD_SPAWNITEM, Items[ii].position, Items[ii]);
}

bool IsItemSpawnerOpen()
{
	return IsOpen;
}

void OpenItemSpawner()
{
	if (gbIsMultiplayer || MyPlayer == nullptr)
		return;
	IsOpen = true;
	Catalog = BuildItemSpawnerCatalog();
	LastCategory = SpawnerCategory::Count; // Forces a refresh.
	RefreshResults();
	ClearWasdMovementKeys();
	SDLC_StartTextInput(ghMainWnd);
}

void CloseItemSpawner()
{
	if (!IsOpen)
		return;
	IsOpen = false;
	ClearWasdMovementKeys();
	SDLC_StopTextInput(ghMainWnd);
}

void ToggleItemSpawner()
{
	if (IsOpen)
		CloseItemSpawner();
	else
		OpenItemSpawner();
}

bool ItemSpawnerHandleEvent(const SDL_Event &event)
{
	if (!IsOpen)
		return false;

	if (HandleTextInputEvent(event, QueryInput)) {
		RefreshResults();
		return true;
	}

	const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
		switch (SDLC_EventKey(event)) {
		case SDLK_ESCAPE:
		case SDLK_INSERT:
			CloseItemSpawner();
			break;
		case SDLK_UP:
			MoveSelection(-1);
			break;
		case SDLK_DOWN:
			MoveSelection(1);
			break;
		case SDLK_PAGEUP:
			MoveSelection(-static_cast<int>(CurrentLayout().visibleRows()));
			break;
		case SDLK_PAGEDOWN:
			MoveSelection(static_cast<int>(CurrentLayout().visibleRows()));
			break;
		case SDLK_TAB:
			CycleCategory(shift ? -1 : 1);
			break;
		case SDLK_RETURN:
		case SDLK_KP_ENTER:
			SpawnSelected(shift ? BatchCount : 1);
			break;
		default:
			break;
		}
		return true;
	case SDL_EVENT_KEY_UP:
#ifndef USE_SDL1
	case SDL_EVENT_TEXT_INPUT:
#endif
		return true;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (event.button.button == SDL_BUTTON_LEFT)
			return HandleClick(MousePosition, shift);
#ifdef USE_SDL1
		if (event.button.button == SDL_BUTTON_WHEELUP)
			Scroll(-3);
		else if (event.button.button == SDL_BUTTON_WHEELDOWN)
			Scroll(3);
#endif
		return true;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		return true;
#ifndef USE_SDL1
	case SDL_EVENT_MOUSE_WHEEL:
		Scroll(SDLC_EventWheelIntY(event) > 0 ? -3 : 3);
		return true;
#endif
	default:
		return false;
	}
}

void DrawItemSpawner(const Surface &out)
{
	if (!IsOpen)
		return;
	RefreshResults();
	const Layout layout = ComputeLayout(out);
	KeepSelectionVisible(layout);

	FillRectangle(out, layout.panel, PanelColor);
	DrawFrame(out, layout.panel, BorderColor);

	const Rectangle title { layout.panel.position + Displacement { Padding, 8 }, { layout.panel.size.width - (2 * Padding), 26 } };
	DrawString(out, _("Item Spawner"), title, { .flags = UiFlags::FontSize24 | UiFlags::ColorUiGold | UiFlags::VerticalCenter });
	DrawString(out, FormatRuntime(_("{:d} items  -  Esc to close"), static_cast<int>(Results.size())), title,
	    { .flags = UiFlags::AlignRight | UiFlags::VerticalCenter | UiFlags::ColorWhite });

	// Search box
	FillRectangle(out, layout.search, FieldColor);
	DrawFrame(out, layout.search, BorderColor);
	const Rectangle searchText { layout.search.position + Displacement { 8, 0 }, { layout.search.size.width - 16, layout.search.size.height } };
	if (QueryInput.empty()) {
		DrawString(out, _("Search items, e.g. \"bow\", \"cleave\", \"unique ring\""), searchText,
		    { .flags = UiFlags::VerticalCenter | UiFlags::ColorButtonpushed });
	}
	DrawString(out, QueryInput.value(), searchText,
	    { .flags = UiFlags::VerticalCenter | UiFlags::ColorWhite, .cursorPosition = static_cast<int>(QueryCursor.position) });

	for (size_t i = 0; i < layout.categories.size(); i++) {
		const auto category = static_cast<SpawnerCategory>(i);
		DrawChip(out, layout.categories[i], SpawnerCategoryName(category), category == Category, layout.categories[i].contains(MousePosition));
	}

	const Point qualityRow = layout.qualities[0].position;
	DrawString(out, _("Quality"), { { layout.panel.position.x + Padding, qualityRow.y }, { 60, ChipHeight } }, { .flags = UiFlags::VerticalCenter | UiFlags::ColorWhite });
	for (size_t i = 0; i < Qualities.size(); i++)
		DrawChip(out, layout.qualities[i], QualityName(Qualities[i]), i == QualityIndex, layout.qualities[i].contains(MousePosition));
	const int levelLabelWidth = GetLineWidth(_("Level")) + 10;
	DrawString(out, _("Level"), { { layout.levelMinus.position.x - levelLabelWidth, qualityRow.y }, { levelLabelWidth, ChipHeight } },
	    { .flags = UiFlags::VerticalCenter | UiFlags::ColorWhite });
	DrawChip(out, layout.levelMinus, "-", false, layout.levelMinus.contains(MousePosition));
	FillRectangle(out, layout.levelValue, FieldColor);
	DrawString(out, StrCat(Level), layout.levelValue, { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhitegold });
	DrawChip(out, layout.levelPlus, "+", false, layout.levelPlus.contains(MousePosition));

	// Result list
	FillRectangle(out, layout.list, FieldColor);
	const size_t rows = layout.visibleRows();
	for (size_t row = 0; row < rows && ScrollTop + row < Results.size(); row++) {
		const size_t index = ScrollTop + row;
		const SpawnerEntry &entry = Catalog[Results[index]];
		const Rectangle rowRect { layout.list.position + Displacement { 0, static_cast<int>(row) * RowHeight }, { layout.list.size.width, RowHeight } };
		if (index == Selected)
			FillRectangle(out, rowRect, RowSelectedColor);
		else if (rowRect.contains(MousePosition))
			FillRectangle(out, rowRect, RowHoverColor);
		const Rectangle textRect { rowRect.position + Displacement { 6, 0 }, { rowRect.size.width - 12, RowHeight } };
		DrawString(out, entry.name, textRect, { .flags = UiFlags::VerticalCenter | EntryColor(entry) });
		DrawString(out, entry.detail, textRect, { .flags = UiFlags::VerticalCenter | UiFlags::AlignRight | UiFlags::ColorButtonface });
	}
	if (Results.size() > rows) {
		// Scroll bar
		const int trackHeight = layout.list.size.height;
		const int thumbHeight = std::max(trackHeight * static_cast<int>(rows) / static_cast<int>(Results.size()), 12);
		const int thumbY = (trackHeight - thumbHeight) * static_cast<int>(ScrollTop) / std::max(static_cast<int>(Results.size() - rows), 1);
		FillRect(out, layout.list.position.x + layout.list.size.width - 3, layout.list.position.y + thumbY, 3, thumbHeight, BorderColor);
	}

	DrawPreview(out, layout.preview);

	DrawHorizontalLine(out, layout.footer.position - Displacement { 0, 4 }, layout.footer.size.width, DividerColor);
	DrawString(out, _("Type to search - Up/Down to pick - Tab: next category - Items go straight into your inventory"), layout.footer,
	    { .flags = UiFlags::VerticalCenter | UiFlags::AlignCenter | UiFlags::ColorButtonface });
}

} // namespace devilution
