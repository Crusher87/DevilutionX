/**
 * @file poe2/economy_state.hpp
 *
 * POE2 Mashup economy state kept with the hero: currency shards from disenchanting and salvaging, and the
 * gambler's purchase charges.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace devilution {

/** @brief Shards that add up to a full currency item (bundle salvage_and_disenchant.json). */
enum class ShardKind : uint8_t {
	/** From magic items; ten make an Orb of Transmutation. */
	Transmutation,
	/** From rare items; ten make a Regal Orb. */
	Regal,
	/** From unique items; ten make an Orb of Chance. */
	Chance,
	/** From salvaged sockets; ten make an Artificer's Orb. */
	Artificer,
	Count,
};
constexpr size_t ShardKindCount = static_cast<size_t>(ShardKind::Count);

/** @brief Shards that make one full currency item. */
constexpr int ShardsPerOrb = 10;
/** @brief The gambler's purchase charges in the campaign: what a hero starts with, and the most they can store. */
constexpr uint8_t GambleStartCharges = 8;
constexpr uint8_t MaxGambleCharges = 100;

struct EconomyState {
	/**
	 * @brief Shards counted before shards were items. POE2 Mashup: they become shard items in the bag when the hero
	 * loads, so this stays 0 afterwards (Curtis 2026-10-29).
	 */
	std::array<uint8_t, ShardKindCount> shards {};
	uint8_t gambleCharges = GambleStartCharges;
	/** @brief POE2 Mashup: whether a support gem has dropped for the hero yet (Curtis 2026-10-29: none in two floors). */
	bool supportGemDropped = false;
	/** @brief Kills on dungeon levels 1 and 2 without the first support gem; its chance climbs with each. */
	uint8_t supportDroughtKills = 0;
};

/** @brief Bytes the shards and the gambler's charges take in the save (version 17). */
constexpr size_t EconomySaveSize = ShardKindCount + 1;
/** @brief Bytes the first support gem's flag and drought take in the save (version 26). */
constexpr size_t SupportDropSaveSize = 2;

} // namespace devilution
