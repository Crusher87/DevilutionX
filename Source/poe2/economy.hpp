/**
 * @file poe2/economy.hpp
 *
 * POE2 Mashup economy (bundle 06_Economy_Item_Disposition): vendor stock that partly refreshes on level-up
 * and can be rerolled for gold, the gambler, and disenchanting and salvaging items for currency.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "items.h"
#include "poe2/economy_state.hpp"

namespace devilution {

struct Player;

// --- Vendors ---

/** @brief Share of an ordinary vendor's stock replaced when the hero gains a level, in percent. */
constexpr int VendorRefreshPercent = 50;
/** @brief Gold a vendor asks to replace all of their stock now. */
int VendorRerollCost(int characterLevel);

// --- Gambling ---

/** @brief The item families the gambler sells; the exact base, rarity and modifiers are hidden until bought. */
enum class GambleFamily : uint8_t {
	Sword,
	Axe,
	Mace,
	Bow,
	Staff,
	Shield,
	Helmet,
	BodyArmour,
	Ring,
	Amulet,
	Count,
};
constexpr size_t GambleFamilyCount = static_cast<size_t>(GambleFamily::Count);

std::string_view GambleFamilyName(GambleFamily family);
/** @brief Whether bases of the item type belong to the family. */
bool GambleFamilyHas(GambleFamily family, ItemType type);
/** @brief The gambler's price: round(class base * (1 + 0.045 * item level)^1.22). */
int GamblePrice(GambleFamily family, int poe2ItemLevel);
/** @brief Rolls the rarity of a gamble: normal 52%, magic 36%, rare 12%; never unique (Curtis 2026-10-16). */
item_quality RollGambleRarity();
/** @brief Rolls the rarity of a vendor's ware from the item's own seed: normal 75%, magic 21%, rare 4%, never unique. */
item_quality RollVendorRarity();
/** @brief Gambling uses up charges in the campaign; in the endgame (after the first map) it doesn't. */
bool GambleUsesCharges(const Player &player);
/** @brief Whether the hero may gamble now: a charge left, or in the endgame. */
bool CanGamble(const Player &player);
void SpendGambleCharge(Player &player);
/** @brief A unique monster died: the gambler has another charge for the hero. */
void OnUniqueMonsterKilledForGamble(Player &player);

// --- Disenchanting and salvaging ---

struct DisenchantYield {
	ShardKind kind;
	int shards;
};

/** @brief What disenchanting the item gives: shards by its rarity; normal items and non-equipment give nothing. */
std::optional<DisenchantYield> GetDisenchantYield(const Item &item);

struct SalvageYield {
	/** @brief The quality currency it gives, its full items, and the chance in percent of one more. */
	item_misc_id currency;
	int full;
	int extraPercent;
	/** @brief Artificer shards from its sockets. */
	int socketShards;
};

/** @brief What salvaging the item gives: weapons, armour, shields, flasks and charms; not jewellery. */
std::optional<SalvageYield> GetSalvageYield(const Item &item);
/** @brief Adds shards to the hero's; returns how many full currency items they made. */
int AddShards(Player &player, ShardKind kind, int shards);
item_misc_id ShardCurrency(ShardKind kind);
/** @brief Disenchants the inventory item: it's destroyed for shards, and full sets become currency. */
bool DisenchantInvItem(Player &player, int invIndex);
/** @brief Salvages the inventory item: it's destroyed for quality currency and socket shards. */
bool SalvageInvItem(Player &player, int invIndex);
std::string_view ShardName(ShardKind kind);

} // namespace devilution
