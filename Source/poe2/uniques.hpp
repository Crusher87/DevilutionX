/**
 * @file poe2/uniques.hpp
 *
 * POE2 Mashup uniques from the itemization bundle (research/itemization-bundle-v5, made into
 * txtdata/poe2/uniques.tsv by tools/poe2_uniques.py). They join Diablo's uniques at the end of the
 * unique table and drop on the bundle's bases; each has item modifiers and rules (poe2/unique_rules.hpp).
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/unique_rules.hpp"

namespace devilution {

struct Item;

/** @brief On a unique, marks one of the bundle's uniques: its unique offset picks it among its base's bundle uniques. */
constexpr uint32_t CF_POE2_BUNDLE_UNIQUE = 1U << 6;
/** @brief Mapping ids of the bundle's uniques start here, after Diablo's. */
constexpr int32_t Poe2UniqueMappingId = 1000;

struct UniqueEffect {
	/** @brief An effect on the item itself, or a rule when it's None. */
	ModEffect mod = ModEffect::None;
	UniqueRule rule = UniqueRule::None;
	int16_t min = 0;
	int16_t max = 0;
	/** @brief Tooltip text, with {0} for the rolled value. */
	std::string text;
};

struct Poe2Unique {
	std::string name;
	/** @brief Index of its base in the item data. */
	int baseItem = -1;
	UniqueTier tier = UniqueTier::Common;
	/** @brief "global" for the generic pool, else the boss whose table holds it. */
	std::string pool;
	/** @brief POE2 drop level, 1 to 80. */
	int dropLevel = 1;
	/** @brief Index in the unique item table. */
	int uid = -1;
	std::vector<UniqueEffect> effects;

	[[nodiscard]] bool isGlobal() const { return pool == "global"; }
};

/** @brief Loads the bundle's uniques and adds them to the unique item table; none outside POE2 mode. */
void LoadPoe2Uniques();
const std::vector<Poe2Unique> &Poe2Uniques();
/** @brief The bundle unique with this unique table index, or nullptr for Diablo's. */
const Poe2Unique *Poe2UniqueFor(int uid);
const Poe2Unique *FindPoe2Unique(std::string_view name);
/** @brief The bundle uniques that drop on the base, in table order; a drop stores its place here as the unique offset. */
std::vector<int> Poe2UniquesOnBase(int itemIndex);

/** @brief The unique's effects with their values rolled from the item seed. */
std::vector<std::pair<const UniqueEffect *, int>> RollUniqueEffects(const Item &item);
/** @brief Applies a bundle unique's item modifiers; its rules count while it's worn (AddUniqueRules). */
void ApplyPoe2UniqueEffects(Item &item);
/** @brief Adds the rules of a worn bundle unique to the totals. */
void AddUniqueRules(const Item &item, UniqueRuleTotals &totals);
/** @brief The unique's tooltip lines. */
std::vector<std::string> Poe2UniqueText(const Item &item);

} // namespace devilution
