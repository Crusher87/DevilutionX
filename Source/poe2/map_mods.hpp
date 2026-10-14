/**
 * @file poe2/map_mods.hpp
 *
 * POE2 Mashup waystone modifiers (bundle 05_Endgame_Loot_Environment): each pairs a danger with a reward on one
 * of the five reward axes, and the open map adds them all up into its effects.
 */
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "poe2/craft_types.hpp"

namespace devilution {

/** @brief The five reward axes a map's modifiers, tablets and Atlas passives raise; they never merge into one. */
enum class MapAxis : uint8_t {
	PackSize,
	MonsterRarity,
	ItemRarity,
	Effectiveness,
	WaystoneFind,
	Count,
};

/** @brief What a waystone modifier makes harder. */
enum class MapDanger : uint8_t {
	MonsterDamage,
	ExtraFire,
	ExtraCold,
	ExtraLightning,
	ExtraChaos,
	MonsterLife,
	MonsterSpeed,
	PoisonChance,
	BleedChance,
	ArmourBreak,
	Penetration,
	RareSkillSpeed,
	MonsterRegen,
	MonsterArmour,
	MonsterEvasion,
	MonsterEnergyShield,
	RareExtraMod,
	MorePacks,
	FewerFlaskCharges,
	LessRecovery,
	LessExposure,
	LessCritDamageTaken,
	MonsterResistance,
	MonsterThreshold,
	PlayerLessDamage,
	PlayerSlower,
	BurningGround,
	ChilledGround,
	ShockedGround,
	MoreSkillCost,
	WeakerAuras,
	DeathEffects,
	LessLeech,
	RareMoveSpeed,
	MoreMagicRare,
	LootVariance,
	Count,
};

struct WaystoneMod {
	std::string_view name;
	bool prefix;
	int weight;
	MapDanger danger;
	/** @brief The danger's range at low (tiers 1-5), mid (6-10) and high (11-16) tiers. */
	std::array<std::array<int16_t, 2>, 3> dangerValues;
	MapAxis axis;
	std::array<int16_t, 3> reward;
	/** @brief Tooltip text of the danger, with {} for its value. */
	std::string_view text;
};

/** @brief Waystone modifier ids are 1 to this; 0 is none. */
constexpr uint8_t WaystoneModIds = static_cast<uint8_t>(MapDanger::Count);

const WaystoneMod &GetWaystoneMod(uint8_t id);
/** @brief 0 for tiers 1-5, 1 for 6-10, 2 for 11-16. */
int WaystoneBand(int tier);
/** @brief The danger's value of a crafted waystone modifier: its band's range, picked by its salt. */
int WaystoneModValue(const Poe2CraftMod &mod);
/** @brief The reward of a crafted waystone modifier. */
int WaystoneModReward(const Poe2CraftMod &mod);
/** @brief Tooltip lines: the danger, then the reward. */
std::vector<std::string> WaystoneModText(const Poe2CraftMod &mod);
std::string_view MapAxisName(MapAxis axis);

/**
 * @brief Picks another modifier for a waystone with these, by weight; none twice, at most three a side.
 * @return 0 if none fits.
 */
uint8_t RollWaystoneMod(const std::array<Poe2CraftMod, MaxCraftMods> &existing, int perSide, uint32_t seed, int forcedSide = 0);

/** @brief Everything the open map's waystone, tablets and Atlas passives add up to. */
struct MapEffects {
	std::array<int, static_cast<size_t>(MapDanger::Count)> danger {};
	/** @brief Each axis in percent, the sources multiplied together: 120 x 115 = 138. */
	std::array<int, static_cast<size_t>(MapAxis::Count)> axis { 100, 100, 100, 100, 100 };

	[[nodiscard]] int operator[](MapDanger d) const { return danger[static_cast<size_t>(d)]; }
	[[nodiscard]] int operator[](MapAxis a) const { return axis[static_cast<size_t>(a)]; }
	/** @brief Multiplies one more independent source into an axis. */
	void multiply(MapAxis a, int percent) { axis[static_cast<size_t>(a)] = axis[static_cast<size_t>(a)] * (100 + percent) / 100; }
};

} // namespace devilution
