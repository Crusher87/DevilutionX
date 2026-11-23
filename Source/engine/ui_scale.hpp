/**
 * @file engine/ui_scale.hpp
 *
 * POE2 Mashup: UI scaling. The world is drawn at the full resolution, so a big screen shows more of the map,
 * while the UI (panels, text, tooltips, menus) is drawn at a smaller size and scaled up over it. The screen size
 * the rest of the game sees (gnScreenWidth/gnScreenHeight) is the UI's; the world drawing and the mouse picking of
 * the world switch to the full size with WorldSpaceScope.
 */
#pragma once

#include <cstdint>

#include "engine/point.hpp"
#include "engine/size.hpp"
#include "utils/attributes.h"

namespace devilution {

/** @brief The size the world is drawn at and the frame is shown at; the same as the screen size when the UI isn't scaled. */
extern DVL_API_FOR_TEST uint16_t gnRenderWidth;
extern DVL_API_FOR_TEST uint16_t gnRenderHeight;

/** @brief The UI scale in effect, in percent; 100 when the UI isn't scaled. */
int ActiveUiScale();
/** @brief The UI is drawn smaller than the frame and scaled up over it. Means "the UI is bigger", not "the world has a surface of its own": for that see HasWorldLayer (dx.h). */
bool IsUiScaled();
/**
 * @brief POE2 Mashup: whether the world gets a frame of its own, under the UI, which the HD world, its light, the
 * effects and the HD UI art need. Always with the UI scaled; at UI Scale 100 only where frames can go up in layers (an
 * SDL2 renderer) and HD Graphics or Modern Effects are on, so plain Diablo at 100 draws exactly as it did.
 */
bool WantsWorldLayer(bool uiScaled, bool layeredFrames, bool hdOrEffects);
/**
 * @brief Works out the UI size for the frame size and the UI scale option, keeping the UI at least 640x480.
 * @return the UI size, which becomes the screen size.
 */
Size SetRenderSize(Size renderSize, int requestedScalePercent, bool canScale);

/** @brief From UI (screen) coordinates to world drawing coordinates, and back. */
Point UiToWorld(Point position);
Point WorldToUi(Point position);
int UiToWorldHeight(int height);

/**
 * @brief While it lives, the screen size, the viewport and the mouse position are the world's, for drawing the world and
 * finding what the mouse points at. Nests; does nothing when the UI isn't scaled.
 */
class WorldSpaceScope {
public:
	WorldSpaceScope();
	~WorldSpaceScope();
	WorldSpaceScope(const WorldSpaceScope &) = delete;
	WorldSpaceScope &operator=(const WorldSpaceScope &) = delete;

private:
	bool active_;
};

} // namespace devilution
