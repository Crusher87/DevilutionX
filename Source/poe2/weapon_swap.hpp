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
 * @brief The only points beyond levels come from Books of Specialisation, like POE2's campaign rewards: in each act
 * one Book is taken from a lord and one is handed over when the act's quest ends (tree start redesign, plan 4.5).
 * Each is worth 2 weapon set points for both sets, plus 2 normal passive points. The books keep their item rows:
 * 0 a lord of the Cathedral or Catacombs, 1 the Chamber of Bone, 2 a lord of the Caves, 3 a lord of Hell,
 * 4 Lazarus, 5 the Defiler.
 */
constexpr size_t NumSpecBooks = 6;
constexpr int WeaponSetPointsPerBook = 2;
constexpr int PassivePointsPerBook = 2;
/** @brief Slots on the tree panel's book shelf: two Books for each of six acts (Acts 4 to 6 aren't in the game yet). */
constexpr size_t BookShelfSlots = 12;
/** @brief Set in questsRewarded once a hero's points come from Books only; older heroes are moved over on load. */
constexpr uint32_t BooksOnlyFlag = 1U << 31;

/** @brief Weapon set points from the books read so far; each set gets this many. */
int WeaponSetPointsTotal(const Player &player);
/** @brief Which book (0 to NumSpecBooks - 1) the item is; -1 if it isn't a Book of Specialisation. */
int SpecBookIndex(const Item &item);
/** @brief The place a book is named for: "the Caves" in "Book of the Caves". */
std::string_view SpecBookPlace(int book);
/** @brief Where the book came from, for its description. */
std::string_view SpecBookSource(int book);
/** @brief Where to find a book not read yet, for its gap on the shelf. */
std::string_view SpecBookWhere(int book);
/** @brief The book on a shelf slot (two per act, the lord's Book first); -1 for a slot of an act not in the game yet. */
int ShelfSlotBook(size_t slot);
/** @brief Reads a book for its points. @return true if the book was used up. */
bool UseSpecBook(Player &player, const Item &item);
/** @brief Drops a book when a boss that guards one dies and the player hasn't read it yet. */
void DropSpecBook(const Monster &monster, bool sendmsg);
/** @brief Drops a quest's Book at the hero's feet the first time each Book quest (Chamber of Bone, Lazarus, Defiler) ends. */
void HandOutQuestBooks(Player &player);
/**
 * @brief Moves a hero from quest points to Books: the Cathedral's and the Catacombs' Books become one, and each Book
 * quest already finished counts as its Book read. @return the change in passive points (zero or less).
 */
int MigrateToBooks(Player &player);

} // namespace devilution
