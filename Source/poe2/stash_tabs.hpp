/**
 * @file poe2/stash_tabs.hpp
 *
 * POE2 Mashup stash tabs: each stash page is a tab with its own name, colour and kind.
 *
 * A blank tab is a free grid. Its affinities limit what it takes: a blank tab with the Currency affinity only takes
 * currency, one with none takes anything. Each affinity belongs to one tab at a time, as in POE2.
 *
 * The premade tabs (Currency, Gems, Maps and Uniques) lay their items out themselves, like POE2's special tabs.
 * Each item on one sits in one numbered slot, whatever its size. A tab's slots fill its own 10x10 grid first
 * and then extra grids saved under keys from ExtraStashGridBase, so the stash file keeps its format.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
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

enum class StashTabKind : uint8_t {
	/** A free grid the player arranges. */
	Blank,
	/** A slot for each kind of currency, essence and omen. */
	Currency,
	/** Gems listed by level, with filters. */
	Gems,
	/** Waystones by tier, with pages; tablets, fragments and keys under "Other". */
	Maps,
	/** One slot for each unique, a collection. */
	Uniques,
	Count,
};

constexpr StashTabKind PremadeStashTabKinds[] = {
	StashTabKind::Currency,
	StashTabKind::Gems,
	StashTabKind::Maps,
	StashTabKind::Uniques,
};

constexpr unsigned MaxStashTabs = 100;
/** @brief A new stash starts with this many blank tabs, then one of each premade tab. */
constexpr unsigned DefaultBlankStashTabs = 4;
constexpr unsigned DefaultStashTabs = DefaultBlankStashTabs + std::size(PremadeStashTabKinds);
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
	/** StashAffinity bits; blank tabs only. */
	uint8_t affinities = 0;
	StashTabKind kind = StashTabKind::Blank;

	[[nodiscard]] bool isPremade() const { return kind != StashTabKind::Blank; }
};

struct StashTabSet {
	std::vector<StashTab> tabs = DefaultTabs();

	static std::vector<StashTab> DefaultTabs();

	[[nodiscard]] unsigned count() const { return static_cast<unsigned>(tabs.size()); }
	/** @brief Back to a new stash's tabs. */
	void reset();
	/** @brief Adds tabs until there are at least this many (up to the maximum). */
	void ensureCount(unsigned count);
	/** @brief Adds a tab at the end; false once the stash has the maximum. */
	bool addTab(StashTabKind kind = StashTabKind::Blank);
	/** @brief Adds a premade tab of each kind the stash has none of, at the end, for stashes from before premade tabs. */
	void addMissingPremadeTabs();
	[[nodiscard]] StashTabKind kind(unsigned page) const;
	/** @brief Makes the tab another kind; the caller checks it's empty. Premade tabs have no affinities. */
	void setKind(unsigned page, StashTabKind kind);
	/** @brief Whether the tab takes the item: a premade tab its own kind, a blank tab anything its affinities allow. */
	[[nodiscard]] bool accepts(unsigned page, const Item &item) const;
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

/** @brief Blank currency, waystone and unique tabs keep their items sorted, like POE2's special tabs. */
bool IsSortedStashTab(unsigned page);

std::string_view StashTabKindName(StashTabKind kind);
/** @brief The colour a new premade tab of the kind gets. */
uint8_t DefaultStashTabColorFor(StashTabKind kind);

/** @brief A tab's first grid is keyed by its page; its extra grids from here, MaxStashGridsPerTab - 1 per tab. */
constexpr unsigned ExtraStashGridBase = 1000;
constexpr unsigned MaxStashGridsPerTab = 8;
constexpr unsigned StashGridCells = 100;

/** @brief The key in Stash.stashGrids of a tab's grid. */
unsigned StashGridKey(unsigned page, unsigned grid);
/** @brief Whether the key is an extra grid of a premade tab, and not a page. */
bool IsExtraStashGridKey(unsigned key);
/** @brief The page an extra grid belongs to. */
unsigned StashGridPage(unsigned key);
/** @brief How many slots a premade tab has; 0 for blank tabs. */
unsigned PremadeTabCapacity(StashTabKind kind);
/** @brief Whether the premade tab kind takes the item. */
bool PremadeTabTakes(StashTabKind kind, const Item &item);
/** @brief The stash sort order: by kind, then currency by type, waystones by tier, the rest by size and name. */
bool StashSortsBefore(const Item &a, const Item &b);

/** @brief The affinity that would take this item, or None. */
StashAffinity GetStashAffinity(const Item &item);
std::string_view StashAffinityName(StashAffinity affinity);
/** @brief The palette colour of a tab colour. */
uint8_t StashTabPaletteColor(uint8_t color);
/** @brief Light tab colours get dark text. */
bool IsLightStashTabColor(uint8_t color);

/** @brief Version 2 adds each tab's kind after the version 1 tabs. */
constexpr uint8_t StashTabsVersion = 2;

std::vector<uint8_t> SerializeStashTabs(const StashTabSet &tabs);
/**
 * @brief Reads tabs written by SerializeStashTabs, of this or an older version; leaves tabs untouched and returns false on bad data.
 * @param version Set to the version the data was written with.
 */
bool DeserializeStashTabs(std::span<const uint8_t> data, StashTabSet &tabs, uint8_t *version = nullptr);

} // namespace devilution
