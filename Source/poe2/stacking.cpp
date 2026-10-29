#include "poe2/stacking.hpp"

#include <algorithm>

#include "game_mode.hpp"
#include "msg.h"
#include "player.h"
#include "poe2/crafting_items.hpp"
#include "poe2/currency.hpp"
#include "poe2/economy.hpp"

namespace devilution {

bool IsIdentifyScroll(const Item &item)
{
	return !item.isEmpty() && item._iMiscId == IMISC_SCROLL && item._iSpell == SpellID::Identify;
}

bool IsShard(const Item &item)
{
	return gbIsPoe2 && !item.isEmpty() && item._iClass == ICLASS_MISC && ShardKindForMiscId(item._iMiscId).has_value();
}

bool IsStackable(const Item &item)
{
	return gbIsPoe2 && !item.isEmpty() && item._iClass == ICLASS_MISC && (IsCurrencyMiscId(item._iMiscId) || IsIdentifyScroll(item) || IsShard(item));
}

int StackMax(const Item &item)
{
	return IsShard(item) ? ShardsPerOrb : CurrencyStackMax;
}

bool CombineShardStack(Item &stack)
{
	const std::optional<ShardKind> kind = IsShard(stack) ? ShardKindForMiscId(stack._iMiscId) : std::nullopt;
	if (!kind || StackCount(stack) < ShardsPerOrb)
		return false;
	const item_misc_id orb = ShardCurrency(*kind);
	std::optional<_item_indexes> base;
	for (size_t i = 0; i < AllItemsList.size() && !base; i++) {
		if (AllItemsList[i].iMiscId == orb && IsItemAvailable(static_cast<int>(i)))
			base = static_cast<_item_indexes>(i);
	}
	if (!base)
		return false;
	// Made like a starting item (no creation info), so it rebuilds the same from its base and seed.
	const Point position = stack.position;
	const uint32_t seed = stack._iSeed;
	InitializeItem(stack, *base);
	stack._iSeed = seed;
	stack._iIdentified = true;
	stack._iStatFlag = true;
	stack.position = position;
	return true;
}

int StackCount(const Item &item)
{
	if (!IsStackable(item))
		return 1;
	return std::clamp(item._iCharges, 1, StackMax(item));
}

void SetStackCount(Item &item, int count)
{
	item._iMaxCharges = StackMax(item);
	item._iCharges = std::clamp(count, 1, StackMax(item));
}

bool CanStackWith(const Item &stack, const Item &item)
{
	if (&stack == &item || !IsStackable(stack) || !IsStackable(item))
		return false;
	// The shop's Scroll of Identify and the one found in the dungeon are different bases but the same scroll.
	const bool sameBase = stack.IDidx == item.IDidx || (IsIdentifyScroll(stack) && IsIdentifyScroll(item));
	return sameBase && stack._iMiscId == item._iMiscId
	    && (stack.dwBuff & CF_POE2_VARIANT) == (item.dwBuff & CF_POE2_VARIANT);
}

int StackRoom(const Item &stack)
{
	return IsStackable(stack) ? StackMax(stack) - StackCount(stack) : 0;
}

bool MergeStack(Item &stack, Item &item)
{
	if (!CanStackWith(stack, item))
		return false;
	const int moved = std::min(StackRoom(stack), StackCount(item));
	if (moved <= 0)
		return false;
	SetStackCount(stack, StackCount(stack) + moved);
	// POE2 Mashup: ten shards in a stack become their orb (Curtis 2026-10-29).
	CombineShardStack(stack);
	const int left = StackCount(item) - moved;
	if (left <= 0) {
		item.clear();
		return true;
	}
	SetStackCount(item, left);
	return false;
}

bool TakeOneFromStack(Item &stack)
{
	const int count = StackCount(stack);
	if (count <= 1)
		return false;
	SetStackCount(stack, count - 1);
	return true;
}

std::optional<Item> SplitStack(Item &stack)
{
	return SplitStack(stack, StackCount(stack) / 2);
}

std::optional<Item> SplitStack(Item &stack, int count)
{
	const int have = StackCount(stack);
	if (count <= 0 || count >= have)
		return std::nullopt;
	Item part = stack;
	GenerateNewSeed(part); // Its own seed, so the two parts are told apart.
	SetStackCount(part, count);
	SetStackCount(stack, have - count);
	return part;
}

bool InventoryStacksHaveRoom(const Player &player, const Item &item)
{
	if (!IsStackable(item))
		return false;
	int room = 0;
	for (int i = 0; i < player._pNumInv; i++) {
		if (CanStackWith(player.InvList[i], item))
			room += StackRoom(player.InvList[i]);
	}
	return room >= StackCount(item);
}

bool MergeIntoInventoryStacks(Player &player, const Item &item, bool sendNetworkMessage)
{
	if (!InventoryStacksHaveRoom(player, item))
		return false;
	Item rest = item;
	for (int i = 0; i < player._pNumInv && !rest.isEmpty(); i++) {
		if (!CanStackWith(player.InvList[i], rest) || StackRoom(player.InvList[i]) <= 0)
			continue;
		MergeStack(player.InvList[i], rest);
		if (sendNetworkMessage)
			NetSyncInvItem(player, i);
	}
	return true;
}

} // namespace devilution
