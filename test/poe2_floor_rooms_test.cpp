#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "poe2/floor_rooms.hpp"

namespace devilution {
namespace {

/** A small cave drawn as text: '.' floor, '#' wall, a digit floor of that fixed (theme) room. */
struct Cave {
	explicit Cave(std::vector<std::string> rows)
	    : width(static_cast<int>(rows[0].size()))
	    , height(static_cast<int>(rows.size()))
	    , walkable(static_cast<size_t>(width * height))
	    , fixed(static_cast<size_t>(width * height))
	    , rooms(static_cast<size_t>(width * height))
	{
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				const char c = rows[static_cast<size_t>(y)][static_cast<size_t>(x)];
				walkable[index(x, y)] = c == '#' ? 0 : 1;
				fixed[index(x, y)] = c >= '1' && c <= '9' ? static_cast<uint8_t>(c - '0') : 0;
			}
		}
		SplitIntoChambers(width, height, walkable, fixed, rooms);
	}

	size_t index(int x, int y) const
	{
		return static_cast<size_t>(x * height + y);
	}

	int at(int x, int y) const
	{
		return rooms[index(x, y)];
	}

	std::set<int> chambers() const
	{
		std::set<int> found;
		for (size_t i = 0; i < rooms.size(); i++) {
			if (walkable[i] != 0 && fixed[i] == 0)
				found.insert(rooms[i]);
		}
		return found;
	}

	int width;
	int height;
	std::vector<uint8_t> walkable;
	std::vector<uint8_t> fixed;
	std::vector<uint8_t> rooms;
};

/** Two open chambers joined by a long passage two tiles wide. */
const std::vector<std::string> TwoChambers {
	"##########################",
	"#.......#########.......##",
	"#.......#########.......##",
	"#.......#########.......##",
	"#..........................",
	"#..........................",
	"#.......#########.......##",
	"#.......#########.......##",
	"#.......#########.......##",
	"##########################",
};

TEST(Poe2FloorRoomsTest, NarrowPassageSplitsTwoChambers)
{
	Cave cave(TwoChambers);
	EXPECT_EQ(cave.chambers().size(), 2U);
	EXPECT_NE(cave.at(4, 4), cave.at(20, 4));
	EXPECT_NE(cave.at(4, 4), 0);
	EXPECT_NE(cave.at(20, 4), 0);
	// The passage is shared out: its ends belong to the chamber each is nearer.
	EXPECT_EQ(cave.at(9, 4), cave.at(4, 4));
	EXPECT_EQ(cave.at(16, 4), cave.at(20, 4));
}

TEST(Poe2FloorRoomsTest, WallsAreNoRoom)
{
	Cave cave(TwoChambers);
	EXPECT_EQ(cave.at(0, 0), 0);
	EXPECT_EQ(cave.at(12, 2), 0);
}

TEST(Poe2FloorRoomsTest, ThemeRoomKeepsItsNumber)
{
	std::vector<std::string> rows = TwoChambers;
	rows[2] = "#.......#########...22..##";
	Cave cave(rows);
	EXPECT_EQ(cave.at(20, 2), 2);
	EXPECT_EQ(cave.at(21, 2), 2);
	for (const int chamber : cave.chambers())
		EXPECT_GT(chamber, 2);
}

TEST(Poe2FloorRoomsTest, NarrowCaveIsOneChamber)
{
	Cave cave({
	    "##########",
	    "#........#",
	    "#........#",
	    "#..####..#",
	    "#..####..#",
	    "#........#",
	    "#........#",
	    "##########",
	});
	EXPECT_EQ(cave.chambers().size(), 1U);
	EXPECT_NE(cave.at(1, 1), 0);
}

TEST(Poe2FloorRoomsTest, CutOffPocketIsItsOwnChamber)
{
	Cave cave({
	    "############",
	    "#.......#..#",
	    "#.......#..#",
	    "#.......####",
	    "#.......#..#",
	    "#.......#..#",
	    "#.......#..#",
	    "############",
	});
	EXPECT_EQ(cave.chambers().size(), 3U);
	EXPECT_NE(cave.at(9, 1), cave.at(9, 5));
	EXPECT_NE(cave.at(9, 1), cave.at(4, 4));
}

} // namespace
} // namespace devilution
