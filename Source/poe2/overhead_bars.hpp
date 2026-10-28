/**
 * @file poe2/overhead_bars.hpp
 *
 * POE2 Mashup: small bars drawn over characters in the world (Curtis 2026-10-28, question 12).
 *  - "Mini Life Bar Above You": the hero's life with a thin mana bar under it, and with "Mini Bar Shows Energy
 *    Shield" the Energy Shield laid over the top of the life bar, as POE2 draws it.
 * The world drawing notes where the hero was drawn; the bars are drawn afterwards with the UI, so they keep their
 * size at any UI scale and zoom.
 */
#pragma once

#include <cstdint>

#include "engine/clx_sprite.hpp"
#include "engine/palette.h"
#include "engine/point.hpp"
#include "engine/surface.hpp"

namespace devilution {

struct Player;

/** @brief Sizes, in UI pixels. */
constexpr int HeroBarWidth = 40;
constexpr int HeroLifeRows = 4;
constexpr int HeroManaRows = 2;
/** @brief Rows at the top of the life bar the Energy Shield covers. */
constexpr int HeroEnergyShieldRows = 2;
/** @brief Gap between the top of the sprite and the bottom of its bar, in world pixels. */
constexpr int OverheadBarGap = 6;
/** @brief How far above the hero's feet the bar sits when the stand picture isn't loaded, in world pixels. */
constexpr int DefaultHeroHeight = 76;

/** @brief Bar colours. */
constexpr uint8_t OverheadBarEmptyColor = PAL16_GRAY + 15;
constexpr uint8_t HeroBarFrameColor = PAL16_GRAY + 13;
constexpr uint8_t OverheadBarLifeColor = PAL16_RED + 6;
constexpr uint8_t OverheadBarEnergyShieldColor = PAL16_BLUE + 3;
constexpr uint8_t OverheadBarManaColor = PAL16_BLUE + 9;

/** @brief Overall height of the hero's bar: frame, life, a dark line, mana, frame. */
constexpr int HeroBarHeight = 1 + HeroLifeRows + 1 + HeroManaRows + 1;

/** @brief The first row of a sprite, counted from its top, with any solid pixel; the height when it has none. */
int ClxSolidTopRow(ClxSprite sprite);

/** @brief Pixels of a bar @p width wide filled for current out of maximum; at least 1 while current is above 0. */
int OverheadBarFill(int64_t current, int64_t maximum, int width);

/** @brief What a bar shows, in whatever units; maxima of 0 hide that part. */
struct OverheadBarValues {
	int64_t life = 0;
	int64_t maxLife = 0;
	int64_t energyShield = 0;
	int64_t maxEnergyShield = 0;
	int64_t mana = 0;
	int64_t maxMana = 0;
};

/** @brief Draws the hero's bar with its top left corner at @p topLeft. */
void DrawHeroBar(const Surface &out, Point topLeft, const OverheadBarValues &values, bool showEnergyShield);

/** @brief Notes where the hero was drawn: the sprite's bottom left in the world drawing's buffer. */
void QueueHeroOverheadBar(const Player &player, Point spritePosition, ClxSprite sprite);
/** @brief Draws the noted bars over the view and forgets them. */
void DrawOverheadBars(const Surface &out);
void ClearOverheadBars();

} // namespace devilution
