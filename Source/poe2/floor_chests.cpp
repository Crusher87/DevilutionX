#include "poe2/floor_chests.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "control/control.hpp"
#include "diablo.h"
#include "effects.h"
#include "engine/path.h"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "missiles.h"
#include "monster.h"
#include "objects.h"
#include "player.h"
#include "poe2/camera_shake.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/vfx.hpp"
#include "utils/language.h"

namespace devilution {

namespace {

/** _oVar5 holds the kind as KindMark plus the kind, so a value left in a reused object slot never reads as one. */
constexpr int KindMark = 100;
constexpr int GuardRadius = 6;
/** The red runes show where the guards will stand this long before the first steps out, and then one comes every GuardGapMs. */
constexpr int WarningMs = 1500;
constexpr int GuardGapMs = 500;

/*
 * A strongbox keeps its guards in its object, so a save in the middle of them coming keeps the rest coming:
 * _oVar6: how many guards it has (bits 0-7), how many are still to come (bits 8-15), and whether its loot is still
 * waiting for them all to die (bit 16).
 * _oVar4: the guards' two kinds once it's open (bits 0-7 and 8-15); a strongbox is never trapped.
 * _oVar3: game ticks until the next guard steps out.
 */
int GuardsTotal(const Object &chest)
{
	return chest._oVar6 & 0xFF;
}

int GuardsToCome(const Object &chest)
{
	return (chest._oVar6 >> 8) & 0xFF;
}

constexpr int LootWaiting = 1 << 16;

void SetGuardsToCome(Object &chest, int count)
{
	chest._oVar6 = (chest._oVar6 & ~0xFF00) | (count << 8);
}

bool IsLootWaiting(const Object &chest)
{
	return (chest._oVar6 & LootWaiting) != 0;
}

/** Whether any strongbox guard on the floor is still alive. */
bool GuardsAlive()
{
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &monster = Monsters[ActiveMonsters[i]];
		if ((monster.flags & MFLAG_STRONGBOX_GUARD) != 0 && monster.mode != MonsterMode::Death && monster.hitPoints > 0)
			return true;
	}
	return false;
}

/** The sure rare and the box's own rolls (at least one), dropped where it stands. */
void DropStrongboxLoot(Object &chest, bool sendmsg)
{
	DropPoe2ChestItem(chest.position, ITEM_QUALITY_RARE, sendmsg, "strongbox");
	for (int j = 0; j < std::max(chest._oVar1, 1); j++) {
		if (chest._oVar2 != 0)
			CreateRndItem(chest.position, false, sendmsg, false);
		else
			CreateRndUseful(chest.position, sendmsg);
	}
}

int Ticks(int ms)
{
	return std::max(1, ms / std::max<int>(gnTickDelay, 1));
}

bool CanPlaceGuard(Point position)
{
	return InDungeonBounds(position) && !IsTileSolid(position) && !IsTileOccupied(position) && dMonster[position.x][position.y] == 0
	    && dPlayer[position.x][position.y] == 0 && dObject[position.x][position.y] == 0;
}

/** The floor's own kinds: every kind a monster on the floor is, but not bosses, minions or golems. */
std::vector<size_t> FloorMonsterTypes()
{
	std::vector<size_t> types;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &monster = Monsters[ActiveMonsters[i]];
		if (monster.isPlayerMinion() || monster.isUnique() || monster.position.tile == GolemHoldingCell)
			continue;
		if (std::find(types.begin(), types.end(), monster.levelType) == types.end())
			types.push_back(monster.levelType);
	}
	return types;
}

/**
 * Where the guards will stand: the free tiles nearest the strongbox, each one taken in turn. The guards step out on
 * the same tiles unless something has moved onto one by then.
 */
std::vector<Point> GuardTiles(const Object &chest, int count)
{
	std::vector<Point> tiles;
	const auto free = [&tiles](Point position) { return CanPlaceGuard(position) && std::find(tiles.begin(), tiles.end(), position) == tiles.end(); };
	for (int i = 0; i < count; i++) {
		const std::optional<Point> at = FindClosestValidPosition(free, chest.position, 1, GuardRadius);
		if (!at)
			break;
		tiles.push_back(*at);
	}
	return tiles;
}

/**
 * Opening a strongbox: a low summoning sound, the ground shakes, and red runes draw themselves where its guards will
 * stand. Its guards are one kind with another kind as every third where the floor has one.
 */
void WarnOfGuards(Object &chest)
{
	const std::vector<size_t> types = FloorMonsterTypes();
	if (GuardsTotal(chest) <= 0 || types.empty())
		return;
	const size_t main = types[GenerateRnd(static_cast<int>(types.size()))];
	const size_t other = types[GenerateRnd(static_cast<int>(types.size()))];
	chest._oVar4 = static_cast<int>(main | (other << 8));
	SetGuardsToCome(chest, GuardsTotal(chest));
	chest._oVar6 |= LootWaiting;
	chest._oVar3 = Ticks(WarningMs);
	PlaySfxLoc(SfxID::SpellGolem, chest.position, false);
	ShakeCamera(ShakeStrength::Light, Ticks(WarningMs / 2));
	const std::vector<Point> tiles = GuardTiles(chest, GuardsTotal(chest));
	for (size_t i = 0; i < tiles.size(); i++)
		vfx::Sigil(tiles[i], 1, WarningMs + (static_cast<int>(i) * GuardGapMs), vfx::Element::Blood);
}

/**
 * The next guard steps out. None of them is rare (rares lead the big fights only); the first is made magic to lead
 * them. Spawned monsters only come for the level's owner. Others in a multiplayer game see the leader as a normal
 * monster.
 */
void BringOutGuard(Object &chest)
{
	const int left = GuardsToCome(chest);
	SetGuardsToCome(chest, left - 1);
	chest._oVar3 = Ticks(GuardGapMs);
	const int index = GuardsTotal(chest) - left;
	const std::optional<Point> at = FindClosestValidPosition(CanPlaceGuard, chest.position, 1, GuardRadius);
	if (!at || MyPlayer == nullptr)
		return;
	const size_t type = static_cast<size_t>(index % 3 == 2 ? (chest._oVar4 >> 8) & 0xFF : chest._oVar4 & 0xFF);
	if (type >= LevelMonsterTypeCount)
		return;
	const size_t before = ActiveMonsterCount;
	SpawnMonster(*at, GetDirection(*at, MyPlayer->position.tile), type);
	if (ActiveMonsterCount == before)
		return;
	vfx::Impact(*at, vfx::Element::Blood, 1.2F);
	Monster &monster = Monsters[ActiveMonsters[ActiveMonsterCount - 1]];
	monster.flags |= MFLAG_STRONGBOX_GUARD;
	if (GetMonsterAffixes(monster).rarity == MonsterRarity::Rare)
		DemoteMonsterRarity(monster);
	if (index == 0)
		PromoteMonsterRarity(monster, MonsterRarity::Magic);
}

} // namespace

FloorChestKind GetFloorChestKind(const Object &chest)
{
	if (!chest.IsChest())
		return FloorChestKind::Normal;
	const int kind = chest._oVar5 - KindMark;
	if (kind <= static_cast<int>(FloorChestKind::Normal) || kind > static_cast<int>(FloorChestKind::Strongbox))
		return FloorChestKind::Normal;
	return static_cast<FloorChestKind>(kind);
}

void SetFloorChestKind(Object &chest, FloorChestKind kind, int guards)
{
	if (!chest.IsChest())
		return;
	chest._oVar5 = kind == FloorChestKind::Normal ? 0 : KindMark + static_cast<int>(kind);
	chest._oVar6 = kind == FloorChestKind::Strongbox ? std::clamp(guards, 0, 0xFF) : 0;
	chest._oVar3 = 0;
}

bool StrongboxStandsOut(const Object &object)
{
	return gbIsPoe2 && GetFloorChestKind(object) == FloorChestKind::Strongbox && (object.canInteractWith() || IsLootWaiting(object));
}

bool OpenFloorChest(const Player &player, Object &chest, bool sendmsg)
{
	const FloorChestKind kind = GetFloorChestKind(chest);
	if (!gbIsPoe2 || setlevel || kind == FloorChestKind::Normal)
		return false;
	if (kind == FloorChestKind::Strongbox) {
		// Its loot waits until every guard is dead (Curtis, 10-29); with no guards it drops now.
		if (sendmsg && &player == MyPlayer)
			WarnOfGuards(chest);
		if (!IsLootWaiting(chest))
			DropStrongboxLoot(chest, sendmsg);
		return true;
	}
	// The sure item, then the chest's own rolls, of which the sure item counts as one.
	const bool magic = kind == FloorChestKind::Magic;
	DropPoe2ChestItem(chest.position, magic ? ITEM_QUALITY_MAGIC : ITEM_QUALITY_RARE, sendmsg, magic ? "magic chest" : "rare chest");
	for (int j = 1; j < std::max(chest._oVar1, 1); j++) {
		if (chest._oVar2 != 0)
			CreateRndItem(chest.position, false, sendmsg, false);
		else
			CreateRndUseful(chest.position, sendmsg);
	}
	return true;
}

void UpdateFloorChest(Object &chest)
{
	if (!gbIsPoe2 || GetFloorChestKind(chest) != FloorChestKind::Strongbox || !IsLootWaiting(chest))
		return;
	if (GuardsToCome(chest) > 0) {
		if (--chest._oVar3 <= 0)
			BringOutGuard(chest);
		return;
	}
	if (GuardsAlive())
		return;
	// The last guard is dead: the strongbox gives up its loot.
	chest._oVar6 &= ~LootWaiting;
	SetRndSeed(chest._oRndSeed);
	PlaySfxLoc(SfxID::ChestOpen, chest.position, false);
	vfx::Impact(chest.position, vfx::Element::Holy, 1.5F);
	DropStrongboxLoot(chest, true);
}

std::string PlaceTestStrongbox()
{
	if (MyPlayer == nullptr || leveltype == DTYPE_TOWN)
		return "Go into the dungeon first.";
	const auto fits = [](Point tile) {
		for (int dy = -1; dy <= 1; dy++) {
			for (int dx = -1; dx <= 1; dx++) {
				const Point p = tile + Displacement { dx, dy };
				if (!CanPlaceGuard(p))
					return false;
			}
		}
		return true;
	};
	const std::optional<Point> at = FindClosestValidPosition(fits, MyPlayer->position.tile, 3, 8);
	if (!at)
		return "No room for a strongbox here.";
	Object *strongbox = AddObject(OBJ_CHEST3, *at);
	if (strongbox == nullptr)
		return "The floor has too many objects.";
	SetFloorChestKind(*strongbox, FloorChestKind::Strongbox, 5);
	return "A strongbox with 5 guards is next to you.";
}

void DescribeFloorChest(const Object &chest)
{
	if (!gbIsPoe2)
		return;
	switch (GetFloorChestKind(chest)) {
	case FloorChestKind::Magic:
		InfoColor = UiFlags::ColorBlue;
		break;
	case FloorChestKind::Rare:
		InfoColor = UiFlags::ColorYellow;
		break;
	case FloorChestKind::Strongbox:
		InfoString = _("Strongbox");
		InfoColor = UiFlags::ColorGold;
		break;
	default:
		break;
	}
}

} // namespace devilution
