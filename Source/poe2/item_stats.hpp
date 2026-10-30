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
	Count,
};

} // namespace devilution
