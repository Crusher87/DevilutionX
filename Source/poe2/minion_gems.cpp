#include "poe2/minion_gems.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "engine/path.h"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/monster_levels.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/profane_roads.hpp"
#include "poe2/skill_resources.hpp"
#include "poe2/spirit.hpp"
#include "poe2/spirit_buffs.hpp"
#include "poe2/support_table.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/vfx.hpp"

namespace devilution {

namespace {

// POE2's Spirit per minion for gem levels 1 to 20 (Path of Building's data).
constexpr std::array<uint8_t, 20> WarriorSpirit { 60, 44, 33, 27, 25, 25, 23, 23, 23, 22, 22, 22, 21, 21, 21, 21, 20, 20, 20, 20 };
constexpr std::array<uint8_t, 20> ArsonistSpirit { 90, 65, 50, 47, 47, 47, 44, 44, 44, 42, 42, 42, 40, 40, 40, 40, 39, 39, 39, 39 };
constexpr std::array<uint8_t, 20> SniperSpirit { 90, 65, 50, 41, 37, 37, 35, 35, 35, 33, 33, 33, 31, 31, 31, 31, 30, 30, 30, 30 };
constexpr std::array<uint8_t, 20> FrostMageSpirit { 120, 87, 66, 54, 49, 49, 46, 46, 46, 43, 43, 43, 41, 41, 41, 41, 39, 39, 39, 39 };
constexpr std::array<uint8_t, 20> StormMageSpirit { 120, 87, 66, 54, 49, 49, 45, 45, 45, 42, 42, 42, 40, 40, 40, 40, 39, 39, 39, 39 };
constexpr std::array<uint8_t, 20> BruteSpirit { 165, 119, 91, 74, 63, 63, 58, 58, 58, 54, 54, 54, 51, 51, 51, 51, 48, 48, 48, 48 };
/** The other minion gems reserve their Spirit once, not per minion. */
constexpr std::array<uint8_t, 20> NoSpirit {};

/**
 * Life and damage follow POE2's minions against its Skeletal Warrior. Each kind wears its own colours: the Warriors
 * and Arsonists keep theirs, the archers are the other bowmen, and the Reavers, Brutes and Clerics are captains.
 * Diablo has no wolves, so Wolf Pack's are Shadow Beasts; the raging spirits are Hellfire's fire bats, and the
 * illusion of the hero is an Illusion Weaver.
 */
constexpr std::array<MinionGemData, MinionCountSkills.size() + 3> MinionGems { {
	{ PersistentSkill::SkeletalWarriors, MT_WSKELAX, 100, 100, MinionShot::None, 0, 0, 0, WarriorSpirit, 2, 2 },
	{ PersistentSkill::SkeletalMages, MT_RSKELBW, 60, 100, MinionShot::Firebolt, 150, 7, 0, ArsonistSpirit, 0, 1 },
	{ PersistentSkill::SkeletalSnipers, MT_WSKELBW, 60, 100, MinionShot::Arrow, 160, 10, 24, SniperSpirit, 0, 1 },
	{ PersistentSkill::SkeletalFrostMages, MT_XSKELBW, 60, 90, MinionShot::FrostBolt, 130, 7, 0, FrostMageSpirit, 0, 1 },
	{ PersistentSkill::SkeletalStormMages, MT_TSKELBW, 60, 90, MinionShot::ChargedBolts, 70, 6, 0, StormMageSpirit, 0, 1 },
	{ PersistentSkill::SkeletalReavers, MT_RSKELSD, 80, 70, MinionShot::None, 0, 0, 0, SniperSpirit, 0, 1 },
	{ PersistentSkill::SkeletalBrutes, MT_XSKELSD, 140, 280, MinionShot::None, 0, 0, 24, BruteSpirit, 0, 1 },
	{ PersistentSkill::SkeletalClerics, MT_WSKELSD, 60, 0, MinionShot::None, 0, 0, 0, WarriorSpirit, 0, 1 },
	{ PersistentSkill::WolfPack, MT_WSCAV, 65, 55, MinionShot::None, 0, 0, 0, NoSpirit, 0, 0 },
	{ PersistentSkill::RagingSpirits, MT_FIREBAT, 35, 70, MinionShot::None, 0, 0, 0, NoSpirit, 0, 0 },
	{ PersistentSkill::LingeringIllusion, MT_ILLWEAV, 1, 0, MinionShot::None, 0, 0, 0, NoSpirit, 0, 0 },
} };

constexpr bool SameOrder()
{
	for (size_t i = 0; i < MinionCountSkills.size(); i++) {
		if (MinionGems[i].skill != MinionCountSkills[i])
			return false;
	}
	return true;
}
static_assert(SameOrder(), "MinionGems follows MinionCountSkills");
static_assert(MinionGems[MinionCountSkills.size()].skill == PersistentSkill::WolfPack, "Wolf Pack follows the skeleton gems, as in RevivingMinionSkills");

int AtLevel(int gemLevel, int at1, int at20)
{
	const int level = std::clamp(gemLevel, 1, 30);
	return at1 + ((at20 - at1) * (level - 1) / 19);
}

bool IsGemMinionOf(const Monster &monster, const Player &player)
{
	return IsMinionOf(monster, player) && monster.position.tile != GolemHoldingCell;
}

struct FrenzyRemnant {
	Point position;
	uint16_t ticks;
};

constexpr size_t MaxFrenzyRemnants = 6;
std::vector<FrenzyRemnant> FrenzyRemnants;
/** Tiles of enemies the hero's fire spell hit, each calling a raging spirit next tick. */
std::vector<Point> PendingSpirits;
/** Where a dodge roll began, and the game ticks until the hero has left the tile for the illusion. */
std::optional<Point> PendingIllusion;
uint8_t PendingIllusionTicks = 0;
constexpr uint8_t IllusionDelayTicks = 3;

int GemLevel(const Player &player, PersistentSkill skill)
{
	return std::max(player.poe2.skillLevel(skill), 1);
}

bool On(const Player &player, PersistentSkill skill)
{
	return gbIsPoe2 && IsPersistentSkillActive(player, skill);
}

bool Alive(const Monster &monster)
{
	return !monster.hasNoLife() && monster.mode != MonsterMode::Death;
}

std::optional<Point> FreeTileNear(Point center, int maxRadius)
{
	return FindClosestValidPosition([](Point tile) { return InDungeonBounds(tile) && !IsTileOccupied(tile) && IsTileWalkable(tile); }, center, 0, maxRadius);
}

/** @brief Raises one of the gem's minions; the new minion, or nullptr if there's no room. */
Monster *SpawnGemMinion(const Player &player, Point position, int gemLevel, _monster_id type)
{
	if (!SpawnSkeletonMinion(player, position, static_cast<uint8_t>(std::clamp(gemLevel, 1, 255)), type))
		return nullptr;
	// It's the last active monster.
	return &Monsters[ActiveMonsters[ActiveMonsterCount - 1]];
}

void DropFrenzyRemnant(Point position)
{
	if (FrenzyRemnants.size() >= MaxFrenzyRemnants)
		FrenzyRemnants.erase(FrenzyRemnants.begin());
	FrenzyRemnants.push_back({ position, IllusionRemnantTicks });
	AddVisualEffect(position, MissileGraphicID::RedFlareExplosion);
}

void TickFrenzyRemnants(Player &player)
{
	for (size_t i = 0; i < FrenzyRemnants.size();) {
		FrenzyRemnant &remnant = FrenzyRemnants[i];
		// (The red flare itself is a monster's missile that isn't always loaded; its burst is.)
		if (remnant.ticks % 20 == 0)
			AddVisualEffect(remnant.position, MissileGraphicID::RedFlareExplosion);
		if (player.position.tile.WalkingDistance(remnant.position) <= 1) {
			GainFrenzyCharge(player);
			TelemetryLog("spirit", "picked up a Frenzy remnant");
			FrenzyRemnants.erase(FrenzyRemnants.begin() + static_cast<std::ptrdiff_t>(i));
			continue;
		}
		if (--remnant.ticks == 0) {
			FrenzyRemnants.erase(FrenzyRemnants.begin() + static_cast<std::ptrdiff_t>(i));
			continue;
		}
		i++;
	}
}

/** @brief Raging Spirits: calls the spirits the fire spell's hits asked for last tick, beside the enemies hit. */
void CallRagingSpirits(Player &player)
{
	if (PendingSpirits.empty())
		return;
	const std::vector<Point> near = std::move(PendingSpirits);
	PendingSpirits.clear();
	if (!On(player, PersistentSkill::RagingSpirits) || leveltype == DTYPE_TOWN)
		return;
	const int gemLevel = GemLevel(player, PersistentSkill::RagingSpirits);
	for (const Point tile : near) {
		if (CountSkeletonMinions(player, MT_FIREBAT) >= RagingSpiritsMax)
			break;
		const std::optional<Point> position = FreeTileNear(tile, 3);
		if (!position)
			continue;
		Monster *spirit = SpawnGemMinion(player, *position, gemLevel, MT_FIREBAT);
		if (spirit == nullptr)
			continue;
		spirit->poe2.minionLifeTicks = RagingSpiritTicks;
		AddVisualEffect(*position, MissileGraphicID::FireArrowExplosion);
		vfx::Impact(*position, vfx::Element::Fire, 0.9F);
	}
}

/** @brief Lingering Illusion: once the hero has rolled off the tile, an illusion of them stands where the roll began. */
void PlaceIllusion(Player &player)
{
	if (!PendingIllusion || PendingIllusionTicks-- > 0)
		return;
	const Point start = *PendingIllusion;
	PendingIllusion = std::nullopt;
	if (!On(player, PersistentSkill::LingeringIllusion) || leveltype == DTYPE_TOWN)
		return;
	// Too many: the one with the least time left fades.
	if (CountSkeletonMinions(player, MT_ILLWEAV) >= MaxIllusions) {
		Monster *oldest = nullptr;
		for (size_t i = 0; i < ActiveMonsterCount; i++) {
			Monster &monster = Monsters[ActiveMonsters[i]];
			if (monster.poe2.decoy && Alive(monster) && IsGemMinionOf(monster, player) && (oldest == nullptr || monster.poe2.minionLifeTicks < oldest->poe2.minionLifeTicks))
				oldest = &monster;
		}
		if (oldest != nullptr) {
			oldest->poe2.minionLifeTicks = 0;
			KillGolem(*oldest);
		}
	}
	const std::optional<Point> position = FreeTileNear(start, 2);
	if (!position)
		return;
	Monster *illusion = SpawnGemMinion(player, *position, GemLevel(player, PersistentSkill::LingeringIllusion), MT_ILLWEAV);
	if (illusion == nullptr)
		return;
	// 1% of the hero's life: a hit or two breaks it.
	illusion->maxHitPoints = Fixed26_6::fromRaw(std::max<int32_t>(player._pMaxHP.raw() / 100, 64));
	illusion->hitPoints = illusion->maxHitPoints;
	illusion->poe2.minionLifeTicks = IllusionTicks;
	illusion->poe2.decoy = true;
	illusion->direction = player._pdir;
	AddVisualEffect(*position, MissileGraphicID::Etherealize);
}

bool SwarmCanBite(const Monster &monster)
{
	return Alive(monster) && !monster.isPlayerMinion() && monster.isPossibleToHit();
}

/** @brief The enemy nearest the hero, within reach, that no other swarm bites yet if there is one. */
Monster *SwarmTarget(const Player &player, const SpiritRuntime &state)
{
	Monster *best = nullptr;
	int bestScore = INT32_MAX;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (!SwarmCanBite(monster))
			continue;
		const int distance = player.position.tile.WalkingDistance(monster.position.tile);
		if (distance > SwarmReachTiles)
			continue;
		const bool bitten = std::any_of(state.swarms.begin(), state.swarms.end(),
		    [&](const RavenousSwarm &swarm) { return swarm.ticks > 0 && swarm.target == static_cast<int16_t>(monster.getId()); });
		const int score = distance + (bitten ? SwarmReachTiles + 1 : 0);
		if (score < bestScore) {
			best = &monster;
			bestScore = score;
		}
	}
	return best;
}

void SwarmBite(Player &player, Monster &monster, int gemLevel)
{
	const int32_t bite = SwarmBiteRaw(player, gemLevel);
	AddVisualEffect(monster.position.tile, MissileGraphicID::AcidSplat);
	vfx::Sparks(monster.position.tile, vfx::Element::Chaos, 0.5F);
	TelemetryNoteDamageSource("Ravenous Swarm");
	if (!SpiritHitMonster(player, monster, DamageType::Physical, Fixed26_6::fromRaw(bite)))
		return;
	// Each bite adds a poison of its own, up to a few at once.
	Poe2MonsterState &target = monster.poe2;
	const int32_t perTick = std::max<int32_t>(static_cast<int32_t>(static_cast<int64_t>(bite) * SwarmPoisonPercent / 100 / SwarmPoisonTicks), 1);
	const int32_t cap = std::max(perTick * SwarmPoisonStacks, target.poisonDamagePerTick);
	target.poisonDamagePerTick = target.poisonTicks > 0 ? std::min(target.poisonDamagePerTick + perTick, cap) : perTick;
	target.poisonTicks = std::max<uint8_t>(target.poisonTicks, SwarmPoisonTicks);
	target.poisonSource = static_cast<int8_t>(player.getId());
}

/** @brief Ravenous Swarm: sends out a new swarm while an enemy is near, and lets each swarm bite. */
void TickSwarms(Player &player)
{
	SpiritRuntime &state = player.poe2.spiritState;
	if (!On(player, PersistentSkill::RavenousSwarm) || leveltype == DTYPE_TOWN) {
		state.swarms = {};
		state.swarmTicks = 0;
		return;
	}
	const int gemLevel = GemLevel(player, PersistentSkill::RavenousSwarm);
	if (state.swarmTicks > 0)
		state.swarmTicks--;
	if (state.swarmTicks == 0) {
		const auto free = std::find_if(state.swarms.begin(), state.swarms.end(), [](const RavenousSwarm &swarm) { return swarm.ticks == 0; });
		Monster *target = free != state.swarms.end() ? SwarmTarget(player, state) : nullptr;
		if (target != nullptr) {
			*free = { static_cast<int16_t>(target->getId()), static_cast<uint16_t>(SwarmTicks(gemLevel)), 0 };
			state.swarmTicks = SwarmEveryTicks;
			AddVisualEffect(player.position.tile, MissileGraphicID::AcidSplat);
			vfx::Arc(player.position.tile, target->position.tile, vfx::Element::Chaos);
		}
	}
	for (RavenousSwarm &swarm : state.swarms) {
		if (swarm.ticks == 0)
			continue;
		swarm.ticks--;
		Monster *target = swarm.target >= 0 ? &Monsters[static_cast<size_t>(swarm.target)] : nullptr;
		// It finds another enemy near the hero when its own dies or gets away.
		if (target == nullptr || !SwarmCanBite(*target) || player.position.tile.WalkingDistance(target->position.tile) > SwarmReachTiles + 2) {
			target = SwarmTarget(player, state);
			swarm.target = target != nullptr ? static_cast<int16_t>(target->getId()) : -1;
			if (target != nullptr)
				vfx::Arc(player.position.tile, target->position.tile, vfx::Element::Chaos);
		}
		if (target == nullptr) {
			// It hums around the hero, waiting.
			if (swarm.ticks % 6 == 0)
				vfx::Sparks(player.position.tile, vfx::Element::Chaos, 0.3F);
			continue;
		}
		if (swarm.ticks % 3 == 0)
			vfx::Sparks(target->position.tile, vfx::Element::Chaos, 0.3F);
		if (swarm.biteTicks > 0) {
			swarm.biteTicks--;
			continue;
		}
		swarm.biteTicks = SwarmBiteTicks;
		SwarmBite(player, *target, gemLevel);
	}
}

/** @brief The hero's minion this close to the Cleric that has lost the largest share of its life, at least a tenth. */
Monster *MostHurtMinion(const Player &player, const Monster &cleric)
{
	Monster *hurt = nullptr;
	int64_t hurtMissing = 0;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (&monster == &cleric || !IsGemMinionOf(monster, player) || monster.position.tile.WalkingDistance(cleric.position.tile) > ClericRadius)
			continue;
		const int64_t maxLife = std::max<int32_t>(monster.maxHitPoints.raw(), 1);
		// The share of its life it has lost, in 1/10000.
		const int64_t missing = (maxLife - monster.hitPoints.raw()) * 10000 / maxLife;
		if (missing >= ClericHurtPercent * 100 && missing > hurtMissing) {
			hurt = &monster;
			hurtMissing = missing;
		}
	}
	return hurt;
}

void ClericHeal(const Player &player, Monster &cleric)
{
	Monster *hurt = MostHurtMinion(player, cleric);
	if (hurt == nullptr) {
		// Looks again soon.
		cleric.poe2.clericTicks = 10;
		return;
	}
	const int gemLevel = std::max(player.poe2.skillLevel(PersistentSkill::SkeletalClerics), 1);
	const int32_t heal = static_cast<int32_t>(static_cast<int64_t>(hurt->maxHitPoints.raw()) * ClericHealPercent(gemLevel) / 100);
	hurt->hitPoints = Fixed26_6::fromRaw(std::min(hurt->hitPoints.raw() + heal, hurt->maxHitPoints.raw()));
	vfx::Arc(cleric.position.tile, hurt->position.tile, vfx::Element::Holy);
	vfx::Impact(hurt->position.tile, vfx::Element::Holy, 0.8F);
	cleric.poe2.clericTicks = ClericHealTicks;
}

} // namespace

int WolfPackSize(int gemLevel)
{
	return AtLevel(gemLevel, 3, 6);
}

int SwarmTicks(int gemLevel)
{
	return AtLevel(gemLevel, 6 * 20, 196);
}

int32_t SwarmBiteRaw(const Player &player, int gemLevel)
{
	// A Skeletal Warrior's average blow (InitSkeletonMinion).
	const int level = MinionLevelForGemLevel(gemLevel);
	const int damageSum = 2 + (level / 3) + gemLevel + 5 + (level / 2) + (2 * gemLevel);
	int64_t blow = static_cast<int64_t>(damageSum / 2) * 64;
	// On the minion tables (plan v2 step 11): the Warrior's blow at its level.
	if (HasMonsterLevelTable())
		blow = static_cast<int64_t>(MinionHit10(SkeletonMinionType, level, damageSum)) * 64 / 10;
	const int64_t raw = blow * MinionDamagePercent(player) / 100 * SwarmBitePercent / 100;
	return static_cast<int32_t>(std::clamp<int64_t>(raw, 64, INT32_MAX / 2));
}

int SacrificeRevivePercent(int gemLevel)
{
	return AtLevel(gemLevel, 159, 140);
}

int IllusionSecondRemnantChance(int gemLevel)
{
	return std::min(AtLevel(gemLevel, 0, 76), 100);
}

std::span<const MinionGemData> MinionGemTable()
{
	return MinionGems;
}

const MinionGemData *FindMinionGem(PersistentSkill skill)
{
	for (const MinionGemData &gem : MinionGems) {
		if (gem.skill == skill)
			return &gem;
	}
	return nullptr;
}

const MinionGemData *FindMinionGemByType(_monster_id type)
{
	for (const MinionGemData &gem : MinionGems) {
		if (gem.type == type)
			return &gem;
	}
	return nullptr;
}

bool IsMinionGemType(_monster_id type)
{
	return FindMinionGemByType(type) != nullptr;
}

const MinionGemData *MinionGemOf(const Monster &monster)
{
	if (!monster.isPlayerMinion() || monster.poe2.revived || IsKingsDead(monster))
		return nullptr;
	return FindMinionGemByType(monster.type().type);
}

int ClericHealPercent(int gemLevel)
{
	return AtLevel(gemLevel, 20, 35);
}

int MinionGemDamagePercent(const Monster &minion)
{
	const MinionGemData *gem = MinionGemOf(minion);
	if (gem == nullptr)
		return 100;
	return gem->damagePercent * (100 + (ReaverRagePercent * minion.poe2.minionRage)) / 100;
}

int MinionGemFirePercent(const Monster &minion)
{
	const MinionGemData *gem = MinionGemOf(minion);
	return gem != nullptr && gem->skill == PersistentSkill::RagingSpirits ? RagingSpiritFirePercent : 0;
}

MissileID MinionShotMissile(MinionShot shot)
{
	switch (shot) {
	case MinionShot::Arrow:
		return MissileID::Arrow;
	case MinionShot::FrostBolt:
		return MissileID::BlueFlare;
	case MinionShot::ChargedBolts:
		return MissileID::ChargedBolt;
	default:
		return MissileID::Firebolt;
	}
}

void OnMinionGemHit(const Player &owner, Monster &minion, Monster &target, Fixed26_6 damage)
{
	const MinionGemData *gem = MinionGemOf(minion);
	if (!gbIsPoe2 || gem == nullptr || damage <= 0)
		return;
	switch (gem->skill) {
	case PersistentSkill::SkeletalReavers:
		minion.poe2.minionRage = static_cast<uint8_t>(std::min(minion.poe2.minionRage + 1, ReaverMaxRage));
		minion.poe2.minionRageTicks = ReaverRageTicks;
		if (minion.poe2.minionRage == ReaverMaxRage)
			vfx::Sparks(target.position.tile, vfx::Element::Blood, 0.6F);
		break;
	case PersistentSkill::SkeletalBrutes:
		if (!target.hasNoLife()) {
			TryInflictAilment(target, owner, DamageType::Physical, Fixed26_6::fromRaw(damage.raw() * BruteStunPercent / 100));
			vfx::Impact(target.position.tile, vfx::Element::Physical, 1.2F);
		}
		break;
	default:
		break;
	}
}

void OnMinionGemDied(const Player &owner, const Monster &minion)
{
	// An illusion that fades on its own leaves nothing; one an enemy breaks before its time leaves Frenzy remnants.
	if (!gbIsPoe2 || &owner != MyPlayer || !minion.poe2.decoy || minion.poe2.minionLifeTicks == 0 || leveltype == DTYPE_TOWN)
		return;
	DropFrenzyRemnant(minion.position.tile);
	if (GenerateRnd(100) < IllusionSecondRemnantChance(GemLevel(owner, PersistentSkill::LingeringIllusion))) {
		if (const std::optional<Point> beside = FindClosestValidPosition([](Point tile) { return InDungeonBounds(tile) && IsTileWalkable(tile); }, minion.position.tile, 1, 2); beside)
			DropFrenzyRemnant(*beside);
	}
	TelemetryLog("spirit", "an enemy broke a Lingering Illusion");
}

void OnRagingSpiritsCast(Player &player, SkillSlot skill, uint32_t tags)
{
	if (!gbIsPoe2 || &player != MyPlayer || (tags & ST_Fire) == 0 || (tags & ST_Spell) == 0 || !On(player, PersistentSkill::RagingSpirits))
		return;
	SpiritRuntime &state = player.poe2.spiritState;
	state.ragingSkill = skill;
	state.ragingBudget = RagingSpiritsPerCast;
	state.ragingTicks = RagingSpiritsCastTicks;
}

void OnRagingSpiritsHit(Player &player, SkillSlot skill, const Monster &monster, DamageType type)
{
	SpiritRuntime &state = player.poe2.spiritState;
	if (type != DamageType::Fire || state.ragingBudget == 0 || skill.isEmpty() || !(skill == state.ragingSkill) || monster.isPlayerMinion())
		return;
	state.ragingBudget--;
	// Called next tick, not in the middle of the hit.
	PendingSpirits.push_back(monster.position.tile);
}

void OnIllusionDodge(Player &player, Point start)
{
	if (!On(player, PersistentSkill::LingeringIllusion) || &player != MyPlayer || leveltype == DTYPE_TOWN)
		return;
	PendingIllusion = start;
	PendingIllusionTicks = IllusionDelayTicks;
}

Monster *SacrificeMinionNear(const Player &player, Point center, int radius)
{
	if (!On(player, PersistentSkill::Sacrifice))
		return nullptr;
	Monster *best = nullptr;
	int bestDistance = radius + 1;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (!Alive(monster) || !IsGemMinionOf(monster, player))
			continue;
		// Only the undead that rise again: the skeletons.
		const MinionGemData *gem = MinionGemOf(monster);
		if (gem == nullptr || !IsMinionCountSkill(gem->skill))
			continue;
		const int distance = center.WalkingDistance(monster.position.tile);
		if (distance < bestDistance) {
			best = &monster;
			bestDistance = distance;
		}
	}
	return best;
}

void SacrificeMinion(Monster &minion)
{
	AddVisualEffect(minion.position.tile, MissileGraphicID::BoneHit);
	vfx::Impact(minion.position.tile, vfx::Element::Physical, 1.0F);
	TelemetryLog("spirit", "Sacrifice used a skeleton as a corpse");
	KillGolem(minion);
}

bool ClericStands(const Player &player)
{
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &monster = Monsters[ActiveMonsters[i]];
		if (!IsGemMinionOf(monster, player))
			continue;
		if (const MinionGemData *gem = MinionGemOf(monster); gem != nullptr && gem->skill == PersistentSkill::SkeletalClerics)
			return true;
	}
	return false;
}

void KillGemMinions(const Player &player)
{
	// Illusions that go with the hero weren't broken by an enemy.
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (monster.poe2.decoy && IsMinionOf(monster, player))
			monster.poe2.minionLifeTicks = 0;
	}
	for (const MinionGemData &gem : MinionGems)
		KillSkeletonMinions(player, gem.type);
}

void ProcessMinionGems(Player &player)
{
	if (!gbIsPoe2 || &player != MyPlayer)
		return;
	SpiritRuntime &spirit = player.poe2.spiritState;
	if (spirit.ragingTicks > 0 && --spirit.ragingTicks == 0)
		spirit.ragingBudget = 0;
	CallRagingSpirits(player);
	PlaceIllusion(player);
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &minion = Monsters[ActiveMonsters[i]];
		if (!IsGemMinionOf(minion, player) || !Alive(minion))
			continue;
		Poe2MonsterState &state = minion.poe2;
		// A raging spirit burns out, an illusion fades.
		if (state.minionLifeTicks > 0 && --state.minionLifeTicks == 0) {
			KillGolem(minion);
			continue;
		}
		if (state.minionAttackWait > 0)
			state.minionAttackWait--;
		if (state.minionRage > 0) {
			if (state.minionRageTicks > 0) {
				state.minionRageTicks--;
			} else {
				state.minionRage--;
				state.minionRageTicks = ReaverRageFadeTicks;
			}
		}
		const MinionGemData *gem = MinionGemOf(minion);
		if (gem == nullptr || gem->skill != PersistentSkill::SkeletalClerics)
			continue;
		if (state.clericTicks > 0)
			state.clericTicks--;
		else
			ClericHeal(player, minion);
	}
	TickSwarms(player);
	if (leveltype == DTYPE_TOWN)
		FrenzyRemnants.clear();
	TickFrenzyRemnants(player);
}

void ClearMinionGems()
{
	FrenzyRemnants.clear();
	PendingSpirits.clear();
	PendingIllusion = std::nullopt;
}

int SwarmCount(const Player &player)
{
	const auto &swarms = player.poe2.spiritState.swarms;
	return static_cast<int>(std::count_if(swarms.begin(), swarms.end(), [](const RavenousSwarm &swarm) { return swarm.ticks > 0; }));
}

size_t FrenzyRemnantCount()
{
	return FrenzyRemnants.size();
}

} // namespace devilution
