#include "poe2/level_census.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include <magic_enum/magic_enum.hpp>

#include "engine/point.hpp"
#include "items.h"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "levels/themes.h"
#include "levels/tile_properties.hpp"
#include "levels/trigs.h"
#include "monster.h"
#include "multi.h"
#include "objects.h"
#include "player.h"
#include "plrmsg.h"
#include "poe2/floors.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/telemetry_events.hpp"
#include "poe2/waystones.hpp"
#include "utils/format.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

/** dTransVal is an int8_t, so a floor has at most this many room numbers. */
constexpr int MaxRooms = 128;
/** Rooms smaller than this are corners and door gaps, left out of the headline counts. */
constexpr int NookTiles = 6;
/** Monsters this close to the hero count as "near" (the same distance as the fight log uses). */
constexpr int NearTiles = 5;
/** Monsters this close to each other in the same room count as one group. */
constexpr int GroupTiles = 3;
/** Floor further than this many steps from every monster counts as quiet floor. */
constexpr int QuietSteps = 8;
/** Kills are counted in steps of this many seconds for the kill timeline. */
constexpr uint32_t KillStepMs = 10000;
/** A tick longer than this (a loading screen, a pause) only counts this much. */
constexpr uint32_t LongestTickMs = 250;

/** The rarity names used by the rest of the play log, by MonsterRarityIndex. */
constexpr std::array<std::string_view, 4> RarityNames { "normal", "magic", "rare", "unique" };

uint32_t Now()
{
	return static_cast<uint32_t>(SDL_GetTicks());
}

int RoomAt(Point tile)
{
	return static_cast<uint8_t>(dTransVal[tile.x][tile.y]) % MaxRooms;
}

/** Floor the hero can walk on; a closed door counts, as the hero opens it by walking into it. */
bool Walkable(Point tile)
{
	if (!InDungeonBounds(tile))
		return false;
	if (IsTileNotSolid(tile))
		return true;
	const Object *object = FindObjectAtPosition(tile, false);
	return object != nullptr && object->isDoor();
}

bool IsHostile(const Monster &monster)
{
	return !monster.isPlayerMinion() && !monster.hasNoLife() && monster.talkMsg == TEXT_NONE && InDungeonBounds(monster.position.tile);
}

int RarityOf(const Monster &monster)
{
	if (monster.isUnique())
		return 3;
	switch (GetMonsterAffixes(monster).rarity) {
	case MonsterRarity::Rare: return 2;
	case MonsterRarity::Magic: return 1;
	default: return 0;
	}
}

/** Monsters that mostly fight from a distance: archers, casters and spitters. */
bool IsRanged(const Monster &monster)
{
	switch (monster.ai) {
	case MonsterAIID::SkeletonRanged:
	case MonsterAIID::GoatRanged:
	case MonsterAIID::Succubus:
	case MonsterAIID::LazarusSuccubus:
	case MonsterAIID::Counselor:
	case MonsterAIID::Mega:
	case MonsterAIID::Magma:
	case MonsterAIID::Storm:
	case MonsterAIID::Acid:
	case MonsterAIID::AcidUnique:
	case MonsterAIID::Zhar:
	case MonsterAIID::Lazarus:
	case MonsterAIID::Torchant:
	case MonsterAIID::Lich:
	case MonsterAIID::ArchLich:
	case MonsterAIID::Psychorb:
	case MonsterAIID::Necromorb:
	case MonsterAIID::BoneDemon:
		return true;
	default:
		return false;
	}
}

/** Objects worth counting: anything that can still be used or broken, but not doors, levers' walls or traps. */
bool CountsAsObject(const Object &object)
{
	if (object.isDoor() || object.IsTrap())
		return false;
	if (object.IsBarrel())
		return !object.IsBroken();
	return object.canInteractWith();
}

/** One letter per kind of object, for the map: b barrel, c chest, s shrine, x sarcophagus, o anything else. */
char ObjectLetter(const Object &object)
{
	if (object.IsBarrel())
		return 'b';
	if (object.IsChest())
		return 'c';
	if (object.IsShrine())
		return 's';
	if (object._otype == OBJ_SARC)
		return 'x';
	return 'o';
}

std::string ThemeName(theme_id theme)
{
	std::string name(magic_enum::enum_name(theme));
	if (name.starts_with("THEME_"))
		name.erase(0, 6);
	std::transform(name.begin(), name.end(), name.begin(), [](char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
	return name;
}

struct Room {
	int tiles = 0;
	std::bitset<MaxRooms> neighbours;
	/** Walking tiles from the up stairs to the nearest tile of the room; -1 when it can't be reached. */
	int distance = -1;
	bool onRoute = false;
	int monsters = 0;
	int ranged = 0;
	std::array<int, 4> rarity {};
	int groups = 0;
	int biggestGroup = 0;
	int barrels = 0;
	int chests = 0;
	int shrines = 0;
	int sarcophagi = 0;
	int otherObjects = 0;
	int items = 0;
	int gold = 0;
	std::string theme;

	// The visit.
	uint32_t ms = 0;
	int kills = 0;
	int visits = 0;

	[[nodiscard]] bool nook() const
	{
		return tiles < NookTiles;
	}
	[[nodiscard]] int doors() const
	{
		return static_cast<int>(neighbours.count());
	}
	[[nodiscard]] bool deadEnd() const
	{
		return doors() == 1;
	}
	/** Something worth walking in for besides monsters. */
	[[nodiscard]] bool reward() const
	{
		return chests + shrines + sarcophagi + items + gold > 0;
	}
	[[nodiscard]] bool empty() const
	{
		return monsters == 0 && barrels + otherObjects == 0 && !reward();
	}
	[[nodiscard]] bool propsOnly() const
	{
		return monsters == 0 && !reward() && barrels + otherObjects > 0;
	}
};

struct Spot {
	int x;
	int y;
	char kind;
	int rarity;
	bool ranged;
	int group;
};

/** The floor's numbers that go in the headline, the readable log and the floor checker's totals. */
struct Headline {
	int rooms = 0;
	int nooks = 0;
	int emptyRooms = 0;
	int propsOnly = 0;
	int singleMonster = 0;
	int deadEnds = 0;
	int emptyDeadEnds = 0;
	int biggestRoomShare = 0;
	int monsters = 0;
	int monsterTypes = 0;
	int ranged = 0;
	std::array<int, 4> rarity {};
	int groups = 0;
	/** Groups of 1, 2-3, 4-6, 7-11 and 12 or more. */
	std::array<int, 5> groupsBySize {};
	int chests = 0;
	int barrels = 0;
	int shrines = 0;
	int sarcophagi = 0;
	int otherObjects = 0;
	int items = 0;
	int themeRooms = 0;
	int routeLength = 0;
	int routeRooms = 0;
	int routeEmptyShare = 0;
	/** The longest stretch of the way from stairs to stairs with no monster within QuietSteps. */
	int longestEmptyWalk = 0;
	/** Share of the floor further than QuietSteps from every monster. */
	int quietFloorShare = 0;
};

struct Census {
	bool valid = false;
	/** The floor's name in the log, kept for when it's left (by then the next level is loaded). */
	std::string name;
	std::array<Room, MaxRooms> rooms;
	int walkable = 0;
	int minX = 0;
	int minY = 0;
	int maxX = -1;
	int maxY = -1;
	bool hasStart = false;
	bool hasEnd = false;
	Point start;
	Point end;
	std::vector<Point> route;
	std::vector<Spot> monsters;
	std::vector<Spot> objects;
	std::vector<Spot> items;
	Headline headline;
};

/** How the time on the floor was spent. */
struct Visit {
	uint32_t startMs = 0;
	uint32_t lastMs = 0;
	int room = -1;
	bool walkingBack = false;
	bool fightSeen = false;
	uint32_t firstFightMs = 0;
	uint32_t activeMs = 0;
	uint32_t fightMs = 0;
	uint32_t quietMs = 0;
	uint32_t walkBackMs = 0;
	uint32_t gapMs = 0;
	bool inGap = true;
	std::vector<uint32_t> gaps;
	std::vector<int> killSteps;
	int kills = 0;
	int peakNear = 0;
	int peakAdjacent = 0;
};

struct Batch {
	int left = 0;
	int total = 0;
	int levelA = 0;
	int levelB = 0;
	bool stepDue = false;
	bool waiting = false;
	std::vector<std::pair<int, Headline>> floors;
};

Census Current;
Visit CurrentVisit;
Batch FloorBatch;
bool BuiltPending = false;
bool CurrentBuilt = false;
int CurrentBatchIndex = -1;

int16_t Distance[MAXDUNX][MAXDUNY];
int16_t MonsterReach[MAXDUNX][MAXDUNY];
int8_t CameFrom[MAXDUNX][MAXDUNY];

constexpr std::array<Displacement, 8> Steps { {
    { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } } };

/** The nearest walkable tile to a stairs trigger (the stairs themselves are often solid). */
bool NearestWalkable(Point from, Point &found)
{
	for (int radius = 0; radius <= 3; radius++) {
		for (int dy = -radius; dy <= radius; dy++) {
			for (int dx = -radius; dx <= radius; dx++) {
				if (std::max(std::abs(dx), std::abs(dy)) != radius)
					continue;
				const Point tile = from + Displacement { dx, dy };
				if (Walkable(tile)) {
					found = tile;
					return true;
				}
			}
		}
	}
	return false;
}

/** Walking distance from the start to every tile (8 ways, no cutting corners), and the way back from each. */
void WalkFrom(Point start)
{
	for (auto &column : Distance)
		std::fill(std::begin(column), std::end(column), static_cast<int16_t>(-1));
	std::vector<Point> queue;
	queue.reserve(MAXDUNX * MAXDUNY / 2);
	queue.push_back(start);
	Distance[start.x][start.y] = 0;
	CameFrom[start.x][start.y] = -1;
	for (size_t head = 0; head < queue.size(); head++) {
		const Point tile = queue[head];
		for (int8_t i = 0; i < static_cast<int8_t>(Steps.size()); i++) {
			const Displacement step = Steps[i];
			const Point next = tile + step;
			if (!Walkable(next) || Distance[next.x][next.y] >= 0)
				continue;
			if (step.deltaX != 0 && step.deltaY != 0 && (!Walkable(tile + Displacement { step.deltaX, 0 }) || !Walkable(tile + Displacement { 0, step.deltaY })))
				continue;
			Distance[next.x][next.y] = static_cast<int16_t>(Distance[tile.x][tile.y] + 1);
			CameFrom[next.x][next.y] = i;
			queue.push_back(next);
		}
	}
}

/** Walking steps from every tile to the nearest monster, up to QuietSteps; -1 further than that. */
void MeasureMonsterReach(const Census &census)
{
	for (auto &column : MonsterReach)
		std::fill(std::begin(column), std::end(column), static_cast<int16_t>(-1));
	std::vector<Point> queue;
	for (const Spot &spot : census.monsters) {
		if (MonsterReach[spot.x][spot.y] < 0) {
			MonsterReach[spot.x][spot.y] = 0;
			queue.push_back({ spot.x, spot.y });
		}
	}
	for (size_t head = 0; head < queue.size(); head++) {
		const Point tile = queue[head];
		if (MonsterReach[tile.x][tile.y] >= QuietSteps)
			continue;
		for (const Displacement step : Steps) {
			const Point next = tile + step;
			if (!Walkable(next) || MonsterReach[next.x][next.y] >= 0)
				continue;
			if (step.deltaX != 0 && step.deltaY != 0 && (!Walkable(tile + Displacement { step.deltaX, 0 }) || !Walkable(tile + Displacement { 0, step.deltaY })))
				continue;
			MonsterReach[next.x][next.y] = static_cast<int16_t>(MonsterReach[tile.x][tile.y] + 1);
			queue.push_back(next);
		}
	}
}

int GroupSizeBucket(int size)
{
	if (size <= 1)
		return 0;
	if (size <= 3)
		return 1;
	if (size <= 6)
		return 2;
	if (size <= 11)
		return 3;
	return 4;
}

int Find(std::vector<int> &parent, int i)
{
	while (parent[i] != i) {
		parent[i] = parent[parent[i]];
		i = parent[i];
	}
	return i;
}

void CountMonsters(Census &census)
{
	std::bitset<256> types;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &monster = Monsters[ActiveMonsters[i]];
		if (!IsHostile(monster))
			continue;
		const Point tile = monster.position.tile;
		census.monsters.push_back({ tile.x, tile.y, 'm', RarityOf(monster), IsRanged(monster), -1 });
		types.set(monster.levelType);
	}
	census.headline.monsterTypes = static_cast<int>(types.count());

	// Groups: monsters within a few tiles of each other in the same room.
	const int count = static_cast<int>(census.monsters.size());
	std::vector<int> parent(count);
	std::iota(parent.begin(), parent.end(), 0);
	for (int a = 0; a < count; a++) {
		const Spot &first = census.monsters[a];
		for (int b = a + 1; b < count; b++) {
			const Spot &second = census.monsters[b];
			if (std::max(std::abs(first.x - second.x), std::abs(first.y - second.y)) > GroupTiles)
				continue;
			if (RoomAt({ first.x, first.y }) != RoomAt({ second.x, second.y }))
				continue;
			parent[Find(parent, a)] = Find(parent, b);
		}
	}
	std::vector<int> groupOf(count, -1);
	std::vector<int> sizes;
	for (int i = 0; i < count; i++) {
		const int root = Find(parent, i);
		if (groupOf[root] < 0) {
			groupOf[root] = static_cast<int>(sizes.size());
			sizes.push_back(0);
		}
		census.monsters[i].group = groupOf[root];
		sizes[groupOf[root]]++;
	}
	std::vector<bool> counted(sizes.size(), false);
	for (const Spot &spot : census.monsters) {
		Room &room = census.rooms[RoomAt({ spot.x, spot.y })];
		room.monsters++;
		room.rarity[spot.rarity]++;
		if (spot.ranged)
			room.ranged++;
		const int size = sizes[spot.group];
		room.biggestGroup = std::max(room.biggestGroup, size);
		if (!counted[spot.group]) {
			counted[spot.group] = true;
			room.groups++;
		}
	}
	Headline &headline = census.headline;
	headline.groups = static_cast<int>(sizes.size());
	for (const int size : sizes)
		headline.groupsBySize[GroupSizeBucket(size)]++;
}

void CountObjectsAndItems(Census &census)
{
	for (int i = 0; i < ActiveObjectCount; i++) {
		const Object &object = Objects[ActiveObjects[i]];
		if (!InDungeonBounds(object.position) || !CountsAsObject(object))
			continue;
		const char letter = ObjectLetter(object);
		census.objects.push_back({ object.position.x, object.position.y, letter, 0, false, -1 });
		Room &room = census.rooms[RoomAt(object.position)];
		switch (letter) {
		case 'b': room.barrels++; break;
		case 'c': room.chests++; break;
		case 's': room.shrines++; break;
		case 'x': room.sarcophagi++; break;
		default: room.otherObjects++; break;
		}
	}
	for (uint8_t i = 0; i < ActiveItemCount; i++) {
		const Item &item = Items[ActiveItems[i]];
		if (item.isEmpty() || !InDungeonBounds(item.position))
			continue;
		Room &room = census.rooms[RoomAt(item.position)];
		if (item._itype == ItemType::Gold) {
			room.gold++;
			continue;
		}
		room.items++;
		census.items.push_back({ item.position.x, item.position.y, 'i', 0, false, -1 });
	}
}

void FindStairs(Census &census)
{
	for (int i = 0; i < numtrigs; i++) {
		const interface_mode message = trigs[i]._tmsg;
		const bool up = IsAnyOf(message, WM_DIABPREVLVL, WM_DIABTWARPUP, WM_DIABRTNLVL);
		const bool down = message == WM_DIABNEXTLVL;
		Point tile;
		if (up && !census.hasStart && NearestWalkable(trigs[i].position, tile)) {
			census.start = tile;
			census.hasStart = true;
		}
		if (down && !census.hasEnd && NearestWalkable(trigs[i].position, tile)) {
			census.end = tile;
			census.hasEnd = true;
		}
	}
	// No way up (a quest map): the hero's own spot stands in.
	if (!census.hasStart && MyPlayer != nullptr && Walkable(MyPlayer->position.tile)) {
		census.start = MyPlayer->position.tile;
		census.hasStart = true;
	}
}

void MapRooms(Census &census)
{
	census.minX = MAXDUNX;
	census.minY = MAXDUNY;
	for (int y = 0; y < MAXDUNY; y++) {
		for (int x = 0; x < MAXDUNX; x++) {
			const Point tile { x, y };
			if (!Walkable(tile))
				continue;
			census.walkable++;
			census.minX = std::min(census.minX, x);
			census.minY = std::min(census.minY, y);
			census.maxX = std::max(census.maxX, x);
			census.maxY = std::max(census.maxY, y);
			const int id = RoomAt(tile);
			Room &room = census.rooms[id];
			room.tiles++;
			for (const Displacement step : { Displacement { 1, 0 }, Displacement { 0, 1 } }) {
				const Point next = tile + step;
				if (!Walkable(next))
					continue;
				const int other = RoomAt(next);
				if (other != id) {
					room.neighbours.set(other);
					census.rooms[other].neighbours.set(id);
				}
			}
			if (census.hasStart && Distance[x][y] >= 0 && (room.distance < 0 || Distance[x][y] < room.distance))
				room.distance = Distance[x][y];
		}
	}
	if (!census.hasStart || !census.hasEnd || Distance[census.end.x][census.end.y] < 0)
		return;
	Point tile = census.end;
	census.route.push_back(tile);
	while (CameFrom[tile.x][tile.y] >= 0) {
		tile -= Steps[CameFrom[tile.x][tile.y]];
		census.route.push_back(tile);
	}
	std::reverse(census.route.begin(), census.route.end());
	for (const Point step : census.route)
		census.rooms[RoomAt(step)].onRoute = true;
}

void Summarise(Census &census)
{
	Headline &headline = census.headline;
	if (!setlevel) {
		for (int i = 0; i < numthemes; i++) {
			if (themes[i].ttval < 0)
				continue;
			census.rooms[themes[i].ttval % MaxRooms].theme = ThemeName(themes[i].ttype);
			headline.themeRooms++;
		}
	}
	int biggest = 0;
	for (const Room &room : census.rooms) {
		if (room.tiles == 0)
			continue;
		headline.monsters += room.monsters;
		headline.ranged += room.ranged;
		for (size_t i = 0; i < room.rarity.size(); i++)
			headline.rarity[i] += room.rarity[i];
		headline.chests += room.chests;
		headline.barrels += room.barrels;
		headline.shrines += room.shrines;
		headline.sarcophagi += room.sarcophagi;
		headline.otherObjects += room.otherObjects;
		headline.items += room.items;
		biggest = std::max(biggest, room.tiles);
		if (room.nook()) {
			headline.nooks++;
			continue;
		}
		headline.rooms++;
		if (room.empty())
			headline.emptyRooms++;
		if (room.propsOnly())
			headline.propsOnly++;
		if (room.monsters == 1)
			headline.singleMonster++;
		if (room.deadEnd()) {
			headline.deadEnds++;
			if (room.monsters == 0 && !room.reward())
				headline.emptyDeadEnds++;
		}
		if (room.onRoute)
			headline.routeRooms++;
	}
	headline.biggestRoomShare = census.walkable > 0 ? biggest * 100 / census.walkable : 0;
	headline.routeLength = std::max(static_cast<int>(census.route.size()) - 1, 0);
	int run = 0;
	int emptyTiles = 0;
	for (const Point step : census.route) {
		if (MonsterReach[step.x][step.y] < 0) {
			run++;
			emptyTiles++;
			headline.longestEmptyWalk = std::max(headline.longestEmptyWalk, run);
		} else {
			run = 0;
		}
	}
	headline.routeEmptyShare = census.route.empty() ? 0 : emptyTiles * 100 / static_cast<int>(census.route.size());
	int quietTiles = 0;
	for (int x = census.minX; x <= census.maxX; x++) {
		for (int y = census.minY; y <= census.maxY; y++) {
			if (MonsterReach[x][y] < 0 && Walkable({ x, y }))
				quietTiles++;
		}
	}
	headline.quietFloorShare = census.walkable > 0 ? quietTiles * 100 / census.walkable : 0;
}

std::string FloorName();

void TakeCensus(Census &census)
{
	census = {};
	FindStairs(census);
	if (census.hasStart)
		WalkFrom(census.start);
	MapRooms(census);
	CountMonsters(census);
	CountObjectsAndItems(census);
	MeasureMonsterReach(census);
	Summarise(census);
	census.name = FloorName();
	census.valid = true;
}

bool OnDungeonFloor()
{
	return leveltype != DTYPE_TOWN;
}

/** The level's name in the log: L3, set2 for a quest map, map4-L3 for an endgame map. */
std::string FloorName()
{
	if (setlevel)
		return std::format("set{:d}", static_cast<int>(setlvlnum));
	if (const int tier = ActiveMapTier(); tier > 0)
		return std::format("map{:d}-L{:d}", tier, static_cast<int>(currlevel));
	return std::format("L{:d}", static_cast<int>(currlevel));
}

std::string HeadlineText(const Headline &headline)
{
	const std::array<int, 5> &sizes = headline.groupsBySize;
	return std::format("{:d} rooms ({:d} empty, {:d} with only barrels or props, {:d} with one monster), {:d} dead ends ({:d} with nothing in them), biggest room {:d}% of the floor; "
	                   "{:d} monsters of {:d} kinds in {:d} groups ({:d} alone, {:d} of 2-3, {:d} of 4-6, {:d} of 7-11, {:d} of 12 or more), {:d} magic, {:d} rare, {:d} unique, {:d} ranged; "
	                   "{:d} chests, {:d} barrels, {:d} shrines, {:d} sarcophagi, {:d} items, {:d} theme rooms; "
	                   "{:d}% of the floor more than {:d} steps from any monster; stairs to stairs {:d} tiles through {:d} rooms, {:d}% of the way with no monster that close, longest empty stretch {:d} tiles",
	    headline.rooms, headline.emptyRooms, headline.propsOnly, headline.singleMonster, headline.deadEnds, headline.emptyDeadEnds, headline.biggestRoomShare,
	    headline.monsters, headline.monsterTypes, headline.groups, sizes[0], sizes[1], sizes[2], sizes[3], sizes[4], headline.rarity[1], headline.rarity[2], headline.rarity[3], headline.ranged,
	    headline.chests, headline.barrels, headline.shrines, headline.sarcophagi, headline.items, headline.themeRooms,
	    headline.quietFloorShare, QuietSteps, headline.routeLength, headline.routeRooms, headline.routeEmptyShare, headline.longestEmptyWalk);
}

void AddHeadline(TelemetryEvent &event, const Headline &headline)
{
	JsonArray groups;
	for (const int count : headline.groupsBySize)
		groups.addRaw(std::to_string(count));
	event.add("rooms", headline.rooms)
	    .add("nooks", headline.nooks)
	    .add("emptyRooms", headline.emptyRooms)
	    .add("propsOnlyRooms", headline.propsOnly)
	    .add("singleMonsterRooms", headline.singleMonster)
	    .add("deadEnds", headline.deadEnds)
	    .add("emptyDeadEnds", headline.emptyDeadEnds)
	    .add("biggestRoomShare", headline.biggestRoomShare)
	    .add("monsters", headline.monsters)
	    .add("monsterTypes", headline.monsterTypes)
	    .add("ranged", headline.ranged)
	    .add("magic", headline.rarity[1])
	    .add("rare", headline.rarity[2])
	    .add("unique", headline.rarity[3])
	    .add("groups", headline.groups)
	    .addRaw("groupsBySize", groups.str())
	    .add("chests", headline.chests)
	    .add("barrels", headline.barrels)
	    .add("shrines", headline.shrines)
	    .add("sarcophagi", headline.sarcophagi)
	    .add("otherObjects", headline.otherObjects)
	    .add("items", headline.items)
	    .add("themeRooms", headline.themeRooms)
	    .add("routeLength", headline.routeLength)
	    .add("routeRooms", headline.routeRooms)
	    .add("routeEmptyShare", headline.routeEmptyShare)
	    .add("longestEmptyWalk", headline.longestEmptyWalk)
	    .add("quietFloorShare", headline.quietFloorShare);
}

/** Letters for the map, one per room in the order of the room list ('#' is wall, '+' past the end). */
constexpr std::string_view MapLetters = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!$%&'()*,-./:;<=>?@[]^_`{|}~";

void WriteCensus(const Census &census)
{
	const Headline &headline = census.headline;
	const int level = setlevel ? static_cast<int>(setlvlnum) : static_cast<int>(currlevel);
	const uint16_t visit = Poe2FloorVisits(level, setlevel);
	const uint32_t seed = setlevel ? 0 : DungeonSeeds[currlevel];
	TelemetryText("rooms", StrCat("floor ", FloorName(), " visit ", visit, CurrentBuilt ? " (built)" : " (as it was left)",
	                           CurrentBatchIndex >= 0 ? StrCat(", floor checker ", CurrentBatchIndex + 1, " of ", FloorBatch.total) : std::string(), ": ", HeadlineText(headline)));
	TelemetryEvent event = TelemetryEventNow(LogChannel::Rooms, LogLevel::Always, "floor_census");
	if (!event)
		return;
	event.add("p", static_cast<int>(MyPlayerId))
	    .add("dungeonLevel", static_cast<int>(currlevel))
	    .add("setLevel", setlevel ? static_cast<int>(setlvlnum) : -1)
	    .add("levelType", static_cast<int>(leveltype))
	    .add("mapTier", ActiveMapTier())
	    .add("seed", seed)
	    .add("visit", visit)
	    .add("built", CurrentBuilt)
	    .add("batch", CurrentBatchIndex)
	    .add("walkable", census.walkable);
	AddHeadline(event, headline);

	// The room list, in room number order; the map's letters are positions in this list.
	JsonArray rooms;
	std::array<int, MaxRooms> listIndex {};
	listIndex.fill(-1);
	int listed = 0;
	for (int id = 0; id < MaxRooms; id++) {
		const Room &room = census.rooms[id];
		if (room.tiles == 0)
			continue;
		listIndex[id] = listed++;
		std::string json = std::format(R"({{"id":{:d},"tiles":{:d},"doors":{:d},"dist":{:d},"route":{:s},"mon":{:d},"ranged":{:d},"magic":{:d},"rare":{:d},"unique":{:d},"groups":{:d},"bigGroup":{:d},"barrels":{:d},"chests":{:d},"shrines":{:d},"sarc":{:d},"other":{:d},"items":{:d},"gold":{:d},"theme":)",
		    id, room.tiles, room.doors(), room.distance, room.onRoute ? "true" : "false", room.monsters, room.ranged, room.rarity[1], room.rarity[2], room.rarity[3],
		    room.groups, room.biggestGroup, room.barrels, room.chests, room.shrines, room.sarcophagi, room.otherObjects, room.items, room.gold);
		AppendJsonString(json, room.theme);
		json += '}';
		rooms.addRaw(json);
	}
	event.addRaw("roomList", rooms.str());

	if (census.maxX >= census.minX) {
		JsonArray map;
		for (int y = census.minY; y <= census.maxY; y++) {
			std::string row;
			row.reserve(census.maxX - census.minX + 1);
			for (int x = census.minX; x <= census.maxX; x++) {
				const Point tile { x, y };
				if (!Walkable(tile)) {
					row += '#';
					continue;
				}
				const int index = listIndex[RoomAt(tile)];
				row += index >= 0 && index < static_cast<int>(MapLetters.size()) ? MapLetters[index] : '+';
			}
			map.add(row);
		}
		event.add("mapX", census.minX).add("mapY", census.minY).addRaw("map", map.str());
	}
	if (census.hasStart)
		event.addRaw("start", std::format("[{:d},{:d}]", census.start.x, census.start.y));
	if (census.hasEnd)
		event.addRaw("end", std::format("[{:d},{:d}]", census.end.x, census.end.y));
	JsonArray route;
	for (const Point step : census.route)
		route.addRaw(std::format("[{:d},{:d}]", step.x, step.y));
	event.addRaw("route", route.str());
	JsonArray monsters;
	for (const Spot &spot : census.monsters)
		monsters.addRaw(std::format("[{:d},{:d},{:d},{:d},{:d}]", spot.x, spot.y, spot.rarity, spot.ranged ? 1 : 0, spot.group));
	event.addRaw("monsterSpots", monsters.str());
	JsonArray objects;
	for (const Spot &spot : census.objects)
		objects.addRaw(std::format("[{:d},{:d},\"{:c}\"]", spot.x, spot.y, spot.kind));
	event.addRaw("objectSpots", objects.str());
	JsonArray items;
	for (const Spot &spot : census.items)
		items.addRaw(std::format("[{:d},{:d}]", spot.x, spot.y));
	event.addRaw("itemSpots", items.str());
}

/** A room with nothing left in it: no monsters alive, nothing to open or break, nothing on the floor. */
bool RoomIsDone(int id)
{
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &monster = Monsters[ActiveMonsters[i]];
		if (IsHostile(monster) && RoomAt(monster.position.tile) == id)
			return false;
	}
	for (int i = 0; i < ActiveObjectCount; i++) {
		const Object &object = Objects[ActiveObjects[i]];
		if (InDungeonBounds(object.position) && CountsAsObject(object) && RoomAt(object.position) == id)
			return false;
	}
	for (uint8_t i = 0; i < ActiveItemCount; i++) {
		const Item &item = Items[ActiveItems[i]];
		if (!item.isEmpty() && InDungeonBounds(item.position) && RoomAt(item.position) == id)
			return false;
	}
	return true;
}

void CloseGap(Visit &visit)
{
	if (visit.inGap && visit.gapMs > 0)
		visit.gaps.push_back(visit.gapMs);
	visit.gapMs = 0;
	visit.inGap = false;
}

double Seconds(uint32_t ms)
{
	return ms / 1000.0;
}

struct GapStats {
	uint32_t worst = 0;
	uint32_t p90 = 0;
	uint32_t mean = 0;
};

GapStats StatsOf(std::vector<uint32_t> gaps)
{
	GapStats stats;
	if (gaps.empty())
		return stats;
	std::sort(gaps.begin(), gaps.end());
	stats.worst = gaps.back();
	stats.p90 = gaps[std::min(gaps.size() - 1, gaps.size() * 9 / 10)];
	uint64_t total = 0;
	for (const uint32_t gap : gaps)
		total += gap;
	stats.mean = static_cast<uint32_t>(total / gaps.size());
	return stats;
}

int RoomsVisited()
{
	int visited = 0;
	for (const Room &room : Current.rooms) {
		if (!room.nook() && room.visits > 0)
			visited++;
	}
	return visited;
}

/** One line on the floor checker's floors so far, per floor. */
std::string BatchText()
{
	std::string text;
	for (const int level : { FloorBatch.levelA, FloorBatch.levelB }) {
		std::vector<Headline> floors;
		for (const auto &[floorLevel, headline] : FloorBatch.floors) {
			if (floorLevel == level)
				floors.push_back(headline);
		}
		if (floors.empty())
			continue;
		const auto average = [&](auto field) {
			double total = 0;
			for (const Headline &headline : floors)
				total += field(headline);
			return total / floors.size();
		};
		std::vector<int> longest;
		for (const Headline &headline : floors)
			longest.push_back(headline.longestEmptyWalk);
		std::sort(longest.begin(), longest.end());
		StrAppend(text, text.empty() ? "" : "\n",
		    std::format("L{:d}, {:d} floors, on average: {:.1f} rooms, {:.1f} empty, {:.1f} with only props, {:.1f} with one monster, {:.1f} empty dead ends, "
		                "{:.1f} monsters in {:.1f} groups ({:.1f} groups of 4-6), {:.1f} magic, {:.1f} rare, {:.1f} chests; {:.0f}% of the floor far from monsters, "
		                "stairs to stairs {:.0f} tiles, longest empty stretch {:.0f} tiles (worst {:d})",
		        level, static_cast<int>(floors.size()), average([](const Headline &h) { return h.rooms; }), average([](const Headline &h) { return h.emptyRooms; }),
		        average([](const Headline &h) { return h.propsOnly; }), average([](const Headline &h) { return h.singleMonster; }),
		        average([](const Headline &h) { return h.emptyDeadEnds; }), average([](const Headline &h) { return h.monsters; }),
		        average([](const Headline &h) { return h.groups; }), average([](const Headline &h) { return h.groupsBySize[2]; }),
		        average([](const Headline &h) { return h.rarity[1]; }), average([](const Headline &h) { return h.rarity[2]; }),
		        average([](const Headline &h) { return h.chests; }), average([](const Headline &h) { return h.quietFloorShare; }), average([](const Headline &h) { return h.routeLength; }),
		        average([](const Headline &h) { return h.longestEmptyWalk; }), longest.back()));
	}
	return text;
}

void FinishBatch()
{
	const std::string summary = BatchText();
	TelemetryText("rooms", StrCat("floor checker done, ", FloorBatch.total, " floors\n", summary));
	if (TelemetryEvent event = TelemetryEventNow(LogChannel::Rooms, LogLevel::Always, "floor_batch"); event) {
		event.add("floors", FloorBatch.total).add("levelA", FloorBatch.levelA).add("levelB", FloorBatch.levelB).add("summary", summary);
	}
	TelemetryFlush();
	EventPlrMsg(StrCat("Floor checker done: ", FloorBatch.total, " floors counted. rooms() shows the totals."));
	FloorBatch.left = 0;
	FloorBatch.stepDue = false;
	FloorBatch.waiting = false;
}

void StepBatch()
{
	if (MyPlayer == nullptr || MyPlayer->_pLvlChanging || gbIsMultiplayer)
		return;
	FloorBatch.stepDue = false;
	FloorBatch.waiting = true;
	const int target = currlevel == FloorBatch.levelA ? FloorBatch.levelB : FloorBatch.levelA;
	Poe2ForceFreshFloor = true;
	StartNewLvl(*MyPlayer, target > currlevel ? WM_DIABNEXTLVL : WM_DIABPREVLVL, target);
}

} // namespace

void LevelCensusNoteBuilt()
{
	BuiltPending = true;
}

void LevelCensusEntered()
{
	CurrentBuilt = BuiltPending;
	BuiltPending = false;
	CurrentBatchIndex = -1;
	if (FloorBatch.waiting) {
		FloorBatch.waiting = false;
		CurrentBatchIndex = FloorBatch.total - FloorBatch.left;
		FloorBatch.left--;
	}
	Current = {};
	CurrentVisit = {};
	if (!OnDungeonFloor() || !TelemetryEnabled()) {
		if (FloorBatch.left > 0)
			FloorBatch = {};
		return;
	}
	TakeCensus(Current);
	CurrentVisit.startMs = CurrentVisit.lastMs = Now();
	WriteCensus(Current);
	if (CurrentBatchIndex >= 0) {
		FloorBatch.floors.emplace_back(static_cast<int>(currlevel), Current.headline);
		if (FloorBatch.left > 0)
			FloorBatch.stepDue = true;
		else
			FinishBatch();
	}
}

void LevelCensusLeft(std::string_view how)
{
	if (!Current.valid)
		return;
	Visit &visit = CurrentVisit;
	CloseGap(visit);
	const uint32_t totalMs = visit.lastMs - visit.startMs;
	const GapStats gaps = StatsOf(visit.gaps);
	const int visited = RoomsVisited();
	// A floor the checker only passed through tells nothing about pacing.
	if (CurrentBatchIndex < 0) {
		TelemetryText("rooms", std::format("left {:s} after {:.0f} s ({:s}): {:.0f} s moving or fighting, {:.0f} s in fights, {:.0f} s quiet, {:s}, "
		                                   "{:d} quiet stretches (longest {:.0f} s, 90% under {:.0f} s, {:.0f} s on average), {:.0f} s walking back through emptied rooms, "
		                                   "{:d} of {:d} rooms seen, {:d} kills, at most {:d} monsters near and {:d} next to the hero",
		                                   Current.name, Seconds(totalMs), how, Seconds(visit.activeMs), Seconds(visit.fightMs), Seconds(visit.quietMs),
		                                   visit.fightSeen ? std::format("first fight after {:.0f} s", Seconds(visit.firstFightMs)) : std::string("no fights"), static_cast<int>(visit.gaps.size()), Seconds(gaps.worst), Seconds(gaps.p90),
		                                   Seconds(gaps.mean), Seconds(visit.walkBackMs), visited, Current.headline.rooms, visit.kills, visit.peakNear, visit.peakAdjacent));
		if (TelemetryEvent event = TelemetryEventNow(LogChannel::Rooms, LogLevel::Always, "floor_pacing"); event) {
			JsonArray gapList;
			for (const uint32_t gap : visit.gaps)
				gapList.addRaw(std::format("{:.1f}", Seconds(gap)));
			JsonArray killSteps;
			for (const int kills : visit.killSteps)
				killSteps.addRaw(std::to_string(kills));
			JsonArray rooms;
			for (int id = 0; id < MaxRooms; id++) {
				const Room &room = Current.rooms[id];
				if (room.visits > 0 || room.kills > 0)
					rooms.addRaw(std::format("[{:d},{:.1f},{:d},{:d}]", id, Seconds(room.ms), room.kills, room.visits));
			}
			event.add("p", static_cast<int>(MyPlayerId))
			    .add("how", how)
			    .add("seconds", Seconds(totalMs))
			    .add("activeSeconds", Seconds(visit.activeMs))
			    .add("fightSeconds", Seconds(visit.fightMs))
			    .add("quietSeconds", Seconds(visit.quietMs))
			    .add("firstFightSeconds", visit.fightSeen ? Seconds(visit.firstFightMs) : -1.0)
			    .add("quietStretches", static_cast<int>(visit.gaps.size()))
			    .add("longestQuiet", Seconds(gaps.worst))
			    .add("quietP90", Seconds(gaps.p90))
			    .add("quietMean", Seconds(gaps.mean))
			    .addRaw("quietList", gapList.str())
			    .add("walkBackSeconds", Seconds(visit.walkBackMs))
			    .add("roomsVisited", visited)
			    .add("rooms", Current.headline.rooms)
			    .add("kills", visit.kills)
			    .add("monstersAtEntry", Current.headline.monsters)
			    .add("peakNear", visit.peakNear)
			    .add("peakAdjacent", visit.peakAdjacent)
			    .addRaw("killsPer10s", killSteps.str())
			    .addRaw("roomTime", rooms.str());
		}
	}
	Current.valid = false;
}

void LevelCensusTick(const Player &player, bool inFight)
{
	if (FloorBatch.stepDue) {
		StepBatch();
		return;
	}
	if (!Current.valid || !InDungeonBounds(player.position.tile))
		return;
	Visit &visit = CurrentVisit;
	const uint32_t now = Now();
	const uint32_t tick = std::min(now - visit.lastMs, LongestTickMs);
	visit.lastMs = now;

	const int id = RoomAt(player.position.tile);
	Room &room = Current.rooms[id];
	if (id != visit.room) {
		visit.room = id;
		room.visits++;
		visit.walkingBack = room.visits > 1 && RoomIsDone(id);
	}
	room.ms += tick;

	const bool active = inFight || player._pmode != PM_STAND;
	if (active)
		visit.activeMs += tick;
	if (inFight) {
		visit.fightMs += tick;
		CloseGap(visit);
		if (!visit.fightSeen) {
			visit.fightSeen = true;
			visit.firstFightMs = now - visit.startMs;
		}
	} else {
		visit.inGap = true;
		if (active) {
			visit.quietMs += tick;
			visit.gapMs += tick;
			if (visit.walkingBack)
				visit.walkBackMs += tick;
		}
	}

	int near = 0;
	int adjacent = 0;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &monster = Monsters[ActiveMonsters[i]];
		if (!IsHostile(monster))
			continue;
		const int distance = player.position.tile.ExactDistance(monster.position.tile);
		if (distance <= NearTiles)
			near++;
		if (distance <= 1)
			adjacent++;
	}
	visit.peakNear = std::max(visit.peakNear, near);
	visit.peakAdjacent = std::max(visit.peakAdjacent, adjacent);
}

void LevelCensusKill(const Monster &monster)
{
	if (!Current.valid || !InDungeonBounds(monster.position.tile))
		return;
	Visit &visit = CurrentVisit;
	Current.rooms[RoomAt(monster.position.tile)].kills++;
	visit.kills++;
	const size_t step = (Now() - visit.startMs) / KillStepMs;
	if (visit.killSteps.size() <= step)
		visit.killSteps.resize(step + 1, 0);
	visit.killSteps[step]++;
}

std::string LevelCensusText()
{
	if (!OnDungeonFloor())
		return "rooms() counts dungeon floors: go down the stairs first.";
	Census census;
	TakeCensus(census);
	std::string text = StrCat("Floor ", FloorName(), " now: ", HeadlineText(census.headline));
	if (MyPlayer != nullptr && InDungeonBounds(MyPlayer->position.tile)) {
		const int id = RoomAt(MyPlayer->position.tile);
		const Room &room = census.rooms[id];
		StrAppend(text, std::format("\nYou're in room {:d}: {:d} tiles, {:d} doors, {:d} monsters, {:d} barrels, {:d} chests{:s}{:s}.", id, room.tiles, room.doors(), room.monsters,
		                    room.barrels, room.chests, room.theme.empty() ? "" : ", theme ", room.theme));
	}
	if (Current.valid) {
		const Visit &visit = CurrentVisit;
		const GapStats gaps = StatsOf(visit.gaps);
		StrAppend(text, std::format("\nThis visit: {:.0f} s, {:.0f} s in fights, {:.0f} s quiet (longest quiet stretch {:.0f} s, current {:.0f} s), {:.0f} s walking back, {:d} of {:d} rooms seen, {:d} kills.",
		                    Seconds(visit.lastMs - visit.startMs), Seconds(visit.fightMs), Seconds(visit.quietMs), Seconds(std::max(gaps.worst, visit.gapMs)), Seconds(visit.gapMs),
		                    Seconds(visit.walkBackMs), RoomsVisited(), Current.headline.rooms, visit.kills));
	} else if (!TelemetryEnabled()) {
		StrAppend(text, "\nThe play log is off, so this visit isn't being tracked.");
	}
	if (!FloorBatch.floors.empty())
		StrAppend(text, "\nFloor checker", FloorBatch.left > 0 ? StrCat(" (", FloorBatch.left, " floors to go)") : std::string(), ":\n", BatchText());
	return text;
}

std::string LevelCensusStartBatch(int floors)
{
	if (MyPlayer == nullptr)
		return "No hero loaded.";
	if (gbIsMultiplayer)
		return "The floor checker only runs in single player.";
	if (!TelemetryEnabled())
		return "The play log is off, so there's nowhere to write the counts.";
	if (FloorBatch.left > 0)
		return StrCat("The floor checker is already running: ", FloorBatch.left, " floors to go.");
	if (setlevel || ActiveMapTier() > 0 || currlevel < 1 || currlevel > 15)
		return "Stand on a normal floor between level 1 and 15 first.";
	if (floors < 1 || floors > 500)
		return "Give a number of floors from 1 to 500, like censusfloors(40).";
	FloorBatch = {};
	FloorBatch.left = floors;
	FloorBatch.total = floors;
	FloorBatch.levelA = currlevel;
	FloorBatch.levelB = currlevel < 15 ? currlevel + 1 : currlevel - 1;
	FloorBatch.stepDue = true;
	return std::format("Floor checker: walking between L{:d} and L{:d} {:d} times, building each floor fresh and counting it. Both floors are rebuilt for this hero (like Ctrl+clicking the stairs).",
	    FloorBatch.levelA, FloorBatch.levelB, floors);
}

} // namespace devilution
