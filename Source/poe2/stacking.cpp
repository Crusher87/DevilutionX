#include "poe2/stacking.hpp"

#include <algorithm>

#include "game_mode.hpp"
#include "msg.h"
#include "player.h"
#include "poe2/crafting_items.hpp"
#include "poe2/currency.hpp"

namespace devilution {

bool IsStackable(const Item &item)
{
	return gbIsPoe2 && !item.isEmpty() && item._iClass == ICLASS_MISC && IsCurrencyMiscId(item._iMiscId);
}

int StackCount(const Item &item)
{
	if (!IsStackable(item))
		return 1;
	return std::clamp(item._iCharges, 1, CurrencyStackMax);
}

void SetStackCount(Item &item, int count)
{
	item._iMaxCharges = CurrencyStackMax;
	item._iCharges = std::clamp(count, 1, CurrencyStackMax);
}

bool CanStackWith(const Item &stack, const Item &item)
{
	if (&stack == &item || !IsStackable(stack) || !IsStackable(item))
		return false;
	return stack.IDidx == item.IDidx && stack._iMiscId == item._iMiscId
	    && (stack.dwBuff & CF_POE2_VARIANT) == (item.dwBuff & CF_POE2_VARIANT);
}

int StackRoom(const Item &stack)
{
	return IsStackable(stack) ? CurrencyStackMax - StackCount(stack) : 0;
}

bool MergeStack(Item &stack, Item &item)
{
	if (!CanStackWith(stack, item))
		return false;
	const int moved = std::min(StackRoom(stack), StackCount(item));
	if (moved <= 0)
		return false;
	SetStackCount(stack, StackCount(stack) + moved);
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
	const int count = StackCount(stack);
	if (count <= 1)
		return std::nullopt;
	const int taken = count / 2;
	Item half = stack;
	SetStackCount(half, taken);
	SetStackCount(stack, count - taken);
	return half;
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
