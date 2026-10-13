#include "poe2/spirit.hpp"

#include <algorithm>
#include <optional>
#include <vector>

#include "engine/path.h"
#include "engine/point.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/monster_mods.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/skill_gems.hpp"
#include "utils/language.h"

namespace devilution {

namespace {

struct HeraldTrigger {
	Point position;
	int32_t lifeRaw;
	bool ash;
	bool thunder;
};

std::vector<HeraldTrigger> PendingHeralds;

bool MeetsGemRequirements(const Player &player, PersistentSkill skill)
{
	const std::optional<size_t> gemIndex = SkillGemItemIndex(SkillSlot::ForPersistent(skill));
	if (!gemIndex)
		return false;
	const ItemData &gem = AllItemsList[*gemIndex];
	return player._pStrength >= gem.iMinStr && player._pMagic >= gem.iMinMag && player._pDexterity >= gem.iMinDex;
}

bool CanHeraldHit(const Monster &monster)
{
	return !monster.hasNoLife() && !monster.isPlayerMinion() && monster.isPossibleToHit() && monster.mode != MonsterMode::Death;
}

/** @brief Deals herald damage to a monster on behalf of the local player, killing it if it drops. */
void HeraldHit(Monster &monster, DamageType damageType, Fixed26_6 damage)
{
	Player &player = *MyPlayer;
	ApplyMonsterDamage(damageType, monster, damage);
	monster.tag(player);
	TryInflictAilment(monster, player, damageType, damage);
	if (monster.hasNoLife())
		M_StartKill(monster, player);
}

void ExplodeAsh(const HeraldTrigger &trigger)
{
	AddVisualEffect(trigger.position, MissileGraphicID::BigExplosion);
	const Fixed26_6 damage = Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>(trigger.lifeRaw) * HeraldOfAshPercent / 100));
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (CanHeraldHit(monster) && trigger.position.ExactDistance(monster.position.tile) <= HeraldOfAshRadius)
			HeraldHit(monster, DamageType::Fire, damage);
	}
}

void ReleaseThunder(const HeraldTrigger &trigger)
{
	std::vector<Monster *> targets;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (CanHeraldHit(monster) && trigger.position.ExactDistance(monster.position.tile) <= HeraldOfThunderRadius)
			targets.push_back(&monster);
	}
	std::sort(targets.begin(), targets.end(), [&trigger](const Monster *a, const Monster *b) {
		return trigger.position.ExactDistance(a->position.tile) < trigger.position.ExactDistance(b->position.tile);
	});
	if (targets.size() > HeraldOfThunderBolts)
		targets.resize(HeraldOfThunderBolts);

	AddVisualEffect(trigger.position, MissileGraphicID::BlueFlareExplosion2);
	const Fixed26_6 damage = Fixed26_6::fromRaw(static_cast<int32_t>(static_cast<int64_t>(trigger.lifeRaw) * HeraldOfThunderPercent / 100));
	for (Monster *monster : targets) {
		AddVisualEffect(monster->position.tile, MissileGraphicID::LightningArrow);
		HeraldHit(*monster, DamageType::Lightning, damage);
	}
}

} // namespace

std::string_view PersistentSkillName(PersistentSkill skill)
{
	switch (skill) {
	case PersistentSkill::HeraldOfAsh:
		return _("Herald of Ash");
	case PersistentSkill::HeraldOfThunder:
		return _("Herald of Thunder");
	case PersistentSkill::PurityOfElements:
		return _("Purity of Elements");
	case PersistentSkill::SummonGolem:
		return _("Summon Golem");
	case PersistentSkill::SkeletalWarriors:
		return _("Skeletal Warriors");
	default:
		return {};
	}
}

std::string_view PersistentSkillDescription(PersistentSkill skill)
{
	switch (skill) {
	case PersistentSkill::HeraldOfAsh:
		return _("Enemies you kill explode, burning those nearby");
	case PersistentSkill::HeraldOfThunder:
		return _("Enemies you kill release bolts of lightning");
	case PersistentSkill::PurityOfElements:
		return _("Aura: +25% to fire, lightning and magic resistance");
	case PersistentSkill::SummonGolem:
		return _("A golem fights for you and returns when it falls");
	case PersistentSkill::SkeletalWarriors:
		return _("Skeletons fight for you and rise again when they fall");
	default:
		return {};
	}
}

int SpiritReservation(PersistentSkill skill)
{
	switch (skill) {
	case PersistentSkill::HeraldOfAsh:
	case PersistentSkill::HeraldOfThunder:
		return 30;
	case PersistentSkill::PurityOfElements:
		return 60;
	case PersistentSkill::SummonGolem:
		return 40;
	case PersistentSkill::SkeletalWarriors:
		return 30;
	default:
		return 0;
	}
}

bool ItemGrantsSpirit(const Item &item)
{
	if (item.isEmpty() || item.IDidx < 0 || static_cast<size_t>(item.IDidx) >= AllItemsList.size())
		return false;
	const std::string_view name = AllItemsList[static_cast<size_t>(item.IDidx)].iName;
	return name.ends_with("Sceptre");
}

int MinionDamagePercent(const Player &player)
{
	return 100 + GetPassiveTotals(player)[static_cast<size_t>(PassiveStat::MinionDamage)];
}

int MaxSpirit(const Player &player)
{
	int spirit = BaseSpirit + GetPassiveTotals(player)[static_cast<size_t>(PassiveStat::Spirit)];
	for (const inv_body_loc hand : { INVLOC_HAND_LEFT, INVLOC_HAND_RIGHT }) {
		const Item &item = player.InvBody[hand];
		if (ItemGrantsSpirit(item) && item._iStatFlag)
			spirit += SceptreSpirit;
	}
	return spirit;
}

namespace {

/** @brief Walks the Skills panel in order, reserving Spirit for each usable Spirit gem that fits. */
template <typename F>
int ReserveSpirit(const Player &player, F &&onActive)
{
	const int max = MaxSpirit(player);
	int reserved = 0;
	for (const SkillSlot &slot : player.poe2.skillSlots) {
		const PersistentSkill skill = slot.persistent();
		if (skill == PersistentSkill::None || !MeetsGemRequirements(player, skill))
			continue;
		const int cost = SpiritReservation(skill);
		if (reserved + cost > max)
			continue;
		reserved += cost;
		onActive(skill);
	}
	return reserved;
}

} // namespace

int ReservedSpirit(const Player &player)
{
	return ReserveSpirit(player, [](PersistentSkill) {});
}

bool IsPersistentSkillActive(const Player &player, PersistentSkill skill)
{
	if (!gbIsPoe2 || skill == PersistentSkill::None)
		return false;
	bool active = false;
	ReserveSpirit(player, [&](PersistentSkill activeSkill) {
		if (activeSkill == skill)
			active = true;
	});
	return active;
}

void QueueHeraldTriggers(const Player &player, const Monster &monster)
{
	if (!gbIsPoe2 || &player != MyPlayer)
		return;
	const bool ash = IsPersistentSkillActive(player, PersistentSkill::HeraldOfAsh);
	const bool thunder = IsPersistentSkillActive(player, PersistentSkill::HeraldOfThunder);
	if (!ash && !thunder)
		return;
	PendingHeralds.push_back({ monster.position.tile, monster.maxHitPoints.raw(), ash, thunder });
}

void ProcessHeralds()
{
	if (PendingHeralds.empty() || MyPlayer == nullptr)
		return;
	// Kills from these heralds queue more, which go off on the next tick: a chain reaction, not recursion.
	std::vector<HeraldTrigger> triggers;
	triggers.swap(PendingHeralds);
	for (const HeraldTrigger &trigger : triggers) {
		if (trigger.ash)
			ExplodeAsh(trigger);
		if (trigger.thunder)
			ReleaseThunder(trigger);
	}
}

void ClearHeralds()
{
	PendingHeralds.clear();
}

int SkeletalWarriorCount(int characterLevel)
{
	return std::min(2 + (characterLevel / 10), 4);
}

namespace {

std::optional<Point> FindMinionSpawnPosition(const Player &player)
{
	return FindClosestValidPosition(
	    [&player](Point target) { return !IsTileOccupied(target) && LineClearMissile(player.position.tile, target); },
	    player.position.tile, 1, 5);
}

bool CanRaiseMinions(const Player &player)
{
	return leveltype != DTYPE_TOWN && player.isLevelOwnedByLocalClient();
}

void KeepGolemAlive(Player &player)
{
	Monster *golem = FindGolemForPlayer(player);
	if (!IsPersistentSkillActive(player, PersistentSkill::SummonGolem)) {
		player.poe2.golemCooldown = 0;
		return;
	}
	if (golem != nullptr || !CanRaiseMinions(player)) {
		player.poe2.golemCooldown = GolemReviveTicks / 4;
		return;
	}
	if (player.poe2.golemCooldown > 0) {
		player.poe2.golemCooldown--;
		return;
	}
	const std::optional<Point> position = FindMinionSpawnPosition(player);
	if (!position)
		return;
	SpawnGolem(player, *position, static_cast<uint8_t>(GemSpellLevel(player.getCharacterLevel())));
	if (Monster *golem = FindGolemForPlayer(player); golem != nullptr) {
		golem->minDamage = static_cast<uint8_t>(std::min(golem->minDamage * MinionDamagePercent(player) / 100, 255));
		golem->maxDamage = static_cast<uint8_t>(std::min(golem->maxDamage * MinionDamagePercent(player) / 100, 255));
	}
	AddVisualEffect(*position, MissileGraphicID::Resurrect);
	player.poe2.golemCooldown = GolemReviveTicks;
}

void KeepSkeletonsAlive(Player &player)
{
	if (!IsPersistentSkillActive(player, PersistentSkill::SkeletalWarriors)) {
		player.poe2.skeletonCooldown = 0;
		if (CountSkeletonMinions(player) > 0)
			KillSkeletonMinions(player);
		return;
	}
	const size_t wanted = static_cast<size_t>(SkeletalWarriorCount(player.getCharacterLevel()));
	if (!CanRaiseMinions(player) || CountSkeletonMinions(player) >= wanted) {
		player.poe2.skeletonCooldown = SkeletonReviveTicks / 4;
		return;
	}
	if (player.poe2.skeletonCooldown > 0) {
		player.poe2.skeletonCooldown--;
		return;
	}
	const std::optional<Point> position = FindMinionSpawnPosition(player);
	if (!position)
		return;
	if (SpawnSkeletonMinion(player, *position, static_cast<uint8_t>(GemSpellLevel(player.getCharacterLevel()))))
		AddVisualEffect(*position, MissileGraphicID::Resurrect);
	player.poe2.skeletonCooldown = SkeletonReviveTicks;
}

} // namespace

void ProcessPersistentSkills(Player &player)
{
	if (!gbIsPoe2 || &player != MyPlayer || player.hasNoLife())
		return;
	KeepGolemAlive(player);
	KeepSkeletonsAlive(player);
}

} // namespace devilution
