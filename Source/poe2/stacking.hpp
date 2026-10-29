/**
 * @file poe2/stacking.hpp
 *
 * POE2 Mashup: currency stacks. Orbs, essences, omens, runes and catalysts of the same kind share one
 * inventory or stash cell up to a stack limit. The count lives in the item's charges.
 */
#pragma once

#include <optional>

#include "items.h"

namespace devilution {

struct Player;

/** @brief How many of one currency (or Scrolls of Identify) fit in one cell. */
constexpr int CurrencyStackMax = 40;

/** @brief A Scroll of Identify, which stacks like currency and is only used up when it identifies something. */
bool IsIdentifyScroll(const Item &item);
/** @brief Currency and Scrolls of Identify stack; everything else is one item per cell. */
bool IsStackable(const Item &item);
/** @brief POE2 Mashup: a shard item, which stacks like currency up to ShardsPerOrb (Curtis 2026-10-29). */
bool IsShard(const Item &item);
/** @brief The most of the item one inventory cell holds: ShardsPerOrb for shards, CurrencyStackMax for other currency. */
int StackMax(const Item &item);
/**
 * @brief A stack of ShardsPerOrb shards becomes one of their orb in its place, like POE2's, keeping its seed.
 * @return true if it did.
 */
bool CombineShardStack(Item &stack);
/** @brief The number of items in the stack; 1 for anything that doesn't stack. */
int StackCount(const Item &item);
void SetStackCount(Item &item, int count);
/** @brief Both stack and are the same kind (and the same essence, omen, rune or catalyst). */
bool CanStackWith(const Item &stack, const Item &item);
/** @brief How many more fit on the stack. */
int StackRoom(const Item &stack);
/**
 * @brief Moves as much of the item onto the stack as fits; shards that fill the stack become their orb there.
 * @return true if all of it fit, and the item is now empty.
 */
bool MergeStack(Item &stack, Item &item);
/**
 * @brief Uses one item of the stack.
 * @return true if some are left, false if that was the last one and the caller removes the item.
 */
bool TakeOneFromStack(Item &stack);
/** @brief Takes half of a stack off it, rounded down; nothing if the stack has a single item. */
std::optional<Item> SplitStack(Item &stack);
/** @brief Takes count items off a stack; nothing unless at least one is left behind. */
std::optional<Item> SplitStack(Item &stack, int count);

/** @brief The inventory stacks that match the item have room for all of it. */
bool InventoryStacksHaveRoom(const Player &player, const Item &item);
/**
 * @brief Merges the item into the inventory stacks that match it, when they have room for all of it.
 * @return true if the item was merged; it then needs no cell of its own.
 */
bool MergeIntoInventoryStacks(Player &player, const Item &item, bool sendNetworkMessage);

} // namespace devilution
