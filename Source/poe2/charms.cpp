#include "poe2/charms.hpp"

#include <algorithm>
#include <optional>

#include "effects.h"
#include "engine/backbuffer_state.hpp"
#include "levels/tile_properties.hpp"
#include "msg.h"
#include "game_mode.hpp"
#include "items.h"
#include "missiles.h"
#include "monster.h"
#include "player.h"
#include "plrmsg.h"
#include "poe2/belt_uniques.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"
#include "tables/misdat.h"
#include "utils/language.h"

namespace devilution {

namespace {

/** @brief What activating a charm does for the unique charms. */
void OnCharmActivated(Player &player, Item &item, bool startCooldown)
{
	if (startCooldown)
		player.poe2.charmCooldown = CharmCooldownTicks;
	if (IsBeltUnique(item, BeltUniqueId::Witchglass))
		player.poe2.uniqueState.witchglassTicks = WitchglassTicks;
	TelemetryCharmUsed(player, item._iIName);
}

/**
 * @brief Spends a use of the first charged charm with this effect in the belt.
 * @param startCooldown Whether the life, mana and Guard charms wait a second afterwards; the charms that answer a hit don't hold them up.
 * @return The charm used, or nullptr.
 */
Item *UseCharm(Player &player, CharmEffect effect, bool startCooldown = true)
{
	UniqueRuleState &state = player.poe2.uniqueState;
	for (Item &item : player.SpdList) {
		if (GetCharmEffect(item) != effect || item._iCharges < CharmChargesPerUseFor(item))
			continue;
		// The Second Bell: every third activation is free.
		const bool free = IsBeltUnique(item, BeltUniqueId::SecondBell) && ++state.bellActivations % 3 == 0;
		if (!free)
			item._iCharges -= CharmChargesPerUseFor(item);
		OnCharmActivated(player, item, startCooldown);
		return &item;
	}
	// Saint's Token activates while empty, then rests.
	for (Item &item : player.SpdList) {
		if (GetCharmEffect(item) != effect || !IsBeltUnique(item, BeltUniqueId::SaintsToken) || state.saintsTokenCooldown > 0)
			continue;
		state.saintsTokenCooldown = SaintsTokenCooldownTicks;
		OnCharmActivated(player, item, startCooldown);
		return &item;
	}
	return nullptr;
}

/** @brief A charm effect's duration after the charm's increased duration. */
int CharmTicks(const Item &item, int ticks)
{
	int percent = 100 + BeltUniqueEffect(item);
	if (IsBeltUnique(item, BeltUniqueId::Witchglass))
		percent -= 25;
	return std::clamp(ticks * percent / 100, 1, 255);
}

int ResistCharmIndex(DamageType damageType)
{
	switch (damageType) {
	case DamageType::Fire:
		return 0;
	case DamageType::Lightning:
		return 1;
	// The Amethyst Charm answers chaos, as in POE2 (Curtis 2026-10-30), and the Sapphire Charm cold (2026-10-31).
	case DamageType::Chaos:
		return 2;
	case DamageType::Cold:
		return 3;
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
	case IMISC_CHARM_COLD:
		return CharmEffect::Cold;
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
		return _("A chaos hit lands: +25% chaos resistance for 4 seconds");
	case CharmEffect::Stun:
		return _("You're stunned: break free and can't be stunned for 3 seconds");
	case CharmEffect::Golden:
		return _("You kill a rare or unique monster: it drops an extra item");
	case CharmEffect::Cold:
		return _("A cold hit lands: +25% cold resistance for 4 seconds");
	default:
		return {};
	}
}

int CharmChargesPerUseFor(CharmEffect effect)
{
	return effect == CharmEffect::Golden ? MaxCharmCharges : CharmChargesPerUse;
}

int CharmChargesPerUseFor(const Item &item)
{
	return std::min(CharmChargesPerUseFor(GetCharmEffect(item)) * BeltUniqueChargePercent(item) / 100, MaxCharmCharges);
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

bool IsBesideTownWell(Point tile)
{
	// The well in Tristram's square covers tiles 60-61, 70-71 (see CleanTownFountain); a step from its rim counts.
	return tile.x >= 58 && tile.x <= 63 && tile.y >= 68 && tile.y <= 73;
}

namespace {

/** The local hero clicked the well and is on the way to it. */
bool WellAsked = false;
/** Ticks before giving up on reaching it, so the walk has time to start. */
int WellWaitTicks = 0;

void UseTownWell(Player &player)
{
	bool refilled = false;
	for (Item &item : player.SpdList) {
		if (IsCharm(item) && item._iCharges < MaxCharmCharges) {
			item._iCharges = MaxCharmCharges;
			refilled = true;
		}
	}
	if (&player != MyPlayer)
		return;
	PlaySfxLoc(SfxID::OperateFountain, player.position.tile);
	EventPlrMsg(refilled ? _("The well refills your charms.") : _("Your charms are already full."), UiFlags::ColorWhitegold);
	RedrawEverything();
}

} // namespace

bool IsTownWellTile(Point tile)
{
	return tile.x >= 60 && tile.x <= 61 && tile.y >= 70 && tile.y <= 71;
}

void ClickTownWell(Player &player)
{
	if (!gbIsPoe2 || !player.isOnLevel(0))
		return;
	if (IsBesideTownWell(player.position.tile)) {
		WellAsked = false;
		UseTownWell(player);
		return;
	}
	// Walk to the nearest open tile beside the well.
	std::optional<Point> best;
	for (int y = 69; y <= 72; y++) {
		for (int x = 59; x <= 62; x++) {
			const Point tile { x, y };
			if (IsTownWellTile(tile) || IsTileSolid(tile))
				continue;
			if (!best || player.position.tile.WalkingDistance(tile) < player.position.tile.WalkingDistance(*best))
				best = tile;
		}
	}
	if (!best)
		return;
	WellAsked = true;
	WellWaitTicks = 10;
	NetSendCmdLoc(MyPlayerId, true, CMD_WALKXY, *best);
}

void RefillCharmsAtWell(Player &player)
{
	if (!WellAsked || &player != MyPlayer)
		return;
	if (!gbIsPoe2 || !player.isOnLevel(0)) {
		WellAsked = false;
		return;
	}
	if (IsBesideTownWell(player.position.tile) && player._pmode == PM_STAND) {
		WellAsked = false;
		UseTownWell(player);
	} else if (--WellWaitTicks <= 0 && player._pmode == PM_STAND && player.walkpath[0] == WALK_NONE) {
		WellAsked = false; // It stopped short.
	}
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
	for (size_t i = 0; i < data.resistCharmTicks.size(); i++) {
		uint8_t &ticks = data.resistCharmTicks[i];
		// Little Guardian keeps some of its resistance a little longer.
		if (ticks > 0 && --ticks == 0 && data.uniqueState.guardianArmed[i]) {
			data.uniqueState.guardianArmed[i] = false;
			data.uniqueState.guardianTicks[i] = LittleGuardianTicks;
		}
	}
	if (data.charmCooldown > 0) {
		data.charmCooldown--;
		return;
	}
	if (&player != MyPlayer)
		return;

	if (player._pMaxHP > 0 && player._pHitPoints.raw() < PercentOf(player._pMaxHP, CharmLifeThreshold) && UseCharm(player, CharmEffect::Life) != nullptr) {
		const Fixed26_6 heal = Fixed26_6::fromRaw(PercentOf(player._pMaxHP, CharmLifeRecovery) * LifeRecoveryPercent(player, false) / 100);
		player._pHitPoints = std::min(player._pHitPoints + heal, player._pMaxHP);
		player._pHPBase = std::min(player._pHPBase + heal, player._pMaxHPBase);
		AddVisualEffect(player.position.tile, MissileGraphicID::RedFlareExplosion);
		RedrawComponent(PanelDrawComponent::Health);
		return;
	}
	if (player._pMaxMana > 0 && player._pMana.raw() < PercentOf(player._pMaxMana, CharmManaThreshold) && UseCharm(player, CharmEffect::Mana) != nullptr) {
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
	    && damage.raw() >= PercentOf(player._pMaxHP, GuardHitThreshold)) {
		if (const Item *charm = UseCharm(player, CharmEffect::Guard); charm != nullptr) {
			player.poe2.guardTicks = static_cast<uint8_t>(CharmTicks(*charm, GuardTicks));
			AddVisualEffect(player.position.tile, MissileGraphicID::ManaShield);
		}
	}
	return damage;
}

int ResistanceAgainstHit(Player &player, DamageType damageType, int resistance)
{
	const int index = ResistCharmIndex(damageType);
	if (!gbIsPoe2 || index < 0)
		return resistance;
	uint8_t &ticks = player.poe2.resistCharmTicks[index];
	static constexpr CharmEffect Effects[] = { CharmEffect::Fire, CharmEffect::Lightning, CharmEffect::Magic, CharmEffect::Cold };
	static constexpr MissileGraphicID Visuals[] = { MissileGraphicID::OrangeFlareExplosion, MissileGraphicID::YellowFlareExplosion, MissileGraphicID::AcidSplat, MissileGraphicID::BlueFlareExplosion };
	if (ticks == 0 && &player == MyPlayer) {
		if (const Item *charm = UseCharm(player, Effects[index], false); charm != nullptr) {
			ticks = static_cast<uint8_t>(CharmTicks(*charm, ResistCharmTicks));
			player.poe2.uniqueState.guardianArmed[index] = IsBeltUnique(*charm, BeltUniqueId::LittleGuardian);
			AddVisualEffect(player.position.tile, Visuals[index]);
		}
	}
	if (ticks == 0) {
		// Little Guardian's lingering resistance.
		if (player.poe2.uniqueState.guardianTicks[index] > 0)
			return std::min(resistance + ResistCharmBonus * LittleGuardianPercent / 100, MaxResistance);
		return resistance;
	}
	return std::min(resistance + ResistCharmBonus, MaxResistance);
}

bool CharmPreventsStun(Player &player)
{
	if (!gbIsPoe2)
		return false;
	if (player.poe2.stunImmuneTicks > 0)
		return true;
	if (&player != MyPlayer)
		return false;
	const Item *charm = UseCharm(player, CharmEffect::Stun, false);
	// The Last Warning, a Guard charm, also breaks stuns.
	if (charm == nullptr) {
		const bool lastWarning = std::any_of(std::begin(player.SpdList), std::end(player.SpdList), [](const Item &item) { return IsBeltUnique(item, BeltUniqueId::LastWarning); });
		if (lastWarning)
			charm = UseCharm(player, CharmEffect::Guard, false);
	}
	if (charm == nullptr)
		return false;
	player.poe2.stunImmuneTicks = static_cast<uint8_t>(CharmTicks(*charm, StunImmuneTicks));
	AddVisualEffect(player.position.tile, MissileGraphicID::StoneCurseShatter);
	return true;
}

int GoldenCharmExtraDrops(Player &player, const Monster &monster)
{
	if (!gbIsPoe2)
		return 0;
	if (!monster.isUnique() && GetMonsterAffixes(monster).rarity != MonsterRarity::Rare)
		return 0;
	return UseCharm(player, CharmEffect::Golden, false) != nullptr ? 1 : 0;
}

} // namespace devilution
