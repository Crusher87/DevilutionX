/**
 * @file poe2/profane_roads.hpp
 *
 * POE2 Mashup: the rules of the Profane Roads that act in the dungeon (design-v8, research/tree-rework/profane-road).
 * Leoric's Road: the dead do not rest (a kill may rise again as a hostile skeleton, which a hit puts down in its first
 * second) and the King's dead (with the pact, culled monsters rise for the hero for a while). The Sickness Road: every
 * death spreads it (the pact). The Maiden's Road: eyes in the dark and the light caps, and out of mind with a staff.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace devilution {

struct Monster;
struct Player;

/** @brief Leoric's Road: the put-down window, in game ticks; a hit from the hero destroys a rising monster for good. */
constexpr uint8_t RisingTicks = 20;
/** @brief Leoric's Road pact: the King's dead fight for the hero this long, in game ticks. */
constexpr uint16_t KingsDeadTicks = 12 * 20;
/** @brief The King's dead and Revive's minions together never pass the larger of this and Revive's own limit. */
constexpr int KingsDeadMinCap = 3;
/** @brief The Maiden's Road toll: monsters this close with a clear line wake as if they saw the hero. */
constexpr int MaidenWakeTiles = 10;
/** @brief The Maiden's Road pact, with a staff: monsters farther than this can't shoot or cast at the hero. */
constexpr int OutOfMindTiles = 5;
/** @brief The Maiden's Road light caps: the toll's, and the pact's while a staff is held. */
constexpr int MaidenLightRadiusCap = 7;
constexpr int StaffLightRadiusCap = 4;
/** @brief The Maiden's Road toll: First Strike's extra against a monster that hasn't noticed the hero, in percent. */
constexpr int UnawareFirstStrikePercent = 40;

/** @brief A monster that rose hostile after a kill on Leoric's Road. It gives no reward when it dies. */
bool IsRisen(const Monster &monster);
/** @brief One of the King's dead: a monster risen for its killer by Leoric's Road pact, for a while. */
bool IsKingsDead(const Monster &monster);

/**
 * @brief A monster credited to the player died: Leoric's Road may queue a rise (called from MonsterDeath before the
 * other on-kill effects). A culled monster rises for the player with the pact; any other may rise hostile.
 */
void OnProfaneRoadKill(const Player &player, const Monster &monster);
/**
 * @brief A hit from the player on a monster still rising puts it down for good (with the pact, as a Cull).
 * @return true if it did: the monster has no life left and the hit deals nothing else.
 */
bool TryPutDownRisen(const Player &player, Monster &monster);
/**
 * @brief Any monster died (called from MonsterDeath after the on-kill effects, never for the risen): the Sickness Road
 * pact passes the ailments it carried to the enemies beside it, unless Kill Burst's contagion already did.
 */
void OnProfaneRoadDeath(const Monster &monster);
/** @brief Per game tick: rises whose corpse is down stand up, and the King's dead count down and leave. */
void ProcessProfaneRoads();
/** @brief Forgets the rises still waiting (a new level). */
void ClearPendingRises();

/** @brief Whether the player sees monsters in the dark as shapes: the Infravision spell, or the Maiden's Road toll. */
bool HasInfravision(const Player &player);
/** @brief The Maiden's Road caps on the player's light radius (15 when none applies), before the 2 to 15 clamp. */
int ProfaneLightRadiusCap(const Player &player);
/** @brief The Maiden's Road toll: First Strike's extra against a monster that hasn't noticed the player, in percent. */
int UnawareFirstStrikeBonus(const Player &player, const Monster &monster);
/** @brief The Maiden's Road toll: a monster within 10 tiles with a clear line to the hero sees her, lit or not. */
bool MaidenSeesYou(const Monster &monster);
/**
 * @brief The Maiden's Road pact, with a staff: a monster that hasn't attacked the hero yet can't shoot or cast at her
 * from more than 5 tiles away or without a clear line.
 */
bool CanShootAtPlayer(const Monster &monster);
/**
 * @brief A monster attacks the hero: whether it is its first blow against her with the Maiden's Road pact and a staff
 * (that blow can't be evaded and ignores armour). The monster knows where she is from now on.
 */
bool TakeFirstBlow(Monster &monster, const Player &player);

size_t CountKingsDead(const Player &player);
/**
 * @brief Revive is about to raise a minion: the oldest of the King's dead leaves if the shared cap is full.
 * @param revived Revive's minions now, @param limit Revive's own limit.
 */
void MakeRoomForRevive(const Player &player, size_t revived, int limit);

} // namespace devilution
