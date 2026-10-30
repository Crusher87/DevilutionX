/**
 * @file poe2/hud_minions.hpp
 *
 * POE2 Mashup: the row of minion portraits in the top left corner, with how many are up and when the next one rises.
 */
#pragma once

#include <string>
#include <vector>

#include "engine/surface.hpp"
#include "poe2/player_data.hpp"

namespace devilution {

struct Player;

/** @brief One minion portrait: whose minions, how many are up of how many, and the soonest to rise again. */
struct MinionPortrait {
	SkillSlot icon;
	std::string name;
	int alive;
	int target;
	int pending;
	/** @brief Game ticks until the soonest rises, of reviveTicks. */
	int ticksLeft;
	int reviveTicks;
};

/**
 * @brief The minion portraits, left to right: the golem, the minion gems' and the sceptre's skeletons and Wolf Pack,
 * and Revive's minions.
 */
std::vector<MinionPortrait> MinionHudPortraits(const Player &player);

void DrawMinionHud(const Surface &out);

} // namespace devilution
