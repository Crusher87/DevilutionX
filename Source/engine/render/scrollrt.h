/**
 * @file scrollrt.h
 *
 * Interface of functionality for rendering the dungeons, monsters and calling other render routines.
 */
#pragma once

#include <cstdint>
#include <optional>

#include "engine/animationinfo.h"
#include "engine/direction.hpp"
#include "engine/displacement.hpp"
#include "engine/point.hpp"
#include "engine/surface.hpp"

namespace devilution {

extern bool AutoMapShowItems;
extern bool frameflag;

/**
 * @brief Returns the offset for the walking animation
 * @param animationInfo the current active walking animation
 * @param dir walking direction
 * @param cameraMode Adjusts the offset relative to the camera
 */
Displacement GetOffsetForWalking(const AnimationInfo &animationInfo, Direction dir, bool cameraMode = false);

/**
 * @brief Clear cursor state
 */
void ClearCursor();

/**
 * @brief Shifting the view area along the logical grid
 *        Note: this won't allow you to shift between even and odd rows
 * @param horizontal Shift the screen left or right
 * @param vertical Shift the screen up or down
 */
void ShiftGrid(Point *offset, int horizontal, int vertical);

/**
 * @brief Gets the number of rows covered by the main panel
 */
int RowsCoveredByPanel();

/**
 * @brief Calculate the offset needed for centering tiles in view area
 * @param offsetX Offset in pixels
 * @param offsetY Offset in pixels
 */
void CalcTileOffset(int *offsetX, int *offsetY);

/**
 * @brief Calculate the needed diamond tile to cover the view area
 * @param columns Tiles needed per row
 * @param rows Both even and odd rows
 */
void TilesInView(int *columns, int *rows);
void CalcViewportGeometry();

/**
 * @brief Calculate the screen position of a given tile
 * @param tile Position of a dungeon tile
 */
Point GetScreenPosition(Point tile);

/**
 * @brief Render the whole screen black
 */
void ClearScreenBuffer();
#ifdef _DEBUG

/**
 * @brief Scroll the screen when mouse is close to the edge
 */
void ScrollView();
#endif

/**
 * @brief Initialize the FPS meter
 */
void EnableFrameCount();

/**
 * @brief Redraw screen
 */
void scrollrt_draw_game_screen();

/**
 * @brief Render the game
 */
void DrawAndBlit();

/**
 * @brief POE2 Mashup: what a frame's world was drawn for. While the game stands still under the game menu and none of
 * this changes, the world drawn for an earlier frame is shown again instead of being drawn anew (KeepWorldPicture).
 */
struct WorldPictureKey {
	/** @brief The view's first tile and how far it is moved (walking, a shake). */
	Point view;
	Displacement offset;
	int screenWidth = 0;
	int screenHeight = 0;
	int renderWidth = 0;
	int renderHeight = 0;
	int viewportHeight = 0;
	/** @brief The world's frame (WorldLayerGeneration), the options (OptionChangeCount), the menu's items, the renderer. */
	uint32_t worldLayer = 0;
	uint32_t options = 0;
	const void *menu = nullptr;
	const void *renderer = nullptr;
	int level = 0;
	int levelType = 0;
	int setLevel = -1;
	bool zoom = false;
	bool hd = false;
	bool effects = false;
	bool leftPanel = false;
	bool rightPanel = false;
	/** @brief The effects wanted the floor's copy made as the world was drawn (vfx::WantsFloorCopy). */
	bool floorCopy = false;

	bool operator==(const WorldPictureKey &) const = default;
};

/** @brief POE2 Mashup: what says whether the world stands still from one frame to the next. */
struct WorldStillness {
	bool gameMenuOpen = false;
	bool multiplayer = false;
	bool heroDead = false;
	/** @brief Everything is to be drawn again (RedrawEverything: the window shown again, a display option changed). */
	bool redrawAll = false;
	/** @brief The world's frame keeps the world as drawn between frames (WorldLayerKeepsWorld). */
	bool layerKeepsWorld = false;
};

/**
 * @brief POE2 Mashup: whether nothing in the world can change: a single player game stops under the game menu (a
 * multiplayer one goes on), and the frame the world is drawn into keeps it.
 */
bool WorldStandsStill(const WorldStillness &world);

/**
 * @brief POE2 Mashup: whether this frame may show the world drawn for the frame `kept` was noted for, instead of drawing
 * it again: the world stands still and nothing it was drawn for has changed. The first frame that stands still is drawn.
 */
bool MayShowKeptWorld(const WorldStillness &world, const std::optional<WorldPictureKey> &kept, const WorldPictureKey &now);

} // namespace devilution
