/**
 * @file poe2/passive_tree.hpp
 *
 * POE2 Mashup passive tree: each level grants a point to spend on nodes linked to ones you already have.
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "poe2/unique_rules.hpp"
#include "tables/playerdat.hpp"

namespace devilution {

struct Player;
enum class DamageType : uint8_t;

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
	// Stage 1 tree stats. Weapon damage: increased attack damage while wielding that kind of weapon.
	/** Melee weapons and unarmed, daggers and thrown blades included. */
	MeleeDamage,
	/** Bows and crossbows. */
	BowDamage,
	StaffDamage,
	/** Two-handed melee weapons. */
	TwoHandDamage,
	/** One-handed melee weapons. */
	OneHandDamage,
	/** Increased spell damage in percent. */
	SpellDamage,
	/** Increased damage of that type in percent, attacks and spells. Cold is the Magic damage type. */
	FireDamage,
	LightningDamage,
	ColdDamage,
	/** Increased chance to ignite or shock, and increased freeze and stun buildup, in percent. */
	IgniteChance,
	ShockChance,
	FreezeBuildup,
	StunBuildup,
	/** Increased stun threshold in percent. */
	StunThreshold,
	/** Added to the chance to block with a shield. */
	BlockChance,
	/** Life and mana gained for each kill. */
	LifeOnKill,
	ManaOnKill,
	/** Increased flask recovery and flask charges gained, in percent. */
	FlaskRecovery,
	FlaskCharges,
	/** Faster energy shield recharge in percent. */
	EnergyShieldRecharge,
	/** Increased minion life in percent. */
	MinionLife,
	/** Light radius steps (each is a tenth of the base radius). */
	LightRadius,
	/** Life regenerated per second in tenths of a percent of maximum life. */
	LifeRegenRate,
	// Rule stats: these feed the unique rule of the same name (see AddPassiveRules).
	ManaCost,
	MaxElementalResist,
	MaxMagicResist,
	ArmourVsElements,
	EvasionVsSpells,
	HitDamageFromMana,
	ManaSpellDamage,
	LifeRecoveryToShield,
	NoEnergyShieldRecharge,
	StationaryDefences,
	MovingVulnerability,
	PhysicalToFire,
	SpellsSpendEnergyShield,
	LessEnergyShieldRecharge,
	LessManaRecovery,
	SpiritDamage,
	DamageVsRareUnique,
	LessDamageVsNormal,
	FifthAttackRepeat,
	NoEvasion,
	LessMaxLife,
	DistanceRule,
	SpellElementCycle,
	OffElementLess,
	// Capstone, crossover and Profane rules (stage 3 content pass); flags have the value 1.
	TwoHandKillFrenzy,
	BlockRiposte,
	ElementRotation,
	MinionSpellDamage,
	StaffToLightning,
	StaffArc,
	UnarmedHitStacks,
	ArrowPierce,
	SwapStrike,
	ArrowDamageMod,
	SpellDamageToFireAttacks,
	StaffAppliesToBow,
	Cull,
	FirstStrike,
	KillBurst,
	FlaskFrenzy,
	MoreVsStunned,
	// The Profane Roads' rules (design v8, Curtis 2026-10-18): flags with the value 1, read as held or not, never summed.
	/** Leoric's toll: 1 in 4 kills rise as hostile skeletons; Culled monsters stay dead. */
	DeadDoNotRest,
	/** Leoric's pact: two-handed passives and Fresh Meat reach bows; Culled and put-down dead rise for you; 1 in 3 other kills rise at full life. */
	KingsDeadPact,
	/** Bartuc's toll: a quarter of mana costs is paid in life. */
	BartucsHunger,
	/** Bartuc's pact: half your Magic counts as Strength; each melee attack costs 1% of maximum life. */
	PaidInBlood,
	/** The Sickness's toll: Kill Burst only from ailing monsters, and it passes their ailments on; then a short fever. */
	DarkSickness,
	/** The Sickness's pact: melee passives reach spell hits; every death spreads ailments; spells can't inflict them. */
	HiveBreath,
	/** The Maiden's toll: infravision and more damage against unaware monsters; light capped at 7, and monsters wake at 10 tiles. */
	EyesInTheDark,
	/** The Maiden's pact: bow passives reach staves; with a staff nothing shoots from beyond 5 tiles until it has struck; light capped at 4. */
	OutOfDarkness,
	Count,
};

/** @brief The four Profane Roads of design v8, each one-way from its door. */
enum class ProfaneRoad : uint8_t {
	/** The Warrior's, into the Rogue's bow side: The King's Dead. */
	Leoric,
	/** The Sorcerer's, into the Warrior's two-hand side. */
	Bartuc,
	/** The Monk's, into the Sorcerer's elemental side: The Carrier. */
	Sickness,
	/** The Rogue's, into the Monk's staff side: Out of Darkness. */
	Maiden,
	Count,
};

/** @brief Nodes on each road, from the door: small, small, toll, small, small, pact, greater small, greater small. */
constexpr int ProfaneRoadNodes = 8;

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
	/** A big boon with a price (POE2's keystones; until the start redesign these were named after Diablo's shrines). */
	Keystone,
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
	/** @brief Position in the tree, in tree units; the tree is a circle with a radius of about 1,200, the ascendancies in its middle. */
	int x;
	int y;
	std::vector<std::pair<PassiveStat, int>> stats;
	std::vector<PassiveNodeId> links;
	/** @brief For ascendancy nodes, the root node of the ascendancy they belong to. */
	std::optional<PassiveNodeId> ascendancyRoot;
	/** @brief The area of the tree: a class's own, a border shared by two classes, or Tristram for the ascendancies. */
	std::string region;
	/** @brief One line of Diablo lore for notables and shrines. */
	std::string lore;
	/** @brief The generator's tag for the node's part of the tree; "profane" marks a Profane Path and "capstone" a capstone notable. */
	std::string group;
	/** @brief The cluster the node belongs to (a lattice node is its own), and the centre its orbit arcs are drawn round. */
	int clusterId = 0;
	int gx = 0;
	int gy = 0;
	/** @brief The node's orbit round its cluster's centre; -1 for nodes of the attribute web (the lattice). */
	int8_t orbit = -1;
	/** @brief Which of the player's jewel slots a jewel socket uses. */
	std::optional<uint8_t> socketIndex;
	/** @brief The Profane Road the node is on, and its place from the door (1 to 8). */
	std::optional<ProfaneRoad> road;
	uint8_t roadStep = 0;
};

/** @brief A point in the tree, in tree units. */
struct PassiveTreePoint {
	float x;
	float y;
};

using PassiveTotals = std::array<int, static_cast<size_t>(PassiveStat::Count)>;

/** @brief Most animation frames passives may skip, for attacks and casts each. */
constexpr int8_t MaxPassiveSpeedFrames = 3;
/** @brief Percent of increased attack or cast speed (tree and gear together) that skips one animation frame. */
constexpr int SpeedPercentPerFrame = 10;
/** @brief Character level that unlocks the ascendancy, like POE2's first Trial of Ascendancy. */
constexpr int AscendancyLevel = 15;
constexpr int MaxAscendancyPoints = 8;

/** @brief Loads the tree from the POE2 Mashup mod, or clears it outside POE2 mode. */
void LoadPassiveTreeData();
const std::vector<PassiveNode> &PassiveTree();
/** @brief The points a link is drawn through, from the lower id node to the higher: its two ends for a straight link, more along a curve. Empty when the nodes aren't linked. */
std::span<const PassiveTreePoint> PassiveLinkPoints(PassiveNodeId a, PassiveNodeId b);
/** @brief Whether both ends of a link are on the attribute web (the lattice), which is drawn thicker. */
bool IsLatticeLink(const PassiveNode &a, const PassiveNode &b);
bool IsProfaneNode(const PassiveNode &node);
bool IsCapstoneNode(const PassiveNode &node);
/** @brief The node a Profane Road is entered from: its first node's one link off the road. */
std::optional<PassiveNodeId> ProfaneRoadDoor(ProfaneRoad road);
/** @brief The ordinary node past a Profane Road's last node. It can't step back onto the road. */
std::optional<PassiveNodeId> ProfaneRoadFarGate(ProfaneRoad road);
/** @brief Whether a hero holding `from` may take `to` next: a road is walked only from its door toward its far end. */
bool CanStepPassive(PassiveNodeId from, PassiveNodeId to);

/** @brief The class whose start and ascendancies a hero uses: the tree has the four main classes, so the Bard uses the Rogue's and the Barbarian the Warrior's. */
HeroClass PassiveTreeClass(HeroClass heroClass);
std::optional<PassiveNodeId> PassiveStartNode(HeroClass heroClass);

/** @brief A number for the tree's shape (node kinds and links); heroes saved on a different shape get their points back. */
uint32_t PassiveTreeHash();
/** @brief Refunds every passive if the hero's allocations were made on a differently shaped tree. */
void RefundPassivesForNewTree(Player &player);
/** @brief Refunds both weapon sets if a hero spent more set points than the books now give (books used to give 4). */
void RefundOverspentWeaponSets(Player &player);
/** @brief Refunds every passive if the hero spent more points than they now have (quest points became Books); true if it did. */
bool RefundOverspentPassives(Player &player);

/** @brief Points from the Books read (poe2/weapon_swap.hpp): quests and lords hand over Books, not points. */
int BookPassivePoints(const Player &player);
/** @brief One point per level after the first, plus the Books' points. */
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
/** @brief True when taking a node in this weapon set would need a normal passive point and none is left. */
bool WeaponSetNeedsPassivePoint(const Player &player, PassivePool pool);
bool CanAllocatePassive(const Player &player, PassiveNodeId node, PassivePool pool = PassivePool::Main);
/**
 * @brief The quickest way to a node in that pool's tree: an allocated node, then each node still to take, ending with the target.
 * Empty when the target is allocated already, can't be taken with that pool's points, or isn't linked to anything allocated.
 */
std::vector<PassiveNodeId> PassiveRoute(const Player &player, PassiveNodeId target, PassivePool pool = PassivePool::Main);
/** @brief Whether allocating the node needs the player to pick an attribute. */
bool PassiveNeedsAttributeChoice(PassiveNodeId node);
/** @param attribute The attribute picked for an "any Attribute" node; ignored for other nodes. */
bool AllocatePassive(Player &player, PassiveNodeId node, PassiveAttribute attribute = PassiveAttribute::Strength, PassivePool pool = PassivePool::Main);
/** @brief The attribute picked on an allocated "any Attribute" node. */
PassiveAttribute ChosenPassiveAttribute(const Player &player, PassiveNodeId node);
/** @brief The attribute picked on the node in that pool: Set I and Set II each keep their own pick (the low and high half of the saved byte). */
PassiveAttribute ChosenPassiveAttributeIn(const Player &player, PassiveNodeId node, PassivePool pool);
void SetChosenPassiveAttribute(Player &player, PassiveNodeId node, PassiveAttribute attribute, PassivePool pool);
PassiveStat PassiveAttributeStat(PassiveAttribute attribute);
std::string_view PassiveAttributeName(PassiveAttribute attribute);
/** @brief A node can be refunded if everything else allocated stays linked to the start. */
bool CanRefundPassive(const Player &player, PassiveNodeId node, PassivePool pool = PassivePool::Main);
bool RefundPassive(Player &player, PassiveNodeId node, PassivePool pool = PassivePool::Main);
void ResetPassives(Player &player);

/** @brief Sum of the stats of every allocated node; all zero in multiplayer, where passives aren't synced yet. */
PassiveTotals GetPassiveTotals(const Player &player);
/** @brief The tree's total of a stat as of the last CalcPlrItemVals, for combat code that runs every hit. */
int PassiveTotal(const Player &player, PassiveStat stat);
/** @brief Keeps the totals for PassiveTotal. */
void CachePassiveTotals(Player &player, const PassiveTotals &passives);
/** @brief Adds the rule stats to the unique rules, then caps the rules that must not stack without end. */
void AddPassiveRules(const PassiveTotals &passives, UniqueRuleTotals &rules);
/** @brief Increased attack damage from the weapon-type stats for the weapons the hero holds. */
int PassiveWeaponDamage(const Player &player, const PassiveTotals &passives);
/** @brief Strength for item and gem requirements and the melee damage bonus: with Paid in Blood (the pact of Bartuc's
 * Road) half of Magic, gear included, counts as Strength. The character sheet's Strength doesn't change. */
int RequirementStrength(const Player &player);
/** @brief The same for the given Strength and Magic, reading the hero's tree afresh (the equipment check runs before the
 * cached totals are updated). */
int RequirementStrength(const Player &player, int strength, int magic);
/** @brief Increased damage of one type (fire, lightning or Magic) from the tree. */
int PassiveElementDamage(const Player &player, DamageType type);

std::string PassiveStatText(PassiveStat stat, int value);
/** @brief A tooltip line: its text, and whether it is a price (drawn red). */
struct PassiveStatLine {
	std::string text;
	bool price;
};
/** @brief The stat's tooltip lines: one for most stats, the rule and its price apart for a Profane Road's rules. */
std::vector<PassiveStatLine> PassiveStatLines(PassiveStat stat, int value);
/** @brief Extra tooltip lines for a node a Profane Road starts or ends at. */
std::vector<PassiveStatLine> ProfaneRoadEndLines(PassiveNodeId node);
std::string PassiveNodeName(const PassiveNode &node);
/** @brief The node's stat lines as the player has them: an allocated "any Attribute" node names the attribute picked. */
std::vector<std::pair<PassiveStat, int>> PassiveNodeStats(const Player &player, const PassiveNode &node);

} // namespace devilution
