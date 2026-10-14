/**
 * @file poe2/default_attack.hpp
 *
 * POE2 Mashup default attacks: every weapon gives a plain attack that costs no mana, so a hero is never stuck when
 * the mana runs dry. Bows shoot, melee weapons strike, and a staff (outside a Monk's hands) fires a bolt scaled to
 * the staff's damage.
 */
#pragma once

#include <cstdint>
#include <string_view>

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

/** @brief The staff bolt's damage is the staff's attack damage plus this percent of the hero's Magic. */
constexpr int StaffBoltMagicPercent = 20;

DefaultAttack DefaultAttackFor(const Player &player);
std::string_view DefaultAttackName(DefaultAttack attack);
/** @brief A word or two for the skill tray slot. */
std::string_view DefaultAttackLabel(DefaultAttack attack);
bool IsRangedDefaultAttack(DefaultAttack attack);

/** @brief Whether the hero's ranged attack is the staff bolt rather than an arrow. */
bool FiresStaffBolt(const Player &player);
/** @brief Damage of one staff bolt, rolled from the staff's damage. */
int RollStaffBoltDamage(const Player &player);

/**
 * @brief Attacks towards the cursor with the weapon's default attack, without moving unless the target is out of reach.
 * @return true if the attack was ordered.
 */
bool UseDefaultAttack(Player &player);

} // namespace devilution
