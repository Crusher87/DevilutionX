/**
 * @file poe2/tree_telemetry.hpp
 *
 * POE2 Mashup passive tree tracker: the play log's "tree" channel. Every node taken or refunded, every reset, and the
 * whole tree at game start, each level-up and game end go to the event file; the same go to tree_history.tsv and
 * tree_builds.tsv in poe2_logs, which are never pruned, so a hero's tree can be followed across every run.
 * Read by tools/tree_report.py and the P screen's heat map. Nothing here changes the game or the save.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "poe2/passive_tree.hpp"

namespace devilution {

struct Player;

/** @brief A node was just taken (or refunded) with that pool's points. */
void TelemetryPassiveTaken(const Player &player, PassiveNodeId node, PassivePool pool);
void TelemetryPassiveRefunded(const Player &player, PassiveNodeId node, PassivePool pool);
/** @brief Many nodes are about to be cleared at once: a full reset, a tree update's refund, or the weapon sets' refund. */
void TelemetryPassivesCleared(const Player &player, std::string_view reason, int nodes);
/** @brief The whole tree as it stands: what's taken in each pool, the notables and roads, and what it all adds up to. */
void TelemetryTreeSnapshot(const Player &player, std::string_view when);

/** @brief The P screen opened or closed: the log notes how long it stayed open and what changed meanwhile. */
void TelemetryTreeOpened(const Player &player);
void TelemetryTreeClosed(const Player &player);

/** @brief What every hero in tree_history.tsv and tree_builds.tsv did with each node of the current tree, for the P screen's heat map. */
struct TreeHeat {
	/** Heroes with a build on this tree. */
	int heroes = 0;
	/** By node id: heroes whose latest build holds the node (any pool), times taken, times refunded. */
	std::vector<uint16_t> holders;
	std::vector<uint16_t> takes;
	std::vector<uint16_t> refunds;
	uint16_t mostHolders = 0;
};
/** @brief Reads the kept tree files; all empty when there are none or the play log isn't open. */
TreeHeat LoadTreeHeat();

/** @brief The console's tree() command: points, notables by area, roads walked, and the tree's totals. */
std::string TreeTrackerText(const Player &player);

} // namespace devilution
