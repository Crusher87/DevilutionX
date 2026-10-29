#include "poe2/dodge_roll.hpp"

#include <algorithm>
#include <cstddef>

#include "controls/wasd_movement.hpp"
#include "cursor.h"
#include "engine/backbuffer_state.hpp"
#include "engine/direction.hpp"
#include "engine/point.hpp"
#include "game_mode.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/tile_properties.hpp"
#include "monster.h"
#include "missiles.h"
#include "msg.h"
#include "multi.h"
#include "player.h"
#include "poe2/minion_gems.hpp"
#include "poe2/spirit_buffs.hpp"
#include "poe2/support_table.hpp"
#include "poe2/telemetry.hpp"
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

/** @brief Blink: a short teleport the roll's way instead of the roll. It isn't a dodge roll, so nothing set off by rolls goes. */
bool Blink(Player &player, Direction dir)
{
	const int gemLevel = std::max(player.poe2.skillLevel(PersistentSkill::Blink), 1);
	const Point from = player.position.future;
	Point to = from;
	for (int i = 1; i <= BlinkTiles(gemLevel); i++) {
		const Point step = from + Displacement { dir } * i;
		if (!InDungeonBounds(step) || !LineClearMissile(from, step))
			break;
		if (PosOkPlayer(player, step))
			to = step;
	}
	if (to == from)
		return false;
	BreakOffActionForDodge(player);
	ClrPlrPath(player);
	StartStand(player, dir);
	AddVisualEffect(player.position.tile, MissileGraphicID::BlueFlareExplosion2);
	AddVisualEffect(to, MissileGraphicID::BlueFlareExplosion2, 1);
	AddMissile(player.position.tile, to, dir, MissileID::Teleport, TARGET_MONSTERS, player, 0, 0);
	player.poe2.dodgeCooldown = static_cast<uint8_t>(std::clamp(BlinkCooldownTicks(gemLevel), 1, 255));
	TelemetryLog("spirit", "Blink");
	return true;
}

/** @brief Gives back what the action under way paid as it started (see ActionPaymentScope). */
void GiveBackActionPrice(Player &player)
{
	Poe2PlayerData &data = player.poe2;
	if (data.actionPaidMana > 0) {
		const Fixed26_6 mana = Fixed26_6::fromRaw(data.actionPaidMana);
		player._pMana = std::min(player._pMana + mana, player._pMaxMana);
		player._pManaBase = std::min(player._pManaBase + mana, player._pMaxManaBase);
		// Nor does it count as spent for the uniques that grow with what is spent.
		data.uniqueState.resourcesSpent = std::max<int32_t>(data.uniqueState.resourcesSpent - data.actionPaidMana, 0);
		if (&player == MyPlayer)
			RedrawComponent(PanelDrawComponent::Mana);
	}
	if (data.actionPaidLife > 0) {
		const Fixed26_6 life = Fixed26_6::fromRaw(data.actionPaidLife);
		player._pHitPoints = std::min(player._pHitPoints + life, player._pMaxHP);
		player._pHPBase = std::min(player._pHPBase + life, player._pMaxHPBase);
		if (&player == MyPlayer)
			RedrawComponent(PanelDrawComponent::Health);
	}
	// Deep Well's free skill waits for the next one.
	if (data.actionTookFreeSkill)
		data.uniqueState.freeSkill = true;
	data.forgetActionPaid();
}

} // namespace

ActionPaymentScope::ActionPaymentScope(Player &player)
    : player_(player)
    , mana_(player._pMana.raw())
    , life_(player._pHitPoints.raw())
    , freeSkill_(player.poe2.uniqueState.freeSkill)
{
}

ActionPaymentScope::~ActionPaymentScope()
{
	Poe2PlayerData &data = player_.poe2;
	data.actionPaidMana += std::max<int32_t>(mana_ - player_._pMana.raw(), 0);
	data.actionPaidLife += std::max<int32_t>(life_ - player_._pHitPoints.raw(), 0);
	data.actionTookFreeSkill = data.actionTookFreeSkill || (freeSkill_ && !data.uniqueState.freeSkill);
}

bool ActionHasGoneOff(const Player &player)
{
	// The action frame's work runs on the game tick that shows it, so while that frame is current it hasn't happened yet.
	switch (player._pmode) {
	case PM_ATTACK:
	case PM_RATTACK:
		return player.AnimInfo.currentFrame > player._pAFNum - 1;
	case PM_SPELL:
		return player.AnimInfo.currentFrame > (player.poe2.castSkill != WeaponSkill::None ? player.poe2.castFrame : player._pSFNum);
	default:
		return true;
	}
}

void BreakOffActionForDodge(Player &player)
{
	if (IsNoneOf(player._pmode, PM_ATTACK, PM_RATTACK, PM_SPELL))
		return;
	Poe2PlayerData &data = player.poe2;
	if (!ActionHasGoneOff(player)) {
		GiveBackActionPrice(player);
		// The swing's skill never landed.
		data.activeSkill = WeaponSkill::None;
		data.flurryStrike = 0;
		TelemetryLog("dodge", "rolled out of an attack or cast before it went off");
	}
	// Missiles, volleys and zones it already set going carry on; a Whirlwind's spin effect fades on its own.
	data.forgetActionPaid();
	data.castSkill = WeaponSkill::None;
	data.castTargetMonster = -1;
	data.pendingSkill = WeaponSkill::None;
	data.pendingSkillTicks = 0;
	data.recoveryTicks = 0;
	player.destAction = ACTION_NONE;
	ClrPlrPath(player);
	StartStand(player, player._pdir);
	// As an action's end does (ClearStateVariables).
	player.position.temp = { 0, 0 };
	player.tempDirection = Direction::South;
	player.queuedSpell.spellLevel = 0;
}

bool TryDodgeRoll(Player &player)
{
	// The roll isn't sent over the network yet, so it's single player only.
	if (!gbIsPoe2 || gbIsMultiplayer)
		return false;
	if (player.poe2.dodgeCooldown > 0 || player.hasNoLife() || player._pInvincible)
		return false;
	// POE2 Mashup: the roll also breaks off an attack or cast (Curtis 2026-10-29).
	if (IsNoneOf(player._pmode, PM_STAND, PM_WALK_NORTHWARDS, PM_WALK_SOUTHWARDS, PM_WALK_SIDEWAYS, PM_ATTACK, PM_RATTACK, PM_SPELL))
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
	if (BlinkReplacesDodge(player))
		return Blink(player, dir);

	const Point start = player.position.future;
	Point target = start;
	int tiles = 0;
	while (tiles < DodgeRollTiles && (CanStepTo(player, target, dir) || (ShoveAside(target + dir, dir) && CanStepTo(player, target, dir)))) {
		target += dir;
		tiles++;
	}
	if (tiles == 0)
		return false;

	BreakOffActionForDodge(player);
	// It cuts short the stand a slowed skill holds the hero in, too, so the roll goes at once.
	player.poe2.recoveryTicks = 0;
	player.poe2.dodgeTicks = static_cast<uint8_t>((tiles * DodgeTicksPerTile) + DodgeGraceTicks);
	player.poe2.dodgeCooldown = DodgeCooldownTicks;
	player.poe2.untouchableTicks = DodgeUntouchableTicks;
	NetSendCmdLoc(player.getId(), true, CMD_WALKXY, target);
	OnSupportedDodge(player, tiles);
	OnSpiritDodge(player, start, target);
	OnIllusionDodge(player, start);
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
