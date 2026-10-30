#include "poe2/cathedral_floors.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <deque>
#include <utility>

#include "data/file.hpp"
#include "data/record_reader.hpp"
#include "engine/random.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "objects.h"
#include "tables/objdat.h"
#include "utils/is_of.hpp"
#include "utils/log.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

/** The floor is Slots x Slots slots of SlotSize blocks; the first slot's wall is at block FirstSlot. */
constexpr int Slots = 6;
constexpr int SlotSize = 6;
constexpr int FirstSlot = 2;

/** Main way length in rooms, counting the arrival room and the way down. */
constexpr int MainWayMin = 8;
constexpr int MainWayExtra = 3;
/** Side areas tried per floor. */
constexpr int SidesMin = 3;
constexpr int SidesExtra = 3;
/** Most rooms of one family on a floor. */
constexpr int FamilyMost = 2;
/** Room rotations kept for each turn of history: normal rooms skip 2 floors, memorable ones 5. */
constexpr size_t RecentNormal = 2;
constexpr size_t RecentMemorable = 5;

std::vector<CathedralRoomDef> Rooms;
bool RoomsLoaded = false;

bool FunnelFloor = false;
CathedralPlan CurrentPlan;

enum Side : uint8_t {
	North,
	East,
	South,
	West,
};

constexpr std::array<Displacement, 4> SideStep { Displacement { 0, -1 }, Displacement { 1, 0 }, Displacement { 0, 1 }, Displacement { -1, 0 } };

Side Opposite(Side side)
{
	return static_cast<Side>((side + 2) % 4);
}

uint8_t ParseRoles(std::string_view text)
{
	uint8_t roles = 0;
	while (!text.empty()) {
		const size_t comma = text.find(',');
		const std::string_view role = text.substr(0, comma);
		if (role == "big")
			roles |= RoomRoleBigFight;
		else if (role == "main")
			roles |= RoomRoleMainWay;
		else if (role == "side")
			roles |= RoomRoleSideArea;
		else if (role == "quiet")
			roles |= RoomRoleQuiet;
		else if (role == "junction")
			roles |= RoomRoleJunction;
		if (comma == std::string_view::npos)
			break;
		text.remove_prefix(comma + 1);
	}
	return roles;
}

void LoadRooms()
{
	RoomsLoaded = true;
	Rooms.clear();
	constexpr std::string_view Filename = "txtdata\\poe2\\cathedral_rooms.tsv";
	auto dataFile = DataFile::load(Filename);
	if (!dataFile.has_value()) {
		LogError("Couldn't load the Cathedral rooms: {}", Filename);
		return;
	}
	dataFile->skipHeaderOrDie(Filename);
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		CathedralRoomDef room;
		std::string roles;
		std::string options;
		std::string grid;
		int memorable = 0;
		int pillarsBehind = 0;
		reader.readString("id", room.id);
		reader.readString("family", room.family);
		reader.readString("roles", roles);
		reader.readInt("memorable", memorable);
		reader.readInt("pillarsBehind", pillarsBehind);
		reader.readString("reward", room.reward);
		reader.readString("options", options);
		reader.readString("grid", grid);
		room.roles = ParseRoles(roles);
		room.memorable = memorable != 0;
		room.pillarsBehind = pillarsBehind != 0;
		for (size_t i = 0; i + 2 < options.size(); i += 4)
			room.options.push_back({ options[i], options[i + 1], options[i + 2] });
		if (!ParseCathedralRoomGrid(grid, room.grid)) {
			LogError("Cathedral room {} has a ragged grid", room.id);
			continue;
		}
		if (Rooms.size() >= 64) {
			LogError("Only 64 Cathedral rooms fit the room history; {} is left out", room.id);
			continue;
		}
		Rooms.push_back(std::move(room));
	}
}

int FindRoom(std::string_view id)
{
	const std::vector<CathedralRoomDef> &rooms = GetCathedralRooms();
	for (size_t i = 0; i < rooms.size(); i++) {
		if (rooms[i].id == id)
			return static_cast<int>(i);
	}
	return -1;
}

bool IsLine(char c)
{
	return c == '#' || c == 'a' || c == '=' || c == 'd' || c == 'R';
}

/** Arches, doors and broken walls can be walked through; walls and fences can't. */
bool LineIsOpen(char c)
{
	return c == 'a' || c == 'd' || c == 'R';
}

char At(const std::vector<std::string> &grid, int x, int y)
{
	if (y < 0 || y >= static_cast<int>(grid.size()) || x < 0 || x >= static_cast<int>(grid[y].size()))
		return ' ';
	return grid[y][x];
}

/** Shrine spots, stairs up, hanging bodies and burning crosses need a wall north or west of them. */
bool BackedOk(const std::vector<std::string> &grid)
{
	for (int y = 0; y < static_cast<int>(grid.size()); y++) {
		for (int x = 0; x < static_cast<int>(grid[y].size()); x++) {
			const char c = grid[y][x];
			if (c != 'A' && c != 'T' && c != 'x' && c != 'U')
				continue;
			if (c == 'U') {
				// The stairs up go into the wall north of the room's first row.
				if (y != 0)
					return false;
				continue;
			}
			if (At(grid, x - 1, y) != ' ' && At(grid, x, y - 1) != ' ')
				return false;
		}
	}
	return true;
}

/** The stairs down need their four squares side by side, at the back of the room, with floor in front. */
bool StairsDownOk(const std::vector<std::string> &grid)
{
	for (int y = 0; y < static_cast<int>(grid.size()); y++) {
		for (int x = 0; x < static_cast<int>(grid[y].size()); x++) {
			if (grid[y][x] != 'V' || At(grid, x - 1, y) == 'V' || At(grid, x, y - 1) == 'V')
				continue;
			for (int i = 0; i < 4; i++) {
				if (At(grid, x + i, y) != 'V' || At(grid, x + i, y + 1) != 'V' || At(grid, x + i, y + 2) == ' ' || At(grid, x + i, y + 2) == 'V')
					return false;
			}
		}
	}
	return true;
}

/** No pillar stands in front of the room's middle, nearer the camera and in line with it. */
bool PillarsBehindOk(const std::vector<std::string> &grid)
{
	int mx = -1;
	int my = -1;
	for (int y = 0; y < static_cast<int>(grid.size()); y++) {
		for (int x = 0; x < static_cast<int>(grid[y].size()); x++) {
			if (grid[y][x] == '*') {
				mx = x;
				my = y;
			}
		}
	}
	if (mx < 0) {
		mx = static_cast<int>(grid[0].size()) / 2;
		my = static_cast<int>(grid.size()) / 2;
	}
	for (int y = 0; y < static_cast<int>(grid.size()); y++) {
		for (int x = 0; x < static_cast<int>(grid[y].size()); x++) {
			if (grid[y][x] == 'O' && x + y > mx + my && std::abs((x - y) - (mx - my)) < 3)
				return false;
		}
	}
	return true;
}

Rectangle InteriorOf(Rectangle slots)
{
	return { { FirstSlot + (slots.position.x * SlotSize) + 1, FirstSlot + (slots.position.y * SlotSize) + 1 },
		{ (slots.size.width * SlotSize) - 1, (slots.size.height * SlotSize) - 1 } };
}

Point CentreOf(Rectangle r)
{
	return { (r.position.x * 2) + r.size.width, (r.position.y * 2) + r.size.height };
}

int Distance(Rectangle a, Rectangle b)
{
	const Point ca = CentreOf(a);
	const Point cb = CentreOf(b);
	return (std::abs(ca.x - cb.x) + std::abs(ca.y - cb.y)) / 2;
}

/** Which side of a the area b lies on, when they touch; nullopt when they don't. */
std::optional<Side> Touching(Rectangle a, Rectangle b)
{
	const bool rowsOverlap = a.position.y < b.position.y + b.size.height && b.position.y < a.position.y + a.size.height;
	const bool columnsOverlap = a.position.x < b.position.x + b.size.width && b.position.x < a.position.x + a.size.width;
	if (rowsOverlap && a.position.x + a.size.width == b.position.x)
		return East;
	if (rowsOverlap && b.position.x + b.size.width == a.position.x)
		return West;
	if (columnsOverlap && a.position.y + a.size.height == b.position.y)
		return South;
	if (columnsOverlap && b.position.y + b.size.height == a.position.y)
		return North;
	return std::nullopt;
}

struct Link {
	int a;
	int b;
	/** The side of a that b is on. */
	Side side;
	/** Block coordinate along the wall: a row for an east or west link, a column for a north or south one. */
	int at = 0;
};

/** One way a designed room can sit in its area. */
struct Placement {
	int design = -1;
	int turn = 0;
	std::vector<std::string> grid;
	Point offset;
	/** Per side, the wall coordinates a doorway can open at. */
	std::array<std::vector<int>, 4> reach;
};

class Planner {
public:
	Planner(const CathedralRequest &request, CathedralPlan &plan)
	    : request_(request)
	    , plan_(plan)
	{
	}

	bool run()
	{
		plan_ = {};
		for (auto &row : slots_)
			row.fill(Free);
		if (!planMainWay())
			return false;
		if (!planQuestRooms())
			return false;
		planSides();
		planLoop();
		choosePlacements();
		if (!chooseDoorways())
			return false;
		buildAreas();
		return connected();
	}

private:
	static constexpr int8_t Free = -1;
	static constexpr int8_t Reserved = -2;

	const CathedralRequest &request_;
	CathedralPlan &plan_;
	std::array<std::array<int8_t, Slots>, Slots> slots_ {};
	std::vector<Link> links_;
	std::vector<std::optional<Placement>> placements_;
	std::array<int, 64> familyCount_ {};
	int mainWayLength_ = 0;

	bool isFree(Rectangle r) const
	{
		if (r.position.x < 0 || r.position.y < 0 || r.position.x + r.size.width > Slots || r.position.y + r.size.height > Slots)
			return false;
		for (int y = r.position.y; y < r.position.y + r.size.height; y++) {
			for (int x = r.position.x; x < r.position.x + r.size.width; x++) {
				if (slots_[y][x] != Free)
					return false;
			}
		}
		return true;
	}

	int addArea(Rectangle r, CathedralAreaKind kind, int step)
	{
		CathedralArea area;
		area.slots = r;
		area.kind = kind;
		area.step = step;
		plan_.areas.push_back(std::move(area));
		const int index = static_cast<int>(plan_.areas.size()) - 1;
		for (int y = r.position.y; y < r.position.y + r.size.height; y++) {
			for (int x = r.position.x; x < r.position.x + r.size.width; x++)
				slots_[y][x] = static_cast<int8_t>(index);
		}
		return index;
	}

	void addLink(int a, int b)
	{
		const std::optional<Side> side = Touching(plan_.areas[a].slots, plan_.areas[b].slots);
		if (side)
			links_.push_back({ a, b, *side });
	}

	std::vector<Rectangle> placesNextTo(Rectangle of, Size size) const
	{
		std::vector<Rectangle> places;
		for (int y = of.position.y - size.height; y <= of.position.y + of.size.height; y++) {
			for (int x = of.position.x - size.width; x <= of.position.x + of.size.width; x++) {
				const Rectangle r { { x, y }, size };
				if (Touching(of, r) && isFree(r))
					places.push_back(r);
			}
		}
		return places;
	}

	static Size RandomRoomSize()
	{
		switch (GenerateRnd(4)) {
		case 0: return { 2, 1 };
		case 1: return { 1, 2 };
		default: return { 1, 1 };
		}
	}

	bool planMainWay()
	{
		const int n = MainWayMin + GenerateRnd(MainWayExtra);
		const int big1 = std::max(2, (n * 35) / 100);
		const int big2 = std::min(n - 2, std::max(big1 + 2, (n * 68) / 100));
		mainWayLength_ = n;

		// The arrival room: the stairs up go into its north wall, so nothing may stand north of it.
		std::vector<Rectangle> starts;
		for (int y = 0; y < Slots; y++) {
			for (int x = 0; x + 1 < Slots; x++) {
				const Rectangle r { { x, y }, { 2, 1 } };
				if (isFree(r) && (y == 0 || isFree({ { x, y - 1 }, { 2, 1 } })))
					starts.push_back(r);
			}
		}
		const Rectangle start = starts[GenerateRnd(static_cast<int>(starts.size()))];
		if (start.position.y > 0) {
			slots_[start.position.y - 1][start.position.x] = Reserved;
			slots_[start.position.y - 1][start.position.x + 1] = Reserved;
		}
		int previous = addArea(start, CathedralAreaKind::Arrival, 0);

		for (int step = 1; step < n; step++) {
			CathedralAreaKind kind = CathedralAreaKind::Way;
			Size size = RandomRoomSize();
			if (step == big1 || step == big2) {
				kind = CathedralAreaKind::BigFight;
				size = { 2, 2 };
			} else if (step == big1 + 1 || step == big2 + 1) {
				kind = CathedralAreaKind::Quiet;
			}
			if (step == n - 1) {
				kind = request_.questPieceIsWayDown ? CathedralAreaKind::QuestPiece : CathedralAreaKind::WayDown;
				size = request_.questPieceIsWayDown ? Size { 2, 2 } : Size { 2, 1 };
			}
			std::vector<Rectangle> places = placesNextTo(plan_.areas[previous].slots, size);
			if (places.empty() && kind != CathedralAreaKind::BigFight && kind != CathedralAreaKind::WayDown && kind != CathedralAreaKind::QuestPiece)
				places = placesNextTo(plan_.areas[previous].slots, { 1, 1 });
			if (places.empty())
				return false;
			// Of two picks, take the one further from the arrival room, so the way wanders off rather than circling.
			Rectangle pick = places[GenerateRnd(static_cast<int>(places.size()))];
			const Rectangle other = places[GenerateRnd(static_cast<int>(places.size()))];
			if (Distance(other, start) > Distance(pick, start))
				pick = other;
			if (step == n - 1 && Distance(pick, start) < 3)
				return false;
			// The way down's doorways are on its west, east and south sides.
			if (kind == CathedralAreaKind::WayDown && Touching(pick, plan_.areas[previous].slots) == North)
				return false;
			const int area = addArea(pick, kind, step);
			addLink(previous, area);
			previous = area;
		}
		return true;
	}

	/** A great hall off the main way for a quest room, or for the Poisoned Water entrance. */
	bool addQuestHall(CathedralAreaKind kind)
	{
		std::vector<std::pair<int, Rectangle>> places;
		for (int i = 1; i < mainWayLength_ - 1; i++) {
			for (const Rectangle r : placesNextTo(plan_.areas[i].slots, { 2, 2 }))
				places.emplace_back(i, r);
		}
		if (places.empty())
			return false;
		const auto [from, r] = places[GenerateRnd(static_cast<int>(places.size()))];
		addLink(from, addArea(r, kind, -1));
		return true;
	}

	bool planQuestRooms()
	{
		if (request_.questPiece && !request_.questPieceIsWayDown && !addQuestHall(CathedralAreaKind::QuestPiece))
			return false;
		if (request_.water && !addQuestHall(CathedralAreaKind::QuestWater))
			return false;
		return true;
	}

	void planSides()
	{
		const int wanted = SidesMin + GenerateRnd(SidesExtra);
		int made = 0;
		for (int tries = 0; tries < 40 && made < wanted; tries++) {
			const int from = 1 + GenerateRnd(mainWayLength_ - 2);
			const int length = FlipCoin(3) ? 2 : 1;
			int previous = from;
			for (int i = 0; i < length; i++) {
				const std::vector<Rectangle> places = placesNextTo(plan_.areas[previous].slots, RandomRoomSize());
				if (places.empty())
					break;
				const Rectangle r = places[GenerateRnd(static_cast<int>(places.size()))];
				const int area = addArea(r, CathedralAreaKind::SideEnd, -1);
				if (previous != from)
					plan_.areas[previous].kind = CathedralAreaKind::Side;
				addLink(previous, area);
				previous = area;
			}
			if (previous != from)
				made++;
		}
	}

	/** Now and then two rooms of the main way that touch get a second doorway, so there is a way round. */
	void planLoop()
	{
		std::vector<std::pair<int, int>> pairs;
		for (int a = 1; a < mainWayLength_ - 1; a++) {
			for (int b = a + 3; b < mainWayLength_ - 1; b++) {
				if (Touching(plan_.areas[a].slots, plan_.areas[b].slots))
					pairs.emplace_back(a, b);
			}
		}
		if (pairs.empty() || FlipCoin())
			return;
		const auto [a, b] = pairs[GenerateRnd(static_cast<int>(pairs.size()))];
		addLink(a, b);
	}

	std::vector<std::pair<int, Side>> linksOf(int area) const
	{
		std::vector<std::pair<int, Side>> out;
		for (size_t i = 0; i < links_.size(); i++) {
			if (links_[i].a == area)
				out.emplace_back(static_cast<int>(i), links_[i].side);
			else if (links_[i].b == area)
				out.emplace_back(static_cast<int>(i), Opposite(links_[i].side));
		}
		return out;
	}

	/** The block range along the wall two linked areas share. */
	std::pair<int, int> overlap(const Link &link) const
	{
		const Rectangle a = plan_.areas[link.a].interior();
		const Rectangle b = plan_.areas[link.b].interior();
		if (link.side == East || link.side == West)
			return { std::max(a.position.y, b.position.y), std::min(a.position.y + a.size.height, b.position.y + b.size.height) - 1 };
		return { std::max(a.position.x, b.position.x), std::min(a.position.x + a.size.width, b.position.x + b.size.width) - 1 };
	}

	static int DesignChance(CathedralAreaKind kind)
	{
		switch (kind) {
		case CathedralAreaKind::BigFight: return 85;
		case CathedralAreaKind::Quiet: return 60;
		case CathedralAreaKind::SideEnd: return 55;
		case CathedralAreaKind::Way: return 35;
		case CathedralAreaKind::Side: return 20;
		default: return 0;
		}
	}

	static bool RoleFits(const CathedralRoomDef &room, CathedralAreaKind kind)
	{
		if (room.family == "stairs")
			return false;
		switch (kind) {
		case CathedralAreaKind::BigFight: return (room.roles & RoomRoleBigFight) != 0;
		case CathedralAreaKind::Quiet: return (room.roles & RoomRoleQuiet) != 0;
		case CathedralAreaKind::SideEnd: return (room.roles & RoomRoleSideArea) != 0;
		case CathedralAreaKind::Side: return (room.roles & (RoomRoleSideArea | RoomRoleJunction)) != 0;
		case CathedralAreaKind::Way: return (room.roles & (RoomRoleMainWay | RoomRoleJunction)) != 0 && (room.roles & RoomRoleBigFight) == 0;
		default: return false;
		}
	}

	int familyIndex(const std::string &family) const
	{
		// Families are few; a small hash into the counter array is plenty.
		size_t h = 0;
		for (const char c : family)
			h = (h * 31) + static_cast<unsigned char>(c);
		return static_cast<int>(h % familyCount_.size());
	}

	/**
	 * Where the doorways of a designed room placed in an area can open, per side.
	 * Normally only at its 'E' marks; with anyFloor, at any plain floor facing that side.
	 */
	static std::array<std::vector<int>, 4> Reach(const std::vector<std::string> &grid, Rectangle interior, Point offset, bool anyFloor)
	{
		std::array<std::vector<int>, 4> reach;
		const int gw = static_cast<int>(grid[0].size());
		const int gh = static_cast<int>(grid.size());
		const int w = interior.size.width;
		const int h = interior.size.height;
		for (int y = 0; y < gh; y++) {
			for (int x = 0; x < gw; x++) {
				if (grid[y][x] != 'E' && (!anyFloor || grid[y][x] != '.'))
					continue;
				for (int s = 0; s < 4; s++) {
					const Displacement step = SideStep[s];
					bool clear = true;
					for (Point p { x + step.deltaX, y + step.deltaY };; p += step) {
						const Point inArea = p + Displacement { offset.x, offset.y };
						if (inArea.x < 0 || inArea.y < 0 || inArea.x >= w || inArea.y >= h)
							break;
						if (At(grid, p.x, p.y) != ' ') {
							clear = false;
							break;
						}
					}
					if (!clear)
						continue;
					const int at = (s == East || s == West) ? interior.position.y + offset.y + y : interior.position.x + offset.x + x;
					reach[s].push_back(at);
				}
			}
		}
		return reach;
	}

	std::optional<Placement> tryPlace(int areaIndex, int design, bool anyOffset)
	{
		const CathedralArea &area = plan_.areas[areaIndex];
		const CathedralRoomDef &room = GetCathedralRooms()[design];
		const Rectangle interior = area.interior();
		const auto links = linksOf(areaIndex);

		// Settle the optional parts first: each is used or not on this floor.
		std::vector<std::string> base = room.grid;
		for (const auto &option : room.options) {
			const char use = FlipCoin() ? option[1] : option[2];
			for (std::string &row : base)
				std::replace(row.begin(), row.end(), option[0], use);
		}

		// The marked entries come first. A room whose marks can't meet this area's neighbours may open
		// at its plain floor instead, so rooms with a single entry per side still get their turn.
		for (const bool anyFloor : { false, true }) {
			if (anyFloor && !anyOffset)
				break;
			std::array<int, 8> turns { 0, 1, 2, 3, 4, 5, 6, 7 };
			for (int i = 7; i > 0; i--)
				std::swap(turns[i], turns[GenerateRnd(i + 1)]);
			for (const int turn : turns) {
				std::vector<std::string> grid = TurnCathedralGrid(base, turn);
				const int gw = static_cast<int>(grid[0].size());
				const int gh = static_cast<int>(grid.size());
				if (gw > interior.size.width || gh > interior.size.height)
					continue;
				if (!BackedOk(grid) || !StairsDownOk(grid) || (room.pillarsBehind && !PillarsBehindOk(grid)))
					continue;
				std::vector<Point> offsets;
				for (int oy = 0; oy <= interior.size.height - gh; oy++) {
					for (int ox = 0; ox <= interior.size.width - gw; ox++)
						offsets.push_back({ ox, oy });
				}
				if (!anyOffset)
					offsets.erase(std::remove_if(offsets.begin(), offsets.end(), [](Point o) { return o.y != 0; }), offsets.end());
				for (size_t i = offsets.size(); i > 1; i--)
					std::swap(offsets[i - 1], offsets[GenerateRnd(static_cast<int>(i))]);
				for (const Point offset : offsets) {
					std::array<std::vector<int>, 4> reach = Reach(grid, interior, offset, anyFloor);
					bool fits = true;
					for (const auto &[linkIndex, side] : links) {
						const auto [lo, hi] = overlap(links_[linkIndex]);
						const std::vector<int> &at = reach[side];
						if (std::none_of(at.begin(), at.end(), [lo, hi](int v) { return v >= lo && v <= hi; })) {
							fits = false;
							break;
						}
					}
					if (!fits)
						continue;
					return Placement { design, turn, std::move(grid), offset, std::move(reach) };
				}
			}
		}
		return std::nullopt;
	}

	void choosePlacements()
	{
		const std::vector<CathedralRoomDef> &rooms = GetCathedralRooms();
		placements_.assign(plan_.areas.size(), std::nullopt);
		for (size_t i = 0; i < plan_.areas.size(); i++) {
			const CathedralArea &area = plan_.areas[i];
			if (area.kind == CathedralAreaKind::Arrival || area.kind == CathedralAreaKind::WayDown) {
				const int design = FindRoom(area.kind == CathedralAreaKind::Arrival ? "arrival" : "waydown");
				if (design >= 0)
					placements_[i] = tryPlace(static_cast<int>(i), design, area.kind != CathedralAreaKind::Arrival);
				continue;
			}
			if (GenerateRnd(100) >= DesignChance(area.kind))
				continue;
			std::vector<int> candidates;
			for (size_t r = 0; r < rooms.size(); r++) {
				const CathedralRoomDef &room = rooms[r];
				if (!RoleFits(room, area.kind))
					continue;
				if (((request_.forbidden | plan_.usedRooms) >> r) & 1)
					continue;
				if (familyCount_[familyIndex(room.family)] >= FamilyMost)
					continue;
				candidates.push_back(static_cast<int>(r));
			}
			for (size_t k = candidates.size(); k > 1; k--)
				std::swap(candidates[k - 1], candidates[GenerateRnd(static_cast<int>(k))]);
			for (const int design : candidates) {
				std::optional<Placement> placement = tryPlace(static_cast<int>(i), design, true);
				if (!placement)
					continue;
				placements_[i] = std::move(placement);
				plan_.usedRooms |= uint64_t { 1 } << design;
				familyCount_[familyIndex(rooms[design].family)]++;
				break;
			}
		}
	}

	std::vector<int> reachOf(int area, Side side, const Link &link) const
	{
		const auto [lo, hi] = overlap(link);
		std::vector<int> out;
		if (placements_[area]) {
			for (const int v : placements_[area]->reach[side]) {
				if (v >= lo && v <= hi)
					out.push_back(v);
			}
			return out;
		}
		for (int v = lo; v <= hi; v++)
			out.push_back(v);
		return out;
	}

	static bool Fixed(CathedralAreaKind kind)
	{
		return kind == CathedralAreaKind::Arrival || kind == CathedralAreaKind::WayDown;
	}

	void dropDesign(int area)
	{
		const int design = placements_[area]->design;
		placements_[area] = std::nullopt;
		plan_.usedRooms &= ~(uint64_t { 1 } << design);
		familyCount_[familyIndex(GetCathedralRooms()[design].family)]--;
	}

	bool chooseDoorways()
	{
		for (Link &link : links_) {
			for (int attempt = 0; attempt < 3; attempt++) {
				std::vector<int> a = reachOf(link.a, link.side, link);
				const std::vector<int> b = reachOf(link.b, Opposite(link.side), link);
				std::erase_if(a, [&b](int v) { return std::find(b.begin(), b.end(), v) == b.end(); });
				if (!a.empty()) {
					link.at = a[GenerateRnd(static_cast<int>(a.size()))];
					break;
				}
				// Two designed rooms whose doorways don't line up: one of them becomes a plain room.
				const int drop = placements_[link.b] && !Fixed(plan_.areas[link.b].kind) ? link.b : link.a;
				if (!placements_[drop] || Fixed(plan_.areas[drop].kind))
					return false;
				dropDesign(drop);
				if (attempt == 2)
					return false;
			}
		}
		return true;
	}

	void carve(Point block)
	{
		if (block.x > 0 && block.y > 0 && block.x < DMAXX - 1 && block.y < DMAXY - 1)
			plan_.mask.set(block.x, block.y);
	}

	/** A passage from the block inside a doorway to the target: first straight in, then along. */
	void carvePath(Point from, Side inward, Point to, int width)
	{
		Point p = from;
		const bool alongX = inward == East || inward == West;
		const auto mark = [this, width, alongX](Point at) {
			for (int k = 0; k < width; k++)
				carve(at + (alongX ? Displacement { 0, k } : Displacement { k, 0 }));
		};
		mark(p);
		if (alongX) {
			while (p.x != to.x) {
				p.x += p.x < to.x ? 1 : -1;
				mark(p);
			}
			while (p.y != to.y) {
				p.y += p.y < to.y ? 1 : -1;
				carve(p);
			}
		} else {
			while (p.y != to.y) {
				p.y += p.y < to.y ? 1 : -1;
				mark(p);
			}
			while (p.x != to.x) {
				p.x += p.x < to.x ? 1 : -1;
				carve(p);
			}
		}
	}

	/** The block just inside each doorway of an area, with the direction into the area. */
	std::vector<std::pair<Point, Side>> doorwaysInto(int area) const
	{
		std::vector<std::pair<Point, Side>> out;
		const Rectangle in = plan_.areas[area].interior();
		for (const auto &[linkIndex, side] : linksOf(area)) {
			const Link &link = links_[linkIndex];
			Point p;
			switch (side) {
			case North: p = { link.at, in.position.y }; break;
			case South: p = { link.at, in.position.y + in.size.height - 1 }; break;
			case West: p = { in.position.x, link.at }; break;
			case East: p = { in.position.x + in.size.width - 1, link.at }; break;
			}
			out.emplace_back(p, Opposite(side));
		}
		return out;
	}

	void buildDesigned(int index)
	{
		CathedralArea &area = plan_.areas[index];
		Placement &placement = *placements_[index];
		const Rectangle in = area.interior();
		area.design = placement.design;
		area.turn = placement.turn;
		area.grid = placement.grid;
		area.origin = in.position + Displacement { placement.offset.x, placement.offset.y };
		for (int y = in.position.y; y < in.position.y + in.size.height; y++) {
			for (int x = in.position.x; x < in.position.x + in.size.width; x++)
				plan_.designed.set(x, y);
		}
		const int gw = static_cast<int>(area.grid[0].size());
		const int gh = static_cast<int>(area.grid.size());
		for (int y = 0; y < gh; y++) {
			for (int x = 0; x < gw; x++) {
				const char c = area.grid[y][x];
				if (c == ' ')
					continue;
				const Point block = area.origin + Displacement { x, y };
				if (IsLine(c)) {
					plan_.lines.push_back({ block, c });
					continue;
				}
				carve(block);
				if (c == 'O')
					plan_.pillars.push_back(block);
				else if (c == 'U' && !plan_.stairsUp && At(area.grid, x - 1, y) != 'U')
					plan_.stairsUp = block;
				else if (c == 'V' && !plan_.stairsDown && At(area.grid, x - 1, y) != 'V' && At(area.grid, x, y - 1) != 'V')
					plan_.stairsDown = block;
				else if (c != '.' && c != 'E' && c != 'U' && c != 'V')
					plan_.props.push_back({ block, c, index });
			}
		}
		// Each doorway runs straight in to its 'E'.
		for (const auto &[inside, inward] : doorwaysInto(index)) {
			const Displacement step = SideStep[inward];
			for (Point p = inside; in.contains(p); p += step) {
				const Point g = p - Displacement { area.origin.x, area.origin.y };
				if (At(area.grid, g.x, g.y) != ' ')
					break;
				carve(p);
			}
		}
	}

	void buildPlain(int index)
	{
		CathedralArea &area = plan_.areas[index];
		const Rectangle in = area.interior();
		const auto doorways = doorwaysInto(index);
		const bool mainWay = area.step >= 0;
		const int width = mainWay ? 2 : 1;

		const auto fill = [this](Rectangle r) {
			for (int y = r.position.y; y < r.position.y + r.size.height; y++) {
				for (int x = r.position.x; x < r.position.x + r.size.width; x++)
					carve({ x, y });
			}
		};

		switch (area.kind) {
		case CathedralAreaKind::Arrival: {
			fill(in);
			plan_.stairsUp = in.position + Displacement { 1 + GenerateRnd(in.size.width - 5), 0 };
			return;
		}
		case CathedralAreaKind::WayDown: {
			fill(in);
			plan_.stairsDown = in.position + Displacement { 1 + GenerateRnd(in.size.width - 5), 0 };
			return;
		}
		case CathedralAreaKind::QuestPiece:
			fill(in);
			plan_.questPiece = in.position + Displacement { 1, 1 };
			for (int y = in.position.y; y < in.position.y + in.size.height; y++) {
				for (int x = in.position.x; x < in.position.x + in.size.width; x++)
					plan_.designed.set(x, y);
			}
			return;
		case CathedralAreaKind::QuestWater:
			fill(in);
			plan_.water = in.position + Displacement { 2, 2 };
			return;
		default:
			break;
		}

		// A winding passage: every doorway runs to one meeting point.
		if (area.kind == CathedralAreaKind::Side || (area.kind == CathedralAreaKind::Way && area.slots.size.width * area.slots.size.height == 1 && FlipCoin(3))) {
			area.corridor = true;
			const Point hub = in.position + Displacement { 1 + GenerateRnd(in.size.width - 2), 1 + GenerateRnd(in.size.height - 2) };
			for (const auto &[inside, inward] : doorways)
				carvePath(inside, inward, hub, width);
			return;
		}

		// A rough room: a body with a wing or two growing out of it, and maybe its corners cut.
		const int minW = std::max(3, (in.size.width * 55) / 100);
		const int minH = std::max(3, (in.size.height * 55) / 100);
		const int w = minW + GenerateRnd(in.size.width - minW + 1);
		const int h = minH + GenerateRnd(in.size.height - minH + 1);
		const Rectangle body { in.position + Displacement { GenerateRnd(in.size.width - w + 1), GenerateRnd(in.size.height - h + 1) }, { w, h } };
		fill(body);
		const int wings = (in.size.width >= 7 || in.size.height >= 7) ? 1 + GenerateRnd(2) : GenerateRnd(2);
		for (int i = 0; i < wings; i++) {
			const Side side = static_cast<Side>(GenerateRnd(4));
			const bool alongX = side == North || side == South;
			const int bodyLength = alongX ? w : h;
			const int length = std::min<int>(bodyLength, 2 + GenerateRnd(std::max(1, bodyLength - 1)));
			const int start = GenerateRnd(bodyLength - length + 1);
			int room = 0;
			switch (side) {
			case North: room = body.position.y - in.position.y; break;
			case South: room = (in.position.y + in.size.height) - (body.position.y + h); break;
			case West: room = body.position.x - in.position.x; break;
			case East: room = (in.position.x + in.size.width) - (body.position.x + w); break;
			}
			if (room <= 0)
				continue;
			const int depth = 1 + GenerateRnd(room);
			Rectangle wing;
			switch (side) {
			case North: wing = { { body.position.x + start, body.position.y - depth }, { length, depth } }; break;
			case South: wing = { { body.position.x + start, body.position.y + h }, { length, depth } }; break;
			case West: wing = { { body.position.x - depth, body.position.y + start }, { depth, length } }; break;
			case East: wing = { { body.position.x + w, body.position.y + start }, { depth, length } }; break;
			}
			fill(wing);
		}
		if (w >= 7 && h >= 7) {
			const int cut = 1 + GenerateRnd(2);
			for (int corner = 0; corner < 4; corner++) {
				if (FlipCoin(3))
					continue;
				const int sx = (corner & 1) != 0 ? -1 : 1;
				const int sy = (corner & 2) != 0 ? -1 : 1;
				const Point c { (corner & 1) != 0 ? body.position.x + w - 1 : body.position.x, (corner & 2) != 0 ? body.position.y + h - 1 : body.position.y };
				for (int k = 0; k < cut; k++) {
					for (int m = 0; m < cut - k; m++)
						plan_.mask.reset(c.x + (sx * k), c.y + (sy * m));
				}
			}
		}
		if (area.kind == CathedralAreaKind::BigFight && FlipCoin()) {
			// Four pillars, as Diablo's own chambers have them.
			const int px = body.position.x + (w / 3);
			const int py = body.position.y + (h / 3);
			const int qx = body.position.x + w - 1 - (w / 3);
			const int qy = body.position.y + h - 1 - (h / 3);
			for (const Point p : { Point { px, py }, Point { qx, py }, Point { px, qy }, Point { qx, qy } }) {
				if (plan_.mask.test(p.x, p.y))
					plan_.pillars.push_back(p);
			}
		}
		for (const auto &[inside, inward] : doorways) {
			Point target = body.position;
			int best = DMAXX * DMAXY;
			for (int y = body.position.y; y < body.position.y + h; y++) {
				for (int x = body.position.x; x < body.position.x + w; x++) {
					if (!plan_.mask.test(x, y))
						continue;
					const int d = std::abs(x - inside.x) + std::abs(y - inside.y);
					if (d < best) {
						best = d;
						target = { x, y };
					}
				}
			}
			carvePath(inside, inward, target, 1);
		}
	}

	void buildAreas()
	{
		for (size_t i = 0; i < plan_.areas.size(); i++) {
			if (placements_[i])
				buildDesigned(static_cast<int>(i));
			else
				buildPlain(static_cast<int>(i));
		}
		for (const Link &link : links_) {
			CathedralDoor door;
			const Rectangle b = plan_.areas[link.b].interior();
			const Rectangle a = plan_.areas[link.a].interior();
			switch (link.side) {
			case East: door.block = { b.position.x - 1, link.at }; break;
			case West: door.block = { a.position.x - 1, link.at }; break;
			case South: door.block = { link.at, b.position.y - 1 }; break;
			case North: door.block = { link.at, a.position.y - 1 }; break;
			}
			door.vertical = link.side == East || link.side == West;
			const bool sideArea = plan_.areas[link.a].step < 0 || plan_.areas[link.b].step < 0;
			door.door = sideArea && FlipCoin();
			plan_.doors.push_back(door);
		}
	}

	/** Every floor block can be reached from the stairs up through the doorways. */
	bool connected() const
	{
		if (!plan_.stairsUp || (!plan_.stairsDown && !request_.questPieceIsWayDown))
			return false;
		Bitset2d<DMAXX, DMAXY> open = plan_.mask;
		for (const CathedralDoor &door : plan_.doors)
			open.set(door.block.x, door.block.y);
		for (const CathedralLine &line : plan_.lines) {
			if (LineIsOpen(line.kind))
				open.set(line.block.x, line.block.y);
		}
		Bitset2d<DMAXX, DMAXY> seen;
		std::deque<Point> queue { *plan_.stairsUp };
		seen.set(plan_.stairsUp->x, plan_.stairsUp->y);
		size_t reached = 1;
		while (!queue.empty()) {
			const Point p = queue.front();
			queue.pop_front();
			for (const Displacement step : SideStep) {
				const Point n = p + step;
				if (n.x < 0 || n.y < 0 || n.x >= DMAXX || n.y >= DMAXY || !open.test(n.x, n.y) || seen.test(n.x, n.y))
					continue;
				seen.set(n.x, n.y);
				reached++;
				queue.push_back(n);
			}
		}
		return reached == open.count();
	}
};

/** The world tiles a prop may stand on: its block's four, and the floor half of the wall blocks north and west of it. */
std::vector<Point> PropTiles(Point block)
{
	const Point world = block.megaToWorld();
	return {
		world + Displacement { 0, 0 },
		world + Displacement { 1, 0 },
		world + Displacement { 0, 1 },
		world + Displacement { 1, 1 },
		world + Displacement { 0, -1 },
		world + Displacement { 1, -1 },
		world + Displacement { -1, 0 },
		world + Displacement { -1, 1 },
	};
}

bool TileClear(Point tile)
{
	return InDungeonBounds(tile) && IsTileNotSolid(tile) && !IsObjectAtPosition(tile);
}

/** Room around the tile for a prop: no other object right next to it. */
bool RoomAround(Point tile)
{
	for (int dy = -1; dy <= 1; dy++) {
		for (int dx = -1; dx <= 1; dx++) {
			if (IsObjectAtPosition(tile + Displacement { dx, dy }))
				return false;
		}
	}
	return true;
}

bool AgainstWall(Point tile)
{
	return TileHasAny(tile + Displacement { 0, -1 }, TileProperties::Solid) || TileHasAny(tile + Displacement { -1, 0 }, TileProperties::Solid);
}

std::optional<Point> PropSpot(Point block, bool wall, bool big)
{
	const std::vector<Point> tiles = PropTiles(block);
	for (int pass = 0; pass < 2; pass++) {
		for (size_t i = 0; i < tiles.size(); i++) {
			const Point tile = tiles[i];
			// The north and west wall halves only serve props that stand against a wall.
			if (i >= 4 && !wall)
				continue;
			if (!TileClear(tile) || !RoomAround(tile))
				continue;
			if (big && !TileClear(tile + Displacement { 0, -1 }))
				continue;
			if (pass == 0 && wall && !AgainstWall(tile))
				continue;
			return tile;
		}
	}
	return std::nullopt;
}

bool PlaceShrine(Point block)
{
	for (const Point tile : PropTiles(block)) {
		if (!TileClear(tile))
			continue;
		if (TileHasAny(tile + Direction::NorthEast, TileProperties::Trap) && TileClear(tile + Direction::NorthWest) && TileClear(tile + Direction::SouthEast)
		    && TileClear(tile + Displacement { -1, 0 }) && TileClear(tile + Displacement { 1, 0 })) {
			AddObject(OBJ_CANDLE2, tile + Displacement { -1, 0 });
			AddObject(OBJ_SHRINER, tile);
			AddObject(OBJ_CANDLE2, tile + Displacement { 1, 0 });
			return true;
		}
		if (TileHasAny(tile + Direction::NorthWest, TileProperties::Trap) && TileClear(tile + Direction::NorthEast) && TileClear(tile + Direction::SouthWest)
		    && TileClear(tile + Displacement { 0, -1 }) && TileClear(tile + Displacement { 0, 1 })) {
			AddObject(OBJ_CANDLE2, tile + Displacement { 0, -1 });
			AddObject(OBJ_SHRINEL, tile);
			AddObject(OBJ_CANDLE2, tile + Displacement { 0, 1 });
			return true;
		}
	}
	return false;
}

template <size_t N>
_object_id PickOne(const std::array<_object_id, N> &ids)
{
	return ids[GenerateRnd(static_cast<int>(N))];
}

constexpr std::array<_object_id, 4> Fountains { OBJ_BLOODFTN, OBJ_PURIFYINGFTN, OBJ_MURKYFTN, OBJ_TEARFTN };
constexpr std::array<_object_id, 7> Chained { OBJ_TNUDEM1, OBJ_TNUDEM2, OBJ_TNUDEM3, OBJ_TNUDEM4, OBJ_TNUDEW1, OBJ_TNUDEW2, OBJ_TNUDEW3 };
constexpr std::array<_object_id, 5> Staked { OBJ_TORTURE1, OBJ_TORTURE2, OBJ_TORTURE3, OBJ_TORTURE4, OBJ_TORTURE5 };
constexpr std::array<_object_id, 3> Crucified { OBJ_CRUX1, OBJ_CRUX2, OBJ_CRUX3 };
constexpr std::array<_object_id, 3> Banners { OBJ_BANNERL, OBJ_BANNERM, OBJ_BANNERR };
constexpr std::array<_object_id, 3> Chests { OBJ_CHEST1, OBJ_CHEST2, OBJ_CHEST3 };

/** The objects a prop letter can turn into, for loading their art. */
std::vector<_object_id> ObjectsFor(char kind)
{
	switch (kind) {
	case 'c': return { OBJ_CANDLE2 };
	case 'A': return { OBJ_CANDLE2, OBJ_SHRINEL, OBJ_SHRINER };
	case 'S': return { OBJ_SARC };
	case 'K': return { OBJ_BOOKSHELF, OBJ_BOOKSHELFR };
	case 'k': return { OBJ_BOOKSTAND };
	case 'F': return { Fountains.begin(), Fountains.end() };
	case 'G': return { OBJ_GOATSHRINE };
	case 'X': return { Crucified.begin(), Crucified.end() };
	case 'x': return { OBJ_TBCROSS };
	case 'T': return { Chained.begin(), Chained.end() };
	case 'Y': return { Staked.begin(), Staked.end() };
	case 'D': return { OBJ_DECAP };
	case 'P':
	case 's': return { OBJ_SKPILE };
	case 'b': return { Banners.begin(), Banners.end() };
	case 'r': return { OBJ_BARREL, OBJ_BARRELEX };
	case '*': return { Chests.begin(), Chests.end() };
	default: return {};
	}
}

} // namespace

Rectangle CathedralArea::interior() const
{
	return InteriorOf(slots);
}

bool ParseCathedralRoomGrid(std::string_view text, std::vector<std::string> &grid)
{
	grid.clear();
	while (true) {
		const size_t slash = text.find('/');
		std::string row(text.substr(0, slash));
		std::replace(row.begin(), row.end(), '_', ' ');
		grid.push_back(std::move(row));
		if (slash == std::string_view::npos)
			break;
		text.remove_prefix(slash + 1);
	}
	if (grid.empty() || grid[0].empty())
		return false;
	return std::all_of(grid.begin(), grid.end(), [&grid](const std::string &row) { return row.size() == grid[0].size(); });
}

const std::vector<CathedralRoomDef> &GetCathedralRooms()
{
	if (!RoomsLoaded)
		LoadRooms();
	return Rooms;
}

void SetCathedralRooms(std::vector<CathedralRoomDef> rooms)
{
	Rooms = std::move(rooms);
	// An empty list reads the file again on next use.
	RoomsLoaded = !Rooms.empty();
}

std::vector<std::string> TurnCathedralGrid(const std::vector<std::string> &grid, int turn)
{
	std::vector<std::string> out = grid;
	for (int i = 0; i < (turn & 3); i++) {
		const int h = static_cast<int>(out.size());
		const int w = static_cast<int>(out[0].size());
		std::vector<std::string> turned(w, std::string(h, ' '));
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++)
				turned[x][h - 1 - y] = out[y][x];
		}
		out = std::move(turned);
	}
	if ((turn & 4) != 0) {
		for (std::string &row : out)
			std::reverse(row.begin(), row.end());
	}
	return out;
}

bool PlanCathedralFloor(const CathedralRequest &request, CathedralPlan &plan)
{
	return Planner(request, plan).run();
}

std::string DescribeCathedralPlan(const CathedralPlan &plan)
{
	static constexpr std::string_view KindNames[] = { "arrival", "way", "big fight", "quiet", "way down", "side", "side end", "quest room", "water" };
	std::string out = StrCat("new Cathedral floor, ", plan.areas.size(), " rooms:");
	for (const CathedralArea &area : plan.areas) {
		StrAppend(out, " [", KindNames[static_cast<size_t>(area.kind)]);
		if (area.step >= 0)
			StrAppend(out, " ", area.step);
		StrAppend(out, " at ", area.slots.position.x, ",", area.slots.position.y, " size ", area.slots.size.width, "x", area.slots.size.height);
		if (area.design >= 0)
			StrAppend(out, " ", GetCathedralRooms()[area.design].id);
		else if (area.corridor)
			StrAppend(out, " passage");
		out += "]";
	}
	if (plan.stairsUp)
		StrAppend(out, "; up ", plan.stairsUp->x, ",", plan.stairsUp->y);
	if (plan.stairsDown)
		StrAppend(out, "; down ", plan.stairsDown->x, ",", plan.stairsDown->y);
	return out;
}

uint64_t CathedralRoomsToAvoid(std::span<const uint64_t> recent)
{
	uint64_t memorable = 0;
	const std::vector<CathedralRoomDef> &rooms = GetCathedralRooms();
	for (size_t i = 0; i < rooms.size(); i++) {
		if (rooms[i].memorable)
			memorable |= uint64_t { 1 } << i;
	}
	uint64_t avoid = 0;
	for (size_t i = 0; i < recent.size(); i++) {
		if (i < RecentNormal)
			avoid |= recent[i];
		else if (i < RecentMemorable)
			avoid |= recent[i] & memorable;
	}
	return avoid;
}

bool IsCathedralFunnelFloor()
{
	return FunnelFloor;
}

void SetCathedralFunnelFloor(bool active)
{
	FunnelFloor = active;
	if (!active)
		CurrentPlan = {};
}

CathedralPlan &CurrentCathedralPlan()
{
	return CurrentPlan;
}

bool IsCathedralDesignedTile(Point tile)
{
	if (!FunnelFloor || !InDungeonBounds(tile))
		return false;
	const Point block = { (tile.x - 16) / 2, (tile.y - 16) / 2 };
	if (tile.x < 16 || tile.y < 16 || block.x >= DMAXX || block.y >= DMAXY)
		return false;
	return CurrentPlan.designed.test(block.x, block.y);
}

bool IsCathedralThemeRoom(int8_t tv)
{
	if (!FunnelFloor || tv <= 0)
		return false;
	for (const CathedralArea &area : CurrentPlan.areas) {
		if (!IsAnyOf(area.kind, CathedralAreaKind::Side, CathedralAreaKind::SideEnd) || area.design >= 0 || area.corridor)
			continue;
		// The room's own slots, walls included, in world tiles: the whole of the transparency area has to be in them.
		const Point first { 16 + 2 * (FirstSlot + area.slots.position.x * SlotSize), 16 + 2 * (FirstSlot + area.slots.position.y * SlotSize) };
		const Point last = first + Displacement { 2 * (area.slots.size.width * SlotSize + 1) - 1, 2 * (area.slots.size.height * SlotSize + 1) - 1 };
		bool inside = false;
		bool outside = false;
		for (int y = 0; y < MAXDUNY && !outside; y++) {
			for (int x = 0; x < MAXDUNX; x++) {
				if (dTransVal[x][y] != tv)
					continue;
				if (x < first.x || y < first.y || x > last.x || y > last.y) {
					outside = true;
					break;
				}
				inside = true;
			}
		}
		if (inside && !outside)
			return true;
	}
	return false;
}

int CathedralRoomAt(Point tile)
{
	if (!FunnelFloor)
		return 0;
	// The floor half of a wall block belongs to the room south or east of the wall.
	for (const Displacement shift : { Displacement { 0, 0 }, Displacement { 0, 1 }, Displacement { 1, 0 } }) {
		const Point t = tile + shift;
		if (t.x < 16 || t.y < 16)
			continue;
		const Point block { (t.x - 16) / 2, (t.y - 16) / 2 };
		for (size_t i = 0; i < CurrentPlan.areas.size(); i++) {
			if (CurrentPlan.areas[i].interior().contains(block))
				return static_cast<int>(i) + 1;
		}
	}
	return 0;
}

std::vector<std::pair<int, int>> CathedralBigFights()
{
	std::vector<std::pair<int, int>> fights;
	if (!FunnelFloor)
		return fights;
	const std::vector<CathedralArea> &areas = CurrentPlan.areas;
	for (size_t i = 0; i < areas.size(); i++) {
		if (areas[i].kind != CathedralAreaKind::BigFight)
			continue;
		int quiet = 0;
		for (size_t j = 0; j < areas.size(); j++) {
			if (areas[j].kind == CathedralAreaKind::Quiet && areas[j].step == areas[i].step + 1)
				quiet = static_cast<int>(j) + 1;
		}
		fights.emplace_back(static_cast<int>(i) + 1, quiet);
	}
	return fights;
}

int CathedralDesignedSarcophagi()
{
	if (!FunnelFloor)
		return 0;
	return static_cast<int>(std::count_if(CurrentPlan.props.begin(), CurrentPlan.props.end(), [](const CathedralProp &prop) { return prop.kind == 'S'; }));
}

void MarkCathedralObjectGraphics(uint16_t filesWidths[65])
{
	if (!FunnelFloor)
		return;
	for (const CathedralProp &prop : CurrentPlan.props) {
		for (const _object_id id : ObjectsFor(prop.kind)) {
			const ObjectData &data = AllObjects[id];
			filesWidths[data.ofindex] = data.animWidth;
		}
	}
}

void AddCathedralRoomObjects()
{
	if (!FunnelFloor)
		return;
	// On one floor in three the shrine spots hold a shrine; on the others, candles.
	const bool shrineFloor = GenerateRnd(3) == 0;
	std::vector<int> standsInArea(CurrentPlan.areas.size(), 0);
	for (const CathedralProp &prop : CurrentPlan.props) {
		const bool wall = prop.kind == 'A' || prop.kind == 'T' || prop.kind == 'x' || prop.kind == 'K' || prop.kind == 'X';
		if (prop.kind == 'A' && shrineFloor && PlaceShrine(prop.block))
			continue;
		if (prop.kind == 'K') {
			// Two shelves to a block, so a row of them reads as one long bookcase.
			const bool row = std::any_of(CurrentPlan.props.begin(), CurrentPlan.props.end(), [&prop](const CathedralProp &other) {
				return other.kind == 'K' && other.block.y == prop.block.y && std::abs(other.block.x - prop.block.x) == 1;
			});
			const Point world = prop.block.megaToWorld();
			for (const Displacement d : row ? std::array { Displacement { 0, 1 }, Displacement { 1, 1 } } : std::array { Displacement { 1, 0 }, Displacement { 1, 1 } }) {
				if (TileClear(world + d))
					AddObject(row ? OBJ_BOOKSHELF : OBJ_BOOKSHELFR, world + d);
			}
			continue;
		}
		const std::optional<Point> spot = PropSpot(prop.block, wall, prop.kind == 'S');
		if (!spot)
			continue;
		switch (prop.kind) {
		case 'A':
		case 'c': AddObject(OBJ_CANDLE2, *spot); break;
		case 'L': AddObject(OBJ_L1LIGHT, *spot); break;
		case 'S': AddObject(OBJ_SARC, *spot); break;
		case 'k':
			// Each stand gives a book or scroll, so at most two to a room.
			if (standsInArea[prop.area]++ < 2)
				AddObject(OBJ_BOOKSTAND, *spot);
			break;
		case 'F': AddObject(PickOne(Fountains), *spot); break;
		case 'G': AddObject(OBJ_GOATSHRINE, *spot); break;
		case 'X': AddObject(PickOne(Crucified), *spot); break;
		case 'x': AddObject(OBJ_TBCROSS, *spot); break;
		case 'T': AddObject(PickOne(Chained), *spot); break;
		case 'Y': AddObject(PickOne(Staked), *spot); break;
		case 'D': AddObject(OBJ_DECAP, *spot); break;
		case 'P':
		case 's': AddObject(OBJ_SKPILE, *spot); break;
		case 'b': AddObject(PickOne(Banners), *spot); break;
		case 'r': AddObject(FlipCoin(4) ? OBJ_BARRELEX : OBJ_BARREL, *spot); break;
		case '*': AddObject(PickOne(Chests), *spot); break;
		default: break;
		}
	}
}

} // namespace devilution
