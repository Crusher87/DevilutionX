/**
 * @file poe2/player_data.hpp
 *
 * POE2 Mashup character state that isn't part of the original hero file.
 */
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "poe2/flasks.hpp"
#include "tables/spelldat.h"

namespace devilution {

/** @brief Skills granted by skill gems that aren't spells; most need a certain weapon type. */
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
	/** Maces, axes, swords and staves: leaps to the target and slams the ground there. */
	LeapSlam,
	/** Bows: a volley of arrows over an area. */
	RainOfArrows,
	/** Shields: charges at the target and bashes everything in front, knocking it back. */
	ShieldCharge,
	/** Any weapon: a war cry that knocks back adjacent enemies and empowers melee hits. */
	BattleCry,
	/** Bows: an arrow that bursts into lightning, hitting everything around the target. */
	LightningArrow,
	/** Bows: fires three arrows and leaps back from the target. */
	EscapeShot,
	/** No weapon needed: a fan of five firebolts. */
	EmberFusillade,
	/** No weapon needed: a spray of erratic lightning sparks. */
	Spark,
	Last = Spark,
};

enum class SupportGem : uint8_t {
	None,
	MultipleProjectiles,
	Empower,
	FasterCasting,
	Efficiency,
	Count,
};

/** @brief What a socketed skill gem grants. */
enum class SkillKind : uint8_t {
	None,
	/** A spell; the id is a SpellID. */
	Spell,
	/** A weapon attack; the id is a WeaponSkill. */
	Attack,
};

/** @brief A skill bar slot: the skill of the gem socketed in it. */
struct SkillSlot {
	SkillKind kind = SkillKind::None;
	uint8_t id = 0;

	[[nodiscard]] bool isEmpty() const { return kind == SkillKind::None; }
	[[nodiscard]] SpellID spell() const { return kind == SkillKind::Spell ? static_cast<SpellID>(id) : SpellID::Invalid; }
	[[nodiscard]] WeaponSkill attack() const { return kind == SkillKind::Attack ? static_cast<WeaponSkill>(id) : WeaponSkill::None; }

	static SkillSlot ForSpell(SpellID spell) { return { SkillKind::Spell, static_cast<uint8_t>(spell) }; }
	static SkillSlot ForAttack(WeaponSkill skill) { return { SkillKind::Attack, static_cast<uint8_t>(skill) }; }

	bool operator==(const SkillSlot &other) const = default;
};

constexpr size_t NumSkillSlots = 6;

/** @brief Spell level granted by a socketed spell gem: it grows with the character. */
constexpr int GemSpellLevel(int characterLevel)
{
	const int level = ((std::max(characterLevel, 1) - 1) / 4) + 1;
	return std::min(level, 15);
}

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
	/** @brief Skill gems socketed in the skill bar. */
	std::array<SkillSlot, NumSkillSlots> skillSlots {};
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
	/** @brief Skill that hits when the current dash lands, e.g. Leap Slam (not saved). */
	WeaponSkill dashSkill = WeaponSkill::None;
	/** @brief Game ticks the dash skill may take to land before it's dropped; 0 if not dashing (not saved). */
	uint8_t dashSkillTicks = 0;
	/** @brief Game ticks left of Battle Cry's empowered melee hits (not saved). */
	uint8_t warcryTicks = 0;
	/** @brief Life regenerated per second from gear (not saved; from CalcPlrItemVals). */
	int lifeRegen = 0;
	/** @brief Increased mana regeneration in percent from gear (not saved; from CalcPlrItemVals). */
	int manaRegenBonus = 0;
	/** @brief Fractions of a raw life and mana point carried to the next tick (not saved). */
	int lifeRegenRemainder = 0;
	int manaRegenRemainder = 0;

	void clear()
	{
		for (auto &supports : spellSupports)
			supports.fill(SupportGem::None);
		flaskCharges.fill(MaxFlaskCharges);
		skillSlots.fill({});
		dodgeTicks = 0;
		dodgeCooldown = 0;
		pendingSkill = WeaponSkill::None;
		pendingSkillTicks = 0;
		activeSkill = WeaponSkill::None;
		dashSkill = WeaponSkill::None;
		dashSkillTicks = 0;
		warcryTicks = 0;
		lifeRegen = 0;
		manaRegenBonus = 0;
		lifeRegenRemainder = 0;
		manaRegenRemainder = 0;
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
		if (warcryTicks > 0)
			warcryTicks--;
	}

	[[nodiscard]] const std::array<SupportGem, MaxSupportsPerSpell> &supportsFor(SpellID spell) const;
	[[nodiscard]] bool hasSupport(SpellID spell, SupportGem gem) const;

	/** @brief Whether a gem for the spell is socketed in the skill bar. */
	[[nodiscard]] bool hasSpellGem(SpellID spell) const
	{
		for (const SkillSlot &slot : skillSlots) {
			if (slot.spell() == spell)
				return true;
		}
		return false;
	}
};

} // namespace devilution
