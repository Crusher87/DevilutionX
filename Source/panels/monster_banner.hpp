/**
 * @file panels/monster_banner.hpp
 *
 * POE2 Mashup monster banner (and Sanctuary Reforged's, with Diablo's own facts only): the hovered monster's name, life bar, modifiers and ailments at the top middle
 * of the screen, like POE2, framed in Diablo's gold and red.
 */
#pragma once

#include <string>
#include <vector>

#include "DiabloUI/ui_flags.hpp"
#include "engine/surface.hpp"

namespace devilution {

struct Monster;

/** @brief Sanctuary Reforged: the banner's words for a monster, from Diablo's own facts only. */
struct MonsterBannerText {
	std::string name;
	UiFlags nameColor;
	/** The lines under the life bar: for a unique its kind, then its type and kills and its resistances, in Diablo's words. */
	std::vector<std::string> lines;
};

/**
 * @brief Sanctuary Reforged: what the banner says about @p monster: its name in Diablo's colours (gold for a unique,
 * blue for a pack's minion), the kind behind a unique's name, Diablo's "Type:" and kills line, and its resistances
 * and immunities as Diablo's info box words them once they are known. No POE2 modifiers, levels, armour or percentages.
 */
MonsterBannerText ReforgedMonsterBannerText(const Monster &monster);

/** @brief Draws the banner for the monster under the cursor, or the one last hovered for a moment after. */
void DrawMonsterBanner(const Surface &out);

} // namespace devilution
