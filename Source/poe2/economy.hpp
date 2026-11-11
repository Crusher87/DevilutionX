/**
 * @file poe2/economy.hpp
 *
 * POE2 Mashup economy (bundle 06_Economy_Item_Disposition): vendor stock that partly refreshes on level-up
 * and can be rerolled for gold, the gambler, and disenchanting and salvaging items for currency.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "items.h"
#include "poe2/economy_state.hpp"

namespace devilution {

struct Player;

// --- Vendors ---

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
/** @brief Whether a row of the item data belongs to the gamble family. */
bool GambleFamilyHas(GambleFamily family, int itemIndex);
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

/** @brief Chance Shards a unique gives, disenchanted or salvaged (bundle salvage_and_disenchant.json); ShardsPerOrb make an Orb of Chance. */
constexpr int ChanceShardsPerUnique = 1;

struct DisenchantYield {
	ShardKind kind;
	int shards;
};

/** @brief What disenchanting the item gives: shards by its rarity; normal items and non-equipment give nothing. */
std::optional<DisenchantYield> GetDisenchantYield(const Item &item);

struct SalvageYield {
	/** @brief The quality currency it gives, and how many: one per 5% quality, rounded up, like POE2's salvage bench. IMISC_NONE for unique jewellery. */
	item_misc_id currency;
	int full;
	/** @brief Artificer shards from its sockets. */
	int socketShards;
	/** @brief Chance Shards from a unique, as many as disenchanting gives (Curtis 2026-10-23). */
	int chanceShards = 0;
};

/** @brief What salvaging the item gives: weapons, armour, shields, flasks and charms, and unique equipment (jewellery too). */
std::optional<SalvageYield> GetSalvageYield(const Item &item);
/** @brief Whether salvaging the item gives anything, for the anvil's list: quality, sockets or being unique. */
bool SalvageGivesSomething(const Item &item);
/** @brief The orb ShardsPerOrb shards of the kind make. */
item_misc_id ShardCurrency(ShardKind kind);
/** @brief POE2 Mashup: the shard item of the kind (Curtis 2026-10-29: shards are items in the bag, like POE2's). */
item_misc_id ShardMiscId(ShardKind kind);
/** @brief The kind of shard an item of this misc id is, if it is a shard. */
std::optional<ShardKind> ShardKindForMiscId(int miscId);
/** @brief Shards of the kind in the hero's bag, all stacks together. */
int ShardsInBag(const Player &player, ShardKind kind);
/** @brief The biggest stack of the kind's shards in the hero's bag, 0 if none; it becomes an orb at ShardsPerOrb. */
int FullestShardStack(const Player &player, ShardKind kind);
/**
 * @brief Puts shard items in the hero's bag (at their feet when it's full), with a message saying where they are; a stack
 * that reaches ShardsPerOrb becomes an orb.
 * @return How many orbs they made.
 */
int GainShards(Player &player, ShardKind kind, int shards);
/** @brief Turns the shards an older hero had counted into shard items in their bag; a kind that doesn't fit stays counted. */
void ConvertCountedShards(Player &player);
/** @brief The shard's tooltip line, like POE2's: "A stack of 10 shards becomes an Orb of Chance". */
std::string ShardStackText(ShardKind kind);
/** @brief Disenchants the inventory item: it's destroyed for shard items, and a full stack becomes its orb. */
bool DisenchantInvItem(Player &player, int invIndex);
/** @brief Salvages the inventory item: it's destroyed for quality currency, socket shards and a unique's Chance Shards. */
bool SalvageInvItem(Player &player, int invIndex);
/** @brief "3 Blacksmith's Whetstones, 2 Artificer's Shards and 1 Chance Shard", or "nothing", for the anvil. */
std::string SalvageYieldText(const SalvageYield &yield);
std::string_view ShardName(ShardKind kind);

} // namespace devilution
