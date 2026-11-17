/**
 * @file poe2/affix_glow.hpp
 *
 * POE2 Mashup: when crafting adds a modifier to an item, its line in the item tooltip glows gold and slowly fades back
 * to its normal colour, so the player sees at once which modifier is new. This is UI state only: it is never saved
 * and never sent over the network.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace devilution {

struct Item;

/** @brief How long a new modifier's line glows, in milliseconds. */
constexpr uint32_t AffixGlowDurationMs = 4000;
/** @brief How long the glow stays at its brightest before it starts to fade, in milliseconds. */
constexpr uint32_t AffixGlowHoldMs = 600;

/** @brief Which of an item's tooltip lines are new. */
struct NewAffixLines {
	/** @brief Bit i: the item's _iPoe2Mods[i]. */
	uint32_t poe2Mods = 0;
	/** @brief Bit i: _iPoe2Craft.mods[i], for its desecrated and unrevealed lines. */
	uint32_t craftMods = 0;
	/** @brief Bit i: _iRarePowers[i], on an item from before the POE2 modifiers. */
	uint32_t rarePowers = 0;
	/** @brief The prefix and suffix power of a magic item from before the POE2 modifiers. */
	bool prefixPower = false;
	bool suffixPower = false;
	/** @brief Essence marks and augment socket lines, by their text. */
	std::vector<std::string> lines;

	[[nodiscard]] bool empty() const;
	[[nodiscard]] bool hasPoe2Mod(size_t index) const { return index < 32 && (poe2Mods & (1U << index)) != 0; }
	[[nodiscard]] bool hasCraftMod(size_t index) const { return index < 32 && (craftMods & (1U << index)) != 0; }
	[[nodiscard]] bool hasRarePower(size_t index) const { return index < 32 && (rarePowers & (1U << index)) != 0; }
	[[nodiscard]] bool hasLine(std::string_view text) const;
};

/**
 * @brief The lines of @p after that @p before didn't have: each modifier is matched with one just like it (kind, tier
 * and values), and those left over are new. A Chaos Orb's replacement modifier is new; the ones it kept aren't.
 */
NewAffixLines FindNewAffixLines(const Item &before, const Item &after);

/**
 * @brief Makes the lines crafting added to @p after glow from @p now (SDL_GetTicks). Nothing new keeps the glow there
 * is. @return Whether any line was new.
 */
bool MarkNewAffixes(const Item &before, const Item &after, uint32_t now);
bool MarkNewAffixes(const Item &before, const Item &after);

/** @brief Stops any glow. */
void ClearAffixGlow();

/**
 * @brief The glowing lines of @p item, or nullptr when none glow: the glow belongs to one item, found by its base,
 * seed and create info, and ends when its time is up or the item's modifiers change again.
 */
const NewAffixLines *GlowingAffixLines(const Item &item, uint32_t now);
const NewAffixLines *GlowingAffixLines(const Item &item);

/** @brief How bright a glow is @p elapsed ms after it starts: 1 for a moment, then easing out to 0 at its end. */
float AffixGlowCurve(uint32_t elapsed);

/** @brief How bright the glow is at @p now, from 1 to 0; 0 when nothing glows. */
float AffixGlowStrength(uint32_t now);
float AffixGlowStrength();

} // namespace devilution
