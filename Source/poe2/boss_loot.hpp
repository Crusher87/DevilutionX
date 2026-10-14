/**
 * @file poe2/boss_loot.hpp
 *
 * POE2 Mashup boss unique tables from the itemization bundle (03_Unique_Item_Data/boss_tables.json): each of the
 * bundle's bosses is played by one or more of Diablo's bosses and rolls a unique from its own pool when it dies.
 */
#pragma once

#include <string_view>
#include <vector>

namespace devilution {

struct Monster;

struct BossUniqueEntry {
	/** @brief A bundle unique (poe2/uniques.hpp) or a unique flask or charm (poe2/belt_uniques.hpp). */
	std::string_view unique;
	int weight;
};

struct BossLootTable {
	/** @brief The bundle's boss id, which the uniques' pool names. */
	std::string_view id;
	std::string_view name;
	/** @brief Chance to drop one of its uniques, in percent: 18 for area bosses, 100 for pinnacle bosses. */
	int chancePercent;
	/** @brief Diablo's unique monsters that play the boss. */
	std::vector<std::string_view> monsters;
	/** @brief Diablo himself plays it on Hell difficulty. */
	bool diabloOnHell;
	std::vector<BossUniqueEntry> pool;
};

const std::vector<BossLootTable> &BossLootTables();
/** @brief The boss table the monster plays, or nullptr. */
const BossLootTable *BossLootTableFor(const Monster &monster);

} // namespace devilution
