/**
 * @file poe2/passive_tree.hpp
 *
 * POE2 Mashup passive tree: each level grants a point to spend on nodes linked to ones you already have.
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tables/playerdat.hpp"

namespace devilution {

struct Player;

enum class PassiveStat : uint8_t {
	Strength,
	Magic,
	Dexterity,
	Vitality,
	/** Increased maximum life in percent. */
	LifePercent,
	/** Increased maximum mana in percent. */
	ManaPercent,
	/** Increased damage in percent. */
	Damage,
	ToHit,
	/** Increased armor in percent. */
	ArmorPercent,
	FireResist,
	LightningResist,
	MagicResist,
	AllResist,
	/** Life regenerated per second. */
	LifeRegen,
	/** Increased mana regeneration in percent. */
	ManaRegen,
	/** Added levels to all spells. */
	SpellLevels,
	/** Animation frames skipped by attacks. */
	AttackSpeed,
	/** Animation frames skipped by spell casts. */
	CastSpeed,
	/** Increased evasion in percent. */
	EvasionPercent,
	/** Increased maximum energy shield in percent. */
	EnergyShieldPercent,
	/** Added maximum Spirit. */
	Spirit,
	/** Increased damage of your minions in percent. */
	MinionDamage,
	Count,
};

enum class PassiveKind : uint8_t {
	Start,
	Small,
	Notable,
	/** A big boon with a price, like Diablo's shrines. */
	Shrine,
	/** The root of an ascendancy: choosing it picks that ascendancy class. */
	Ascendancy,
	/** A node inside an ascendancy, paid for with ascendancy points. */
	AscendancyNotable,
};

using PassiveNodeId = uint16_t;

struct PassiveNode {
	PassiveNodeId id;
	/** @brief Name of a notable; small nodes are named after their stats. */
	std::string name;
	PassiveKind kind;
	/** @brief The class that starts here, for start nodes, or that may take this ascendancy node. */
	std::optional<HeroClass> startClass;
	/** @brief Position in the tree; the tree is a circle with a radius of about 360, the ascendancies in its middle. */
	int x;
	int y;
	std::vector<std::pair<PassiveStat, int>> stats;
	std::vector<PassiveNodeId> links;
};

using PassiveTotals = std::array<int, static_cast<size_t>(PassiveStat::Count)>;

/** @brief Most animation frames passives may skip, for attacks and casts each. */
constexpr int8_t MaxPassiveSpeedFrames = 2;
/** @brief Character level that unlocks the ascendancy, like POE2's first Trial of Ascendancy. */
constexpr int AscendancyLevel = 15;
constexpr int MaxAscendancyPoints = 8;

/** @brief Loads the tree from the POE2 Mashup mod, or clears it outside POE2 mode. */
void LoadPassiveTreeData();
const std::vector<PassiveNode> &PassiveTree();

std::optional<PassiveNodeId> PassiveStartNode(HeroClass heroClass);

int PassivePointsTotal(const Player &player);
int PassivePointsSpent(const Player &player);
int PassivePointsLeft(const Player &player);

bool IsAscendancyNode(const PassiveNode &node);
/** @brief 2 ascendancy points at level 15, then 2 more every 10 levels, up to 8. */
int AscendancyPointsTotal(const Player &player);
int AscendancyPointsSpent(const Player &player);
int AscendancyPointsLeft(const Player &player);
/** @brief The root node of the ascendancy the player picked, if any. */
std::optional<PassiveNodeId> ChosenAscendancy(const Player &player);

bool IsPassiveAllocated(const Player &player, PassiveNodeId node);
bool CanAllocatePassive(const Player &player, PassiveNodeId node);
bool AllocatePassive(Player &player, PassiveNodeId node);
/** @brief A node can be refunded if everything else allocated stays linked to the start. */
bool CanRefundPassive(const Player &player, PassiveNodeId node);
bool RefundPassive(Player &player, PassiveNodeId node);
void ResetPassives(Player &player);

/** @brief Sum of the stats of every allocated node; all zero in multiplayer, where passives aren't synced yet. */
PassiveTotals GetPassiveTotals(const Player &player);

std::string PassiveStatText(PassiveStat stat, int value);
std::string PassiveNodeName(const PassiveNode &node);

} // namespace devilution
