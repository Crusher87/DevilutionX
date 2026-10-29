#include "poe2/economy.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "engine/random.hpp"
#include "player.h"
#include "plrmsg.h"
#include "poe2/bases.hpp"
#include "poe2/charms.hpp"
#include "poe2/currency.hpp"
#include "poe2/flasks.hpp"
#include "poe2/loot.hpp"
#include "poe2/telemetry.hpp"
#include "poe2/weapon_kinds.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

struct GambleFamilyData {
	std::string_view name;
	/** @brief The gamble price's class base: weapon 100, armour 85, off-hand 90 and jewellery 145. */
	int classBase;
};

constexpr std::array<GambleFamilyData, GambleFamilyCount> GambleFamilies { {
	{ N_("Sword"), 100 },
	{ N_("Axe"), 100 },
	{ N_("Mace"), 100 },
	{ N_("Bow"), 100 },
	{ N_("Staves and Wands"), 100 },
	{ N_("Shield"), 90 },
	{ N_("Helmet"), 85 },
	{ N_("Body Armour"), 85 },
	{ N_("Ring"), 145 },
	{ N_("Amulet"), 145 },
} };

} // namespace

int VendorRerollCost(int characterLevel)
{
	return 500 + (40 * std::max(characterLevel, 1));
}

std::string_view GambleFamilyName(GambleFamily family)
{
	return _(GambleFamilies[static_cast<size_t>(family)].name.data());
}

bool GambleFamilyHas(GambleFamily family, int itemIndex)
{
	if (itemIndex < 0 || static_cast<size_t>(itemIndex) >= AllItemsList.size())
		return false;
	const ItemType type = AllItemsList[static_cast<size_t>(itemIndex)].itype;
	// The caster weapons of the rework (wands and sceptres are maces underneath) gamble with the staves.
	const bool caster = IsCasterWeapon(itemIndex);
	switch (family) {
	case GambleFamily::Sword:
		return type == ItemType::Sword;
	case GambleFamily::Axe:
		return type == ItemType::Axe;
	case GambleFamily::Mace:
		return type == ItemType::Mace && !caster;
	case GambleFamily::Bow:
		return type == ItemType::Bow;
	case GambleFamily::Staff:
		return type == ItemType::Staff || caster;
	case GambleFamily::Shield:
		return type == ItemType::Shield;
	case GambleFamily::Helmet:
		return type == ItemType::Helm;
	case GambleFamily::BodyArmour:
		return IsAnyOf(type, ItemType::LightArmor, ItemType::MediumArmor, ItemType::HeavyArmor);
	case GambleFamily::Ring:
		return type == ItemType::Ring;
	case GambleFamily::Amulet:
		return type == ItemType::Amulet;
	default:
		return false;
	}
}

int GamblePrice(GambleFamily family, int poe2ItemLevel)
{
	const int classBase = GambleFamilies[static_cast<size_t>(family)].classBase;
	// Rounded by hand: the Amiga toolchain has no std::lround.
	return static_cast<int>((classBase * std::pow(1.0 + (0.045 * std::max(poe2ItemLevel, 1)), 1.22)) + 0.5);
}

item_quality RollGambleRarity()
{
	const int roll = LootRandom(1000);
	if (roll < 520)
		return ITEM_QUALITY_NORMAL;
	if (roll < 880)
		return ITEM_QUALITY_MAGIC;
	return ITEM_QUALITY_RARE;
}

item_quality RollVendorRarity()
{
	const int roll = GenerateRnd(100);
	if (roll < 75)
		return ITEM_QUALITY_NORMAL;
	if (roll < 96)
		return ITEM_QUALITY_MAGIC;
	return ITEM_QUALITY_RARE;
}

bool GambleUsesCharges(const Player &player)
{
	return player.poe2.mapsCompleted == 0;
}

bool CanGamble(const Player &player)
{
	return !GambleUsesCharges(player) || player.poe2.economy.gambleCharges > 0;
}

void SpendGambleCharge(Player &player)
{
	if (GambleUsesCharges(player) && player.poe2.economy.gambleCharges > 0)
		player.poe2.economy.gambleCharges--;
}

void OnUniqueMonsterKilledForGamble(Player &player)
{
	uint8_t &charges = player.poe2.economy.gambleCharges;
	charges = std::min<uint8_t>(charges + 1, MaxGambleCharges);
}

std::optional<DisenchantYield> GetDisenchantYield(const Item &item)
{
	if (item.isEmpty() || EquipmentClassWeight(item._itype) == 0)
		return std::nullopt;
	switch (item._iMagical) {
	case ITEM_QUALITY_MAGIC:
		return DisenchantYield { ShardKind::Transmutation, 1 };
	case ITEM_QUALITY_RARE:
		return DisenchantYield { ShardKind::Regal, RareAffixCount(item) >= 6 ? 2 : 1 };
	case ITEM_QUALITY_UNIQUE:
		return DisenchantYield { ShardKind::Chance, ChanceShardsPerUnique };
	default:
		return std::nullopt;
	}
}

namespace {

/** @brief The quality currency salvaging the item gives: weapons, armour, shields, flasks and charms. */
std::optional<item_misc_id> SalvageCurrency(const Item &item)
{
	if (IsFlaskPotion(item) || IsCharm(item))
		return IMISC_ORB_BAUBLE;
	switch (item._itype) {
	case ItemType::Sword:
	case ItemType::Axe:
	case ItemType::Mace:
	case ItemType::Bow:
		return IMISC_ORB_WHETSTONE;
	case ItemType::Staff:
		return IMISC_ORB_ETCHER;
	case ItemType::LightArmor:
	case ItemType::MediumArmor:
	case ItemType::HeavyArmor:
	case ItemType::Helm:
	case ItemType::Shield:
		return IMISC_ORB_SCRAP;
	default:
		return std::nullopt;
	}
}

} // namespace

std::optional<SalvageYield> GetSalvageYield(const Item &item)
{
	if (item.isEmpty())
		return std::nullopt;
	const std::optional<item_misc_id> currency = SalvageCurrency(item);
	// Curtis 2026-10-23: the anvil takes uniques too, for the Chance Shards disenchanting gives, on top of what
	// their quality and sockets give. Unique flasks and charms, which Adria doesn't take, give the same.
	int chanceShards = 0;
	if (item._iMagical == ITEM_QUALITY_UNIQUE) {
		if (const std::optional<DisenchantYield> disenchant = GetDisenchantYield(item); disenchant && disenchant->kind == ShardKind::Chance)
			chanceShards = disenchant->shards;
		else if (currency)
			chanceShards = ChanceShardsPerUnique;
	}
	if (!currency) {
		// Unique jewellery has no quality currency or sockets, only its shards.
		if (chanceShards == 0)
			return std::nullopt;
		return SalvageYield { IMISC_NONE, 0, 0, chanceShards };
	}
	const int quality = ItemQuality(item);
	return SalvageYield { *currency, (quality + 4) / 5, std::min<int>(item._iPoe2Craft.sockets, 3), chanceShards };
}

bool SalvageGivesSomething(const Item &item)
{
	const std::optional<SalvageYield> yield = GetSalvageYield(item);
	return yield && (yield->full > 0 || yield->socketShards > 0 || yield->chanceShards > 0);
}

int AddShards(Player &player, ShardKind kind, int shards)
{
	uint8_t &held = player.poe2.economy.shards[static_cast<size_t>(kind)];
	const int total = held + std::max(shards, 0);
	held = static_cast<uint8_t>(total % ShardsPerOrb);
	return total / ShardsPerOrb;
}

item_misc_id ShardCurrency(ShardKind kind)
{
	switch (kind) {
	case ShardKind::Regal:
		return IMISC_ORB_REGAL;
	case ShardKind::Chance:
		return IMISC_ORB_CHANCE;
	case ShardKind::Artificer:
		return IMISC_ORB_ARTIFICER;
	default:
		return IMISC_ORB_TRANSMUTE;
	}
}

namespace {

std::string_view CurrencyName(item_misc_id currency)
{
	for (const ItemData &data : AllItemsList) {
		if (data.iMiscId == currency)
			return _(data.iName);
	}
	return _("currency");
}

void GainShards(Player &player, ShardKind kind, int shards)
{
	const int orbs = AddShards(player, kind, shards);
	for (int i = 0; i < orbs; i++)
		GiveCurrencyItem(player, ShardCurrency(kind));
	// Shards are a count, not an item, so the message says where they went (Curtis 2026-10-29 couldn't find his).
	const std::string_view orbName = CurrencyName(ShardCurrency(kind));
	const std::string_view article = orbName.find_first_of("AEIOU") == 0 ? "an " : "a ";
	std::string text = StrCat(shards, " ", ShardName(kind), shards == 1 ? "" : "s");
	if (orbs > 0)
		StrAppend(text, ": ", ShardsPerOrb, _(" shards became "), article, orbName);
	else
		StrAppend(text, ": ", player.poe2.economy.shards[static_cast<size_t>(kind)], _(" of "), ShardsPerOrb, _(" make "), article, orbName);
	EventPlrMsg(text);
}

} // namespace

bool DisenchantInvItem(Player &player, int invIndex)
{
	const Item &item = player.InvList[invIndex];
	const std::optional<DisenchantYield> yield = GetDisenchantYield(item);
	if (!yield) {
		EventPlrMsg(_("Only magic, rare and unique equipment can be disenchanted."), UiFlags::ColorRed);
		return false;
	}
	TelemetryLog("economy", StrCat("disenchanted ", item.getName()));
	player.RemoveInvItem(invIndex);
	GainShards(player, yield->kind, yield->shards);
	return true;
}

bool SalvageInvItem(Player &player, int invIndex)
{
	const Item &item = player.InvList[invIndex];
	const std::optional<SalvageYield> yield = GetSalvageYield(item);
	if (!yield) {
		EventPlrMsg(_("Only weapons, armour, shields, flasks, charms and uniques can be salvaged."), UiFlags::ColorRed);
		return false;
	}
	TelemetryLog("economy", StrCat("salvaged ", item.getName()));
	player.RemoveInvItem(invIndex);
	for (int i = 0; i < yield->full; i++)
		GiveCurrencyItem(player, yield->currency);
	if (yield->full > 0)
		EventPlrMsg(SalvageYieldText(SalvageYield { yield->currency, yield->full, 0 }));
	if (yield->socketShards > 0)
		GainShards(player, ShardKind::Artificer, yield->socketShards);
	if (yield->chanceShards > 0)
		GainShards(player, ShardKind::Chance, yield->chanceShards);
	if (yield->full == 0 && yield->socketShards == 0 && yield->chanceShards == 0)
		EventPlrMsg(_("Salvaging it gave nothing: it had no quality or sockets."));
	return true;
}

std::string SalvageYieldText(const SalvageYield &yield)
{
	std::vector<std::string> parts;
	if (yield.full > 0) {
		parts.push_back(StrCat(yield.full, " ", CurrencyName(yield.currency), yield.full == 1 ? "" : "s"));
	}
	if (yield.socketShards > 0)
		parts.push_back(StrCat(yield.socketShards, " ", ShardName(ShardKind::Artificer), yield.socketShards == 1 ? "" : "s"));
	if (yield.chanceShards > 0)
		parts.push_back(StrCat(yield.chanceShards, " ", ShardName(ShardKind::Chance), yield.chanceShards == 1 ? "" : "s"));
	if (parts.empty())
		return std::string(_("nothing"));
	std::string text = parts[0];
	for (size_t i = 1; i < parts.size(); i++)
		StrAppend(text, i + 1 == parts.size() ? _(" and ") : ", ", parts[i]);
	return text;
}

std::string_view ShardName(ShardKind kind)
{
	switch (kind) {
	case ShardKind::Regal:
		return _("Regal Shard");
	case ShardKind::Chance:
		return _("Chance Shard");
	case ShardKind::Artificer:
		return _("Artificer's Shard");
	default:
		return _("Transmutation Shard");
	}
}

} // namespace devilution
