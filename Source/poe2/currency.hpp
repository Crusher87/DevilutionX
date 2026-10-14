/**
 * @file poe2/currency.hpp
 *
 * POE2 Mashup currency orbs: consumables that reroll an item's quality and affixes.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

#include "items.h"

namespace devilution {

struct Player;

enum class CurrencyOrb : uint8_t {
	None,
	/** Normal item to magic. */
	Transmutation,
	/** Normal or magic item to rare. */
	Alchemy,
	/** Rare item to a different rare. */
	Chaos,
	/** Magic or rare item back to normal. */
	Scouring,
	/** Adds a second affix to a magic item with one. */
	Augmentation,
	/** Magic item to rare, keeping its affixes. */
	Regal,
	/** Adds an affix to a rare item. */
	Exalted,
	/** Rerolls the values of a rare item's affixes. */
	Divine,
	/** Corrupts an item: nothing, an implicit or a reroll; it can't be crafted afterwards. */
	Vaal,
	/** Glassblower's Bauble: adds 1% quality to a flask. */
	Bauble,
};

/** @brief Most affixes an Exalted Orb can bring a rare item to. */
constexpr int MaxExaltedAffixes = 6;

/** @brief The implicit a Vaal Orb adds: 1 all resistances, 2 life, 3 all attributes. */
ItemPower CorruptionImplicitPower(int kind);
/** @brief Number of affixes on a rare item. */
int RareAffixCount(const Item &item);

CurrencyOrb CurrencyOrbForMiscId(int miscId);
std::string_view CurrencyOrbDescription(CurrencyOrb orb);

/**
 * @brief Rolls an item of the given base and quality, the way a monster of the given level would drop it.
 *
 * Tries seeds derived from firstSeed until one gives the wanted quality, then restores the game's
 * random number generator. Items made this way rebuild identically from their seed.
 * @param avoidSeed A seed the result must not use, e.g. the item's current one.
 */
bool RollItemOfQuality(const Player &player, Item &item, _item_indexes baseIndex, item_quality quality, int level, uint32_t firstSeed, uint32_t avoidSeed = 0, const std::function<bool(const Item &)> &accept = {}, uint32_t keepBuff = 0);

/**
 * @brief Uses an orb on an item.
 * @return false (with a message to the player) if the orb can't be used on that item.
 */
bool ApplyCurrencyOrb(CurrencyOrb orb, const Player &player, Item &item);

} // namespace devilution
