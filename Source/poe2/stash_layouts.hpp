/**
 * @file poe2/stash_layouts.hpp
 *
 * POE2 Mashup premade stash tabs: what goes in which slot of the Currency tab, how the Gems and Maps tabs group
 * their items, and the unique collection of the Uniques tab. The slot numbers are saved with the stash, so new
 * kinds of currency take unused slots and nothing moves.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "items.h"

namespace devilution {

// --- Currency tab ---

/** @brief The sections of the Currency tab; each owns a range of slots. */
enum class CurrencySection : uint8_t {
	Orbs,
	Catalysts,
	Augments,
	/** Currency without a slot of its own (oils, skulls, books): first free slot. */
	Other,
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

/** @brief How many are in a stack on the Currency tab, where stacks go to CurrencyTabStackMax. */
int StoredStackCount(const Item &item);
void SetStoredStackCount(Item &item, int count);

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
