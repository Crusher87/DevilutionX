/**
 * @file poe2/spell_damage.hpp
 *
 * POE2 Mashup: one damage rule for the hero's spells. A spell's hit is the spell base (RollSpellBase: Magic and the
 * spell level, never the character level) times the spell's percent from txtdata/poe2/spell_damage.tsv; the gem's
 * curve (Fireball's steps to 312%, gem_levels.cpp) multiplies it when the missile hits. Diablo's 12.5% a spell level
 * (ScaleSpellEffect) and its character-level terms are gone.
 *
 * The percent is the damage the spell shows, as Diablo's own damage text counts it. A few spells show several of
 * their missiles' hits as one number (Nova and Immolation 5 balls, Flame Wave 6, Chain Lightning and Elemental 2);
 * the file's hits column keeps that: each missile deals percent / hits.
 *
 * Weapon skills that fire spell missiles (Ember Fusillade's Firebolts, Spark's Charged Bolts) roll their own hit from
 * their own row and keep their own gem curve, so the spell's curve never applies to them twice.
 */
#pragma once

#include "missiles.h"
#include "poe2/player_data.hpp"
#include "tables/spelldat.h"

namespace devilution {

struct Player;

/** @brief Whether a missile is a hero's in POE2 and so takes the one spell damage rule. */
bool UsesPoe2SpellRule(Missile &missile);

/** @brief The spell's hit in percent of the spell base (100 for a spell with no row). Runes count as their spell. */
int SpellDamagePercent(SpellID spell);
/** @brief How many of the spell's missile hits its percent counts (1 for most). */
int SpellDamageHits(SpellID spell);
/** @brief The share of a Firebolt hit that splashes onto the monsters beside the one hit, in percent. */
int SpellSplashPercent(SpellID spell);
/** @brief A spell-like weapon skill's hit (Spark's spark, Ember Fusillade's ember) in percent of the spell base. */
int SkillSpellDamagePercent(WeaponSkill skill);

/** @brief Rolls one missile's hit of a hero's spell (one GenerateRnd call). */
int RollPoe2SpellHit(const Player &player, SpellID spell, int spellLevel);
/** @brief Rolls one missile's hit of a spell-like weapon skill (one GenerateRnd call). */
int RollPoe2SkillSpellHit(const Player &player, WeaponSkill skill, int gemLevel);
/** @brief The damage a spell shows, before its gem curve and supports. */
DamageRange Poe2SpellDamageRange(const Player &player, SpellID spell, int spellLevel);

/** @brief Loads txtdata/poe2/spell_damage.tsv; in Diablo mode it only forgets what was loaded. */
void LoadSpellDamage();

} // namespace devilution
