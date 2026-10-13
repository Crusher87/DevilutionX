/**
 * @file poe2/regen.hpp
 *
 * POE2 Mashup regeneration: mana regenerates on its own, life only from gear.
 */
#pragma once

namespace devilution {

struct Player;

/** @brief Base mana regeneration in percent of maximum mana per second, like in POE2. */
constexpr int BaseManaRegenPercent = 4;

/** @brief Extra mana every class gets in POE2 mode, so attack skills can be used at a POE2 pace. */
constexpr int Poe2FlatMana = 20;
constexpr int Poe2ManaPerLevel = 2;

constexpr int Poe2BonusMana(int characterLevel)
{
	return Poe2FlatMana + (Poe2ManaPerLevel * characterLevel);
}

/** @brief Applies one game tick of life and mana regeneration (POE2 mode only). */
void ApplyRegen(Player &player);

} // namespace devilution
