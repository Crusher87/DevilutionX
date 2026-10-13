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
	FlaskEffect effect { tier.life * percent / 100, tier.lifeTicks * ticksPercent / 100, tier.mana * percent / 100, tier.manaTicks * ticksPercent / 100, instantPercent };
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
	if (!IsFlaskPotion(item) || (item.dwBuff & CF_POE2) == 0)
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
	if (IsAnyOf(item._iMiscId, IMISC_FULLHEAL, IMISC_FULLMANA, IMISC_FULLREJUV))
		tier += FullFlaskTierBonus;
	return std::min(tier, NumFlaskTiers - 1);
}

uint8_t FlaskChargesPerUseFor(const Item &item)
{
	const uint8_t charges = IsRejuvenation(item) ? RejuvenationChargesPerUse : FlaskChargesPerUse;
	if (GetFlaskSuffix(item) == FlaskSuffix::Plenty)
		return static_cast<uint8_t>((charges * 3 + 3) / 4);
	return charges;
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
	player.poe2.flaskCharges[beltSlot] -= FlaskChargesPerUseFor(flask);
	if (&player == MyPlayer)
		PlaySFX(ItemInvSnds[ItemCAnimTbl[flask._iCurs]]);

	const FlaskEffect effect = GetFlaskEffect(flask);
	const bool canGainMana = !HasAnyOf(player._pIFlags, ItemSpecialEffect::NoMana);
	const bool life = IsRejuvenation(flask) || RecoversLife(flask);
	const bool mana = IsRejuvenation(flask) || !RecoversLife(flask);
	int recoveryTicks = 0;
	if (life) {
		const int32_t raw = Fixed26_6::fromInt(effect.life).raw();
		const auto instant = static_cast<int32_t>(static_cast<int64_t>(raw) * effect.instantPercent / 100);
		Restore(player._pHitPoints, player._pHPBase, player._pMaxHP, instant);
		// Drinking again restarts the recovery with the new flask, like POE2.
		if (raw > instant) {
			player.poe2.lifeFlaskRaw = raw - instant;
			player.poe2.lifeFlaskTicks = static_cast<uint8_t>(effect.lifeTicks);
			recoveryTicks = effect.lifeTicks;
		}
	}
	if (mana && canGainMana) {
		const int32_t raw = Fixed26_6::fromInt(effect.mana).raw();
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
	for (size_t slot = 0; slot < NumFlasks; slot++) {
		uint8_t &flaskCharges = player.poe2.flaskCharges[slot];
		flaskCharges = static_cast<uint8_t>(std::min<int>(MaxFlaskChargesFor(player.SpdList[slot]), flaskCharges + charges));
	}
	GainCharmCharges(player, charges);
}

void RefillFlasks(Player &player)
{
	for (size_t slot = 0; slot < NumFlasks; slot++)
		player.poe2.flaskCharges[slot] = MaxFlaskChargesFor(player.SpdList[slot]);
	RefillCharms(player);
}

} // namespace devilution
