/**
 * @file poe2/shrine_state.hpp
 *
 * POE2 Mashup shrines: the shrine kinds and powers, and the timed shrine buffs a hero carries (build spec step 8).
 * Kept apart from poe2/shrines.hpp so the hero's data can hold the buffs without the rest.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace devilution {

/** @brief Diablo's shrines in the order of objects.cpp's shrine_type, which a shrine object keeps in _oVar1. */
enum class ShrineKind : uint8_t {
	Mysterious,
	Hidden,
	Gloomy,
	Weird,
	Magical,
	Stone,
	Religious,
	Enchanted,
	Thaumaturgic,
	Fascinating,
	Cryptic,
	Magical2,
	Eldritch,
	Eerie,
	Divine,
	Holy,
	Sacred,
	Spiritual,
	Spooky,
	Abandoned,
	Creepy,
	Quiet,
	Secluded,
	Ornate,
	Glimmering,
	Tainted,
	Oily,
	Glowing,
	Mendicant,
	Sparkling,
	Town,
	Shimmering,
	Solar,
	Murphys,
	Count,
};

/** @brief What a shrine does in POE2 mode: the effect column of shrines.tsv. Append only. */
enum class ShrineEffect : uint8_t {
	None,
	/** Another shrine's power, picked at random. */
	Random,
	/** Another shrine's power, picked at random, and a curse on the hero. */
	RandomCursed,
	/** Diablo's Town shrine: a portal to town, unchanged. */
	TownPortal,
	Echoing,
	Gloom,
	Hexing,
	Replenishing,
	Impenetrable,
	Resistance,
	Diamond,
	Covetous,
	Meteoric,
	Tempest,
	Freezing,
	Divine,
	Acceleration,
	TempestArcs,
	Greed,
	Skeletal,
	Brutal,
	Thorned,
	Enduring,
	EnduringII,
	Seeking,
	AccelerationI,
	Tainted,
	ExtraFire,
	Enlightening,
	GreedII,
	ExtraLightning,
	ExtraCold,
	Solar,
	Count,
};

/** @brief A curse Murphy's shrine puts on the hero. Append only: saved. */
enum class ShrineCurse : uint8_t {
	None,
	/** You take 20% more damage. */
	Vulnerability,
	/** -20% to elemental resistances. */
	ElementalWeakness,
	/** 20% less skill and movement speed. */
	TemporalChains,
	Count,
};

/** @brief At most this many shrine buffs at once; another replaces the oldest. */
constexpr size_t MaxShrineBuffs = 2;

/** @brief Solar's variant: what the clock said on the hero's computer when they touched the shrine. */
constexpr uint8_t SolarDay = 0;
constexpr uint8_t SolarNight = 1;

/** @brief One shrine's power on the hero, and how long it has left. */
struct ShrineBuff {
	ShrineKind kind = ShrineKind::Count;
	/** @brief Solar's day or night. */
	uint8_t variant = 0;
	uint16_t ticksLeft = 0;
	uint16_t ticksTotal = 0;
	/** @brief Game ticks to the next strike of a storm (not saved). */
	uint16_t pulseTicks = 0;
	/** @brief Steps the storm has taken, for spirals (not saved). */
	uint8_t pulses = 0;

	[[nodiscard]] bool active() const { return ticksLeft > 0 && kind < ShrineKind::Count; }
};

/** @brief The hero's shrine buffs, oldest first, and a Murphy's curse. Saved in the "poe2shrines" entry. */
struct ShrineBuffState {
	std::array<ShrineBuff, MaxShrineBuffs> buffs {};
	ShrineCurse curse = ShrineCurse::None;
	uint16_t curseTicks = 0;
	uint16_t curseTotal = 0;
	/** @brief A buff ended this tick: its icon's fade sound plays once (not saved). */
	bool fadeSound = false;
};

} // namespace devilution
