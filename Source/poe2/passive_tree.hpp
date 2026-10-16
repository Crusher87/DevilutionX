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
	/** Added to the attribute the player picks when allocating the node. */
	AnyAttribute,
	Count,
};

/** @brief The attributes an "any Attribute" node can give. */
enum class PassiveAttribute : uint8_t {
	Strength,
	Dexterity,
	Magic,
	Vitality,
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
	/** "+5 to any Attribute": the player picks which when allocating it. */
	Attribute,
	/** Holds a jewel. */
	JewelSocket,
};

using PassiveNodeId = uint16_t;

/** @brief Which points a node is allocated with: the main tree, or one weapon set's points (they only count while that set is out). */
enum class PassivePool : uint8_t {
	Main,
	SetI,
	SetII,
};

struct PassiveNode {
	PassiveNodeId id;
	/** @brief Name of a notable; small nodes are named after their stats. */
	std::string name;
	PassiveKind kind;
	/** @brief The class that starts here, for start nodes, or that may take this ascendancy node. */
	std::optional<HeroClass> startClass;
	/** @brief Position in the tree; the tree is a circle with a radius of about 420, the ascendancies in its middle. */
	int x;
	int y;
	std::vector<std::pair<PassiveStat, int>> stats;
	std::vector<PassiveNodeId> links;
	/** @brief For ascendancy nodes, the root node of the ascendancy they belong to. */
	std::optional<PassiveNodeId> ascendancyRoot;
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

/** @brief The class whose start and ascendancies a hero uses: the tree has the four main classes, so the Bard uses the Rogue's and the Barbarian the Warrior's. */
HeroClass PassiveTreeClass(HeroClass heroClass);
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

/** @brief Ascendancies stay hidden until the hero can ascend; once one is chosen, the other is hidden again. */
bool IsPassiveNodeVisible(const Player &player, const PassiveNode &node);
/** @brief Whether the node counts right now: allocated in the main tree or in the weapon set that is out. */
bool IsPassiveAllocated(const Player &player, PassiveNodeId node);
/** @brief Whether the node is part of that pool's tree: main nodes belong to every pool, set nodes only to their set. */
bool IsPassiveAllocatedIn(const Player &player, PassiveNodeId node, PassivePool pool);
/** @brief Whether the node was allocated with that pool's points. */
bool IsPassiveInPool(const Player &player, PassiveNodeId node, PassivePool pool);
/** @brief Small, notable and attribute nodes can take weapon set points; ascendancies, shrines and jewel sockets can't. */
bool CanTakeWeaponSetPoints(const PassiveNode &node);
int WeaponSetPointsSpent(const Player &player, PassivePool pool);
int WeaponSetPointsLeft(const Player &player, PassivePool pool);
bool CanAllocatePassive(const Player &player, PassiveNodeId node, PassivePool pool = PassivePool::Main);
/** @brief Whether allocating the node needs the player to pick an attribute. */
bool PassiveNeedsAttributeChoice(PassiveNodeId node);
/** @param attribute The attribute picked for an "any Attribute" node; ignored for other nodes. */
bool AllocatePassive(Player &player, PassiveNodeId node, PassiveAttribute attribute = PassiveAttribute::Strength, PassivePool pool = PassivePool::Main);
/** @brief The attribute picked on an allocated "any Attribute" node. */
PassiveAttribute ChosenPassiveAttribute(const Player &player, PassiveNodeId node);
PassiveStat PassiveAttributeStat(PassiveAttribute attribute);
std::string_view PassiveAttributeName(PassiveAttribute attribute);
/** @brief A node can be refunded if everything else allocated stays linked to the start. */
bool CanRefundPassive(const Player &player, PassiveNodeId node, PassivePool pool = PassivePool::Main);
bool RefundPassive(Player &player, PassiveNodeId node, PassivePool pool = PassivePool::Main);
void ResetPassives(Player &player);

/** @brief Sum of the stats of every allocated node; all zero in multiplayer, where passives aren't synced yet. */
PassiveTotals GetPassiveTotals(const Player &player);

std::string PassiveStatText(PassiveStat stat, int value);
std::string PassiveNodeName(const PassiveNode &node);
/** @brief The node's stat lines as the player has them: an allocated "any Attribute" node names the attribute picked. */
std::vector<std::pair<PassiveStat, int>> PassiveNodeStats(const Player &player, const PassiveNode &node);

} // namespace devilution
