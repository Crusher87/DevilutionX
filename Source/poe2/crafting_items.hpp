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
#include <optional>
#include <span>
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
	// The runes rework's kinds. The twelve above are legacy: an item that has one gets the kind essences.tsv's
	// becomes column names when it loads (ConvertLegacyCraftingIds). Until the rework gives these their own names and
	// modifiers, each has those of the legacy kind that becomes it.
	Body,
	Mind,
	Insulation,
	Thawing,
	Grounding,
	Abrasion,
	Haste,
	Alacrity,
	Battle,
	Enhancement,
	Infinite,
	Command,
	/** Chaos resistance, from armour, belts, jewellery, shields and foci (chaos damage plan step 5). */
	Ruin,
	// The rest of the build spec's 19 families (stash step 3). Until step 7 gives them their modifiers, each row of
	// essences.tsv names the family of the nearest older row and never drops (its drops column is 0).
	Flames,
	Ice,
	Electricity,
	/** POE2's Essence of Sorcery; the legacy kind Sorcery above became Alacrity. */
	SpellSorcery,
	Seeking,
	Opulence,
	/** The seven Evil essences: one strength each, which their variants keep as Normal. */
	Terror,
	Hatred,
	Destruction,
	Lies,
	Sin,
	Pain,
	Anguish,
	Count,
};

/** @brief The essence kinds before the runes rework: the currency stash keeps a slot for each, holding what it became. */
constexpr int LegacyEssenceKindCount = static_cast<int>(EssenceKind::Summoning);
/** @brief The first Evil essence; it and the kinds after it have one strength. */
constexpr EssenceKind FirstEvilEssence = EssenceKind::Terror;
/**
 * @brief An essence's variant: the kind's low 4 bits, the tier in bits 4-5, the kind's 5th bit in bit 6 and its 6th in
 * bit 7, so 63 kinds fit and older variants read the same.
 */
static_assert(static_cast<int>(EssenceKind::Count) <= 64);
/** @brief Whether the kind is one of the seven Evil essences. */
[[nodiscard]] constexpr bool IsEvilEssence(EssenceKind kind)
{
	return kind >= FirstEvilEssence && kind < EssenceKind::Count;
}

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
	/** @brief A legacy kind: the kind it becomes when an item that has it loads. None for a kind of today. */
	EssenceKind becomes = EssenceKind::None;
	/** @brief Whether it drops; the kinds still waiting for their modifiers don't. */
	bool drops = false;
};

/** @brief POE2 drop level of each essence tier, its weight, and the lowest modifier level it adds. */
constexpr std::array<int, 4> EssenceDropLevels = { 20, 35, 55, 72 };
constexpr std::array<int, 4> EssenceWeights = { 100, 70, 35, 12 };
constexpr std::array<int, 4> EssenceModLevelFloors = { 0, 20, 35, 50 };

const Essence &GetEssence(EssenceKind kind);
EssenceKind GetEssenceKind(const Item &item);
EssenceTier GetEssenceTier(const Item &item);
uint8_t EssenceVariant(EssenceKind kind, EssenceTier tier);
/** @brief The kind an essence variant holds; None for one out of range. */
EssenceKind EssenceVariantKind(uint8_t variant);
EssenceTier EssenceVariantTier(uint8_t variant);
/** @brief The kind of today a kind stands for: what a legacy kind becomes, else the kind itself. */
EssenceKind CurrentEssenceKind(EssenceKind kind);

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
	// The runes rework's ids. The ten above are legacy: a socket or an item that has one gets the id augments.tsv's
	// becomes column names when it loads (ConvertLegacyCraftingIds). Until the rework gives these their own names and
	// numbers, each has those of the legacy id that becomes it.
	LesserRal,
	LesserThul,
	LesserOrt,
	LesserOhm,
	LesserShael,
	LordOfThePit,
	TheDefiler,
	StareyeTheWitch,
	RelicOfRakkis,
	RelicOfAkarat,
	/** Chaos resistance in every slot (chaos damage plan step 5). */
	VenomRune,
	// The full sets of the build spec's steps 5 and 6 (stash step 3). Until steps 5 and 6 give them their numbers, each
	// row of augments.tsv copies those of the nearest older row and never drops (its drops column is 0).
	// The rune families' other strengths: a family is Lesser, plain, Greater and Perfect (Ith has no Lesser).
	Ral,
	GreaterRal,
	PerfectRal,
	Thul,
	GreaterThul,
	PerfectThul,
	Ort,
	GreaterOrt,
	PerfectOrt,
	Ohm,
	GreaterOhm,
	PerfectOhm,
	LesserAmn,
	Amn,
	GreaterAmn,
	PerfectAmn,
	LesserVex,
	Vex,
	GreaterVex,
	PerfectVex,
	LesserIo,
	Io,
	GreaterIo,
	PerfectIo,
	LesserTir,
	Tir,
	GreaterTir,
	PerfectTir,
	LesserSol,
	Sol,
	GreaterSol,
	PerfectSol,
	LesserGul,
	Gul,
	GreaterGul,
	PerfectGul,
	LesserFal,
	Fal,
	GreaterFal,
	PerfectFal,
	LesserKo,
	Ko,
	GreaterKo,
	PerfectKo,
	LesserLum,
	Lum,
	GreaterLum,
	PerfectLum,
	Ith,
	GreaterIth,
	PerfectIth,
	Zod,
	// The Runes of Legend.
	TalicsRune,
	MadawcsRune,
	KorlicsRune,
	BulKathosRune,
	TalRashasRune,
	HorazonsRune,
	JeredCainsRune,
	ZoltunKullesRune,
	// Soulstones that add levels to a skill type.
	ButcherSoulstone,
	LeoricSoulstone,
	LazarusSoulstone,
	DoomcloudSoulstone,
	BoneheadSoulstone,
	WarlordSoulstone,
	BloodmoonSoulstone,
	NaKrulSoulstone,
	HorkDemonSoulstone,
	// The other soulstones.
	ZharSoulstone,
	SnotspillSoulstone,
	GharbadSoulstone,
	WitchfireSoulstone,
	BlackJadeSoulstone,
	// Greater soulstones (LordOfThePit, TheDefiler and StareyeTheWitch above are three more).
	Plaguewrath,
	NightwingTheCold,
	Stormbane,
	WrathfireTheDoomed,
	Dreadjudge,
	Bloodlust,
	GutshankTheQuick,
	Blackskull,
	HowlingireTheShade,
	BronzefistFirestone,
	Shadowcrow,
	Zamphir,
	// Horadric soulstones.
	TalRashasSoulstone,
	JeredCainsSoulstone,
	ZoltunKullesSoulstone,
	Count,
};

/** @brief The augment ids before the runes rework: the currency stash keeps a slot for each, holding what it became. */
constexpr int LegacyAugmentCount = static_cast<int>(AugmentId::GuardianTalisman);
/** @brief A socket of the wide craft layout keeps 8 bits, the top one spare for a Zod Rune, so ids stay below 128. */
static_assert(static_cast<int>(AugmentId::Count) <= 128);

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
	/** @brief A legacy id: the id it becomes when an item that has it loads. None for an id of today. */
	AugmentId becomes = AugmentId::None;
	/** @brief Whether it drops; the ids still waiting for their numbers don't. */
	bool drops = false;
};

const Augment &GetAugment(AugmentId id);
AugmentId GetAugmentId(const Item &item);
/** @brief The id of today an id stands for: what a legacy id becomes, else the id itself. */
AugmentId CurrentAugmentId(AugmentId id);
/**
 * @brief Gives a loading item the ids of today in place of legacy ones (an augment or essence it is, and the augments
 * in its sockets), and gives an augment or essence the name its row has today. Saves from before the runes rework hold
 * only legacy ids.
 * @return Whether it changed the item's ids.
 */
bool ConvertLegacyCraftingIds(Item &item);
/** @brief Most augment sockets the item can have: 2 on body armour and two-handed weapons, 1 on other weapons and armour, 0 on jewellery. */
int MaxAugmentSockets(const Item &item);
/** @brief Gives the item what its socketed augments do. */
void ApplyAugments(Item &item);
/** @brief The tooltip lines of the item's sockets. */
std::vector<std::string> AugmentSocketText(const Item &item);

/** @brief The rune families, in the order of the Runes tab's rows (Diablo 2 names, Curtis 2026-10-21). */
enum class RuneFamily : uint8_t {
	Ral,
	Thul,
	Ort,
	Ohm,
	Amn,
	Vex,
	Io,
	Tir,
	Sol,
	Gul,
	Fal,
	Ko,
	Lum,
	Ith,
	Shael,
	Zod,
	/** The chaos damage plan's rune, which has no family in the spec. */
	Venom,
	Count,
};

/** @brief A rune's strength, the Runes tab's columns: Lesser, plain, Greater and Perfect, as essence tiers are. */
using RuneStrength = EssenceTier;

struct RuneKind {
	RuneFamily family;
	RuneStrength strength;
};

/** @brief The rune's family and strength; none for soul cores, soulstones, relics and Runes of Legend. */
[[nodiscard]] std::optional<RuneKind> GetRuneKind(AugmentId id);
/** @brief The rune of the family at the strength; None where the family has no such strength (Ith has no Lesser). */
[[nodiscard]] AugmentId RuneOf(RuneFamily family, RuneStrength strength);
std::string_view RuneFamilyName(RuneFamily family);
/** @brief Whether the id is a Rune of Legend. */
[[nodiscard]] bool IsRuneOfLegend(AugmentId id);
constexpr AugmentId FirstRuneOfLegend = AugmentId::TalicsRune;
constexpr int RunesOfLegendCount = 8;
/** @brief The soulstones and relics, in the order of the Runes tab's third page. */
[[nodiscard]] std::span<const AugmentId> SoulstonesAndRelics();
/** @brief The Essences tab's family order (the build spec's step 7 list): 19 families of 4 strengths. */
[[nodiscard]] std::span<const EssenceKind> EssenceFamilies();

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
