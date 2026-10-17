#include "poe2/stash_tabs.hpp"

#include <algorithm>
#include <iterator>

#include "engine/palette.h"
#include "inv.h"
#include "items.h"
#include "poe2/charms.hpp"
#include "poe2/crafting_items.hpp"
#include "poe2/currency.hpp"
#include "poe2/endgame.hpp"
#include "poe2/flasks.hpp"
#include "poe2/gemcutting.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/stacking.hpp"
#include "poe2/stash_layouts.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/waystones.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/str_cat.hpp"

namespace devilution {

StashTabSet StashTabs;

namespace {

constexpr uint8_t Hues[StashTabColumns] = { PAL16_RED, PAL16_ORANGE, PAL16_YELLOW, PAL16_BEIGE, PAL16_BLUE, PAL16_GRAY };
/** Shades within a 16-shade hue: 0 is the lightest. */
constexpr uint8_t Shades[3] = { 12, 8, 3 };

bool IsSupportGem(const Item &item)
{
	return SupportGemForItem(item) != SupportGem::None;
}

bool IsCurrency(const Item &item)
{
	if (IsCurrencyMiscId(item._iMiscId))
		return true;
	if (item._iMiscId > IMISC_OILFIRST && item._iMiscId < IMISC_OILLAST)
		return true;
	return IsAnyOf(item._iMiscId, IMISC_ORB_BAUBLE, IMISC_SPIRIT_SKULL, IMISC_SPEC_BOOK);
}

} // namespace

std::vector<StashTab> StashTabSet::DefaultTabs()
{
	std::vector<StashTab> result(DefaultBlankStashTabs);
	for (const StashTabKind kind : PremadeStashTabKinds)
		result.push_back({ .color = DefaultStashTabColorFor(kind), .kind = kind });
	return result;
}

void StashTabSet::reset()
{
	tabs = DefaultTabs();
}

void StashTabSet::ensureCount(unsigned count)
{
	count = std::min(count, MaxStashTabs);
	if (tabs.size() < count)
		tabs.resize(count);
}

bool StashTabSet::addTab(StashTabKind kind)
{
	if (tabs.size() >= MaxStashTabs)
		return false;
	tabs.push_back({ .color = kind == StashTabKind::Blank ? DefaultStashTabColor : DefaultStashTabColorFor(kind), .kind = kind });
	return true;
}

void StashTabSet::addMissingPremadeTabs()
{
	for (const StashTabKind kind : PremadeStashTabKinds) {
		if (std::none_of(tabs.begin(), tabs.end(), [kind](const StashTab &tab) { return tab.kind == kind; }))
			addTab(kind);
	}
}

StashTabKind StashTabSet::kind(unsigned page) const
{
	return page < tabs.size() ? tabs[page].kind : StashTabKind::Blank;
}

void StashTabSet::setKind(unsigned page, StashTabKind kind)
{
	if (page >= tabs.size() || kind >= StashTabKind::Count)
		return;
	tabs[page].kind = kind;
	if (kind != StashTabKind::Blank)
		tabs[page].affinities = 0;
}

bool StashTabSet::accepts(unsigned page, const Item &item) const
{
	if (item.isEmpty() || page >= tabs.size())
		return false;
	const StashTab &tab = tabs[page];
	if (tab.isPremade())
		return PremadeTabTakes(tab.kind, item);
	if (tab.affinities == 0)
		return true;
	return (tab.affinities & static_cast<uint8_t>(GetStashAffinity(item))) != 0;
}

std::string StashTabSet::displayName(unsigned page) const
{
	if (page < tabs.size() && !tabs[page].name.empty())
		return tabs[page].name;
	if (page < tabs.size() && tabs[page].isPremade())
		return std::string(StashTabKindName(tabs[page].kind));
	return StrCat(page + 1);
}

void StashTabSet::rename(unsigned page, std::string_view name)
{
	if (page >= tabs.size())
		return;
	tabs[page].name = std::string(name.substr(0, MaxStashTabNameLength));
}

void StashTabSet::setColor(unsigned page, uint8_t color)
{
	if (page < tabs.size() && color < StashTabColorCount)
		tabs[page].color = color;
}

bool StashTabSet::hasAffinity(unsigned page, StashAffinity affinity) const
{
	return page < tabs.size() && (tabs[page].affinities & static_cast<uint8_t>(affinity)) != 0;
}

void StashTabSet::setAffinity(unsigned page, StashAffinity affinity, bool enabled)
{
	if (page >= tabs.size() || (enabled && tabs[page].isPremade()))
		return;
	const auto bit = static_cast<uint8_t>(affinity);
	if (enabled) {
		for (StashTab &tab : tabs)
			tab.affinities &= ~bit;
		tabs[page].affinities |= bit;
	} else {
		tabs[page].affinities &= ~bit;
	}
}

std::optional<unsigned> StashTabSet::affinityTabFor(const Item &item) const
{
	const StashAffinity affinity = GetStashAffinity(item);
	if (affinity == StashAffinity::None)
		return std::nullopt;
	// A blank tab the player gave its affinity, else the premade tab for it (Currency, Gems, Maps, Uniques).
	for (unsigned page = 0; page < tabs.size(); page++) {
		if (hasAffinity(page, affinity))
			return page;
	}
	for (unsigned page = 0; page < tabs.size(); page++) {
		if (tabs[page].isPremade() && PremadeTabTakes(tabs[page].kind, item))
			return page;
	}
	return std::nullopt;
}

std::string StashTabSet::affinityText(unsigned page) const
{
	std::string text;
	for (const StashAffinity affinity : AllStashAffinities) {
		if (!hasAffinity(page, affinity))
			continue;
		if (!text.empty())
			text += ", ";
		text += StashAffinityName(affinity);
	}
	return text;
}

bool IsSortedStashTab(unsigned page)
{
	if (StashTabs.kind(page) != StashTabKind::Blank)
		return false;
	return StashTabs.hasAffinity(page, StashAffinity::Currency) || StashTabs.hasAffinity(page, StashAffinity::Waystones)
	    || StashTabs.hasAffinity(page, StashAffinity::Uniques);
}

namespace {

int AffinityRank(const Item &item)
{
	const StashAffinity affinity = GetStashAffinity(item);
	for (size_t i = 0; i < std::size(AllStashAffinities); i++) {
		if (AllStashAffinities[i] == affinity)
			return static_cast<int>(i);
	}
	return static_cast<int>(std::size(AllStashAffinities));
}

} // namespace

bool StashSortsBefore(const Item &a, const Item &b)
{
	const int rankA = AffinityRank(a);
	const int rankB = AffinityRank(b);
	if (rankA != rankB)
		return rankA < rankB;
	const StashAffinity affinity = GetStashAffinity(a);
	if (affinity == StashAffinity::Waystones && WaystoneTier(a) != WaystoneTier(b))
		return WaystoneTier(a) < WaystoneTier(b);
	if (affinity != StashAffinity::Currency && affinity != StashAffinity::Waystones) {
		// Big items first so the tab packs tightly.
		const Size sizeA = GetInventorySize(a);
		const Size sizeB = GetInventorySize(b);
		if (sizeA.height != sizeB.height)
			return sizeA.height > sizeB.height;
		if (sizeA.width != sizeB.width)
			return sizeA.width > sizeB.width;
	}
	if (a.IDidx != b.IDidx)
		return a.IDidx < b.IDidx;
	if ((a.dwBuff & CF_POE2_VARIANT) != (b.dwBuff & CF_POE2_VARIANT))
		return (a.dwBuff & CF_POE2_VARIANT) < (b.dwBuff & CF_POE2_VARIANT);
	const int compared = std::string_view(a._iIName).compare(b._iIName);
	if (compared != 0)
		return compared < 0;
	return StackCount(a) > StackCount(b);
}

StashAffinity GetStashAffinity(const Item &item)
{
	if (item.isEmpty())
		return StashAffinity::None;
	if (IsWaystone(item) || IsEndgameItem(item))
		return StashAffinity::Waystones;
	if (IsCurrency(item))
		return StashAffinity::Currency;
	if (IsSkillGem(item) || IsSupportGem(item) || IsUncutGem(item))
		return StashAffinity::Gems;
	if (IsCharm(item))
		return StashAffinity::Charms;
	if (IsFlaskPotion(item))
		return StashAffinity::Flasks;
	if (item._iMagical == ITEM_QUALITY_UNIQUE)
		return StashAffinity::Uniques;
	return StashAffinity::None;
}

std::string_view StashAffinityName(StashAffinity affinity)
{
	switch (affinity) {
	case StashAffinity::Currency:
		return _("Currency");
	case StashAffinity::Gems:
		return _("Gems");
	case StashAffinity::Flasks:
		return _("Flasks");
	case StashAffinity::Charms:
		return _("Charms");
	case StashAffinity::Waystones:
		return _("Waystones");
	case StashAffinity::Uniques:
		return _("Uniques");
	default:
		return {};
	}
}

std::string_view StashTabKindName(StashTabKind kind)
{
	switch (kind) {
	case StashTabKind::Currency:
		return _("Currency");
	case StashTabKind::Gems:
		return _("Gems");
	case StashTabKind::Maps:
		return _("Maps");
	case StashTabKind::Uniques:
		return _("Uniques");
	default:
		return _("Blank");
	}
}

uint8_t DefaultStashTabColorFor(StashTabKind kind)
{
	// Hue + 6 * shade: 0-5 dark, 6-11 mid, 12-17 light (red, orange, yellow, beige, blue, grey).
	switch (kind) {
	case StashTabKind::Currency:
		return 8;
	case StashTabKind::Gems:
		return 10;
	case StashTabKind::Maps:
		return 3;
	case StashTabKind::Uniques:
		return 7;
	default:
		return DefaultStashTabColor;
	}
}

unsigned StashGridKey(unsigned page, unsigned grid)
{
	if (grid == 0)
		return page;
	return ExtraStashGridBase + (page * (MaxStashGridsPerTab - 1)) + (grid - 1);
}

bool IsExtraStashGridKey(unsigned key)
{
	return key >= ExtraStashGridBase;
}

unsigned StashGridPage(unsigned key)
{
	if (!IsExtraStashGridKey(key))
		return key;
	return (key - ExtraStashGridBase) / (MaxStashGridsPerTab - 1);
}

unsigned PremadeTabCapacity(StashTabKind kind)
{
	constexpr unsigned MaxSlots = MaxStashGridsPerTab * StashGridCells;
	switch (kind) {
	case StashTabKind::Currency:
		return CurrencyTabSlots;
	case StashTabKind::Gems:
		return GemTabSlots;
	case StashTabKind::Maps:
		return MapTabSlots;
	case StashTabKind::Uniques:
		return std::min<unsigned>(static_cast<unsigned>(UniqueCollectionSlots()), MaxSlots);
	default:
		return 0;
	}
}

bool PremadeTabTakes(StashTabKind kind, const Item &item)
{
	switch (kind) {
	case StashTabKind::Currency:
		return GetStashAffinity(item) == StashAffinity::Currency;
	case StashTabKind::Gems:
		return GetStashAffinity(item) == StashAffinity::Gems;
	case StashTabKind::Maps:
		return GetStashAffinity(item) == StashAffinity::Waystones;
	case StashTabKind::Uniques:
		return UniqueCollectionSlot(item).has_value();
	default:
		return false;
	}
}

uint8_t StashTabPaletteColor(uint8_t color)
{
	color = std::min<uint8_t>(color, StashTabColorCount - 1);
	return Hues[color % StashTabColumns] + Shades[color / StashTabColumns];
}

bool IsLightStashTabColor(uint8_t color)
{
	return color / StashTabColumns == 2;
}

std::vector<uint8_t> SerializeStashTabs(const StashTabSet &tabs)
{
	std::vector<uint8_t> data;
	data.push_back(StashTabsVersion);
	data.push_back(static_cast<uint8_t>(tabs.count()));
	for (const StashTab &tab : tabs.tabs) {
		data.push_back(tab.color);
		data.push_back(tab.affinities);
		const size_t length = std::min(tab.name.size(), MaxStashTabNameLength);
		data.push_back(static_cast<uint8_t>(length));
		data.insert(data.end(), tab.name.begin(), tab.name.begin() + static_cast<std::ptrdiff_t>(length));
	}
	// Version 2: the kinds, after the version 1 data.
	for (const StashTab &tab : tabs.tabs)
		data.push_back(static_cast<uint8_t>(tab.kind));
	return data;
}

bool DeserializeStashTabs(std::span<const uint8_t> data, StashTabSet &tabs, uint8_t *version)
{
	if (data.size() < 2 || data[0] == 0 || data[0] > StashTabsVersion)
		return false;
	const uint8_t dataVersion = data[0];
	const unsigned count = data[1];
	if (count == 0 || count > MaxStashTabs)
		return false;
	StashTabSet result;
	result.tabs.clear();
	size_t offset = 2;
	for (unsigned i = 0; i < count; i++) {
		if (offset + 3 > data.size())
			return false;
		StashTab tab;
		tab.color = std::min<uint8_t>(data[offset], StashTabColorCount - 1);
		tab.affinities = data[offset + 1];
		const size_t length = data[offset + 2];
		offset += 3;
		if (length > MaxStashTabNameLength || offset + length > data.size())
			return false;
		tab.name.assign(reinterpret_cast<const char *>(data.data() + offset), length);
		offset += length;
		result.tabs.push_back(std::move(tab));
	}
	if (dataVersion >= 2) {
		if (offset + count > data.size())
			return false;
		for (StashTab &tab : result.tabs) {
			const uint8_t kind = data[offset++];
			tab.kind = kind < static_cast<uint8_t>(StashTabKind::Count) ? static_cast<StashTabKind>(kind) : StashTabKind::Blank;
			if (tab.isPremade())
				tab.affinities = 0;
		}
	}
	// An affinity belongs to one tab; the first tab that claims it keeps it.
	uint8_t claimed = 0;
	for (StashTab &tab : result.tabs) {
		tab.affinities &= ~claimed;
		claimed |= tab.affinities;
	}
	tabs = std::move(result);
	if (version != nullptr)
		*version = dataVersion;
	return true;
}

} // namespace devilution
