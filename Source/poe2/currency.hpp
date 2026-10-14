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
	/** Normal item to a unique of its base, or destroys it. */
	Chance,
	/** Removes a random modifier from a magic or rare item. */
	Annul,
	/** Blacksmith's Whetstone: quality on martial weapons. */
	Whetstone,
	/** Arcanist's Etcher: quality on caster weapons. */
	Etcher,
	/** Armourer's Scrap: quality on armour. */
	Scrap,
	GreaterExalted,
	PerfectExalted,
	GreaterChaos,
	PerfectChaos,
	/** Artificer's Orb: adds an augment socket. */
	Artificer,
	/** Ancient Bone: desecrates a rare item, adding an unrevealed modifier. */
	Desecrate,
	/** Catalysts: quality on jewellery that boosts one kind of modifier. */
	Catalyst,
	Essence,
	Augment,
};

/** @brief Most affixes an Exalted Orb can bring a rare item to. */
constexpr int MaxExaltedAffixes = 6;
/** @brief Most quality currency can add to an item. */
constexpr int MaxCraftedQuality = 20;
/** @brief The lowest modifier level a Greater or Perfect Exalted or Chaos Orb adds. */
constexpr int GreaterOrbModLevel = 35;
constexpr int PerfectOrbModLevel = 50;

/** @brief The implicit a Vaal Orb adds: 1 all resistances, 2 life, 3 all attributes. */
ItemPower CorruptionImplicitPower(int kind);
/** @brief Number of affixes on a rare item. */
int RareAffixCount(const Item &item);

CurrencyOrb CurrencyOrbForMiscId(int miscId);
/** @brief Whether the item kind is POE2 crafting currency: orbs, essences, augments and catalysts. */
[[nodiscard]] bool IsCurrencyMiscId(int miscId);
std::string_view CurrencyOrbDescription(CurrencyOrb orb);

/** @brief Set when an orb destroyed the item it was used on (a failed Orb of Chance); whoever used it removes the item. */
extern bool CurrencyDestroyedItem;
/** @brief Set when the item an Ancient Bone was used on has an unrevealed modifier to choose; whoever used it opens the choice. */
extern bool DesecrationRevealItem;

/**
 * @brief Rolls an item of the given base and quality, the way a monster of the given level would drop it.
 *
 * Tries seeds derived from firstSeed until one gives the wanted quality, then restores the game's
 * random number generator. Items made this way rebuild identically from their seed.
 * @param avoidSeed A seed the result must not use, e.g. the item's current one.
 */
bool RollItemOfQuality(const Player &player, Item &item, _item_indexes baseIndex, item_quality quality, int level, uint32_t firstSeed, uint32_t avoidSeed = 0, const std::function<bool(const Item &)> &accept = {}, uint32_t keepBuff = 0);

/**
 * @brief Uses an orb on an item. Omens in the player's inventory change it and are used up.
 * @param variant Which essence, augment or catalyst it is (poe2/crafting_items.hpp).
 * @return false (with a message to the player) if the orb can't be used on that item.
 */
bool ApplyCurrencyOrb(CurrencyOrb orb, Player &player, Item &item, uint8_t variant = 0);
/** @brief Removes the omens the last crafting action used from the inventory; call once done with the crafted item. */
void ConsumeCraftOmens(Player &player);

} // namespace devilution
