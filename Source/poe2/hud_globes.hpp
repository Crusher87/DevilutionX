/**
 * @file poe2/hud_globes.hpp
 *
 * POE2 Mashup HUD: animated life and mana globes, the energy shield ring around the life globe,
 * the Spirit arc along the mana globe, the numbers above both globes, the Rage bar over the belt and
 * liquid levels on belt flasks, all drawn in code on top of Diablo's own panel art.
 */
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "DiabloUI/ui_flags.hpp"
#include "engine/clx_sprite.hpp"
#include "engine/point.hpp"
#include "engine/rectangle.hpp"
#include "engine/surface.hpp"

namespace devilution {

struct Player;

/** @brief Where the life and mana globes' art starts in the main panel. */
constexpr int LifeGlobeX = 96;
constexpr int ManaGlobeX = 464;
/** @brief Size of the bulb art; rows above GlobePanelTopRow stick out above the main panel. */
constexpr int GlobeArtSize = 88;
constexpr int GlobeTopRow = 3;
constexpr int GlobePanelTopRow = 16;
constexpr int GlobeBottomRow = 85;
/** @brief Columns of the dome above the panel. */
constexpr int GlobeDomeLeft = 11;
constexpr int GlobeDomeRight = 73;
/** @brief How full a globe is when full, like Player::_pHPPer. */
constexpr int GlobeFull = 81;

constexpr int EnergyShieldSegments = 12;
constexpr int SpiritSegments = 10;

enum class GlobeLiquid : uint8_t {
	Life,
	Mana,
};

/** @brief Where the liquid sits in the bulb art, measured from the empty and full art. */
struct GlobeShape {
	float centerX = 0;
	float centerY = 0;
	float radiusX = 0;
	float radiusY = 0;
	int top = 0;
	int bottom = 0;
	bool valid = false;
};

/** @brief A ring of segments around a globe. Angles are in degrees on screen: 0 is right, -90 is up. */
struct GlobeArc {
	float startDegrees;
	float endDegrees;
	int segments;
	/** @brief First color of the 16 shade palette ramp lit segments use. */
	uint8_t rampBase;
	/** @brief Light segments from the end of the arc instead of the start. */
	bool fillFromEnd;
};

/** @brief The life globe's energy shield ring runs down its right side and fills from the bottom. */
constexpr GlobeArc EnergyShieldArc { -80.F, 80.F, EnergyShieldSegments, 176, true };
/** @brief The mana globe's Spirit arc runs from its left side over the top. */
constexpr GlobeArc SpiritArc { -205.F, -70.F, SpiritSegments, 192, false };

/** @brief Pixels that differ between the empty and the full art are liquid. */
GlobeShape MeasureGlobe(const Surface &emptyArt, const Surface &fullArt);

/** @brief Segments to light for current out of maximum; at least one while current is above zero. */
int LitArcSegments(int current, int maximum, int segments);

/** @brief The art row the liquid surface rests on at the given fill, before waves. */
float GlobeLiquidLevel(const GlobeShape &shape, int fill);

/**
 * @brief Draws part of a globe: liquid below the waving surface, the empty art above it.
 * @param origin Screen position of the art's top left corner.
 * @param fill How full the globe is, from 0 to GlobeFull.
 * @param dome Draws the rows above the panel (and skips their transparent pixels) instead of the rows inside it.
 * @param pulse Makes the liquid throb, for low life.
 */
void DrawGlobeLiquid(const Surface &out, Point origin, const Surface &emptyArt, const Surface &fullArt, const GlobeShape &shape,
    GlobeLiquid liquid, int fill, bool dome, uint32_t timeMs, bool pulse);

/**
 * @brief Draws the segments of an arc around a globe, only on screen rows from clipTop up to clipBottom.
 */
void DrawGlobeArc(const Surface &out, Point center, int radius, int thickness, const GlobeArc &arc, int litSegments,
    int clipTop, int clipBottom, uint32_t timeMs);

/** @brief Draws the life globe and its energy shield ring; dome selects the part above the main panel. */
void DrawPoe2LifeGlobe(const Surface &out, bool dome);
/** @brief Draws the mana globe and its Spirit arc; dome selects the part above the main panel. */
void DrawPoe2ManaGlobe(const Surface &out, bool dome);
/** @brief Timed lines over the mana globe: the Vizjerei Road toll's backlash and a Venomous map's poison. */
void DrawPoe2GlobeValues(const Surface &out);
/** @brief The numbers printed above the globes in POE2 mode. */
struct GlobeLevels {
	int life = 0;
	int maxLife = 0;
	int energyShield = 0;
	int maxEnergyShield = 0;
	int mana = 0;
	int maxMana = 0;
	int reservedSpirit = 0;
	int maxSpirit = 0;
	int rage = 0;
	int combo = 0;
	int frenzyCharges = 0;
};

/** @brief One line above a globe, like POE2's: its label at the left of the block, its number at the right. */
struct GlobeLevelLine {
	std::string label;
	std::string value;
	UiFlags color;
};

/** @brief Height of one line of numbers above a globe. */
constexpr int GlobeLevelLineHeight = 12;
/** @brief Most lines above one globe: Mana, Spirit, Rage and Combo over the mana globe. */
constexpr int GlobeLevelLineSlots = 4;
/**
 * @brief Width of the block of lines over a globe. Over the life globe it is centred; over the mana globe it starts at
 * the globe's left edge, as the skill tray reaches up to there.
 */
constexpr int GlobeLevelBlockWidth = 104;
/** @brief Pixels between the longest name over a globe and the numbers (POE2 keeps them close). */
constexpr int GlobeLevelValueGap = 5;
/** @brief POE2's colours for the names (Life, Mana...) and the numbers over the globes, read off Curtis's picture. */
constexpr std::array<uint8_t, 3> GlobeLevelLabelColour { 140, 152, 178 };
constexpr std::array<uint8_t, 3> GlobeLevelValueColour { 232, 235, 236 };
/** @brief Gap between the top of a globe's dome and the lowest line above it, clear of the ring and arc round the dome. */
constexpr int GlobeLevelGap = 11;

GlobeLevels ReadGlobeLevels(const Player &player);
/** @brief Lines over the life globe, top to bottom: Life, Energy Shield when the hero has any, Frenzy charges while held. */
std::vector<GlobeLevelLine> LifeGlobeLevelLines(const GlobeLevels &levels);
/**
 * @brief Lines over the mana globe, top to bottom: Mana, Spirit (reserved / total) when the hero has any, then Rage and
 * Combo while built up.
 */
std::vector<GlobeLevelLine> ManaGlobeLevelLines(const GlobeLevels &levels);
/** @brief Whether the numbers above the globes are drawn: always in POE2 mode. */
bool ShowingGlobeLevels();
/** @brief Draws the numbers above both globes. */
void DrawPoe2GlobeLevels(const Surface &out);

/**
 * @brief The Rage bar lies along the top edge of the belt, between the globes, like POE2's along its skill bar: a
 * 1 pixel dark frame round a red fill on a darker track. It is as wide as the belt's frame, and its top row is this
 * far below the main panel's top.
 */
constexpr int RageBarY = -1;
constexpr int RageBarHeight = 5;
/** @brief Pixels of the bar's track the fill covers, left to right. */
int RageBarFill(int rage, int maxRage, int trackWidth);
/** @brief Draws a Rage bar in the rectangle, frame included; nothing while rage is 0. */
void DrawRageBar(const Surface &out, const Rectangle &bar, int rage, int maxRage);
/** @brief Draws the hero's Rage bar over the belt while they have Rage. */
void DrawPoe2RageBar(const Surface &out);

/**
 * @brief Darkens the empty top part of a flask in the belt and lights its liquid surface.
 * @param position Bottom left corner the sprite was drawn at.
 */
void DrawFlaskLiquidLevel(const Surface &out, Point position, ClxSprite sprite, int charges, int maxCharges);

} // namespace devilution
