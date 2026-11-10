#include "poe2/effect_primitives.hpp"

#include <algorithm>

#include "engine/backbuffer_state.hpp"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "monster.h"
#include "player.h"
#include "poe2/item_stats.hpp"
#include "poe2/map_play.hpp"
#include "poe2/shrines.hpp"
#include "poe2/support_table.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"
#include "poe2/unique_rules.hpp"

namespace devilution {

namespace {

constexpr int RecoupTicks = RecoupSeconds * 20;

int Gear(const Player &player, Poe2ItemStat stat)
{
	return player.poe2.itemStats[static_cast<size_t>(stat)];
}

int32_t PercentOf(int64_t raw, int percent)
{
	return static_cast<int32_t>(std::clamp<int64_t>(raw * percent / 100, 0, INT32_MAX / 2));
}

void GainLife(Player &player, int32_t raw)
{
	if (raw <= 0 || player.hasNoLife())
		return;
	player._pHitPoints = std::min(player._pHitPoints + Fixed26_6::fromRaw(raw), player._pMaxHP);
	player._pHPBase = std::min(player._pHPBase + Fixed26_6::fromRaw(raw), player._pMaxHPBase);
	OnLifeRecovered(player, raw);
	if (&player == MyPlayer)
		RedrawComponent(PanelDrawComponent::Health);
}

void GainMana(Player &player, int32_t raw)
{
	if (raw <= 0 || HasAnyOf(player._pIFlags, ItemSpecialEffect::NoMana))
		return;
	player._pMana = std::min(player._pMana + Fixed26_6::fromRaw(raw), player._pMaxMana);
	player._pManaBase = std::min(player._pManaBase + Fixed26_6::fromRaw(raw), player._pMaxManaBase);
	if (&player == MyPlayer)
		RedrawComponent(PanelDrawComponent::Mana);
}

} // namespace

int GainAsExtraPercent(const Player &player, DamageType type)
{
	if (!gbIsPoe2)
		return 0;
	const ShrineBonuses shrines = GetShrineBonuses(player);
	switch (type) {
	case DamageType::Fire:
		return shrines.extraFirePercent + Gear(player, Poe2ItemStat::FireAsExtra);
	case DamageType::Lightning:
		return shrines.extraLightningPercent + Gear(player, Poe2ItemStat::LightningAsExtra);
	case DamageType::Cold:
		return shrines.extraColdPercent + Gear(player, Poe2ItemStat::ColdAsExtra);
	default:
		return 0;
	}
}

bool AvoidsAilments(const Player &player)
{
	return gbIsPoe2 && GetShrineBonuses(player).avoidAilments;
}

int GearLeechPercent(const Player &player, bool mana)
{
	if (!gbIsPoe2)
		return 0;
	return std::clamp(Gear(player, mana ? Poe2ItemStat::ManaLeech : Poe2ItemStat::LifeLeech), 0, MaxLeechPercent);
}

void LeechFromHit(Player &player, SkillSlot skill, int32_t damageRaw)
{
	if (!gbIsPoe2 || damageRaw <= 0 || player.hasNoLife() || (SkillTags(skill) & ST_Attack) == 0)
		return;
	const int map = MapLeechPercent();
	if (const int life = GearLeechPercent(player, false); life > 0)
		GainLife(player, PercentOf(static_cast<int64_t>(damageRaw) * map / 100, life * LifeRecoveryPercent(player, false) / 100));
	if (const int mana = GearLeechPercent(player, true); mana > 0)
		GainMana(player, PercentOf(static_cast<int64_t>(damageRaw) * map / 100, mana * ManaRecoveryPercent(player) / 100));
}

std::pair<int, int> ThornsDamage(const Player &player)
{
	if (!gbIsPoe2)
		return { 0, 0 };
	const int min = std::max(Gear(player, Poe2ItemStat::ThornsMin), 0);
	return { min, std::max(Gear(player, Poe2ItemStat::ThornsMax), min) };
}

void DealThorns(Player &player, Monster &monster)
{
	if (&player != MyPlayer || monster.hasNoLife() || monster.mode == MonsterMode::Death)
		return;
	const auto [min, max] = ThornsDamage(player);
	if (max <= 0)
		return;
	const Fixed26_6 damage = Fixed26_6::fromInt(RandomIntBetween(min, max));
	if (damage <= 0)
		return;
	TelemetryNoteDamageSource("thorns");
	ApplyMonsterDamage(DamageType::Physical, monster, damage);
	monster.tag(player);
	if (monster.hasNoLife())
		M_StartKill(monster, player);
}

int PercentOnKill(const Player &player, bool mana)
{
	if (!gbIsPoe2)
		return 0;
	return std::max(Gear(player, mana ? Poe2ItemStat::ManaPercentOnKill : Poe2ItemStat::LifePercentOnKill), 0);
}

std::pair<int, int> AddedColdDamage(const Player &player)
{
	if (!gbIsPoe2)
		return { 0, 0 };
	const int min = std::max(Gear(player, Poe2ItemStat::AddedColdMin), 0);
	return { min, std::max(Gear(player, Poe2ItemStat::AddedColdMax), min) };
}

int ArmourAppliesToElementPercent(const Player &player, DamageType type)
{
	if (!gbIsPoe2)
		return 0;
	int gear = 0;
	switch (type) {
	case DamageType::Fire:
		gear = Gear(player, Poe2ItemStat::ArmourToFire);
		break;
	case DamageType::Lightning:
		gear = Gear(player, Poe2ItemStat::ArmourToLightning);
		break;
	case DamageType::Cold:
		gear = Gear(player, Poe2ItemStat::ArmourToCold);
		break;
	default:
		return 0;
	}
	return std::clamp(player.poe2.rule(UniqueRule::ArmourVsElements) + gear, 0, 100);
}

int32_t StunThresholdBonusRaw(const Player &player)
{
	if (!gbIsPoe2)
		return 0;
	const int64_t flat = static_cast<int64_t>(std::max(Gear(player, Poe2ItemStat::StunThreshold), 0)) * 64;
	const int64_t shield = static_cast<int64_t>(std::max(player.poe2.maxEnergyShield, 0)) * 64 * std::max(Gear(player, Poe2ItemStat::EnergyShieldAsStunThreshold), 0) / 100;
	return static_cast<int32_t>(std::min<int64_t>(flat + shield, INT32_MAX / 2));
}

int64_t StunDamageAgainstLife(const Player &player, int32_t damageRaw, int thresholdPercent)
{
	int64_t stunDamage = static_cast<int64_t>(damageRaw) * 100 / std::max(thresholdPercent, 1);
	const int64_t life = std::max<int32_t>(player._pMaxHP.raw(), 1);
	if (const int32_t bonus = StunThresholdBonusRaw(player); bonus > 0)
		stunDamage = stunDamage * life / (life + bonus);
	return stunDamage;
}

int LifeRecoupPercent(const Player &player)
{
	if (!gbIsPoe2)
		return 0;
	return std::clamp(Gear(player, Poe2ItemStat::LifeRecoup), 0, 100);
}

void NoteRecoup(Player &player, int64_t damageTakenRaw)
{
	const int percent = LifeRecoupPercent(player);
	if (percent <= 0 || damageTakenRaw <= 0)
		return;
	UniqueRuleState &state = player.poe2.uniqueState;
	const int32_t recouped = PercentOf(damageTakenRaw, percent * LifeRecoveryPercent(player, false) / 100);
	if (recouped <= 0)
		return;
	state.recoupPool = static_cast<int32_t>(std::min<int64_t>(static_cast<int64_t>(state.recoupPool) + recouped, INT32_MAX / 2));
	state.recoupTicks = RecoupTicks;
}

void ProcessRecoup(Player &player)
{
	UniqueRuleState &state = player.poe2.uniqueState;
	if (state.recoupTicks == 0)
		return;
	if (player.hasNoLife()) {
		state.recoupPool = 0;
		state.recoupTicks = 0;
		return;
	}
	// What is left comes back evenly over the ticks left, so the last tick pays the rest.
	const int32_t heal = state.recoupPool / state.recoupTicks;
	state.recoupPool -= heal;
	state.recoupTicks--;
	GainLife(player, heal);
	if (state.recoupTicks == 0)
		state.recoupPool = 0;
}

} // namespace devilution
