/**
 * @file poe2/dodge_roll.hpp
 *
 * POE2 Mashup dodge roll: a quick dash a few tiles away; hits and missiles pass through for its first moments.
 */
#pragma once

#include <cstdint>
#include <optional>

#include "engine/point.hpp"

namespace devilution {

struct Player;

/** @brief Number of tiles a dodge roll covers when nothing is in the way. */
constexpr int DodgeRollTiles = 3;

/**
 * @brief Starts a dodge roll towards the held WASD direction, or towards the cursor. POE2 Mashup: it also rolls out of an
 * attack or cast (see BreakOffActionForDodge).
 * @return true if the roll started.
 */
bool TryDodgeRoll(Player &player);

/** @brief POE2 Mashup: whether the attack or cast under way has reached its action frame: its hit, shot or spell went off. */
bool ActionHasGoneOff(const Player &player);

/**
 * @brief POE2 Mashup: a dodge roll breaks off the attack or cast under way (Curtis 2026-10-29: "when I'm mid cast/attack
 * I should be able to dodge roll out of the animation"). Before the action goes off it is cancelled and gives back what
 * it paid ("If you dodge in the middle of a spell, it's cancelled and costs nothing"); after, the roll only cuts its
 * follow-through short and what it set going carries on. Nothing queued goes off after the roll. The hero stands.
 */
void BreakOffActionForDodge(Player &player);

/**
 * @brief POE2 Mashup: notes what the hero spends while it lives as the price of the attack or cast that is starting, so
 * BreakOffActionForDodge can give it back (Curtis 2026-10-29).
 */
class ActionPaymentScope {
public:
	explicit ActionPaymentScope(Player &player);
	~ActionPaymentScope();
	ActionPaymentScope(const ActionPaymentScope &) = delete;
	ActionPaymentScope &operator=(const ActionPaymentScope &) = delete;

private:
	Player &player_;
	int32_t mana_;
	int32_t life_;
	bool freeSkill_;
};

/**
 * @brief Dashes in a straight line towards a tile, untouchable like a dodge roll, e.g. for Leap Slam.
 * @return Where the dash ends, or nothing if the way is blocked.
 */
std::optional<Point> DashTowards(Player &player, Point target, int maxTiles);

/** @brief Animation frames skipped per tile while rolling, so the roll moves quickly. */
constexpr int DodgeRollSkippedWalkFrames = 4;

} // namespace devilution
