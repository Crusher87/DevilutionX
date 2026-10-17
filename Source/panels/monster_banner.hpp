/**
 * @file panels/monster_banner.hpp
 *
 * POE2 Mashup monster banner: the hovered monster's name, life bar, modifiers and ailments at the top middle
 * of the screen, like POE2, framed in Diablo's gold and red.
 */
#pragma once

#include "engine/surface.hpp"

namespace devilution {

/** @brief Draws the banner for the monster under the cursor, or the one last hovered for a moment after. */
void DrawMonsterBanner(const Surface &out);

} // namespace devilution
