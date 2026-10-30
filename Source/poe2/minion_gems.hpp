/**
 * @file poe2/minion_gems.hpp
 *
 * POE2 Mashup: the minion Spirit gems. Each skeleton gem raises its own kind of skeleton, told apart by its monster type,
 * so a skeleton always knows its gem: Warriors fight up close; the Skeletal Mages gem's Arsonists, Frost Mages and Storm
 * Mages cast from range; Snipers shoot arrows from afar; Reavers build Rage; Brutes hit slow, hard and stun; Clerics don't
 * fight but heal the others and raise the fallen sooner. Wolf Pack's shadow beasts revive like the skeletons; Raging
 * Spirits' fire bats and Lingering Illusion's illusions last a few seconds. Ravenous Swarm's swarms aren't monsters at
 * all, so nothing can hit them. Sacrifice lets the corpse skills use the hero's skeletons.
 */
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "engine/point.hpp"
#include "poe2/player_data.hpp"
#include "tables/monstdat.h"
#include "utils/fixed_point.hpp"

namespace devilution {

struct Monster;
struct Player;
enum class MissileID : int8_t;
enum class DamageType : uint8_t;

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

/** @brief Wolf Pack's wolves: 3, up to 6 at gem level 20. */
int WolfPackSize(int gemLevel);

/** @brief Raging Spirits: a cast of the hero's own fire spell calls at most 2 with its hits, within 3 seconds; at most 10 burn at once, for 5 seconds each. */
constexpr int RagingSpiritsPerCast = 2;
constexpr int RagingSpiritsCastTicks = 3 * 20;
constexpr int RagingSpiritsMax = 10;
constexpr int RagingSpiritTicks = 5 * 20;
/** @brief The share of a raging spirit's blows that is fire. */
constexpr int RagingSpiritFirePercent = 70;

/** @brief Ravenous Swarm: while an enemy is this close, a swarm leaves the hero every 6 seconds, at most 3 out. */
constexpr int SwarmReachTiles = 4;
constexpr int SwarmEveryTicks = 6 * 20;
/** @brief A swarm bites every half second for this share of a Skeletal Warrior's blow, and poisons for this share of the bite over 2 seconds. */
constexpr int SwarmBiteTicks = 10;
constexpr int SwarmBitePercent = 25;
constexpr int SwarmPoisonPercent = 60;
constexpr int SwarmPoisonTicks = 2 * 20;
constexpr int SwarmPoisonStacks = 5;
/** @brief How long a swarm lasts, 6 seconds up to 9.8 at gem level 20. */
int SwarmTicks(int gemLevel);
/** @brief A swarm's bite in 1/64 points: a Skeletal Warrior's average blow at the gem's level, with the hero's minion damage. */
int32_t SwarmBiteRaw(const Player &player, int gemLevel);

/** @brief Sacrifice: the minions' revive time, in percent of POE2's 12 seconds: 159% at gem level 1, 140% at 20. */
int SacrificeRevivePercent(int gemLevel);

/** @brief Lingering Illusion: an illusion with 1% of the hero's life stands 3 seconds and draws enemies this close. */
constexpr int IllusionTicks = 3 * 20;
constexpr int IllusionDrawTiles = 6;
constexpr int MaxIllusions = 2;
/** @brief One destroyed by an enemy leaves a remnant for 5 seconds that gives a Frenzy charge; sometimes two. */
constexpr int IllusionRemnantTicks = 5 * 20;
int IllusionSecondRemnantChance(int gemLevel);

/** @brief The minion gem's data (a skeleton gem, Wolf Pack, Raging Spirits or Lingering Illusion), or nullptr for any other skill. */
const MinionGemData *FindMinionGem(PersistentSkill skill);
/** @brief The minion gem that raises this monster type, or nullptr. */
const MinionGemData *FindMinionGemByType(_monster_id type);
bool IsMinionGemType(_monster_id type);
/** @brief A Spirit gem that raises minions: the minion gems, the golems, the Guardian and Revive. Their levels take "+N to Level of all Minion Skills". */
bool IsMinionSkill(PersistentSkill skill);
/** @brief Every minion gem's data: the skeleton gems in MinionCountSkills order, then Wolf Pack, Raging Spirits and Lingering Illusion. */
std::span<const MinionGemData> MinionGemTable();
/**
 * @brief The gem of a hero's skeleton: a minion raised by a skeleton gem, not one brought back by Revive or one of the
 * King's dead; nullptr for anything else.
 */
const MinionGemData *MinionGemOf(const Monster &monster);

/** @brief A minion's life, in 1/64 points, and its blow's damage bytes as it rises, before its skill's supports. */
struct MinionRiseStats {
	int32_t lifeRaw;
	int minDamage;
	int maxDamage;
};
/** @brief A skeleton gem's (or Wolf Pack's, Raging Spirits') minion of the type at the gem level, with its owner's uniques' minion life. */
MinionRiseStats SkeletonMinionRiseStats(const Player &owner, _monster_id type, int gemLevel);
/** @brief A golem at the golem skill's or Golem spell's level, with its owner's uniques' minion life. */
MinionRiseStats GolemRiseStats(const Player &owner, int spellLevel);

/** @brief A Skeletal Cleric's heal, in percent of the minion's life. */
int ClericHealPercent(int gemLevel);
/** @brief The damage of a skeleton's blows from its kind and its Rage, in percent. */
int MinionGemDamagePercent(const Monster &minion);
MissileID MinionShotMissile(MinionShot shot);

/** @brief The share of the minion's blows that is fire: a raging spirit's 70%, else 0. */
int MinionGemFirePercent(const Monster &minion);

/**
 * @brief A minion's blow with its owner's gear's added damage (a sceptre's ally added damage), in 1/64 points: the
 * range's middle, as monster-on-monster hits carry one damage number and roll no more, raised by the owner's minion
 * damage as the blow is. @p dam is unchanged for anything but a POE2 player's minion.
 */
[[nodiscard]] int MinionBlowWithGearDamage(const Monster &attacker, int dam);
/** @brief What the owner's gear adds to each of their minions' blows, in 1/64 points (see MinionBlowWithGearDamage). */
[[nodiscard]] int32_t MinionGearAddedRaw(const Player &owner);
/** @brief A POE2 player's minion's life regeneration from its owner's gear, in 1/64 points a game tick; 0 for anything else. */
[[nodiscard]] int32_t MinionGearRegenRaw(const Monster &minion);
/** @brief A skeleton's blow landed: a Reaver gains Rage, a Brute builds heavy stun. */
void OnMinionGemHit(const Player &owner, Monster &minion, Monster &target, Fixed26_6 damage);
/** @brief One of the hero's minions died: an illusion destroyed by an enemy leaves Frenzy remnants. */
void OnMinionGemDied(const Player &owner, const Monster &minion);
/** @brief The hero used a skill themselves (not a trigger or a repeat): a fire spell may call raging spirits. */
void OnRagingSpiritsCast(Player &player, SkillSlot skill, uint32_t tags);
/** @brief The hero's skill hit a monster for this kind of damage: a fire hit of the last fire spell calls a raging spirit. */
void OnRagingSpiritsHit(Player &player, SkillSlot skill, const Monster &monster, DamageType type);
/** @brief A dodge roll began here: Lingering Illusion leaves an illusion of the hero. */
void OnIllusionDodge(Player &player, Point start);

/** @brief Sacrifice: the hero's skeleton nearest the tile, within the radius, that a corpse skill can use; nullptr without one or without Sacrifice. */
Monster *SacrificeMinionNear(const Player &player, Point center, int radius);
/** @brief The corpse skill uses the skeleton up: it falls, and rises again on its timer. */
void SacrificeMinion(Monster &minion);
/** @brief Whether one of the hero's Skeletal Clerics is standing, which raises the fallen twice as fast. */
bool ClericStands(const Player &player);
/** @brief Kills every skeleton of the hero's skeleton gems, as when the hero dies or leaves. */
void KillGemMinions(const Player &player);

/** @brief Once per game tick for the local hero: the skeletons' attack waits, the Reavers' Rage, the Clerics' heals, the short-lived
 * minions' time, the raging spirits called last tick, the swarms and the Frenzy remnants. */
void ProcessMinionGems(Player &player);
/** @brief Forgets the remnants and the spirits waiting to be called, on a new level. */
void ClearMinionGems();
/** @brief Swarms out now and Frenzy remnants lying about, for the HUD and tests. */
int SwarmCount(const Player &player);
size_t FrenzyRemnantCount();

} // namespace devilution
