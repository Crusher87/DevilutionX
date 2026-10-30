/**
 * @file poe2/cathedral_floors.hpp
 *
 * POE2 Mashup: the new Cathedral floors (dungeon plan step 5, "the funnel"; Curtis picked it on 2026-10-29).
 *
 * Diablo builds a Cathedral floor as one big tangle of rooms with the stairs anywhere. Path of Exile 2 builds a winding
 * main way from the way in to the way out, with side areas that end in a reward. This builder does the same on
 * Diablo's own tiles:
 *  - The floor is a 6 x 6 grid of slots, each 6 blocks wide: one block of wall and 5 of floor. A room takes one slot
 *    (5 x 5), two (11 x 5) or four (11 x 11, a great hall). Slots no room uses stay solid rock.
 *  - The main way runs from the arrival room (the stairs up) through about 8 rooms to the way down. The two big
 *    fights are great halls about a third and two thirds of the way along, each followed by a quiet room.
 *  - Side areas of one or two rooms branch off the main way and end in a reward room. Now and then two rooms of the
 *    main way that touch get a second doorway, so there is a way round.
 *  - About a third of the rooms are designed rooms from the collection Curtis picked (mods/poe2/txtdata/poe2/
 *    cathedral_rooms.tsv); the rest are rough rooms and winding passages made up on the spot.
 *  - Rooms join through one-block doorways: arches on the main way, sometimes a door into a side area.
 *  - The quest rooms keep their own layouts: the Butcher's room, the Skeleton King's door and Ogden's sign go in a
 *    great hall (Ogden's sign is the way down on its floor), the Poisoned Water entrance in a side hall.
 *  - A room never comes back on the same floor or the next two floors built; the memorable ones wait five floors.
 *    The rooms each floor had to avoid are saved with the floor (poe2/floors.hpp), so a floor built again from its
 *    seed comes out the same.
 *
 * Old floors stay one option away (Gameplay > New Cathedral Floors). A floor keeps the builder it was made with.
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "engine/point.hpp"
#include "engine/rectangle.hpp"
#include "levels/gendung_defs.hpp"
#include "utils/bitset2d.hpp"

namespace devilution {

/** @brief What a designed room is good for (bit flags). */
enum CathedralRoomRole : uint8_t {
	RoomRoleBigFight = 1,
	RoomRoleMainWay = 2,
	RoomRoleSideArea = 4,
	RoomRoleQuiet = 8,
	RoomRoleJunction = 16,
};

/**
 * @brief One designed room, drawn on the block grid.
 * ' ' rock, '.' floor, 'E' where a doorway may open, 'O' pillar, 'U' stairs up, 'V' stairs down, '*' a reward,
 * and props: c candles, L brazier, S sarcophagus, K bookshelf, k book stand, A shrine spot, F fountain, G goat shrine,
 * X crucified skeleton, x burning cross, T man or woman in chains, Y body on a stake, D headless body, P and s skulls,
 * b banner, r barrels, and lines inside the room: '#' wall, 'a' arches, '=' fence, 'd' a door in a wall or fence line,
 * 'R' broken wall.
 */
struct CathedralRoomDef {
	std::string id;
	std::string family;
	uint8_t roles = 0;
	bool memorable = false;
	/** The room only turns ways that keep its pillars behind the middle, so they never hide the fight. */
	bool pillarsBehind = false;
	/** What its reward spot holds on its first version: "", "strongbox" or "raremonster". */
	std::string reward;
	/** Optional parts: a digit in the grid, the square when it is used and when it isn't. */
	std::vector<std::array<char, 3>> options;
	std::vector<std::string> grid;
};

/** @brief Reads a grid written as rows joined by '/', with '_' for rock. False when the rows aren't all one width. */
bool ParseCathedralRoomGrid(std::string_view text, std::vector<std::string> &grid);

/** @brief The room collection, read from cathedral_rooms.tsv on first use. */
const std::vector<CathedralRoomDef> &GetCathedralRooms();

/** @brief Replaces the room collection (tests); an empty list reads the file again on next use. */
void SetCathedralRooms(std::vector<CathedralRoomDef> rooms);

/** @brief A grid turned: turn & 3 quarter turns clockwise, then mirrored left to right when turn & 4. */
std::vector<std::string> TurnCathedralGrid(const std::vector<std::string> &grid, int turn);

enum class CathedralAreaKind : uint8_t {
	Arrival,
	Way,
	BigFight,
	Quiet,
	WayDown,
	Side,
	SideEnd,
	QuestPiece,
	QuestWater,
};

struct CathedralArea {
	/** In slots. */
	Rectangle slots;
	CathedralAreaKind kind = CathedralAreaKind::Way;
	/** Order along the main way, or -1 off it. */
	int step = -1;
	/** Index into GetCathedralRooms(), or -1 for a room made up on the spot. */
	int design = -1;
	int turn = 0;
	/** The designed room as placed: turned, its options settled. */
	std::vector<std::string> grid;
	/** The block of grid[0][0]. */
	Point origin;
	bool corridor = false;

	/** @brief The area's floor blocks: every block of its slots but the wall row and column on its north and west. */
	[[nodiscard]] Rectangle interior() const;
};

struct CathedralDoor {
	Point block;
	/** A doorway in a wall running north to south (between an area and the one east of it). */
	bool vertical = false;
	/** A door rather than an open arch. */
	bool door = false;
};

/** A block of a wall, arch, fence, door or broken-wall line inside a designed room ('#', 'a', '=', 'd', 'R'). */
struct CathedralLine {
	Point block;
	char kind = '#';
};

struct CathedralProp {
	Point block;
	char kind = '.';
	int area = 0;
};

struct CathedralPlan {
	std::vector<CathedralArea> areas;
	std::vector<CathedralDoor> doors;
	std::vector<CathedralProp> props;
	std::vector<Point> pillars;
	/** Lines inside designed rooms: rock in the mask, turned into their tiles once the walls are in. */
	std::vector<CathedralLine> lines;
	Bitset2d<DMAXX, DMAXY> mask;
	/** Blocks of designed rooms, kept free of random objects and theme rooms. */
	Bitset2d<DMAXX, DMAXY> designed;
	/** The first of the four 'U' blocks; the stairs go in the wall north of them. */
	std::optional<Point> stairsUp;
	/** The north-west 'V' block. */
	std::optional<Point> stairsDown;
	/** Where the quest's set piece goes. */
	std::optional<Point> questPiece;
	/** The north-west block of the Poisoned Water entrance (6 x 6). */
	std::optional<Point> water;
	/** Bit i: room i of the collection is on this floor. */
	uint64_t usedRooms = 0;
};

struct CathedralRequest {
	/** Bit i: room i of the collection may not be used. */
	uint64_t forbidden = 0;
	bool questPiece = false;
	/** Ogden's sign: the set piece holds the stairs down, so it ends the main way. */
	bool questPieceIsWayDown = false;
	bool water = false;
};

/**
 * @brief Plans a floor with the level's random numbers (GenerateRnd).
 * @return false when this roll didn't work out; the caller rolls again.
 */
bool PlanCathedralFloor(const CathedralRequest &request, CathedralPlan &plan);

/** @brief One line for the play log: each room of the plan, its kind, slots and design, and where the stairs are. */
std::string DescribeCathedralPlan(const CathedralPlan &plan);

/** @brief Which rooms a new floor may not use, from the rooms the last five floors built used (newest first). */
uint64_t CathedralRoomsToAvoid(std::span<const uint64_t> recent);

/** @brief Whether the floor being played was made by this builder. */
bool IsCathedralFunnelFloor();
void SetCathedralFunnelFloor(bool active);
CathedralPlan &CurrentCathedralPlan();

/** @brief A world tile of a designed room (no random objects, no theme room). */
bool IsCathedralDesignedTile(Point tile);
/**
 * @brief Whether a transparency area (dTransVal) is a plain side room of a new Cathedral floor. Those take Diablo's
 * theme rooms (library, fountain, goat shrine, ...); the designed rooms, the main way and the winding passages don't.
 */
bool IsCathedralThemeRoom(int8_t tv);
/**
 * @brief The room of a new Cathedral floor a tile is in (1 + its area), for the pack planner and the floor census; 0 off
 * every room. A room cut up by lines of arches or walls is still one room.
 */
int CathedralRoomAt(Point tile);
/** @brief The big-fight halls along the main way, each with the quiet room after it (rooms as CathedralRoomAt; 0: none). */
std::vector<std::pair<int, int>> CathedralBigFights();
/** A designed room's reward spot that holds more than a chest (the reward column of the rooms list). */
struct CathedralReward {
	Point tile;
	/** A strongbox stands there; otherwise a pack led by a rare monster is to stand there. */
	bool strongbox = false;
};
/** @brief The reward spots AddCathedralRoomObjects set out on this floor, for the pack planner. */
const std::vector<CathedralReward> &CathedralRewards();
/** @brief The designed rooms' sarcophagi, taken from the floor's usual 10 to 15. */
int CathedralDesignedSarcophagi();
/** @brief Marks the object art the designed rooms use, for InitObjectGFX. */
void MarkCathedralObjectGraphics(uint16_t filesWidths[65]);
/** @brief Puts the designed rooms' props on the floor (InitObjects, before any random object). */
void AddCathedralRoomObjects();

} // namespace devilution
