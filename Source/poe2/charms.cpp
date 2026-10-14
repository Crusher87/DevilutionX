#include "poe2/charms.hpp"

#include <algorithm>

#include "engine/backbuffer_state.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "poe2/monster_mods.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"
#include "tables/misdat.h"
#include "utils/language.h"

namespace devilution {

namespace {

/**
 * @brief Spends a use of the first charged charm with this effect in the belt.
 * @param startCooldown Whether the life, mana and Guard charms wait a second afterwards; the charms that answer a hit don't hold them up.
 */
bool UseCharm(Player &player, CharmEffect effect, bool startCooldown = true)
{
	const int cost = CharmChargesPerUseFor(effect);
	for (Item &item : player.SpdList) {
		if (GetCharmEffect(item) != effect || item._iCharges < cost)
			continue;
		item._iCharges -= cost;
		if (startCooldown)
			player.poe2.charmCooldown = CharmCooldownTicks;
		TelemetryCharmUsed(player, item._iIName);
		return true;
	}
	return false;
}

int ResistCharmIndex(DamageType damageType)
{
	switch (damageType) {
	case DamageType::Fire:
		return 0;
	case DamageType::Lightning:
		return 1;
	case DamageType::Magic:
	case DamageType::Acid:
		return 2;
	default:
		return -1;
	}
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
	case IMISC_CHARM_FIRE:
		return CharmEffect::Fire;
	case IMISC_CHARM_LIGHTNING:
		return CharmEffect::Lightning;
	case IMISC_CHARM_MAGIC:
		return CharmEffect::Magic;
	case IMISC_CHARM_STUN:
		return CharmEffect::Stun;
	case IMISC_CHARM_GOLDEN:
		return CharmEffect::Golden;
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
	case CharmEffect::Fire:
		return _("A fire hit lands: +25% fire resistance for 4 seconds");
	case CharmEffect::Lightning:
		return _("A lightning hit lands: +25% lightning resistance for 4 seconds");
	case CharmEffect::Magic:
		return _("A magic hit lands: +25% magic resistance for 4 seconds");
	case CharmEffect::Stun:
		return _("You're stunned: break free and can't be stunned for 3 seconds");
	case CharmEffect::Golden:
		return _("You kill a rare or unique monster: it drops an extra item");
	default:
		return {};
	}
}

int CharmChargesPerUseFor(CharmEffect effect)
{
	return effect == CharmEffect::Golden ? MaxCharmCharges : CharmChargesPerUse;
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
	if (data.stunImmuneTicks > 0)
		data.stunImmuneTicks--;
	for (uint8_t &ticks : data.resistCharmTicks) {
		if (ticks > 0)
			ticks--;
	}
	if (data.charmCooldown > 0) {
		data.charmCooldown--;
		return;
	}
	if (&player != MyPlayer)
		return;

	if (player._pMaxHP > 0 && player._pHitPoints.raw() < PercentOf(player._pMaxHP, CharmLifeThreshold) && UseCharm(player, CharmEffect::Life)) {
		const Fixed26_6 heal = Fixed26_6::fromRaw(PercentOf(player._pMaxHP, CharmLifeRecovery) * LifeRecoveryPercent(player, false) / 100);
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

int ResistanceAgainstHit(Player &player, DamageType damageType, int resistance)
{
	const int index = ResistCharmIndex(damageType);
	if (!gbIsPoe2 || index < 0)
		return resistance;
	uint8_t &ticks = player.poe2.resistCharmTicks[index];
	static constexpr CharmEffect Effects[] = { CharmEffect::Fire, CharmEffect::Lightning, CharmEffect::Magic };
	static constexpr MissileGraphicID Visuals[] = { MissileGraphicID::OrangeFlareExplosion, MissileGraphicID::YellowFlareExplosion, MissileGraphicID::BlueFlareExplosion };
	if (ticks == 0 && &player == MyPlayer && UseCharm(player, Effects[index], false)) {
		ticks = ResistCharmTicks;
		AddVisualEffect(player.position.tile, Visuals[index]);
	}
	if (ticks == 0)
		return resistance;
	return std::min(resistance + ResistCharmBonus, MaxResistance);
}

bool CharmPreventsStun(Player &player)
{
	if (!gbIsPoe2)
		return false;
	if (player.poe2.stunImmuneTicks > 0)
		return true;
	if (&player != MyPlayer || !UseCharm(player, CharmEffect::Stun, false))
		return false;
	player.poe2.stunImmuneTicks = StunImmuneTicks;
	AddVisualEffect(player.position.tile, MissileGraphicID::StoneCurseShatter);
	return true;
}

int GoldenCharmExtraDrops(Player &player, const Monster &monster)
{
	if (!gbIsPoe2)
		return 0;
	if (!monster.isUnique() && GetMonsterAffixes(monster).rarity != MonsterRarity::Rare)
		return 0;
	return UseCharm(player, CharmEffect::Golden, false) ? 1 : 0;
}

} // namespace devilution
