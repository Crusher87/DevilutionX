#include "poe2/charms.hpp"

#include <algorithm>

#include "engine/backbuffer_state.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "missiles.h"
#include "player.h"
#include "utils/language.h"

namespace devilution {

namespace {

/** @brief Spends a use of the first charged charm with this effect in the belt. */
bool UseCharm(Player &player, CharmEffect effect)
{
	for (Item &item : player.SpdList) {
		if (GetCharmEffect(item) != effect || item._iCharges < CharmChargesPerUse)
			continue;
		item._iCharges -= CharmChargesPerUse;
		player.poe2.charmCooldown = CharmCooldownTicks;
		return true;
	}
	return false;
}

int PercentOf(Fixed26_6 value, int percent)
{
	return static_cast<int>(static_cast<int64_t>(value.raw()) * percent / 100);
}

} // namespace

CharmEffect GetCharmEffect(const Item &item)
{
	if (item.isEmpty())
		return CharmEffect::None;
	switch (item._iMiscId) {
	case IMISC_CHARM_LIFE:
		return CharmEffect::Life;
	case IMISC_CHARM_MANA:
		return CharmEffect::Mana;
	case IMISC_CHARM_GUARD:
		return CharmEffect::Guard;
	default:
		return CharmEffect::None;
	}
}

bool IsCharm(const Item &item)
{
	return GetCharmEffect(item) != CharmEffect::None;
}

std::string_view CharmDescription(CharmEffect effect)
{
	switch (effect) {
	case CharmEffect::Life:
		return _("Below 35% life: recover 30% of your life");
	case CharmEffect::Mana:
		return _("Below 20% mana: recover 40% of your mana");
	case CharmEffect::Guard:
		return _("Hit for 15% of your life: take 40% less damage for 4 seconds");
	default:
		return {};
	}
}

void GainCharmCharges(Player &player, int charges)
{
	for (Item &item : player.SpdList) {
		if (IsCharm(item))
			item._iCharges = std::clamp(item._iCharges + charges, 0, MaxCharmCharges);
	}
}

void RefillCharms(Player &player)
{
	GainCharmCharges(player, MaxCharmCharges);
}

void ProcessCharms(Player &player)
{
	if (!gbIsPoe2 || player.hasNoLife())
		return;
	Poe2PlayerData &data = player.poe2;
	if (data.guardTicks > 0)
		data.guardTicks--;
	if (data.charmCooldown > 0) {
		data.charmCooldown--;
		return;
	}
	if (&player != MyPlayer)
		return;

	if (player._pMaxHP > 0 && player._pHitPoints.raw() < PercentOf(player._pMaxHP, CharmLifeThreshold) && UseCharm(player, CharmEffect::Life)) {
		const Fixed26_6 heal = Fixed26_6::fromRaw(PercentOf(player._pMaxHP, CharmLifeRecovery));
		player._pHitPoints = std::min(player._pHitPoints + heal, player._pMaxHP);
		player._pHPBase = std::min(player._pHPBase + heal, player._pMaxHPBase);
		AddVisualEffect(player.position.tile, MissileGraphicID::RedFlareExplosion);
		RedrawComponent(PanelDrawComponent::Health);
		return;
	}
	if (player._pMaxMana > 0 && player._pMana.raw() < PercentOf(player._pMaxMana, CharmManaThreshold) && UseCharm(player, CharmEffect::Mana)) {
		const Fixed26_6 mana = Fixed26_6::fromRaw(PercentOf(player._pMaxMana, CharmManaRecovery));
		player._pMana = std::min(player._pMana + mana, player._pMaxMana);
		player._pManaBase = std::min(player._pManaBase + mana, player._pMaxManaBase);
		AddVisualEffect(player.position.tile, MissileGraphicID::BlueFlareExplosion);
		RedrawComponent(PanelDrawComponent::Mana);
	}
}

Fixed26_6 ApplyGuard(Player &player, Fixed26_6 damage)
{
	if (!gbIsPoe2 || damage <= 0)
		return damage;
	if (player.poe2.guardTicks > 0)
		damage = Fixed26_6::fromRaw(damage.raw() - PercentOf(damage, GuardDamageReduction));
	if (player.poe2.guardTicks == 0 && player.poe2.charmCooldown == 0 && &player == MyPlayer
	    && damage.raw() >= PercentOf(player._pMaxHP, GuardHitThreshold) && UseCharm(player, CharmEffect::Guard)) {
		player.poe2.guardTicks = GuardTicks;
		AddVisualEffect(player.position.tile, MissileGraphicID::ManaShield);
	}
	return damage;
}

} // namespace devilution
