/**
 * @file poe2/monster_resistance.hpp
 *
 * POE2 Mashup: monster resistances as percentages, like POE2's (Curtis 2026-10-18). No monster is immune: Diablo's
 * resist and immune marks become percentages, curses and exposure lower them, and penetration gets past them.
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
constexpr int MagicResistMarkPercent = 20;
constexpr int MagicImmuneMarkPercent = 40;
/** The magic and rare monster modifier "Elemental Resistance": to fire, lightning and magic. */
constexpr int ResistantModPercent = 25;
constexpr int MaxMonsterResistance = 75;
constexpr int MinMonsterResistance = -100;
/** Lower Resist: to fire, lightning and magic. */
constexpr int LowerResistCursePercent = 30;
/** A monster this resistant to a hit doesn't flinch from it. */
constexpr int FlinchProofResistance = 60;

/** @brief A resistance from Diablo's resist and immune marks (monster_resistance bits), before modifiers. */
[[nodiscard]] int ResistanceMarkPercent(uint16_t marks, DamageType type);
/** @brief "Resistances: Fire 30%, Magic 40%", or "No resistances", from the marks. */
[[nodiscard]] std::string ResistanceMarksText(uint16_t marks);
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
