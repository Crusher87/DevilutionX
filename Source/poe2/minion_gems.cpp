#include "poe2/minion_gems.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

#include "game_mode.hpp"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/monster_mods.hpp"
#include "poe2/profane_roads.hpp"
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

/**
 * Life and damage follow POE2's minions against its Skeletal Warrior. Each kind wears its own colours: the Warriors
 * and Arsonists keep theirs, the archers are the other bowmen, and the Reavers, Brutes and Clerics are captains.
 */
constexpr std::array<MinionGemData, MinionCountSkills.size()> MinionGems { {
	{ PersistentSkill::SkeletalWarriors, MT_WSKELAX, 100, 100, MinionShot::None, 0, 0, 0, WarriorSpirit, 2, 2 },
	{ PersistentSkill::SkeletalMages, MT_RSKELBW, 60, 100, MinionShot::Firebolt, 150, 7, 0, ArsonistSpirit, 0, 1 },
	{ PersistentSkill::SkeletalSnipers, MT_WSKELBW, 60, 100, MinionShot::Arrow, 160, 10, 24, SniperSpirit, 0, 1 },
	{ PersistentSkill::SkeletalFrostMages, MT_XSKELBW, 60, 90, MinionShot::FrostBolt, 130, 7, 0, FrostMageSpirit, 0, 1 },
	{ PersistentSkill::SkeletalStormMages, MT_TSKELBW, 60, 90, MinionShot::ChargedBolts, 70, 6, 0, StormMageSpirit, 0, 1 },
	{ PersistentSkill::SkeletalReavers, MT_RSKELSD, 80, 70, MinionShot::None, 0, 0, 0, SniperSpirit, 0, 1 },
	{ PersistentSkill::SkeletalBrutes, MT_XSKELSD, 140, 280, MinionShot::None, 0, 0, 24, BruteSpirit, 0, 1 },
	{ PersistentSkill::SkeletalClerics, MT_WSKELSD, 60, 0, MinionShot::None, 0, 0, 0, WarriorSpirit, 0, 1 },
} };

constexpr bool SameOrder()
{
	for (size_t i = 0; i < MinionGems.size(); i++) {
		if (MinionGems[i].skill != MinionCountSkills[i])
			return false;
	}
	return true;
}
static_assert(SameOrder(), "MinionGems follows MinionCountSkills");

int AtLevel(int gemLevel, int at1, int at20)
{
	const int level = std::clamp(gemLevel, 1, 30);
	return at1 + ((at20 - at1) * (level - 1) / 19);
}

bool IsGemMinionOf(const Monster &monster, const Player &player)
{
	return IsMinionOf(monster, player) && monster.position.tile != GolemHoldingCell;
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
	for (const MinionGemData &gem : MinionGems)
		KillSkeletonMinions(player, gem.type);
}

void ProcessMinionGems(Player &player)
{
	if (!gbIsPoe2 || &player != MyPlayer)
		return;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &minion = Monsters[ActiveMonsters[i]];
		if (!IsGemMinionOf(minion, player))
			continue;
		Poe2MonsterState &state = minion.poe2;
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
}

} // namespace devilution
