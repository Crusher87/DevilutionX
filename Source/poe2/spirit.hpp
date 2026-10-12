/**
 * @file poe2/spirit.hpp
 *
 * POE2 Mashup Spirit: Spirit gems socketed in the skill bar reserve Spirit and stay active,
 * like POE2's persistent buffs (heralds and auras) and minions.
 */
#pragma once

#include <cstdint>
#include <string_view>

#include "poe2/player_data.hpp"

namespace devilution {

struct Item;
struct Monster;
struct Player;

/** @brief Every character has some Spirit, like the Spirit POE2 grants early in the campaign. */
constexpr int BaseSpirit = 30;
/** @brief Sceptres grant Spirit, as in POE2. */
constexpr int SceptreSpirit = 100;
constexpr int PurityResistance = 25;
/** @brief Herald of Ash: the explosion deals this share of the dead monster's life as fire to its neighbours. */
constexpr int HeraldOfAshPercent = 20;
constexpr int HeraldOfAshRadius = 2;
/** @brief Herald of Thunder: bolts hit this many enemies for a share of the dead monster's life. */
constexpr int HeraldOfThunderBolts = 2;
constexpr int HeraldOfThunderPercent = 25;
constexpr int HeraldOfThunderRadius = 6;
/** @brief A fallen golem comes back after 4 seconds. */
constexpr int GolemReviveTicks = 4 * 20;

std::string_view PersistentSkillName(PersistentSkill skill);
std::string_view PersistentSkillDescription(PersistentSkill skill);
int SpiritReservation(PersistentSkill skill);

/** @brief Whether the item grants Spirit as an implicit (sceptres). */
bool ItemGrantsSpirit(const Item &item);
int MaxSpirit(const Player &player);
/** @brief Spirit reserved by the active Spirit gems. */
int ReservedSpirit(const Player &player);
/**
 * @brief Whether a socketed Spirit gem is active. Gems reserve Spirit from the first skill slot on;
 * a gem the character can't use, or that doesn't fit in the Spirit left, stays inactive.
 */
bool IsPersistentSkillActive(const Player &player, PersistentSkill skill);

/** @brief Remembers a kill so the heralds can go off on the next game tick. */
void QueueHeraldTriggers(const Player &player, const Monster &monster);
/** @brief Sets off the queued heralds; once per game tick. */
void ProcessHeralds();
void ClearHeralds();

/** @brief Keeps the Summon Golem minion alive while its gem is active; once per game tick for the local player. */
void ProcessPersistentSkills(Player &player);

} // namespace devilution
