#include "poe2/floor_packs.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <climits>
#include <cstdlib>
#include <vector>

#include "engine/direction.hpp"
#include "engine/point.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "interfac.h"
#include "items.h"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "levels/trigs.h"
#include "missiles.h"
#include "monster.h"
#include "objects.h"
#include "poe2/floor_chests.hpp"
#include "poe2/monster_mods.hpp"
#include "utils/is_of.hpp"

namespace devilution {

namespace {

constexpr int PackMin = 4;
constexpr int PackMax = 6;
constexpr int BigFightMin = 12;
constexpr int BigFightMax = 14;
/** A floor with this many monsters to place or more gets a second big fight. */
constexpr size_t SecondBigFightFrom = 80;
/** Archers and casters in one pack; more of them and a room can pin the hero down from the far side. */
constexpr int RangedPerPack = 3;
/** A pack stands within this many steps of its middle, a big fight within BigFightSteps. */
constexpr int PackSteps = 4;
constexpr int BigFightSteps = 6;
/** A pack's middle has this many free tiles of its room within 3 tiles of it where the floor allows. */
constexpr int PackOpenTiles = 16;
/** A big fight needs this many free tiles of its room within 3 tiles of its middle, so it isn't fought in a doorway. */
constexpr int BigFightOpenTiles = 24;
/** How far (in tiles each way) from its point on the way between the stairs a big fight may stand. */
constexpr int BigFightSearchTiles = 10;
/** Rooms with fewer free tiles than this are corners and door gaps (the floor census uses the same). */
constexpr int NookTiles = 6;
/** Tiles further than this from every monster all count as equally empty. */
constexpr int FarSteps = 40;
/** Counts as this many more steps from the nearest monster when the room has none yet. */
constexpr int EmptyRoomBonus = 10;
/** And this many when the tile is within QuietSteps of the way between the stairs, so the walk down meets fights. */
constexpr int RouteBonus = 8;
/** The floor census counts a tile further than this from every monster as quiet. */
constexpr int QuietSteps = 8;
/** A floor gets a strongbox one time in this many. */
constexpr int StrongboxOneIn = 3;
/** Out of every 100 other chests, this many are rare and this many magic. */
constexpr int RareChestsIn100 = 5;
constexpr int MagicChestsIn100 = 20;
/** A rest room after a big fight has this many free tiles: a room, not a corridor or a hall. */
constexpr int RestRoomMinTiles = 12;
constexpr int RestRoomMaxTiles = 150;
/** How far along the way down past a big fight its rest room may be. */
constexpr size_t RestRoomSearchSteps = 40;
/** The last pack stands this many steps further from the stairs down than the nearest spot at most, to find open floor. */
constexpr int LastPackSlack = 8;
/** dTransVal is an int8_t. */
constexpr int MaxRooms = 256;
constexpr int16_t Unreached = INT16_MAX;

constexpr std::array<Displacement, 8> Steps { { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } } };

/** Walking steps from the up stairs, and the step each tile was reached by (for the way between the stairs). */
int16_t FromStart[MAXDUNX][MAXDUNY];
int8_t CameFrom[MAXDUNX][MAXDUNY];
/** Walking steps from the nearest monster. */
int16_t Reach[MAXDUNX][MAXDUNY];
/** Walking steps from the way between the stairs. */
int16_t NearRoute[MAXDUNX][MAXDUNY];
/** Walking steps from the stairs down. */
int16_t FromEnd[MAXDUNX][MAXDUNY];

int RoomAt(Point tile)
{
	return static_cast<uint8_t>(dTransVal[tile.x][tile.y]);
}

/** Floor a monster can walk on; a closed door counts. */
bool Walkable(Point tile)
{
	if (!InDungeonBounds(tile))
		return false;
	if (IsTileNotSolid(tile))
		return true;
	const Object *object = FindObjectAtPosition(tile, false);
	return object != nullptr && object->isDoor();
}

/** The same rules as the old scatter: free floor, out of sight of the stairs, not in a theme room or set piece. */
bool CanHold(Point tile)
{
	return InDungeonBounds(tile)
	    && dMonster[tile.x][tile.y] == 0
	    && dPlayer[tile.x][tile.y] == 0
	    && !IsTileVisible(tile)
	    && !TileContainsSetPiece(tile)
	    && !IsTileOccupied(tile);
}

/** Where a chest may go: the rules chests already use, on this tile and every tile round it. */
bool CanHoldChest(Point tile)
{
	for (int dy = -1; dy <= 1; dy++) {
		for (int dx = -1; dx <= 1; dx++) {
			const Point p = tile + Displacement { dx, dy };
			if (!InDungeonBounds(p) || dMonster[p.x][p.y] != 0 || dPlayer[p.x][p.y] != 0 || IsObjectAtPosition(p)
			    || TileContainsSetPiece(p) || TileHasAny(p, TileProperties::Solid))
				return false;
			if (dPiece[p.x][p.y] > 125 && dPiece[p.x][p.y] < 143)
				return false;
		}
	}
	return !IsTileVisible(tile);
}

bool CanStep(Point from, Displacement step)
{
	const Point next = from + step;
	if (!Walkable(next))
		return false;
	// No cutting corners.
	return step.deltaX == 0 || step.deltaY == 0 || (Walkable(from + Displacement { step.deltaX, 0 }) && Walkable(from + Displacement { 0, step.deltaY }));
}

/** Lowers field to the walking steps from the nearest of the sources (tiles not reached keep their value). */
void WalkFrom(int16_t (&field)[MAXDUNX][MAXDUNY], const std::vector<Point> &sources, bool trackWay = false)
{
	std::vector<Point> queue;
	queue.reserve(1024);
	for (const Point source : sources) {
		if (field[source.x][source.y] == 0)
			continue;
		field[source.x][source.y] = 0;
		if (trackWay)
			CameFrom[source.x][source.y] = -1;
		queue.push_back(source);
	}
	for (size_t head = 0; head < queue.size(); head++) {
		const Point tile = queue[head];
		const int16_t next = static_cast<int16_t>(field[tile.x][tile.y] + 1);
		for (int8_t i = 0; i < static_cast<int8_t>(Steps.size()); i++) {
			if (!CanStep(tile, Steps[i]))
				continue;
			const Point to = tile + Steps[i];
			if (field[to.x][to.y] <= next)
				continue;
			field[to.x][to.y] = next;
			if (trackWay)
				CameFrom[to.x][to.y] = i;
			queue.push_back(to);
		}
	}
}

void Clear(int16_t (&field)[MAXDUNX][MAXDUNY])
{
	for (auto &column : field)
		std::fill(std::begin(column), std::end(column), Unreached);
}

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

struct RoomFacts {
	int free = 0;
	std::bitset<MaxRooms> neighbours;
	bool theme = false;
	bool stairs = false;
	bool reward = false;
	/** A rest room or a strongbox's room: no pack goes there. */
	bool quiet = false;
	int monsters = 0;

	[[nodiscard]] bool deadEnd() const
	{
		return neighbours.count() == 1 && !stairs && !theme && free >= NookTiles;
	}
};

class Planner {
public:
	Planner(size_t count, std::span<const size_t> types)
	    : rng_(static_cast<uint32_t>(AdvanceRndSeed()))
	    , types_(types)
	    , left_(count)
	{
	}

	size_t run()
	{
		if (types_.empty())
			return 0;
		studyFloor();
		placeBigFights();
		guardTheWayDown();
		placeStrongbox();
		guardDeadEnds();
		fillTheFloor();
		rateChests();
		return placed_;
	}

private:
	DiabloGenerator rng_;
	std::span<const size_t> types_;
	size_t left_;
	size_t placed_ = 0;
	std::array<RoomFacts, MaxRooms> rooms_ {};
	bool hasStart_ = false;
	bool hasEnd_ = false;
	Point end_;
	std::vector<Point> route_;

	int random(int below)
	{
		return rng_.generateRnd(below);
	}

	bool isRanged(size_t typeIndex) const
	{
		return IsRangedMonsterAi(LevelMonsterTypes[typeIndex].data().ai);
	}

	void studyFloor()
	{
		Clear(FromStart);
		Point start;
		for (int i = 0; i < numtrigs; i++) {
			const interface_mode message = trigs[i]._tmsg;
			Point tile;
			if (!NearestWalkable(trigs[i].position, tile))
				continue;
			rooms_[RoomAt(tile)].stairs = true;
			if (!hasStart_ && IsAnyOf(message, WM_DIABPREVLVL, WM_DIABTWARPUP, WM_DIABRTNLVL)) {
				start = tile;
				hasStart_ = true;
			} else if (!hasEnd_ && message == WM_DIABNEXTLVL) {
				end_ = tile;
				hasEnd_ = true;
			}
		}
		if (hasStart_)
			WalkFrom(FromStart, { start }, true);
		Clear(FromEnd);
		if (hasEnd_)
			WalkFrom(FromEnd, { end_ });
		if (hasStart_ && hasEnd_ && FromStart[end_.x][end_.y] != Unreached) {
			for (Point tile = end_; CameFrom[tile.x][tile.y] >= 0; tile -= Steps[CameFrom[tile.x][tile.y]])
				route_.push_back(tile);
			route_.push_back(start);
			std::reverse(route_.begin(), route_.end());
		}
		Clear(NearRoute);
		WalkFrom(NearRoute, route_);

		for (int y = 0; y < MAXDUNY; y++) {
			for (int x = 0; x < MAXDUNX; x++) {
				const Point tile { x, y };
				if (!Walkable(tile))
					continue;
				RoomFacts &room = rooms_[RoomAt(tile)];
				if (TileContainsSetPiece(tile))
					room.theme = true;
				if (CanHold(tile))
					room.free++;
				for (const Displacement step : { Displacement { 1, 0 }, Displacement { 0, 1 } }) {
					const Point next = tile + step;
					if (Walkable(next) && RoomAt(next) != RoomAt(tile)) {
						room.neighbours.set(RoomAt(next));
						rooms_[RoomAt(next)].neighbours.set(RoomAt(tile));
					}
				}
			}
		}
		for (int i = 0; i < ActiveObjectCount; i++) {
			const Object &object = Objects[ActiveObjects[i]];
			if (InDungeonBounds(object.position) && (object.IsChest() || object.IsShrine() || object._otype == OBJ_SARC))
				rooms_[RoomAt(object.position)].reward = true;
		}

		Clear(Reach);
		std::vector<Point> monsters;
		for (size_t i = 0; i < ActiveMonsterCount; i++) {
			const Monster &monster = Monsters[ActiveMonsters[i]];
			if (InDungeonBounds(monster.position.tile) && monster.position.tile != GolemHoldingCell && !monster.isPlayerMinion()) {
				monsters.push_back(monster.position.tile);
				rooms_[RoomAt(monster.position.tile)].monsters++;
			}
		}
		WalkFrom(Reach, monsters);
	}

	/** Free tiles of the tile's room within 3 tiles of it. */
	int openness(Point centre) const
	{
		int open = 0;
		for (int dy = -3; dy <= 3; dy++) {
			for (int dx = -3; dx <= 3; dx++) {
				const Point tile = centre + Displacement { dx, dy };
				if (CanHold(tile) && RoomAt(tile) == RoomAt(centre))
					open++;
			}
		}
		return open;
	}

	void placeBigFights()
	{
		if (route_.empty() || left_ < static_cast<size_t>(BigFightMin + PackMin))
			return;
		const int fights = left_ >= SecondBigFightFrom ? 2 : 1;
		for (int fight = 0; fight < fights && left_ >= static_cast<size_t>(BigFightMin); fight++) {
			// The first about two thirds of the way down, the second about a third.
			const int percent = fight == 0 ? 60 + random(21) : 30 + random(16);
			const size_t alongIndex = route_.size() * percent / 100;
			const Point along = route_[alongIndex];
			std::vector<Point> near;
			for (int y = std::max(0, along.y - BigFightSearchTiles); y <= std::min(MAXDUNY - 1, along.y + BigFightSearchTiles); y++) {
				for (int x = std::max(0, along.x - BigFightSearchTiles); x <= std::min(MAXDUNX - 1, along.x + BigFightSearchTiles); x++) {
					const Point tile { x, y };
					if (!CanHold(tile))
						continue;
					const RoomFacts &room = rooms_[RoomAt(tile)];
					// Two ways in, so the fight isn't held at one door; and not where a pack already stands.
					if (room.theme || room.neighbours.count() < 2 || Reach[x][y] < BigFightSteps * 2)
						continue;
					near.push_back(tile);
				}
			}
			int best = BigFightOpenTiles - 1;
			std::vector<Point> centres;
			for (const Point tile : near) {
				const int open = openness(tile);
				if (open > best) {
					best = open;
					centres.clear();
				}
				if (open == best)
					centres.push_back(tile);
			}
			if (centres.empty())
				continue;
			const Point centre = centres[random(static_cast<int>(centres.size()))];
			placePack(centre, BigFightMin + random(BigFightMax - BigFightMin + 1), BigFightSteps, MonsterRarity::Rare, 2);
			makeRestRoom(alongIndex, RoomAt(centre));
		}
	}

	/**
	 * After a big fight, a room stays empty with a shrine in it: the next room along the way down where one fits, or
	 * else a room off the fight's room.
	 */
	void makeRestRoom(size_t alongIndex, int fightRoom)
	{
		const auto canRest = [this, fightRoom](int r) {
			const RoomFacts &room = rooms_[r];
			return r != fightRoom && !room.theme && !room.stairs && room.monsters == 0 && !room.quiet && room.free >= RestRoomMinTiles && room.free <= RestRoomMaxTiles;
		};
		std::vector<int> candidates;
		for (size_t i = alongIndex; i < route_.size() && i < alongIndex + RestRoomSearchSteps; i++) {
			const int r = RoomAt(route_[i]);
			if (canRest(r) && std::find(candidates.begin(), candidates.end(), r) == candidates.end())
				candidates.push_back(r);
		}
		for (int r = 0; r < MaxRooms; r++) {
			if (rooms_[fightRoom].neighbours.test(r) && canRest(r) && std::find(candidates.begin(), candidates.end(), r) == candidates.end())
				candidates.push_back(r);
		}
		for (const int r : candidates) {
			if (placeShrine(r)) {
				rooms_[r].quiet = true;
				rooms_[r].reward = true;
				return;
			}
		}
	}

	/** A shrine against a wall of the room, with a candle each side, as the shrine theme room has them. */
	bool placeShrine(int r)
	{
		const auto inRoom = [r](Point tile) { return InDungeonBounds(tile) && RoomAt(tile) == r; };
		const auto clear = [](Point tile) { return IsTileNotSolid(tile) && !IsObjectAtPosition(tile) && dMonster[tile.x][tile.y] == 0; };
		std::vector<std::pair<Point, bool>> spots;
		for (int y = 1; y < MAXDUNY - 1; y++) {
			for (int x = 1; x < MAXDUNX - 1; x++) {
				const Point tile { x, y };
				if (!inRoom(tile) || !clear(tile) || TileContainsSetPiece(tile) || IsObjectAtPosition(tile + Direction::North))
					continue;
				// Facing south-west from a north-east wall, or south-east from a north-west wall.
				const Point left = tile + Displacement { -1, 0 };
				const Point right = tile + Displacement { 1, 0 };
				if (TileHasAny(tile + Direction::NorthEast, TileProperties::Trap) && IsTileNotSolid(tile + Direction::NorthWest)
				    && IsTileNotSolid(tile + Direction::SouthEast) && inRoom(left) && inRoom(right) && clear(left) && clear(right))
					spots.emplace_back(tile, true);
				const Point up = tile + Displacement { 0, -1 };
				const Point down = tile + Displacement { 0, 1 };
				if (TileHasAny(tile + Direction::NorthWest, TileProperties::Trap) && IsTileNotSolid(tile + Direction::NorthEast)
				    && IsTileNotSolid(tile + Direction::SouthWest) && inRoom(up) && inRoom(down) && clear(up) && clear(down))
					spots.emplace_back(tile, false);
			}
		}
		if (spots.empty())
			return false;
		const auto [tile, facingSouthWest] = spots[random(static_cast<int>(spots.size()))];
		if (facingSouthWest) {
			AddObject(OBJ_CANDLE2, tile + Displacement { -1, 0 });
			AddObject(OBJ_SHRINER, tile);
			AddObject(OBJ_CANDLE2, tile + Displacement { 1, 0 });
		} else {
			AddObject(OBJ_CANDLE2, tile + Displacement { 0, -1 });
			AddObject(OBJ_SHRINEL, tile);
			AddObject(OBJ_CANDLE2, tile + Displacement { 0, 1 });
		}
		return true;
	}

	/** A last full pack on the way down, as near the stairs down as the stairs' view allows. */
	void guardTheWayDown()
	{
		if (!hasEnd_ || left_ < static_cast<size_t>(PackMax + PackMin))
			return;
		int nearest = Unreached;
		for (int y = 0; y < MAXDUNY; y++) {
			for (int x = 0; x < MAXDUNX; x++) {
				if (CanHold({ x, y }) && !rooms_[RoomAt({ x, y })].quiet)
					nearest = std::min<int>(nearest, FromEnd[x][y]);
			}
		}
		if (nearest == Unreached)
			return;
		std::vector<Point> near;
		std::vector<Point> open;
		for (int y = 0; y < MAXDUNY; y++) {
			for (int x = 0; x < MAXDUNX; x++) {
				const Point tile { x, y };
				if (!CanHold(tile) || rooms_[RoomAt(tile)].quiet || FromEnd[x][y] > nearest + LastPackSlack || Reach[x][y] < PackSteps * 2)
					continue;
				near.push_back(tile);
				if (openness(tile) >= PackOpenTiles)
					open.push_back(tile);
			}
		}
		const std::vector<Point> &spots = open.empty() ? near : open;
		if (spots.empty())
			return;
		// The nearest of them to the stairs.
		Point centre = spots.front();
		for (const Point tile : spots) {
			if (FromEnd[tile.x][tile.y] < FromEnd[centre.x][centre.y])
				centre = tile;
		}
		placePack(centre, PackMax, PackSteps, MonsterRarity::Normal, 1);
	}

	/**
	 * Now and then a strongbox, off the way down: in a dead end where there is one, or else the room furthest from
	 * the way. Its guards come out of the floor's monsters.
	 */
	void placeStrongbox()
	{
		if (random(StrongboxOneIn) != 0)
			return;
		const int guards = PackMin + random(PackMax - PackMin + 1);
		if (left_ < static_cast<size_t>(guards + PackMin))
			return;
		int best = -1;
		std::vector<Point> spots;
		for (int y = 0; y < MAXDUNY; y++) {
			for (int x = 0; x < MAXDUNX; x++) {
				const Point tile { x, y };
				const RoomFacts &room = rooms_[RoomAt(tile)];
				if (room.theme || room.stairs || room.quiet || room.reward || room.monsters > 0 || room.free < NookTiles || !CanHoldChest(tile))
					continue;
				const int score = (room.deadEnd() ? FarSteps : 0) + std::min<int>(NearRoute[x][y], FarSteps);
				if (score > best) {
					best = score;
					spots.clear();
				}
				if (score == best)
					spots.push_back(tile);
			}
		}
		if (spots.empty())
			return;
		const Point tile = spots[random(static_cast<int>(spots.size()))];
		Object *strongbox = AddObject(OBJ_CHEST3, tile);
		if (strongbox == nullptr)
			return;
		SetFloorChestKind(*strongbox, FloorChestKind::Strongbox, guards);
		RoomFacts &room = rooms_[RoomAt(tile)];
		room.reward = true;
		room.quiet = true;
		left_ -= static_cast<size_t>(guards);
	}

	/** About one chest in five magic and one in twenty rare. */
	void rateChests()
	{
		for (int i = 0; i < ActiveObjectCount; i++) {
			Object &chest = Objects[ActiveObjects[i]];
			if (!chest.IsChest() || GetFloorChestKind(chest) != FloorChestKind::Normal)
				continue;
			const int roll = random(100);
			if (roll < RareChestsIn100)
				SetFloorChestKind(chest, FloorChestKind::Rare);
			else if (roll < RareChestsIn100 + MagicChestsIn100)
				SetFloorChestKind(chest, FloorChestKind::Magic);
		}
	}

	void guardDeadEnds()
	{
		// At most a quarter of what's left goes to dead ends, so the way through doesn't go short.
		size_t budget = left_ / 4;
		for (int r = 0; r < MaxRooms; r++) {
			RoomFacts &room = rooms_[r];
			// A chest, shrine or sarcophagus already pays for the walk in.
			if (!room.deadEnd() || room.monsters > 0 || room.reward || room.quiet)
				continue;
			const size_t size = static_cast<size_t>(PackMin + random(2));
			if (budget < size || left_ < size)
				break;
			// The far end of the room: furthest from the stairs up, or the middle of its free tiles without them.
			Point farEnd;
			int farthest = -1;
			Point chestSpot;
			int chestFarthest = -1;
			for (int y = 0; y < MAXDUNY; y++) {
				for (int x = 0; x < MAXDUNX; x++) {
					const Point tile { x, y };
					if (RoomAt(tile) != r || !CanHold(tile))
						continue;
					const int steps = hasStart_ && FromStart[x][y] != Unreached ? FromStart[x][y] : random(100);
					if (steps > farthest) {
						farthest = steps;
						farEnd = tile;
					}
					if (steps > chestFarthest && CanHoldChest(tile)) {
						chestFarthest = steps;
						chestSpot = tile;
					}
				}
			}
			if (farthest < 0)
				continue;
			// Pay for the walk in: a chest at the far end (a pack may still find its way there), or a pack led by a rare.
			if (chestFarthest >= 0 && random(2) == 0) {
				AddObject(random(3) == 0 ? OBJ_CHEST2 : OBJ_CHEST1, chestSpot);
				room.reward = true;
				continue;
			}
			budget -= placePack(farEnd, static_cast<int>(size), PackSteps, MonsterRarity::Rare, 1);
		}
	}

	/** How much a pack is wanted here: far from every monster, in a room with none yet, near the way down. */
	int fillScore(Point tile) const
	{
		const RoomFacts &room = rooms_[RoomAt(tile)];
		int score = std::min<int>(Reach[tile.x][tile.y], FarSteps);
		if (room.monsters == 0 && room.free >= NookTiles)
			score += EmptyRoomBonus;
		if (NearRoute[tile.x][tile.y] <= QuietSteps)
			score += RouteBonus;
		return score;
	}

	/** Packs grow from 4 by the stairs up to 6 by the stairs down. */
	int packSize(Point centre)
	{
		const int toEnd = hasEnd_ ? FromStart[end_.x][end_.y] : Unreached;
		const int steps = FromStart[centre.x][centre.y];
		// How far down the floor, in hundredths; half way where the stairs can't be walked between.
		const int progress = toEnd == Unreached || toEnd == 0 || steps == Unreached ? 50 : std::min(steps * 100 / toEnd, 100);
		return PackMin + (((PackMax - PackMin) * progress) + random(100)) / 100;
	}

	/** Each pack where it's wanted most (see fillScore), picked at random among the spots that score close to best. */
	void fillTheFloor()
	{
		while (left_ > 0) {
			std::vector<std::pair<Point, int>> spots;
			int best = -1;
			for (int y = 0; y < MAXDUNY; y++) {
				for (int x = 0; x < MAXDUNX; x++) {
					const Point tile { x, y };
					if (!CanHold(tile) || rooms_[RoomAt(tile)].quiet)
						continue;
					const int score = fillScore(tile);
					best = std::max(best, score);
					spots.emplace_back(tile, score);
				}
			}
			std::erase_if(spots, [best](const std::pair<Point, int> &spot) { return spot.second < best - 1; });
			if (spots.empty())
				return;
			// Off the walls where the floor allows, so a pack stands in its room rather than in a corner of it.
			if (std::any_of(spots.begin(), spots.end(), [this](const std::pair<Point, int> &spot) { return openness(spot.first) >= PackOpenTiles; }))
				std::erase_if(spots, [this](const std::pair<Point, int> &spot) { return openness(spot.first) < PackOpenTiles; });
			const Point centre = spots[random(static_cast<int>(spots.size()))].first;
			// Packs of 4 to 6; what's left under a pack goes out as stragglers.
			const int size = left_ >= static_cast<size_t>(PackMin) ? packSize(centre) : 1;
			if (placePack(centre, size, PackSteps, MonsterRarity::Normal, 1) == 0)
				return;
		}
	}

	/**
	 * Places up to size monsters within steps of the centre, in its room, nearest first with some gaps. One type makes
	 * up most of the pack; up to extraTypes other types of the floor make up about a third.
	 * @return How many were placed.
	 */
	size_t placePack(Point centre, int size, int steps, MonsterRarity leader, int extraTypes)
	{
		size = static_cast<int>(std::min<size_t>(static_cast<size_t>(size), left_));
		const int room = RoomAt(centre);
		std::vector<Point> tiles;
		std::vector<int16_t> distance(static_cast<size_t>(MAXDUNX * MAXDUNY), -1);
		const auto at = [&](Point p) -> int16_t & { return distance[static_cast<size_t>(p.x * MAXDUNY + p.y)]; };
		std::vector<Point> queue { centre };
		at(centre) = 0;
		for (size_t head = 0; head < queue.size(); head++) {
			const Point tile = queue[head];
			if (CanHold(tile))
				tiles.push_back(tile);
			if (at(tile) >= steps)
				continue;
			for (const Displacement step : Steps) {
				const Point next = tile + step;
				if (!CanStep(tile, step) || at(next) >= 0 || RoomAt(next) != room)
					continue;
				at(next) = static_cast<int16_t>(at(tile) + 1);
				queue.push_back(next);
			}
		}
		// Nearest first, but skip about one tile in three so a pack doesn't stand shoulder to shoulder.
		std::vector<Point> chosen;
		for (int pass = 0; pass < 2 && static_cast<int>(chosen.size()) < size; pass++) {
			for (const Point tile : tiles) {
				if (static_cast<int>(chosen.size()) >= size)
					break;
				if (std::find(chosen.begin(), chosen.end(), tile) != chosen.end())
					continue;
				if (pass == 0 && tile != centre && random(3) == 0)
					continue;
				chosen.push_back(tile);
			}
		}

		const size_t main = types_[random(static_cast<int>(types_.size()))];
		std::vector<size_t> others;
		const size_t offset = static_cast<size_t>(random(static_cast<int>(types_.size())));
		for (size_t i = 0; i < types_.size(); i++) {
			const size_t type = types_[(offset + i) % types_.size()];
			if (type != main && static_cast<int>(others.size()) < extraTypes)
				others.push_back(type);
		}
		if (!others.empty() && extraTypes == 1 && random(2) == 0)
			others.clear();
		size_t melee = types_.size();
		for (const size_t type : types_) {
			if (!isRanged(type)) {
				melee = type;
				break;
			}
		}

		std::vector<Point> newTiles;
		Monster *first = nullptr;
		int ranged = 0;
		for (size_t i = 0; i < chosen.size(); i++) {
			// About one in three of the pack is another type.
			size_t type = !others.empty() && i % 3 == 2 ? others[(i / 3) % others.size()] : main;
			if (isRanged(type) && ranged >= RangedPerPack && melee < types_.size())
				type = melee;
			if (isRanged(type))
				ranged++;
			Monster *monster = AddMonster(chosen[i], static_cast<Direction>(random(8)), type, true);
			if (monster == nullptr)
				break;
			// Rares lead the big fights and guard dead ends; one more rolled at random would add loot on top.
			if (GetMonsterAffixes(*monster).rarity == MonsterRarity::Rare)
				DemoteMonsterRarity(*monster);
			if (first == nullptr && GetMonsterAffixes(*monster).rarity == MonsterRarity::Normal)
				first = monster;
			newTiles.push_back(chosen[i]);
			rooms_[RoomAt(chosen[i])].monsters++;
		}
		if (first != nullptr && leader != MonsterRarity::Normal)
			PromoteMonsterRarity(*first, leader);
		left_ -= newTiles.size();
		placed_ += newTiles.size();
		WalkFrom(Reach, newTiles);
		return newTiles.size();
	}
};

} // namespace

bool IsRangedMonsterAi(MonsterAIID ai)
{
	switch (ai) {
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

bool UseFloorPacks()
{
	return gbIsPoe2 && !setlevel && leveltype == DTYPE_CATHEDRAL;
}

size_t PlaceFloorPacks(size_t count, std::span<const size_t> scatterTypes)
{
	if (count == 0)
		return 0;
	Planner planner(count, scatterTypes);
	return planner.run();
}

void PlaceThemePack(int8_t room, size_t typeIndex, int frequency)
{
	const auto eligible = [room](Point tile) {
		return InDungeonBounds(tile) && dTransVal[tile.x][tile.y] == room && IsTileNotSolid(tile) && dItem[tile.x][tile.y] == 0
		    && !IsObjectAtPosition(tile) && dMonster[tile.x][tile.y] == 0;
	};
	int count = 0;
	int sumX = 0;
	int sumY = 0;
	for (int y = 0; y < MAXDUNY; y++) {
		for (int x = 0; x < MAXDUNX; x++) {
			if (!eligible({ x, y }))
				continue;
			count++;
			sumX += x;
			sumY += y;
		}
	}
	if (count == 0)
		return;
	// The middle: the free tile nearest the average of them all.
	const Point average { sumX / count, sumY / count };
	Point centre;
	int nearest = INT_MAX;
	for (int y = 0; y < MAXDUNY; y++) {
		for (int x = 0; x < MAXDUNX; x++) {
			const int d = std::abs(x - average.x) + std::abs(y - average.y);
			if (d < nearest && eligible({ x, y })) {
				nearest = d;
				centre = { x, y };
			}
		}
	}
	// About as many as the old one-per-so-many-tiles scatter gave, but always a real pack.
	const int size = std::min(count, std::clamp((count + (frequency / 2)) / std::max(frequency, 1), PackMin, PackMax + 2));
	std::vector<Point> queue { centre };
	std::vector<bool> seen(static_cast<size_t>(MAXDUNX * MAXDUNY), false);
	seen[static_cast<size_t>(centre.x * MAXDUNY + centre.y)] = true;
	int placed = 0;
	for (size_t head = 0; head < queue.size() && placed < size; head++) {
		const Point tile = queue[head];
		if (eligible(tile)) {
			if (AddMonster(tile, static_cast<Direction>(GenerateRnd(8)), typeIndex, true) == nullptr)
				return;
			placed++;
		}
		for (const Displacement step : Steps) {
			const Point next = tile + step;
			if (!InDungeonBounds(next) || seen[static_cast<size_t>(next.x * MAXDUNY + next.y)] || dTransVal[next.x][next.y] != room || !IsTileNotSolid(next))
				continue;
			seen[static_cast<size_t>(next.x * MAXDUNY + next.y)] = true;
			queue.push_back(next);
		}
	}
}

} // namespace devilution
