/**
 * @file poe2/stash_layouts.hpp
 *
 * POE2 Mashup premade stash tabs: what goes in which slot of the Currency, Runes and Essences tabs, how the Gems and
 * Maps tabs group their items, and the unique collection of the Uniques tab. The slot numbers are saved with the
 * stash, so new kinds of currency take unused slots and nothing moves.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "items.h"
#include "poe2/crafting_items.hpp"
#include "poe2/stash_tabs.hpp"

namespace devilution {

// --- Currency tab ---

/** @brief The sections of the Currency tab; each owns a range of slots. */
enum class CurrencySection : uint8_t {
	Orbs,
	Catalysts,
	/**
	 * Slots 40-49, where augments were before they got the Runes tab (stash tabs version 4). They stay empty and are
	 * never given to anything else: a stash saved before version 4 has augments there, which move on load.
	 */
	Augments,
	/** Currency without a slot of its own (oils, skulls, books): first free slot. */
	Other,
	/** Slots 70-119, where essences were before the Essences tab; empty and never reused, as Augments. */
	Essences,
	Omens,
	Count,
};

struct CurrencySectionRange {
	unsigned first;
	unsigned count;
};

constexpr unsigned CurrencyTabSlots = 150;
/** @brief A currency slot holds this many before it's full, like POE2's currency tab. */
constexpr int CurrencyTabStackMax = 5000;

CurrencySectionRange GetCurrencySection(CurrencySection section);
std::string_view CurrencySectionName(CurrencySection section);
/** @brief The kind of currency a slot holds: its misc id and variant; IMISC_INVALID for an unused or Other slot. */
struct CurrencySlotKind {
	item_misc_id miscId = IMISC_INVALID;
	uint8_t variant = 0;
};
CurrencySlotKind GetCurrencySlotKind(unsigned slot);
/** @brief The slot that holds this kind of currency, if it has one; Other-section currency has none. */
std::optional<unsigned> CurrencyFixedSlot(const Item &item);
/** @brief Makes a fresh item of the slot's kind, for its picture and name; false for slots without a kind. */
bool MakeCurrencySlotItem(unsigned slot, Item &item);

/** @brief How many are in a stack on the Currency, Runes or Essences tab, where stacks go to CurrencyTabStackMax. */
int StoredStackCount(const Item &item);
void SetStoredStackCount(Item &item, int count);

// --- Runes tab (stash tabs version 4) ---

/** @brief The Runes tab's pages; each owns a range of slots. */
enum class RunePage : uint8_t {
	/** A row of four slots for each rune family (RuneFamily order): Lesser, plain and Greater; the fourth, once Perfect, stays empty. */
	Runes,
	RunesOfLegend,
	SoulstonesAndRelics,
	Count,
};

constexpr unsigned RuneTabSlots = 200;
/** @brief The slots of a rune family's row, one for each strength. */
constexpr unsigned RuneRowSlots = 4;

struct StashSlotRange {
	unsigned first;
	unsigned count;
};

StashSlotRange GetRunePage(RunePage page);
std::string_view RunePageName(RunePage page);
/** @brief The Runes tab slot of an augment item; none for anything else. */
std::optional<unsigned> RuneTabSlot(const Item &item);
/** @brief The augment a Runes tab slot holds; None for an unused slot. */
AugmentId RuneTabSlotAugment(unsigned slot);

// --- Essences tab (stash tabs version 4) ---

constexpr unsigned EssenceTabSlots = 100;
/** @brief The families' slots: four for each of EssenceFamilies(), by strength. */
constexpr unsigned EssenceFamilySlotsFirst = 0;
/** @brief The Evil essences' slots, in EssenceKind order. */
constexpr unsigned EssenceEvilSlotsFirst = 80;
/** @brief The tall slot in the middle: an item to use the tab's essences on. */
constexpr unsigned EssenceCraftSlot = 99;

/** @brief The Essences tab slot of an essence; none for anything else. */
std::optional<unsigned> EssenceTabSlot(const Item &item);
/** @brief The essence variant an Essences tab slot holds; 0 for an unused slot and the craft slot. */
uint8_t EssenceTabSlotVariant(unsigned slot);
/** @brief Whether the item can sit in the Essences tab's craft slot: gear essences can be used on. */
bool EssenceCraftSlotTakes(const Item &item);

/** @brief Makes a fresh item of the kind a premade tab's slot holds, for its dimmed picture and name; false for slots without a kind. */
bool MakePremadeSlotItem(StashTabKind kind, unsigned slot, Item &item);

// --- Gems tab ---

constexpr unsigned GemTabSlots = 500;

enum class GemFilter : uint8_t {
	All,
	Skill,
	Support,
	/** Spirit gems: POE2's persistent buffs. */
	Spirit,
	Uncut,
	Count,
};

std::string_view GemFilterName(GemFilter filter);
bool GemMatchesFilter(const Item &item, GemFilter filter);
/** @brief The heading a gem is listed under: skill, Spirit and uncut gems by level, supports by tier. */
struct GemSection {
	/** 0 for levels, 1 for support tiers; lower lists first. */
	int group;
	/** Higher lists first. */
	int level;

	bool operator==(const GemSection &) const = default;
};
GemSection GetGemSection(const Item &item);
std::string GemSectionName(GemSection section);
/** @brief The Gems tab order: by section, then by name. */
bool GemListsBefore(const Item &a, const Item &b);

// --- Maps tab ---

constexpr unsigned MapTabSlots = 600;
/** @brief The Maps tab's tiers: 1 to 16 for waystones, 0 for tablets, fragments and keys. */
constexpr int MapTabOtherTier = 0;

int MapTabTier(const Item &item);
/** @brief The Maps tab order: by tier, then rarity, then name. */
bool MapListsBefore(const Item &a, const Item &b);

// --- Uniques tab ---

enum class UniqueCategory : uint8_t {
	Weapons,
	Armour,
	Helmets,
	Shields,
	Rings,
	Amulets,
	/** Unique flasks and charms. */
	Belt,
	Other,
	Count,
};

struct UniqueCollectionEntry {
	std::string name;
	/** The cursor graphic of the unique's picture, as an item's _iCurs. */
	int cursor = 0;
	UniqueCategory category = UniqueCategory::Other;
	/** False for table entries with no item base in this game; they have a slot but aren't shown. */
	bool listed = false;
};

std::string_view UniqueCategoryName(UniqueCategory category);
/** @brief Every unique, by slot: Diablo's and the bundle's by unique table index, then the belt uniques. */
const std::vector<UniqueCollectionEntry> &UniqueCollection();
size_t UniqueCollectionSlots();
/** @brief The collection slot of a unique item. */
std::optional<unsigned> UniqueCollectionSlot(const Item &item);

} // namespace devilution
