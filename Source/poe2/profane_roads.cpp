#include "poe2/profane_roads.hpp"

#include <algorithm>
#include <functional>
#include <optional>

#include "effects.h"
#include "engine/animationinfo.h"
#include "engine/direction.hpp"
#include "game_mode.hpp"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "lighting.h"
#include "missiles.h"
#include "monster.h"
#include "nthread.h"
#include "player.h"
#include "poe2/monster_mods.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/skill_lab.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_rules.hpp"
#include "poe2/vfx.hpp"
#include "poe2/weapon_kinds.hpp"
#include "utils/is_of.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

/** @brief The cry is the Butcher's own line, slowed to this share of its speed so it sounds deeper than his. */
constexpr int ButcherCryRatePercent = 78;
/** @brief The pact's hook along an arrow's line takes enemies this close to the line, in tiles. */
constexpr int ArrowLineHalfWidth = 1;

/** @brief The toll, The Butcher's Hook (the stat keeps its old name, DeadDoNotRest, in the tree data). */
bool HasButchersHook(const Player &player)
{
	return PassiveTotal(player, PassiveStat::DeadDoNotRest) > 0;
}

/** @brief The pact, The Larder (the stat keeps its old name, KingsDeadPact, in the tree data). */
bool HasLarder(const Player &player)
{
	return PassiveTotal(player, PassiveStat::KingsDeadPact) > 0;
}

int SquaredDistance(Point a, Point b)
{
	const Displacement d = a - b;
	return d.deltaX * d.deltaX + d.deltaY * d.deltaY;
}

/** @brief An enemy the hook can catch: alive, on the level and in sight, not the hero's and not in a talk. */
bool IsHookableEnemy(const Monster &monster)
{
	return !monster.isInvalid && !monster.hasNoLife() && monster.mode != MonsterMode::Death && !monster.isPlayerMinion()
	    && (monster.flags & MFLAG_HIDDEN) == 0 && !CanTalkToMonst(monster) && !IsLabDummy(monster)
	    && monster.position.tile != GolemHoldingCell;
}

bool IsBoss(const Monster &monster)
{
	return monster.isUnique() || monster.type().type == MT_DIABLO;
}

/**
 * @brief The enemy the hook flies to: the nearest within 6 tiles of the corpse that the hero has a clear line to. With
 * an arrow's line, the nearest behind the corpse along it comes first.
 */
Monster *FindHookTarget(const Player &player, const Monster &corpse, Point from, std::optional<Point> arrowFrom)
{
	Monster *nearest = nullptr;
	int nearestDistance = HookTiles * HookTiles + 1;
	Monster *behind = nullptr;
	int behindDistance = nearestDistance;
	Displacement line {};
	int64_t lineLength = 0;
	if (arrowFrom) {
		line = from - *arrowFrom;
		lineLength = static_cast<int64_t>(line.deltaX) * line.deltaX + static_cast<int64_t>(line.deltaY) * line.deltaY;
	}
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		// One already on the hook is left to it, so the next Cull catches another.
		if (&monster == &corpse || !IsHookableEnemy(monster) || monster.poe2.hookTicks > 0)
			continue;
		const Point tile = monster.position.tile;
		const int distance = SquaredDistance(tile, from);
		if (distance > HookTiles * HookTiles || !LineClearMissile(player.position.tile, tile))
			continue;
		if (distance < nearestDistance) {
			nearest = &monster;
			nearestDistance = distance;
		}
		if (lineLength == 0 || distance >= behindDistance)
			continue;
		// Behind the corpse on the arrow's line: ahead along it, and no more than a tile to either side.
		const Displacement offset = tile - from;
		const int64_t along = static_cast<int64_t>(offset.deltaX) * line.deltaX + static_cast<int64_t>(offset.deltaY) * line.deltaY;
		const int64_t across = static_cast<int64_t>(offset.deltaX) * line.deltaY - static_cast<int64_t>(offset.deltaY) * line.deltaX;
		if (along > 0 && across * across <= static_cast<int64_t>(ArrowLineHalfWidth * ArrowLineHalfWidth) * lineLength) {
			behind = &monster;
			behindDistance = distance;
		}
	}
	return behind != nullptr ? behind : nearest;
}

/** @brief Moves the monster to the tile next to it in that direction, as a walk would allow; false where it can't. */
bool StepMonster(Monster &monster, Direction dir, Point heroTile)
{
	const Point from = monster.position.tile;
	const Point to = from + dir;
	if (!InDungeonBounds(to) || to == heroTile || !IsTileFreeForMonster(to))
		return false;
	// Never through the corner of a wall, as IsRelativeMoveOK sees it for a walk.
	if ((dir == Direction::East && IsTileSolid(from + Direction::SouthEast))
	    || (dir == Direction::West && IsTileSolid(from + Direction::SouthWest))
	    || (dir == Direction::North && (IsTileSolid(from + Direction::NorthEast) || IsTileSolid(from + Direction::NorthWest)))
	    || (dir == Direction::South && (IsTileSolid(from + Direction::SouthWest) || IsTileSolid(from + Direction::SouthEast))))
		return false;
	M_ClearSquares(monster);
	monster.position.tile = to;
	monster.position.old = to;
	monster.position.future = to;
	monster.occupyTile(to, false);
	if (monster.lightId != NO_LIGHT)
		ChangeLightXY(monster.lightId, to);
	return true;
}

/** @brief The free tile beside the hero nearest the monster, or the hero's own tile when none is free. */
Point DragDestination(const Monster &monster, Point heroTile)
{
	std::optional<Point> best;
	int bestDistance = 0;
	for (int dy = -1; dy <= 1; dy++) {
		for (int dx = -1; dx <= 1; dx++) {
			const Point tile = heroTile + Displacement { dx, dy };
			if ((dx == 0 && dy == 0) || !InDungeonBounds(tile))
				continue;
			if (tile != monster.position.tile && !IsTileFreeForMonster(tile))
				continue;
			const int distance = SquaredDistance(tile, monster.position.tile);
			if (!best || distance < bestDistance) {
				best = tile;
				bestDistance = distance;
			}
		}
	}
	return best.value_or(heroTile);
}

/** @brief The pact: answering the call or caught on the hook enrages the monster; a new one resets it. */
void Enrage(const Player &player, Monster &monster)
{
	if (HasLarder(player))
		monster.poe2.enrageTicks = EnrageTicks;
}

/**
 * @brief Drags the monster up to steps tiles on towards its destination; the drag ends early at a wall or another
 * monster, or once it arrives.
 */
void DragSteps(Monster &monster, const Player &player, int steps)
{
	Poe2MonsterState &state = monster.poe2;
	const Point destination { state.hookToX, state.hookToY };
	state.hookFromX = static_cast<uint8_t>(monster.position.tile.x);
	state.hookFromY = static_cast<uint8_t>(monster.position.tile.y);
	state.hookStepped = false;
	for (int i = 0; i < steps; i++) {
		if (monster.position.tile == destination || monster.position.tile.WalkingDistance(player.position.tile) <= 1) {
			state.hookTicks = 0;
			return;
		}
		if (!StepMonster(monster, GetDirection(monster.position.tile, destination), player.position.tile)) {
			state.hookTicks = 0;
			return;
		}
		state.hookStepped = true;
	}
}

/** @brief The hook catches the enemy: a boss flinches, a rare comes a tile and slows, anything else is dragged in. */
void Hook(const Player &player, Point from, Monster &target)
{
	vfx::HookChain(from, target.position.tile, /*thrown=*/true);
	AddVisualEffect(target.position.tile, MissileGraphicID::BloodHit);
	PlaySfxLoc(SfxID::OperateLever, target.position.tile, false);
	TelemetryLog("profane", StrCat("the Butcher's hook caught ", target.name()));
	Poe2MonsterState &state = target.poe2;
	if (target.mode == MonsterMode::Petrified) {
		Enrage(player, target);
		return;
	}
	if (IsBoss(target)) {
		M_StartFlinch(target);
		state.flinchTicks = HookFlinchTicks;
	} else if (GetMonsterAffixes(target).rarity == MonsterRarity::Rare) {
		if (target.position.tile.WalkingDistance(player.position.tile) > 1) {
			const bool walking = target.isWalking();
			// A walk it was making ends where it stood; the pull takes it a tile in from there.
			if (walking) {
				M_ClearSquares(target);
				target.position.tile = target.position.old;
				target.position.future = target.position.old;
				target.occupyTile(target.position.tile, false);
			}
			StepMonster(target, GetDirection(target.position.tile, player.position.tile), player.position.tile);
			if (walking)
				M_StartStand(target, target.direction);
		}
		state.hookSlowTicks = HookRareSlowTicks;
	} else if (target.position.tile.WalkingDistance(player.position.tile) > 1) {
		// Caught mid-stride or mid-swing: it reels on the hook, and can't act until the drag ends.
		M_StartFlinch(target);
		const Point destination = DragDestination(target, player.position.tile);
		state.hookToX = static_cast<uint8_t>(destination.x);
		state.hookToY = static_cast<uint8_t>(destination.y);
		state.hookTicks = HookDragTicks;
	}
	Enrage(player, target);
}

/**
 * @brief The Butcher's cry, let out by the hero: every enemy in reach of her wakes and hunts her, through walls, as if
 * she had hit it. The sound itself rests between Culls.
 */
void Cry(const Player &player, const Monster &corpse)
{
	UniqueRuleState &state = player.poe2.uniqueState;
	if (state.butcherCryTicks == 0) {
		state.butcherCryTicks = ButcherCryCooldownTicks;
		PlayPitchedSfxLoc(SfxID::ButcherGreeting, player.position.tile, ButcherCryRatePercent);
	}
	const int reach = HasLarder(player) ? ButcherCryPactTiles : ButcherCryTiles;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (&monster == &corpse || monster.hasNoLife() || monster.mode == MonsterMode::Death || monster.isPlayerMinion()
		    || (monster.flags & MFLAG_TARGETS_MONSTER) != 0 || CanTalkToMonst(monster) || IsLabDummy(monster))
			continue;
		if (SquaredDistance(monster.position.tile, player.position.tile) > reach * reach)
			continue;
		monster.activeForTicks = UINT8_MAX;
		monster.enemy = static_cast<uint8_t>(player.getId());
		monster.enemyPosition = player.position.future;
		monster.position.last = player.position.future;
		monster.flags &= ~MFLAG_NO_ENEMY;
		Enrage(player, monster);
	}
}

} // namespace

void OnProfaneRoadKill(const Player &player, const Monster &monster)
{
	if (!gbIsPoe2 || gbIsMultiplayer || &player != MyPlayer || monster.isPlayerMinion() || !monster.poe2.culled || leveltype == DTYPE_TOWN)
		return;
	if (!HasButchersHook(player))
		return;
	// MonsterDeath puts the body down on the tile the monster came from.
	const Point from = monster.position.old;
	const UniqueRuleState &state = player.poe2.uniqueState;
	std::optional<Point> arrowFrom;
	if (HasLarder(player) && state.lastHitRanged && !state.lastHitBySpell && Point(state.arrowFromX, state.arrowFromY) != from)
		arrowFrom = Point(state.arrowFromX, state.arrowFromY);
	if (Monster *target = FindHookTarget(player, monster, from, arrowFrom); target != nullptr)
		Hook(player, from, *target);
	Cry(player, monster);
}

void NoteArrowLine(const Player &player, Point from)
{
	UniqueRuleState &state = player.poe2.uniqueState;
	state.arrowFromX = static_cast<int16_t>(from.x);
	state.arrowFromY = static_cast<int16_t>(from.y);
}

void ProcessProfaneRoads()
{
	if (!gbIsPoe2 || gbIsMultiplayer || MyPlayer == nullptr)
		return;
	const Player &player = *MyPlayer;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		Poe2MonsterState &state = monster.poe2;
		if (state.hookSlowTicks > 0)
			state.hookSlowTicks--;
		if (state.flinchTicks > 0)
			state.flinchTicks--;
		if (state.enrageTicks > 0 && --state.enrageTicks == 0)
			state.enrageCarry = 0;
		if (state.hookTicks == 0) {
			state.hookStepped = false;
			continue;
		}
		if (monster.hasNoLife() || monster.mode == MonsterMode::Death || !player.isOnActiveLevel()) {
			state.hookTicks = 0;
			state.hookStepped = false;
			continue;
		}
		// Evenly over the drag's ticks: what is left of the way, shared out over the ticks left.
		const Point destination { state.hookToX, state.hookToY };
		const int left = monster.position.tile.WalkingDistance(destination);
		const int steps = (left + state.hookTicks - 1) / state.hookTicks;
		state.hookTicks--;
		DragSteps(monster, player, std::max(steps, 1));
		vfx::HookChain(monster.position.tile, player.position.tile, /*thrown=*/false);
	}
}

bool IsHooked(const Monster &monster)
{
	return gbIsPoe2 && monster.poe2.hookTicks > 0;
}

int HookSlowPercent(const Monster &monster)
{
	return gbIsPoe2 && monster.poe2.hookSlowTicks > 0 ? HookRareSlowPercent : 0;
}

bool IsFlinching(const Monster &monster)
{
	return gbIsPoe2 && monster.poe2.flinchTicks > 0;
}

bool IsEnraged(const Monster &monster)
{
	return gbIsPoe2 && monster.poe2.enrageTicks > 0 && !monster.hasNoLife();
}

bool EnragedExtraTick(Monster &monster)
{
	if (!IsEnraged(monster) || monster.isPlayerMinion())
		return false;
	monster.poe2.enrageCarry = static_cast<uint8_t>(monster.poe2.enrageCarry + EnrageHastePercent);
	if (monster.poe2.enrageCarry < 100)
		return false;
	monster.poe2.enrageCarry = static_cast<uint8_t>(monster.poe2.enrageCarry - 100);
	return true;
}

Displacement HookDrawOffset(const Monster &monster)
{
	if (!gbIsPoe2 || !monster.poe2.hookStepped)
		return {};
	// The step just taken, drawn from where it began, so the drag slides between game ticks instead of jumping.
	const Displacement back = Point(monster.poe2.hookFromX, monster.poe2.hookFromY) - Point(monster.position.tile);
	const int remaining = AnimationInfo::baseValueFraction - std::min<int>(ProgressToNextGameTick, AnimationInfo::baseValueFraction);
	return { (back.deltaX - back.deltaY) * 32 * remaining / AnimationInfo::baseValueFraction, (back.deltaX + back.deltaY) * 16 * remaining / AnimationInfo::baseValueFraction };
}

bool HasInfravision(const Player &player)
{
	return player._pInfraFlag || (gbIsPoe2 && PassiveTotal(player, PassiveStat::EyesInTheDark) > 0);
}

namespace {

bool WieldsStaff(const Player &player)
{
	for (const Item &item : { std::cref(player.InvBody[INVLOC_HAND_LEFT]), std::cref(player.InvBody[INVLOC_HAND_RIGHT]) }) {
		// Any staff: a caster staff, a quarterstaff or one from before the caster weapons rework.
		if (!item.isEmpty() && item._iStatFlag && IsAnyOf(WeaponKindOf(item), WeaponKind::CasterStaff, WeaponKind::Quarterstaff, WeaponKind::LegacyStaff))
			return true;
	}
	return false;
}

/** @brief The Maiden's Road pact, while a staff is held. */
bool OutOfMind(const Player &player)
{
	return gbIsPoe2 && !gbIsMultiplayer && PassiveTotal(player, PassiveStat::OutOfDarkness) > 0 && WieldsStaff(player);
}

/** @brief A monster that hunts the hero: not a minion, not one that fights other monsters. */
bool HuntsHero(const Monster &monster)
{
	return !monster.isPlayerMinion() && (monster.flags & MFLAG_TARGETS_MONSTER) == 0;
}

} // namespace

int ProfaneLightRadiusCap(const Player &player)
{
	int cap = 15;
	if (!gbIsPoe2)
		return cap;
	if (PassiveTotal(player, PassiveStat::EyesInTheDark) > 0)
		cap = MaidenLightRadiusCap;
	if (OutOfMind(player))
		cap = std::min(cap, StaffLightRadiusCap);
	return cap;
}

int UnawareFirstStrikeBonus(const Player &player, const Monster &monster)
{
	if (!gbIsPoe2 || PassiveTotal(player, PassiveStat::EyesInTheDark) <= 0 || monster.activeForTicks != 0)
		return 0;
	return UnawareFirstStrikePercent;
}

bool MaidenSeesYou(const Monster &monster)
{
	if (!gbIsPoe2 || gbIsMultiplayer || MyPlayer == nullptr || !HuntsHero(monster))
		return false;
	const Player &player = *MyPlayer;
	if (PassiveTotal(player, PassiveStat::EyesInTheDark) <= 0 || player.plrlevel != currlevel)
		return false;
	return monster.position.tile.ExactDistance(player.position.tile) <= MaidenWakeTiles
	    && LineClearMissile(monster.position.tile, player.position.tile);
}

bool CanShootAtPlayer(const Monster &monster)
{
	if (MyPlayer == nullptr || !HuntsHero(monster) || monster.poe2.struckHero || monster.type().type == MT_DIABLO)
		return true;
	const Player &player = *MyPlayer;
	if (!OutOfMind(player))
		return true;
	return monster.position.tile.ExactDistance(player.position.tile) <= OutOfMindTiles
	    && LineClearMissile(monster.position.tile, player.position.tile);
}

bool TakeFirstBlow(Monster &monster, const Player &player)
{
	if (&player != MyPlayer || !gbIsPoe2)
		return false;
	const bool first = !monster.poe2.struckHero && OutOfMind(player);
	monster.poe2.struckHero = true;
	return first;
}

} // namespace devilution
