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
struct Monster;
struct Player;
enum class DamageType : uint8_t;

enum class CharmEffect : uint8_t {
	None,
	/** Recovers life when it drops low. */
	Life,
	/** Recovers mana when it drops low. */
	Mana,
	/** Grants Guard after a heavy hit. */
	Guard,
	/** Raises fire resistance when a fire hit lands. */
	Fire,
	/** Raises lightning resistance when a lightning hit lands. */
	Lightning,
	/** Raises magic resistance when a magic hit lands. */
	Magic,
	/** Breaks a stun and keeps you from being stunned for a moment. */
	Stun,
	/** Drops an extra item when you kill a rare or unique monster. */
	Golden,
};

constexpr int MaxCharmCharges = 40;
constexpr int CharmChargesPerUse = 20;
/** @brief Ruby, Topaz and Amethyst charms: +25% resistance to that element for 4 seconds. */
constexpr int ResistCharmBonus = 25;
constexpr int ResistCharmTicks = 4 * 20;
/** @brief Stone Charm: breaks a stun and keeps you from being stunned for 3 seconds. */
constexpr int StunImmuneTicks = 3 * 20;
/** @brief Bloodstone Charm: goes off below 35% life and recovers 30% of it. */
constexpr int CharmLifeThreshold = 35;
constexpr int CharmLifeRecovery = 30;
/** @brief Sapphire Charm: goes off below 20% mana and recovers 40% of it. */
constexpr int CharmManaThreshold = 20;
constexpr int CharmManaRecovery = 40;
/** @brief Iron Charm: a hit for 15% of your life grants Guard, taking 40% less damage for 4 seconds. */
constexpr int GuardHitThreshold = 15;
constexpr int GuardDamageReduction = 40;
constexpr int GuardTicks = 4 * 20;
/** @brief A charm that went off waits a second before any charm can go off again. */
constexpr int CharmCooldownTicks = 20;

CharmEffect GetCharmEffect(const Item &item);
bool IsCharm(const Item &item);
std::string_view CharmDescription(CharmEffect effect);
/** @brief Charges a charm spends each time it goes off; a Golden Charm spends all of them. */
int CharmChargesPerUseFor(CharmEffect effect);
/** @brief Charges the charm uses per activation, after its unique's cost. */
int CharmChargesPerUseFor(const Item &item);

void GainCharmCharges(Player &player, int charges);
void RefillCharms(Player &player);

/** @brief Sets off Bloodstone and Sapphire charms and counts down charm effects; once per game tick. */
void ProcessCharms(Player &player);
/** @brief Takes Guard off a hit, then sets off an Iron Charm if the hit was heavy. */
Fixed26_6 ApplyGuard(Player &player, Fixed26_6 damage);
/**
 * @brief A hit of this element is landing: sets off a Ruby, Topaz or Amethyst charm if one is ready.
 * @return The resistance to use against the hit, with any charm bonus added.
 */
int ResistanceAgainstHit(Player &player, DamageType damageType, int resistance);
/** @brief The player is about to be stunned: true if a Stone Charm breaks it or they can't be stunned right now. */
bool CharmPreventsStun(Player &player);
/** @brief The player killed this monster: extra items a Golden Charm adds to its drops. */
int GoldenCharmExtraDrops(Player &player, const Monster &monster);

} // namespace devilution
