#include "poe2/loot_filter.hpp"

#include <algorithm>

#include "engine/palette.h"
#include "game_mode.hpp"
#include "items.h"
#include "poe2/currency.hpp"
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

std::optional<uint8_t> GroundItemOutlineColor(const Item &item)
{
	if (!gbIsPoe2 || item.isEmpty())
		return std::nullopt;
	// Darker shades than the hover outline, so the floor reads busy only where loot is.
	if (item._iClass == ICLASS_QUEST)
		return PAL16_ORANGE + 2;
	if (item._itype == ItemType::Gold)
		return PAL16_YELLOW + 8;
	switch (item._iMagical) {
	case ITEM_QUALITY_MAGIC:
		return PAL16_BLUE + 6;
	case ITEM_QUALITY_RARE:
		return PAL16_YELLOW + 4;
	case ITEM_QUALITY_UNIQUE:
		return PAL16_ORANGE + 4;
	default:
		break;
	}
	if (item._iClass == ICLASS_MISC && IsCurrencyMiscId(item._iMiscId))
		return PAL16_BEIGE + 4;
	return PAL16_GRAY + 9;
}

std::optional<uint8_t> LitGroundOutline(uint8_t color, int lightTableIndex)
{
	if (lightTableIndex >= 12)
		return std::nullopt;
	const int shade = std::min((color & 15) + std::max(lightTableIndex, 0) / 2, 15);
	return static_cast<uint8_t>((color & ~15) | shade);
}

} // namespace devilution
