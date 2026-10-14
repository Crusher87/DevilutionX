#include "poe2/skill_resources.hpp"

#include <algorithm>

#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/monster_mods.hpp"
#include "poe2/support_gems.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"

namespace devilution {

int ResourceAttackDamagePercent(const Player &player)
{
	return 100 + player.poe2.rage + (player.poe2.frenzyCharges * FrenzyChargeDamagePercent);
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

void AddRage(Player &player, int rage)
{
	Poe2PlayerData &data = player.poe2;
	data.rage = static_cast<uint8_t>(std::clamp<int>(data.rage + rage, 0, MaxRage));
	data.rageTicks = RageHoldTicks;
}

void OnSkillMeleeHit(Player &player)
{
	const WeaponSkill skill = CurrentWeaponSkill(player);
	if (skill != WeaponSkill::None && player.poe2.hasSupport(SkillSlot::ForAttack(skill), SupportGem::BattleRage))
		AddRage(player, 1);
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

void BreakMonsterArmour(Monster &monster)
{
	if (monster.poe2.armourBreakTicks == 0)
		AddVisualEffect(monster.position.tile, MissileGraphicID::StoneCurseShatter);
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
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (monster.hasNoLife() || monster.isPlayerMinion() || !monster.isPossibleToHit() || monster.mode == MonsterMode::Death)
			continue;
		if (center.ExactDistance(monster.position.tile) > radius)
			continue;
		const Fixed26_6 dealt = Fixed26_6::fromRaw(damage.raw() * MonsterDamageTakenPercent(monster, type) / 100);
		ApplyMonsterDamage(type, monster, dealt);
		monster.tag(player);
		TryInflictAilment(monster, player, type, dealt);
		if (monster.hasNoLife())
			M_StartKill(monster, player);
	}
}

} // namespace devilution
