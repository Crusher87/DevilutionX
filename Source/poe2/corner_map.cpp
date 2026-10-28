#include "poe2/corner_map.hpp"

#include <algorithm>
#include <cstdlib>

#include "automap.h"
#include "data/file.hpp"
#include "data/record_reader.hpp"
#include "engine/palette.h"
#include "engine/render/automap_render.hpp"
#include "engine/render/text_render.hpp"
#include "game_mode.hpp"
#include "levels/gendung.h"
#include "levels/trigs.h"
#include "missiles.h"
#include "monster.h"
#include "objects.h"
#include "options.h"
#include "poe2/floor_chests.hpp"
#include "poe2/monster_mods.hpp"
#include "utils/log.hpp"
#include "utils/ui_fwd.h"

namespace devilution {

namespace {

constexpr std::string_view MapIconsFile = "txtdata\\poe2\\map_icons.tsv";
constexpr size_t IconCount = static_cast<size_t>(MapIcon::Count);

/** @brief The keys the table's rows go by, in MapIcon order. */
constexpr std::array<std::string_view, IconCount> IconKeys {
	"stairs_down",
	"stairs_up",
	"town",
	"warp",
	"entrance",
	"exit",
	"portal",
	"red_portal",
	"shrine",
	"strongbox",
	"rare_monster",
	"unique_monster",
};

struct NamedColor {
	std::string_view name;
	uint8_t color;
};

constexpr std::array<NamedColor, 9> MapColors { {
	{ "white", PAL16_GRAY + 1 },
	{ "grey", PAL16_GRAY + 6 },
	{ "yellow", PAL16_YELLOW + 1 },
	{ "gold", PAL16_YELLOW + 5 },
	{ "orange", PAL8_ORANGE + 1 },
	{ "red", PAL8_RED + 1 },
	{ "blue", PAL8_BLUE + 1 },
	{ "lightblue", PAL16_BLUE + 2 },
	{ "beige", PAL16_BEIGE + 2 },
} };

constexpr std::array<std::pair<std::string_view, MapIconShape>, 6> Shapes { {
	{ "diamond", MapIconShape::Diamond },
	{ "square", MapIconShape::Square },
	{ "circle", MapIconShape::Circle },
	{ "triangle_up", MapIconShape::TriangleUp },
	{ "triangle_down", MapIconShape::TriangleDown },
	{ "cross", MapIconShape::Cross },
} };

/** @brief Used until the table loads, and for any row it lacks: the same as the shipped table. */
std::array<MapIconStyle, IconCount> DefaultStyles()
{
	return { {
	    { MapIconShape::TriangleDown, PAL16_GRAY + 1, "Down" },
	    { MapIconShape::TriangleUp, PAL16_GRAY + 1, "Up" },
	    { MapIconShape::Square, PAL16_GRAY + 1, "Town" },
	    { MapIconShape::Diamond, PAL16_BLUE + 2, "Warp" },
	    { MapIconShape::Diamond, PAL16_YELLOW + 5, "Entrance" },
	    { MapIconShape::TriangleUp, PAL16_YELLOW + 5, "Exit" },
	    { MapIconShape::Circle, PAL8_BLUE + 1, "Portal" },
	    { MapIconShape::Circle, PAL8_RED + 1, "Portal" },
	    { MapIconShape::Cross, PAL16_BLUE + 2, "Shrine" },
	    { MapIconShape::Square, PAL8_ORANGE + 1, "Strongbox" },
	    { MapIconShape::Circle, PAL16_YELLOW + 1, "" },
	    { MapIconShape::Circle, PAL8_ORANGE + 1, "{name}" },
	} };
}

std::array<MapIconStyle, IconCount> Styles = DefaultStyles();
bool StylesLoaded = false;

/** @brief The corner map's centre while Auto-Centre Map is off, and the level it belongs to. */
struct Anchor {
	bool set = false;
	Point tile;
	uint8_t level = 0;
	bool questLevel = false;
	uint8_t questLevelNumber = 0;
};
Anchor CornerAnchor;

int FloorDiv(int value, int divisor)
{
	const int quotient = value / divisor;
	return (value % divisor != 0 && (value < 0) != (divisor < 0)) ? quotient - 1 : quotient;
}

bool IsExplored(Point tile)
{
	const int mx = (tile.x - 16) / 2;
	const int my = (tile.y - 16) / 2;
	if (tile.x < 16 || tile.y < 16 || mx >= DMAXX || my >= DMAXY)
		return false;
	return AutomapView[mx][my] != MAP_EXP_NONE;
}

std::optional<MapIcon> TriggerIcon(const TriggerStruct &trigger)
{
	switch (trigger._tmsg) {
	case WM_DIABNEXTLVL:
		return MapIcon::StairsDown;
	case WM_DIABPREVLVL:
		return trigger._tlvl == 0 ? MapIcon::Town : MapIcon::StairsUp;
	case WM_DIABTWARPUP:
		return MapIcon::Town;
	case WM_DIABTOWNWARP:
		return MapIcon::Warp;
	case WM_DIABSETLVL:
		return MapIcon::Entrance;
	case WM_DIABRTNLVL:
		return MapIcon::Exit;
	default:
		return std::nullopt;
	}
}

void DrawLabel(const Surface &out, const Rectangle &clip, Point middle, int radius, std::string_view text, std::vector<Rectangle> &taken)
{
	const int width = GetLineWidth(text, GameFont12);
	constexpr int Height = 12;
	Rectangle box { { middle.x + radius + 2, middle.y - (Height / 2) }, { width, Height } };
	// Flipped to the marker's left when it would run off the map.
	if (box.position.x + width > clip.position.x + clip.size.width)
		box.position.x = middle.x - radius - 2 - width;
	for (const Rectangle &other : taken) {
		const bool overlaps = box.position.x < other.position.x + other.size.width && other.position.x < box.position.x + box.size.width
		    && box.position.y < other.position.y + other.size.height && other.position.y < box.position.y + box.size.height;
		if (overlaps)
			return;
	}
	taken.push_back(box);
	const Surface clipped = out.subregion(clip.position.x, clip.position.y, clip.size.width, clip.size.height);
	const Point at = box.position - Displacement { clip.position.x, clip.position.y };
	DrawString(clipped, text, { at + Displacement { 1, 1 }, box.size }, { .flags = UiFlags::ColorBlack, .spacing = 0 });
	DrawString(clipped, text, { at, box.size }, { .flags = UiFlags::ColorWhite, .spacing = 0 });
}

void ResetAnchorOnNewLevel()
{
	if (CornerAnchor.level != currlevel || CornerAnchor.questLevel != setlevel || CornerAnchor.questLevelNumber != static_cast<uint8_t>(setlvlnum))
		CornerAnchor.set = false;
	CornerAnchor.level = currlevel;
	CornerAnchor.questLevel = setlevel;
	CornerAnchor.questLevelNumber = static_cast<uint8_t>(setlvlnum);
}

} // namespace

int CornerMapWidthPercent(CornerMapSize size)
{
	switch (size) {
	case CornerMapSize::Small:
		return 15;
	case CornerMapSize::Large:
		return 25;
	default:
		return 20;
	}
}

Rectangle CornerMapRect(int screenWidth, CornerMapSize size)
{
	const int width = screenWidth * CornerMapWidthPercent(size) / 100;
	const int height = width * 2 / 3;
	return { { screenWidth - CornerMapMargin - width, CornerMapMargin }, { width, height } };
}

bool CornerMapNeedsRecentre(Displacement fromCentre, int scale, Size mapSize)
{
	// A dungeon tile is 32 by 16 map pixels at scale 100.
	const int dx = (fromCentre.deltaX - fromCentre.deltaY) * 16 * scale / 100;
	const int dy = (fromCentre.deltaX + fromCentre.deltaY) * 8 * scale / 100;
	return std::abs(dx) * 3 > mapSize.width || std::abs(dy) * 3 > mapSize.height;
}

int CornerMapScale(int zoomLevel, int screenHeight)
{
	const int factor = std::clamp(screenHeight / 480, 1, 8);
	return 25 * std::clamp(zoomLevel, 1, 4) * factor;
}

size_t StepListIndex(size_t index, size_t listSize, int step)
{
	if (listSize == 0)
		return 0;
	if (step < 0)
		return index == 0 ? 0 : std::min(index, listSize) - 1;
	return std::min(index + 1, listSize - 1);
}

int MapOpacityPercent()
{
	if (!gbIsPoe2)
		return 100;
	return 100 - std::clamp(*GetOptions().Gameplay.mapTransparency, 0, 100);
}

int LandscapeOpacityPercent()
{
	if (!gbIsPoe2)
		return 0;
	const int landscape = 100 - std::clamp(*GetOptions().Gameplay.landscapeTransparency, 0, 100);
	return landscape * MapOpacityPercent() / 100;
}

void LoadMapIcons()
{
	Styles = DefaultStyles();
	StylesLoaded = true;
	auto dataFile = DataFile::load(MapIconsFile);
	if (!dataFile.has_value()) {
		LogError("Couldn't load the map icons: {}", MapIconsFile);
		return;
	}
	dataFile->skipHeaderOrDie(MapIconsFile);
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, MapIconsFile };
		std::string key;
		std::string shape;
		std::string colour;
		std::string label;
		reader.readString("key", key);
		reader.readString("shape", shape);
		reader.readString("colour", colour);
		reader.readString("label", label);
		const std::optional<MapIcon> icon = MapIconFromKey(key);
		if (!icon) {
			LogError("Unknown map icon in {}: {}", MapIconsFile, key);
			continue;
		}
		MapIconStyle &style = Styles[static_cast<size_t>(*icon)];
		const auto shapeIt = std::find_if(Shapes.begin(), Shapes.end(), [&](const auto &entry) { return entry.first == shape; });
		if (shapeIt != Shapes.end())
			style.shape = shapeIt->second;
		else
			LogError("Unknown map icon shape in {}: {}", MapIconsFile, shape);
		const auto colourIt = std::find_if(MapColors.begin(), MapColors.end(), [&](const NamedColor &entry) { return entry.name == colour; });
		if (colourIt != MapColors.end())
			style.color = colourIt->color;
		else
			LogError("Unknown map icon colour in {}: {}", MapIconsFile, colour);
		style.label = label == "-" ? std::string() : label;
	}
}

const MapIconStyle &GetMapIconStyle(MapIcon icon)
{
	if (!StylesLoaded)
		LoadMapIcons();
	return Styles[static_cast<size_t>(icon)];
}

std::optional<MapIcon> MapIconFromKey(std::string_view key)
{
	for (size_t i = 0; i < IconKeys.size(); i++) {
		if (IconKeys[i] == key)
			return static_cast<MapIcon>(i);
	}
	return std::nullopt;
}

std::string MapIconLabel(const MapIconStyle &style, std::string_view name)
{
	constexpr std::string_view Placeholder = "{name}";
	std::string label = style.label;
	if (const size_t at = label.find(Placeholder); at != std::string::npos)
		label.replace(at, Placeholder.size(), name);
	return label;
}

bool IsInMapIconShape(MapIconShape shape, int dx, int dy, int radius)
{
	const int ax = std::abs(dx);
	const int ay = std::abs(dy);
	if (ax > radius || ay > radius)
		return false;
	switch (shape) {
	case MapIconShape::Diamond:
		return ax + ay <= radius;
	case MapIconShape::Square:
		return ax < radius && ay < radius;
	case MapIconShape::Circle:
		return (dx * dx) + (dy * dy) <= (radius * radius) + radius / 2;
	case MapIconShape::TriangleUp:
		// The point at the top, the flat side at the bottom.
		return 2 * ax <= dy + radius;
	case MapIconShape::TriangleDown:
		return 2 * ax <= radius - dy;
	case MapIconShape::Cross:
		return ax <= radius / 3 || ay <= radius / 3;
	}
	return false;
}

int MapIconRadius(int scale)
{
	return std::clamp((scale / 25) + 1, 2, 5);
}

std::vector<MapMarker> CollectMapMarkers()
{
	std::vector<MapMarker> markers;
	const bool town = leveltype == DTYPE_TOWN;
	for (int i = 0; i < numtrigs; i++) {
		const std::optional<MapIcon> icon = TriggerIcon(trigs[i]);
		if (icon && (town || IsExplored(trigs[i].position)))
			markers.push_back({ *icon, trigs[i].position, {} });
	}
	for (const Missile &missile : Missiles) {
		if (missile._mitype == MissileID::TownPortal)
			markers.push_back({ MapIcon::Portal, missile.position.tile, {} });
		else if (missile._mitype == MissileID::RedPortal)
			markers.push_back({ MapIcon::RedPortal, missile.position.tile, {} });
	}
	for (int i = 0; i < ActiveObjectCount; i++) {
		const Object &object = Objects[ActiveObjects[i]];
		// Used shrines and opened strongboxes can't be clicked any more.
		if (object.selectionRegion == SelectionRegion::None || !IsExplored(object.position))
			continue;
		if (object.IsShrine())
			markers.push_back({ MapIcon::Shrine, object.position, std::string(object.name().str()) });
		else if (object.IsChest() && GetFloorChestKind(object) == FloorChestKind::Strongbox)
			markers.push_back({ MapIcon::Strongbox, object.position, {} });
	}
	// Like POE2 since 0.2.0e, rare and unique monsters always show.
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &monster = Monsters[ActiveMonsters[i]];
		if (monster.isInvalid || monster.isPlayerMinion() || (monster.flags & MFLAG_HIDDEN) != 0 || monster.mode == MonsterMode::Death || monster.hitPoints <= 0)
			continue;
		if (monster.isUnique())
			markers.push_back({ MapIcon::UniqueMonster, monster.position.tile, std::string(monster.name()) });
		else if (GetMonsterAffixes(monster).rarity == MonsterRarity::Rare)
			markers.push_back({ MapIcon::RareMonster, monster.position.tile, RareMonsterName(monster) });
	}
	return markers;
}

void DrawMapIcon(const Surface &out, Point middle, const MapIconStyle &style, int radius, const Rectangle &clip)
{
	const int opacity = GetMapLineOpacity();
	const int left = std::max({ middle.x - radius - 1, clip.position.x, 0 });
	const int right = std::min({ middle.x + radius + 1, clip.position.x + clip.size.width - 1, out.w() - 1 });
	const int top = std::max({ middle.y - radius - 1, clip.position.y, 0 });
	const int bottom = std::min({ middle.y + radius + 1, clip.position.y + clip.size.height - 1, out.h() - 1 });
	for (int y = top; y <= bottom; y++) {
		for (int x = left; x <= right; x++) {
			const int dx = x - middle.x;
			const int dy = y - middle.y;
			uint8_t color;
			if (IsInMapIconShape(style.shape, dx, dy, radius)) {
				color = style.color;
			} else if (IsInMapIconShape(style.shape, dx + 1, dy, radius) || IsInMapIconShape(style.shape, dx - 1, dy, radius)
			    || IsInMapIconShape(style.shape, dx, dy + 1, radius) || IsInMapIconShape(style.shape, dx, dy - 1, radius)) {
				// A dark outline keeps the marker clear of the map's lines.
				color = 0;
			} else {
				continue;
			}
			uint8_t &pixel = *out.at(x, y);
			pixel = BlendMapColor(color, pixel, opacity);
		}
	}
}

void DrawMapMarkers(const Surface &out, const Rectangle &clip, int scale, tl::function_ref<Point(Point)> tileToScreen)
{
	const std::vector<MapMarker> markers = CollectMapMarkers();
	if (markers.empty())
		return;
	const int radius = MapIconRadius(scale);
	const bool names = *GetOptions().Gameplay.mapPoiNames;
	std::vector<Rectangle> taken;
	for (const MapMarker &marker : markers) {
		const Point middle = tileToScreen(marker.tile);
		if (!clip.contains(middle))
			continue;
		const MapIconStyle &style = GetMapIconStyle(marker.icon);
		DrawMapIcon(out, middle, style, radius, clip);
		if (names && !style.label.empty())
			DrawLabel(out, clip, middle, radius, MapIconLabel(style, marker.name), taken);
	}
}

bool CornerMapShown()
{
	return gbIsPoe2 && *GetOptions().Gameplay.showCornerMap && !AutomapActive;
}

Rectangle CurrentCornerMapRect()
{
	return CornerMapRect(gnScreenWidth, *GetOptions().Gameplay.cornerMapSize);
}

void DrawPoe2CornerMap(const Surface &out)
{
	if (!CornerMapShown() || MyPlayer == nullptr)
		return;
	ResetAnchorOnNewLevel();
	CornerMapView view;
	view.rect = CurrentCornerMapRect();
	view.scale = CornerMapScale(*GetOptions().Gameplay.mapZoom, gnScreenHeight);
	const Point hero = MyPlayer->position.tile;
	if (*GetOptions().Gameplay.autoCentreMap || !CornerAnchor.set || CornerMapNeedsRecentre(hero - CornerAnchor.tile, view.scale, view.rect.size)) {
		CornerAnchor.tile = hero;
		CornerAnchor.set = true;
	}
	view.offset = *GetOptions().Gameplay.autoCentreMap
	    ? Displacement { 0, 0 }
	    : Displacement { FloorDiv(CornerAnchor.tile.x - ViewPosition.x, 2), FloorDiv(CornerAnchor.tile.y - ViewPosition.y, 2) };
	DrawCornerMap(out, view);
}

bool CornerMapZoomKey(int step)
{
	if (!CornerMapShown())
		return false;
	auto &zoom = GetOptions().Gameplay.mapZoom;
	zoom.SetActiveListIndex(StepListIndex(zoom.GetActiveListIndex(), zoom.GetListSize(), step));
	return true;
}

} // namespace devilution
