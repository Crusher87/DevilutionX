/**
 * @file poe2/player_data.hpp
 *
 * POE2 Mashup character state that isn't part of the original hero file.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "poe2/flasks.hpp"
#include "tables/spelldat.h"

namespace devilution {

/** @brief Skills granted by the equipped weapon type, used with the weapon skill key. */
enum class WeaponSkill : uint8_t {
	None,
	/** Swords and axes: hits the target and both sides. */
	Cleave,
	/** Maces, hammers and mauls: hits everything around and knocks it back. */
	GroundSlam,
	/** Bows: fires a fan of five arrows. */
	Barrage,
	/** Staves: hits everything around. */
	WhirlingStaff,
	/** Daggers: a very fast double strike. */
	Flurry,
};

enum class SupportGem : uint8_t {
	None,
	MultipleProjectiles,
	Empower,
	FasterCasting,
	Efficiency,
	Count,
};

constexpr size_t MaxSupportsPerSpell = 2;
constexpr size_t MaxPoe2Spells = 64;
static_assert(static_cast<size_t>(SpellID::LAST) < MaxPoe2Spells);

/**
 * @brief All POE2 Mashup character state that isn't part of the original hero file.
 *
 * Saved in its own "poe2hero" entry of the hero save, so heroes stay loadable by other builds.
 */
struct Poe2PlayerData {
	std::array<std::array<SupportGem, MaxSupportsPerSpell>, MaxPoe2Spells> spellSupports {};
	/** @brief Charges of the flasks in belt slots 1 to 4. */
	std::array<uint8_t, NumFlasks> flaskCharges {};
	/** @brief Game ticks left in the current dodge roll (not saved). */
	uint8_t dodgeTicks = 0;
	/** @brief Game ticks until the next dodge roll is allowed (not saved). */
	uint8_t dodgeCooldown = 0;
	/** @brief Weapon skill queued for the next attack (not saved). */
	WeaponSkill pendingSkill = WeaponSkill::None;
	/** @brief Game ticks before a queued weapon skill is dropped (not saved). */
	uint8_t pendingSkillTicks = 0;
	/** @brief Weapon skill used by the attack in progress (not saved). */
	WeaponSkill activeSkill = WeaponSkill::None;

	void clear()
	{
		for (auto &supports : spellSupports)
			supports.fill(SupportGem::None);
		flaskCharges.fill(MaxFlaskCharges);
		dodgeTicks = 0;
		dodgeCooldown = 0;
		pendingSkill = WeaponSkill::None;
		pendingSkillTicks = 0;
		activeSkill = WeaponSkill::None;
	}

	[[nodiscard]] bool isDodging() const { return dodgeTicks > 0; }

	/** @brief Advances the dodge roll and weapon skill timers by one game tick. */
	void tick()
	{
		if (dodgeTicks > 0)
			dodgeTicks--;
		if (dodgeCooldown > 0)
			dodgeCooldown--;
		if (pendingSkillTicks > 0 && --pendingSkillTicks == 0)
			pendingSkill = WeaponSkill::None;
	}

	[[nodiscard]] const std::array<SupportGem, MaxSupportsPerSpell> &supportsFor(SpellID spell) const;
	[[nodiscard]] bool hasSupport(SpellID spell, SupportGem gem) const;
};

} // namespace devilution
