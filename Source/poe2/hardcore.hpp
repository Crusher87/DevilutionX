#pragma once

namespace devilution {

struct Player;

/**
 * @file poe2/hardcore.hpp
 *
 * POE2 Mashup Hardcore league: a hardcore hero has one life and their own stash. When they die they move to
 * Softcore with what they wear and carry; the hardcore stash stays as it is for the other hardcore heroes.
 */

/** @brief Whether the hero is in the Hardcore league (single player only). */
bool IsHardcoreHero(const Player &player);

/** @brief Whether the hero has just fallen in Hardcore and is waiting to continue in Softcore. */
bool HardcoreHeroFell();

/** @brief Called when the local hardcore hero dies: keeps the hardcore stash and moves the hero to Softcore. */
void FallFromHardcore(Player &player);

/** @brief Brings the fallen hero back in town (Esc on the death screen). */
void ContinueInSoftcore(Player &player);

/** @brief Once the town has loaded: full life, the Softcore stash, and a save so the move to Softcore sticks. */
void FinishHardcoreFall();

} // namespace devilution
