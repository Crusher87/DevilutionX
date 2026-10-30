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
#include <optional>
#include <vector>

#include "items.h"

namespace devilution {

struct Player;

/** @brief Bytes the crafting state takes in the spare (alignment) bytes of an item save. */
constexpr size_t CraftStateSaveSize = 19;
/**
 * @brief Family id of the placeholder an unrevealed desecrated modifier holds; its tier is 1 for a prefix, 2 for a suffix.
 * Out of the range of modifier families in memory; the narrow save layout stores it as NarrowUnrevealedModFamily.
 */
constexpr uint8_t UnrevealedModFamily = 255;
/** @brief Highest value salt a crafted modifier can have. */
constexpr uint8_t MaxCraftSalt = 31;
/** @brief Highest value salt a modifier of a family appended by the caster weapons rework can have: the wide layout keeps 4 bits. */
constexpr uint8_t MaxWideCraftSalt = 15;

/**
 * @brief The craft save's two layouts. The narrow one, every item saved before the caster weapons rework, keeps 7 bits
 * of family (desecrated families from NarrowDesecratedFamilyBase, the unrevealed one as NarrowUnrevealedModFamily),
 * 4 of tier and 5 of salt. An item with a family above NarrowMaxModFamily saves wide (a flag in the header byte): 8 bits
 * of family, 4 of tier and 4 of salt, ids as in memory.
 */
constexpr uint8_t NarrowMaxModFamily = 112;
constexpr uint8_t NarrowDesecratedFamilyBase = 113;
constexpr uint8_t NarrowUnrevealedModFamily = 127;

/** @brief The highest salt a modifier of the family may have: MaxWideCraftSalt for the families the rework appended, else MaxCraftSalt. */
[[nodiscard]] uint8_t CraftSaltLimit(uint8_t family);
/** @brief A salt from 1 to the family's CraftSaltLimit, from a roll. */
[[nodiscard]] uint8_t RollCraftSalt(uint8_t family, uint32_t roll);

/**
 * @brief The craft save's two socket layouts. The narrow one, every item saved before the runes rework, keeps three
 * augment ids of 4 bits and the catalyst's 4. An item with an augment id above NarrowMaxAugmentId saves wide (its own
 * marker byte): one byte for each of the two sockets, and no third socket or catalyst, which only jewellery and
 * waystones have, and they have no sockets. Ids keep below 128 there: the top bit is kept for a Zod Rune.
 */
constexpr uint8_t NarrowMaxAugmentId = 15;

std::array<uint8_t, CraftStateSaveSize> PackCraftState(const Poe2Craft &craft);
/** @brief Reads crafting state from an item save; zeros from an older save, or bytes that don't check out, give none. */
Poe2Craft UnpackCraftState(const std::array<uint8_t, CraftStateSaveSize> &bytes);

/** @brief Bytes the crafting extension takes in an item save: a marker, Poe2Craft::extension and a checksum. */
constexpr size_t CraftExtensionSaveSize = 8;
/**
 * @brief The item save's 8 bytes of anim widths, which loading skips and saving fills with Diablo's constants, hold
 * the crafting extension when it isn't all zero.
 * @return The bytes, or nothing when the extension is empty and the constants go there as before.
 */
std::optional<std::array<uint8_t, CraftExtensionSaveSize>> PackCraftExtension(const Poe2Craft &craft);
/** @brief Reads the crafting extension into the craft; Diablo's constants, or bytes that don't check out, give none. */
void UnpackCraftExtension(const std::array<uint8_t, CraftExtensionSaveSize> &bytes, Poe2Craft &craft);

/** @brief Whether crafting can work on the item's modifiers one by one: it rolls them from the POE2 tables. */
[[nodiscard]] bool CanCraftMods(const Item &item);
/** @brief The item's crafting state with its current modifiers written out, ready to edit. */
Poe2Craft CurrentCraft(const Item &item);
/**
 * @brief Whether the item is a caster staff, wand or sceptre of the rework whose modifiers still roll from its seed and
 * today's pools: the ones the lock-in writes out (caster-weapons-plan-v2.md 5.4).
 */
[[nodiscard]] bool LocksCasterWeaponMods(const Item &item);
/**
 * @brief The crafting state an item saves with. A caster weapon of the rework saves its rolled modifiers written out
 * (CurrentCraft, salt 0, so they keep their values), so switching on a family of its pool later never re-rolls it;
 * every other item saves what it has.
 */
[[nodiscard]] Poe2Craft SavedCraftState(const Item &item);
/** @brief Writes a caster weapon's rolled modifiers into its crafting state, as SavedCraftState does; on load. */
void LockCasterWeaponMods(Item &item);
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
/**
 * @brief Rebuilds the item from its seed with new crafting bits and state. An item saved before the bases took POE2's
 * numbers (IsLegacyScaleItem) keeps its defence and damage, and its modifiers keep their old scales.
 */
Item RebuildWithCraft(const Player &player, const Item &item, uint32_t dwBuff, const Poe2Craft &craft);

/**
 * @brief Whether a POE2 item was saved before its base and flat modifiers took POE2's numbers (harder monsters plan,
 * step 10b): its saved defence, damage or added damage is below what its seed rolls now. Nothing lowers those once an
 * item has dropped (Gloomy shrines only lower the top of the damage), and every base now rolls exactly its defence and
 * damage, so a newer item never has less.
 * @param saved The item as saved.
 * @param rebuilt The item rebuilt from its seed with today's tables.
 */
[[nodiscard]] bool IsLegacyScaleItem(const Item &saved, const Item &rebuilt);

} // namespace devilution
