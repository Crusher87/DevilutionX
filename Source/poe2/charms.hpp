/**
 * @file poe2/charms.hpp
 *
 * POE2 Mashup charms: belt items that go off on their own when something happens to you,
 * using charges that refill from kills and in town.
 */
#pragma once

#include <cstdint>
#include <string_view>

#include "utils/fixed_point.hpp"

namespace devilution {

struct Item;
struct Player;

enum class CharmEffect : uint8_t {
	None,
	/** Recovers life when it drops low. */
	Life,
	/** Recovers mana when it drops low. */
	Mana,
	/** Grants Guard after a heavy hit. */
	Guard,
};

constexpr int MaxCharmCharges = 40;
constexpr int CharmChargesPerUse = 20;
/** @brief Ruby Charm: goes off below 35% life and recovers 30% of it. */
constexpr int CharmLifeThreshold = 35;
constexpr int CharmLifeRecovery = 30;
/** @brief Sapphire Charm: goes off below 20% mana and recovers 40% of it. */
constexpr int CharmManaThreshold = 20;
constexpr int CharmManaRecovery = 40;
/** @brief Stone Charm: a hit for 15% of your life grants Guard, taking 40% less damage for 4 seconds. */
constexpr int GuardHitThreshold = 15;
constexpr int GuardDamageReduction = 40;
constexpr int GuardTicks = 4 * 20;
/** @brief A charm that went off waits a second before any charm can go off again. */
constexpr int CharmCooldownTicks = 20;

CharmEffect GetCharmEffect(const Item &item);
bool IsCharm(const Item &item);
std::string_view CharmDescription(CharmEffect effect);

void GainCharmCharges(Player &player, int charges);
void RefillCharms(Player &player);

/** @brief Sets off Ruby and Sapphire charms and counts down Guard; once per game tick. */
void ProcessCharms(Player &player);
/** @brief Takes Guard off a hit, then sets off a Stone Charm if the hit was heavy. */
Fixed26_6 ApplyGuard(Player &player, Fixed26_6 damage);

} // namespace devilution
