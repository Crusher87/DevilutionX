#include "poe2/loot_filter.hpp"

#include "items.h"
#include "utils/is_of.hpp"

namespace devilution {

bool IsItemHiddenByLootFilter(const Item &item, LootFilter filter)
{
	if (filter == LootFilter::Off || item.isEmpty() || item._iClass == ICLASS_QUEST)
		return false;
	if (!IsAnyOf(item._itype, ItemType::Sword, ItemType::Axe, ItemType::Bow, ItemType::Mace, ItemType::Staff, ItemType::Shield,
	        ItemType::LightArmor, ItemType::MediumArmor, ItemType::HeavyArmor, ItemType::Helm, ItemType::Ring, ItemType::Amulet))
		return false;
	switch (item._iMagical) {
	case ITEM_QUALITY_NORMAL:
		return true;
	case ITEM_QUALITY_MAGIC:
		return filter == LootFilter::HideNormalAndMagic;
	default:
		return false;
	}
}

bool IsItemHiddenByLootFilter(const Item &item)
{
	return IsItemHiddenByLootFilter(item, *GetOptions().Gameplay.lootFilter);
}

} // namespace devilution
