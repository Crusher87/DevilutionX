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
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "poe2/loot.hpp"
#include "poe2/modifiers.hpp"
#include "poe2/unique_rules.hpp"
#include "poe2/weapon_kinds.hpp"

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
	/**
	 * @brief Index of the base its saved items are on: the one it had before it moved (Curtis 2026-10-27: the heavy,
	 * swift and crossbow bases stopped dropping), else its base.
	 */
	int savedBaseItem = -1;
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
/**
 * @brief The weapon kind a Diablo staff unique belongs on in POE2 Mashup (diablo_unique_homes.tsv, caster-weapons-plan-v2.md
 * 6.14): a caster staff or a quarterstaff; None for every other unique, and outside POE2 mode.
 */
WeaponKind DiabloUniqueHome(int uid);
/**
 * @brief Whether a Diablo unique can be made on the item row. Only a new caster row (a caster staff, wand or sceptre of
 * the rework) turns away a unique whose home is another kind; legacy rows, Diablo's staves and quarterstaves take every
 * one they did, so saved uniques rebuild as before.
 */
bool DiabloUniqueFitsRow(int uid, int itemIndex);
/**
 * @brief The bundle uniques of the base, whose place here a drop stores as its unique offset: those saved on it in table
 * order, so saved items keep theirs, then those that moved to it.
 */
std::vector<int> Poe2UniquesOnBase(int itemIndex);

/** @brief The unique's effects with their values rolled from the item seed. */
std::vector<std::pair<const UniqueEffect *, int>> RollUniqueEffects(const Item &item);
/** @brief Applies a bundle unique's item modifiers; its rules count while it's worn (AddUniqueRules). */
void ApplyPoe2UniqueEffects(Item &item);
/** @brief Adds the rules of a worn bundle unique to the totals. */
void AddUniqueRules(const Item &item, UniqueRuleTotals &totals);
/** @brief A unique's flavour text lines by its name (Diablo's, the bundle's or a belt unique), from txtdata/poe2/unique_lore.tsv; empty if it has none. */
std::span<const std::string> UniqueLore(std::string_view name);
/** @brief The unique's tooltip lines. */
std::vector<std::string> Poe2UniqueText(const Item &item);

} // namespace devilution
