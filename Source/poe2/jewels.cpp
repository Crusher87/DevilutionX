#include "poe2/jewels.hpp"

#include "game_mode.hpp"
#include "items.h"
#include "multi.h"
#include "player.h"
#include "poe2/modifiers.hpp"

namespace devilution {

std::optional<size_t> JewelSocketIndex(PassiveNodeId node)
{
	size_t index = 0;
	for (const PassiveNode &candidate : PassiveTree()) {
		if (candidate.kind != PassiveKind::JewelSocket)
			continue;
		if (candidate.id == node)
			return index < MaxJewelSockets ? std::optional<size_t>(index) : std::nullopt;
		index++;
	}
	return std::nullopt;
}

const Item *SocketedJewel(const Player &player, PassiveNodeId node)
{
	const std::optional<size_t> index = JewelSocketIndex(node);
	if (!index || player.jewels[*index].isEmpty())
		return nullptr;
	return &player.jewels[*index];
}

std::vector<const Item *> ActiveJewels(const Player &player)
{
	std::vector<const Item *> jewels;
	if (!gbIsPoe2 || gbIsMultiplayer)
		return jewels;
	for (const PassiveNode &node : PassiveTree()) {
		if (node.kind != PassiveKind::JewelSocket || !IsPassiveAllocated(player, node.id))
			continue;
		if (const Item *jewel = SocketedJewel(player, node.id); jewel != nullptr)
			jewels.push_back(jewel);
	}
	return jewels;
}

bool SocketJewel(Player &player, PassiveNodeId node, Item &held)
{
	const std::optional<size_t> index = JewelSocketIndex(node);
	if (!index || !IsJewel(held) || !IsPassiveAllocated(player, node))
		return false;
	held._iStatFlag = true;
	std::swap(player.jewels[*index], held);
	CalcPlrInv(player, true);
	return true;
}

std::optional<PassiveNodeId> FreeJewelSocket(const Player &player)
{
	for (const PassiveNode &node : PassiveTree()) {
		if (node.kind == PassiveKind::JewelSocket && IsPassiveAllocated(player, node.id) && JewelSocketIndex(node.id) && SocketedJewel(player, node.id) == nullptr)
			return node.id;
	}
	return std::nullopt;
}

std::optional<Item> TakeJewel(Player &player, PassiveNodeId node)
{
	const std::optional<size_t> index = JewelSocketIndex(node);
	if (!index || player.jewels[*index].isEmpty())
		return std::nullopt;
	Item jewel = player.jewels[*index];
	player.jewels[*index].clear();
	CalcPlrInv(player, true);
	return jewel;
}

} // namespace devilution
