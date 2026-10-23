/**
 * @file poe2/support_tiers.hpp
 *
 * POE2 Mashup support gem tiers: like POE2's Brutality I, II and III, most supports come in up to three tiers,
 * each cut from a higher uncut support gem and with its own numbers. The numbers follow POE2 0.5.4's support tiers,
 * matched to our supports by what they do.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "poe2/player_data.hpp"

namespace devilution {

/** @brief Highest uncut support gem level, like POE2's Uncut Support Gem levels 1 to 5. */
constexpr int MaxUncutSupportLevel = 5;

/** @brief One tier of a support: what it is cut from, what it costs, its numbers and its description. */
struct SupportTierData {
	SupportGem gem;
	uint8_t tier;
	/** POE2's uncut support gem level (1 to 5) the tier is cut from. */
	uint8_t uncut;
	/** Mana cost multiplier in percent. */
	uint16_t mana;
	/** The support's numbers at this tier; what each means is up to the support's code. */
	std::array<int16_t, 3> value;
	std::string_view description;
};

std::span<const SupportTierData> SupportTierTable();
/** @brief The data of a tier of a support, or nullptr if the support has no such tier in the table. */
const SupportTierData *FindSupportTier(SupportGem gem, int tier);
/** @brief The support's highest tier; 1 for supports with only one. */
int SupportTierCount(SupportGem gem);
/** @brief A number of the support at a tier (clamped to its tiers); 0 if the table doesn't have it. */
int SupportTierValue(SupportGem gem, int tier, size_t index);
/** @brief A number of the support at the tier it has on the skill; 0 if the skill doesn't have it. */
int SupportValue(const Poe2PlayerData &data, SkillSlot skill, SupportGem gem, size_t index);

/** @brief Our gem level that stands for a POE2 uncut support gem level: 1, 3, 6, 10 or 15 (gemcutting rows I to IX). */
int UncutSupportGemLevel(int uncutLevel);
/** @brief The POE2 uncut support gem level (1 to 5) of an uncut support gem of our level. */
int UncutSupportLevelForGemLevel(int gemLevel);
/**
 * @brief POE2's uncut support gem level (1 to 5) dropping at a POE2 area level: 1 from 4, 2 from 16, 3 from 33,
 * 4 from 45 and 5 from 55; 0 below 4, where none drop.
 */
int UncutSupportLevelForArea(int areaLevel);
/** @brief The gem level a tier of the support is cut from; @param baseLevel the level of supports without tier data. */
int SupportTierGemLevel(SupportGem gem, int tier, int baseLevel);

} // namespace devilution
