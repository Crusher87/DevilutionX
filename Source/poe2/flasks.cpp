#include "poe2/flasks.hpp"

#include <algorithm>

#include "effects.h"
#include "engine/backbuffer_state.hpp"
#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "monster.h"
#include "player.h"
#include "plrmsg.h"
#include "poe2/charms.hpp"
#include "poe2/monster_mods.hpp"
#include "utils/format.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"

namespace devilution {

bool IsFlaskPotion(const Item &item)
{
	return !item.isEmpty() && IsAnyOf(item._iMiscId, IMISC_HEAL, IMISC_FULLHEAL, IMISC_MANA, IMISC_FULLMANA, IMISC_REJUV, IMISC_FULLREJUV);
}

bool IsFlaskSlot(const Player &player, size_t beltSlot)
{
	return gbIsPoe2 && beltSlot < NumFlasks && IsFlaskPotion(player.SpdList[beltSlot]);
}

namespace {

bool IsRejuvenation(const Item &item)
{
	return IsAnyOf(item._iMiscId, IMISC_REJUV, IMISC_FULLREJUV);
}

bool RecoversLife(const Item &item)
{
	return IsAnyOf(item._iMiscId, IMISC_HEAL, IMISC_FULLHEAL);
}

/** @brief Adds raw points to a current/base pair without going over the maximum. */
void Restore(Fixed26_6 &current, Fixed26_6 &base, Fixed26_6 max, int32_t raw)
{
	const int32_t gain = std::min(raw, max.raw() - current.raw());
	if (gain <= 0)
		return;
	current += Fixed26_6::fromRaw(gain);
	base += Fixed26_6::fromRaw(gain);
}

/** @brief Recovers this tick's share of a flask; stops early once the pool is full, like POE2. */
void RecoverTick(int32_t &raw, uint8_t &ticks, Fixed26_6 &current, Fixed26_6 &base, Fixed26_6 max)
{
	if (ticks == 0)
		return;
	if (current >= max) {
		raw = 0;
		ticks = 0;
		return;
	}
	const int32_t gain = raw / ticks;
	Restore(current, base, max, gain);
	raw -= gain;
	ticks--;
}

} // namespace

size_t GetFlaskTier(const Item &item)
{
	const int itemLevel = item._iCreateInfo & CF_LEVEL;
	size_t tier = 0;
	for (size_t i = 0; i < NumFlaskTiers; i++) {
		if (itemLevel >= FlaskTiers[i].level)
			tier = i;
	}
	if (IsAnyOf(item._iMiscId, IMISC_FULLHEAL, IMISC_FULLMANA, IMISC_FULLREJUV))
		tier += FullFlaskTierBonus;
	return std::min(tier, NumFlaskTiers - 1);
}

uint8_t FlaskChargesPerUseFor(const Item &item)
{
	return IsRejuvenation(item) ? RejuvenationChargesPerUse : FlaskChargesPerUse;
}

std::string FlaskDescription(const Item &item)
{
	const FlaskTier &tier = FlaskTiers[GetFlaskTier(item)];
	if (IsRejuvenation(item))
		return FormatRuntime(_("Instantly recovers {:d} life and {:d} mana"), tier.life / 2, tier.mana / 2);
	if (RecoversLife(item))
		return FormatRuntime(_("Recovers {:d} life over {:.1f} seconds"), tier.life, tier.lifeTicks / 20.F);
	return FormatRuntime(_("Recovers {:d} mana over {:.1f} seconds"), tier.mana, tier.manaTicks / 20.F);
}

bool CanUseFlask(const Player &player, size_t beltSlot)
{
	return IsFlaskSlot(player, beltSlot) && player.poe2.flaskCharges[beltSlot] >= FlaskChargesPerUseFor(player.SpdList[beltSlot]);
}

bool UseFlask(Player &player, size_t beltSlot)
{
	if (!IsFlaskSlot(player, beltSlot))
		return false;
	if (!CanUseFlask(player, beltSlot)) {
		if (&player == MyPlayer)
			EventPlrMsg(_("That flask is empty. Kill monsters to refill it."), UiFlags::ColorRed);
		return false;
	}

	const Item &flask = player.SpdList[beltSlot];
	player.poe2.flaskCharges[beltSlot] -= FlaskChargesPerUseFor(flask);
	if (&player == MyPlayer)
		PlaySFX(ItemInvSnds[ItemCAnimTbl[flask._iCurs]]);

	const FlaskTier &tier = FlaskTiers[GetFlaskTier(flask)];
	if (IsRejuvenation(flask)) {
		// Rejuvenation is the emergency button: half the tier's life and mana, all at once.
		if (!HasAnyOf(player._pIFlags, ItemSpecialEffect::NoMana))
			Restore(player._pMana, player._pManaBase, player._pMaxMana, Fixed26_6::fromInt(tier.mana / 2).raw());
		Restore(player._pHitPoints, player._pHPBase, player._pMaxHP, Fixed26_6::fromInt(tier.life / 2).raw());
	} else if (RecoversLife(flask)) {
		// Drinking again restarts the recovery with the new flask, like POE2.
		player.poe2.lifeFlaskRaw = Fixed26_6::fromInt(tier.life).raw();
		player.poe2.lifeFlaskTicks = static_cast<uint8_t>(tier.lifeTicks);
	} else {
		player.poe2.manaFlaskRaw = Fixed26_6::fromInt(tier.mana).raw();
		player.poe2.manaFlaskTicks = static_cast<uint8_t>(tier.manaTicks);
	}
	if (&player == MyPlayer) {
		RedrawComponent(PanelDrawComponent::Health);
		RedrawComponent(PanelDrawComponent::Mana);
	}
	return true;
}

void ProcessFlaskRecovery(Player &player)
{
	if (!gbIsPoe2 || player.hasNoLife())
		return;
	if (player.poe2.lifeFlaskTicks > 0) {
		RecoverTick(player.poe2.lifeFlaskRaw, player.poe2.lifeFlaskTicks, player._pHitPoints, player._pHPBase, player._pMaxHP);
		if (&player == MyPlayer)
			RedrawComponent(PanelDrawComponent::Health);
	}
	if (player.poe2.manaFlaskTicks > 0) {
		if (HasAnyOf(player._pIFlags, ItemSpecialEffect::NoMana)) {
			player.poe2.manaFlaskRaw = 0;
			player.poe2.manaFlaskTicks = 0;
		} else {
			RecoverTick(player.poe2.manaFlaskRaw, player.poe2.manaFlaskTicks, player._pMana, player._pManaBase, player._pMaxMana);
		}
		if (&player == MyPlayer)
			RedrawComponent(PanelDrawComponent::Mana);
	}
}

uint8_t FlaskChargesForKill(const Monster &monster)
{
	// POE2: magic monsters give twice a normal monster's charges and rares twice that.
	if (monster.isUnique())
		return 15;
	switch (GetMonsterAffixes(monster).rarity) {
	case MonsterRarity::Rare:
		return 8;
	case MonsterRarity::Magic:
		return 4;
	default:
		return 2;
	}
}

void GainFlaskCharges(Player &player, uint8_t charges)
{
	for (uint8_t &flaskCharges : player.poe2.flaskCharges)
		flaskCharges = static_cast<uint8_t>(std::min<int>(MaxFlaskCharges, flaskCharges + charges));
	GainCharmCharges(player, charges);
}

void RefillFlasks(Player &player)
{
	player.poe2.flaskCharges.fill(MaxFlaskCharges);
	RefillCharms(player);
}

} // namespace devilution
