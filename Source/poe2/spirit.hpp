/**
 * @file poe2/spirit.hpp
 *
 * POE2 Mashup Spirit: Spirit gems socketed in the Skills panel reserve Spirit and stay active,
 * like POE2's persistent buffs (heralds and auras) and minions.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "poe2/player_data.hpp"

namespace devilution {

struct Item;
struct Monster;
struct Player;

/** @brief Characters start with no Spirit, as in POE2; it comes from Spirit skulls (see SpiritSkullSpirit) and gear. */
constexpr int BaseSpirit = 0;
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
/** @brief Herald of Frost: a chilled or frozen enemy you kill shatters, dealing this share of its life as cold to its neighbours. */
constexpr int HeraldOfFrostPercent = 12;
constexpr int HeraldOfFrostRadius = 2;
/** @brief A fallen golem comes back after 4 seconds. */
constexpr int GolemReviveTicks = 4 * 20;
/** @brief A fallen skeleton rises again after 12 seconds, POE2's minion revive time. */
constexpr int SkeletonReviveTicks = 12 * 20;
/** @brief Skeletons that haven't been raised yet (a new level, a higher count) come up this many ticks apart. */
constexpr int SkeletonRaiseTicks = 4;

/** @brief How many skeletons Skeletal Warriors raised before the count could be chosen: 2, one more at character level 10 and 20. */
int SkeletalWarriorCount(int characterLevel);
/** @brief The same for Skeletal Mages: 1, one more at character level 20 and 40. */
int SkeletalMageCount(int characterLevel);

/** @brief Index of the skill in MinionCountSkills; -1 if its number of minions isn't chosen. */
int MinionCountSkillIndex(PersistentSkill skill);
inline bool IsMinionCountSkill(PersistentSkill skill) { return MinionCountSkillIndex(skill) >= 0; }
/** @brief Minions that reserve no Spirit: POE2's Skeletal Warriors raises its first 2 for free. */
int FreeMinions(PersistentSkill skill);
/** @brief Minions a gem raises until the player chooses, POE2's "desired initial amount". */
int StartingMinionCount(PersistentSkill skill);
/** @brief POE2's Spirit per minion at a gem level; higher levels cost less. */
int BaseSpiritPerMinion(PersistentSkill skill, int gemLevel);
/** @brief Spirit each minion past the free ones reserves at the socketed gem's level, with the uniques that raise reservations. */
int SpiritPerMinion(const Player &player, PersistentSkill skill);
/** @brief What a Spirit gem reserves at a gem level, for tooltips. */
std::string SpiritCostText(PersistentSkill skill, int gemLevel);
/** @brief Number of minions the player set for the gem in the Skills panel. */
int ChosenMinionCount(const Player &player, PersistentSkill skill);
/** @brief Sets it, from 0 to MaxMinionsPerGem. */
void SetChosenMinionCount(Player &player, PersistentSkill skill, int count);
/** @brief Minions the gem keeps: the chosen number, or as many as the Spirit left fits. */
int ActiveMinionCount(const Player &player, PersistentSkill skill);
/** @brief Most minions the gem could keep with the Spirit the gems before it leave, up to MaxMinionsPerGem. */
int AffordableMinionCount(const Player &player, PersistentSkill skill);
/** @brief Spirit the socketed gem reserves now. */
int GemSpiritReservation(const Player &player, PersistentSkill skill);
/** @brief The level a minion fights at: the highest character level its gem level is meant for. */
int MinionLevelForGemLevel(int gemLevel);
/** @brief Brings the gems' fallen minions back within this many game ticks. */
void HastenMinionRevives(Player &player, int ticks);
/** @brief Minions of the gem waiting to rise, and the game ticks until the first; for the HUD. */
int PendingMinionRevives(const Player &player, PersistentSkill skill, int *ticksLeft = nullptr);
/** @brief Minions the gem keeps up here: its count plus any a support adds, or 0 where none rise (town). */
int MinionTarget(const Player &player, PersistentSkill skill);
/** @brief The gem's minions alive on this level. */
int AliveMinionCount(const Player &player, PersistentSkill skill);

std::string_view PersistentSkillName(PersistentSkill skill);
std::string_view PersistentSkillDescription(PersistentSkill skill);
int SpiritReservation(PersistentSkill skill);
/** @brief Spirit the gem reserves when on, with its supports (Deadly Herald) and uniques that raise reservations. */
int GemSpiritCost(const Player &player, PersistentSkill skill);
/** @brief Whether a right-click in the Skills panel switches the gem on and off: all but Horadric Totem, which is used from the skill tray. */
[[nodiscard]] bool IsSpiritGemSwitchable(PersistentSkill skill);

/** @brief Whether the item grants Spirit as an implicit (sceptres). */
bool ItemGrantsSpirit(const Item &item);
int MaxSpirit(const Player &player);
/** @brief Spirit from the skulls the character has broken. */
int SkullSpirit(const Player &player);
/** @brief Which skull (0 to 2) the item is; -1 if it isn't a Spirit skull. */
int SpiritSkullIndex(const Item &item);
/** @brief Breaks a Spirit skull for its Spirit. @return true if the skull was used up. */
bool UseSpiritSkull(Player &player, const Item &item);
/** @brief Drops a Spirit skull when a boss that guards one dies and the player hasn't broken it yet. */
void DropSpiritSkull(const Monster &monster, bool sendmsg);
/** @brief Minion damage in percent of normal, from the passive tree and ascendancy. */
int MinionDamagePercent(const Player &player);
/** @brief Spirit reserved by the active Spirit gems. */
int ReservedSpirit(const Player &player);
/** @brief Right-click in the Skills panel: switches the Spirit gem in a socket on or off. @return false if it holds no Spirit gem. */
bool ToggleSpiritGem(Player &player, size_t socket);
/** @brief Spirit not reserved, which POE2's globe shows lit. */
inline int AvailableSpirit(const Player &player) { return MaxSpirit(player) - ReservedSpirit(player); }
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

/** @brief Keeps the Summon Golem, Skeletal Warriors and Skeletal Mages minions alive while its gem is active; once per game tick for the local player. */
void ProcessPersistentSkills(Player &player);

} // namespace devilution
