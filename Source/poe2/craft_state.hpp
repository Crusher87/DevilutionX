/**
 * @file poe2/craft_state.hpp
 *
 * POE2 Mashup crafting state that an item's seed can't rebuild: the modifiers crafting chose one by one
 * (Annul, Chaos, essences, omens), augment sockets, corruption enchantments and desecration.
 * It is saved in spare bytes of the item save, so saves keep their format.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "items.h"

namespace devilution {

struct Player;

/** @brief Bytes the crafting state takes in the spare (alignment) bytes of an item save. */
constexpr size_t CraftStateSaveSize = 19;
/** @brief Family id of the placeholder an unrevealed desecrated modifier holds; its tier is 1 for a prefix, 2 for a suffix. */
constexpr uint8_t UnrevealedModFamily = 127;
/** @brief Highest value salt a crafted modifier can have. */
constexpr uint8_t MaxCraftSalt = 31;

std::array<uint8_t, CraftStateSaveSize> PackCraftState(const Poe2Craft &craft);
/** @brief Reads crafting state from an item save; zeros from an older save, or bytes that don't check out, give none. */
Poe2Craft UnpackCraftState(const std::array<uint8_t, CraftStateSaveSize> &bytes);

/** @brief Whether crafting can work on the item's modifiers one by one: it rolls them from the POE2 tables. */
[[nodiscard]] bool CanCraftMods(const Item &item);
/** @brief The item's crafting state with its current modifiers written out, ready to edit. */
Poe2Craft CurrentCraft(const Item &item);
/** @brief The crafted modifiers, without values. */
std::vector<Poe2ItemMod> CraftModList(const Poe2Craft &craft);
/** @brief Number of crafted modifiers, the unrevealed one included. */
int CraftModCount(const Poe2Craft &craft);
int CraftSideCount(const Poe2Craft &craft, bool prefix);
/** @brief Adds a modifier with a fresh value salt, keeping prefixes ahead of suffixes. @return Whether there was room. */
bool AddCraftMod(Poe2Craft &craft, const Poe2ItemMod &mod, uint32_t saltSeed);
/** @brief Removes the modifier at the index, closing the gap. */
void RemoveCraftMod(Poe2Craft &craft, size_t index);
/** @brief Whether the crafted modifier is a prefix; the unrevealed placeholder knows its side. */
[[nodiscard]] bool IsCraftPrefix(const Poe2CraftMod &mod);
/** @brief The values a crafted modifier with a salt rolls. */
std::array<int16_t, 2> CraftModValues(const Item &item, const Poe2CraftMod &mod);
/**
 * @brief The crafted modifiers with their values; ones only desecration gives aren't among them.
 * @param natural The modifiers the item's seed rolls, whose values a modifier with salt 0 keeps.
 */
std::vector<Poe2ItemMod> ResolveCraftMods(const Item &item, const std::vector<Poe2ItemMod> &natural);
/** @brief Rebuilds the item from its seed with new crafting bits and state. */
Item RebuildWithCraft(const Player &player, const Item &item, uint32_t dwBuff, const Poe2Craft &craft);

} // namespace devilution
