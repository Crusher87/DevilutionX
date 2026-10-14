#include "poe2/stash_tabs.hpp"

#include <algorithm>

#include "engine/palette.h"
#include "items.h"
#include "poe2/charms.hpp"
#include "poe2/currency.hpp"
#include "poe2/flasks.hpp"
#include "poe2/gemcutting.hpp"
#include "poe2/skill_gems.hpp"
#include "poe2/waystones.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/str_cat.hpp"

namespace devilution {

StashTabSet StashTabs;

namespace {

constexpr uint8_t StashTabsVersion = 1;

constexpr uint8_t Hues[StashTabColumns] = { PAL16_RED, PAL16_ORANGE, PAL16_YELLOW, PAL16_BEIGE, PAL16_BLUE, PAL16_GRAY };
/** Shades within a 16-shade hue: 0 is the lightest. */
constexpr uint8_t Shades[3] = { 12, 8, 3 };

bool IsSupportGem(const Item &item)
{
	return item._iMiscId > IMISC_SUPPORTFIRST && item._iMiscId < IMISC_SUPPORTLAST;
}

bool IsCurrency(const Item &item)
{
	if (IsCurrencyMiscId(item._iMiscId))
		return true;
	if (item._iMiscId > IMISC_OILFIRST && item._iMiscId < IMISC_OILLAST)
		return true;
	return IsAnyOf(item._iMiscId, IMISC_ORB_BAUBLE, IMISC_SPIRIT_SKULL);
}

} // namespace

void StashTabSet::reset()
{
	tabs = std::vector<StashTab>(DefaultStashTabs);
}

void StashTabSet::ensureCount(unsigned count)
{
	count = std::min(count, MaxStashTabs);
	if (tabs.size() < count)
		tabs.resize(count);
}

bool StashTabSet::addTab()
{
	if (tabs.size() >= MaxStashTabs)
		return false;
	tabs.emplace_back();
	return true;
}

std::string StashTabSet::displayName(unsigned page) const
{
	if (page < tabs.size() && !tabs[page].name.empty())
		return tabs[page].name;
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
	if (page >= tabs.size())
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
	for (unsigned page = 0; page < tabs.size(); page++) {
		if (hasAffinity(page, affinity))
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

StashAffinity GetStashAffinity(const Item &item)
{
	if (item.isEmpty())
		return StashAffinity::None;
	if (IsWaystone(item))
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
	return data;
}

bool DeserializeStashTabs(std::span<const uint8_t> data, StashTabSet &tabs)
{
	if (data.size() < 2 || data[0] != StashTabsVersion)
		return false;
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
	// An affinity belongs to one tab; the first tab that claims it keeps it.
	uint8_t claimed = 0;
	for (StashTab &tab : result.tabs) {
		tab.affinities &= ~claimed;
		claimed |= tab.affinities;
	}
	tabs = std::move(result);
	return true;
}

} // namespace devilution
