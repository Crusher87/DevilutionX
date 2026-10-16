#include "poe2/regen.hpp"

#include <algorithm>
#include <cstdint>

#include "engine/backbuffer_state.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "player.h"
#include "poe2/flasks.hpp"
#include "poe2/unique_effects.hpp"

namespace devilution {

namespace {

constexpr int TicksPerSecond = 20;

/** @brief Adds raw points to a current/base pair without going over the maximum. */
void Restore(Fixed26_6 &current, Fixed26_6 &base, Fixed26_6 max, int32_t raw)
{
	const int32_t gain = std::min(raw, max.raw() - current.raw());
	if (gain <= 0)
		return;
	current += Fixed26_6::fromRaw(gain);
	base += Fixed26_6::fromRaw(gain);
}

} // namespace

void ApplyRegen(Player &player)
{
	if (!gbIsPoe2 || player.hasNoLife())
		return;

	// POE2 Mashup: uniques can lessen life recovery or lock it for a moment.
	const int lifeRecovery = LifeRecoveryPercent(player, false);
	// The passive tree regenerates a share of maximum life (per mille per second) on top of the flat regeneration.
	const int64_t lifePerSecond = static_cast<int64_t>(player.poe2.lifeRegen) * 64 + static_cast<int64_t>(std::max<int32_t>(player._pMaxHP.raw(), 0)) * player.poe2.lifeRegenPerMille / 1000;
	// With life recovery going to energy shield (the Quiet Shrine), regeneration keeps working at full life.
	const bool feedsShield = player.poe2.uniqueRules[static_cast<size_t>(UniqueRule::LifeRecoveryToShield)] > 0 && player.poe2.energyShield < player.poe2.maxEnergyShield * 64;
	if (lifePerSecond > 0 && (player._pHitPoints < player._pMaxHP || feedsShield) && lifeRecovery > 0) {
		const int64_t total = lifePerSecond * lifeRecovery / 100 + player.poe2.lifeRegenRemainder;
		player.poe2.lifeRegenRemainder = static_cast<int>(total % TicksPerSecond);
		Restore(player._pHitPoints, player._pHPBase, player._pMaxHP, static_cast<int32_t>(total / TicksPerSecond));
		OnLifeRecovered(player, static_cast<int32_t>(total / TicksPerSecond));
		if (&player == MyPlayer)
			RedrawComponent(PanelDrawComponent::Health);
	} else {
		player.poe2.lifeRegenRemainder = 0;
	}

	const int manaMultiplier = std::max(100 + player.poe2.manaRegenBonus, 0) * ManaRecoveryPercent(player) / 100;
	if (!HasAnyOf(player._pIFlags, ItemSpecialEffect::NoMana) && player._pMana < player._pMaxMana && player._pMaxMana.raw() > 0 && !IsRecoveryLocked(player)) {
		constexpr int64_t Divisor = 100 * 100 * TicksPerSecond;
		const int64_t total = static_cast<int64_t>(player._pMaxMana.raw()) * BaseManaRegenPercent * manaMultiplier + player.poe2.manaRegenRemainder;
		player.poe2.manaRegenRemainder = static_cast<int>(total % Divisor);
		Restore(player._pMana, player._pManaBase, player._pMaxMana, static_cast<int32_t>(total / Divisor));
		if (&player == MyPlayer)
			RedrawComponent(PanelDrawComponent::Mana);
	} else {
		player.poe2.manaRegenRemainder = 0;
	}

	ProcessFlaskRecovery(player);
}

} // namespace devilution
