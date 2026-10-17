/**
 * @file poe2/craft_types.hpp
 *
 * POE2 Mashup crafting state of an item, apart from items.h so the player's data can hold one too (an open map's waystone).
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace devilution {

/** @brief Crafted modifiers an item holds: up to three prefixes and three suffixes. */
constexpr size_t MaxCraftMods = 6;

/** POE2 Mashup: a crafted modifier, rebuilt from its family, tier and a salt for its values. */
struct Poe2CraftMod {
	/** Index of the modifier family plus 1; 0 for none. */
	uint8_t family = 0;
	uint8_t tier = 0;
	/** 0 keeps the values the item's seed rolled for this modifier; others roll them anew. */
	uint8_t salt = 0;

	bool operator==(const Poe2CraftMod &other) const = default;
};

/** POE2 Mashup: crafting state an item's seed can't rebuild. Saved in the spare bytes of the item save (poe2/craft_state.hpp). */
struct Poe2Craft {
	/** Set once crafting chose the item's modifiers; they then replace the ones its seed rolls. */
	bool explicitMods = false;
	/** With explicit modifiers: the item's rarity (normal, magic or rare). */
	uint8_t rarity = 0;
	/** Whether the seed rolled the item magic rather than rare, so a modifier with salt 0 finds its rolled values. */
	bool rolledMagic = false;
	std::array<Poe2CraftMod, MaxCraftMods> mods {};
	/** Augment sockets (0 to 3) and what is in each (an Augment id; 0 for empty). */
	uint8_t sockets = 0;
	std::array<uint8_t, 3> augments {};
	/** Jewellery: the catalyst its quality boosts (a Catalyst id; 0 for none). A waystone: its tier less one. */
	uint8_t catalyst = 0;
	/** A corruption enchantment (a CorruptionEnchant id) and its roll, 0 to 7. */
	uint8_t enchant = 0;
	uint8_t enchantRoll = 0;
	/** Corruption's change to the item's explicit modifiers, in percent. */
	int8_t magnitude = 0;
	/** The item was desecrated, which only happens once. */
	bool desecrated = false;

	[[nodiscard]] bool isEmpty() const { return *this == Poe2Craft {}; }
	bool operator==(const Poe2Craft &other) const = default;
};

} // namespace devilution
