/**
 * @file poe2/monster_resistance.hpp
 *
 * POE2 Mashup: monster resistances as percentages, like POE2's (Curtis 2026-10-18). No monster is immune: Diablo's
 * resist and immune marks become percentages, curses and exposure lower them, and penetration gets past them.
 *
 * Chaos resistance comes from the monster's kind alone (monster_chaos_resistance.tsv, Curtis 2026-10-30): acid beasts,
 * spiders and other poison creatures 30%, demons 20%, Diablo 25%, everything else 0. Nothing else raises or lowers it
 * except the Despair curse: not the Elemental Resistance modifier, waystones, minion gear, Lower Resist or exposure.
 */
#pragma once

#include <cstdint>
#include <string>

namespace devilution {

struct Monster;
enum class DamageType : uint8_t;

/** Diablo's marks: fire and lightning. */
constexpr int ResistMarkPercent = 30;
constexpr int ImmuneMarkPercent = 60;
/** Magic stands in for POE2's cold, and most of Diablo's monsters resist it, so its marks are lower. */
constexpr int ColdResistMarkPercent = 20;
constexpr int ColdImmuneMarkPercent = 40;
/**
 * An undead monster's magic (cold) immune mark: every Act 1 undead has one, which put the cold skills at 0.6x on most
 * of the Cathedral (Curtis 2026-10-28: 40 to 20; audit-casters-v3.md). Demons and animals keep ColdImmuneMarkPercent.
 */
constexpr int UndeadColdImmuneMarkPercent = 20;
/** The magic and rare monster modifier "Elemental Resistance": to fire, lightning and magic. */
constexpr int ResistantModPercent = 25;
constexpr int MaxMonsterResistance = 75;
constexpr int MinMonsterResistance = -100;
/** Lower Resist: to fire, lightning and magic. */
constexpr int LowerResistCursePercent = 30;
/** Despair: to chaos (Curtis 2026-10-30; POE2's is 35% at level 1). */
constexpr int DespairCursePercent = 30;
/** A monster this resistant to a hit doesn't flinch from it. */
constexpr int FlinchProofResistance = 60;

/**
 * @brief A resistance from Diablo's resist and immune marks (monster_resistance bits), before modifiers.
 * @param undead Whether the monster is undead, whose magic immune mark is lower (UndeadColdImmuneMarkPercent).
 */
[[nodiscard]] int ResistanceMarkPercent(uint16_t marks, DamageType type, bool undead = false);
/**
 * @brief "Resistances: Fire 30%, Magic 40%, Chaos 20%", or "No resistances", from the marks (see
 * ResistanceMarkPercent) and the kind's chaos resistance.
 */
[[nodiscard]] std::string ResistanceMarksText(uint16_t marks, bool undead = false, int chaos = 0);
/** @brief Reads monster_chaos_resistance.tsv in POE2 mode; clears the table outside it. */
void LoadMonsterChaosResistances();
/** @brief A monster kind's chaos resistance (a _monster_id), 0 when the table doesn't list it or outside POE2 mode. */
[[nodiscard]] int MonsterTypeChaosResistance(int monsterType);
/** @brief How much of a curse or exposure takes hold, in percent: magic monsters shrug off 15%, rare 30%, unique 50%. */
[[nodiscard]] int CurseEffectPercent(const Monster &monster);
/** @brief The resistance from the monster's kind and modifiers and the map, before curses and exposure. */
[[nodiscard]] int MonsterBaseResistance(const Monster &monster, DamageType type);
/** @brief How much curses and exposure lower the resistance now. */
[[nodiscard]] int MonsterResistanceLowered(const Monster &monster, DamageType type);
/** @brief The resistance now, capped: below zero the monster takes more damage. 0 outside POE2 mode. */
[[nodiscard]] int MonsterResistance(const Monster &monster, DamageType type);
/** @brief The resistance a hit meets: penetration lowers a positive resistance, to 0 at most. */
[[nodiscard]] int MonsterHitResistance(const Monster &monster, DamageType type, int penetration);

} // namespace devilution
