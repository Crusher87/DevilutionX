/**
 * @file poe2/table_skills.hpp
 *
 * POE2 Mashup: the rest of the skill roster, described by a data table instead of hand-written code.
 * Each skill is a shape (a strike, a fan of projectiles, a burst, a lingering zone, a curse, a buff...)
 * with its numbers, built from the effects Diablo already has.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "engine/point.hpp"
#include "poe2/player_data.hpp"
#include "sound_effect_enums.h"
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
	/** -30% chaos resistance (chaos damage plan step 11). */
	Despair,
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
enum TableFlag : uint32_t {
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
	/**
	 * A staff's granted skill priced per target: the first monster it hits (the nearest the aim, for an area) takes the
	 * full hit, every other one at most ExtraTargetPercentPerSecond of the spell base for each second of the cast, and a
	 * cast's projectiles hit each monster once.
	 */
	TF_PerTarget = 1 << 15,
	/** The skill's hits poison: POE2's poison, 20% of the hit a second for 2 seconds, the strongest only. */
	TF_Poison = 1 << 16,
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
	uint32_t flags;
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

/**
 * @brief What a per-target granted skill (TF_PerTarget) deals each monster but its first: this percent of the spell base
 * a second of its cast (the coordinator's per-target rule after the plan's value model, 10-28).
 */
constexpr int ExtraTargetPercentPerSecond = 25;

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
 * stack is the game's Wither (3% more damage taken), so ten make up to +30% (Curtis 2026-10-30: smaller steps, more stacks).
 */
constexpr int GrantedWitherMaxStacks = 10;
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
/** @brief Volatile Bolt: a monster it kills bursts for this percent of the hit, radius 1. */
constexpr int VolatileBurstPercent = 50;
/** @brief Galvanic Bolt zaps one more enemy within this many tiles for this percent of the hit. */
constexpr int GalvanicChainTiles = 2;
constexpr int GalvanicChainPercent = 50;
/** @brief Decompose's plague deals its hit again over 4 seconds; a monster that dies with it passes it to the nearest one. */
constexpr int PlagueTicks = 4 * 20;
constexpr int PlagueSpreadTiles = 3;
/** @brief Exsanguinate: a kill on a bleeding enemy restores this percent of maximum life. */
constexpr int ExsanguinateLifePercent = 1;
/** @brief Feast of Flesh: a kill restores this percent of maximum life and of maximum mana. */
constexpr int FeastRestorePercent = 2;
/** @brief Echoing Blast goes off again this many ticks later, for this percent of the hit. */
constexpr int EchoDelayTicks = 5;
constexpr int EchoPercent = 60;
/** @brief Chiming Bolt's every fourth cast leaves a Sigil of Power under the hero: 4 seconds, +20% spell damage inside. */
constexpr int SigilEvery = 4;
constexpr int SigilTicks = 4 * 20;
constexpr int SigilRadius = 2;
constexpr int SigilSpellDamagePercentInside = 120;
/** @brief Consecrated Ground: 1% of maximum life a second for 4 seconds; a new cast starts it over, it doesn't stack. */
constexpr int ConsecrateRegenPercentPerSecond = 1;
constexpr int ConsecrateRegenTicks = 4 * 20;
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
/**
 * @brief The percent of its hit a per-target granted skill deals each monster but the first a cast hits: its extra-target
 * budget (ExtraTargetPercentPerSecond for each second of its cast, or of its pulse; its bleed and echo count) over its
 * hit. 100 for every other skill.
 */
[[nodiscard]] int GrantedExtraTargetPercent(const TableSkillData &data);
/** @brief Whether the projectile's cast already hit the monster: a per-target skill's projectiles hit each one once. */
[[nodiscard]] bool GrantedMissileSkipsMonster(const Missile &missile, const Monster &monster);
/**
 * @brief The percent of its damage a per-target skill's projectile deals the monster it is hitting now: 100 for its
 * cast's first, GrantedExtraTargetPercent for the rest; 100 for every other projectile. Marks the monster hit.
 */
int GrantedMissileTargetPercent(const Missile &missile, Player &player, Monster &monster);
/** @brief The rider of the granted skill whose hit is being dealt now (SupportHitSkill), or None. */
GrantedRider HitGrantedRider();
/** @brief The hero's spell hits in percent: SigilSpellDamagePercentInside while they stand in their Sigil of Power, else 100. */
int SigilSpellDamagePercent(const Player &player);
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

/** @brief POE2 Mashup: a sound a skill makes, one of Diablo's or one of the mod's own files. */
struct SkillSound {
	SfxID sfx = SfxID::None;
	/** A mod sound's path (PlayModSfxLoc), when sfx is None. */
	std::string_view mod = {};

	[[nodiscard]] bool empty() const
	{
		return sfx == SfxID::None && mod.empty();
	}
};

/**
 * @brief POE2 Mashup: what a table skill sounds like, so every skill is heard as it is cast and as it goes off (Curtis
 * 2026-10-29: Freezing Shards, Lightning Bolt, Withering Bolt and Mana Drain made no sound). Each is the closest of
 * Diablo's sounds for the skill's element, shape and look, or one of the mod's frost sounds.
 */
struct SkillSounds {
	/**
	 * The hero casting it: a spell's incantation as its cast animation starts (as Diablo's spells do), an attack's
	 * swing, throw or shot as it lands.
	 */
	SkillSound cast;
	/**
	 * The skill going off, once a cast: its projectiles leaving, its blast, strike, curse or buff, its zone appearing;
	 * a Stream's with each shot, and a zone that bursts later (Meteor, Condemn, Glacial Spike, Frozen Orb) as it bursts.
	 */
	SkillSound spell;
	/**
	 * Its projectiles, landing bursts and zone strikes hitting, at most one each HitSoundGapTicks for the skill, so a
	 * fan of shards doesn't stack into noise; empty when its hits make no sound of their own (arrows, blades, blows).
	 */
	SkillSound hit;
};

/** @brief Game ticks between two hit sounds of one skill. */
constexpr int HitSoundGapTicks = 9;

/** @brief A table skill's sounds (see SkillSounds). */
[[nodiscard]] SkillSounds TableSkillSounds(const TableSkillData &data);
/** @brief Plays a skill's sound at a tile, nothing for an empty one; an ice shard is one of the four in turn. */
void PlaySkillSound(const SkillSound &sound, Point tile);
/** @brief Plays a table skill's hit sound at a tile, unless the skill's last one played under HitSoundGapTicks ago. */
void PlayTableSkillHitSound(const TableSkillData &data, Point tile);
/**
 * @brief A projectile is landing (its explosion is being added): plays its skill's hit sound, at most so often, and
 * returns the sound its explosion makes instead of its own, SfxID::None; nullopt keeps the explosion's own sound, for
 * every projectile that isn't a skill's.
 */
std::optional<SfxID> SkillMissileLandSound(const Missile &missile);

/** @brief Runs the zones on the ground; once per game tick for the local player. */
void ProcessSkillZones(Player &player);
void ClearSkillZones();
/** @brief How many zones are on the ground. */
size_t SkillZoneCount();
/** @brief Where a zone on the ground is, oldest first; nullopt past the last. */
std::optional<Point> SkillZoneCenter(size_t index);

/** @brief A monster's melee hit on the player, after curses and the player's defensive skills. */
Fixed26_6 ApplyTableSkillDefences(Monster &monster, Player &player, Fixed26_6 damage);
/** @brief Counts down a monster's table curses; once per game tick. */
void ProcessTableCurses(Monster &monster);
/** @brief More damage taken by a monster from table curses, in percent. */
int TableCurseDamageTakenPercent(const Monster &monster, DamageType type);
/**
 * @brief POE2 Mashup: how strong a table curse on a monster is, as the enemy debuff icons show it (Curtis 2026-10-30):
 * how much softer it hits for Weaken and Decrepify, how much more damage it takes for Death Mark and Decrepify, how
 * much of its hit Iron Maiden turns back. 0 when the curse isn't on it or has no strength to show.
 */
[[nodiscard]] int TableCurseShownPercent(const Monster &monster, TableCurse curse);
/** @brief A monster died: Haunt and Locust Swarm move on to the next. */
void OnTableCursedMonsterKilled(const Monster &monster);
/** @brief More damage from Soul Harvest's souls, in percent. */
int SoulHarvestDamagePercent(const Player &player);

} // namespace devilution
