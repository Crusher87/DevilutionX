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
	Count,
};

using UniqueRuleTotals = std::array<int16_t, static_cast<size_t>(UniqueRule::Count)>;

} // namespace devilution
