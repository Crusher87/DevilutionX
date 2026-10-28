/**
 * @file poe2/weapon_kinds.hpp
 *
 * POE2 Mashup weapon kinds: what a weapon base is for the caster weapons rework. The caster Staff, Wand and Sceptre
 * bases grant a skill and roll spell modifiers; the Quarterstaff is a martial staff; the Focused and Quick staves and
 * sceptres from before the rework, and Diablo's staves, keep working as they did (the legacy kinds).
 *
 * Outside POE2 Mashup every helper gives the legacy answer: a staff is a LegacyStaff, a sceptre a LegacySceptre, and
 * nothing is a new caster weapon.
 */
#pragma once

#include <cstdint>

namespace devilution {

struct Item;
struct BaseMeta;

enum class WeaponKind : uint8_t {
	/** Not a weapon. */
	None,
	/** Any other weapon: swords, axes, maces, bows, daggers. */
	Martial,
	/** A caster staff of the rework: two-handed, grants a spell, rolls staff spell modifiers. */
	CasterStaff,
	/** A martial staff, POE2's quarterstaff. */
	Quarterstaff,
	/** A wand: one-handed, grants a bolt. */
	Wand,
	/** A sceptre of the rework: one-handed, no attack, +100 Spirit and a granted Spirit skill. */
	Sceptre,
	/** A staff from before the rework (the Focused and Quick staves) or Diablo's. */
	LegacyStaff,
	/** A sceptre from before the rework (the Focused and Quick sceptres). */
	LegacySceptre,
};

/** @brief Works out every row's kind from the item data and the base metadata; clears them outside POE2 mode. */
void LoadWeaponKinds();

/** @brief The kind of a row of the item data. */
[[nodiscard]] WeaponKind WeaponKindOf(int itemIndex);
[[nodiscard]] WeaponKind WeaponKindOf(const Item &item);

/** @brief A weapon of the rework that grants a skill and rolls spell modifiers: a caster staff, wand or new sceptre. */
[[nodiscard]] bool IsCasterWeapon(const Item &item);
[[nodiscard]] bool IsCasterWeapon(int itemIndex);
[[nodiscard]] bool IsWand(const Item &item);
[[nodiscard]] bool IsNewSceptre(const Item &item);
/** @brief A sceptre, new or legacy: it grants Spirit. */
[[nodiscard]] bool IsAnySceptre(const Item &item);
[[nodiscard]] bool IsCasterStaff(const Item &item);
[[nodiscard]] bool IsQuarterstaff(const Item &item);
/** @brief A staff or sceptre from before the rework. */
[[nodiscard]] bool IsLegacyCaster(const Item &item);
/** @brief A staff that fights: a quarterstaff or a legacy staff. */
[[nodiscard]] bool IsMartialStaff(const Item &item);

/** @brief The base metadata of a caster weapon that grants a skill, or nullptr. */
[[nodiscard]] const BaseMeta *GrantingBase(const Item &item);

} // namespace devilution
