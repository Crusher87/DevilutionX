/**
 * @file poe2/hud_minions.hpp
 *
 * POE2 Mashup: the row of minion portraits in the top left corner, with how many are up and when the next one rises.
 */
#pragma once

#include <string>
#include <vector>

#include "engine/surface.hpp"
#include "panels/spell_icons.hpp"
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

/** @brief An icon in the row that runs out: a ring drains round it and it blinks for its last seconds. */
struct TimedHudIcon {
	/** @brief A shrine's icon; unused when skill is set. */
	SpellIcon icon;
	/** @brief The tooltip, one line. */
	std::string tooltip;
	int ticksLeft;
	int ticksTotal;
	/** @brief A curse on the hero rather than a buff: its ring is red. */
	bool harmful;
	/** @brief A skill gem's buff: the gem, drawn with the skill bar's icon. Empty for a shrine's. */
	SkillSlot skill {};
	/** @brief The hits or souls the buff has left, shown on the icon; 0 shows none. */
	int count = 0;
};

/**
 * @brief The timed icons after the portraits: the shrine buffs, oldest first, a shrine's curse, then the skill gems'
 * timed buffs on the hero (Battle Cry and the other war cries, Frozen Armor, Holy Shield and Soul Harvest).
 */
std::vector<TimedHudIcon> TimedHudIcons(const Player &player);

void DrawMinionHud(const Surface &out);

} // namespace devilution
