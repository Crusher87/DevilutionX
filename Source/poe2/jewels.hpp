/**
 * @file poe2/jewels.hpp
 *
 * POE2 Mashup jewels: Rubies, Emeralds and Sapphires go in the passive tree's jewel sockets and add their modifiers.
 */
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "items.h"

#include "poe2/passive_tree.hpp"

namespace devilution {

struct Player;

/** @brief Which jewel slot a socket node uses, counting the tree's jewel sockets by node id. */
std::optional<size_t> JewelSocketIndex(PassiveNodeId node);
/** @brief The jewel in the socket node, or nullptr if it's empty. */
const Item *SocketedJewel(const Player &player, PassiveNodeId node);
/** @brief The jewels whose sockets are allocated; none in multiplayer, where passives aren't synced yet. */
std::vector<const Item *> ActiveJewels(const Player &player);

/**
 * @brief Puts the held jewel in the socket; a jewel already there goes into the hand.
 * @return false if the item isn't a jewel or the node isn't an allocated jewel socket.
 */
bool SocketJewel(Player &player, PassiveNodeId node, Item &held);
/** @brief The first allocated jewel socket without a jewel, if any. */
std::optional<PassiveNodeId> FreeJewelSocket(const Player &player);
/** @brief Takes the jewel out of the socket node. */
std::optional<Item> TakeJewel(Player &player, PassiveNodeId node);

} // namespace devilution
