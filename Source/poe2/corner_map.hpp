/**
 * @file poe2/corner_map.hpp
 *
 * POE2 Mashup: the always-on corner map and the POE2 map settings (Curtis 2026-10-28, questions 14 to 17).
 *  - A small map in the top right corner, about a fifth of the screen wide (Corner Map Size), drawn with the
 *    automap's own lines. Diablo's full-screen Tab map stays; the corner map steps aside while it is open.
 *  - Show Corner Map, Auto-Centre Map, Map Transparency, Landscape Transparency, Map Zoom (also + and - while the Tab
 *    map is closed) and Point of Interest names, as POE2's Options > UI > Map has them. Transparency, landscape and
 *    the markers apply to the Tab map too; zoom and auto-centre are the corner map's.
 *  - Markers for stairs and other exits, town portals, shrines, strongboxes and rare and unique monsters. Their
 *    shapes, colours and names come from txtdata/poe2/map_icons.tsv.
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <function_ref.hpp>

#include "engine/displacement.hpp"
#include "engine/point.hpp"
#include "engine/rectangle.hpp"
#include "engine/surface.hpp"

namespace devilution {

enum class CornerMapSize : uint8_t;

enum class MapIcon : uint8_t {
	StairsDown,
	StairsUp,
	Town,
	Warp,
	Entrance,
	Exit,
	Portal,
	RedPortal,
	Shrine,
	Strongbox,
	RareMonster,
	UniqueMonster,
	Count,
};

enum class MapIconShape : uint8_t {
	Diamond,
	Square,
	Circle,
	TriangleUp,
	TriangleDown,
	Cross,
};

struct MapIconStyle {
	MapIconShape shape = MapIconShape::Diamond;
	uint8_t color = 0;
	/** @brief The name shown with Point of Interest names on; "{name}" is the thing's own name, empty shows none. */
	std::string label;
};

/** @brief Something on the level the map marks. */
struct MapMarker {
	MapIcon icon;
	/** @brief Dungeon tile. */
	Point tile;
	/** @brief The thing's own name, for a "{name}" label. */
	std::string name;
};

/** @brief Gap between the corner map and the screen's top and right edges, in UI pixels. */
constexpr int CornerMapMargin = 8;

/** @brief The corner map's width in percent of the screen's for each Corner Map Size. */
int CornerMapWidthPercent(CornerMapSize size);
/** @brief Where the corner map goes: the top right corner, two thirds as high as it is wide. */
Rectangle CornerMapRect(int screenWidth, CornerMapSize size);
/**
 * @brief With Auto-Centre Map off, whether the hero has gone far enough from the map's centre for the map to centre
 * on them again: more than a third of the way to an edge.
 * @param fromCentre The hero's tile less the map centre's tile.
 */
bool CornerMapNeedsRecentre(Displacement fromCentre, int scale, Size mapSize);
/**
 * @brief The corner map's scale (as AutoMapScale) for a Map Zoom level: 25 a level, times the screen's height in
 * steps of 480 lines as Diablo's own minimap does, so the map keeps its look at every resolution and UI scale.
 */
int CornerMapScale(int zoomLevel, int screenHeight);
/** @brief The next value of a stepped option, one step up (+1) or down (-1), staying in the list. */
size_t StepListIndex(size_t index, size_t listSize, int step);

/** @brief Line opacity from Map Transparency, in percent (100 in Diablo and Hellfire). */
int MapOpacityPercent();
/** @brief Floor opacity from Landscape Transparency and Map Transparency, in percent. */
int LandscapeOpacityPercent();

/** @brief The marker styles, from txtdata/poe2/map_icons.tsv. */
void LoadMapIcons();
const MapIconStyle &GetMapIconStyle(MapIcon icon);
std::optional<MapIcon> MapIconFromKey(std::string_view key);
/** @brief A label with "{name}" filled in. */
std::string MapIconLabel(const MapIconStyle &style, std::string_view name);
/** @brief Whether a pixel at (dx, dy) from the middle of a marker of the given radius is inside its shape. */
bool IsInMapIconShape(MapIconShape shape, int dx, int dy, int radius);
/** @brief Marker radius in pixels for a map scale. */
int MapIconRadius(int scale);

/** @brief The markers on the current level: exits, portals, unused shrines and strongboxes the hero has seen, and every live rare and unique monster. */
std::vector<MapMarker> CollectMapMarkers();
/** @brief Draws one marker: its shape in its colour with a dark outline, at the map's line opacity. */
void DrawMapIcon(const Surface &out, Point middle, const MapIconStyle &style, int radius, const Rectangle &clip);
/** @brief Draws the level's markers on a map, and their names when Point of Interest names is on. */
void DrawMapMarkers(const Surface &out, const Rectangle &clip, int scale, tl::function_ref<Point(Point)> tileToScreen);

/** @brief Whether the corner map shows now: POE2 mode, the option on and the Tab map closed. */
bool CornerMapShown();
/** @brief The corner map's rectangle on this screen. */
Rectangle CurrentCornerMapRect();
/** @brief Draws the corner map, when it shows. */
void DrawPoe2CornerMap(const Surface &out);
/** @brief + and - while the Tab map is closed: zooms the corner map. @return whether the key was used. */
bool CornerMapZoomKey(int step);

} // namespace devilution
