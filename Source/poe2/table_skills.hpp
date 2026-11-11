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
#include <string>
#include <string_view>
#include <vector>

#include "engine/direction.hpp"
#include "engine/point.hpp"
#include "poe2/player_data.hpp"
#include "sound_effect_enums.h"
#include "tables/misdat.h"
#include "utils/fixed_point.hpp"

namespace devilution {

namespace vfx {
enum class Element : uint8_t;
} // namespace vfx

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
	/** Hits monsters along a line `count` tiles long, wave after wave; a spell's (Bone Spikes) is a cone that long. */
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
	/** Bursts the curses at least half run out on the monsters within the radius of the target (Hex Rupture). */
	HexBurst,
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
	/**
	 * Poisoned ground (an acid pool, a cloud, a vine): it lands with a burst of the skill's hit unless an arrow brought it,
	 * then poisons everything in the radius every `interval` ticks with the skill's poison of that hit.
	 */
	PoisonGround,
	/** A gas trap: waits for a monster to step within the radius, then puffs poison gas every `interval` ticks. */
	GasTrap,
	/** A pod: bursts when a monster steps next to it, or when its time runs out. */
	Pod,
	/** A bog around the hero: hinders the monsters in it, and the hero's arrows that cross it poison (Witch's Bog). */
	Bog,
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
	/** A mace or an axe (Arreat Crash). */
	MaceOrAxe,
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
	/**
	 * The skill's hits poison: POE2's poison for 2 seconds, the strongest only. It deals 20% of the hit a second, or the
	 * row's own poisonPercent (TablePoisonPercentPerSecond).
	 */
	TF_Poison = 1 << 16,
	/** A projectile's first hit bursts in its radius: every enemy there is poisoned, the others also hit (Plague Arrow). */
	TF_PoisonBurst = 1 << 17,
	/** Its hit traps the non-unique survivors (a heavy stun) for the skill's duration (Bone Prison). */
	TF_Pin = 1 << 18,
	/** The zone is thrown or shot over the enemies: it lands at the target after its flight (Acid Flask, the pods). */
	TF_Lobbed = 1 << 19,
	/** Where the projectile lands, it leaves the row's zone (Thornvine Arrow's vine). */
	TF_LandingZone = 1 << 20,
	/** A turret that shoots only the enemies the hero's poison is on (Plague Effigy). */
	TF_PoisonedOnly = 1 << 21,
};

/** @brief Venom Sentry's trap waits this long for an enemy before it's gone; Bloated Pods' pods burst when their time runs out. */
constexpr int VenomSentryArmTicks = 10 * 20;
/** @brief Venom Sentry springs when an enemy comes this close; a pod bursts when one is next to it. */
constexpr int PodTriggerTiles = 1;
/** @brief Bloated Pods' pods land this many tiles apart, across the line of the shot. */
constexpr int PodSpreadTiles = 2;
/** @brief Thornvine Arrow's vine and Witch's Bog hinder what's in them, in percent; the hinder is renewed this often. */
constexpr int ThornvineHinderPercent = 30;
constexpr int WitchsBogHinderPercent = 15;
constexpr int PoisonGroundHinderTicks = 12;
/** @brief Acid Flask's and Acid Cloud's pools out at once; the oldest goes. */
constexpr int AcidFlaskPools = 3;
constexpr int AcidCloudPools = 2;
/** @brief Rathma's Doom: a curse running out bursts for this percent of the spell base, radius 1, with this poison a second. */
constexpr int RathmasDoomPercent = 400;
constexpr int RathmasDoomRadius = 1;
constexpr int RathmasDoomPoisonPercent = 40;
/** @brief A monster takes at most one doom burst this often. */
constexpr int RathmasDoomCooldownTicks = 20;
/** @brief Festering Hex: the cursed are poisoned for this percent of the spell base a second, renewed this often. */
constexpr int FesteringHexPercent = 25;
constexpr int FesteringHexEvery = 20;
/** @brief Locust Swarm (the poison skills plan): POE2's Contagion as a poison. */
constexpr int LocustSwarmPercent = 165;

/** @brief Bone Spear flies through this many enemies (in practice every one in its path); other piercing skills through 3. */
constexpr int BoneSpearPierces = 20;
/** @brief Plague Arrow's burst hits the enemies around the one the arrow struck for this share of the arrow, in percent. */
constexpr int PlagueArrowSplashPercent = 50;
/** @brief How big Plague Arrow's green splash of venom is where it bursts (a normal hit's is 1). */
constexpr float PlagueArrowSplashSize = 1.4F;
/** @brief Hex Rupture bursts at most this many curses a cast, each with this radius; a monster takes one a cast. */
constexpr int HexRuptureMaxBursts = 10;
constexpr int HexRuptureBurstRadius = 1;
/**
 * @brief How wide Bone Spikes' cone is (Curtis 2026-11-10): a tile is in it when the cosine of its angle off the aim is
 * at least the square root of this percent, about 23 degrees either side. Along the grid it is 1, 1, 3, 3 and 5 tiles
 * wide.
 */
constexpr int BoneSpikesConeCosSquaredPercent = 85;

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
	TT_Dagger = 1 << 3,
	TT_Bow = 1 << 4,
	TT_Staff = 1 << 5,
	TT_Shield = 1 << 6,
	TT_Elemental = 1 << 7,
	TT_Occult = 1 << 8,
	TT_Arcane = 1 << 9,
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
	/**
	 * A poisoning skill's own poison (TF_Poison, TF_PoisonBurst): its damage a second, for 2 seconds, in percent of the
	 * hit. 0 is POE2's common poison, PoisonPercentPerSecond (20); Cobra Strike's is 60 and Poison Dagger's 100, as POE2's
	 * skills with a stronger poison have. A skill's poison never stacks on its own: only supports add stacks (Twin Fangs;
	 * Curtis 2026-11-03).
	 */
	uint8_t poisonPercent = 0;
	/**
	 * A weapon attack's hit that lands as the row's element, in percent of what the fire leaves (ApplyPlayerAttackDamage),
	 * as Smite's: the draft Staff gems' (Curtis 2026-11-07, Q48). Such a row's hit doesn't also land a third again as its
	 * element, as the older elemental attacks' does. 0 for every other row.
	 */
	uint8_t convertPercent = 0;
	/**
	 * The ailment its hits build, in percent: shock and ignite chance, and the freeze build-up of cold (Thunder Furrow's
	 * doubled shock chance, Northwind Wave's and Palm of the Frozen North's doubled freeze). 0 is 100.
	 */
	uint16_t ailmentPercent = 0;
};

/** @brief A poisoning table skill's poison a second, in percent of the hit: the row's own, or POE2's common 20%. */
[[nodiscard]] int TablePoisonPercentPerSecond(const TableSkillData &data);
/** @brief Whether a hero's arrow struck a monster in their Witch's Bog, was shot from it or flew over it on its way. */
[[nodiscard]] bool ArrowCrossedBog(const Player &player, Point from, Point to);
/**
 * @brief The tiles Bloated Pods' pods land on: the target, then by twos either side of it across the line of the shot,
 * PodSpreadTiles apart.
 */
[[nodiscard]] std::vector<Point> PodTiles(Point from, Point target, int count);
/** @brief Whether the monster is poisoned by this hero (Plague Effigy shoots only those). */
[[nodiscard]] bool PoisonedBy(const Monster &monster, const Player &player);
/** @brief A doom burst or a hex poison the curses' supports have ready; once per game tick for the local player. */
void ProcessCurseSupports(Player &player);

/** @brief One tile of a Bone Spikes cone: where, how many steps out from the hero, and whether it's its row's middle. */
struct BoneSpikesTile {
	Point tile;
	int step;
	bool middle;
};
/**
 * @brief The tiles a Bone Spikes cast bursts on: a cone from the hero toward the target, out to `length` steps, nearest
 * row first and each row from its middle out. A tile is in it when it lies within BoneSpikesConeCosSquaredPercent of the
 * aim, and the tile straight ahead always is. Only tiles the hero can see: never past a wall. The spikes are drawn on
 * exactly these tiles. Whole numbers only, so every machine in a game gets the same tiles.
 */
[[nodiscard]] std::vector<BoneSpikesTile> BoneSpikesTiles(Point from, Point target, int length);

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
/** @brief Fire Arrow's landing burst hits for this percent of the arrow's own percent (Curtis 2026-11-07, Q32). */
constexpr int FireArrowBurstShare = 45;
/** @brief Zeal (Curtis 2026-11-07, Q37): every this many uses is a heavy blow at this percent on every monster beside you. */
constexpr int ZealHeavyEvery = 4;
constexpr int ZealHeavyPercent = 150;
/** @brief Zeal's count starts over this many game ticks after the last use (3 seconds). */
constexpr int ZealResetTicks = 60;
/** @brief Falling Sword (Q37): the hero fades out for this long as the cast begins (0.4 s), untargetable. */
constexpr int FallingSwordFadeTicks = 8;
/** @brief Falling Sword's holy ground: radius, game ticks (3 s) and percent of the hit it burns for each second. */
constexpr int FallingSwordGroundRadius = 2;
constexpr int FallingSwordGroundTicks = 60;
constexpr int FallingSwordGroundPercent = 20;
/** @brief Explosive Arrow (Q37): its arrow sticks where it lands and blows this many game ticks later (1 s). */
constexpr int ExplosiveArrowFuseTicks = 20;
/** @brief Shield Wall (Q37): POE2's wall of earth, its segments, their life in percent of the hero's, and its shatter. */
constexpr int ShieldWallSegments = 5;
constexpr int ShieldWallLifePercent = 20;
constexpr int ShieldWallShatterTiles = 2;
constexpr int ShieldWallShatterPercent = 100;
/** @brief Blessed Shield (Q37): the thrown shield bounces to this many more monsters within this many tiles, each bounce at this percent. */
constexpr int BlessedShieldBounces = 3;
constexpr int BlessedShieldBounceTiles = 4;
constexpr int BlessedShieldBouncePercent = 75;
/** @brief Rend (Curtis 2026-11-07, Q38): Rage for each monster it hits. */
constexpr int RendRagePerHit = 3;
/** @brief Death Mark (Q38): the next Shadow Lunge or Cobra Strike on the marked monster takes the mark for this percent. */
constexpr int DeathMarkConsumePercent = 200;
/**
 * @brief Wave of Light (Q38): at full Combo it drops a bell in front of the hero, a weapon hit at this percent in this
 * radius; it stands this many game ticks (6 s), and each hit on it tolls for this percent in this radius.
 */
constexpr int WaveBellPercent = 100;
constexpr int WaveBellRadius = 2;
constexpr int WaveBellTicks = 120;
constexpr int WaveBellTollPercent = 50;
constexpr int WaveBellTollRadius = 2;
/**
 * @brief Game ticks between two tolls of the bell: one a swing, however many of the bell's tiles it hits. Only weapon
 * swings toll it, not area skills sweeping its tile (Curtis 2026-11-07, Q42).
 */
constexpr int WaveBellTollCooldown = 12;
/** @brief Glacial Spike (Q38): a frozen monster it hits takes this percent of the hit and loses the freeze. */
constexpr int GlacialSpikeFrozenPercent = 200;
/** @brief Attract (Curtis 2026-11-07, Q36): monsters this near the cursed monster attack it instead of the hero. */
constexpr int AttractTiles = 5;
/** @brief Confuse cast at a rare or unique from this gem level gives it Berserk's old effect (Q36). */
constexpr int ConfuseBerserkGemLevel = 10;
/** @brief Decrepify slows the cursed monster's actions this much: a normal or magic one, a rare, a unique or boss (Q36). */
constexpr int DecrepifySlowPercent = 30;
constexpr int DecrepifyRareSlowPercent = 20;
constexpr int DecrepifyUniqueSlowPercent = 10;
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

// The draft gems' Staff batch (Curtis 2026-11-07, Q46-Q48; drafts-61-priced.json).
/** @brief Wrath of the Skies: its bolts hit for this percent of the slam, and fly this many tiles in a fan from it. */
constexpr int WrathBoltSharePercent = 50;
constexpr int WrathBoltTiles = 5;
/** @brief Frostvein Rupture: the spike at the line's end hits for this percent of the line, a frozen enemy for this percent more. */
constexpr int FrostveinSpikePercent = 200;
constexpr int FrostveinFrozenPercent = 150;
/** @brief Rime Strikes: every this many strikes throws ice shards ahead, this many, this many tiles. */
constexpr int RimeStrikesShardEvery = 3;
constexpr int RimeStrikesShards = 3;
constexpr int RimeStrikesShardTiles = 4;
/** @brief Rime Strikes' count starts over this many game ticks after the last strike (3 seconds). */
constexpr int RimeStrikesResetTicks = 60;
/** @brief Northwind Wave: the chill it leaves on what it hits, in percent. */
constexpr int NorthwindChillPercent = 50;
/** @brief Shatterpalm: a frozen enemy takes this percent of the hit and shatters, a cold burst of this percent in this radius. */
constexpr int ShatterpalmFrozenPercent = 140;
constexpr int ShatterpalmBurstPercent = 25;
constexpr int ShatterpalmBurstRadius = 1;
/** @brief Dervish Advance spins this many times along its walk, each pulse all around the hero. */
constexpr int DervishPulses = 3;
/** @brief Storm Siphon: a shocked target takes this percent of the hit; the shock arcs on to this many within this many tiles for this percent. */
constexpr int StormSiphonShockedPercent = 150;
constexpr int StormSiphonArcs = 2;
constexpr int StormSiphonArcTiles = 3;
constexpr int StormSiphonArcPercent = 25;
/** @brief Phantom Step's strikes each hit all within this radius of the enemy it blinks to. */
constexpr int PhantomStepStrikeRadius = 1;
/** @brief Tempest Sweep's gale hits what is beyond the sweep for this percent of it, out to the row's count of tiles. */
constexpr int TempestGalePercent = 60;
/** @brief Skylord's Storm blinks the hero back this many tiles, or as far as there is room. */
constexpr int SkylordBlinkTiles = 3;
/** @brief Palm of the Frozen North's lance hits for this percent of the palm and flies this many tiles. */
constexpr int PalmLancePercent = 60;
constexpr int PalmLanceTiles = 5;
/**
 * @brief The tiles Northwind Wave's cone covers: in front of the hero out to `length` steps, widening from three tiles
 * to five, stopped by walls.
 */
[[nodiscard]] std::vector<Point> NorthwindWaveTiles(Point from, Direction dir, int length);
/** @brief Whether a Rime Strikes use about to start is the third, which throws the shards and swings slower. */
[[nodiscard]] bool RimeStrikesNextIsThird(const Player &player);
/** @brief The ailment build-up of a table skill's hits, in percent (the row's ailmentPercent). */
[[nodiscard]] int TableAilmentPercent(const TableSkillData &data);
/** @brief What a table skill does besides its hit, for its tooltip; empty for most. */
[[nodiscard]] std::string TableSkillExtraNote(WeaponSkill skill);

// The draft gems' Mace, Axe and Sword batch (Curtis 2026-11-07, Q46-Q48; drafts-61-priced.json).
/** @brief The war cries whose hits wait on the hero's next melee hits (Poe2PlayerData::cryHits). */
enum class EmpoweringCry : uint8_t {
	Hellfire,
	Quaking,
	BulKathos,
	/** Bulwark Roar's shield waves, which shield attacks send. */
	Bulwark,
};
static_assert(static_cast<size_t>(EmpoweringCry::Bulwark) == BulwarkRoarCry);
/** @brief Hellfire Roar's burst is around what each empowered hit struck, this many tiles. */
constexpr int HellfireBurstRadius = 1;
/** @brief Quaking Roar: each empowered slam's aftershock hits what the slam hit, this percent of the hit, this many ticks later. */
constexpr int QuakingAftershockPercent = 40;
constexpr int QuakingAftershockTicks = 6;
/** @brief Cry of Bul-Kathos: each empowered strike strikes again this many ticks later. */
constexpr int BulKathosRepeatTicks = 4;
/** @brief Frostforged Hammer: the sides take this percent; every this many blows freezes the target this many ticks. */
constexpr int FrostforgedSidePercent = 50;
constexpr int FrostforgedFreezeEvery = 3;
constexpr int FrostforgedFreezeTicks = 20;
constexpr int FrostforgedResetTicks = 60;
/** @brief Magma Hurl: the rock's burst hits those beside its target; this many shrapnel bolts fly on, this percent, this many tiles. */
constexpr int MagmaHurlShrapnel = 4;
constexpr int MagmaHurlShrapnelPercent = 50;
constexpr int MagmaHurlShrapnelTiles = 3;
/** @brief Arreat Crash lands this far ahead at most; its rings of ice, this percent of the slam, this many ticks after it. */
constexpr int ArreatReachTiles = 2;
constexpr int ArreatRingPercent[2] = { 60, 35 };
constexpr int ArreatRingTicks[2] = { 4, 8 };
/** @brief Earthsplitter: an enemy with broken armour takes this percent of the hit and sends a shockwave of this percent. */
constexpr int EarthsplitterBrokenPercent = 140;
constexpr int EarthsplitterShockwavePercent = 25;
constexpr int EarthsplitterShockwaveRadius = 1;
/** @brief Brimstone Rift: the forks at the rift's end, this many tiles each; the burning ground, this percent a pulse; this many at once. */
constexpr int BrimstoneForkTiles = 3;
constexpr int BrimstoneGroundPercent = 15;
constexpr int BrimstoneRifts = 2;
/** @brief Ancestor Totem: one at a time; its slams hit all within this radius of the enemy they fall on. */
constexpr int AncestorTotems = 1;
constexpr int AncestorSlamRadius = 1;
/** @brief Hephasto's Hammer: its burst on those beside its target; a warcry this near shatters it into lines of fire. */
constexpr int HephastosSplashPercent = 50;
constexpr int HephastosCryTiles = 4;
constexpr int HephastosLines = 5;
constexpr int HephastosLineTiles = 3;
constexpr int HephastosLinePercent = 50;
/** @brief Hammer of Zakarum falls this far away at most; a stunned enemy takes this percent. */
constexpr int HammerOfZakarumReachTiles = 4;
constexpr int HammerOfZakarumStunnedPercent = 160;
/** @brief Mountain's Fall lands this far away at most; its spikes stand this far out; the one in the middle hits for this percent. */
constexpr int MountainsFallReachTiles = 4;
constexpr int MountainsFallArmTiles = 2;
constexpr int MountainsFallCentrePercent = 150;
/** @brief Storm Edge's storm lashes the nearest enemy for this percent of the strike. */
constexpr int StormEdgeBeamPercent = 30;
/** @brief Frostbite Blades' blades: this percent of the strike, this many tiles, through this many enemies. */
constexpr int FrostbiteBladePercent = 50;
constexpr int FrostbiteBladeTiles = 4;
constexpr int FrostbiteBladePierces = 1;
/** @brief Cinder Strike's molten balls: this percent of the strike each, landing up to this many tiles ahead. */
constexpr int CinderBallPercent = 40;
constexpr int CinderBallTiles = 3;
/** @brief Steel Tempest's storm: this percent of the spin a pulse; this many at once. */
constexpr int SteelTempestStormPercent = 20;
constexpr int SteelTempestStorms = 2;
/** @brief Thunder Hatchet's lightning leaps this many times, each within this many tiles, for this percent of the throw. */
constexpr int ThunderHatchetChains = 3;
constexpr int ThunderHatchetChainTiles = 3;
constexpr int ThunderHatchetChainPercent = 60;
/** @brief Blood Harvest: a bleeding enemy takes this percent; one it kills bursts for this percent of the hit. */
constexpr int BloodHarvestBleedingPercent = 150;
constexpr int BloodHarvestBurstPercent = 25;
/** @brief Pyre Blow: the sides take this percent; what dies within the row's duration of its hit bursts for this percent. */
constexpr int PyreBlowSidePercent = 50;
constexpr int PyreBlowBurstPercent = 50;
/** @brief Mammoth Charge: what is beside the path takes this percent, and its cracks burst for this percent with the slam. */
constexpr int MammothPathPercent = 50;
constexpr int MammothCrackPercent = 30;
/** @brief Whether Frostforged Hammer's next blow is the third, which freezes. */
[[nodiscard]] bool FrostforgedNextFreezes(const Player &player);
/** @brief Brimstone Rift's tiles: a rift `length` tiles ahead, then a fork of BrimstoneForkTiles to either side; stopped by walls. */
[[nodiscard]] std::vector<Point> BrimstoneRiftTiles(Point from, Direction dir, int length);
/** @brief Mountain's Fall's five spikes: the centre, then MountainsFallArmTiles out to the north, east, south and west. */
[[nodiscard]] std::vector<Point> MountainsFallSpikes(Point center);
/**
 * @brief The percent of Mountain's Fall's hit (of `percent`) a tile takes, or 0 off it: a + as the screen shows it, its
 * arms `width` tiles wide (the row's radius) and running out past the arm spikes, the middle 5 tiles the centre spike's
 * (balance re-score, batch2-fixes.md 6: about 21 tiles, where the grid-diagonal arms had covered 41).
 */
[[nodiscard]] int MountainsFallPercentAt(Point center, Point tile, int width, int percent);
/** @brief Where a slam that reaches only so far lands, aimed at the target: the tile in sight that far at most. */
[[nodiscard]] Point SlamLanding(const Player &player, Point target, int reach);
/** @brief A warcry went off here: the Hephasto's Hammers lodged near it shatter into lines of fire. */
void ShatterLodgedHammers(Player &player, Point at);

// The draft gems' Sword and Shield batch (Q48).
/** @brief Phantom Blade: the ghost sword flies this many tiles out, then back to the hero, piercing up to this many. */
constexpr int PhantomBladeTiles = 6;
constexpr int PhantomBladePierces = 20;
/** @brief Horadric Sundering: a frozen, shocked or burning enemy in it bursts this far for this percent (frozen, the second). */
constexpr int HoradricBurstRadius = 1;
constexpr int HoradricBurstPercent = 25;
constexpr int HoradricFrozenBurstPercent = 50;
/** @brief Horadric Sundering: an enemy takes at most this many of its bursts a pulse. */
constexpr int HoradricMaxBursts = 2;
/** @brief Zakarum's Judgement: the ring comes again this many ticks later. */
constexpr int ZakarumsJudgementTicks = 8;
/** @brief Bulwark Roar: its Guard is this percent of maximum life; each shield wave runs this many tiles. */
constexpr int BulwarkGuardPercent = 10;
constexpr int BulwarkWaveTiles = 4;
/**
 * @brief Where a table skill's hits land when aimed at the target, for the supports that follow them (Echo of Arreat,
 * Immortal King's Stride): Wrath of the Skies slams the tile in front, a slam that reaches only so far its landing.
 */
[[nodiscard]] Point TableSkillHitTarget(const Player &player, WeaponSkill skill, Point target);
/** @brief A shield attack: a shield gem's attack, Raise Shield's bash or Shield Charge, which Bulwark Roar's waves follow. */
[[nodiscard]] bool IsShieldAttack(SkillSlot skill);
/** @brief Bulwark Roar's shield wave, from the hero towards the target. */
void SendBulwarkWave(Player &player, Point target);
/** @brief What Bulwark Roar's Guard leaves of a hit on the hero, taking the rest from the Guard. */
[[nodiscard]] Fixed26_6 AbsorbBulwarkGuard(Player &player, Fixed26_6 damage);

// The draft gems' Bow batch (Q48, build order 3): real arrows, with the bow's arrow supports.
/** @brief Cold Arrow: what it hits is chilled this much. */
constexpr int ColdArrowChillPercent = 50;
/** @brief Deadeye Shot: a frozen target takes this percent of the arrow, and its freeze shatters. */
constexpr int DeadeyeFrozenPercent = 160;
/** @brief Shatter Shot: on its first hit this many ice shards fly on behind the target, this many tiles, for this percent of the arrow. */
constexpr int ShatterShotShards = 6;
constexpr int ShatterShotShardTiles = 3;
constexpr int ShatterShotShardPercent = 50;
/** @brief Spark Arrow: its first hit bursts this far for this percent of the arrow, and shocks all it hits. */
constexpr int SparkArrowBurstRadius = 1;
constexpr int SparkArrowBurstPercent = 60;
/** @brief Strafe: on its first hit the arrow splits into this many arrows in a ring, for this percent each, piercing this many. */
constexpr int StrafeSplitArrows = 6;
constexpr int StrafeSplitPercent = 50;
constexpr int StrafeSplitPierces = 1;
/** @brief Kindling Arrow: its first hit blasts this far; a burning enemy takes this percent of the arrow and of the blast. */
constexpr int KindlingBlastRadius = 1;
constexpr int KindlingBurningPercent = 160;
/** @brief Whirling Volley: arrows loosed in a ring where the spin ends; each monster takes one of them a use. */
constexpr int WhirlingVolleyArrows = 16;
/** @brief Lodestone Salvo: at most this many of its bolts fall on one monster; a shocked one takes this percent. */
constexpr int LodestoneBoltsPerMonster = 2;
constexpr int LodestoneShockedPercent = 160;
/**
 * @brief Swamp Gas Arrow: the arrow hits for this percent; at most this many clouds; a fire hit or a burning enemy in a
 * cloud sets off its one fire burst, this far for this percent, and the cloud stays.
 */
constexpr int SwampGasArrowPercent = 30;
constexpr int SwampGasClouds = 3;
constexpr int SwampGasBurstRadius = 2;
constexpr int SwampGasBurstPercent = 40;
/** @brief Game ticks a fire hit counts as fresh on a monster, for Swamp Gas Arrow's clouds. */
constexpr int FireHitTicks = 3;
/**
 * @brief The percent of its hit a table skill's arrow deals this monster before it lands, and what it uses up: Deadeye
 * Shot's frozen target (its freeze shatters), Kindling Arrow's burning one. 100 for the others.
 */
[[nodiscard]] int TableArrowHitPercent(const TableSkillData &data, Monster &monster);

// The draft gems' Dagger batch (Q48, build order 4): stabs, two throws and a sentry.
/** @brief Fan of Blades: each blade that hits bleeds the monster for this percent of its hit over this many ticks. */
constexpr int FanOfBladesBleedPercent = 30;
constexpr int FanOfBladesBleedTicks = 60;
/** @brief Shock Web: the web lands with a burst of this percent of the hit, then pulses for this percent; this many webs at most. */
constexpr int ShockWebBurstPercent = 50;
constexpr int ShockWebPulsePercent = 25;
constexpr int ShockWebWebs = 2;
/** @brief Blades of Ice: every this many stabs freezes the target this many ticks; the count starts over after this many idle. */
constexpr int BladesOfIceFreezeEvery = 3;
constexpr int BladesOfIceFreezeTicks = 20;
constexpr int BladesOfIceResetTicks = 60;
/**
 * @brief Phoenix Strike: every this many strikes adds an element in turn (the count starts over after this many idle
 * ticks): a fire burst this far for this percent of the strike; a ring of this many ice shards this many tiles for this
 * percent each; lightning that leaps to this many enemies, this far each leap, for this percent.
 */
constexpr int PhoenixEvery = 3;
constexpr int PhoenixResetTicks = 60;
constexpr int PhoenixFireRadius = 1;
constexpr int PhoenixFirePercent = 150;
constexpr int PhoenixShards = 6;
constexpr int PhoenixShardTiles = 3;
constexpr int PhoenixShardPercent = 60;
constexpr int PhoenixChains = 3;
constexpr int PhoenixChainTiles = 3;
constexpr int PhoenixChainPercent = 80;
/** @brief Bloodletting: a bleeding target takes this percent of the stab, and another skill's bleed on it is dealt at once. */
constexpr int BloodlettingBleedingPercent = 160;
/** @brief Ember Fang: its ignite spreads to this many enemies within this many tiles of the target. */
constexpr int EmberFangSpreads = 2;
constexpr int EmberFangSpreadTiles = 1;
/** @brief Death Sentry: every this many ticks it blows up one corpse within this many tiles; this many sentries at most (MaxPlacedTraps). */
constexpr int DeathSentryIntervalTicks = 20;
constexpr int DeathSentryReachTiles = 4;
/** @brief Viz-Jaq'taar Strike: a lone monster takes this many of its stabs (the others go to the next enemies in turn). */
constexpr int VizJaqtaarStabsEach = 2;
/** @brief Winter's Edge: a frozen target takes this percent, and shatters into this many shards within this far, this percent each. */
constexpr int WintersEdgeFrozenPercent = 150;
constexpr int WintersEdgeShards = 4;
constexpr int WintersEdgeShardRadius = 1;
constexpr int WintersEdgeShardPercent = 15;
/** @brief Whether Blades of Ice's next stab is the third, which freezes. */
[[nodiscard]] bool BladesOfIceNextFreezes(const Player &player);
/** @brief The element Phoenix Strike's next strike adds, or none if it isn't the third: Fire, Cold or Lightning. */
[[nodiscard]] std::optional<DamageType> PhoenixNextElement(const Player &player);

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
/** @brief A table skill's cast animation began: Falling Sword's hero fades out in light, untargetable (Q37). */
void BeginTableSkillCast(Player &player, const TableSkillData &data);
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
/** @brief The rider of the table skill whose hit is being dealt now (SupportHitSkill): a granted skill's, or Bone Spear's; else None. */
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
/** @brief The flash a table skill shows just in front of the hero as it goes off: only a lightning skill has one. */
[[nodiscard]] std::optional<MissileGraphicID> TableSkillCastFlash(const TableSkillData &data);
/** @brief Whether a projectile's landing burst is a small one (Fire Arrow's): a little burst of its element, not an explosion. */
[[nodiscard]] bool TableLandingBurstIsSmall(const TableSkillData &data);
/** @brief The size of a small landing burst's particles (1 is a normal hit). */
constexpr float SmallLandingBurstSize = 0.45F;
/**
 * @brief How a table skill's effects look: its damage type's element, but a physical spell is bone, blood for one whose
 * hits bleed blood (Reap, Exsanguinate, Blood Nova) and holy for Blessed Hammer.
 */
[[nodiscard]] vfx::Element TableSkillLook(const TableSkillData &data);

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
 * how much softer it hits for Weaken, how much slower it acts for Decrepify, how much more damage it takes for Death
 * Mark, how much of its hit Iron Maiden turns back. 0 when the curse isn't on it or has no strength to show.
 */
[[nodiscard]] int TableCurseShownPercent(const Monster &monster, TableCurse curse);
/**
 * @brief Death Mark (Q38): when the monster is marked, the mark is used up and the hit deals DeathMarkConsumePercent;
 * returns the percent of the hit (100 when there was no mark). For Shadow Lunge and Cobra Strike.
 */
int ConsumeDeathMark(Monster &monster);
/** @brief Wave of Light's bell stands on the tile (Q38). */
[[nodiscard]] std::optional<Point> WaveBellTile();
/** @brief The hero struck the tile: if the bell stands there it tolls, hitting around it (Q38). */
void StrikeWaveBell(const Player &player, Point tile);
/** @brief Whether a Shield Wall segment stands on the tile: monsters can't step there until they break it (Q37). */
[[nodiscard]] bool IsShieldWallTile(Point tile);
[[nodiscard]] size_t ShieldWallSegmentCount();
/**
 * @brief A slam, Shield Charge or a war cry went off: the Shield Wall segments within ShieldWallShatterTiles of the
 * point or the hero shatter, each exploding for a weapon hit in radius 1. Returns how many shattered.
 */
int ShatterShieldWall(Player &player, Point at);
/** @brief How much Decrepify slows the monster, by its rarity (Q36). */
[[nodiscard]] int DecrepifySlowPercentFor(const Monster &monster);
/**
 * @brief Whether a monster near an Attract-cursed one attacks it instead of the hero (Q36): a normal or magic monster
 * always, a rare half the time (fixed for the whole curse by its roll), a unique or boss never.
 */
[[nodiscard]] bool MonsterObeysAttract(const Monster &monster, const Monster &cursed);
/** @brief Confuse's Berserk (Q36): the monster turns on its kind and hits harder for the curse's length. */
void GiveConfuseBerserk(Monster &monster, int ticks);
/** @brief A monster died: Haunt and Locust Swarm move on to the next. */
void OnTableCursedMonsterKilled(const Monster &monster);
/** @brief More damage from Soul Harvest's souls, in percent. */
int SoulHarvestDamagePercent(const Player &player);

} // namespace devilution
