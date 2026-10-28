/**
 * @file poe2/table_skills.hpp
 *
 * POE2 Mashup: the rest of the skill roster, described by a data table instead of hand-written code.
 * Each skill is a shape (a strike, a fan of projectiles, a burst, a lingering zone, a curse, a buff...)
 * with its numbers, built from the effects Diablo already has.
 */
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "engine/point.hpp"
#include "poe2/player_data.hpp"
#include "tables/misdat.h"
#include "utils/fixed_point.hpp"

namespace devilution {

struct ItemData;
struct Missile;
struct Monster;
struct Player;

/** @brief What a table skill does when it goes off. */
enum class TableShape : uint8_t {
	/** Hits the monster in front. */
	Strike,
	/** Hits the monster in front and both sides. */
	Arc,
	/** Hits everything around the player within the radius. */
	Around,
	/** Hits the monster in front `count` times. */
	Flurry,
	/** Throws `count` projectiles in a fan at the target. */
	Projectiles,
	/** Fires `count` projectiles one after another, `interval` ticks apart. */
	Stream,
	/** Hits everything within the radius of the target. */
	Burst,
	/** Dashes `count` tiles at the target, then hits everything within the radius. */
	Dash,
	/** Hits monsters along a line `count` tiles long, wave after wave. */
	Line,
	/** Leaves a zone on the ground (see TableZone). */
	Zone,
	/** Curses the monsters within the radius of the target. */
	Curse,
	/** A buff on the player, `duration` ticks long. */
	Buff,
	/** Strikes up to `count` monsters within the radius, one after another. */
	Seven,
	/** Blows up a corpse near the target. */
	CorpseBurst,
	/** Takes the souls of the corpses around the player. */
	SoulHarvest,
	/** Spends all Rage on a storm of ancestral blows. */
	CallOfTheAncients,
};

/** @brief How a Zone skill's zone behaves. */
enum class TableZone : uint8_t {
	None,
	/** Hits everything in the radius every `interval` ticks. */
	Pulse,
	/** Strikes a random spot in the radius every `interval` ticks. */
	RandomStrikes,
	/** Strikes the nearest monster in the radius every `interval` ticks. */
	NearestStrike,
	/** Waits for a monster to step close, then bursts once. */
	Trap,
	/** Shoots an arrow at the nearest monster in the radius every `interval` ticks. */
	Turret,
	/** Bursts once after `delay` ticks. */
	Delayed,
	/** After `delay` ticks, bursts into a ring of ice shards. */
	ShardRing,
	/** After `delay` ticks, an ice spike bursts out of the ground and hits everything in the radius once. */
	IceSpike,
};

enum class TableWeapon : uint8_t {
	None,
	Sword,
	Dagger,
	Axe,
	AxeOrSword,
	Mace,
	Staff,
	Bow,
	Crossbow,
	Shield,
	Melee,
};

enum class TableCurse : uint8_t {
	None,
	Weaken,
	IronMaiden,
	Decrepify,
	LowerResist,
	Confuse,
	Attract,
	DeathMark,
	BonePrison,
	Haunt,
	Locust,
};

enum class TableBuff : uint8_t {
	None,
	FrozenArmor,
	HolyShield,
	ShieldWall,
	WarCry,
};

/** @brief The POE2 gem whose per-level table a skill follows (see gem_levels.cpp). */
enum class TableCurve : uint8_t {
	Boneshatter,
	LeapSlam,
	LightningArrow,
	Earthquake,
	RainOfArrows,
	Fireball,
	Spark,
};

/** @brief Things a table skill's hits do besides damage. */
enum TableFlag : uint16_t {
	TF_Stun = 1 << 0,
	TF_Knockback = 1 << 1,
	TF_ArmourBreak = 1 << 2,
	TF_Bleed = 1 << 3,
	TF_Frenzy = 1 << 4,
	TF_Combo = 1 << 5,
	TF_Pierce = 1 << 6,
	/** Damage comes from Magic and the gem level, like a spell, instead of the weapon. */
	TF_Spell = 1 << 7,
	/** The zone moves with the player. */
	TF_FollowPlayer = 1 << 8,
	TF_Rage = 1 << 9,
	/** Costs 5% of current life as well as mana. */
	TF_LifeCost = 1 << 10,
	/** The skill's hits also burst into fire, lightning and cold. */
	TF_Elements = 1 << 11,
	/** The zone is placed where the player stands. */
	TF_AtPlayer = 1 << 12,
	/** The projectiles burst where they land after a short delay. */
	TF_LandingBurst = 1 << 13,
	/**
	 * A caster weapon grants it (caster-weapons-plan-v2.md 6.12): it has no gem, no mana cost of its own and no gemcutting
	 * tab, and is cast with a left click at the weapon's grant level.
	 */
	TF_Granted = 1 << 14,
};

/** @brief What a granted skill's hits do besides damage (caster-weapons-plan-v2.md 6.12). */
enum class GrantedRider : uint8_t {
	None,
	Wither,
	StunBuildup,
	ManaDrain,
	CullFrenzy,
	BurstOnKill,
	ChainOnce,
	Plague,
	KillRestoreLife,
	Weaken,
	EveryFifthDouble,
	Echo,
	SigilEveryFourth,
	RegenBuff,
	KillRestoreBoth,
};

/** @brief Gemcutting tabs a table skill shows under, as bits of GemCategory. */
enum TableTab : uint16_t {
	TT_Mace = 1 << 0,
	TT_Axe = 1 << 1,
	TT_Sword = 1 << 2,
	TT_Bow = 1 << 3,
	TT_Staff = 1 << 4,
	TT_Shield = 1 << 5,
	TT_Elemental = 1 << 6,
	TT_Occult = 1 << 7,
};

struct TableSkillData {
	WeaponSkill skill;
	std::string_view name;
	std::string_view description;
	/** The gem's tier level: an uncut gem of this level can be cut into it. */
	uint8_t level;
	uint8_t mana;
	TableWeapon weapon;
	/** The animation: 0 attack, 1 magic, 2 fire, 3 lightning. */
	uint8_t graphic;
	TableShape shape;
	/** Damage in percent of a weapon hit, or of the spell base for TF_Spell skills. */
	int16_t damagePercent;
	uint8_t count;
	uint8_t radius;
	/** Zone, curse and buff length, in game ticks. */
	uint16_t duration;
	uint8_t interval;
	uint8_t delay;
	DamageType type;
	MissileID missile;
	MissileGraphicID effect;
	uint16_t flags;
	TableZone zone;
	TableCurse curse;
	TableBuff buff;
	uint16_t tabs;
	/** A SpellIcon for the skill bar. */
	uint8_t icon;
	TableCurve curve;
	/** The damage at gem level 20 in percent of level 1, along the curve; 0 to copy the curve exactly. */
	int16_t curveEnd;
	/** A granted skill's rider. */
	GrantedRider rider = GrantedRider::None;
};

/** @brief Whether the table row is a skill a caster weapon grants. */
[[nodiscard]] inline bool IsGrantedTableSkill(const TableSkillData &data)
{
	return (data.flags & TF_Granted) != 0;
}

/**
 * @brief The tile a monster will be on, as hits see it, after this many game ticks: a walking monster keeps walking the
 * same way at its walk's speed, up to MaxPredictedSteps tiles; any other stays where it is. It reads only the monster's
 * position, walk and animation state, so every game in a party works it out the same.
 */
Point PredictMonsterTile(const Monster &monster, int ticks);
/** @brief The most tiles PredictMonsterTile leads a monster by: a very fast one outruns it. */
constexpr int MaxPredictedSteps = 2;
/** @brief Glacial Spike follows the monster it was cast at only while it's within this many tiles of the target. */
constexpr int IceSpikeTrackTiles = 4;

/** @brief Freezing Arrow's and Explosive Arrow's landing bursts hit for this percent of a base hit, on top of the arrow. */
constexpr int FreezingArrowBurstPercent = 40;
constexpr int ExplosiveBoltBurstPercent = 45;
/**
 * @brief Wither from a granted skill (Withering Bolt, Withering Ray): a stack a hit, up to this many, for 4 seconds. Each
 * stack is the game's Wither (5% more damage taken), so four make the plan's "up to +20%".
 */
constexpr int GrantedWitherMaxStacks = 4;
constexpr int GrantedWitherTicks = 4 * 20;
/** @brief Mana Drain's mana a hit at a skill level (the skill-balance review: 1 + level / 3), at most once a second a target. */
constexpr int ManaDrainMana(int level)
{
	return 1 + level / 3;
}
constexpr int ManaDrainCooldownTicks = 20;
/** @brief Mana Drain slows what it hits by this much for 2 seconds. */
constexpr int ManaDrainSlowPercent = 20;
constexpr int ManaDrainSlowTicks = 2 * 20;
/** @brief Power Siphon culls a monster its hit leaves on this percent of its life or less (a unique on 5%). */
constexpr int PowerSiphonCullPercent = 10;
constexpr int PowerSiphonUniqueCullPercent = 5;
/** @brief Torment Bolt weakens what it hits for 2 seconds. */
constexpr int TormentWeakenTicks = 2 * 20;
/** @brief Critical Bolt's every fifth cast hits for double. */
constexpr int CriticalBoltEvery = 5;
/** @brief A granted skill's bleed (Soulrend) deals this percent of its hit over GrantedBleedTicks (plan 7.1: +75% over 5 s). */
constexpr int GrantedBleedPercent = 75;
constexpr int GrantedBleedTicks = 5 * 20;
/** @brief The bolts a Burst skill sprays out (Fist of the Heavens) hit for this percent of the spell base each. */
constexpr int BurstBoltDamagePercent = 50;

/**
 * @brief Seven-Sided Strike's strikes land in full on this many enemies or more; on fewer, each strike deals their share
 * of it (a lone monster a third of each strike).
 */
constexpr int SevenSidedStrikeFullTargets = 3;
/** @brief The percent of a Seven-Sided Strike strike that lands with this many enemies about, of a burst of `burst`. */
[[nodiscard]] int SevenSidedStrikeSharePercent(int targets, int burst);

std::span<const TableSkillData> TableSkills();
/** @brief A projectile's landing burst in percent of a base hit: Freezing Arrow's and Explosive Arrow's set ones, else two thirds of the skill's percent. */
[[nodiscard]] int TableLandingBurstPercent(const TableSkillData &data);
/** @brief The table entry of a skill, or nullptr if it isn't a table skill. */
const TableSkillData *FindTableSkill(WeaponSkill skill);
/** @brief The table skill an item grants, from its name ("Zeal Gem"), or nullptr. */
const TableSkillData *FindTableSkillGem(const ItemData &data);

bool IsTableMeleeSkill(const TableSkillData &data);
bool IsTableProjectileSkill(const TableSkillData &data);
bool IsTableAreaSkill(const TableSkillData &data);

/** @brief Starts a table skill: pays for it and plays its animation or dash. */
bool StartTableSkill(Player &player, const TableSkillData &data);
/** @brief Makes a table skill go off, on the action frame of its animation. */
void ExecuteTableSkill(Player &player, const TableSkillData &data, Point target);
/** @brief Fires one shot of a Stream skill. */
void FireTableShot(Player &player, const TableSkillData &data, Point target, int index);
/**
 * @brief A table skill's projectile hit a monster for this much (whole points, or raw when shifted): a granted skill's
 * extras on the hit (Soulrend's bleed; the riders of caster-weapons-plan-v2.md 6.12).
 */
void OnTableMissileHit(const Missile &missile, Player &player, Monster &monster, int damage, bool isDamageShifted);
/** @brief The rider of the granted skill whose hit is being dealt now (SupportHitSkill), or None. */
GrantedRider HitGrantedRider();
/** @brief Puts a table curse skill's curse on a monster, for the given part of its duration. */
void ApplyTableCurse(const Player &player, Monster &monster, WeaponSkill skill, int durationPercent);
/**
 * @brief Blasphemy: puts a table curse on a monster at part strength for a few ticks. A curse cast by hand that lasts longer stays as it is.
 */
void ApplyAuraCurse(const Player &player, Monster &monster, WeaponSkill skill, int strengthPercent, int ticks);
/** @brief How strong a monster's table curse is, in percent: Blasphemy's are weaker than those cast by hand. */
[[nodiscard]] int TableCurseStrength(const Monster &monster, TableCurse curse);
/** @brief The cast effects of a table skill. */
void PlayTableSkillEffects(const Player &player, const TableSkillData &data, Point target);

/** @brief Runs the zones on the ground; once per game tick for the local player. */
void ProcessSkillZones(Player &player);
void ClearSkillZones();
/** @brief How many zones are on the ground. */
size_t SkillZoneCount();

/** @brief A monster's melee hit on the player, after curses and the player's defensive skills. */
Fixed26_6 ApplyTableSkillDefences(Monster &monster, Player &player, Fixed26_6 damage);
/** @brief Counts down a monster's table curses; once per game tick. */
void ProcessTableCurses(Monster &monster);
/** @brief More damage taken by a monster from table curses, in percent. */
int TableCurseDamageTakenPercent(const Monster &monster, DamageType type);
/** @brief A monster died: Haunt and Locust Swarm move on to the next. */
void OnTableCursedMonsterKilled(const Monster &monster);
/** @brief More damage from Soul Harvest's souls, in percent. */
int SoulHarvestDamagePercent(const Player &player);

} // namespace devilution
