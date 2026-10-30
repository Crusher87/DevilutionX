/**
 * @file poe2/crafting_items.hpp
 *
 * POE2 Mashup crafting items that come in kinds: essences, omens, augments (runes, soul cores and talismans)
 * and catalysts. Each kind shares one item base and keeps which one it is in its create flags. The essences, omens
 * and augments come from txtdata/poe2/essences.tsv, omens.tsv and augments.tsv, one row per id in id order.
 */
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "poe2/modifiers.hpp"
#include "poe2/unique_rules.hpp"

namespace devilution {

struct Item;
struct Poe2Craft;

/** @brief On an essence, omen, augment or catalyst: which one it is. */
constexpr uint32_t CF_POE2_VARIANT = 0xFFU << 16;

uint8_t CraftingItemVariant(const Item &item);
void SetCraftingItemVariant(Item &item, uint8_t variant);
/** @brief Whether the item is an essence, omen, augment or catalyst. */
[[nodiscard]] bool IsCraftingVariantItem(const Item &item);
/** @brief Names the item after its kind, rolling a kind from its seed if it has none. */
void FinishCraftingItem(Item &item);
/** @brief Tooltip lines saying what the essence, omen, augment or catalyst does. */
std::vector<std::string> CraftingItemText(const Item &item);
/** @brief Reads the essence, omen and augment tables; LoadModifierData calls it. They are empty outside POE2 mode. */
void LoadCraftingItemData();

enum class EssenceKind : uint8_t {
	None,
	Vigor,
	Clarity,
	Ember,
	Frost,
	Storm,
	Might,
	Celerity,
	Sorcery,
	Precision,
	Warding,
	Spirit,
	Summoning,
	Count,
};

enum class EssenceTier : uint8_t {
	Lesser,
	Normal,
	Greater,
	Perfect,
	Count,
};

struct Essence {
	EssenceKind kind = EssenceKind::None;
	std::string name;
	/** @brief The modifier family it guarantees. */
	std::string family;
};

/** @brief POE2 drop level of each essence tier, its weight, and the lowest modifier level it adds. */
constexpr std::array<int, 4> EssenceDropLevels = { 20, 35, 55, 72 };
constexpr std::array<int, 4> EssenceWeights = { 100, 70, 35, 12 };
constexpr std::array<int, 4> EssenceModLevelFloors = { 0, 20, 35, 50 };

const Essence &GetEssence(EssenceKind kind);
EssenceKind GetEssenceKind(const Item &item);
EssenceTier GetEssenceTier(const Item &item);
uint8_t EssenceVariant(EssenceKind kind, EssenceTier tier);

enum class OmenId : uint8_t {
	None,
	SinistralExaltation,
	DextralExaltation,
	GreaterExaltation,
	HomogenisingExaltation,
	DiscordantExaltation,
	SinistralAnnulment,
	DextralAnnulment,
	Whittling,
	SinistralErasure,
	DextralErasure,
	SinistralPreservation,
	DextralPreservation,
	SinistralCrystallisation,
	DextralCrystallisation,
	Severance,
	Mercy,
	Corruption,
	Artificer,
	SinistralNecromancy,
	DextralNecromancy,
	Kinship,
	Count,
};

/** @brief The crafting action an omen changes; it is used up when that action happens. */
enum class OmenTrigger : uint8_t {
	Exalt,
	Annul,
	Chaos,
	PerfectEssence,
	Chance,
	Corrupt,
	Desecrate,
};

struct Omen {
	OmenId id = OmenId::None;
	std::string name;
	OmenTrigger trigger = OmenTrigger::Exalt;
	int dropLevel = 0;
	int weight = 0;
	std::string description;
};

const Omen &GetOmen(OmenId id);
OmenId GetOmenId(const Item &item);

enum class AugmentId : uint8_t {
	None,
	EmberRune,
	FrostRune,
	StormRune,
	IronRune,
	WindRune,
	HeartCore,
	MindCore,
	KeenCore,
	HunterTalisman,
	GuardianTalisman,
	Count,
};

struct Augment {
	AugmentId id = AugmentId::None;
	std::string name;
	/** @brief 1 for runes, 2 for soul cores, 3 for talismans; an item takes one talisman. */
	int tier = 0;
	int dropLevel = 0;
	/** @brief What it does in a martial weapon, in a caster weapon (a staff, wand or sceptre of the rework), and in armour. */
	ModEffect weaponEffect = ModEffect::None;
	std::array<int16_t, 2> weaponValues {};
	ModEffect staffEffect = ModEffect::None;
	std::array<int16_t, 2> staffValues {};
	ModEffect armourEffect = ModEffect::None;
	std::array<int16_t, 2> armourValues {};
	/** @brief A rule it adds anywhere instead, for talismans. */
	UniqueRule rule = UniqueRule::None;
	int16_t ruleValue = 0;
	std::string weaponText;
	std::string staffText;
	std::string armourText;
};

const Augment &GetAugment(AugmentId id);
AugmentId GetAugmentId(const Item &item);
/** @brief Most augment sockets the item can have: 2 on body armour and two-handed weapons, 1 on other weapons and armour, 0 on jewellery. */
int MaxAugmentSockets(const Item &item);
/** @brief Gives the item what its socketed augments do. */
void ApplyAugments(Item &item);
/** @brief The tooltip lines of the item's sockets. */
std::vector<std::string> AugmentSocketText(const Item &item);

enum class Catalyst : uint8_t {
	None,
	Vital,
	Arcane,
	Prismatic,
	Martial,
	Swift,
	Critical,
	Attribute,
	Spiritual,
	Count,
};

/** @brief Enchantments a Vaal Orb can give, by item class (bundle corruption_rules). */
enum class CorruptionEnchant : uint8_t {
	None,
	AttackSpeed,
	CriticalDamage,
	PhysicalAsFire,
	WeaponSkillLevel,
	LifePercent,
	MaxResistance,
	MoreDefence,
	ShieldSkillLevel,
	AllResistances,
	Attributes,
	Count,
};

/** @brief The enchantments the item can roll, with their weights, for its class and POE2 item level. */
std::vector<std::pair<CorruptionEnchant, int>> CorruptionEnchantChoices(const Item &item);
/** @brief Gives the item what its corruption enchantment does. */
void ApplyCorruptionEnchant(Item &item);
/** @brief The enchantment's tooltip line, or an empty string. */
std::string CorruptionEnchantText(const Item &item);
/** @brief Adds the rules of the item's talismans and enchantment to the totals. */
void AddCraftRules(const Item &item, UniqueRuleTotals &totals);

std::string_view CatalystName(Catalyst catalyst);
Catalyst GetCatalyst(const Item &item);
/** @brief Whether a catalyst's quality boosts the modifier family. */
[[nodiscard]] bool CatalystBoosts(Catalyst catalyst, ModEffect effect);

} // namespace devilution
