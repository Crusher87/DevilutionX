#include "poe2/skill_resources.hpp"

#include <algorithm>

#include "missiles.h"
#include "monster.h"
#include "objects.h"
#include "player.h"
#include "poe2/monster_armour.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/spirit_auras.hpp"
#include "poe2/spirit_skills.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/support_table.hpp"
#include "poe2/support_tiers.hpp"
#include "poe2/table_skills.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"

namespace devilution {

int ResourceAttackDamagePercent(const Player &player)
{
	return (100 + player.poe2.rage + (player.poe2.frenzyCharges * FrenzyChargeDamagePercent) + SoulHarvestDamagePercent(player)) * SpiritAttackDamagePercent(player) / 100;
}

void GainFrenzyCharge(Player &player)
{
	Poe2PlayerData &data = player.poe2;
	if (data.frenzyCharges < MaxFrenzyCharges) {
		data.frenzyCharges++;
		AddVisualEffect(player.position.tile, MissileGraphicID::BloodStarRedExplosion);
	}
	data.frenzyTicks = FrenzyChargeTicks;
}

int AddCombo(Player &player)
{
	Poe2PlayerData &data = player.poe2;
	data.combo = static_cast<uint8_t>(std::min<int>(data.combo + 1, MaxCombo));
	data.comboTicks = ComboTicks;
	return data.combo;
}

bool FistsOfThunderNextHitBursts(const Player &player)
{
	// As DoAttack bursts: when the Combo the hit brings it to is a multiple of FistsOfThunderBurstEvery.
	return std::min<int>(player.poe2.combo + 1, MaxCombo) % FistsOfThunderBurstEvery == 0;
}

void AddRage(Player &player, int rage)
{
	Poe2PlayerData &data = player.poe2;
	data.rage = static_cast<uint8_t>(std::clamp<int>(data.rage + rage, 0, MaxRage));
	data.rageTicks = RageHoldTicks;
}

void OnSkillMeleeHit(Player &player)
{
	// The default attack, a punch as much as a weapon's, takes Battle Rage like a skill does (Curtis 2026-10-22).
	const SkillSlot skill = CurrentAttackSkill(player);
	if (!skill.isEmpty() && player.poe2.hasSupport(skill, SupportGem::BattleRage))
		AddRage(player, SupportValue(player.poe2, skill, SupportGem::BattleRage, 0));
	OnSpiritMeleeHit(player);
}

void CastAmplifyDamage(const Player &player, Point target, int gemLevel)
{
	const int radius = 2 + (std::max(gemLevel, 1) / 5);
	const int percent = AmplifyDamagePercent + (AmplifyDamagePercentPerLevel * (std::max(gemLevel, 1) - 1));
	AddVisualEffect(target, MissileGraphicID::BloodStarExplosion);
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (monster.hasNoLife() || monster.isPlayerMinion() || !monster.isPossibleToHit() || target.ExactDistance(monster.position.tile) > radius)
			continue;
		monster.poe2.amplifyTicks = AmplifyDamageTicks;
		monster.poe2.amplifyPercent = static_cast<uint8_t>(std::max<int>(percent, monster.poe2.amplifyPercent));
		monster.tag(player);
		AddVisualEffect(monster.position.tile, MissileGraphicID::BloodHit);
	}
}

void ApplyAuraAmplifyDamage(const Player &player, Monster &monster, int gemLevel, int strengthPercent, int ticks)
{
	const int percent = (AmplifyDamagePercent + (AmplifyDamagePercentPerLevel * (std::max(gemLevel, 1) - 1))) * strengthPercent / 100;
	Poe2MonsterState &state = monster.poe2;
	// A curse cast by hand outlasts the aura's, and keeps its strength while it does.
	if (state.amplifyTicks > ticks)
		return;
	if (state.amplifyTicks == 0)
		AddVisualEffect(monster.position.tile, MissileGraphicID::BloodHit);
	state.amplifyTicks = static_cast<uint8_t>(std::clamp(ticks, 1, 255));
	state.amplifyPercent = static_cast<uint8_t>(std::clamp(percent, 0, 255));
	monster.tag(player);
}

void BreakMonsterArmour(Monster &monster, const Player *breaker)
{
	if (monster.poe2.armourBreakTicks == 0) {
		AddVisualEffect(monster.position.tile, MissileGraphicID::StoneCurseShatter);
		if (breaker != nullptr)
			OnSpiritArmourBroken(*breaker, monster);
	}
	monster.poe2.armourBreakTicks = ArmourBreakTicks;
}

void HeavyStunMonster(Monster &monster)
{
	if (monster.hasNoLife() || monster.isUnique())
		return;
	monster.poe2.heavyStunTicks = HeavyStunTicks;
	monster.poe2.stunBuildup = 0;
}

void SkillBurst(const Player &player, Point center, int radius, DamageType type, Fixed26_6 damage, MissileGraphicID effect)
{
	AddVisualEffect(center, effect);
	if (&player != MyPlayer || damage <= 0)
		return;
	BreakObjectsInArea(player, center, radius);
	const WeaponSkill skill = CurrentWeaponSkill(player);
	const SupportHitScope scope(player, skill != WeaponSkill::None ? SkillSlot::ForAttack(skill) : SkillSlot {});
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (monster.hasNoLife() || monster.isPlayerMinion() || !monster.isPossibleToHit() || monster.mode == MonsterMode::Death)
			continue;
		if (center.ExactDistance(monster.position.tile) > radius)
			continue;
		const Fixed26_6 dealt = Fixed26_6::fromRaw(static_cast<int32_t>(MonsterHitDamage(player, monster, type, damage.raw())));
		ApplyMonsterDamage(type, monster, dealt);
		monster.tag(player);
		TryInflictAilment(monster, player, type, dealt);
		if (monster.hasNoLife())
			M_StartKill(monster, player);
	}
}

} // namespace devilution
