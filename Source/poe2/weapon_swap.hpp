#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace devilution {

struct Item;
struct Monster;
struct Player;
enum class WeaponSkill : uint8_t;

/**
 * @brief POE2 Mashup weapon swap: two full weapon sets (main hand and off hand), swapped with X.
 *
 * The active set is in the hands (InvBody); the other one waits in Player::swapHands. Skills whose weapon is
 * only in the other set swap to it when used, and weapon-set passive points only count while their set is out.
 */
constexpr size_t NumWeaponSets = 2;

/** @brief The other set's hand for an InvBody hand slot (INVLOC_HAND_LEFT or INVLOC_HAND_RIGHT). */
Item &InactiveHand(Player &player, size_t handSlot);
const Item &InactiveHand(const Player &player, size_t handSlot);

bool CanSwapWeapons(const Player &player);

/** @brief Swaps to the other weapon set; false if the hero can't swap right now. */
bool SwapWeapons(Player &player);

/** @brief Whether the weapon set that isn't out allows the skill. */
bool OtherWeaponSetAllowsSkill(const Player &player, WeaponSkill skill);

/** @brief "I" or "II". */
std::string_view WeaponSetName(uint8_t set);

/**
 * @brief Weapon set passive points come from Books of Specialisation that quest bosses drop, like POE2's campaign
 * rewards: one book for each part of the dungeon, each worth WeaponSetPointsPerBook points for both sets.
 */
constexpr size_t NumSpecBooks = 6;
constexpr int WeaponSetPointsPerBook = 4;

/** @brief Weapon set points from the books read so far; each set gets this many. */
int WeaponSetPointsTotal(const Player &player);
/** @brief Which book (0 to NumSpecBooks - 1) the item is; -1 if it isn't a Book of Specialisation. */
int SpecBookIndex(const Item &item);
/** @brief Where the book comes from, for its description. */
std::string_view SpecBookSource(int book);
/** @brief Reads a book for its points. @return true if the book was used up. */
bool UseSpecBook(Player &player, const Item &item);
/** @brief Drops a book when a boss that guards one dies and the player hasn't read it yet. */
void DropSpecBook(const Monster &monster, bool sendmsg);

} // namespace devilution
