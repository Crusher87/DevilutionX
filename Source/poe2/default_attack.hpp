/**
 * @file poe2/default_attack.hpp
 *
 * POE2 Mashup default attacks: every weapon gives a plain attack that costs no mana, so a hero is never stuck when
 * the mana runs dry. Bows shoot, melee weapons strike, and a staff (outside a Monk's hands) or a sceptre fires a bolt
 * scaled to its damage: POE2's caster weapons don't melee.
 */
#pragma once

#include <cstdint>
#include <string_view>
#include <utility>

#include "poe2/player_data.hpp"

namespace devilution {

struct Player;

enum class DefaultAttack : uint8_t {
	Punch,
	SwordStrike,
	DaggerStab,
	AxeChop,
	MaceSmash,
	StaffStrike,
	StaffBolt,
	BowShot,
	ShieldBash,
};

/**
 * @brief The default attacks hit for this percent of the weapon's damage: weaker than skills cut from gems, since they
 * cost no mana (Curtis, 2026-10-16).
 */
constexpr int DefaultAttackDamagePercent = 85;
/**
 * @brief The staff bolt's damage is this percent of the staff's attack damage, plus a part of the hero's Magic. 80 since
 * it became a 0.9-second cast (was 70 when a held bolt took 0.6 s): the Sorcerer's free bolt and Firebolt together kill
 * as much slower than before as the melee classes do (research/attack-speed/rebalance).
 */
constexpr int StaffBoltDamagePercent = 80;
constexpr int StaffBoltMagicPercent = 10;

DefaultAttack DefaultAttackFor(const Player &player);
/** @brief The default attack of a weapon set (0 or 1), whether or not it's the one out. */
DefaultAttack DefaultAttackForSet(const Player &player, uint8_t set);
/** @brief The hero's default attack as a skill, for its supports; empty outside POE2 Mashup. */
SkillSlot DefaultSkillSlot(const Player &player);
std::string_view DefaultAttackName(DefaultAttack attack);
bool IsRangedDefaultAttack(DefaultAttack attack);

/**
 * @brief A Monk's bare fists hit for this percent of a plain quarterstaff of the attack gem's level: a bit below
 * a real staff, which also rolls modifiers (Curtis 2026-10-19).
 */
constexpr int BareFistStaffPercent = 85;

/**
 * @brief Whether the hero fights with bare fists, whose damage comes from the attack gem's level (see BareFistDamage):
 * a Monk in POE2 Mashup with no weapon that deals damage (one the hero can't use doesn't count, as for the panel).
 */
bool FightsWithBareFists(const Player &player);
/**
 * @brief Bare fists' damage range at a gem level: BareFistStaffPercent of a plain quarterstaff at the character level
 * the gem level needs. Between the quarterstaffs that drop it goes geometrically by their drop levels.
 */
std::pair<int, int> BareFistDamage(int gemLevel);
/**
 * @brief The gem level bare fists hit at for a skill: its own gem's for an attack skill; for the default attack, which
 * has no gem, the highest of the hero's socketed attack gems; 1 without any.
 */
int BareFistGemLevel(const Player &player, SkillSlot skill);
/** @brief The weapon damage an attack of the skill rolls: bare fists' at its gem level, else the weapon's (_pIMinDam to _pIMaxDam). */
std::pair<int, int> AttackWeaponDamage(const Player &player, SkillSlot skill);

/** @brief Whether the hero's ranged attack is the staff bolt rather than an arrow: a caster's staff or a sceptre in POE2 Mashup. */
bool FiresStaffBolt(const Player &player);
/** @brief Whether the hero's attack is a ranged one, a bow's arrow or the staff bolt, so a click shoots rather than swings. */
bool AttacksAtRange(const Player &player);
/** @brief Damage of one staff bolt, rolled from the staff's damage. */
int RollStaffBoltDamage(const Player &player);

/**
 * @brief Swaps to a weapon set unless it's already out.
 * @return false if the swap has to wait (mid-swing or dodging).
 */
bool ReadyWeaponSet(Player &player, uint8_t set);

/**
 * @brief Attacks towards the cursor with a weapon set's default attack, swapping to that set first, without moving
 * unless the target is out of reach.
 * @return true if the attack was ordered.
 */
bool UseDefaultAttack(Player &player, uint8_t set);

} // namespace devilution
