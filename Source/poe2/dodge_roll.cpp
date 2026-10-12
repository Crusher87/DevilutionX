#include "poe2/dodge_roll.hpp"

#include <cstddef>

#include "controls/wasd_movement.hpp"
#include "cursor.h"
#include "engine/direction.hpp"
#include "engine/point.hpp"
#include "game_mode.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/tile_properties.hpp"
#include "msg.h"
#include "multi.h"
#include "player.h"
#include "utils/is_of.hpp"

namespace devilution {

namespace {

/** Game ticks a rolled tile takes, plus a little grace at the end of the roll. */
constexpr int DodgeTicksPerTile = 4;
constexpr int DodgeGraceTicks = 2;
/** Game ticks between rolls, so the invulnerability can't be chained forever. */
constexpr int DodgeCooldownTicks = 20;

Direction WasdDirection(AxisDirection dir)
{
	constexpr Direction FaceDir[3][3] = {
		// NONE             UP                    DOWN
		{ Direction::South, Direction::North, Direction::South },        // NONE
		{ Direction::West, Direction::NorthWest, Direction::SouthWest }, // LEFT
		{ Direction::East, Direction::NorthEast, Direction::SouthEast }, // RIGHT
	};
	return FaceDir[static_cast<size_t>(dir.x)][static_cast<size_t>(dir.y)];
}

bool CanStepTo(const Player &player, Point from, Direction dir)
{
	const Point to = from + dir;
	if (!PosOkPlayer(player, to))
		return false;
	// Don't cut diagonally between two walls.
	if (IsNoneOf(dir, Direction::North, Direction::East, Direction::South, Direction::West)) {
		if (IsTileSolid(from + Left(dir)) && IsTileSolid(from + Right(dir)))
			return false;
	}
	return true;
}

} // namespace

bool TryDodgeRoll(Player &player)
{
	// The roll isn't sent over the network yet, so it's single player only.
	if (!gbIsPoe2 || gbIsMultiplayer)
		return false;
	if (player.poe2.dodgeCooldown > 0 || player.hasNoLife() || player._pInvincible)
		return false;
	if (IsNoneOf(player._pmode, PM_STAND, PM_WALK_NORTHWARDS, PM_WALK_SOUTHWARDS, PM_WALK_SIDEWAYS))
		return false;

	const AxisDirection wasd = GetWasdMovementDirection();
	Direction dir;
	if (wasd.x != AxisDirectionX_NONE || wasd.y != AxisDirectionY_NONE) {
		dir = WasdDirection(wasd);
	} else if (cursPosition != player.position.future && InDungeonBounds(cursPosition)) {
		dir = GetDirection(player.position.future, cursPosition);
	} else {
		dir = player._pdir;
	}

	Point target = player.position.future;
	int tiles = 0;
	while (tiles < DodgeRollTiles && CanStepTo(player, target, dir)) {
		target += dir;
		tiles++;
	}
	if (tiles == 0)
		return false;

	player.poe2.dodgeTicks = static_cast<uint8_t>((tiles * DodgeTicksPerTile) + DodgeGraceTicks);
	player.poe2.dodgeCooldown = DodgeCooldownTicks;
	NetSendCmdLoc(player.getId(), true, CMD_WALKXY, target);
	return true;
}

std::optional<Point> DashTowards(Player &player, Point target, int maxTiles)
{
	if (IsNoneOf(player._pmode, PM_STAND, PM_WALK_NORTHWARDS, PM_WALK_SOUTHWARDS, PM_WALK_SIDEWAYS))
		return std::nullopt;

	Point end = player.position.future;
	int tiles = 0;
	while (tiles < maxTiles && end != target && CanStepTo(player, end, GetDirection(end, target))) {
		end += GetDirection(end, target);
		tiles++;
	}
	if (tiles == 0)
		return std::nullopt;

	player.poe2.dodgeTicks = static_cast<uint8_t>((tiles * DodgeTicksPerTile) + DodgeGraceTicks);
	NetSendCmdLoc(player.getId(), true, CMD_WALKXY, end);
	return end;
}

} // namespace devilution
