/**
 * @file poe2/rune_fusing.hpp
 *
 * POE2 Mashup: Cain fuses runes (build spec step 5). Three Lesser runes make one plain rune, three plain make one
 * Greater, and five Greater and gold make one Perfect, once Diablo, the Act 3 boss, is dead. The runes come from the
 * hero's bag and then the stash; the fusing goes through a command (CMD_POE2_FUSERUNES) with the new rune's seed.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "poe2/crafting_items.hpp"

namespace devilution {

struct Player;

/** @brief Runes a fusing takes for a plain or Greater rune. */
constexpr int RunesPerFusion = 3;
/** @brief Greater runes a Perfect rune takes. */
constexpr int RunesPerPerfectFusion = 5;
/** @brief The gold a Perfect rune also costs. */
constexpr int PerfectFusionGold = 5000;

struct RuneFusion {
	RuneFamily family = RuneFamily::Count;
	/** The strength of the runes it takes. */
	RuneStrength from = RuneStrength::Lesser;
	AugmentId input = AugmentId::None;
	AugmentId output = AugmentId::None;
	int count = 0;
	int gold = 0;
	/** It waits for Diablo's death (a Perfect rune). */
	bool needsActThreeBoss = false;
};

/** @brief The fusing of the family's runes of the strength into the next; none where either strength doesn't exist. */
[[nodiscard]] std::optional<RuneFusion> GetRuneFusion(RuneFamily family, RuneStrength from);

/** @brief The runes of the id the hero has in the bag, and in the stash when it is the local hero's. */
[[nodiscard]] int OwnedRuneCount(const Player &player, AugmentId id);

/** @brief The fusings the hero has the runes for, in the Runes tab's order. */
[[nodiscard]] std::vector<RuneFusion> RuneFusionsToOffer(const Player &player);

/** @brief The fusing's line in Cain's list: "3 Lesser Ral Runes into a Ral Rune". */
[[nodiscard]] std::string RuneFusionText(const RuneFusion &fusion);

/** @brief Why the hero can't make the fusing now; empty when they can. */
[[nodiscard]] std::string_view RuneFusionRefusal(const Player &player, const RuneFusion &fusion);

/**
 * @brief Takes the runes (and gold) and gives the new rune, made with the seed; plays the stone sound and Cain's line.
 * @return Whether it fused.
 */
bool FuseRunes(Player &player, RuneFamily family, RuneStrength from, uint32_t seed);

/** @brief Cain's line after a fusing, picked by the seed. */
[[nodiscard]] std::string_view CainFusingLine(uint32_t seed);

} // namespace devilution
