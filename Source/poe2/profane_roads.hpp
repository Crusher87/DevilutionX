/**
 * @file poe2/profane_roads.hpp
 *
 * POE2 Mashup: the rules of the Profane Roads that act in the dungeon (design-v8, research/tree-rework/profane-road).
 * The Butcher's Road (Leoric's Road until 2026-10-30, research/tree-rework/profane-road/leoric-rework/spec-v1.md): a Cull
 * throws a chained hook that drags the next enemy in, and lets out a cry that wakes everything near; with the pact,
 * arrow Culls hook along the arrow's line and the cry reaches further and enrages. The Maiden's Road: eyes in the dark
 * and the light caps, and out of mind with a staff. The Vizjerei Road's blasts live with Kill Burst
 * (poe2/unique_effects.cpp).
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/displacement.hpp"
#include "engine/point.hpp"

namespace devilution {

struct Monster;
struct Player;

/** @brief The Butcher's Road toll: the hook reaches enemies this far from the culled monster. */
constexpr int HookTiles = 6;
/** @brief A normal or magic monster is dragged to the hero over this many game ticks (about 0.3 seconds). */
constexpr uint8_t HookDragTicks = 6;
/** @brief A rare is pulled one tile and slowed this much for this long. */
constexpr int HookRareSlowPercent = 30;
constexpr uint8_t HookRareSlowTicks = 30;
/** @brief A unique flinches: it can't attack for this many game ticks. */
constexpr uint8_t HookFlinchTicks = 10;
/** @brief The Butcher's cry wakes every enemy this close, through walls; with the pact the call reaches further. */
constexpr int ButcherCryTiles = 8;
constexpr int ButcherCryPactTiles = 16;
/** @brief The cry sounds at most once in this many game ticks (the enemies still answer every Cull). */
constexpr uint8_t ButcherCryCooldownTicks = 40;
/** @brief The pact: a monster that answers the cry or is hooked is enraged this long, moving and attacking faster. */
constexpr uint8_t EnrageTicks = 60;
constexpr int EnrageHastePercent = 20;
/** @brief The Maiden's Road toll: monsters this close with a clear line wake as if they saw the hero. */
constexpr int MaidenWakeTiles = 10;
/** @brief The Maiden's Road pact, with a staff: monsters farther than this can't shoot or cast at the hero. */
constexpr int OutOfMindTiles = 5;
/** @brief The Maiden's Road light caps: the toll's, and the pact's while a staff is held. */
constexpr int MaidenLightRadiusCap = 7;
constexpr int StaffLightRadiusCap = 4;
/** @brief The Maiden's Road toll: First Strike's extra against a monster that hasn't noticed the hero, in percent. */
constexpr int UnawareFirstStrikePercent = 40;

/**
 * @brief A monster credited to the player died (called from MonsterDeath before the other on-kill effects). On the
 * Butcher's Road a Culled one throws the hook and lets out the cry.
 */
void OnProfaneRoadKill(const Player &player, const Monster &monster);
/** @brief An arrow of the player's hits: where it flew from, for the pact's hook along its line. */
void NoteArrowLine(const Player &player, Point from);
/** @brief Per game tick: the hook drags its monsters in, and the hook's slows, flinches and the pact's rage run out. */
void ProcessProfaneRoads();

/** @brief Being dragged by the hook: the monster can't act. */
bool IsHooked(const Monster &monster);
/** @brief A rare the hook pulled is slowed by this much, in percent (0 for none). */
int HookSlowPercent(const Monster &monster);
/** @brief A unique the hook made flinch can't start an attack yet. */
bool IsFlinching(const Monster &monster);
/** @brief Enraged by the pact's call: it moves and attacks faster, and is drawn red. */
bool IsEnraged(const Monster &monster);
/** @brief Whether an enraged monster takes a second turn this tick (20% more turns). */
bool EnragedExtraTick(Monster &monster);
/** @brief Where a monster being dragged is drawn between game ticks, from its tile, in screen pixels. */
Displacement HookDrawOffset(const Monster &monster);

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

} // namespace devilution
