#include "poe2/economy.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "engine/random.hpp"
#include "player.h"
#include "plrmsg.h"
#include "poe2/bases.hpp"
#include "poe2/charms.hpp"
#include "poe2/currency.hpp"
#include "poe2/flasks.hpp"
#include "poe2/loot.hpp"
#include "poe2/telemetry.hpp"
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
	{ N_("Staff"), 100 },
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

bool GambleFamilyHas(GambleFamily family, ItemType type)
{
	switch (family) {
	case GambleFamily::Sword:
		return type == ItemType::Sword;
	case GambleFamily::Axe:
		return type == ItemType::Axe;
	case GambleFamily::Mace:
		return type == ItemType::Mace;
	case GambleFamily::Bow:
		return type == ItemType::Bow;
	case GambleFamily::Staff:
		return type == ItemType::Staff;
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
	if (roll < 985)
		return ITEM_QUALITY_RARE;
	return ITEM_QUALITY_UNIQUE;
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
		return DisenchantYield { ShardKind::Chance, 1 };
	default:
		return std::nullopt;
	}
}

std::optional<SalvageYield> GetSalvageYield(const Item &item)
{
	if (item.isEmpty())
		return std::nullopt;
	item_misc_id currency;
	if (IsFlaskPotion(item) || IsCharm(item)) {
		currency = IMISC_ORB_BAUBLE;
	} else {
		switch (item._itype) {
		case ItemType::Sword:
		case ItemType::Axe:
		case ItemType::Mace:
		case ItemType::Bow:
			currency = IMISC_ORB_WHETSTONE;
			break;
		case ItemType::Staff:
			currency = IMISC_ORB_ETCHER;
			break;
		case ItemType::LightArmor:
		case ItemType::MediumArmor:
		case ItemType::HeavyArmor:
		case ItemType::Helm:
		case ItemType::Shield:
			currency = IMISC_ORB_SCRAP;
			break;
		default:
			return std::nullopt;
		}
	}
	const int quality = ItemQuality(item);
	return SalvageYield { currency, quality / 5, (quality % 5) * 20, std::min<int>(item._iPoe2Craft.sockets, 3) };
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

void GainShards(Player &player, ShardKind kind, int shards)
{
	const int orbs = AddShards(player, kind, shards);
	for (int i = 0; i < orbs; i++)
		GiveCurrencyItem(player, ShardCurrency(kind));
	EventPlrMsg(StrCat(shards, " ", ShardName(kind), shards == 1 ? "" : "s", " (", player.poe2.economy.shards[static_cast<size_t>(kind)], "/", ShardsPerOrb, ")"));
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
		EventPlrMsg(_("Only weapons, armour, shields, flasks and charms can be salvaged."), UiFlags::ColorRed);
		return false;
	}
	TelemetryLog("economy", StrCat("salvaged ", item.getName()));
	player.RemoveInvItem(invIndex);
	int full = yield->full;
	if (yield->extraPercent > 0 && GenerateRnd(100) < yield->extraPercent)
		full++;
	for (int i = 0; i < full; i++)
		GiveCurrencyItem(player, yield->currency);
	if (yield->socketShards > 0)
		GainShards(player, ShardKind::Artificer, yield->socketShards);
	else if (full == 0)
		EventPlrMsg(_("Salvaging it gave nothing: it had no quality or sockets."));
	return true;
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
