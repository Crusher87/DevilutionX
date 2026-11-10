/**
 * @file poe2/floor_chests.hpp
 *
 * POE2 Mashup: chests in three kinds and strongboxes on the dungeon floors (step 3 of the dungeon plan, Curtis's
 * pace-and-rewards picks, 10-27).
 *  - A magic chest always holds a magic item and a rare chest a rare one, like POE2's; the rest of what they hold
 *    is the chest's usual rolls. Their names show in blue and yellow, like items.
 *  - A strongbox always holds a rare item and at least one more roll. It stands out: drawn bright even in the dark,
 *    with a gold edge and a gold name (and a glow and rising sparks with Modern Effects). Opening it brings out its
 *    guards: 4 to 6 of the floor's own kinds led by a magic one, taken out of the floor's monsters when it was made.
 *    The one exception to "no monsters from nowhere", so they come with a warning (Curtis, 10-28: "0-100 with no
 *    warning"): a summoning sound, a shake, and red runes where each will stand, then one at a time. Its loot drops
 *    only when every guard is dead (Curtis, 10-29).
 *  - Normal chests keep Diablo's rule and can be empty; the other kinds never are.
 * The kind is kept in the chest's _oVar5 (saved with the level), a strongbox's guards in _oVar3, _oVar4 and _oVar6.
 */
#pragma once

#include <cstdint>
#include <string>

namespace devilution {

struct Object;
struct Player;

enum class FloorChestKind : uint8_t {
	Normal,
	Magic,
	Rare,
	Strongbox,
};

FloorChestKind GetFloorChestKind(const Object &chest);

/** @brief Makes a chest one of the kinds; guards only count for a strongbox. */
void SetFloorChestKind(Object &chest, FloorChestKind kind, int guards = 0);

/**
 * @brief Opens a magic or rare chest or a strongbox: its sure item, its other rolls and a strongbox's guards.
 * Call with the chest's seed set, as OperateChest does.
 * @return false for any other chest, which opens the usual way.
 */
bool OpenFloorChest(const Player &player, Object &chest, bool sendmsg);

/** @brief A strongbox not opened yet, or holding its loot until its guards are dead: drawn so it stands out. */
bool StrongboxStandsOut(const Object &object);

/** @brief Each game tick: an open strongbox's guards step out one at a time, and its loot drops once they are dead. */
void UpdateFloorChest(Object &chest);

/** @brief The console's strongbox(): puts a strongbox with 5 guards next to the hero, to try one. */
std::string PlaceTestStrongbox();

/** @brief A chest's name on hover ("Strongbox" or the chest's own) and its colour, for GetObjectStr. */
void DescribeFloorChest(const Object &chest);

} // namespace devilution
