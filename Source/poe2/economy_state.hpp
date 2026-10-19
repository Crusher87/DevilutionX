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
	std::array<uint8_t, ShardKindCount> shards {};
	uint8_t gambleCharges = GambleStartCharges;
};

/** @brief Bytes the economy state takes in the save. */
constexpr size_t EconomySaveSize = ShardKindCount + 1;

} // namespace devilution
