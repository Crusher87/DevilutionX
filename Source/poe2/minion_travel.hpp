/**
 * @file poe2/minion_travel.hpp
 *
 * POE2 Mashup: the hero's minions go with the hero between levels, by stairs, waypoints and portals (Curtis 2026-10-29:
 * "they should follow me like POE2"). Skeletons and wolves of the skeleton gems and a sceptre's granted skill, golems and
 * Revive's monsters come off the level alive as the hero leaves and stand beside the hero on arrival, with the life they
 * had. The game keeps no monsters in town (they aren't drawn, moved or let past there), so while the hero is in town
 * they wait with the hero, in the hero's save too, and come back on the next dungeon level. Raging spirits and illusions
 * last a few seconds, so they don't come. Single player for now: the
 * spawns go out over the network, but Revive's don't, and the removal from the old level isn't sent.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace devilution {

struct Player;
struct Poe2PlayerData;

/** @brief The hero leaves the level: their minions come off it alive and go with them. */
void TakeMinionsAlong(Player &player);

/**
 * @brief The hero has arrived on a level: the minions that came along stand on free tiles beside them, keeping their
 * life, as long as the hero still keeps them (the gem is still on). In town they wait for the next dungeon level.
 */
void BringMinionsAlong(Player &player);

/** @brief Bytes one travelling minion takes in the hero's save. */
constexpr size_t TravellingMinionSaveSize = 16;

/** @brief The travelling minions as saved with the hero: a count, then each minion. */
std::vector<uint8_t> PackTravellingMinions(const Poe2PlayerData &data);

/**
 * @brief Reads the travelling minions PackTravellingMinions wrote; ones of an unknown kind are skipped.
 * @return Whether the bytes held a whole list.
 */
bool UnpackTravellingMinions(Poe2PlayerData &data, std::span<const uint8_t> bytes);

} // namespace devilution
