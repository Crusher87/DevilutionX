#include "poe2/dodge_roll.hpp"

#include <cstddef>

#include "controls/wasd_movement.hpp"
#include "cursor.h"
#include "engine/direction.hpp"
#include "engine/point.hpp"
#include "game_mode.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/tile_properties.hpp"
#include "monster.h"
#include "msg.h"
#include "multi.h"
#include "player.h"
#include "poe2/support_table.hpp"
#include "utils/is_of.hpp"

namespace devilution {

namespace {

/** Game ticks a rolled tile takes, plus a little grace at the end of the roll. */
constexpr int DodgeTicksPerTile = 4;
constexpr int DodgeGraceTicks = 2;
/** Game ticks between rolls, so the invulnerability can't be chained forever. */
constexpr int DodgeCooldownTicks = 20;
/** Game ticks at the start of a roll that hits and missiles pass through, like POE2's brief i-frames. */
constexpr int DodgeUntouchableTicks = 6;

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

/**
 * @brief Shoves a monster in the roll's way to one side, like POE2's roll pushing small monsters aside.
 * Unique monsters stand their ground, and a monster with no room to move blocks the roll, so a crowd can still trap the hero.
 */
bool ShoveAside(Point tile, Direction dir)
{
	Monster *monster = FindMonsterAtPosition(tile, true);
	if (monster == nullptr || monster->isUnique() || monster->hasNoLife() || monster->mode == MonsterMode::Petrified)
		return false;
	for (const Direction side : { Left(Left(dir)), Right(Right(dir)), Left(dir), Right(dir) }) {
		M_GetKnockback(*monster, tile - Displacement { side });
		if (monster->position.old != tile)
			return true;
	}
	return false;
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
	while (tiles < DodgeRollTiles && (CanStepTo(player, target, dir) || (ShoveAside(target + dir, dir) && CanStepTo(player, target, dir)))) {
		target += dir;
		tiles++;
	}
	if (tiles == 0)
		return false;

	player.poe2.dodgeTicks = static_cast<uint8_t>((tiles * DodgeTicksPerTile) + DodgeGraceTicks);
	player.poe2.dodgeCooldown = DodgeCooldownTicks;
	player.poe2.untouchableTicks = DodgeUntouchableTicks;
	NetSendCmdLoc(player.getId(), true, CMD_WALKXY, target);
	OnSupportedDodge(player);
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
	player.poe2.untouchableTicks = player.poe2.dodgeTicks;
	NetSendCmdLoc(player.getId(), true, CMD_WALKXY, end);
	return end;
}

} // namespace devilution
