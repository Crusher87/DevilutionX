/**
 * @file poe2/belt_uniques.hpp
 *
 * POE2 Mashup unique flasks and charms from the itemization bundle (03_Unique_Item_Data). Each is a normal
 * flask or charm base marked with its unique in the item's create flags; its effects work from the belt.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "poe2/loot.hpp"
#include "poe2/unique_rules.hpp"
#include "utils/fixed_point.hpp"

namespace devilution {

struct Item;
struct Player;
enum class CharmEffect : uint8_t;

/** @brief On a flask or charm, which belt unique it is (BeltUniqueId); 0 for none. Equipment uses these bits for crafting. */
constexpr uint32_t CF_POE2_BELT_UNIQUE = ((1U << 5) - 1) << 6;

enum class BeltUniqueId : uint8_t {
	None,
	RedMercy,
	LastDraught,
	SaintsBlood,
	BorrowedBreath,
	LongRecovery,
	Heartglass,
	GodsLastBreath,
	BlueSilence,
	ScholarsReserve,
	DeepWell,
	BorrowedThought,
	LastIdea,
	Moonwater,
	LittleGuardian,
	SecondBell,
	QuietWard,
	Witchglass,
	SaintsToken,
	LastWarning,
	FrozenStep,
	Count,
};

enum class BeltUniqueKind : uint8_t {
	LifeFlask,
	ManaFlask,
	Charm,
};

/** @brief Heartglass saves the hero from death once a minute when its flask is at least 80% full. */
constexpr int HeartglassChargePercent = 80;
constexpr int HeartglassCooldownTicks = 60 * 20;
/** @brief Saint's Token activates while empty, then rests for 30 seconds. */
constexpr int SaintsTokenCooldownTicks = 30 * 20;
/** @brief Witchglass: 15% more damage for 3 seconds when it activates. */
constexpr int WitchglassDamagePercent = 15;
constexpr int WitchglassTicks = 3 * 20;
/** @brief Little Guardian keeps 40% of its resistance for 3 seconds after it ends. */
constexpr int LittleGuardianPercent = 40;
constexpr int LittleGuardianTicks = 3 * 20;
/** @brief Scholar's Reserve: 1% spell damage per 20 mana over the maximum, up to 25%, for 6 seconds. */
constexpr int ScholarsReserveManaPerPercent = 20;
constexpr int ScholarsReserveMaxPercent = 25;
constexpr int ScholarsReserveTicks = 6 * 20;

struct BeltUnique {
	BeltUniqueId id;
	std::string_view name;
	BeltUniqueKind kind;
	/** @brief For charms: which charm base it is on. */
	CharmEffect charm;
	/** @brief For flasks: its flask tier, the one a plain flask of its drop level has (FlaskTierForLevel). */
	uint8_t flaskTier;
	UniqueTier tier;
	/** @brief POE2 drop level. */
	int dropLevel;
	/** @brief "global", or the boss whose table holds it. */
	std::string_view pool;
	/** @brief The rolled "increased Flask Effect" or "increased Charm Duration", or 0 for none. */
	int16_t effectMin;
	int16_t effectMax;
	/** @brief Rules it adds while in the belt (boss uniques), with a value each. */
	std::vector<std::pair<UniqueRule, int16_t>> rules;
	/** @brief Resistance to fire, lightning and magic it adds while in the belt, rolled between these. */
	int16_t resistMin;
	int16_t resistMax;
	std::vector<std::string_view> lines;

	[[nodiscard]] bool isGlobal() const { return pool == "global"; }
};

const std::vector<BeltUnique> &BeltUniques();
const BeltUnique *GetBeltUnique(BeltUniqueId id);
const BeltUnique *FindBeltUnique(std::string_view name);
/** @brief The belt unique the flask or charm is, or nullptr. */
const BeltUnique *GetBeltUnique(const Item &item);
[[nodiscard]] bool IsBeltUnique(const Item &item, BeltUniqueId id);
/** @brief The item base a belt unique is made on. */
std::optional<int> BeltUniqueBase(const BeltUnique &unique);
/** @brief Marks the flask or charm as the belt unique: its name, rarity and flags. */
void MakeBeltUnique(Item &item, BeltUniqueId id);
/** @brief Names a flask or charm that is a belt unique. @return Whether it is one. */
bool FinishBeltUnique(Item &item);
/** @brief The rolled increased flask effect or charm duration, in percent. */
int BeltUniqueEffect(const Item &item);
/** @brief Resistance the belt unique adds while in the belt. */
int BeltUniqueResistance(const Item &item);
/** @brief The tooltip lines of a belt unique. */
std::vector<std::string> BeltUniqueText(const Item &item);
/** @brief Adds the rules of a belt unique in the belt to the totals. */
void AddBeltUniqueRules(const Item &item, UniqueRuleTotals &totals);

/** @brief Charm uses (activations) cost this percent of the usual charges. */
[[nodiscard]] int BeltUniqueChargePercent(const Item &item);
/** @brief Heartglass: lethal damage leaves the hero at 1 life instead. @return Whether it saved the hero. */
bool TryHeartglassSave(Player &player);

} // namespace devilution
