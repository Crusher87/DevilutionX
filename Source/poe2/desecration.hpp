/**
 * @file poe2/desecration.hpp
 *
 * POE2 Mashup desecration: an Ancient Bone adds an unrevealed modifier to a rare item, and the player
 * reveals it by picking one of three offers, some of them modifiers only desecration gives.
 */
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "items.h"
#include "poe2/unique_rules.hpp"

namespace devilution {

struct Player;

/**
 * @brief Crafted modifier families from here up are modifiers only desecration gives: above every modifier family in
 * memory. The narrow craft save layout stores them from NarrowDesecratedFamilyBase (craft_state.hpp).
 */
constexpr uint8_t DesecratedFamilyBase = 240;

enum class DesecratedModId : uint8_t {
	None,
	AshBeyondMeasure,
	HungerOfTheWound,
	BlackReservoir,
	ThirdEye,
	RedPursuit,
	ChoirOfTeeth,
	HollowSaint,
	UncleanPerfection,
	PredatorsMark,
	ClosedWound,
	Count,
};

struct DesecratedMod {
	DesecratedModId id;
	std::string_view name;
	bool prefix;
	/** @brief Bits of the ModSlot values it can go on. */
	uint32_t slots;
	int minItemLevel;
	int weight;
	/** @brief What it does, as rules the player adds up; items it changes itself have none. */
	std::array<std::pair<UniqueRule, int16_t>, 2> rules;
	std::string_view text;
};

/** @brief Chance in percent that, at item level 65 and up, one offer is a modifier only desecration gives. */
constexpr int EndgameExclusiveOfferChance = 35;
constexpr int EndgameItemLevel = 65;
constexpr size_t DesecrationOfferCount = 3;
/** @brief Hollow Saint: more effect of the item's augments. */
constexpr int HollowSaintAugmentPercent = 135;

[[nodiscard]] bool IsDesecratedFamily(uint8_t family);
/** @brief The modifier only desecration gives, or nullptr for any other family. */
const DesecratedMod *GetDesecratedMod(uint8_t family);
[[nodiscard]] bool HasDesecratedMod(const Item &item, DesecratedModId id);
/** @brief Whether the item holds an unrevealed desecrated modifier. */
[[nodiscard]] bool HasUnrevealedMod(const Item &item);

/**
 * @brief Desecrates a rare item: adds an unrevealed modifier on a side an omen picks or a random one,
 * removing a modifier from that side when it's full.
 * @return false (with a message) if the item can't be desecrated.
 */
bool DesecrateCraft(const Item &item, Poe2Craft &craft, uint32_t seed, int forcedSide);

/**
 * @brief The three modifiers the unrevealed one can become, the same each time for the same item.
 * @param kinship With an Omen of Kinship, one offer shares a kind with the item's other modifiers.
 */
std::vector<Poe2CraftMod> DesecrationOffers(const Item &item, bool kinship);
/** @brief Replaces the unrevealed modifier with the chosen offer. */
bool RevealDesecratedMod(const Player &player, Item &item, const Poe2CraftMod &choice);
/** @brief The tooltip lines of an offer or of a crafted modifier only desecration gives. */
std::vector<std::string> DesecratedModText(const Item &item, const Poe2CraftMod &mod);

/** @brief Adds the rules of the item's desecrated modifiers to the totals. */
void AddDesecratedRules(const Item &item, UniqueRuleTotals &totals);
/** @brief Most modifiers the item can hold: one fewer with Hollow Saint. */
int CraftModLimit(const Item &item, int perSide);
/** @brief Most quality currency can give the item: 5% less with Unclean Perfection. */
int CraftQualityLimit(const Item &item);
/** @brief Percent of their values the item's explicit modifiers have from Unclean Perfection's quality scaling. */
int DesecratedModEffectPercent(const Item &item);

} // namespace devilution
