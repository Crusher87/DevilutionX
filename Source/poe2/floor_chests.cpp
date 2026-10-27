#include "poe2/floor_chests.hpp"

#include <algorithm>
#include <optional>
#include <vector>

#include "control/control.hpp"
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
#include "poe2/monster_mods.hpp"
#include "utils/language.h"

namespace devilution {

namespace {

/** _oVar5 holds the kind as KindMark plus the kind, so a value left in a reused object slot never reads as one. */
constexpr int KindMark = 100;
constexpr int GuardRadius = 6;

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
 * The strongbox's guards come out around it, facing the hero: one kind with another kind as every third where the
 * floor has one. None of them is rare (rares lead the big fights only); the first is made magic to lead them.
 * Spawned monsters only come for the level's owner. Others in a multiplayer game see the leader as a normal monster.
 */
void BringOutGuards(const Player &player, const Object &chest)
{
	const int guards = chest._oVar6;
	const std::vector<size_t> types = FloorMonsterTypes();
	if (guards <= 0 || types.empty())
		return;
	const size_t main = types[GenerateRnd(static_cast<int>(types.size()))];
	const size_t other = types[GenerateRnd(static_cast<int>(types.size()))];
	Monster *leader = nullptr;
	for (int i = 0; i < guards; i++) {
		const std::optional<Point> at = FindClosestValidPosition(CanPlaceGuard, chest.position, 1, GuardRadius);
		if (!at)
			return;
		const size_t before = ActiveMonsterCount;
		SpawnMonster(*at, GetDirection(*at, player.position.tile), i % 3 == 2 ? other : main);
		if (ActiveMonsterCount == before)
			return;
		Monster &monster = Monsters[ActiveMonsters[ActiveMonsterCount - 1]];
		if (GetMonsterAffixes(monster).rarity == MonsterRarity::Rare)
			DemoteMonsterRarity(monster);
		if (leader == nullptr && GetMonsterAffixes(monster).rarity == MonsterRarity::Normal)
			leader = &monster;
	}
	if (leader != nullptr)
		PromoteMonsterRarity(*leader, MonsterRarity::Magic);
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
	chest._oVar6 = kind == FloorChestKind::Strongbox ? guards : 0;
}

bool OpenFloorChest(const Player &player, Object &chest, bool sendmsg)
{
	const FloorChestKind kind = GetFloorChestKind(chest);
	if (!gbIsPoe2 || setlevel || kind == FloorChestKind::Normal)
		return false;
	// The sure item, then the chest's own rolls; a magic or rare chest's sure item counts as one of them.
	int rolls = std::max(chest._oVar1, 1);
	switch (kind) {
	case FloorChestKind::Magic:
		DropPoe2ChestItem(chest.position, ITEM_QUALITY_MAGIC, sendmsg, "magic chest");
		rolls--;
		break;
	case FloorChestKind::Rare:
		DropPoe2ChestItem(chest.position, ITEM_QUALITY_RARE, sendmsg, "rare chest");
		rolls--;
		break;
	default:
		DropPoe2ChestItem(chest.position, ITEM_QUALITY_RARE, sendmsg, "strongbox");
		break;
	}
	for (int j = 0; j < rolls; j++) {
		if (chest._oVar2 != 0)
			CreateRndItem(chest.position, false, sendmsg, false);
		else
			CreateRndUseful(chest.position, sendmsg);
	}
	if (kind == FloorChestKind::Strongbox && sendmsg && &player == MyPlayer)
		BringOutGuards(player, chest);
	return true;
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
		break;
	default:
		break;
	}
}

} // namespace devilution
