/**
 * @file poe2/stash_tabs.hpp
 *
 * POE2 Mashup stash tabs: each stash page is a tab with its own name, colour and affinities. An affinity sends
 * that kind of item (currency, gems, flasks, charms, waystones, uniques) to its tab when it's Ctrl+clicked
 * into the stash. Each affinity belongs to one tab at a time, as in POE2.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "utils/attributes.h"

namespace devilution {

struct Item;

enum class StashAffinity : uint8_t {
	None = 0,
	Currency = 1 << 0,
	Gems = 1 << 1,
	Flasks = 1 << 2,
	Charms = 1 << 3,
	Waystones = 1 << 4,
	Uniques = 1 << 5,
};

constexpr StashAffinity AllStashAffinities[] = {
	StashAffinity::Currency,
	StashAffinity::Gems,
	StashAffinity::Flasks,
	StashAffinity::Charms,
	StashAffinity::Waystones,
	StashAffinity::Uniques,
};

constexpr unsigned MaxStashTabs = 100;
/** @brief A new stash starts with this many tabs; more are added from the tab list. */
constexpr unsigned DefaultStashTabs = 6;
constexpr size_t MaxStashTabNameLength = 16;
/** @brief Six hues in three shades: dark, mid and light. */
constexpr uint8_t StashTabColorCount = 18;
constexpr uint8_t StashTabColumns = 6;
/** @brief Dark grey, the colour of a new tab. */
constexpr uint8_t DefaultStashTabColor = 5;

struct StashTab {
	/** Empty shows the tab's number. */
	std::string name;
	uint8_t color = DefaultStashTabColor;
	/** StashAffinity bits. */
	uint8_t affinities = 0;
};

struct StashTabSet {
	std::vector<StashTab> tabs = std::vector<StashTab>(DefaultStashTabs);

	[[nodiscard]] unsigned count() const { return static_cast<unsigned>(tabs.size()); }
	/** @brief Back to a new stash's tabs. */
	void reset();
	/** @brief Adds tabs until there are at least this many (up to the maximum). */
	void ensureCount(unsigned count);
	/** @brief Adds a tab at the end; false once the stash has the maximum. */
	bool addTab();
	[[nodiscard]] std::string displayName(unsigned page) const;
	void rename(unsigned page, std::string_view name);
	void setColor(unsigned page, uint8_t color);
	[[nodiscard]] bool hasAffinity(unsigned page, StashAffinity affinity) const;
	/** @brief Gives the tab the affinity (taking it from any other tab) or takes it away. */
	void setAffinity(unsigned page, StashAffinity affinity, bool enabled);
	/** @brief The tab that holds the item's kind, if one has that affinity. */
	[[nodiscard]] std::optional<unsigned> affinityTabFor(const Item &item) const;
	/** @brief The affinities of a tab, like "Currency, Gems", or "" when it has none. */
	[[nodiscard]] std::string affinityText(unsigned page) const;
};

extern DVL_API_FOR_TEST StashTabSet StashTabs;

/** @brief The affinity that would take this item, or None. */
StashAffinity GetStashAffinity(const Item &item);
std::string_view StashAffinityName(StashAffinity affinity);
/** @brief The palette colour of a tab colour. */
uint8_t StashTabPaletteColor(uint8_t color);
/** @brief Light tab colours get dark text. */
bool IsLightStashTabColor(uint8_t color);

std::vector<uint8_t> SerializeStashTabs(const StashTabSet &tabs);
/** @brief Reads tabs written by SerializeStashTabs; leaves tabs untouched and returns false on bad data. */
bool DeserializeStashTabs(std::span<const uint8_t> data, StashTabSet &tabs);

} // namespace devilution
