/**
 * @file poe2/support_gems.hpp
 *
 * POE2 Mashup support gems: items that link to a spell and change how it behaves.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "poe2/player_data.hpp"
#include "tables/spelldat.h"

namespace devilution {

struct Item;
struct Player;

enum class SupportLinkResult : uint8_t {
	Linked,
	/** Linked, and the oldest support was pushed out to make room. */
	Replaced,
	NoReadiedSpell,
	NotApplicable,
	AlreadyLinked,
};

/** @brief The support gem an item represents, or SupportGem::None. */
SupportGem SupportGemForItem(const Item &item);
SupportGem SupportGemForMiscId(int miscId);
std::string_view SupportGemName(SupportGem gem);
std::string_view SupportGemDescription(SupportGem gem);

/** @brief Makes a fresh support gem item, e.g. when a linked gem is swapped back out. */
bool CreateSupportGemItem(Item &item, SupportGem gem);

/** @brief Whether the gem does anything for the given spell. */
bool SupportGemCanSupport(SupportGem gem, SpellID spell);

/**
 * @brief Links a support gem to a spell.
 * @param replaced Set to the support that was pushed out when both slots were full.
 */
SupportLinkResult LinkSupportGem(Poe2PlayerData &data, SpellID spell, SupportGem gem, SupportGem &replaced);

/** @brief Extra spell levels the spell's supports add. */
int SupportSpellLevelBonus(const Player &player, SpellID spell);
/** @brief Mana cost multiplier in percent from the spell's supports. */
int SupportManaCostPercent(const Player &player, SpellID spell);
/** @brief Animation frames skipped when casting the spell. */
int8_t SupportCastSkippedFrames(const Player &player, SpellID spell);
/** @brief Additional projectiles fired with the spell. */
int SupportExtraProjectiles(const Player &player, SpellID spell);

} // namespace devilution
