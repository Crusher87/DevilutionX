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

/**
 * @brief How far a boss unique's drop level may be above the boss's POE2 item level (its area level plus the boss bonus)
 * and still drop from it (Curtis, Rogue run 2: the Butcher dropped Iron Testament, drop level 70, at character level 5).
 */
constexpr int BossUniqueLevelSlack = 3;

const std::vector<BossLootTable> &BossLootTables();
/** @brief A boss unique's POE2 drop level, or 0 when the game has no unique of that name. */
int BossUniqueDropLevel(std::string_view unique);
/**
 * @brief The entries of the boss's table it may drop at this POE2 item level: those whose drop level is at most
 * BossUniqueLevelSlack above it. Empty for an early boss whose uniques are endgame ones.
 */
std::vector<BossUniqueEntry> BossUniquesAt(const BossLootTable &table, int bossItemLevel);
/** @brief The boss table the monster plays, or nullptr. */
const BossLootTable *BossLootTableFor(const Monster &monster);

} // namespace devilution
