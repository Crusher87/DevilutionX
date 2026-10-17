#include "poe2/flasks.hpp"

#include <algorithm>
#include <cstdlib>

#include "effects.h"
#include "engine/backbuffer_state.hpp"
#include "game_mode.hpp"
#include "inv.h"
#include "items.h"
#include "monster.h"
#include "player.h"
#include "plrmsg.h"
#include "poe2/belt_uniques.hpp"
#include "poe2/charms.hpp"
#include "poe2/map_play.hpp"
#include "poe2/monster_mods.hpp"
#include "poe2/passive_tree.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/unique_effects.hpp"
#include "utils/format.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/str_cat.hpp"
#include "utils/utf8.hpp"

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

/** @brief The Long Recovery: heals the owner's minions within a few tiles. */
void HealNearbyMinions(const Player &player, int32_t raw)
{
	if (raw <= 0)
		return;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &minion = Monsters[ActiveMonsters[i]];
		if (!minion.isPlayerMinion() || minion.hasNoLife() || static_cast<size_t>(minion.goalVar3) != player.getId())
			continue;
		if (std::max(std::abs(minion.position.tile.x - player.position.tile.x), std::abs(minion.position.tile.y - player.position.tile.y)) > FarRangeTiles)
			continue;
		minion.hitPoints = std::min(minion.hitPoints + Fixed26_6::fromRaw(raw), minion.maxHitPoints);
	}
}

uint32_t Mix(uint32_t value)
{
	value ^= value >> 16;
	value *= 0x7FEB352DU;
	value ^= value >> 15;
	value *= 0x846CA68BU;
	value ^= value >> 16;
	return value;
}

constexpr const char *TierNames[NumFlaskTiers] = { N_("Minor"), N_("Light"), "", N_("Greater"), N_("Superior"), N_("Full") };

std::string_view PrefixName(FlaskPrefix prefix)
{
	switch (prefix) {
	case FlaskPrefix::Concentrated: return _("Concentrated");
	case FlaskPrefix::Bubbling: return _("Bubbling");
	case FlaskPrefix::Seething: return _("Seething");
	case FlaskPrefix::Ample: return _("Ample");
	case FlaskPrefix::Saturated: return _("Saturated");
	default: return {};
	}
}

std::string_view SuffixName(FlaskSuffix suffix)
{
	switch (suffix) {
	case FlaskSuffix::Ox: return _("of the Ox");
	case FlaskSuffix::Plenty: return _("of Plenty");
	case FlaskSuffix::Springs: return _("of the Springs");
	case FlaskSuffix::Guarding: return _("of Guarding");
	default: return {};
	}
}

/** @brief What drinking the flask does once its mods and quality are counted. */
struct FlaskEffect {
	int life;
	int lifeTicks;
	int mana;
	int manaTicks;
	/** @brief Percent of the amount recovered at once. */
	int instantPercent;
};

FlaskEffect GetFlaskEffect(const Item &item)
{
	const FlaskTier &tier = FlaskTiers[GetFlaskTier(item)];
	int percent = 100 + GetFlaskQuality(item);
	int ticksPercent = 100;
	int instantPercent = 0;
	switch (GetFlaskPrefix(item)) {
	case FlaskPrefix::Concentrated:
		percent += 40;
		break;
	case FlaskPrefix::Bubbling:
		percent -= 25;
		instantPercent = 50;
		break;
	case FlaskPrefix::Seething:
		percent -= 40;
		instantPercent = 100;
		break;
	case FlaskPrefix::Saturated:
		percent += 60;
		ticksPercent = 200;
		break;
	default:
		break;
	}
	// POE2 Mashup: unique flasks.
	percent += BeltUniqueEffect(item);
	int lifePercent = percent;
	if (IsBeltUnique(item, BeltUniqueId::SaintsBlood))
		lifePercent += 20;
	if (IsBeltUnique(item, BeltUniqueId::Heartglass))
		lifePercent /= 2;
	if (IsBeltUnique(item, BeltUniqueId::RedMercy)) {
		instantPercent = 50;
		ticksPercent = 6 * 20 * 100 / std::max(tier.lifeTicks, 1);
	}
	if (IsBeltUnique(item, BeltUniqueId::BlueSilence))
		instantPercent = 40;
	FlaskEffect effect { tier.life * lifePercent / 100, tier.lifeTicks * ticksPercent / 100, tier.mana * percent / 100, tier.manaTicks * ticksPercent / 100, instantPercent };
	if (IsRejuvenation(item)) {
		// Rejuvenation is the emergency button: half the tier's life and mana, all at once.
		effect.life /= 2;
		effect.mana /= 2;
		effect.instantPercent = 100;
	}
	return effect;
}

} // namespace

FlaskPrefix GetFlaskPrefix(const Item &item)
{
	const auto prefix = static_cast<uint8_t>((item.dwBuff & CF_POE2_FLASK_PREFIX) >> 16);
	return prefix < static_cast<uint8_t>(FlaskPrefix::Count) ? static_cast<FlaskPrefix>(prefix) : FlaskPrefix::None;
}

FlaskSuffix GetFlaskSuffix(const Item &item)
{
	const auto suffix = static_cast<uint8_t>((item.dwBuff & CF_POE2_FLASK_SUFFIX) >> 19);
	return suffix < static_cast<uint8_t>(FlaskSuffix::Count) ? static_cast<FlaskSuffix>(suffix) : FlaskSuffix::None;
}

int GetFlaskQuality(const Item &item)
{
	return std::min(static_cast<int>((item.dwBuff & CF_POE2_FLASK_QUALITY) >> 22), MaxFlaskQuality);
}

void SetFlaskMods(Item &item, FlaskPrefix prefix, FlaskSuffix suffix, int quality)
{
	quality = std::clamp(quality, 0, MaxFlaskQuality);
	item.dwBuff &= ~(CF_POE2_FLASK_PREFIX | CF_POE2_FLASK_SUFFIX | CF_POE2_FLASK_QUALITY);
	item.dwBuff |= CF_POE2_FLASK_ROLLED | (static_cast<uint32_t>(prefix) << 16) | (static_cast<uint32_t>(suffix) << 19) | (static_cast<uint32_t>(quality) << 22);
	NameFlask(item);
}

void NameFlask(Item &item)
{
	if (!IsFlaskPotion(item) || (item.dwBuff & CF_POE2) == 0 || FinishBeltUnique(item))
		return;
	const FlaskPrefix prefix = GetFlaskPrefix(item);
	const FlaskSuffix suffix = GetFlaskSuffix(item);
	const bool magic = prefix != FlaskPrefix::None || suffix != FlaskSuffix::None;
	item._iMagical = magic ? ITEM_QUALITY_MAGIC : ITEM_QUALITY_NORMAL;
	item._iIdentified = true;

	std::string_view kind = _("Mana Flask");
	if (IsRejuvenation(item))
		kind = _("Rejuvenation Flask");
	else if (RecoversLife(item))
		kind = _("Healing Flask");
	std::string name;
	if (prefix != FlaskPrefix::None)
		StrAppend(name, PrefixName(prefix), " ");
	const char *tierName = TierNames[GetFlaskTier(item)];
	if (*tierName != '\0')
		StrAppend(name, _(tierName), " ");
	StrAppend(name, kind);
	if (suffix != FlaskSuffix::None)
		StrAppend(name, " ", SuffixName(suffix));
	CopyUtf8(item._iIName, name, ItemNameLength);
}

void FinishFlask(Item &item)
{
	if (FinishBeltUnique(item))
		return;
	if (!IsFlaskPotion(item) || (item.dwBuff & CF_POE2) == 0)
		return;
	if ((item.dwBuff & CF_POE2_FLASK_ROLLED) != 0) {
		NameFlask(item);
		return;
	}
	// Dropped flasks can be magic; shop flasks start normal. Rolled from the seed so it doesn't use up random numbers.
	FlaskPrefix prefix = FlaskPrefix::None;
	FlaskSuffix suffix = FlaskSuffix::None;
	const uint32_t roll = Mix(static_cast<uint32_t>(item._iSeed) ^ 0xF1A5u);
	if ((item._iCreateInfo & CF_TOWN) == 0 && item._iCreateInfo != 0 && roll % 100 < MagicFlaskChance) {
		const uint32_t affixes = (roll / 100) % 3; // 0: prefix, 1: suffix, 2: both
		const uint32_t mods = roll / 300;
		if (affixes != 1)
			prefix = static_cast<FlaskPrefix>(1 + (mods % (static_cast<uint32_t>(FlaskPrefix::Count) - 1)));
		if (affixes != 0)
			suffix = static_cast<FlaskSuffix>(1 + ((mods / 8) % (static_cast<uint32_t>(FlaskSuffix::Count) - 1)));
	}
	SetFlaskMods(item, prefix, suffix, 0);
}

size_t GetFlaskTier(const Item &item)
{
	const int itemLevel = item._iCreateInfo & CF_LEVEL;
	size_t tier = 0;
	for (size_t i = 0; i < NumFlaskTiers; i++) {
		if (itemLevel >= FlaskTiers[i].level)
			tier = i;
	}
	if (const BeltUnique *unique = GetBeltUnique(item); unique != nullptr)
		return unique->flaskTier;
	if (IsAnyOf(item._iMiscId, IMISC_FULLHEAL, IMISC_FULLMANA, IMISC_FULLREJUV))
		tier += FullFlaskTierBonus;
	return std::min(tier, NumFlaskTiers - 1);
}

uint8_t FlaskChargesPerUseFor(const Item &item)
{
	const uint8_t charges = IsRejuvenation(item) ? RejuvenationChargesPerUse : FlaskChargesPerUse;
	if (GetFlaskSuffix(item) == FlaskSuffix::Plenty)
		return static_cast<uint8_t>((charges * 3 + 3) / 4);
	return static_cast<uint8_t>(charges * BeltUniqueChargePercent(item) / 100);
}

uint8_t MaxFlaskChargesFor(const Item &item)
{
	if (IsFlaskPotion(item) && GetFlaskPrefix(item) == FlaskPrefix::Ample)
		return MaxFlaskChargesCap;
	return MaxFlaskCharges;
}

std::string FlaskDescription(const Item &item)
{
	const FlaskEffect effect = GetFlaskEffect(item);
	if (IsRejuvenation(item))
		return FormatRuntime(_("Instantly recovers {:d} life and {:d} mana"), effect.life, effect.mana);
	const bool life = RecoversLife(item);
	const int amount = life ? effect.life : effect.mana;
	if (effect.instantPercent >= 100)
		return FormatRuntime(life ? _("Instantly recovers {:d} life") : _("Instantly recovers {:d} mana"), amount);
	const float seconds = (life ? effect.lifeTicks : effect.manaTicks) / 20.F;
	return FormatRuntime(life ? _("Recovers {:d} life over {:.1f} seconds") : _("Recovers {:d} mana over {:.1f} seconds"), amount, seconds);
}

std::vector<std::string> FlaskModLines(const Item &item)
{
	std::vector<std::string> lines;
	switch (GetFlaskPrefix(item)) {
	case FlaskPrefix::Concentrated: lines.emplace_back(_("40% more recovered")); break;
	case FlaskPrefix::Bubbling: lines.emplace_back(_("Half recovered at once, 25% less in total")); break;
	case FlaskPrefix::Seething: lines.emplace_back(_("All recovered at once, 40% less in total")); break;
	case FlaskPrefix::Ample: lines.emplace_back(FormatRuntime(_("{:d} more maximum charges"), AmpleFlaskCharges)); break;
	case FlaskPrefix::Saturated: lines.emplace_back(_("60% more recovered over twice the time")); break;
	default: break;
	}
	switch (GetFlaskSuffix(item)) {
	case FlaskSuffix::Ox: lines.emplace_back(FormatRuntime(_("{:d}% more armour while recovering"), OxArmourPercent)); break;
	case FlaskSuffix::Plenty: lines.emplace_back(_("25% fewer charges used")); break;
	case FlaskSuffix::Springs: lines.emplace_back(_("Gains a charge every 3 seconds")); break;
	case FlaskSuffix::Guarding: lines.emplace_back(_("Guard for 2 seconds when drunk")); break;
	default: break;
	}
	if (const int quality = GetFlaskQuality(item); quality > 0)
		lines.emplace_back(FormatRuntime(_("Quality: +{:d}% (more recovered)"), quality));
	for (std::string &line : BeltUniqueText(item))
		lines.push_back(std::move(line));
	return lines;
}

std::vector<std::string> FlaskAdvancedLines(const Item &item, const Player *player)
{
	std::vector<std::string> lines;
	if (const std::string_view prefix = PrefixName(GetFlaskPrefix(item)); !prefix.empty())
		lines.emplace_back(StrCat(_("Prefix: "), prefix));
	if (const std::string_view suffix = SuffixName(GetFlaskSuffix(item)); !suffix.empty())
		lines.emplace_back(StrCat(_("Suffix: "), suffix));
	int charges = -1;
	if (player != nullptr) {
		for (size_t slot = 0; slot < NumFlasks; slot++) {
			if (&player->SpdList[slot] == &item)
				charges = player->poe2.flaskCharges[slot];
		}
	}
	if (charges >= 0)
		lines.emplace_back(FormatRuntime(_("Charges: {:d}/{:d}, {:d} per use"), charges, MaxFlaskChargesFor(item), FlaskChargesPerUseFor(item)));
	else
		lines.emplace_back(FormatRuntime(_("Holds {:d} charges, {:d} per use"), MaxFlaskChargesFor(item), FlaskChargesPerUseFor(item)));
	lines.emplace_back(_("Refills from kills; drink with keys 1-5"));
	return lines;
}

int FlaskArmourPercent(const Player &player)
{
	return player.poe2.oxFlaskTicks > 0 ? OxArmourPercent : 0;
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
	// Like POE2, a flask won't drink when there's nothing to recover.
	const bool lifeFull = player._pHitPoints >= player._pMaxHP;
	const bool manaFull = player._pMana >= player._pMaxMana || HasAnyOf(player._pIFlags, ItemSpecialEffect::NoMana);
	const bool full = IsRejuvenation(flask) ? (lifeFull && manaFull) : (RecoversLife(flask) ? lifeFull : manaFull);
	if (full) {
		if (&player == MyPlayer)
			EventPlrMsg(RecoversLife(flask) && !IsRejuvenation(flask) ? _("Your life is already full.") : (IsRejuvenation(flask) ? _("Your life and mana are already full.") : _("Your mana is already full.")), UiFlags::ColorWhite);
		return false;
	}
	if (IsBeltUnique(flask, BeltUniqueId::BorrowedBreath) && static_cast<int64_t>(player._pHitPoints.raw()) * 100 > static_cast<int64_t>(player._pMaxHP.raw()) * 70) {
		if (&player == MyPlayer)
			EventPlrMsg(_("Borrowed Breath can't be drunk above 70% life."), UiFlags::ColorWhite);
		return false;
	}
	player.poe2.flaskCharges[beltSlot] -= FlaskChargesPerUseFor(flask);
	if (&player == MyPlayer)
		PlaySFX(ItemInvSnds[ItemCAnimTbl[flask._iCurs]]);

	const FlaskEffect effect = GetFlaskEffect(flask);
	const bool canGainMana = !HasAnyOf(player._pIFlags, ItemSpecialEffect::NoMana);
	const bool life = IsRejuvenation(flask) || RecoversLife(flask);
	const bool mana = IsRejuvenation(flask) || !RecoversLife(flask);
	if (&player == MyPlayer) {
		const auto percent = [](Fixed26_6 value, Fixed26_6 max) { return max > 0 ? static_cast<int>(static_cast<int64_t>(value.raw()) * 100 / max.raw()) : 100; };
		TelemetryFlaskUsed(player, flask, player.poe2.flaskCharges[beltSlot], life, mana, percent(player._pHitPoints, player._pMaxHP), percent(player._pMana, player._pMaxMana));
	}
	int recoveryTicks = 0;
	UniqueRuleState &uniqueState = player.poe2.uniqueState;
	if (life) {
		// POE2 Mashup: uniques can lessen what a life flask recovers.
		auto raw = static_cast<int32_t>(static_cast<int64_t>(Fixed26_6::fromInt(effect.life).raw()) * LifeRecoveryPercent(player, true) / 100);
		raw = static_cast<int32_t>(static_cast<int64_t>(raw) * std::max(100 + PassiveTotal(player, PassiveStat::FlaskRecovery), 0) / 100);
		if (IsBeltUnique(flask, BeltUniqueId::BorrowedBreath) && IsOnLowLife(player))
			raw *= 2;
		const int32_t missing = std::max<int32_t>(player._pMaxHP.raw() - player._pHitPoints.raw(), 0);
		if (IsBeltUnique(flask, BeltUniqueId::LastDraught) && raw > missing)
			player.poe2.guardTicks = static_cast<uint8_t>(std::max<int>(player.poe2.guardTicks, GuardTicks));
		if (IsBeltUnique(flask, BeltUniqueId::LongRecovery))
			HealNearbyMinions(player, raw * 40 / 100);
		const auto instant = static_cast<int32_t>(static_cast<int64_t>(raw) * effect.instantPercent / 100);
		Restore(player._pHitPoints, player._pHPBase, player._pMaxHP, instant);
		OnLifeRecovered(player, raw);
		// Drinking again restarts the recovery with the new flask, like POE2.
		if (raw > instant) {
			player.poe2.lifeFlaskRaw = raw - instant;
			player.poe2.lifeFlaskTicks = static_cast<uint8_t>(effect.lifeTicks);
			recoveryTicks = effect.lifeTicks;
		}
	}
	if (mana && canGainMana) {
		auto raw = static_cast<int32_t>(static_cast<int64_t>(Fixed26_6::fromInt(effect.mana).raw()) * std::max(100 + PassiveTotal(player, PassiveStat::FlaskRecovery), 0) / 100);
		if (IsBeltUnique(flask, BeltUniqueId::BorrowedThought) && static_cast<int64_t>(player._pMana.raw()) * 100 < static_cast<int64_t>(player._pMaxMana.raw()) * 30)
			raw *= 2;
		const int32_t missing = std::max<int32_t>(player._pMaxMana.raw() - player._pMana.raw(), 0);
		if (IsBeltUnique(flask, BeltUniqueId::ScholarsReserve) && raw > missing) {
			uniqueState.scholarPercent = static_cast<uint8_t>(std::min<int32_t>((raw - missing) / 64 / ScholarsReserveManaPerPercent, ScholarsReserveMaxPercent));
			uniqueState.scholarTicks = ScholarsReserveTicks;
		}
		if (IsBeltUnique(flask, BeltUniqueId::DeepWell))
			uniqueState.freeSkill = true;
		if (IsBeltUnique(flask, BeltUniqueId::Moonwater))
			uniqueState.moonwaterTicks = static_cast<uint8_t>(std::max(effect.manaTicks, 20));
		const auto instant = static_cast<int32_t>(static_cast<int64_t>(raw) * effect.instantPercent / 100);
		Restore(player._pMana, player._pManaBase, player._pMaxMana, instant);
		if (raw > instant) {
			player.poe2.manaFlaskRaw = raw - instant;
			player.poe2.manaFlaskTicks = static_cast<uint8_t>(effect.manaTicks);
			recoveryTicks = std::max(recoveryTicks, effect.manaTicks);
		}
	}
	switch (GetFlaskSuffix(flask)) {
	case FlaskSuffix::Ox:
		// An instant flask still gives the armour for a moment.
		player.poe2.oxFlaskTicks = static_cast<uint8_t>(std::max<int>({ player.poe2.oxFlaskTicks, recoveryTicks, GuardingFlaskTicks }));
		break;
	case FlaskSuffix::Guarding:
		player.poe2.guardTicks = static_cast<uint8_t>(std::max<int>(player.poe2.guardTicks, GuardingFlaskTicks));
		break;
	default:
		break;
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
	// Flasks and charms hold no charges in the bag or on the ground: one put into a belt slot from there starts empty
	// and fills from kills. One lifted from the belt onto the cursor and put back in the belt keeps its charges.
	static_assert(MaxBeltItems == std::tuple_size_v<decltype(player.poe2.flaskSlotKeys)>);
	const auto keyOf = [](const Item &item) -> uint64_t {
		return item.isEmpty() ? 0 : (static_cast<uint64_t>(static_cast<uint32_t>(item._iSeed)) << 16) | static_cast<uint16_t>(item.IDidx + 1);
	};
	Poe2PlayerData &data = player.poe2;
	const uint64_t held = keyOf(player.HoldItem);
	for (size_t slot = 0; slot < MaxBeltItems; slot++) {
		Item &item = player.SpdList[slot];
		const uint64_t key = keyOf(item);
		const uint64_t was = data.flaskSlotKeys[slot];
		data.flaskSlotKeys[slot] = key;
		if (!data.flaskSlotKeysKnown || key == was)
			continue;
		const uint8_t wasCharges = slot < NumFlasks ? data.flaskCharges[slot] : 0;
		if (key != 0) {
			const bool putBack = key == data.liftedBeltKey;
			if (slot < NumFlasks)
				data.flaskCharges[slot] = putBack ? data.liftedFlaskCharges : 0;
			else if (!putBack && IsCharm(item))
				item._iCharges = 0;
			if (putBack)
				data.liftedBeltKey = 0;
		}
		// What was here went onto the cursor (picked up, or swapped for the held one).
		if (was != 0 && was == held) {
			data.liftedBeltKey = was;
			data.liftedFlaskCharges = wasCharges;
		}
	}
	if (data.liftedBeltKey != held)
		data.liftedBeltKey = 0;
	data.flaskSlotKeysKnown = true;
	for (int i = 0; i < player._pNumInv; i++) {
		if (IsCharm(player.InvList[i]))
			player.InvList[i]._iCharges = 0;
	}
	if (player.poe2.oxFlaskTicks > 0)
		player.poe2.oxFlaskTicks--;
	if (++player.poe2.springsFlaskTicks >= SpringsChargeTicks) {
		player.poe2.springsFlaskTicks = 0;
		for (size_t slot = 0; slot < NumFlasks; slot++) {
			const Item &flask = player.SpdList[slot];
			if (IsFlaskPotion(flask) && GetFlaskSuffix(flask) == FlaskSuffix::Springs)
				player.poe2.flaskCharges[slot] = static_cast<uint8_t>(std::min<int>(MaxFlaskChargesFor(flask), player.poe2.flaskCharges[slot] + 1));
		}
	}
	if (player.poe2.lifeFlaskTicks > 0 && !IsRecoveryLocked(player)) {
		RecoverTick(player.poe2.lifeFlaskRaw, player.poe2.lifeFlaskTicks, player._pHitPoints, player._pHPBase, player._pMaxHP);
		if (&player == MyPlayer)
			RedrawComponent(PanelDrawComponent::Health);
	}
	if (player.poe2.manaFlaskTicks > 0 && !IsRecoveryLocked(player)) {
		if (HasAnyOf(player._pIFlags, ItemSpecialEffect::NoMana)) {
			player.poe2.manaFlaskRaw = 0;
			player.poe2.manaFlaskTicks = 0;
		} else {
			const Fixed26_6 before = player._pMana;
			RecoverTick(player.poe2.manaFlaskRaw, player.poe2.manaFlaskTicks, player._pMana, player._pManaBase, player._pMaxMana);
			// Moonwater: mana recovery also recovers energy shield at half value.
			if (player.poe2.uniqueState.moonwaterTicks > 0 && player.poe2.maxEnergyShield > 0)
				player.poe2.energyShield = std::min<int32_t>(player.poe2.energyShield + (player._pMana - before).raw() / 2, player.poe2.maxEnergyShield * 64);
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
	// Some waystones give fewer flask charges.
	if (const int percent = MapFlaskChargePercent(); percent < 100 && charges > 0)
		charges = static_cast<uint8_t>(std::max(charges * percent / 100, 1));
	// The passive tree changes how many charges a kill gives.
	if (const int passive = PassiveTotal(player, PassiveStat::FlaskCharges); passive != 0 && charges > 0)
		charges = static_cast<uint8_t>(std::clamp(charges * (100 + passive) / 100, 1, 255));
	for (size_t slot = 0; slot < NumFlasks; slot++) {
		// Moonwater: no charges for life flasks while it works.
		if (player.poe2.uniqueState.moonwaterTicks > 0 && IsFlaskPotion(player.SpdList[slot]) && RecoversLife(player.SpdList[slot]))
			continue;
		uint8_t &flaskCharges = player.poe2.flaskCharges[slot];
		flaskCharges = static_cast<uint8_t>(std::min<int>(MaxFlaskChargesFor(player.SpdList[slot]), flaskCharges + charges));
	}
	GainCharmCharges(player, charges);
}

void RecoverOnKill(Player &player)
{
	if (player.hasNoLife())
		return;
	if (const int life = PassiveTotal(player, PassiveStat::LifeOnKill); life > 0 && !IsRecoveryLocked(player)) {
		const auto raw = static_cast<int32_t>(static_cast<int64_t>(Fixed26_6::fromInt(life).raw()) * LifeRecoveryPercent(player, false) / 100);
		Restore(player._pHitPoints, player._pHPBase, player._pMaxHP, raw);
		OnLifeRecovered(player, raw);
	}
	if (const int mana = PassiveTotal(player, PassiveStat::ManaOnKill); mana > 0 && !HasAnyOf(player._pIFlags, ItemSpecialEffect::NoMana)) {
		const auto raw = static_cast<int32_t>(static_cast<int64_t>(Fixed26_6::fromInt(mana).raw()) * ManaRecoveryPercent(player) / 100);
		Restore(player._pMana, player._pManaBase, player._pMaxMana, raw);
	}
	RedrawComponent(PanelDrawComponent::Health);
	RedrawComponent(PanelDrawComponent::Mana);
}

void RefillFlasks(Player &player)
{
	for (size_t slot = 0; slot < NumFlasks; slot++)
		player.poe2.flaskCharges[slot] = MaxFlaskChargesFor(player.SpdList[slot]);
	// Charms don't refill in town: they fill from kills (Curtis 2026-10-16).
}

} // namespace devilution
