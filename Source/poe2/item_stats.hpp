/**
 * @file poe2/item_stats.hpp
 *
 * POE2 Mashup item stats that Diablo's item fields have no room for.
 */
#pragma once

#include <cstdint>

namespace devilution {

/** @brief Item stats Diablo's item fields have no room for; the player adds them up. */
enum class Poe2ItemStat : uint8_t {
	/** Increased attack speed in percent. */
	AttackSpeed,
	/** Increased cast speed in percent. */
	CastSpeed,
	Spirit,
	/** Increased minion damage in percent. */
	MinionDamage,
	MeleeSkillLevels,
	ProjectileSkillLevels,
	SpellSkillLevels,
	FireSkillLevels,
	LightningSkillLevels,
	/** Increased maximum life and mana in percent (jewels). */
	LifePercent,
	ManaPercent,
	/** Increased armour, evasion and energy shield from all gear in percent (jewels). */
	ArmourPercent,
	EvasionPercent,
	EnergyShieldPercent,
	/** The points of the item's defence its flat armour, evasion and energy shield modifiers gave, kept to their own kind. */
	FlatArmour,
	FlatEvasion,
	FlatEnergyShield,
	/** Increased rarity of items found in percent. */
	ItemRarity,
	/** Increased spell damage in percent; every spell hit takes it with the tree's (SpellIncreasedDamage). */
	SpellDamage,
	// The caster weapons rework's stats (ModEffect has their meanings).
	/** A sceptre's increased Spirit, of the sceptre's own; MaxSpirit reads it on each hand's item. */
	LocalSpiritPercent,
	FireSpellDamage,
	ColdSpellDamage,
	LightningSpellDamage,
	PhysicalSpellDamage,
	ColdSkillLevels,
	PhysicalSkillLevels,
	MinionSkillLevels,
	MinionLife,
	MinionResistances,
	LifeOnKill,
	ManaOnKill,
	BlockChance,
	DazeChance,
	/** Flat damage added to minions' blows. */
	MinionAddedDamageMin,
	MinionAddedDamageMax,
	/** Tenths of a point of life a second. */
	MinionLifeRegen,
	/** Added to chaos resistance and to its maximum, in percent. */
	ChaosResistance,
	MaxChaosResistance,
	ChaosSpellDamage,
	ChaosSkillLevels,
	// The effect primitives of build spec step 4, for runes, soulstones, relics and essences (ModEffect has their meanings).
	LifeLeech,
	ManaLeech,
	/** Physical damage dealt back to a monster whose melee hit lands. */
	ThornsMin,
	ThornsMax,
	LifePercentOnKill,
	ManaPercentOnKill,
	FireAsExtra,
	LightningAsExtra,
	ColdAsExtra,
	/** Cold damage added to melee hits; it meets magic resistance. */
	AddedColdMin,
	AddedColdMax,
	FlaskRecovery,
	StunBuildup,
	StunThreshold,
	EnergyShieldAsStunThreshold,
	ArmourToFire,
	ArmourToLightning,
	ArmourToCold,
	LifeRecoup,
	EnergyShieldRecharge,
	StrikeSkillLevels,
	SlamSkillLevels,
	WarcrySkillLevels,
	CurseSkillLevels,
	HeraldSkillLevels,
	PlacedSkillLevels,
	BowSkillLevels,
	AreaSpellSkillLevels,
	ElementalSpellSkillLevels,
	// The runes' stats (build spec step 5; ModEffect has their meanings).
	IgniteMagnitude,
	FreezeBuildup,
	ShockMagnitude,
	/** Hundredths of a percent of maximum life a second. */
	LifeRegenRate,
	ManaCostAsLife,
	/** Tenths of a percent. */
	AttackCritChance,
	MovementSpeed,
	MaximumEnergyShield,
	// The soulstones' stats (build spec step 6; ModEffect has their meanings).
	PoisonMagnitude,
	BleedMagnitude,
	ElementalAttackDamage,
	AttackElementalPenetration,
	MaxFireResistance,
	MaxLightningResistance,
	MaxColdResistance,
	SkillDurationAndCooldown,
	/** On the item itself: points of its armour for each point of maximum mana it gives (CalcPlrItemVals reads it per item). */
	ManaPerArmour,
	ArmourFromRecentHitLoss,
	PreventedPhysicalRecoup,
	// The essences' stats (build spec step 7; ModEffect has their meanings).
	FireRecoup,
	ColdRecoup,
	LightningRecoup,
	GoldFind,
	ManaCostEfficiency,
	/** On the shield itself: CalcPlrItemVals reads it with the shield's block. */
	IncreasedBlock,
	PoisonChance,
	Count,
};

} // namespace devilution
