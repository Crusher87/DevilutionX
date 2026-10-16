/**
 * @file poe2/modifiers.hpp
 *
 * POE2 Mashup modifier engine: magic and rare items roll their affixes from the itemization tables
 * (modifier families with T8 to T1 tiers gated by item level), like POE2.
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace devilution {

struct Item;
struct Player;
struct Poe2ItemMod;

/** @brief What a modifier family does to the item. */
enum class ModEffect : uint8_t {
	None,
	Life,
	Mana,
	/** Added to the item's armour, evasion or energy shield. */
	FlatDefense,
	DefensePercent,
	LifeAndDefensePercent,
	ManaAndDefensePercent,
	Spirit,
	/** Added to the weapon's own damage. */
	AddedPhysical,
	PhysicalPercent,
	PhysicalPercentAndAccuracy,
	AddedFire,
	AddedLightning,
	MinionDamage,
	/** Added to every attack, from jewelry. */
	AddedPhysicalToAttacks,
	Strength,
	Dexterity,
	Intelligence,
	AllAttributes,
	FireResistance,
	MagicResistance,
	LightningResistance,
	AllResistances,
	AttackSpeed,
	CastSpeed,
	Accuracy,
	ReducedRequirements,
	LifeRegeneration,
	ManaRegeneration,
	MeleeSkillLevels,
	ProjectileSkillLevels,
	SpellSkillLevels,
	FireSkillLevels,
	LightningSkillLevels,
	/** Increased maximum life in percent, from jewels. */
	LifePercent,
	ManaPercent,
	/** Increased damage in percent, from jewels. */
	DamagePercent,
	/** Increased armour, evasion or energy shield from all gear, from jewels. */
	ArmourPercent,
	EvasionPercent,
	EnergyShieldPercent,
};

/** @brief The equipment slots of the itemization tables; not all of them exist in the game yet. */
enum class ModSlot : uint8_t {
	Helmet,
	BodyArmour,
	Gloves,
	Boots,
	Belt,
	Shield,
	Focus,
	Quiver,
	Ring,
	Amulet,
	OneHandMelee,
	TwoHandMelee,
	Bow,
	Crossbow,
	Spear,
	Quarterstaff,
	Flail,
	Claw,
	Dagger,
	Wand,
	Staff,
	Sceptre,
	Jewel,
	LifeFlask,
	ManaFlask,
	Charm,
	/** Jewel colours; each also rolls the modifiers every jewel can have. */
	Ruby,
	Emerald,
	Sapphire,
	Count,
};

/** @brief Defence tags of armour bases that some families need. */
enum ModBaseTag : uint8_t {
	ModBaseArmour = 1 << 0,
	ModBaseEvasion = 1 << 1,
	ModBaseEnergyShield = 1 << 2,
	ModBaseArmourEvasion = 1 << 3,
	ModBaseArmourEnergyShield = 1 << 4,
	ModBaseEvasionEnergyShield = 1 << 5,
};

struct ModTier {
	/** @brief 1 is the strongest, 8 the weakest. */
	uint8_t tier;
	uint8_t itemLevel;
	uint16_t weight;
	/** @brief Value ranges of the family's one or two stats, before scaling to the game. */
	std::array<int16_t, 2> min;
	std::array<int16_t, 2> max;
};

struct ModFamily {
	std::string id;
	std::string name;
	/** @brief Word a magic item's name takes from it: "Virile" before the base, "of the Brute" after it. */
	std::string word;
	bool prefix;
	std::string group;
	uint32_t slots;
	/** @brief The base needs one of these tags; 0 for any base. */
	uint8_t baseTags;
	/** @brief Families that can't be on the same item, both ways. */
	std::vector<uint8_t> conflicts;
	/** @brief Tooltip text, with {0} and {1} for the values. */
	std::string text;
	ModEffect effect;
	/** @brief Percent of the table's value the game uses, per stat. */
	std::array<int16_t, 2> scale;
	bool enabled;
	/** @brief Ordered T1 first. */
	std::vector<ModTier> tiers;
};

/** @brief POE2: a magic item has at most one prefix and one suffix, a rare three of each. */
constexpr int MaxMagicModsPerSide = 1;
constexpr int MaxRareModsPerSide = 3;
/** @brief A rare jewel has at most two prefixes and two suffixes. */
constexpr int MaxRareJewelModsPerSide = 2;

/** @brief Loads the modifier tables and the item bases (poe2/bases.hpp) and uniques (poe2/uniques.hpp) from the POE2 Mashup mod, or clears them outside POE2 mode. */
void LoadModifierData();
const std::vector<ModFamily> &ModifierFamilies();
std::optional<uint8_t> FindModifierFamily(std::string_view id);

/** @brief Whether the item is a POE2 jewel, for the passive tree's jewel sockets. */
bool IsJewel(const Item &item);

/** @brief The slot of the itemization tables an item fills, if it has one. */
std::optional<ModSlot> ItemModSlot(const Item &item);
/** @brief The defence tags of an armour base; 0 for other items. */
uint8_t ItemModBaseTags(const Item &item);

/** @brief Unidentified quality tiers: ground drops can roll a hidden tier 2 to 5 that culls weak modifiers, unlocked at these item levels. */
constexpr std::array<int, 5> UnidentifiedTierItemLevels = { 1, 20, 34, 52, 65 };
/** @brief Percent of each family's low tiers a magic item of that unidentified tier can't roll. */
constexpr std::array<int, 5> UnidentifiedMagicCullPercent = { 0, 50, 67, 80, 90 };
/** @brief Item level a rare's modifiers need at least at that unidentified tier. */
constexpr std::array<int, 5> UnidentifiedRareMinModLevel = { 0, 11, 23, 35, 48 };

/** @brief The hidden quality tier, 1 to 5, a ground drop rolls from its seed; higher tiers need a higher item level and are rarer. */
int UnidentifiedTier(uint32_t seed, int itemLevel);

/**
 * @brief Rolls an item's modifiers from its seed, without touching the game's random number generator.
 * @param count How many to roll; fewer if the pool runs dry.
 * @param valueSeed Seeds the value rolls apart from the picks, so a Divine Orb can reroll values only.
 * @param unidentifiedTier 1 for the full pool; higher tiers cull weak modifiers (see UnidentifiedTier).
 */
std::vector<Poe2ItemMod> RollItemMods(ModSlot slot, uint8_t baseTags, int itemLevel, int count, int perSide, uint32_t seed, uint32_t valueSeed, int unidentifiedTier = 1);

enum class ModSide : uint8_t {
	Any,
	Prefix,
	Suffix,
};

/** @brief How an added modifier's kind (life, defence, damage...) relates to the item's others, for omens. */
enum class ModTagRule : uint8_t {
	Any,
	Shared,
	NotShared,
};

/** @brief Limits on a modifier that crafting adds. */
struct ModRollFilter {
	ModSide side = ModSide::Any;
	/** @brief The lowest item level the modifier's tier may need: the floor of Greater and Perfect orbs. */
	int minModLevel = 0;
	/** @brief Only this family (index plus 1), for essences; 0 for any. */
	uint8_t family = 0;
	ModTagRule tags = ModTagRule::Any;
};

/**
 * @brief Picks one more modifier for an item that has these, like an Exalted Orb; its values are left at 0.
 * @param seed Seeds the pick, without touching the game's random number generator.
 */
std::optional<Poe2ItemMod> RollAddedMod(ModSlot slot, uint8_t baseTags, int itemLevel, const std::vector<Poe2ItemMod> &existing, int perSide, uint32_t seed, const ModRollFilter &filter);
/** @brief Rolls the values of a modifier's tier from a seed of its own. */
std::array<int16_t, 2> RollModValues(uint8_t family, uint8_t tier, uint32_t valueSeed);
/** @brief The item level a modifier's tier needs; 0 if there is no such tier. */
int ModTierItemLevel(uint8_t family, uint8_t tier);
/** @brief Whether two modifiers are of the same kind (life, defence, damage...). */
bool ModsShareTag(const Poe2ItemMod &a, const Poe2ItemMod &b);

/** @brief The number of modifiers a natural rare rolls: 4, 5 or 6, weighted 8:3:1. */
int RareModCount(uint32_t seed);
/** @brief The number of modifiers a magic item rolls: 1 or 2. */
int MagicModCount(uint32_t seed);

/** @brief Changes the item by the modifier: base values, its own stats, or the POE2 stats the player adds up. */
void ApplyItemMod(Item &item, const Poe2ItemMod &mod);
/** @brief Changes the item by an effect with its values, for modifiers and base implicits alike. */
void ApplyModEffect(Item &item, ModEffect effect, int v0, int v1);

/** @brief The modifier's tooltip lines, with its tier at the end. */
std::vector<std::string> ItemModText(const Poe2ItemMod &mod);
/**
 * @brief The modifier as POE2 shows it with Alt held: a header line with its side, name and tier, then each stat with
 * the tier's range after the rolled value, like "+8(3-10) to Armour".
 */
std::vector<std::string> ItemModAdvancedText(const Poe2ItemMod &mod);

/** @brief The family of a rolled modifier, or nullptr for an empty one. */
const ModFamily *ItemModFamily(const Poe2ItemMod &mod);

/** @brief The item's name with the words of its prefix and suffix, for a magic item. */
std::string MagicModItemName(const Item &item, std::string_view baseName);

} // namespace devilution
