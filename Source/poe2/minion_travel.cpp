#include "poe2/minion_travel.hpp"

#include <algorithm>
#include <optional>

#include "engine/path.h"
#include "engine/point.hpp"
#include "game_mode.hpp"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/minion_gems.hpp"
#include "poe2/player_data.hpp"
#include "poe2/profane_roads.hpp"
#include "poe2/spirit.hpp"
#include "poe2/spirit_skills.hpp"
#include "poe2/support_table.hpp"
#include "poe2/telemetry.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

/** @brief How a minion of the hero's goes along, or nothing for one that stays behind. */
std::optional<MinionTravelKind> TravelKindOf(const Monster &minion, const Player &player)
{
	if (minion.isInvalid || !IsMinionOf(minion, player) || minion.position.tile == GolemHoldingCell || minion.mode == MonsterMode::Death)
		return std::nullopt;
	// Raging spirits and illusions last a few seconds, and the King's dead belong to their road.
	if (minion.poe2.minionLifeTicks > 0 || minion.poe2.decoy || IsKingsDead(minion))
		return std::nullopt;
	if (minion.type().type == MT_GOLEM)
		return MinionTravelKind::Golem;
	if (minion.poe2.revived)
		return MinionTravelKind::Revived;
	if (const MinionGemData *gem = MinionGemOf(minion); gem != nullptr && IsRevivingMinionSkill(gem->skill))
		return MinionTravelKind::Gem;
	return std::nullopt;
}

/** @brief Whether the hero still keeps a minion like this one: its gem or skill is still on. */
bool StillKept(const Player &player, const TravellingMinion &minion)
{
	switch (minion.kind) {
	case MinionTravelKind::Gem: {
		// No more than the gem keeps now, as the count may have been lowered in town.
		const MinionGemData *gem = FindMinionGemByType(static_cast<_monster_id>(minion.type));
		if (gem == nullptr || !IsRevivingMinionSkill(gem->skill))
			return false;
		const int kept = KeptMinionCount(player, gem->skill);
		return kept > 0 && static_cast<int>(CountSkeletonMinions(player, gem->type)) < kept + SupportExtraMinions(player, gem->skill);
	}
	case MinionTravelKind::Golem:
		return ActiveGolemSkill(player) != PersistentSkill::None && FindGolemForPlayer(player) == nullptr;
	case MinionTravelKind::Revived:
		return IsPersistentSkillActive(player, PersistentSkill::Revive) && static_cast<int>(CountRevivedMinions(player)) < ReviveMinionLimit(player);
	}
	return false;
}

/** @brief A free tile beside the hero, in the hero's sight. */
std::optional<Point> FreeTileBeside(const Player &player)
{
	return FindClosestValidPosition(
	    [&player](Point tile) { return InDungeonBounds(tile) && !IsTileOccupied(tile) && IsTileWalkable(tile) && LineClearMissile(player.position.tile, tile); },
	    player.position.tile, 1, 5);
}

/** @brief Raises the minion again on the tile, with the life it had; nullptr when the level can't hold it. */
Monster *PlaceMinion(Player &player, const TravellingMinion &minion, Point tile)
{
	const auto type = static_cast<_monster_id>(minion.type);
	Monster *placed = nullptr;
	switch (minion.kind) {
	case MinionTravelKind::Gem: {
		// At the gem's level now, as a fresh one would be.
		const MinionGemData *gem = FindMinionGemByType(type);
		const int gemLevel = std::max(player.poe2.effectiveLevel(gem->skill), 1);
		if (SpawnSkeletonMinion(player, tile, static_cast<uint8_t>(std::min(gemLevel, 255)), type))
			placed = &Monsters[ActiveMonsters[ActiveMonsterCount - 1]];
		break;
	}
	case MinionTravelKind::Golem: {
		const PersistentSkill golemSkill = ActiveGolemSkill(player);
		SpawnGolem(player, tile, static_cast<uint8_t>(std::clamp(player.poe2.skillLevel(golemSkill), 1, 255)));
		placed = FindGolemForPlayer(player);
		// The golem skill's supports change its life, as when Summon Golem raises it.
		if (const int percent = GolemLifePercent(player); placed != nullptr && percent != 100)
			placed->maxHitPoints = Fixed26_6::fromRaw(std::max<int32_t>(placed->maxHitPoints.raw() * percent / 100, 64));
		break;
	}
	case MinionTravelKind::Revived: {
		const RevivedStats stats { Fixed26_6::fromRaw(std::max<int32_t>(minion.maxLife, 64)), minion.minDamage, minion.maxDamage, minion.armorClass, minion.resistance };
		placed = SpawnRevivedMinion(player, tile, type, stats);
		break;
	}
	}
	if (placed == nullptr)
		return nullptr;
	placed->hitPoints = Fixed26_6::fromRaw(std::clamp<int32_t>(minion.life, 64, std::max<int32_t>(placed->maxHitPoints.raw(), 64)));
	return placed;
}

void PutLE(std::vector<uint8_t> &bytes, uint32_t value, int size)
{
	for (int i = 0; i < size; i++)
		bytes.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
}

uint32_t GetLE(std::span<const uint8_t> bytes, size_t &at, int size)
{
	uint32_t value = 0;
	for (int i = 0; i < size; i++)
		value |= static_cast<uint32_t>(bytes[at++]) << (8 * i);
	return value;
}

} // namespace

void TakeMinionsAlong(Player &player)
{
	if (!gbIsPoe2 || gbIsMultiplayer || &player != MyPlayer || leveltype == DTYPE_TOWN || player.hasNoLife())
		return;
	Poe2PlayerData &data = player.poe2;
	std::vector<Monster *> going;
	for (size_t i = 0; i < ActiveMonsterCount && data.travellingMinionCount < MaxTravellingMinions; i++) {
		Monster &minion = Monsters[ActiveMonsters[i]];
		const std::optional<MinionTravelKind> kind = TravelKindOf(minion, player);
		if (!kind)
			continue;
		TravellingMinion &record = data.travellingMinions[data.travellingMinionCount++];
		record = {};
		record.kind = *kind;
		record.type = static_cast<uint16_t>(minion.type().type);
		record.life = minion.hitPoints.raw();
		record.maxLife = minion.maxHitPoints.raw();
		record.minDamage = minion.minDamage;
		record.maxDamage = minion.maxDamage;
		record.armorClass = minion.armorClass;
		record.resistance = minion.resistance;
		going.push_back(&minion);
	}
	// Off the level only now: taking one off reorders the active monsters.
	for (Monster *minion : going)
		TakeMinionOffLevel(*minion);
	if (!going.empty())
		TelemetryLog("minions", StrCat(going.size(), " went with the hero"));
}

void BringMinionsAlong(Player &player)
{
	if (!gbIsPoe2 || gbIsMultiplayer || &player != MyPlayer || player.hasNoLife())
		return;
	// The game keeps no monsters in town: they wait there with the hero.
	if (leveltype == DTYPE_TOWN)
		return;
	Poe2PlayerData &data = player.poe2;
	const size_t count = std::min<size_t>(data.travellingMinionCount, MaxTravellingMinions);
	data.travellingMinionCount = 0;
	int arrived = 0;
	int waiting = 0;
	int lost = 0;
	for (size_t i = 0; i < count; i++) {
		const TravellingMinion minion = data.travellingMinions[i];
		data.travellingMinions[i] = {};
		// One whose gem was taken off or switched off in town goes, as it would have where it stood.
		if (!StillKept(player, minion))
			continue;
		// Without room beside the hero it's lost; a gem's minion rises again on its own.
		const std::optional<Point> tile = FreeTileBeside(player);
		if (!tile) {
			lost++;
		} else if (PlaceMinion(player, minion, *tile) != nullptr) {
			arrived++;
		} else if (minion.kind != MinionTravelKind::Revived) {
			// Its kind isn't on this level (a quest level loads only some): it waits for the next one. A revived
			// monster's kind is rarely anywhere else, so it doesn't wait.
			data.travellingMinions[data.travellingMinionCount++] = minion;
			waiting++;
		} else {
			lost++;
		}
	}
	if (arrived > 0 || waiting > 0 || lost > 0)
		TelemetryLog("minions", StrCat(arrived, " arrived with the hero, ", waiting, " wait for the next level, ", lost, " were lost"));
}

std::vector<uint8_t> PackTravellingMinions(const Poe2PlayerData &data)
{
	const size_t count = std::min<size_t>(data.travellingMinionCount, MaxTravellingMinions);
	std::vector<uint8_t> bytes;
	bytes.reserve(1 + (count * TravellingMinionSaveSize));
	bytes.push_back(static_cast<uint8_t>(count));
	for (size_t i = 0; i < count; i++) {
		const TravellingMinion &minion = data.travellingMinions[i];
		PutLE(bytes, static_cast<uint8_t>(minion.kind), 1);
		PutLE(bytes, minion.type, 2);
		PutLE(bytes, static_cast<uint32_t>(minion.life), 4);
		PutLE(bytes, static_cast<uint32_t>(minion.maxLife), 4);
		PutLE(bytes, minion.minDamage, 1);
		PutLE(bytes, minion.maxDamage, 1);
		PutLE(bytes, minion.armorClass, 1);
		PutLE(bytes, minion.resistance, 2);
	}
	return bytes;
}

bool UnpackTravellingMinions(Poe2PlayerData &data, std::span<const uint8_t> bytes)
{
	data.travellingMinions = {};
	data.travellingMinionCount = 0;
	if (bytes.empty())
		return false;
	const size_t count = bytes[0];
	if (bytes.size() < 1 + (count * TravellingMinionSaveSize))
		return false;
	size_t at = 1;
	for (size_t i = 0; i < count; i++) {
		const size_t start = at;
		const uint32_t kind = GetLE(bytes, at, 1);
		TravellingMinion minion;
		minion.type = static_cast<uint16_t>(GetLE(bytes, at, 2));
		minion.life = static_cast<int32_t>(GetLE(bytes, at, 4));
		minion.maxLife = static_cast<int32_t>(GetLE(bytes, at, 4));
		minion.minDamage = static_cast<uint8_t>(GetLE(bytes, at, 1));
		minion.maxDamage = static_cast<uint8_t>(GetLE(bytes, at, 1));
		minion.armorClass = static_cast<uint8_t>(GetLE(bytes, at, 1));
		minion.resistance = static_cast<uint16_t>(GetLE(bytes, at, 2));
		at = start + TravellingMinionSaveSize;
		if (kind > static_cast<uint32_t>(MinionTravelKind::Revived) || minion.type >= NUM_MAX_MTYPES || data.travellingMinionCount >= MaxTravellingMinions)
			continue;
		minion.kind = static_cast<MinionTravelKind>(kind);
		data.travellingMinions[data.travellingMinionCount++] = minion;
	}
	return true;
}

} // namespace devilution
