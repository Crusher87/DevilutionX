#include "poe2/profane_roads.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <optional>
#include <vector>

#include "engine/path.h"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "levels/gendung.h"
#include "missiles.h"
#include "monster.h"
#include "multi.h"
#include "player.h"
#include "poe2/monster_mods.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/spirit_skills.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"
#include "utils/is_of.hpp"

namespace devilution {

namespace {

/** @brief The toll, "The dead do not rest": this share of the hero's kills rise, in percent. */
constexpr int DeadDoNotRestChance = 25;
/** @brief With the pact the share grows, and they rise at full life. */
constexpr int KingsDeadPactChance = 33;
/** @brief A body that isn't down by then never rises. */
constexpr uint16_t PendingRiseTimeoutTicks = 10 * 20;
constexpr size_t MaxPendingRises = 32;

/** @brief A rise waiting for its body to fall: it stands up once the dead monster's death animation has ended. */
struct PendingRise {
	Point tile;
	size_t monsterId;
	uint8_t level;
	bool setLevel;
	/** @brief The dead monster's own type (a level type index), for levels where no skeletons walk. */
	size_t ownTypeIndex;
	_monster_id ownType;
	RevivedStats stats;
	/** @brief One of the King's dead (for the hero), else a hostile rise. */
	bool kingsDead;
	bool fullLife;
	uint16_t ticks;
};

std::vector<PendingRise> PendingRises;

bool HasPact(const Player &player)
{
	return PassiveTotal(player, PassiveStat::KingsDeadPact) > 0;
}

/** @brief Never in town, the arena, the quest set levels or Diablo's level. */
bool CanRiseHere()
{
	return leveltype != DTYPE_TOWN && !setlevel && currlevel != 16;
}

void QueueRise(const Monster &monster, bool kingsDead, bool fullLife)
{
	if (PendingRises.size() >= MaxPendingRises)
		return;
	PendingRise rise {};
	// MonsterDeath puts the body down on the tile the monster came from.
	rise.tile = monster.position.old;
	rise.monsterId = monster.getId();
	rise.level = currlevel;
	rise.setLevel = setlevel;
	rise.ownTypeIndex = monster.levelType;
	rise.ownType = monster.type().type;
	rise.stats = { std::max(monster.maxHitPoints / 2, Fixed26_6::fromInt(1)), monster.minDamage, monster.maxDamage, monster.armorClass, monster.resistance };
	rise.kingsDead = kingsDead;
	rise.fullLife = fullLife;
	PendingRises.push_back(rise);
}

/** @brief The King's dead fight in melee (they get the minions' melee AI), so they rise as a random axe or sword skeleton of the level. */
std::optional<_monster_id> KingsDeadType()
{
	_monster_id found[8];
	int count = 0;
	for (size_t i = 0; i < LevelMonsterTypeCount; i++) {
		if (IsAnyOf(LevelMonsterTypes[i].type, MT_WSKELAX, MT_TSKELAX, MT_RSKELAX, MT_XSKELAX, MT_WSKELSD, MT_TSKELSD, MT_RSKELSD, MT_XSKELSD)
		    && std::find(found, found + count, LevelMonsterTypes[i].type) == found + count)
			found[count++] = LevelMonsterTypes[i].type;
	}
	if (count == 0)
		return std::nullopt;
	return found[GenerateRnd(count)];
}

/** @brief The oldest of the hero's King's dead (the least time left), or nullptr. */
std::optional<size_t> OldestKingsDead(const Player &player)
{
	std::optional<size_t> oldest;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &monster = Monsters[ActiveMonsters[i]];
		if (!IsKingsDead(monster) || monster.hasNoLife() || monster.goalVar3 != player.getId())
			continue;
		if (!oldest || monster.poe2.kingsDeadTicks < Monsters[ActiveMonsters[*oldest]].poe2.kingsDeadTicks)
			oldest = i;
	}
	return oldest;
}

void RaiseKingsDead(const PendingRise &rise, Point position)
{
	const Player &player = *MyPlayer;
	const int cap = std::max(KingsDeadMinCap, ReviveMinionLimit(player));
	if (static_cast<int>(CountRevivedMinions(player) + CountKingsDead(player)) >= cap) {
		// Full: the new one replaces the oldest of the King's dead, and never one of Revive's.
		const std::optional<size_t> oldest = OldestKingsDead(player);
		if (!oldest)
			return;
		RemoveMonsterQuietly(*oldest);
	}
	RevivedStats stats = rise.stats;
	stats.life = Fixed26_6::fromRaw(static_cast<int32_t>(std::min<int64_t>(static_cast<int64_t>(stats.life.raw()) * UniqueMinionLifePercent(player) / 100, INT32_MAX / 4)));
	Monster *minion = SpawnRevivedMinion(player, position, KingsDeadType().value_or(rise.ownType), stats);
	if (minion == nullptr)
		return;
	minion->poe2.revived = false;
	minion->flags |= MFLAG_KINGS_DEAD;
	minion->poe2.kingsDeadTicks = KingsDeadTicks;
	AddVisualEffect(position, MissileGraphicID::Resurrect);
	TelemetryLog("profane", "one of the King's dead rose");
}

void RaiseHostile(const PendingRise &rise, Point position)
{
	// As Leoric raises them: a skeleton of the level (archers too), or the dead monster itself where none walk.
	const size_t typeIndex = RandomLevelSkeletonType().value_or(rise.ownTypeIndex);
	Monster *risen = SpawnRisenMonster(position, typeIndex);
	if (risen == nullptr)
		return;
	if (!rise.fullLife) {
		risen->maxHitPoints = std::max(risen->maxHitPoints / 2, Fixed26_6::fromInt(1));
		risen->hitPoints = risen->maxHitPoints;
	}
	risen->poe2.risingTicks = RisingTicks;
	TelemetryLog("profane", "a kill rose again");
}

void StandUp(const PendingRise &rise)
{
	const std::optional<Point> position = FindClosestValidPosition([](Point target) { return IsTileFreeForMonster(target); }, rise.tile, 0, 1);
	if (!position)
		return;
	dCorpse[rise.tile.x][rise.tile.y] = 0;
	if (rise.kingsDead)
		RaiseKingsDead(rise, *position);
	else
		RaiseHostile(rise, *position);
}

void CountDownKingsDead()
{
	for (size_t i = ActiveMonsterCount; i-- > 0;) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (!IsKingsDead(monster) || monster.hasNoLife())
			continue;
		if (monster.poe2.kingsDeadTicks > 0)
			monster.poe2.kingsDeadTicks--;
		// They leave without a body; one loaded with no time left leaves at once.
		if (monster.poe2.kingsDeadTicks == 0)
			RemoveMonsterQuietly(i);
	}
}

} // namespace

bool IsRisen(const Monster &monster)
{
	return (monster.flags & MFLAG_RISEN) != 0;
}

bool IsKingsDead(const Monster &monster)
{
	return (monster.flags & MFLAG_KINGS_DEAD) != 0;
}

void OnProfaneRoadKill(const Player &player, const Monster &monster)
{
	if (!gbIsPoe2 || gbIsMultiplayer || &player != MyPlayer || monster.isPlayerMinion() || !CanRiseHere())
		return;
	const bool pact = HasPact(player);
	// A culled monster stays dead, and with the pact it rises for the hero. So does a rising one put down.
	if (monster.poe2.culled) {
		if (pact)
			QueueRise(monster, /*kingsDead=*/true, /*fullLife=*/false);
		return;
	}
	if (IsRisen(monster) || monster.isUnique() || monster.type().type == MT_DIABLO || GetMonsterAffixes(monster).rarity == MonsterRarity::Rare)
		return;
	const int chance = pact ? KingsDeadPactChance : (PassiveTotal(player, PassiveStat::DeadDoNotRest) > 0 ? DeadDoNotRestChance : 0);
	if (chance == 0 || GenerateRnd(100) >= chance)
		return;
	QueueRise(monster, /*kingsDead=*/false, /*fullLife=*/pact);
}

bool TryPutDownRisen(const Player &player, Monster &monster)
{
	if (!gbIsPoe2 || monster.poe2.risingTicks == 0 || !IsRisen(monster) || monster.hasNoLife() || &player != MyPlayer)
		return false;
	// With the pact a put-down counts as a Cull, so it rises for the hero instead.
	if (HasPact(player))
		monster.poe2.culled = true;
	AddVisualEffect(monster.position.tile, MissileGraphicID::BoneHit);
	ApplyMonsterDamage(DamageType::Physical, monster, monster.hitPoints);
	return true;
}

void ProcessProfaneRoads()
{
	if (!gbIsPoe2 || gbIsMultiplayer)
		return;
	for (size_t i = 0; i < PendingRises.size();) {
		PendingRise &rise = PendingRises[i];
		const Monster &dead = Monsters[rise.monsterId];
		const bool sameLevel = rise.level == currlevel && rise.setLevel == setlevel;
		if (!sameLevel || ++rise.ticks > PendingRiseTimeoutTicks) {
			PendingRises.erase(PendingRises.begin() + static_cast<std::ptrdiff_t>(i));
			continue;
		}
		if (!dead.isInvalid) {
			// While it waits, the ground where it fell glows red.
			if (rise.ticks % 8 == 1)
				AddVisualEffect(rise.tile, MissileGraphicID::RedFlareExplosion);
			i++;
			continue;
		}
		const PendingRise ready = rise;
		PendingRises.erase(PendingRises.begin() + static_cast<std::ptrdiff_t>(i));
		StandUp(ready);
	}
	CountDownKingsDead();
}

void OnProfaneRoadDeath(const Monster &monster)
{
	if (!gbIsPoe2 || gbIsMultiplayer || MyPlayer == nullptr || monster.isPlayerMinion())
		return;
	// A death that Kill Burst's contagion already spread doesn't spread again.
	if (TakeBurstSpread(monster))
		return;
	const Player &player = *MyPlayer;
	if (PassiveTotal(player, PassiveStat::HiveBreath) <= 0 || !CarriesAilment(monster))
		return;
	// An ignite Warden of Embers already spread stays where it is.
	const bool withIgnite = !monster.poe2.igniteSpread;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &other = Monsters[ActiveMonsters[i]];
		if (&other == &monster || other.hasNoLife() || other.isPlayerMinion())
			continue;
		if (std::max(std::abs(other.position.tile.x - monster.position.tile.x), std::abs(other.position.tile.y - monster.position.tile.y)) > 1)
			continue;
		if (CopyAilments(monster, other, player, withIgnite))
			AddVisualEffect(other.position.tile, MissileGraphicID::AcidSplat);
	}
}

void ClearPendingRises()
{
	PendingRises.clear();
}

bool HasInfravision(const Player &player)
{
	return player._pInfraFlag || (gbIsPoe2 && PassiveTotal(player, PassiveStat::EyesInTheDark) > 0);
}

namespace {

bool WieldsStaff(const Player &player)
{
	for (const Item &item : { std::cref(player.InvBody[INVLOC_HAND_LEFT]), std::cref(player.InvBody[INVLOC_HAND_RIGHT]) }) {
		if (!item.isEmpty() && item._iStatFlag && item._itype == ItemType::Staff)
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

size_t CountKingsDead(const Player &player)
{
	size_t count = 0;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &monster = Monsters[ActiveMonsters[i]];
		if (IsKingsDead(monster) && monster.goalVar3 == player.getId() && !monster.hasNoLife())
			count++;
	}
	return count;
}

void MakeRoomForRevive(const Player &player, size_t revived, int limit)
{
	if (static_cast<int>(revived + CountKingsDead(player)) < std::max(KingsDeadMinCap, limit))
		return;
	if (const std::optional<size_t> oldest = OldestKingsDead(player); oldest)
		RemoveMonsterQuietly(*oldest);
}

} // namespace devilution
