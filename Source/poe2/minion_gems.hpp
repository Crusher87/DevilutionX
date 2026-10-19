/**
 * @file poe2/minion_gems.hpp
 *
 * POE2 Mashup: the skeleton Spirit gems. Each raises its own kind of skeleton, told apart by its monster type, so a
 * skeleton always knows its gem: Warriors fight up close; the Skeletal Mages gem's Arsonists, Frost Mages and Storm Mages
 * cast from range; Snipers shoot arrows from afar; Reavers build Rage; Brutes hit slow, hard and stun; Clerics don't
 * fight but heal the others and raise the fallen sooner.
 */
#pragma once

#include <array>
#include <cstdint>

#include "poe2/player_data.hpp"
#include "tables/monstdat.h"
#include "utils/fixed_point.hpp"

namespace devilution {

struct Monster;
struct Player;
enum class MissileID : int8_t;

enum class MinionShot : uint8_t {
	None,
	Firebolt,
	Arrow,
	FrostBolt,
	/** Two charged bolts at once. */
	ChargedBolts,
};

struct MinionGemData {
	PersistentSkill skill;
	_monster_id type;
	/** Life, in percent of a Skeletal Warrior's. */
	uint8_t lifePercent;
	/** Damage of its blows, in percent of a Skeletal Warrior's; 0 for one that doesn't fight. */
	uint16_t damagePercent;
	MinionShot shot;
	/** Damage of a shot (of each of a Storm Mage's two bolts), in percent of a Skeletal Warrior's blow. */
	uint8_t shotPercent;
	/** It shoots at enemies from 2 tiles up to this far. */
	uint8_t range;
	/** Game ticks from the start of one attack to the next, for the slow ones; 0 attacks as fast as it can. */
	uint8_t attackTicks;
	/** POE2's Spirit per minion for gem levels 1 to 20. */
	std::array<uint8_t, 20> spirit;
	/** Minions the gem raises without Spirit, and how many it keeps until the player chooses. */
	uint8_t freeMinions;
	uint8_t startingCount;
};

/** @brief A Skeletal Reaver's blows build Rage, each point more damage; it fades a point at a time once it stops hitting. */
constexpr int ReaverMaxRage = 5;
constexpr int ReaverRagePercent = 12;
constexpr int ReaverRageTicks = 3 * 20;
constexpr int ReaverRageFadeTicks = 10;
/** @brief A Skeletal Brute's blows build this much more heavy stun than their damage, in percent. */
constexpr int BruteStunPercent = 200;
/** @brief A Skeletal Cleric heals the most hurt minion this close to it this often, if one has lost a tenth of its life. */
constexpr int ClericHealTicks = 3 * 20;
constexpr int ClericRadius = 5;
constexpr int ClericHurtPercent = 10;

/** @brief The skeleton gem's data, or nullptr for any other skill. */
const MinionGemData *FindMinionGem(PersistentSkill skill);
/** @brief The skeleton gem that raises this monster type, or nullptr. */
const MinionGemData *FindMinionGemByType(_monster_id type);
bool IsMinionGemType(_monster_id type);
/**
 * @brief The gem of a hero's skeleton: a minion raised by a skeleton gem, not one brought back by Revive or one of the
 * King's dead; nullptr for anything else.
 */
const MinionGemData *MinionGemOf(const Monster &monster);

/** @brief A Skeletal Cleric's heal, in percent of the minion's life. */
int ClericHealPercent(int gemLevel);
/** @brief The damage of a skeleton's blows from its kind and its Rage, in percent. */
int MinionGemDamagePercent(const Monster &minion);
MissileID MinionShotMissile(MinionShot shot);

/** @brief A skeleton's blow landed: a Reaver gains Rage, a Brute builds heavy stun. */
void OnMinionGemHit(const Player &owner, Monster &minion, Monster &target, Fixed26_6 damage);
/** @brief Whether one of the hero's Skeletal Clerics is standing, which raises the fallen twice as fast. */
bool ClericStands(const Player &player);
/** @brief Kills every skeleton of the hero's skeleton gems, as when the hero dies or leaves. */
void KillGemMinions(const Player &player);

/** @brief Once per game tick for the local hero: the skeletons' attack waits, the Reavers' Rage and the Clerics' heals. */
void ProcessMinionGems(Player &player);

} // namespace devilution
