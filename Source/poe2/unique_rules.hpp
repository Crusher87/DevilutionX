/**
 * @file poe2/unique_rules.hpp
 *
 * POE2 Mashup: the rules the bundle's uniques add while worn. Their values add up into the player's
 * totals (not saved; from CalcPlrItemVals) and combat, defences and recovery read them there.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "tables/spelldat.h"

namespace devilution {

/** @brief A unique's rule; the value is its magnitude, in percent unless noted. */
enum class UniqueRule : uint8_t {
	None,
	/** Only shows its line. */
	Text,
	SpellDamage,
	/** Increased attack and cast speed. */
	ActionSpeed,
	/** Percent of physical attack damage converted to fire. */
	PhysicalToFire,
	IgniteBuildup,
	NoColdDamage,
	/** 1% increased attack speed per 4% life missing, up to the value. */
	MissingLifeAttackSpeed,
	LowLifeUnevadable,
	LessLifeRecovery,
	/** Critical hits inflict bleeding as though dealing this much more physical damage. */
	CritBleed,
	CritDamageBonus,
	NonCritLess,
	/** Heavy hits grant a Guard stack for 4 seconds, up to the value. */
	GuardOnHeavyHit,
	GuardStackDamage,
	GuardStackSlow,
	/** Every 3rd attack deals this much more elemental damage, all of it one random element. */
	ThirdAttackElemental,
	ThirdAttackExposure,
	OtherAttacksLessPhysical,
	/** Every 5th attack repeats once at this much of its damage. */
	FifthAttackRepeat,
	/** Increased spell damage equal to this percent of maximum mana. */
	ManaSpellDamage,
	ManaCost,
	/** After standing still for a second, every Nth spell repeats once. */
	StillSpellRepeat,
	MovingCastSlow,
	/** Spells cycle fire, cold and lightning; the current element deals this much more. */
	SpellElementCycle,
	OffElementLess,
	/** Spells spend energy shield before mana; up to this much more spell damage from it. */
	SpellsSpendEnergyShield,
	LessEnergyShieldRecharge,
	/** Seconds an element is remembered after inflicting its ailment. */
	AilmentMemory,
	MemoryPenetration,
	/** Spell kills store echoes; at this many the next spell repeats twice. */
	SpellEchoes,
	/** Seconds between echoes from hitting rare and unique monsters. */
	BossEchoes,
	RepeatedSpellLess,
	ArmourVsElements,
	NoEvasion,
	EvasionVsSpells,
	LessArmour,
	LifeRecoveryToShield,
	NoEnergyShieldRecharge,
	/** 1% less damage taken from hits per 10% resistance above the cap, up to the value. */
	OvercapResistGuard,
	MaxElementalResistance,
	StationaryDefences,
	MovingVulnerability,
	/** Losing 30% of maximum life within 2 seconds recovers this percent of it. */
	BigHitRecovery,
	LessLifeFlaskRecovery,
	/** 1% increased attack damage per this much fire resistance above the cap. */
	FireOvercapDamage,
	MaxMagicResistance,
	ManaToEnergyShield,
	LessMaxLife,
	CritActionSpeed,
	ResistanceShare,
	/** Seconds a defensive modifier stolen from a rare monster lasts. */
	StealRareMod,
	MirrorRing,
	/** 1% increased damage per 50 of the highest attribute, up to the value. */
	HighestAttributeDamage,
	/** Increased damage per 10 unreserved Spirit. */
	SpiritDamage,
	MoreSpiritReservation,
	LowLifeSkillLevels,
	StunThreshold,
	FirstKeystone,
	OtherKeystones,
	AllSkillLevels,
	/** Ignites spread to enemies within this many tiles halfway through. */
	IgniteSpread,
	IgniteSpreadSelfDamage,
	MinionFrenzy,
	MinionResummon,
	LessMinionLife,
	/** 1% more stun buildup per this much armour. */
	ArmourStunBuildup,
	/** Shocking a shocked enemy builds up to an Electrocution. */
	Electrocute,
	ElectrocutedLightning,
	FreezeStoresChill,
	CannotIgnite,
	LuckyCrits,
	BleedHinder,
	DistanceRule,
	/** Every 6th spell repeats three times at this much of its damage. */
	SixthSpellRepeat,
	RepeatManaDrain,
	ArmourPhysicalReduction,
	HeavyArmourSlow,
	ResourceSpentDamage,
	ResourceLock,
	/** More damage against rare and unique monsters (Hunter's Talisman, Predator's Mark). */
	DamageVsRareUnique,
	LessDamageVsNormal,
	/** Less damage taken from hits (Guardian's Talisman). */
	LessHitDamage,
	// Desecrated modifiers (poe2/desecration.hpp).
	/** 1% more damage per this much percent of life missing. */
	MissingLifeDamage,
	/** Percent of hit damage taken from mana before life. */
	HitDamageFromMana,
	LessManaRecovery,
	/** More critical hit chance on every third attack, and less on the others. */
	ThirdHitCritChance,
	OtherHitsLessCrit,
	/** More damage against bleeding monsters while moving, and less while standing still. */
	BleedMovingDamage,
	BleedStationaryLess,
	/** Minions deal more damage while below half life. */
	MinionLowLifeDamage,
	FullLifeLessHitDamage,
	// Passive tree capstones, crossovers and Profane notables.
	/** Kills with a two-handed melee weapon grant a Frenzy Charge. */
	TwoHandKillFrenzy,
	/** After a block, the next melee hit within 3 seconds deals this much more. */
	BlockRiposte,
	/** A spell's hits deal this much more when the spell before it hit with another element within 3 seconds. */
	ElementRotation,
	/** Minions get this percent of the hero's increased spell damage, up to 40%. */
	MinionSpellDamage,
	/** Percent of a staff attack's physical damage converted to lightning (after fire). */
	StaffToLightning,
	/** A staff hit on a shocked enemy arcs to one other enemy nearby for this much of its damage. */
	StaffArc,
	/** Seconds an unarmed hit's stack lasts, up to 10 stacks. */
	UnarmedHitStacks,
	/** Enemies a hero's arrow pierces. */
	ArrowPierce,
	/** After swapping to another weapon type, the next 3 hits within 4 seconds deal this much more. */
	SwapStrike,
	/** Percent of the character damage bonus a non-Rogue's arrows add (50 without it). */
	ArrowDamageMod,
	/** Percent of increased spell damage that also applies to an attack's fire damage. */
	SpellDamageToFireAttacks,
	StaffAppliesToBow,
	/** Hits kill normal and magic monsters left at this percent of life or less. */
	Cull,
	/** The first hit on an enemy at full life deals this much more. */
	FirstStrike,
	/** A melee kill bursts for this percent of the monster's maximum life to the enemies beside it. */
	KillBurst,
	/** Drinking a flask grants a Frenzy Charge. */
	FlaskFrenzy,
	/** More damage against heavy stunned enemies. */
	MoreVsStunned,
	Count,
};

using UniqueRuleTotals = std::array<int16_t, static_cast<size_t>(UniqueRule::Count)>;

/** @brief What the unique rules keep track of while playing: counters, stacks and timers (not saved). */
struct UniqueRuleState {
	/** @brief Attacks and spells used, for "every Nth" rules. */
	uint16_t attacks = 0;
	uint16_t spells = 0;
	uint8_t guardStacks = 0;
	uint8_t guardTicks = 0;
	/** @brief The element spells cycle to next: 0 fire, 1 cold, 2 lightning. */
	uint8_t cycleElement = 0;
	/** @brief The element of the last ailment inflicted (a DamageType), while memoryTicks lasts. */
	uint8_t memoryElement = 0;
	uint8_t memoryTicks = 0;
	uint8_t echoes = 0;
	uint8_t bossEchoTicks = 0;
	/** @brief Game ticks standing still, and since the last step (both capped). */
	uint16_t stillTicks = 0;
	uint16_t sinceMovedTicks = 0;
	/** @brief Slowed walk ticks owed, in percent of a tick. */
	uint8_t walkCarry = 0;
	/** @brief Life lost in the last 2 seconds, in 5-tick buckets, as raw values. */
	std::array<int32_t, 8> lifeLost {};
	uint8_t lifeLostTick = 0;
	uint8_t bigHitCooldown = 0;
	uint8_t critSpeedTicks = 0;
	/** @brief A defensive modifier taken from a rare monster (MonsterMod + 1; 0 for none) and its game ticks left. */
	uint8_t stolenMod = 0;
	uint16_t stolenTicks = 0;
	uint8_t minionFrenzy = 0;
	uint8_t minionFrenzyTicks = 0;
	/** @brief Resources spent recently and energy shield spent on spells recently, as raw values that fade over 4 seconds. */
	int32_t resourcesSpent = 0;
	int32_t shieldSpent = 0;
	uint8_t recoveryLockTicks = 0;
	/** @brief Chill stored by freezing an enemy, used up by the next hit, in percent of more damage. */
	uint8_t storedChill = 0;
	/** @brief Tiles to the nearest enemy; 255 when none is near. */
	uint8_t nearestEnemy = 255;
	/** @brief Whether the last hit on a monster came from a spell, for kills. */
	bool lastHitBySpell = false;
	/** @brief A spell to cast again: how many more times, ticks until the next, at what damage and where. */
	SpellID repeatSpell = SpellID::Invalid;
	uint8_t repeatsLeft = 0;
	uint8_t repeatTicks = 0;
	uint8_t repeatLevel = 0;
	int16_t repeatPercent = 100;
	int16_t repeatX = 0;
	int16_t repeatY = 0;
	/** @brief Damage percent of the spell being cast now; below 100 while repeating. */
	int16_t castDamagePercent = 100;
	bool repeatManaDrain = false;
	/** @brief A repeat is being cast: it costs nothing and doesn't count as a new spell. */
	bool castingRepeat = false;
	/** @brief On low life (35% or less), updated every game tick. */
	bool lowLife = false;
	/** @brief Whether the last hit on a monster was an arrow, for kills. */
	bool lastHitRanged = false;
	/** @brief Lachdanan's Guard: game ticks the counter after a block stays ready. */
	uint8_t blockRiposteTicks = 0;
	/** @brief Wheel of the Elements: the element (DamageType + 1, 0 for none) of the last spell that hit, its ticks left,
	 * the spell count it belongs to, and whether the current spell's hits get the bonus. */
	uint8_t rotationElement = 0;
	uint8_t rotationTicks = 0;
	uint16_t rotationSpell = 0;
	bool rotationBonus = false;
	/** @brief Ceaseless Hands: stacks from unarmed hits, and game ticks until they all fall off. */
	uint8_t unarmedStacks = 0;
	uint8_t unarmedTicks = 0;
	/** @brief The Sickness Road toll's fever: game ticks in which the hero's own hits and spells inflict no ailment. */
	uint8_t feverTicks = 0;
	/** @brief Knife in the Dark: empowered hits left after a swap, their game ticks left, and ticks until it can trigger again. */
	uint8_t swapStrikes = 0;
	uint8_t swapStrikeTicks = 0;
	uint8_t swapStrikeCooldown = 0;

	// Unique flasks and charms (poe2/belt_uniques.hpp).
	uint16_t heartglassCooldown = 0;
	uint16_t saintsTokenCooldown = 0;
	uint8_t witchglassTicks = 0;
	/** @brief Little Guardian's lingering resistance (fire, lightning, magic), and whether it gave the current charm effect. */
	std::array<uint8_t, 3> guardianTicks {};
	std::array<bool, 3> guardianArmed {};
	uint8_t scholarPercent = 0;
	uint8_t scholarTicks = 0;
	/** @brief Deep Well: the next skill costs no mana. */
	bool freeSkill = false;
	uint8_t moonwaterTicks = 0;
	/** @brief The Second Bell's activations, every third free. */
	uint8_t bellActivations = 0;
};

} // namespace devilution
