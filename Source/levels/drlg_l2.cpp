/**
 * @file levels/drlg_l2.cpp
 *
 * Implementation of the catacombs level generation algorithms.
 */
#include "levels/drlg_l2.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <expected>
#include <list>
#include <optional>
#include <set>
#include <vector>

#include "engine/displacement.hpp"
#include "engine/load_file.hpp"
#include "engine/random.hpp"
#include "engine/size.hpp"
#include "engine/world_tile.hpp"
#include "game_mode.hpp"
#include "levels/drlg_quests.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung.h"
#include "levels/gendung_defs.hpp"
#include "objects.h"
#include "poe2/cathedral_floors.hpp"
#include "poe2/floors.hpp"
#include "poe2/telemetry.hpp"
#include "tables/questdat.hpp"
#include "utils/bitset2d.hpp"
#include "utils/is_of.hpp"
#include "utils/status_macros.hpp"

namespace devilution {

namespace {

enum class HallDirection : int8_t {
	None,
	Up,
	Right,
	Down,
	Left,
};

struct HallNode {
	WorldTilePosition beginning;
	WorldTilePosition end;
	HallDirection direction;
};

struct RoomNode {
	WorldTilePosition topLeft;
	WorldTilePosition bottomRight;
};

int nRoomCnt;
RoomNode RoomList[81];
std::list<HallNode> HallList;
// An ASCII representation of the level
char predungeon[DMAXX][DMAXY];

const Displacement DirAdd[5] = {
	{ 0, 0 },
	{ 0, -1 },
	{ 1, 0 },
	{ 0, 1 },
	{ -1, 0 }
};
const ShadowStruct SPATSL2[2] = { { 6, 3, 0, 3, 48, 0, 50 }, { 9, 3, 0, 3, 48, 0, 50 } };
// short word_48489A = 0;

const uint8_t BTYPESL2[161] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0, 0, 17, 18, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 2, 2, 2, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 0, 3, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
const uint8_t BSTYPESL2[161] = { 0, 1, 2, 3, 0, 0, 6, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0, 6, 6, 6, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 2, 2, 2, 0, 0, 0, 1, 1, 1, 1, 6, 2, 2, 2, 0, 3, 3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1, 1, 2, 2, 3, 3, 3, 3, 1, 1, 2, 2, 3, 3, 3, 3, 1, 1, 3, 3, 2, 2, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/** Miniset: Arch vertical. */
const Miniset VARCH1 {
	{ 2, 4 },
	{
	    { 3, 0 },
	    { 3, 1 },
	    { 3, 4 },
	    { 0, 7 },
	},
	{
	    { 48, 0 },
	    { 51, 39 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical. */
const Miniset VARCH2 {
	{ 2, 4 },
	{
	    { 3, 0 },
	    { 3, 1 },
	    { 3, 4 },
	    { 0, 8 },
	},
	{
	    { 48, 0 },
	    { 51, 39 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical. */
const Miniset VARCH3 {
	{ 2, 4 },
	{
	    { 3, 0 },
	    { 3, 1 },
	    { 3, 4 },
	    { 0, 6 },
	},
	{
	    { 48, 0 },
	    { 51, 39 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical. */
const Miniset VARCH4 {
	{ 2, 4 },
	{
	    { 3, 0 },
	    { 3, 1 },
	    { 3, 4 },
	    { 0, 9 },
	},
	{
	    { 48, 0 },
	    { 51, 39 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical. */
const Miniset VARCH5 {
	{ 2, 4 },
	{
	    { 3, 0 },
	    { 3, 1 },
	    { 3, 4 },
	    { 0, 14 },
	},
	{
	    { 48, 0 },
	    { 51, 39 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical. */
const Miniset VARCH6 {
	{ 2, 4 },
	{
	    { 3, 0 },
	    { 3, 1 },
	    { 3, 4 },
	    { 0, 13 },
	},
	{
	    { 48, 0 },
	    { 51, 39 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical. */
const Miniset VARCH7 {
	{ 2, 4 },
	{
	    { 3, 0 },
	    { 3, 1 },
	    { 3, 4 },
	    { 0, 16 },
	},
	{
	    { 48, 0 },
	    { 51, 39 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical. */
const Miniset VARCH8 {
	{ 2, 4 },
	{
	    { 3, 0 },
	    { 3, 1 },
	    { 3, 4 },
	    { 0, 15 },
	},
	{
	    { 48, 0 },
	    { 51, 39 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical - corner. */
const Miniset VARCH9 {
	{ 2, 4 },
	{
	    { 3, 0 },
	    { 3, 8 },
	    { 3, 4 },
	    { 0, 7 },
	},
	{
	    { 48, 0 },
	    { 51, 42 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical - corner. */
const Miniset VARCH10 {
	{ 2, 4 },
	{
	    { 3, 0 },
	    { 3, 8 },
	    { 3, 4 },
	    { 0, 8 },
	},
	{
	    { 48, 0 },
	    { 51, 42 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical - corner. */
const Miniset VARCH11 {
	{ 2, 4 },
	{
	    { 3, 0 },
	    { 3, 8 },
	    { 3, 4 },
	    { 0, 6 },
	},
	{
	    { 48, 0 },
	    { 51, 42 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical - corner. */
const Miniset VARCH12 {
	{ 2, 4 },
	{
	    { 3, 0 },
	    { 3, 8 },
	    { 3, 4 },
	    { 0, 9 },
	},
	{
	    { 48, 0 },
	    { 51, 42 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical - corner. */
const Miniset VARCH13 {
	{ 2, 4 },
	{
	    { 3, 0 },
	    { 3, 8 },
	    { 3, 4 },
	    { 0, 14 },
	},
	{
	    { 48, 0 },
	    { 51, 42 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical - corner. */
const Miniset VARCH14 {
	{ 2, 4 },
	{
	    { 3, 0 },
	    { 3, 8 },
	    { 3, 4 },
	    { 0, 13 },
	},
	{
	    { 48, 0 },
	    { 51, 42 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical - corner. */
const Miniset VARCH15 {
	{ 2, 4 },
	{
	    { 3, 0 },
	    { 3, 8 },
	    { 3, 4 },
	    { 0, 16 },
	},
	{
	    { 48, 0 },
	    { 51, 42 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical - corner. */
const Miniset VARCH16 {
	{ 2, 4 },
	{
	    { 3, 0 },
	    { 3, 8 },
	    { 3, 4 },
	    { 0, 15 },
	},
	{
	    { 48, 0 },
	    { 51, 42 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical - open wall. */
const Miniset VARCH17 {
	{ 2, 3 },
	{
	    { 2, 7 },
	    { 3, 4 },
	    { 0, 7 },
	},
	{
	    { 141, 39 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical - open wall. */
const Miniset VARCH18 {
	{ 2, 3 },
	{
	    { 2, 7 },
	    { 3, 4 },
	    { 0, 8 },
	},
	{
	    { 141, 39 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical - open wall. */
const Miniset VARCH19 {
	{ 2, 3 },
	{
	    { 2, 7 },
	    { 3, 4 },
	    { 0, 6 },
	},
	{
	    { 141, 39 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical - open wall. */
const Miniset VARCH20 {
	{ 2, 3 },
	{
	    { 2, 7 },
	    { 3, 4 },
	    { 0, 9 },
	},
	{
	    { 141, 39 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical - open wall. */
const Miniset VARCH21 {
	{ 2, 3 },
	{
	    { 2, 7 },
	    { 3, 4 },
	    { 0, 14 },
	},
	{
	    { 141, 39 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical - open wall. */
const Miniset VARCH22 {
	{ 2, 3 },
	{
	    { 2, 7 },
	    { 3, 4 },
	    { 0, 13 },
	},
	{
	    { 141, 39 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical - open wall. */
const Miniset VARCH23 {
	{ 2, 3 },
	{
	    { 2, 7 },
	    { 3, 4 },
	    { 0, 16 },
	},
	{
	    { 141, 39 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical - open wall. */
const Miniset VARCH24 {
	{ 2, 3 },
	{
	    { 2, 7 },
	    { 3, 4 },
	    { 0, 15 },
	},
	{
	    { 141, 39 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical. */
const Miniset VARCH25 {
	{ 2, 4 },
	{
	    { 3, 0 },
	    { 3, 4 },
	    { 3, 1 },
	    { 0, 7 },
	},
	{
	    { 48, 0 },
	    { 51, 39 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical. */
const Miniset VARCH26 {
	{ 2, 4 },
	{
	    { 3, 0 },
	    { 3, 4 },
	    { 3, 1 },
	    { 0, 8 },
	},
	{
	    { 48, 0 },
	    { 51, 39 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical. */
const Miniset VARCH27 {
	{ 2, 4 },
	{
	    { 3, 0 },
	    { 3, 4 },
	    { 3, 1 },
	    { 0, 6 },
	},
	{
	    { 48, 0 },
	    { 51, 39 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical. */
const Miniset VARCH28 {
	{ 2, 4 },
	{
	    { 3, 0 },
	    { 3, 4 },
	    { 3, 1 },
	    { 0, 9 },
	},
	{
	    { 48, 0 },
	    { 51, 39 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical. */
const Miniset VARCH29 {
	{ 2, 4 },
	{
	    { 3, 0 },
	    { 3, 4 },
	    { 3, 1 },
	    { 0, 14 },
	},
	{
	    { 48, 0 },
	    { 51, 39 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical. */
const Miniset VARCH30 {
	{ 2, 4 },
	{
	    { 3, 0 },
	    { 3, 4 },
	    { 3, 1 },
	    { 0, 13 },
	},
	{
	    { 48, 0 },
	    { 51, 39 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical. */
const Miniset VARCH31 {
	{ 2, 4 },
	{
	    { 3, 0 },
	    { 3, 4 },
	    { 3, 1 },
	    { 0, 16 },
	},
	{
	    { 48, 0 },
	    { 51, 39 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical. */
const Miniset VARCH32 {
	{ 2, 4 },
	{
	    { 3, 0 },
	    { 3, 4 },
	    { 3, 1 },
	    { 0, 15 },
	},
	{
	    { 48, 0 },
	    { 51, 39 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical - room west entrance. */
const Miniset VARCH33 {
	{ 2, 4 },
	{
	    { 2, 0 },
	    { 3, 8 },
	    { 3, 4 },
	    { 0, 7 },
	},
	{
	    { 142, 0 },
	    { 51, 42 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical - room west entrance. */
const Miniset VARCH34 {
	{ 2, 4 },
	{
	    { 2, 0 },
	    { 3, 8 },
	    { 3, 4 },
	    { 0, 8 },
	},
	{
	    { 142, 0 },
	    { 51, 42 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical - room west entrance. */
const Miniset VARCH35 {
	{ 2, 4 },
	{
	    { 2, 0 },
	    { 3, 8 },
	    { 3, 4 },
	    { 0, 6 },
	},
	{
	    { 142, 0 },
	    { 51, 42 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical - room west entrance. */
const Miniset VARCH36 {
	{ 2, 4 },
	{
	    { 2, 0 },
	    { 3, 8 },
	    { 3, 4 },
	    { 0, 9 },
	},
	{
	    { 142, 0 },
	    { 51, 42 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical - room west entrance. */
const Miniset VARCH37 {
	{ 2, 4 },
	{
	    { 2, 0 },
	    { 3, 8 },
	    { 3, 4 },
	    { 0, 14 },
	},
	{
	    { 142, 0 },
	    { 51, 42 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical - room west entrance. */
const Miniset VARCH38 {
	{ 2, 4 },
	{
	    { 2, 0 },
	    { 3, 8 },
	    { 3, 4 },
	    { 0, 13 },
	},
	{
	    { 142, 0 },
	    { 51, 42 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical - room west entrance. */
const Miniset VARCH39 {
	{ 2, 4 },
	{
	    { 2, 0 },
	    { 3, 8 },
	    { 3, 4 },
	    { 0, 16 },
	},
	{
	    { 142, 0 },
	    { 51, 42 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch vertical - room west entrance. */
const Miniset VARCH40 {
	{ 2, 4 },
	{
	    { 2, 0 },
	    { 3, 8 },
	    { 3, 4 },
	    { 0, 15 },
	},
	{
	    { 142, 0 },
	    { 51, 42 },
	    { 47, 44 },
	    { 0, 0 },
	}
};
/** Miniset: Arch horizontal. */
const Miniset HARCH1 {
	{ 3, 2 },
	{
	    { 3, 3, 0 },
	    { 2, 5, 9 },
	},
	{
	    { 49, 46, 0 },
	    { 40, 45, 0 },
	}
};
/** Miniset: Arch horizontal. */
const Miniset HARCH2 {
	{ 3, 2 },
	{
	    { 3, 3, 0 },
	    { 2, 5, 6 },
	},
	{
	    { 49, 46, 0 },
	    { 40, 45, 0 },
	}
};
/** Miniset: Arch horizontal. */
const Miniset HARCH3 {
	{ 3, 2 },
	{
	    { 3, 3, 0 },
	    { 2, 5, 8 },
	},
	{
	    { 49, 46, 0 },
	    { 40, 45, 0 },
	}
};
/** Miniset: Arch horizontal. */
const Miniset HARCH4 {
	{ 3, 2 },
	{
	    { 3, 3, 0 },
	    { 2, 5, 7 },
	},
	{
	    { 49, 46, 0 },
	    { 40, 45, 0 },
	}
};
/** Miniset: Arch horizontal. */
const Miniset HARCH5 {
	{ 3, 2 },
	{
	    { 3, 3, 0 },
	    { 2, 5, 15 },
	},
	{
	    { 49, 46, 0 },
	    { 40, 45, 0 },
	}
};
/** Miniset: Arch horizontal. */
const Miniset HARCH6 {
	{ 3, 2 },
	{
	    { 3, 3, 0 },
	    { 2, 5, 16 },
	},
	{
	    { 49, 46, 0 },
	    { 40, 45, 0 },
	}
};
/** Miniset: Arch horizontal. */
const Miniset HARCH7 {
	{ 3, 2 },
	{
	    { 3, 3, 0 },
	    { 2, 5, 13 },
	},
	{
	    { 49, 46, 0 },
	    { 40, 45, 0 },
	}
};
/** Miniset: Arch horizontal. */
const Miniset HARCH8 {
	{ 3, 2 },
	{
	    { 3, 3, 0 },
	    { 2, 5, 14 },
	},
	{
	    { 49, 46, 0 },
	    { 40, 45, 0 },
	}
};
/** Miniset: Arch horizontal - north corner. */
const Miniset HARCH9 {
	{ 3, 2 },
	{
	    { 3, 3, 0 },
	    { 8, 5, 9 },
	},
	{
	    { 49, 46, 0 },
	    { 43, 45, 0 },
	}
};
/** Miniset: Arch horizontal - north corner. */
const Miniset HARCH10 {
	{ 3, 2 },
	{
	    { 3, 3, 0 },
	    { 8, 5, 6 },
	},
	{
	    { 49, 46, 0 },
	    { 43, 45, 0 },
	}
};
/** Miniset: Arch horizontal - north corner. */
const Miniset HARCH11 {
	{ 3, 2 },
	{
	    { 3, 3, 0 },
	    { 8, 5, 8 },
	},
	{
	    { 49, 46, 0 },
	    { 43, 45, 0 },
	}
};
/** Miniset: Arch horizontal - north corner. */
const Miniset HARCH12 {
	{ 3, 2 },
	{
	    { 3, 3, 0 },
	    { 8, 5, 7 },
	},
	{
	    { 49, 46, 0 },
	    { 43, 45, 0 },
	}
};
/** Miniset: Arch horizontal - north corner. */
const Miniset HARCH13 {
	{ 3, 2 },
	{
	    { 3, 3, 0 },
	    { 8, 5, 15 },
	},
	{
	    { 49, 46, 0 },
	    { 43, 45, 0 },
	}
};
/** Miniset: Arch horizontal - north corner. */
const Miniset HARCH14 {
	{ 3, 2 },
	{
	    { 3, 3, 0 },
	    { 8, 5, 16 },
	},
	{
	    { 49, 46, 0 },
	    { 43, 45, 0 },
	}
};
/** Miniset: Arch horizontal - north corner. */
const Miniset HARCH15 {
	{ 3, 2 },
	{
	    { 3, 3, 0 },
	    { 8, 5, 13 },
	},
	{
	    { 49, 46, 0 },
	    { 43, 45, 0 },
	}
};
/** Miniset: Arch horizontal - north corner. */
const Miniset HARCH16 {
	{ 3, 2 },
	{
	    { 3, 3, 0 },
	    { 8, 5, 14 },
	},
	{
	    { 49, 46, 0 },
	    { 43, 45, 0 },
	}
};
/** Miniset: Arch horizontal - wall. */
const Miniset HARCH17 {
	{ 3, 2 },
	{
	    { 1, 3, 0 },
	    { 8, 5, 9 },
	},
	{
	    { 140, 46, 0 },
	    { 43, 45, 0 },
	}
};
/** Miniset: Arch horizontal - wall. */
const Miniset HARCH18 {
	{ 3, 2 },
	{
	    { 1, 3, 0 },
	    { 8, 5, 6 },
	},
	{
	    { 140, 46, 0 },
	    { 43, 45, 0 },
	}
};
/** Miniset: Arch horizontal - wall. */
const Miniset HARCH19 {
	{ 3, 2 },
	{
	    { 1, 3, 0 },
	    { 8, 5, 8 },
	},
	{
	    { 140, 46, 0 },
	    { 43, 45, 0 },
	}
};
/** Miniset: Arch horizontal - wall. */
const Miniset HARCH20 {
	{ 3, 2 },
	{
	    { 1, 3, 0 },
	    { 8, 5, 7 },
	},
	{
	    { 140, 46, 0 },
	    { 43, 45, 0 },
	}
};
/** Miniset: Arch horizontal - wall. */
const Miniset HARCH21 {
	{ 3, 2 },
	{
	    { 1, 3, 0 },
	    { 8, 5, 15 },
	},
	{
	    { 140, 46, 0 },
	    { 43, 45, 0 },
	}
};
/** Miniset: Arch horizontal - wall. */
const Miniset HARCH22 {
	{ 3, 2 },
	{
	    { 1, 3, 0 },
	    { 8, 5, 16 },
	},
	{
	    { 140, 46, 0 },
	    { 43, 45, 0 },
	}
};
/** Miniset: Arch horizontal - wall. */
const Miniset HARCH23 {
	{ 3, 2 },
	{
	    { 1, 3, 0 },
	    { 8, 5, 13 },
	},
	{
	    { 140, 46, 0 },
	    { 43, 45, 0 },
	}
};
/** Miniset: Arch horizontal - wall. */
const Miniset HARCH24 {
	{ 3, 2 },
	{
	    { 1, 3, 0 },
	    { 8, 5, 14 },
	},
	{
	    { 140, 46, 0 },
	    { 43, 45, 0 },
	}
};
/** Miniset: Arch horizontal. */
const Miniset HARCH25 {
	{ 3, 2 },
	{
	    { 3, 3, 0 },
	    { 5, 2, 9 },
	},
	{
	    { 49, 46, 0 },
	    { 40, 45, 0 },
	}
};
/** Miniset: Arch horizontal. */
const Miniset HARCH26 {
	{ 3, 2 },
	{
	    { 3, 3, 0 },
	    { 5, 2, 6 },
	},
	{
	    { 49, 46, 0 },
	    { 40, 45, 0 },
	}
};
/** Miniset: Arch horizontal. */
const Miniset HARCH27 {
	{ 3, 2 },
	{
	    { 3, 3, 0 },
	    { 5, 2, 8 },
	},
	{
	    { 49, 46, 0 },
	    { 40, 45, 0 },
	}
};
/** Miniset: Arch horizontal. */
const Miniset HARCH28 {
	{ 3, 2 },
	{
	    { 3, 3, 0 },
	    { 5, 2, 7 },
	},
	{
	    { 49, 46, 0 },
	    { 40, 45, 0 },
	}
};
/** Miniset: Arch horizontal. */
const Miniset HARCH29 {
	{ 3, 2 },
	{
	    { 3, 3, 0 },
	    { 5, 2, 15 },
	},
	{
	    { 49, 46, 0 },
	    { 40, 45, 0 },
	}
};
/** Miniset: Arch horizontal. */
const Miniset HARCH30 {
	{ 3, 2 },
	{
	    { 3, 3, 0 },
	    { 5, 2, 16 },
	},
	{
	    { 49, 46, 0 },
	    { 40, 45, 0 },
	}
};
/** Miniset: Arch horizontal. */
const Miniset HARCH31 {
	{ 3, 2 },
	{
	    { 3, 3, 0 },
	    { 5, 2, 13 },
	},
	{
	    { 49, 46, 0 },
	    { 40, 45, 0 },
	}
};
/** Miniset: Arch horizontal. */
const Miniset HARCH32 {
	{ 3, 2 },
	{
	    { 3, 3, 0 },
	    { 5, 2, 14 },
	},
	{
	    { 49, 46, 0 },
	    { 40, 45, 0 },
	}
};
/** Miniset: Arch horizontal - west corner. */
const Miniset HARCH33 {
	{ 3, 2 },
	{
	    { 1, 3, 0 },
	    { 9, 5, 9 },
	},
	{
	    { 140, 46, 0 },
	    { 40, 45, 0 },
	}
};
/** Miniset: Arch horizontal - west corner. */
const Miniset HARCH34 {
	{ 3, 2 },
	{
	    { 1, 3, 0 },
	    { 9, 5, 6 },
	},
	{
	    { 140, 46, 0 },
	    { 40, 45, 0 },
	}
};
/** Miniset: Arch horizontal - west corner. */
const Miniset HARCH35 {
	{ 3, 2 },
	{
	    { 1, 3, 0 },
	    { 9, 5, 8 },
	},
	{
	    { 140, 46, 0 },
	    { 40, 45, 0 },
	}
};
/** Miniset: Arch horizontal - west corner. */
const Miniset HARCH36 {
	{ 3, 2 },
	{
	    { 1, 3, 0 },
	    { 9, 5, 7 },
	},
	{
	    { 140, 46, 0 },
	    { 40, 45, 0 },
	}
};
/** Miniset: Arch horizontal - west corner. */
const Miniset HARCH37 {
	{ 3, 2 },
	{
	    { 1, 3, 0 },
	    { 9, 5, 15 },
	},
	{
	    { 140, 46, 0 },
	    { 40, 45, 0 },
	}
};
/** Miniset: Arch horizontal - west corner. */
const Miniset HARCH38 {
	{ 3, 2 },
	{
	    { 1, 3, 0 },
	    { 9, 5, 16 },
	},
	{
	    { 140, 46, 0 },
	    { 40, 45, 0 },
	}
};
/** Miniset: Arch horizontal - west corner. */
const Miniset HARCH39 {
	{ 3, 2 },
	{
	    { 1, 3, 0 },
	    { 9, 5, 13 },
	},
	{
	    { 140, 46, 0 },
	    { 40, 45, 0 },
	}
};
/** Miniset: Arch horizontal - west corner. */
const Miniset HARCH40 {
	{ 3, 2 },
	{
	    { 1, 3, 0 },
	    { 9, 5, 14 },
	},
	{
	    { 140, 46, 0 },
	    { 40, 45, 0 },
	}
};
/** Miniset: Stairs up. */
const Miniset USTAIRS {
	{ 4, 4 },
	{
	    { 3, 3, 3, 3 },
	    { 3, 3, 3, 3 },
	    { 3, 3, 3, 3 },
	    { 3, 3, 3, 3 },
	},
	{
	    { 0, 0, 0, 0 },
	    { 0, 72, 77, 0 },
	    { 0, 76, 0, 0 },
	    { 0, 0, 0, 0 },
	}
};
/** Miniset: Stairs down. */
const Miniset DSTAIRS {
	{ 4, 4 },
	{
	    { 3, 3, 3, 3 },
	    { 3, 3, 3, 3 },
	    { 3, 3, 3, 3 },
	    { 3, 3, 3, 3 },
	},
	{
	    { 0, 0, 0, 0 },
	    { 0, 48, 71, 0 },
	    { 0, 50, 78, 0 },
	    { 0, 0, 0, 0 },
	}
};
/** Miniset: Stairs to town. */
const Miniset WARPSTAIRS {
	{ 4, 4 },
	{
	    { 3, 3, 3, 3 },
	    { 3, 3, 3, 3 },
	    { 3, 3, 3, 3 },
	    { 3, 3, 3, 3 },
	},
	{
	    { 0, 0, 0, 0 },
	    { 0, 158, 160, 0 },
	    { 0, 159, 0, 0 },
	    { 0, 0, 0, 0 },
	}
};
/** Miniset: Crumbled south pillar. */
const Miniset CRUSHCOL {
	{ 3, 3 },
	{
	    { 3, 1, 3 },
	    { 2, 6, 3 },
	    { 3, 3, 3 },
	},
	{
	    { 0, 0, 0 },
	    { 0, 83, 0 },
	    { 0, 0, 0 },
	}
};
/** Miniset: Vertical oil spill. */
const Miniset BIG1 {
	{ 2, 2 },
	{
	    { 3, 3 },
	    { 3, 3 },
	},
	{
	    { 113, 0 },
	    { 112, 0 },
	}
};
/** Miniset: Horizontal oil spill. */
const Miniset BIG2 {
	{ 2, 2 },
	{
	    { 3, 3 },
	    { 3, 3 },
	},
	{
	    { 114, 115 },
	    { 0, 0 },
	}
};
/** Miniset: Horizontal platform. */
const Miniset BIG3 {
	{ 1, 2 },
	{
	    { 1 },
	    { 1 },
	},
	{
	    { 117 },
	    { 116 },
	}
};
/** Miniset: Vertical platform. */
const Miniset BIG4 {
	{ 2, 1 },
	{
	    { 2, 2 },
	},
	{
	    { 118, 119 },
	}
};
/** Miniset: Large oil spill. */
const Miniset BIG5 {
	{ 2, 2 },
	{
	    { 3, 3 },
	    { 3, 3 },
	},
	{
	    { 120, 122 },
	    { 121, 123 },
	}
};
/** Miniset: Vertical wall with debris. */
const Miniset BIG6 {
	{ 1, 2 },
	{
	    { 1 },
	    { 1 },
	},
	{
	    { 125 },
	    { 124 },
	}
};
/** Miniset: Horizontal wall with debris. */
const Miniset BIG7 {
	{ 2, 1 },
	{
	    { 2, 2 },
	},
	{
	    { 126, 127 },
	}
};
/** Miniset: Rock pile. */
const Miniset BIG8 {
	{ 2, 2 },
	{
	    { 3, 3 },
	    { 3, 3 },
	},
	{
	    { 128, 130 },
	    { 129, 131 },
	}
};
/** Miniset: Vertical wall collapsed. */
const Miniset BIG9 {
	{ 2, 2 },
	{
	    { 1, 3 },
	    { 1, 3 },
	},
	{
	    { 133, 135 },
	    { 132, 134 },
	}
};
/** Miniset: Horizontal wall collapsed. */
const Miniset BIG10 {
	{ 2, 2 },
	{
	    { 2, 2 },
	    { 3, 3 },
	},
	{
	    { 136, 137 },
	    { 3, 3 },
	}
};
/** Miniset: Bloody gib 1. */
const Miniset PANCREAS1 {
	{ 5, 3 },
	{
	    { 3, 3, 3, 3, 3 },
	    { 3, 3, 3, 3, 3 },
	    { 3, 3, 3, 3, 3 },
	},
	{
	    { 0, 0, 0, 0, 0 },
	    { 0, 0, 108, 0, 0 },
	    { 0, 0, 0, 0, 0 },
	}
};
/** Miniset: Bloody gib 2. */
const Miniset PANCREAS2 {
	{ 5, 3 },
	{
	    { 3, 3, 3, 3, 3 },
	    { 3, 3, 3, 3, 3 },
	    { 3, 3, 3, 3, 3 },
	},
	{
	    { 0, 0, 0, 0, 0 },
	    { 0, 0, 110, 0, 0 },
	    { 0, 0, 0, 0, 0 },
	}
};
/** Miniset: Move vertical doors away from west pillar 1. */
const Miniset CTRDOOR1 {
	{ 3, 3 },
	{
	    { 3, 1, 3 },
	    { 0, 4, 0 },
	    { 0, 9, 0 },
	},
	{
	    { 0, 4, 0 },
	    { 0, 1, 0 },
	    { 0, 0, 0 },
	}
};
/** Miniset: Move vertical doors away from west pillar 2. */
const Miniset CTRDOOR2 {
	{ 3, 3 },
	{
	    { 3, 1, 3 },
	    { 0, 4, 0 },
	    { 0, 8, 0 },
	},
	{
	    { 0, 4, 0 },
	    { 0, 1, 0 },
	    { 0, 0, 0 },
	}
};
/** Miniset: Move vertical doors away from west pillar 3. */
const Miniset CTRDOOR3 {
	{ 3, 3 },
	{
	    { 3, 1, 3 },
	    { 0, 4, 0 },
	    { 0, 6, 0 },
	},
	{
	    { 0, 4, 0 },
	    { 0, 1, 0 },
	    { 0, 0, 0 },
	}
};
/** Miniset: Move vertical doors away from west pillar 4. */
const Miniset CTRDOOR4 {
	{ 3, 3 },
	{
	    { 3, 1, 3 },
	    { 0, 4, 0 },
	    { 0, 7, 0 },
	},
	{
	    { 0, 4, 0 },
	    { 0, 1, 0 },
	    { 0, 0, 0 },
	}
};
/** Miniset: Move vertical doors away from west pillar 5. */
const Miniset CTRDOOR5 {
	{ 3, 3 },
	{
	    { 3, 1, 3 },
	    { 0, 4, 0 },
	    { 0, 15, 0 },
	},
	{
	    { 0, 4, 0 },
	    { 0, 1, 0 },
	    { 0, 0, 0 },
	}
};
/** Miniset: Move vertical doors away from west pillar 6. */
const Miniset CTRDOOR6 {
	{ 3, 3 },
	{
	    { 3, 1, 3 },
	    { 0, 4, 0 },
	    { 0, 13, 0 },
	},
	{
	    { 0, 4, 0 },
	    { 0, 1, 0 },
	    { 0, 0, 0 },
	}
};
/** Miniset: Move vertical doors away from west pillar 7. */
const Miniset CTRDOOR7 {
	{ 3, 3 },
	{
	    { 3, 1, 3 },
	    { 0, 4, 0 },
	    { 0, 16, 0 },
	},
	{
	    { 0, 4, 0 },
	    { 0, 1, 0 },
	    { 0, 0, 0 },
	}
};
/** Miniset: Move vertical doors away from west pillar 8. */
const Miniset CTRDOOR8 {
	{ 3, 3 },
	{
	    { 3, 1, 3 },
	    { 0, 4, 0 },
	    { 0, 14, 0 },
	},
	{
	    { 0, 4, 0 },
	    { 0, 1, 0 },
	    { 0, 0, 0 },
	}
};

int Patterns[100][10] = {
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 3 },
	{ 0, 0, 0, 0, 2, 0, 0, 0, 0, 3 },
	{ 0, 7, 0, 0, 1, 0, 0, 5, 0, 2 },
	{ 0, 5, 0, 0, 1, 0, 0, 7, 0, 2 },
	{ 0, 0, 0, 7, 1, 5, 0, 0, 0, 1 },
	{ 0, 0, 0, 5, 1, 7, 0, 0, 0, 1 },
	{ 0, 1, 0, 0, 3, 0, 0, 1, 0, 4 },
	{ 0, 0, 0, 1, 3, 1, 0, 0, 0, 5 },
	{ 0, 6, 0, 6, 1, 0, 0, 0, 0, 6 },
	{ 0, 6, 0, 0, 1, 6, 0, 0, 0, 9 },
	{ 0, 0, 0, 6, 1, 0, 0, 6, 0, 7 },
	{ 0, 0, 0, 0, 1, 6, 0, 6, 0, 8 },
	{ 0, 6, 0, 6, 6, 0, 8, 6, 0, 7 },
	{ 0, 6, 8, 6, 6, 6, 0, 0, 0, 9 },
	{ 0, 6, 0, 0, 6, 6, 0, 6, 8, 8 },
	{ 6, 6, 6, 6, 6, 6, 0, 6, 0, 8 },
	{ 2, 6, 6, 6, 6, 6, 0, 6, 0, 8 },
	{ 7, 7, 7, 6, 6, 6, 0, 6, 0, 8 },
	{ 6, 6, 2, 6, 6, 6, 0, 6, 0, 8 },
	{ 6, 2, 6, 6, 6, 6, 0, 6, 0, 8 },
	{ 2, 6, 6, 6, 6, 6, 0, 6, 0, 8 },
	{ 6, 7, 7, 6, 6, 6, 0, 6, 0, 8 },
	{ 4, 4, 6, 6, 6, 6, 2, 6, 2, 8 },
	{ 2, 2, 2, 2, 6, 2, 2, 6, 2, 7 },
	{ 2, 2, 2, 2, 6, 2, 6, 6, 6, 7 },
	{ 2, 2, 6, 2, 6, 6, 2, 2, 6, 9 },
	{ 2, 6, 2, 2, 6, 2, 2, 2, 2, 6 },
	{ 2, 2, 2, 2, 6, 6, 2, 2, 2, 9 },
	{ 2, 2, 2, 6, 6, 2, 2, 2, 2, 6 },
	{ 2, 2, 0, 2, 6, 6, 2, 2, 0, 9 },
	{ 0, 0, 0, 0, 4, 0, 0, 0, 0, 12 },
	{ 0, 1, 0, 0, 1, 4, 0, 1, 0, 10 },
	{ 0, 0, 0, 1, 1, 1, 0, 4, 0, 11 },
	{ 0, 0, 0, 6, 1, 4, 0, 1, 0, 14 },
	{ 0, 6, 0, 1, 1, 0, 0, 4, 0, 16 },
	{ 0, 6, 0, 0, 1, 1, 0, 4, 0, 15 },
	{ 0, 0, 0, 0, 1, 1, 0, 1, 4, 13 },
	{ 8, 8, 8, 8, 1, 1, 0, 1, 1, 13 },
	{ 8, 8, 4, 8, 1, 1, 0, 1, 1, 10 },
	{ 0, 0, 0, 1, 1, 1, 1, 1, 1, 11 },
	{ 1, 1, 1, 1, 1, 1, 2, 2, 8, 2 },
	{ 0, 1, 0, 1, 1, 4, 1, 1, 0, 16 },
	{ 0, 0, 0, 1, 1, 1, 1, 1, 4, 11 },
	{ 1, 1, 4, 1, 1, 1, 0, 2, 2, 2 },
	{ 1, 1, 1, 1, 1, 1, 6, 2, 6, 2 },
	{ 4, 1, 1, 1, 1, 1, 6, 2, 6, 2 },
	{ 2, 2, 2, 1, 1, 1, 4, 1, 1, 11 },
	{ 4, 1, 1, 1, 1, 1, 2, 2, 2, 2 },
	{ 1, 1, 4, 1, 1, 1, 2, 2, 1, 2 },
	{ 4, 1, 1, 1, 1, 1, 1, 2, 2, 2 },
	{ 2, 2, 6, 1, 1, 1, 4, 1, 1, 11 },
	{ 4, 1, 1, 1, 1, 1, 2, 2, 6, 2 },
	{ 1, 2, 2, 1, 1, 1, 4, 1, 1, 11 },
	{ 0, 1, 1, 0, 1, 1, 0, 1, 1, 10 },
	{ 2, 1, 1, 3, 1, 1, 2, 1, 1, 14 },
	{ 1, 1, 0, 1, 1, 2, 1, 1, 0, 1 },
	{ 0, 4, 0, 1, 1, 1, 0, 1, 1, 14 },
	{ 4, 1, 0, 1, 1, 0, 1, 1, 0, 1 },
	{ 0, 1, 0, 4, 1, 1, 0, 1, 1, 15 },
	{ 1, 1, 1, 1, 1, 1, 0, 2, 2, 2 },
	{ 0, 1, 1, 2, 1, 1, 2, 1, 4, 10 },
	{ 2, 1, 1, 1, 1, 1, 0, 4, 0, 16 },
	{ 1, 1, 4, 1, 1, 2, 0, 1, 2, 1 },
	{ 2, 1, 1, 2, 1, 1, 1, 1, 4, 10 },
	{ 1, 1, 2, 1, 1, 2, 4, 1, 8, 1 },
	{ 2, 1, 4, 1, 1, 1, 4, 4, 1, 16 },
	{ 2, 1, 1, 1, 1, 1, 1, 1, 1, 16 },
	{ 1, 1, 2, 1, 1, 1, 1, 1, 1, 15 },
	{ 1, 1, 1, 1, 1, 1, 2, 1, 1, 14 },
	{ 4, 1, 1, 1, 1, 1, 2, 1, 1, 14 },
	{ 1, 1, 1, 1, 1, 1, 1, 1, 2, 8 },
	{ 0, 0, 0, 0, 255, 0, 0, 0, 0, 0 },
};

void ApplyShadowsPatterns()
{
	uint8_t sd[2][2];

	for (int y = 1; y < DMAXY; y++) {
		for (int x = 1; x < DMAXX; x++) {
			sd[0][0] = BSTYPESL2[dungeon[x][y]];
			sd[1][0] = BSTYPESL2[dungeon[x - 1][y]];
			sd[0][1] = BSTYPESL2[dungeon[x][y - 1]];
			sd[1][1] = BSTYPESL2[dungeon[x - 1][y - 1]];

			for (const auto &shadow : SPATSL2) {
				if (shadow.strig != sd[0][0])
					continue;
				if (shadow.s1 != 0 && shadow.s1 != sd[1][1])
					continue;
				if (shadow.s2 != 0 && shadow.s2 != sd[0][1])
					continue;
				if (shadow.s3 != 0 && shadow.s3 != sd[1][0])
					continue;

				if (shadow.nv1 != 0) {
					dungeon[x - 1][y - 1] = shadow.nv1;
				}
				if (shadow.nv2 != 0) {
					dungeon[x][y - 1] = shadow.nv2;
				}
				if (shadow.nv3 != 0) {
					dungeon[x - 1][y] = shadow.nv3;
				}
			}
		}
	}
}

void PlaceMiniSetRandom(const Miniset &miniset, int rndper)
{
	const WorldTileCoord sw = miniset.size.width;
	const WorldTileCoord sh = miniset.size.height;

	for (WorldTileCoord sy = 0; sy < DMAXY - sh; sy++) {
		for (WorldTileCoord sx = 0; sx < DMAXX - sw; sx++) {
			if (SetPieceRoom.contains(sx, sy))
				continue;
			if (!miniset.matches({ sx, sy }))
				continue;
			bool found = true;
			for (int yy = std::max(sy - sh, 0); yy < std::min(sy + (2 * sh), DMAXY) && found; yy++) {
				for (int xx = std::max(sx - sw, 0); xx < std::min(sx + (2 * sw), DMAXX); xx++) {
					if (dungeon[xx][yy] == miniset.replace[0][0]) {
						found = false;
						break;
					}
				}
			}
			if (!found)
				continue;
			if (GenerateRnd(100) >= rndper)
				continue;
			miniset.place({ sx, sy });
		}
	}
}

void PlaceMiniSetRandom1x1(uint8_t search, uint8_t replace, int rndper)
{
	PlaceMiniSetRandom({ { 1, 1 }, { { search } }, { { replace } } }, rndper);
}

void InitSetPiece()
{
	std::unique_ptr<uint16_t[]> setPieceData;

	if (Quests[Q_BLIND].IsAvailable()) {
		setPieceData = LoadFileInMem<uint16_t>("levels\\l2data\\blind1.dun");
	} else if (Quests[Q_BLOOD].IsAvailable()) {
		setPieceData = LoadFileInMem<uint16_t>("levels\\l2data\\blood1.dun");
	} else if (Quests[Q_SCHAMB].IsAvailable()) {
		setPieceData = LoadFileInMem<uint16_t>("levels\\l2data\\bonestr2.dun");
	} else {
		return; // no setpiece needed for this level
	}

	const WorldTilePosition setPiecePosition = SetPieceRoom.position;
	PlaceDunTiles(setPieceData.get(), setPiecePosition, 3);
	SetPiece = { setPiecePosition, GetDunSize(setPieceData.get()) };
}

void InitDungeonPieces()
{
	for (int j = 0; j < MAXDUNY; j++) {
		for (int i = 0; i < MAXDUNX; i++) {
			int8_t pc;
			if (IsAnyOf(dPiece[i][j], 540, 177, 550)) {
				pc = 5;
			} else if (IsAnyOf(dPiece[i][j], 541, 552)) {
				pc = 6;
			} else {
				continue;
			}
			dSpecial[i][j] = pc;
		}
	}
	for (int j = 0; j < MAXDUNY; j++) {
		for (int i = 0; i < MAXDUNX; i++) {
			if (dPiece[i][j] == 131) {
				dSpecial[i][j + 1] = 2;
				dSpecial[i][j + 2] = 1;
			} else if (dPiece[i][j] == 134 || dPiece[i][j] == 138) {
				dSpecial[i + 1][j] = 3;
				dSpecial[i + 2][j] = 4;
			}
		}
	}
}

void InitDungeonFlags()
{
	Protected.reset();
	memset(predungeon, ' ', sizeof(predungeon));
}

void MapRoom(int x1, int y1, int x2, int y2)
{
	for (int jj = y1; jj <= y2; jj++) {
		for (int ii = x1; ii <= x2; ii++) {
			predungeon[ii][jj] = '.';
		}
	}
	for (int jj = y1; jj <= y2; jj++) {
		predungeon[x1][jj] = '#';
		predungeon[x2][jj] = '#';
	}
	for (int ii = x1; ii <= x2; ii++) {
		predungeon[ii][y1] = '#';
		predungeon[ii][y2] = '#';
	}
}

void DefineRoom(Point topLeft, Point bottomRight, bool forceHW)
{
	predungeon[topLeft.x][topLeft.y] = 'C';
	predungeon[topLeft.x][bottomRight.y] = 'E';
	predungeon[bottomRight.x][topLeft.y] = 'B';
	predungeon[bottomRight.x][bottomRight.y] = 'A';

	nRoomCnt++;
	RoomList[nRoomCnt] = { topLeft, bottomRight };

	if (forceHW) {
		for (int i = topLeft.x; i < bottomRight.x; i++) {
			/// BUGFIX: Should loop j between nY1 and nY2 instead of always using nY1.
			while (i < bottomRight.y) {
				Protected.set(i, topLeft.y);
				i++;
			}
		}
	}
	for (int i = topLeft.x + 1; i <= bottomRight.x - 1; i++) {
		predungeon[i][topLeft.y] = '#';
		predungeon[i][bottomRight.y] = '#';
	}
	bottomRight.y--;
	for (int j = topLeft.y + 1; j <= bottomRight.y; j++) {
		predungeon[topLeft.x][j] = '#';
		predungeon[bottomRight.x][j] = '#';
		for (int i = topLeft.x + 1; i < bottomRight.x; i++) {
			predungeon[i][j] = '.';
		}
	}
}

void CreateDoorType(Point position)
{
	if (predungeon[position.x - 1][position.y] == 'D')
		return;
	if (predungeon[position.x + 1][position.y] == 'D')
		return;
	if (predungeon[position.x][position.y - 1] == 'D')
		return;
	if (predungeon[position.x][position.y + 1] == 'D')
		return;
	if (IsAnyOf(predungeon[position.x][position.y], 'A', 'B', 'C', 'E'))
		return;

	predungeon[position.x][position.y] = 'D';
}

void PlaceHallExt(Point position)
{
	if (predungeon[position.x][position.y] == ' ')
		predungeon[position.x][position.y] = ',';
}

/**
 * Draws a random room rectangle, and then subdivides the rest of the passed in rectangle into 4 and recurses.
 * @param topLeft Lower X and Y boundaries of the area to draw into.
 * @param bottomRight Upper X and Y boundaries of the area to draw into.
 * @param nRDest The room number of the parent room this call was invoked for. Zero for empty
 * @param nHDir The direction of the hall from nRDest to this room.
 * @param size If set, is is used used for room size instead of random values.
 */
void CreateRoom(WorldTilePosition topLeft, WorldTilePosition bottomRight, int nRDest, HallDirection nHDir, std::optional<WorldTileSize> size)
{
	constexpr int AreaMin = 2;
	if (nRoomCnt >= 80 || topLeft.x + AreaMin > bottomRight.x || topLeft.y + AreaMin > bottomRight.y)
		return;

	const WorldTileDisplacement areaDisplacement = bottomRight - topLeft;
	const WorldTileSize area(areaDisplacement.deltaX, areaDisplacement.deltaY);

	constexpr WorldTileCoord RoomMax = 10;
	constexpr WorldTileCoord RoomMin = 4;
	WorldTileSize roomSize = area;
	if (area.width > RoomMin)
		roomSize.width = GenerateRnd(std::min(area.width, RoomMax) - RoomMin) + RoomMin;
	if (area.height > RoomMin)
		roomSize.height = GenerateRnd(std::min(area.height, RoomMax) - RoomMin) + RoomMin;

	if (size)
		roomSize = *size;

	const int32_t randomWidth = GenerateRnd(area.width);
	const int32_t randomHeight = GenerateRnd(area.height);

	WorldTilePosition roomTopLeft = topLeft + WorldTileDisplacement(randomWidth, randomHeight);
	WorldTilePosition roomBottomRight = roomTopLeft + WorldTileDisplacement(roomSize);
	if (roomBottomRight.x > bottomRight.x) {
		roomBottomRight.x = bottomRight.x;
		roomTopLeft.x = bottomRight.x - roomSize.width;
	}
	if (roomBottomRight.y > bottomRight.y) {
		roomBottomRight.y = bottomRight.y;
		roomTopLeft.y = bottomRight.y - roomSize.height;
	}

	roomTopLeft.x = std::clamp<WorldTileCoord>(roomTopLeft.x, 1, 38);
	roomTopLeft.y = std::clamp<WorldTileCoord>(roomTopLeft.y, 1, 38);
	roomBottomRight.x = std::clamp<WorldTileCoord>(roomBottomRight.x, 1, 38);
	roomBottomRight.y = std::clamp<WorldTileCoord>(roomBottomRight.y, 1, 38);

	DefineRoom(roomTopLeft, roomBottomRight, static_cast<bool>(size));

	constexpr WorldTileDisplacement standoff { 2, 2 };
	if (size)
		SetPieceRoom = { roomTopLeft + standoff, roomSize - 1 };

	const int nRid = nRoomCnt;

	if (nRDest != 0) {
		WorldTileCoord nHx1 = 0;
		WorldTileCoord nHy1 = 0;
		WorldTileCoord nHx2 = 0;
		WorldTileCoord nHy2 = 0;
		if (nHDir == HallDirection::Up) {
			nHx1 = GenerateRnd(roomSize.width - 2) + roomTopLeft.x + 1;
			nHy1 = roomTopLeft.y;
			const int nHw = RoomList[nRDest].bottomRight.x - RoomList[nRDest].topLeft.x - 2;
			nHx2 = GenerateRnd(nHw) + RoomList[nRDest].topLeft.x + 1;
			nHy2 = RoomList[nRDest].bottomRight.y;
		}
		if (nHDir == HallDirection::Down) {
			nHx1 = GenerateRnd(roomSize.width - 2) + roomTopLeft.x + 1;
			nHy1 = roomBottomRight.y;
			const int nHw = RoomList[nRDest].bottomRight.x - RoomList[nRDest].topLeft.x - 2;
			nHx2 = GenerateRnd(nHw) + RoomList[nRDest].topLeft.x + 1;
			nHy2 = RoomList[nRDest].topLeft.y;
		}
		if (nHDir == HallDirection::Right) {
			nHx1 = roomBottomRight.x;
			nHy1 = GenerateRnd(roomSize.height - 2) + roomTopLeft.y + 1;
			nHx2 = RoomList[nRDest].topLeft.x;
			const int nHh = RoomList[nRDest].bottomRight.y - RoomList[nRDest].topLeft.y - 2;
			nHy2 = GenerateRnd(nHh) + RoomList[nRDest].topLeft.y + 1;
		}
		if (nHDir == HallDirection::Left) {
			nHx1 = roomTopLeft.x;
			nHy1 = GenerateRnd(roomSize.height - 2) + roomTopLeft.y + 1;
			nHx2 = RoomList[nRDest].bottomRight.x;
			const int nHh = RoomList[nRDest].bottomRight.y - RoomList[nRDest].topLeft.y - 2;
			nHy2 = GenerateRnd(nHh) + RoomList[nRDest].topLeft.y + 1;
		}
		HallList.push_back({ { nHx1, nHy1 }, { nHx2, nHy2 }, nHDir });
	}

	const WorldTilePosition roomBottomLeft { roomTopLeft.x, roomBottomRight.y };
	const WorldTilePosition roomTopRight { roomBottomRight.x, roomTopLeft.y };
	if (roomSize.height > roomSize.width) {
		CreateRoom(topLeft + standoff, roomBottomLeft - standoff, nRid, HallDirection::Right, {});
		CreateRoom(roomTopRight + standoff, bottomRight - standoff, nRid, HallDirection::Left, {});
		CreateRoom(WorldTilePosition { topLeft.x, roomBottomRight.y } + standoff, WorldTilePosition { roomBottomRight.x, bottomRight.y } - standoff, nRid, HallDirection::Up, {});
		CreateRoom(WorldTilePosition { roomTopLeft.x, topLeft.y } + standoff, WorldTilePosition { bottomRight.x, roomTopLeft.y } - standoff, nRid, HallDirection::Down, {});
	} else {
		CreateRoom(topLeft + standoff, roomTopRight - standoff, nRid, HallDirection::Down, {});
		CreateRoom(roomBottomLeft + standoff, bottomRight - standoff, nRid, HallDirection::Up, {});
		CreateRoom(WorldTilePosition { topLeft.x, roomTopLeft.y } + standoff, WorldTilePosition { roomTopLeft.x, bottomRight.y } - standoff, nRid, HallDirection::Right, {});
		CreateRoom(WorldTilePosition { roomBottomRight.x, topLeft.y } + standoff, WorldTilePosition { bottomRight.x, roomBottomRight.y } - standoff, nRid, HallDirection::Left, {});
	}
}

void ConnectHall(const HallNode &node)
{
	Point beginning = node.beginning;
	Point end = node.end;

	const bool fMinusFlag = GenerateRnd(100) < 50;
	const bool fPlusFlag = GenerateRnd(100) < 50;
	CreateDoorType(beginning);
	CreateDoorType(end);
	HallDirection nCurrd = node.direction;
	end -= DirAdd[static_cast<uint8_t>(nCurrd)];
	predungeon[end.x][end.y] = ',';
	bool fInroom = false;

	do {
		if (beginning.x >= 38 && nCurrd == HallDirection::Right)
			nCurrd = HallDirection::Left;
		if (beginning.y >= 38 && nCurrd == HallDirection::Down)
			nCurrd = HallDirection::Up;
		if (beginning.x <= 1 && nCurrd == HallDirection::Left)
			nCurrd = HallDirection::Right;
		if (beginning.y <= 1 && nCurrd == HallDirection::Up)
			nCurrd = HallDirection::Down;
		if (predungeon[beginning.x][beginning.y] == 'C' && IsAnyOf(nCurrd, HallDirection::Up, HallDirection::Left))
			nCurrd = HallDirection::Right;
		if (predungeon[beginning.x][beginning.y] == 'B' && IsAnyOf(nCurrd, HallDirection::Up, HallDirection::Right))
			nCurrd = HallDirection::Down;
		if (predungeon[beginning.x][beginning.y] == 'E' && IsAnyOf(nCurrd, HallDirection::Left, HallDirection::Down))
			nCurrd = HallDirection::Up;
		if (predungeon[beginning.x][beginning.y] == 'A' && IsAnyOf(nCurrd, HallDirection::Right, HallDirection::Down))
			nCurrd = HallDirection::Left;
		beginning += DirAdd[static_cast<uint8_t>(nCurrd)];
		if (predungeon[beginning.x][beginning.y] == ' ') {
			if (fInroom) {
				CreateDoorType(beginning - DirAdd[static_cast<uint8_t>(nCurrd)]);
				fInroom = false;
			} else {
				if (fMinusFlag) {
					if (IsNoneOf(nCurrd, HallDirection::Up, HallDirection::Down))
						PlaceHallExt(beginning + Displacement { 0, -1 }); // Up
					else
						PlaceHallExt(beginning + Displacement { -1, 0 }); // Left
				}
				if (fPlusFlag) {
					if (IsNoneOf(nCurrd, HallDirection::Up, HallDirection::Down))
						PlaceHallExt(beginning + Displacement { 0, 1 }); // Down
					else
						PlaceHallExt(beginning + Displacement { 1, 0 }); // Right
				}
			}
			predungeon[beginning.x][beginning.y] = ',';
		} else {
			if (!fInroom && predungeon[beginning.x][beginning.y] == '#')
				CreateDoorType(beginning);
			if (predungeon[beginning.x][beginning.y] != ',')
				fInroom = true;
		}
		const int nDx = std::abs(end.x - beginning.x);
		const int nDy = std::abs(end.y - beginning.y);
		if (nDx > nDy) {
			const int nRp = std::min(2 * nDx, 30);
			if (GenerateRnd(100) < nRp) {
				if (end.x <= beginning.x || beginning.x >= DMAXX)
					nCurrd = HallDirection::Left;
				else
					nCurrd = HallDirection::Right;
			}
		} else {
			const int nRp = std::min(5 * nDy, 80);
			if (GenerateRnd(100) < nRp) {
				if (end.y <= beginning.y || beginning.y >= DMAXY)
					nCurrd = HallDirection::Up;
				else
					nCurrd = HallDirection::Down;
			}
		}
		if (nDy < 10 && beginning.x == end.x && IsAnyOf(nCurrd, HallDirection::Right, HallDirection::Left)) {
			if (end.y <= beginning.y || beginning.y >= DMAXY)
				nCurrd = HallDirection::Up;
			else
				nCurrd = HallDirection::Down;
		}
		if (nDx < 10 && beginning.y == end.y && IsAnyOf(nCurrd, HallDirection::Up, HallDirection::Down)) {
			if (end.x <= beginning.x || beginning.x >= DMAXX)
				nCurrd = HallDirection::Left;
			else
				nCurrd = HallDirection::Right;
		}
		if (nDy == 1 && nDx > 1 && IsAnyOf(nCurrd, HallDirection::Up, HallDirection::Down)) {
			if (end.x <= beginning.x || beginning.x >= DMAXX)
				nCurrd = HallDirection::Left;
			else
				nCurrd = HallDirection::Right;
		}
		if (nDx == 1 && nDy > 1 && IsAnyOf(nCurrd, HallDirection::Right, HallDirection::Left)) {
			if (end.y <= beginning.y || beginning.x >= DMAXX)
				nCurrd = HallDirection::Up;
			else
				nCurrd = HallDirection::Down;
		}
		if (nDx == 0 && predungeon[beginning.x][beginning.y] != ' ' && IsAnyOf(nCurrd, HallDirection::Right, HallDirection::Left)) {
			if (end.x <= node.beginning.x || beginning.x >= DMAXX)
				nCurrd = HallDirection::Up;
			else
				nCurrd = HallDirection::Down;
		}
		if (nDy == 0 && predungeon[beginning.x][beginning.y] != ' ' && IsAnyOf(nCurrd, HallDirection::Up, HallDirection::Down)) {
			if (end.y <= node.beginning.y || beginning.y >= DMAXY)
				nCurrd = HallDirection::Left;
			else
				nCurrd = HallDirection::Right;
		}
	} while (beginning != end);
}

void DoPatternCheck(int i, int j)
{
	for (int k = 0; Patterns[k][4] != 255; k++) {
		int x = i - 1;
		int y = j - 1;
		int nOk = 254;
		for (int l = 0; l < 9 && nOk == 254; l++) {
			nOk = 255;
			if (l == 3 || l == 6) {
				y++;
				x = i - 1;
			}
			if (x >= 0 && x < DMAXX && y >= 0 && y < DMAXY) {
				switch (Patterns[k][l]) {
				case 0:
					nOk = 254;
					break;
				case 1:
					if (predungeon[x][y] == '#') {
						nOk = 254;
					}
					break;
				case 2:
					if (predungeon[x][y] == '.') {
						nOk = 254;
					}
					break;
				case 4:
					if (predungeon[x][y] == ' ') {
						nOk = 254;
					}
					break;
				case 3:
					if (predungeon[x][y] == 'D') {
						nOk = 254;
					}
					break;
				case 5:
					if (predungeon[x][y] == 'D' || predungeon[x][y] == '.') {
						nOk = 254;
					}
					break;
				case 6:
					if (predungeon[x][y] == 'D' || predungeon[x][y] == '#') {
						nOk = 254;
					}
					break;
				case 7:
					if (predungeon[x][y] == ' ' || predungeon[x][y] == '.') {
						nOk = 254;
					}
					break;
				case 8:
					if (predungeon[x][y] == 'D' || predungeon[x][y] == '#' || predungeon[x][y] == '.') {
						nOk = 254;
					}
					break;
				}
			} else {
				nOk = 254;
			}
			x++;
		}
		if (nOk == 254) {
			dungeon[i][j] = Patterns[k][9];
		}
	}
}

void FixTilesPatterns()
{
	for (int j = 0; j < DMAXY; j++) {
		for (int i = 0; i < DMAXX; i++) {
			if (dungeon[i][j] == 1 && dungeon[i][j + 1] == 3) {
				dungeon[i][j + 1] = 1;
			}
			if (dungeon[i][j] == 3 && dungeon[i][j + 1] == 1) {
				dungeon[i][j + 1] = 3;
			}
			if (dungeon[i][j] == 3 && dungeon[i + 1][j] == 7) {
				dungeon[i + 1][j] = 3;
			}
			if (dungeon[i][j] == 2 && dungeon[i + 1][j] == 3) {
				dungeon[i + 1][j] = 2;
			}
			if (dungeon[i][j] == 11 && dungeon[i + 1][j] == 14) {
				dungeon[i + 1][j] = 16;
			}
			if (dungeon[i][j] == 15 && dungeon[i][j + 1] == 1) {
				dungeon[i][j + 1] = 8;
			}
		}
	}
}

void Substitution()
{
	for (WorldTileCoord y = 0; y < DMAXY; y++) {
		for (WorldTileCoord x = 0; x < DMAXX; x++) {
			if (SetPieceRoom.contains(x, y))
				continue;
			if (!FlipCoin(4))
				continue;

			const uint8_t c = BTYPESL2[dungeon[x][y]];
			if (c != 0) {
				int rv = GenerateRnd(16);
				int i = -1;
				while (rv >= 0) {
					i++;
					if (i == sizeof(BTYPESL2)) {
						i = 0;
					}
					if (c == BTYPESL2[i]) {
						rv--;
					}
				}

				int j;
				for (j = y - 2; j < y + 2; j++) {
					for (int k = x - 2; k < x + 2; k++) {
						if (dungeon[k][j] == i) {
							j = y + 3;
							k = x + 2;
						}
					}
				}
				if (j < y + 3) {
					dungeon[x][y] = i;
				}
			}
		}
	}
}

int CountEmptyTiles()
{
	int t = 0;
	for (int jj = 0; jj < DMAXY; jj++) {
		for (int ii = 0; ii < DMAXX; ii++) { // NOLINT(modernize-loop-convert)
			if (predungeon[ii][jj] == ' ') {
				t++;
			}
		}
	}

	return t;
}

void KnockWalls(int x1, int y1, int x2, int y2)
{
	for (int ii = x1 + 1; ii < x2; ii++) {
		if (predungeon[ii][y1 - 1] == '.' && predungeon[ii][y1 + 1] == '.') {
			predungeon[ii][y1] = '.';
		}
		if (predungeon[ii][y2 - 1] == '.' && predungeon[ii][y2 + 1] == '.') {
			predungeon[ii][y2] = '.';
		}
		if (predungeon[ii][y1 - 1] == 'D') {
			predungeon[ii][y1 - 1] = '.';
		}
		if (predungeon[ii][y2 + 1] == 'D') {
			predungeon[ii][y2 + 1] = '.';
		}
	}
	for (int jj = y1 + 1; jj < y2; jj++) {
		if (predungeon[x1 - 1][jj] == '.' && predungeon[x1 + 1][jj] == '.') {
			predungeon[x1][jj] = '.';
		}
		if (predungeon[x2 - 1][jj] == '.' && predungeon[x2 + 1][jj] == '.') {
			predungeon[x2][jj] = '.';
		}
		if (predungeon[x1 - 1][jj] == 'D') {
			predungeon[x1 - 1][jj] = '.';
		}
		if (predungeon[x2 + 1][jj] == 'D') {
			predungeon[x2 + 1][jj] = '.';
		}
	}
}

void FillVoid(bool xf1, bool yf1, bool xf2, bool yf2, int xx, int yy)
{
	int x1 = xx;
	if (xf1) {
		x1--;
	}
	int x2 = xx;
	if (xf2) {
		x2++;
	}
	int y1 = yy;
	if (yf1) {
		y1--;
	}
	int y2 = yy;
	if (yf2) {
		y2++;
	}
	if (!xf1) {
		while (yf1 || yf2) {
			if (y1 == 0) {
				yf1 = false;
			}
			if (y2 == DMAXY - 1) {
				yf2 = false;
			}
			if (y2 - y1 >= 14) {
				yf1 = false;
				yf2 = false;
			}
			if (yf1) {
				y1--;
			}
			if (yf2) {
				y2++;
			}
			if (predungeon[x2][y1] != ' ') {
				yf1 = false;
			}
			if (predungeon[x2][y2] != ' ') {
				yf2 = false;
			}
		}
		y1 += 2;
		y2 -= 2;
		if (y2 - y1 > 5) {
			while (xf2) {
				if (x2 == 39) {
					xf2 = false;
				}
				if (x2 - x1 >= 12) {
					xf2 = false;
				}
				for (int jj = y1; jj <= y2; jj++) {
					if (predungeon[x2][jj] != ' ') {
						xf2 = false;
					}
				}
				if (xf2) {
					x2++;
				}
			}
			x2 -= 2;
			if (x2 - x1 > 5) {
				MapRoom(x1, y1, x2, y2);
				KnockWalls(x1, y1, x2, y2);
			}
		}
	} else if (!xf2) {
		while (yf1 || yf2) {
			if (y1 == 0) {
				yf1 = false;
			}
			if (y2 == DMAXY - 1) {
				yf2 = false;
			}
			if (y2 - y1 >= 14) {
				yf1 = false;
				yf2 = false;
			}
			if (yf1) {
				y1--;
			}
			if (yf2) {
				y2++;
			}
			if (predungeon[x1][y1] != ' ') {
				yf1 = false;
			}
			if (predungeon[x1][y2] != ' ') {
				yf2 = false;
			}
		}
		y1 += 2;
		y2 -= 2;
		if (y2 - y1 > 5) {
			while (xf1) {
				if (x1 == 0) {
					xf1 = false;
				}
				if (x2 - x1 >= 12) {
					xf1 = false;
				}
				for (int jj = y1; jj <= y2; jj++) {
					if (predungeon[x1][jj] != ' ') {
						xf1 = false;
					}
				}
				if (xf1) {
					x1--;
				}
			}
			x1 += 2;
			if (x2 - x1 > 5) {
				MapRoom(x1, y1, x2, y2);
				KnockWalls(x1, y1, x2, y2);
			}
		}
	} else if (!yf1) {
		while (xf1 || xf2) {
			if (x1 == 0) {
				xf1 = false;
			}
			if (x2 == DMAXX - 1) {
				xf2 = false;
			}
			if (x2 - x1 >= 14) {
				xf1 = false;
				xf2 = false;
			}
			if (xf1) {
				x1--;
			}
			if (xf2) {
				x2++;
			}
			if (predungeon[x1][y2] != ' ') {
				xf1 = false;
			}
			if (predungeon[x2][y2] != ' ') {
				xf2 = false;
			}
		}
		x1 += 2;
		x2 -= 2;
		if (x2 - x1 > 5) {
			while (yf2) {
				if (y2 == DMAXY - 1) {
					yf2 = false;
				}
				if (y2 - y1 >= 12) {
					yf2 = false;
				}
				for (int ii = x1; ii <= x2; ii++) {
					if (predungeon[ii][y2] != ' ') {
						yf2 = false;
					}
				}
				if (yf2) {
					y2++;
				}
			}
			y2 -= 2;
			if (y2 - y1 > 5) {
				MapRoom(x1, y1, x2, y2);
				KnockWalls(x1, y1, x2, y2);
			}
		}
	} else if (!yf2) {
		while (xf1 || xf2) {
			if (x1 == 0) {
				xf1 = false;
			}
			if (x2 == DMAXX - 1) {
				xf2 = false;
			}
			if (x2 - x1 >= 14) {
				xf1 = false;
				xf2 = false;
			}
			if (xf1) {
				x1--;
			}
			if (xf2) {
				x2++;
			}
			if (predungeon[x1][y1] != ' ') {
				xf1 = false;
			}
			if (predungeon[x2][y1] != ' ') {
				xf2 = false;
			}
		}
		x1 += 2;
		x2 -= 2;
		if (x2 - x1 > 5) {
			while (yf1) {
				if (y1 == 0) {
					yf1 = false;
				}
				if (y2 - y1 >= 12) {
					yf1 = false;
				}
				for (int ii = x1; ii <= x2; ii++) {
					if (predungeon[ii][y1] != ' ') {
						yf1 = false;
					}
				}
				if (yf1) {
					y1--;
				}
			}
			y1 += 2;
			if (y2 - y1 > 5) {
				MapRoom(x1, y1, x2, y2);
				KnockWalls(x1, y1, x2, y2);
			}
		}
	}
}

bool FillVoids()
{
	int to = 0;
	while (CountEmptyTiles() > 700 && to < 100) {
		const int xx = GenerateRnd(38) + 1;
		const int yy = GenerateRnd(38) + 1;
		if (predungeon[xx][yy] != '#') {
			continue;
		}
		bool xf1 = false;
		bool xf2 = false;
		bool yf1 = false;
		bool yf2 = false;
		if (predungeon[xx - 1][yy] == ' ' && predungeon[xx + 1][yy] == '.') {
			if (predungeon[xx + 1][yy - 1] == '.'
			    && predungeon[xx + 1][yy + 1] == '.'
			    && predungeon[xx - 1][yy - 1] == ' '
			    && predungeon[xx - 1][yy + 1] == ' ') {
				xf1 = true;
				yf1 = true;
				yf2 = true;
			}
		} else if (predungeon[xx + 1][yy] == ' ' && predungeon[xx - 1][yy] == '.') {
			if (predungeon[xx - 1][yy - 1] == '.'
			    && predungeon[xx - 1][yy + 1] == '.'
			    && predungeon[xx + 1][yy - 1] == ' '
			    && predungeon[xx + 1][yy + 1] == ' ') {
				xf2 = true;
				yf1 = true;
				yf2 = true;
			}
		} else if (predungeon[xx][yy - 1] == ' ' && predungeon[xx][yy + 1] == '.') {
			if (predungeon[xx - 1][yy + 1] == '.'
			    && predungeon[xx + 1][yy + 1] == '.'
			    && predungeon[xx - 1][yy - 1] == ' '
			    && predungeon[xx + 1][yy - 1] == ' ') {
				yf1 = true;
				xf1 = true;
				xf2 = true;
			}
		} else if (predungeon[xx][yy + 1] == ' ' && predungeon[xx][yy - 1] == '.') {
			if (predungeon[xx - 1][yy - 1] == '.'
			    && predungeon[xx + 1][yy - 1] == '.'
			    && predungeon[xx - 1][yy + 1] == ' '
			    && predungeon[xx + 1][yy + 1] == ' ') {
				yf2 = true;
				xf1 = true;
				xf2 = true;
			}
		}
		if (xf1 || yf1 || xf2 || yf2) {
			FillVoid(xf1, yf1, xf2, yf2, xx, yy);
		}
		to++;
	}

	return CountEmptyTiles() <= 700;
}

bool CreateDungeon()
{
	std::optional<WorldTileSize> size;

	switch (currlevel) {
	case 5:
		if (Quests[Q_BLOOD]._qactive != QUEST_NOTAVAIL)
			size = { 14, 20 };
		break;
	case 6:
		if (Quests[Q_SCHAMB]._qactive != QUEST_NOTAVAIL)
			size = { 10, 10 };
		break;
	case 7:
		if (Quests[Q_BLIND]._qactive != QUEST_NOTAVAIL)
			size = { 15, 15 };
		break;
	case 8:
		break;
	}

	CreateRoom({ 2, 2 }, { DMAXX - 1, DMAXY - 1 }, 0, HallDirection::None, size);

	while (!HallList.empty()) {
		ConnectHall(HallList.front());
		HallList.pop_front();
	}

	for (int j = 0; j < DMAXY; j++) {     /// BUGFIX: change '<=' to '<' (fixed)
		for (int i = 0; i < DMAXX; i++) { /// BUGFIX: change '<=' to '<' (fixed)
			if (IsAnyOf(predungeon[i][j], 'A', 'B', 'C', 'E')) {
				predungeon[i][j] = '#';
			}
			if (predungeon[i][j] == ',') {
				predungeon[i][j] = '.';
				for (int a = -1; a <= 1; a++) {
					for (int b = -1; b <= 1; b++) {
						if (a == 0 && b == 0)
							continue;
						if (i + a < 0 || j + b < 0)
							continue;
						if (i + a >= DMAXX || j + b >= DMAXY)
							continue;
						if (predungeon[i + a][j + b] == ' ') {
							predungeon[i + a][j + b] = '#';
						}
					}
				}
			}
		}
	}

	if (!FillVoids()) {
		return false;
	}

	for (int j = 0; j < DMAXY; j++) {
		for (int i = 0; i < DMAXX; i++) {
			DoPatternCheck(i, j);
		}
	}

	return true;
}

void FixTransparency()
{
	int yy = 16;
	for (int j = 0; j < DMAXY; j++) {
		int xx = 16;
		for (int i = 0; i < DMAXX; i++) {
			// BUGFIX: Should check for `j > 0` first.
			if (dungeon[i][j] == 14 && dungeon[i][j - 1] == 10) {
				dTransVal[xx + 1][yy] = dTransVal[xx][yy];
				dTransVal[xx + 1][yy + 1] = dTransVal[xx][yy];
			}
			// BUGFIX: Should check for `i + 1 < DMAXY` first.
			if (dungeon[i][j] == 15 && dungeon[i + 1][j] == 11) {
				dTransVal[xx][yy + 1] = dTransVal[xx][yy];
				dTransVal[xx + 1][yy + 1] = dTransVal[xx][yy];
			}
			if (dungeon[i][j] == 10) {
				dTransVal[xx + 1][yy] = dTransVal[xx][yy];
				dTransVal[xx + 1][yy + 1] = dTransVal[xx][yy];
			}
			if (dungeon[i][j] == 11) {
				dTransVal[xx][yy + 1] = dTransVal[xx][yy];
				dTransVal[xx + 1][yy + 1] = dTransVal[xx][yy];
			}
			if (dungeon[i][j] == 16) {
				dTransVal[xx + 1][yy] = dTransVal[xx][yy];
				dTransVal[xx][yy + 1] = dTransVal[xx][yy];
				dTransVal[xx + 1][yy + 1] = dTransVal[xx][yy];
			}
			xx += 2;
		}
		yy += 2;
	}
}

void FixDirtTiles()
{
	for (int j = 0; j < DMAXY; j++) {
		for (int i = 0; i < DMAXX; i++) {
			if (dungeon[i][j] == 13 && dungeon[i + 1][j] != 11) {
				dungeon[i][j] = 146;
			}
			if (dungeon[i][j] == 11 && dungeon[i + 1][j] != 11) {
				dungeon[i][j] = 144;
			}
			if (dungeon[i][j] == 15 && dungeon[i + 1][j] != 11) {
				dungeon[i][j] = 148;
			}
			if (dungeon[i][j] == 10 && dungeon[i][j + 1] != 10) {
				dungeon[i][j] = 143;
			}
			if (dungeon[i][j] == 13 && dungeon[i][j + 1] != 10) {
				dungeon[i][j] = 146;
			}
			if (dungeon[i][j] == 14 && dungeon[i][j + 1] != 15) {
				dungeon[i][j] = 147;
			}
		}
	}
}

void FixLockout()
{
	for (int j = 0; j < DMAXY; j++) {
		for (int i = 0; i < DMAXX; i++) {
			if (dungeon[i][j] == 4 && dungeon[i - 1][j] != 3) {
				dungeon[i][j] = 1;
			}
			if (dungeon[i][j] == 5 && dungeon[i][j - 1] != 3) {
				dungeon[i][j] = 2;
			}
		}
	}
	for (int j = 1; j < DMAXY - 1; j++) {
		for (int i = 1; i < DMAXX - 1; i++) {
			if (Protected.test(i, j)) {
				continue;
			}
			if ((dungeon[i][j] == 2 || dungeon[i][j] == 5) && dungeon[i][j - 1] == 3 && dungeon[i][j + 1] == 3) {
				bool doorok = false;
				while (true) {
					if (dungeon[i][j] != 2 && dungeon[i][j] != 5) {
						break;
					}
					if (dungeon[i][j - 1] != 3 || dungeon[i][j + 1] != 3) {
						break;
					}
					if (dungeon[i][j] == 5) {
						doorok = true;
					}
					i++;
				}
				if (!doorok && !Protected.test(i - 1, j)) {
					dungeon[i - 1][j] = 5;
				}
			}
		}
	}
	for (int j = 1; j < DMAXX - 1; j++) { /* check: might be flipped */
		for (int i = 1; i < DMAXY - 1; i++) {
			if (Protected.test(j, i)) {
				continue;
			}
			if ((dungeon[j][i] == 1 || dungeon[j][i] == 4) && dungeon[j - 1][i] == 3 && dungeon[j + 1][i] == 3) {
				bool doorok = false;
				while (true) {
					if (dungeon[j][i] != 1 && dungeon[j][i] != 4) {
						break;
					}
					if (dungeon[j - 1][i] != 3 || dungeon[j + 1][i] != 3) {
						break;
					}
					if (dungeon[j][i] == 4) {
						doorok = true;
					}
					i++;
				}
				if (!doorok && !Protected.test(j, i - 1)) {
					dungeon[j][i - 1] = 4;
				}
			}
		}
	}
}

void FixDoors()
{
	for (int j = 1; j < DMAXY; j++) {
		for (int i = 1; i < DMAXX; i++) {
			if (dungeon[i][j] == 4 && dungeon[i][j - 1] == 3) {
				dungeon[i][j] = 7;
			}
			if (dungeon[i][j] == 5 && dungeon[i - 1][j] == 3) {
				dungeon[i][j] = 9;
			}
		}
	}
}

bool PlaceStairs(lvl_entry entry)
{
	std::optional<Point> position;

	// Place stairs up
	// POE2: the way in is at one end of the floor and the way down at the other.
	position = gbIsPoe2 ? PlaceMiniSetAtFloorEnd(USTAIRS) : PlaceMiniSet(USTAIRS);
	if (!position)
		return false;
	const Point upPosition = *position;
	if (entry == ENTRY_MAIN)
		ViewPosition = position->megaToWorld() + Displacement { 5, 4 };

	// Place stairs down
	position = gbIsPoe2 ? PlaceMiniSetFarFrom(DSTAIRS, upPosition, USTAIRS.size) : PlaceMiniSet(DSTAIRS);
	if (!position)
		return false;
	if (entry == ENTRY_PREV)
		ViewPosition = position->megaToWorld() + Displacement { 4, 6 };

	// Place town warp stairs
	if (currlevel == 5) {
		position = PlaceMiniSet(WARPSTAIRS);
		if (!position)
			return false;
		if (entry == ENTRY_TWARPDN)
			ViewPosition = position->megaToWorld() + Displacement { 5, 4 };
	}

	return true;
}

void DecorateLevel();

void GenerateLevel(lvl_entry entry)
{
	if (LevelSeeds[currlevel])
		SetRndSeed(*LevelSeeds[currlevel]);

	while (true) {
		LevelSeeds[currlevel] = GetLCGEngineState();
		nRoomCnt = 0;
		InitDungeonFlags();
		DRLG_InitTrans();
		if (!CreateDungeon()) {
			continue;
		}
		FixTilesPatterns();
		InitSetPiece();
		FloodTransparencyValues(3);
		FixTransparency();
		if (PlaceStairs(entry))
			break;
	}

	FixLockout();
	FixDoors();
	FixDirtTiles();

	DRLG_PlaceThemeRooms(6, 10, 3, 0, false);
	DecorateLevel();
}

/** The doors moved off corners, the arches, the wall variants, the floor stains and the quest pieces. */
void DecorateLevel()
{
	PlaceMiniSetRandom(CTRDOOR1, 100);
	PlaceMiniSetRandom(CTRDOOR2, 100);
	PlaceMiniSetRandom(CTRDOOR3, 100);
	PlaceMiniSetRandom(CTRDOOR4, 100);
	PlaceMiniSetRandom(CTRDOOR5, 100);
	PlaceMiniSetRandom(CTRDOOR6, 100);
	PlaceMiniSetRandom(CTRDOOR7, 100);
	PlaceMiniSetRandom(CTRDOOR8, 100);
	PlaceMiniSetRandom(VARCH33, 100);
	PlaceMiniSetRandom(VARCH34, 100);
	PlaceMiniSetRandom(VARCH35, 100);
	PlaceMiniSetRandom(VARCH36, 100);
	PlaceMiniSetRandom(VARCH37, 100);
	PlaceMiniSetRandom(VARCH38, 100);
	PlaceMiniSetRandom(VARCH39, 100);
	PlaceMiniSetRandom(VARCH40, 100);
	PlaceMiniSetRandom(VARCH1, 100);
	PlaceMiniSetRandom(VARCH2, 100);
	PlaceMiniSetRandom(VARCH3, 100);
	PlaceMiniSetRandom(VARCH4, 100);
	PlaceMiniSetRandom(VARCH5, 100);
	PlaceMiniSetRandom(VARCH6, 100);
	PlaceMiniSetRandom(VARCH7, 100);
	PlaceMiniSetRandom(VARCH8, 100);
	PlaceMiniSetRandom(VARCH9, 100);
	PlaceMiniSetRandom(VARCH10, 100);
	PlaceMiniSetRandom(VARCH11, 100);
	PlaceMiniSetRandom(VARCH12, 100);
	PlaceMiniSetRandom(VARCH13, 100);
	PlaceMiniSetRandom(VARCH14, 100);
	PlaceMiniSetRandom(VARCH15, 100);
	PlaceMiniSetRandom(VARCH16, 100);
	PlaceMiniSetRandom(VARCH17, 100);
	PlaceMiniSetRandom(VARCH18, 100);
	PlaceMiniSetRandom(VARCH19, 100);
	PlaceMiniSetRandom(VARCH20, 100);
	PlaceMiniSetRandom(VARCH21, 100);
	PlaceMiniSetRandom(VARCH22, 100);
	PlaceMiniSetRandom(VARCH23, 100);
	PlaceMiniSetRandom(VARCH24, 100);
	PlaceMiniSetRandom(VARCH25, 100);
	PlaceMiniSetRandom(VARCH26, 100);
	PlaceMiniSetRandom(VARCH27, 100);
	PlaceMiniSetRandom(VARCH28, 100);
	PlaceMiniSetRandom(VARCH29, 100);
	PlaceMiniSetRandom(VARCH30, 100);
	PlaceMiniSetRandom(VARCH31, 100);
	PlaceMiniSetRandom(VARCH32, 100);
	PlaceMiniSetRandom(HARCH1, 100);
	PlaceMiniSetRandom(HARCH2, 100);
	PlaceMiniSetRandom(HARCH3, 100);
	PlaceMiniSetRandom(HARCH4, 100);
	PlaceMiniSetRandom(HARCH5, 100);
	PlaceMiniSetRandom(HARCH6, 100);
	PlaceMiniSetRandom(HARCH7, 100);
	PlaceMiniSetRandom(HARCH8, 100);
	PlaceMiniSetRandom(HARCH9, 100);
	PlaceMiniSetRandom(HARCH10, 100);
	PlaceMiniSetRandom(HARCH11, 100);
	PlaceMiniSetRandom(HARCH12, 100);
	PlaceMiniSetRandom(HARCH13, 100);
	PlaceMiniSetRandom(HARCH14, 100);
	PlaceMiniSetRandom(HARCH15, 100);
	PlaceMiniSetRandom(HARCH16, 100);
	PlaceMiniSetRandom(HARCH17, 100);
	PlaceMiniSetRandom(HARCH18, 100);
	PlaceMiniSetRandom(HARCH19, 100);
	PlaceMiniSetRandom(HARCH20, 100);
	PlaceMiniSetRandom(HARCH21, 100);
	PlaceMiniSetRandom(HARCH22, 100);
	PlaceMiniSetRandom(HARCH23, 100);
	PlaceMiniSetRandom(HARCH24, 100);
	PlaceMiniSetRandom(HARCH25, 100);
	PlaceMiniSetRandom(HARCH26, 100);
	PlaceMiniSetRandom(HARCH27, 100);
	PlaceMiniSetRandom(HARCH28, 100);
	PlaceMiniSetRandom(HARCH29, 100);
	PlaceMiniSetRandom(HARCH30, 100);
	PlaceMiniSetRandom(HARCH31, 100);
	PlaceMiniSetRandom(HARCH32, 100);
	PlaceMiniSetRandom(HARCH33, 100);
	PlaceMiniSetRandom(HARCH34, 100);
	PlaceMiniSetRandom(HARCH35, 100);
	PlaceMiniSetRandom(HARCH36, 100);
	PlaceMiniSetRandom(HARCH37, 100);
	PlaceMiniSetRandom(HARCH38, 100);
	PlaceMiniSetRandom(HARCH39, 100);
	PlaceMiniSetRandom(HARCH40, 100);
	PlaceMiniSetRandom(CRUSHCOL, 99);
	PlaceMiniSetRandom1x1(1, 80, 10);
	PlaceMiniSetRandom1x1(1, 81, 10);
	PlaceMiniSetRandom1x1(1, 82, 10);
	PlaceMiniSetRandom1x1(2, 84, 10);
	PlaceMiniSetRandom1x1(2, 85, 10);
	PlaceMiniSetRandom1x1(2, 86, 10);
	PlaceMiniSetRandom1x1(8, 87, 50);
	PlaceMiniSetRandom(PANCREAS1, 1);
	PlaceMiniSetRandom(PANCREAS2, 1);
	PlaceMiniSetRandom(BIG1, 3);
	PlaceMiniSetRandom(BIG2, 3);
	PlaceMiniSetRandom(BIG3, 3);
	PlaceMiniSetRandom(BIG4, 3);
	PlaceMiniSetRandom(BIG5, 3);
	PlaceMiniSetRandom(BIG6, 20);
	PlaceMiniSetRandom(BIG7, 20);
	PlaceMiniSetRandom(BIG8, 3);
	PlaceMiniSetRandom(BIG9, 20);
	PlaceMiniSetRandom(BIG10, 20);
	Substitution();
	ApplyShadowsPatterns();

	memcpy(pdungeon, dungeon, sizeof(pdungeon));

	DRLG_CheckQuests(SetPieceRoom.position);
}

/** POE2: the predungeon of a new Catacombs floor: its rooms' floor, the doorways, and a wall round every floor block. */
void MakeCatacombsPredungeon(const CathedralPlan &plan)
{
	memset(predungeon, ' ', sizeof(predungeon));
	for (int y = 0; y < DMAXY; y++) {
		for (int x = 0; x < DMAXX; x++) {
			if (plan.mask.test(x, y))
				predungeon[x][y] = '.';
		}
	}
	// The designed rooms' inner lines start out as floor: their tiles are drawn once Diablo's own passes are done.
	for (const CathedralLine &line : plan.lines)
		predungeon[line.block.x][line.block.y] = '.';
	for (const CathedralDoor &door : plan.doors)
		predungeon[door.block.x][door.block.y] = 'D';
	for (int y = 0; y < DMAXY; y++) {
		for (int x = 0; x < DMAXX; x++) {
			if (predungeon[x][y] != ' ')
				continue;
			for (int dy = -1; dy <= 1 && predungeon[x][y] == ' '; dy++) {
				for (int dx = -1; dx <= 1; dx++) {
					const int nx = x + dx;
					const int ny = y + dy;
					if (nx < 0 || ny < 0 || nx >= DMAXX || ny >= DMAXY)
						continue;
					if (predungeon[nx][ny] == '.' || predungeon[nx][ny] == 'D') {
						predungeon[x][y] = '#';
						break;
					}
				}
			}
		}
	}
}

enum Arm : uint8_t {
	ArmNorth = 1,
	ArmEast = 2,
	ArmSouth = 4,
	ArmWest = 8,
};

/** The tile of an inner wall block from the walls it joins: what Diablo's Patterns give that shape. */
uint8_t LineTile(uint8_t arms)
{
	switch (arms) {
	case ArmNorth | ArmSouth: return 1;
	case ArmEast | ArmWest: return 2;
	case ArmEast | ArmSouth:
	case ArmNorth | ArmEast | ArmSouth:
	case ArmEast | ArmSouth | ArmWest:
	case ArmNorth | ArmEast | ArmSouth | ArmWest: return 8;
	case ArmWest | ArmSouth:
	case ArmNorth | ArmSouth | ArmWest: return 7;
	case ArmNorth | ArmEast:
	case ArmNorth | ArmEast | ArmWest:
	case ArmEast: return 9;
	default:
		// A post: the north-west corner, an east or bottom end, or a pillar.
		return 6;
	}
}

/**
 * POE2: the inner walls, doors, archways, pillars and fallen stone of a new Catacombs floor's designed rooms, drawn
 * straight onto the tiles as Diablo draws its theme rooms. Diablo's own passes would rub out free-standing walls and
 * pillars, and add doors to every wall with floor on both sides.
 * @param floorStamps Filled with the floor tiles to draw once the transparency is flooded (archway shading).
 */
void StampCatacombsLines(const CathedralPlan &plan, std::vector<std::pair<Point, uint8_t>> &floorStamps)
{
	std::array<std::array<char, DMAXY>, DMAXX> kind {};
	for (const CathedralLine &line : plan.lines)
		kind[line.block.x][line.block.y] = line.kind;
	const auto lineAt = [&kind](Point p) {
		return p.x >= 0 && p.y >= 0 && p.x < DMAXX && p.y < DMAXY ? kind[p.x][p.y] : '\0';
	};
	const auto floorAt = [](Point p) {
		return p.x >= 0 && p.y >= 0 && p.x < DMAXX && p.y < DMAXY && dungeon[p.x][p.y] == 3;
	};
	constexpr std::array<std::pair<Displacement, Arm>, 4> Steps { {
		{ { 0, -1 }, ArmNorth },
		{ { 1, 0 }, ArmEast },
		{ { 0, 1 }, ArmSouth },
		{ { -1, 0 }, ArmWest },
	} };

	// Where a line meets the room's own wall, the wall block takes the tile Diablo's Patterns give it with the line
	// drawn in as wall.
	for (const CathedralLine &line : plan.lines)
		predungeon[line.block.x][line.block.y] = '#';

	// Two archways ending at the same corner both shade the block inside it; the north-south one becomes a door.
	std::set<std::pair<int, int>> shadedAcross;
	for (const CathedralLine &line : plan.lines) {
		const Point p = line.block;
		if (line.kind == 'a' && lineAt(p + Displacement { 1, 0 }) == 'a' && lineAt(p + Displacement { -1, 0 }) != 'a') {
			shadedAcross.insert({ p.x, p.y - 1 });
			shadedAcross.insert({ p.x + 1, p.y - 1 });
		}
	}
	const auto archTurnsDoor = [&](Point top) {
		for (int dy = -1; dy <= 1; dy++) {
			if (shadedAcross.count({ top.x - 1, top.y + dy }) != 0)
				return true;
		}
		return false;
	};

	std::vector<std::pair<Point, uint8_t>> tiles;
	for (const CathedralLine &line : plan.lines) {
		const Point p = line.block;
		uint8_t arms = 0;
		for (const auto &[step, arm] : Steps) {
			const Point n = p + step;
			if (lineAt(n) != '\0') {
				arms |= arm;
				continue;
			}
			if (floorAt(n) || n.x < 0 || n.y < 0 || n.x >= DMAXX || n.y >= DMAXY || IsAnyOf(dungeon[n.x][n.y], 4, 5))
				continue;
			const uint8_t wall = dungeon[n.x][n.y];
			DoPatternCheck(n.x, n.y);
			const uint8_t joined = dungeon[n.x][n.y];
			dungeon[n.x][n.y] = wall;
			if (joined == 3)
				continue;
			tiles.emplace_back(n, joined);
			arms |= arm;
		}
		uint8_t tile = LineTile(arms);
		// The north-west block of a solid 2 x 2.
		if (arms == (ArmEast | ArmSouth) && lineAt(p + Displacement { 1, 1 }) != '\0')
			tile = 13;
		switch (line.kind) {
		case 'd':
			tile = (arms & ArmNorth) != 0 ? 4 : 5;
			break;
		case 'a': {
			// Archways are two blocks of a line, drawn as Diablo's VARCH and HARCH sets draw them.
			const bool vertical = lineAt(p + Displacement { 0, -1 }) == 'a' || lineAt(p + Displacement { 0, 1 }) == 'a';
			const Point top = lineAt(p + Displacement { 0, 1 }) == 'a' ? p : p + Displacement { 0, -1 };
			if (vertical && archTurnsDoor(top)) {
				tile = p == top ? 4 : 1;
			} else if (vertical) {
				if (lineAt(p + Displacement { 0, 1 }) == 'a') {
					const Point west = p + Displacement { -1, 0 };
					if (tile == 7 && dungeon[west.x][west.y] == 2 && lineAt(west) == '\0')
						tiles.emplace_back(west, 141);
					else if (floorAt(p + Displacement { -1, -1 }))
						floorStamps.emplace_back(p + Displacement { -1, -1 }, 48);
					if (floorAt(west))
						floorStamps.emplace_back(west, 51);
					tile = tile == 8 ? 42 : 39;
				} else {
					if (floorAt(p + Displacement { -1, 0 }))
						floorStamps.emplace_back(p + Displacement { -1, 0 }, 47);
					tile = 44;
				}
			} else if (lineAt(p + Displacement { 1, 0 }) == 'a') {
				const Point north = p + Displacement { 0, -1 };
				if ((tile == 8 || tile == 9) && dungeon[north.x][north.y] == 1)
					tiles.emplace_back(north, 140);
				else if (floorAt(north))
					floorStamps.emplace_back(north, 49);
				tile = tile == 8 ? 43 : 40;
			} else {
				if (floorAt(p + Displacement { 0, -1 }))
					floorStamps.emplace_back(p + Displacement { 0, -1 }, 46);
				tile = 45;
			}
			break;
		}
		default:
			break;
		}
		tiles.emplace_back(p, tile);
	}
	for (const auto &[p, tile] : tiles) {
		dungeon[p.x][p.y] = tile;
		Protected.set(p.x, p.y);
	}

	// Pillars and broken pillars stand free, with floor all round.
	const auto standsFree = [](Point p) {
		for (int dy = -1; dy <= 1; dy++) {
			for (int dx = -1; dx <= 1; dx++) {
				if ((dx != 0 || dy != 0) && dungeon[p.x + dx][p.y + dy] != 3)
					return false;
			}
		}
		return true;
	};
	std::vector<std::pair<Point, uint8_t>> posts;
	for (const Point p : plan.pillars)
		posts.emplace_back(p, 6);
	// A broken pillar is a pillar with fallen stone beside it (MakeCatacombsRubble). Diablo's own broken pillar, 83,
	// only ever ends a wall.
	for (const CathedralProp &prop : plan.props) {
		if (prop.kind == 'C')
			posts.emplace_back(prop.block, 6);
	}
	for (const auto &[p, tile] : posts) {
		if (!standsFree(p))
			continue;
		dungeon[p.x][p.y] = tile;
		Protected.set(p.x, p.y);
	}
}

/** POE2: a pile of fallen stone (Diablo's BIG8, which can be walked over) beside each broken pillar. */
void MakeCatacombsRubble(const CathedralPlan &plan)
{
	const auto nearStairs = [&plan](Point p) {
		for (const std::optional<Point> &stairs : { plan.stairsUp, plan.stairsDown, plan.townStairs }) {
			if (stairs && p.x >= stairs->x - 1 && p.x <= stairs->x + 2 && p.y >= stairs->y - 1 && p.y <= stairs->y + 2)
				return true;
		}
		return false;
	};
	const auto clear = [&nearStairs](Point p) {
		if (dungeon[p.x][p.y] != 3 || nearStairs(p))
			return false;
		for (const Displacement step : { Displacement { 0, -1 }, Displacement { 1, 0 }, Displacement { 0, 1 }, Displacement { -1, 0 } }) {
			const uint8_t tile = dungeon[p.x + step.deltaX][p.y + step.deltaY];
			if (IsAnyOf(tile, 4, 5) || (tile >= 39 && tile <= 51))
				return false;
		}
		return true;
	};
	for (const CathedralProp &prop : plan.props) {
		const Point p = prop.block;
		if (prop.kind != 'C' || dungeon[p.x][p.y] != 6)
			continue;
		for (const Displacement corner : { Displacement { 1, 0 }, Displacement { -2, 0 }, Displacement { 0, 1 }, Displacement { 0, -2 } }) {
			const Point at = p + corner;
			if (at.x < 1 || at.y < 1 || at.x + 2 >= DMAXX || at.y + 2 >= DMAXY)
				continue;
			if (!clear(at) || !clear(at + Displacement { 1, 0 }) || !clear(at + Displacement { 0, 1 }) || !clear(at + Displacement { 1, 1 }))
				continue;
			dungeon[at.x][at.y] = 128;
			dungeon[at.x + 1][at.y] = 130;
			dungeon[at.x][at.y + 1] = 129;
			dungeon[at.x + 1][at.y + 1] = 131;
			break;
		}
	}
}

/** POE2: the stairs go where the plan put them, each in the middle of 4 x 4 blocks of floor. */
bool PlaceCatacombsFunnelStairs(const CathedralPlan &plan, lvl_entry entry)
{
	const auto put = [](const Miniset &miniset, std::optional<Point> at) -> std::optional<Point> {
		if (!at || at->x < 1 || at->y < 1)
			return std::nullopt;
		const WorldTilePosition position { static_cast<WorldTileCoord>(at->x - 1), static_cast<WorldTileCoord>(at->y - 1) };
		if (!miniset.matches(position))
			return std::nullopt;
		miniset.place(position, true);
		return Point(position);
	};
	const std::optional<Point> up = put(USTAIRS, plan.stairsUp);
	if (!up)
		return false;
	if (entry == ENTRY_MAIN)
		ViewPosition = up->megaToWorld() + Displacement { 5, 4 };
	const std::optional<Point> down = put(DSTAIRS, plan.stairsDown);
	if (!down)
		return false;
	if (entry == ENTRY_PREV)
		ViewPosition = down->megaToWorld() + Displacement { 4, 6 };
	if (currlevel == 5) {
		const std::optional<Point> town = put(WARPSTAIRS, plan.townStairs);
		if (!town)
			return false;
		if (entry == ENTRY_TWARPDN)
			ViewPosition = town->megaToWorld() + Displacement { 5, 4 };
	}
	return true;
}

/** POE2: Diablo's theme rooms go in the plain side rooms, each a transparency area of its own. */
void PlaceCatacombsThemeRooms()
{
	themeCount = 0;
	for (const Rectangle room : CatacombsThemeRooms()) {
		if (themeCount >= MAXTHEMES)
			break;
		// The floor block nearest the room's middle: Zhar stands there when the room is his library.
		const Point middle = room.position + Displacement { room.size.width / 2, room.size.height / 2 };
		std::optional<Point> anchor;
		int best = DMAXX + DMAXY;
		for (int y = room.position.y + 2; y < room.position.y + room.size.height; y++) {
			for (int x = room.position.x + 2; x < room.position.x + room.size.width; x++) {
				const int d = std::abs(x - middle.x) + std::abs(y - middle.y);
				if (dungeon[x][y] == 3 && d < best) {
					best = d;
					anchor = Point { x, y };
				}
			}
		}
		if (!anchor)
			continue;
		const int8_t tv = dTransVal[16 + (2 * anchor->x)][16 + (2 * anchor->y)];
		if (tv <= 0)
			continue;
		// The whole area has to be in the room (its walls included), and big enough for the theme's things.
		const Point first = (room.position - Displacement { 1, 1 }).megaToWorld();
		const Point last = (room.position + Displacement { room.size.width + 1, room.size.height + 1 }).megaToWorld();
		int tiles = 0;
		bool outside = false;
		for (int y = 0; y < MAXDUNY && !outside; y++) {
			for (int x = 0; x < MAXDUNX; x++) {
				if (dTransVal[x][y] != tv)
					continue;
				if (x < first.x || y < first.y || x >= last.x || y >= last.y) {
					outside = true;
					break;
				}
				tiles++;
			}
		}
		if (outside || tiles < 36)
			continue;
		// Diablo's theme rooms start at their wall, two blocks before the middle of the room.
		const Point corner = *anchor - Displacement { 2, 2 };
		const Point end = room.position + Displacement { room.size.width, room.size.height };
		THEME_LOC &theme = themeLoc[themeCount];
		theme.room = { { static_cast<uint8_t>(corner.x), static_cast<uint8_t>(corner.y) }, { static_cast<uint8_t>(end.x - corner.x + 1), static_cast<uint8_t>(end.y - corner.y + 1) } };
		theme.ttval = tv;
		themeCount++;
	}
}

/**
 * POE2: every floor block of a new Catacombs floor can be walked to from the stairs up, through its doors and
 * archways, once Diablo's passes have had their say. They sometimes close a passage the plan left open.
 */
bool CatacombsFloorConnected(const CathedralPlan &plan)
{
	const auto open = [](Point p) {
		if (SetPieceRoom.contains(p.x, p.y))
			return true;
		const uint8_t tile = dungeon[p.x][p.y];
		return IsAnyOf(tile, 3, 4, 5, 39, 40, 42, 43, 44, 45, 72, 76, 77, 48, 50, 71, 78, 158, 159, 160);
	};
	if (!plan.stairsUp)
		return false;
	Bitset2d<DMAXX, DMAXY> seen;
	std::vector<Point> stack { *plan.stairsUp };
	seen.set(plan.stairsUp->x, plan.stairsUp->y);
	while (!stack.empty()) {
		const Point p = stack.back();
		stack.pop_back();
		for (const Displacement step : { Displacement { 0, -1 }, Displacement { 1, 0 }, Displacement { 0, 1 }, Displacement { -1, 0 } }) {
			const Point n = p + step;
			if (n.x < 0 || n.y < 0 || n.x >= DMAXX || n.y >= DMAXY || seen.test(n.x, n.y) || !open(n))
				continue;
			seen.set(n.x, n.y);
			stack.push_back(n);
		}
	}
	for (int y = 0; y < DMAXY; y++) {
		for (int x = 0; x < DMAXX; x++) {
			if (plan.mask.test(x, y) && dungeon[x][y] == 3 && !seen.test(x, y))
				return false;
		}
	}
	return (!plan.stairsDown || seen.test(plan.stairsDown->x, plan.stairsDown->y)) && (!plan.townStairs || currlevel != 5 || seen.test(plan.townStairs->x, plan.townStairs->y));
}

/** POE2: a new Catacombs floor (poe2/cathedral_floors.hpp). */
void GenerateCatacombsFunnelLevel(lvl_entry entry, uint64_t avoidRooms)
{
	if (LevelSeeds[currlevel])
		SetRndSeed(*LevelSeeds[currlevel]);

	CathedralRequest request;
	request.catacombs = true;
	request.forbidden = avoidRooms;
	request.townStairs = currlevel == 5;
	if (Quests[Q_BLIND].IsAvailable() || Quests[Q_BLOOD].IsAvailable()) {
		request.questPiece = true;
		request.questSlots = { 3, 3 };
	} else if (Quests[Q_SCHAMB].IsAvailable()) {
		request.questPiece = true;
	}

	CathedralPlan &plan = CurrentCathedralPlan();
	std::vector<std::pair<Point, uint8_t>> floorStamps;
	while (true) {
		LevelSeeds[currlevel] = GetLCGEngineState();
		DRLG_InitTrans();
		if (!PlanCathedralFloor(request, plan))
			continue;

		Protected.reset();
		SetPieceRoom = {};
		SetPiece = {};
		MakeCatacombsPredungeon(plan);
		for (int j = 0; j < DMAXY; j++) {
			for (int i = 0; i < DMAXX; i++)
				DoPatternCheck(i, j);
		}
		FixTilesPatterns();
		if (plan.questPiece) {
			// Arkaine's Valor and the Halls of the Blind sit one block in from their hall's north-west corner, the
			// Chamber of Bone two.
			const Point at = *plan.questPiece + (Quests[Q_SCHAMB].IsAvailable() ? Displacement { 1, 1 } : Displacement { 0, 0 });
			SetPieceRoom.position = { static_cast<WorldTileCoord>(at.x), static_cast<WorldTileCoord>(at.y) };
			InitSetPiece();
			SetPieceRoom = SetPiece;
		}
		floorStamps.clear();
		StampCatacombsLines(plan, floorStamps);
		FloodTransparencyValues(3);
		FixTransparency();
		if (!PlaceCatacombsFunnelStairs(plan, entry))
			continue;

		// Diablo's lockout pass would add a door to every wall with floor on both sides; the plan has its doorways.
		for (int j = 0; j < DMAXY; j++) {
			for (int i = 0; i < DMAXX; i++) {
				if (dungeon[i][j] == 4 && dungeon[i - 1][j] != 3)
					dungeon[i][j] = 1;
				if (dungeon[i][j] == 5 && dungeon[i][j - 1] != 3)
					dungeon[i][j] = 2;
			}
		}
		FixDoors();
		FixDirtTiles();
		if (!std::all_of(plan.doors.begin(), plan.doors.end(), [](const CathedralDoor &door) { return IsAnyOf(dungeon[door.block.x][door.block.y], 4, 5); }))
			continue;
		// A door right above a corner moves up a block, as Diablo has it, before the designed rooms are closed off.
		for (const Miniset *miniset : { &CTRDOOR1, &CTRDOOR2, &CTRDOOR3, &CTRDOOR4, &CTRDOOR5, &CTRDOOR6, &CTRDOOR7, &CTRDOOR8 })
			PlaceMiniSetRandom(*miniset, 100);
		// Nothing random lands in a designed room.
		for (int y = 0; y < DMAXY; y++) {
			for (int x = 0; x < DMAXX; x++) {
				if (plan.designed.test(x, y))
					Protected.set(x, y);
			}
		}
		if (!CatacombsFloorConnected(plan))
			continue;
		PlaceCatacombsThemeRooms();
		// Zhar waits in a theme room.
		if (Quests[Q_ZHAR].IsAvailable() && themeCount == 0)
			continue;
		break;
	}

	for (const auto &[p, tile] : floorStamps) {
		if (dungeon[p.x][p.y] == 3)
			dungeon[p.x][p.y] = tile;
	}
	for (const CathedralProp &prop : plan.props) {
		if (prop.kind == '~' && dungeon[prop.block.x][prop.block.y] == 3)
			dungeon[prop.block.x][prop.block.y] = FlipCoin() ? 108 : 110;
	}
	MakeCatacombsRubble(plan);
	DecorateLevel();

	SetCathedralFunnelFloor(true);
	Poe2NoteFloorRooms(currlevel, plan.usedRooms | CatacombsRoomsMark);
	TelemetryLog("floor", DescribeCathedralPlan(plan));
}

void Pass3()
{
	DRLG_LPass3(12 - 1);

	InitDungeonPieces();
}

} // namespace

void CreateL2Dungeon(uint32_t rseed, lvl_entry entry)
{
	SetRndSeed(rseed);

	SetCathedralFunnelFloor(false);
	// POE2: the new Catacombs floors, for a floor whose build was settled on them (single player for now).
	const Poe2FloorBuild build = gbIsPoe2 && !gbIsMultiplayer ? Poe2FloorBuildFor(currlevel) : Poe2FloorBuild {};
	if (build.cathedralFunnel)
		GenerateCatacombsFunnelLevel(entry, build.avoidRooms);
	else
		GenerateLevel(entry);

	Pass3();
}

void LoadPreL2Dungeon(const char *path)
{
	memset(dungeon, 12, sizeof(dungeon));

	auto dunData = LoadFileInMem<uint16_t>(path);
	PlaceDunTiles(dunData.get(), { 0, 0 }, 3);

	memcpy(pdungeon, dungeon, sizeof(pdungeon));
}

std::expected<void, std::string> LoadL2Dungeon(const char *path, Point spawn)
{
	RETURN_IF_ERROR(LoadDungeonBase(path, spawn, 3, 12));

	Pass3();

	AddL2Objs(0, 0, MAXDUNX, MAXDUNY);
	return {};
}

} // namespace devilution
