#include "engine/ui_scale.hpp"

#include <algorithm>

#include "diablo.h"
#include "utils/ui_fwd.h"

namespace devilution {

uint16_t gnRenderWidth;
uint16_t gnRenderHeight;

namespace {

int ActiveScale = 100;
int ScopeDepth = 0;
uint16_t SavedScreenWidth;
uint16_t SavedScreenHeight;
uint16_t SavedViewportHeight;
Point SavedMousePosition;

constexpr Size MinUiSize { 640, 480 };

} // namespace

int ActiveUiScale()
{
	return ActiveScale;
}

bool IsUiScaled()
{
	return ActiveScale != 100;
}

Size SetRenderSize(Size renderSize, int requestedScalePercent, bool canScale)
{
	gnRenderWidth = static_cast<uint16_t>(renderSize.width);
	gnRenderHeight = static_cast<uint16_t>(renderSize.height);
	int scale = canScale ? requestedScalePercent : 100;
	// The UI never gets smaller than the game's smallest screen.
	scale = std::min({ scale, renderSize.width * 100 / MinUiSize.width, renderSize.height * 100 / MinUiSize.height });
	ActiveScale = std::max(scale, 100);
	if (ActiveScale == 100)
		return renderSize;
	return { renderSize.width * 100 / ActiveScale, renderSize.height * 100 / ActiveScale };
}

Point UiToWorld(Point position)
{
	if (!IsUiScaled())
		return position;
	const int uiWidth = ScopeDepth > 0 ? SavedScreenWidth : gnScreenWidth;
	const int uiHeight = ScopeDepth > 0 ? SavedScreenHeight : gnScreenHeight;
	return { position.x * gnRenderWidth / uiWidth, position.y * gnRenderHeight / uiHeight };
}

Point WorldToUi(Point position)
{
	if (!IsUiScaled())
		return position;
	const int uiWidth = ScopeDepth > 0 ? SavedScreenWidth : gnScreenWidth;
	const int uiHeight = ScopeDepth > 0 ? SavedScreenHeight : gnScreenHeight;
	return { position.x * uiWidth / gnRenderWidth, position.y * uiHeight / gnRenderHeight };
}

int UiToWorldHeight(int height)
{
	if (!IsUiScaled())
		return height;
	const int uiHeight = ScopeDepth > 0 ? SavedScreenHeight : gnScreenHeight;
	return height * gnRenderHeight / uiHeight;
}

WorldSpaceScope::WorldSpaceScope()
    : active_(IsUiScaled())
{
	if (!active_)
		return;
	if (ScopeDepth++ > 0)
		return;
	SavedScreenWidth = gnScreenWidth;
	SavedScreenHeight = gnScreenHeight;
	SavedViewportHeight = gnViewportHeight;
	SavedMousePosition = MousePosition;
	MousePosition = { MousePosition.x * gnRenderWidth / gnScreenWidth, MousePosition.y * gnRenderHeight / gnScreenHeight };
	gnViewportHeight = SavedViewportHeight == SavedScreenHeight ? gnRenderHeight : static_cast<uint16_t>(SavedViewportHeight * gnRenderHeight / SavedScreenHeight);
	gnScreenWidth = gnRenderWidth;
	gnScreenHeight = gnRenderHeight;
}

WorldSpaceScope::~WorldSpaceScope()
{
	if (!active_)
		return;
	if (--ScopeDepth > 0)
		return;
	gnScreenWidth = SavedScreenWidth;
	gnScreenHeight = SavedScreenHeight;
	gnViewportHeight = SavedViewportHeight;
	MousePosition = SavedMousePosition;
}

} // namespace devilution
