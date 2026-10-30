/**
 * @file poe2/modifiers.hpp
 *
 * POE2 Mashup modifier engine: magic and rare items roll their affixes from the itemization tables
 * (modifier families with T8 to T1 tiers gated by item level), like POE2.
 */
#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
	/** Added to the item's defence, split between its armour, evasion and energy shield. */
	FlatDefense,
	/** Added to the item's defence, but to its armour, evasion or energy shield only (energy shield in defence points). */
	FlatArmour,
	FlatEvasion,
	FlatEnergyShield,
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
	/** Increased rarity of items found, from the Gold Amulet and the Magic Find prefix and suffix. */
	ItemRarity,
	/** Increased spell damage in percent, from staves (plan step 14); adds to the tree's spell damage. */
	SpellDamage,
	// The caster weapons rework's effects.
	/** Increased Spirit of the sceptre itself, in percent of its +100: MaxSpirit reads it per hand. */
	LocalSpiritPercent,
	/** LocalSpiritPercent and maximum mana. */
	SpiritAndMana,
	/** SpellDamage and maximum mana. */
	SpellDamageAndMana,
	/** Increased spell damage of one element, in percent; it adds to the spell's increased damage as the tree's element damage does. Cold is magic. */
	FireSpellDamage,
	ColdSpellDamage,
	LightningSpellDamage,
	PhysicalSpellDamage,
	ColdSkillLevels,
	PhysicalSkillLevels,
	/** Levels of the minion Spirit gems. */
	MinionSkillLevels,
	/** Increased maximum life of minions, in percent. */
	MinionLife,
	/** Added to minions' fire, lightning and magic resistances. */
	MinionResistances,
	LifeOnKill,
	ManaOnKill,
	/** A quarterstaff's block chance, in percent. */
	BlockChance,
	/** A quarterstaff's chance to Daze, in percent: the hit's stun buildup doubles. */
	DazeChance,
	/** Flat damage added to minions' blows, a range. */
	MinionAddedDamage,
	/** Minions' life regeneration, in tenths of a point a second. */
	MinionLifeRegen,
	/** Not an effect: the number of them. New effects go before it. */
	Count,
};

/** @brief Room for one bit per ModEffect in a ModEffectMask. */
constexpr size_t MaxModEffects = 128;
/** @brief A set of effects, one bit per ModEffect (ModEffectBits). */
using ModEffectMask = std::bitset<MaxModEffects>;
static_assert(static_cast<size_t>(ModEffect::Count) <= MaxModEffects, "ModEffectMask holds one bit per effect");

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
	/**
	 * The staves and sceptres from before the caster weapons rework (and Diablo's staves): every family they rolled
	 * before lists these, so they keep their pools. Staff, Wand and Sceptre mean the rework's new bases.
	 */
	LegacyStaff,
	LegacySceptre,
	Count,
};

/** @brief Defence tags of armour bases that some families need (and the element tags below, of caster weapons). */
enum ModBaseTag : uint16_t {
	ModBaseArmour = 1 << 0,
	ModBaseEvasion = 1 << 1,
	ModBaseEnergyShield = 1 << 2,
	ModBaseArmourEvasion = 1 << 3,
	ModBaseArmourEnergyShield = 1 << 4,
	ModBaseEvasionEnergyShield = 1 << 5,
};

/** @brief Element tags of the caster weapons rework's bases (BaseMeta::elementLock), for their element spell modifiers. */
constexpr uint16_t ModBaseFireSpell = 1 << 6;
constexpr uint16_t ModBaseColdSpell = 1 << 7;
constexpr uint16_t ModBaseLightningSpell = 1 << 8;
constexpr uint16_t ModBasePhysicalSpell = 1 << 9;
constexpr uint16_t ModBaseElementSpellBits = ModBaseFireSpell | ModBaseColdSpell | ModBaseLightningSpell | ModBasePhysicalSpell;

/** @brief The weakest tier a family can have: POE2's caster families go to T11, and a craft keeps a tier in 4 bits. */
constexpr int MaxModTier = 15;

struct ModTier {
	/** @brief 1 is the strongest; the bundle's families go to 8, POE2's caster families further (MaxModTier). */
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
	uint16_t baseTags;
	/** @brief Families that can't be on the same item, both ways. */
	std::vector<uint8_t> conflicts;
	/** @brief Tooltip text, with {0} and {1} for the values. */
	std::string text;
	ModEffect effect;
	/** @brief Percent of the table's value the game uses, per stat. */
	std::array<int16_t, 2> scale;
	/**
	 * @brief The scale items saved before the bases took POE2's numbers rolled with (harder monsters plan, step 10b),
	 * so such an item rebuilds with the modifiers it had. The same as scale for most families.
	 */
	std::array<int16_t, 2> legacyScale;
	bool enabled;
	/** @brief Ordered T1 first. */
	std::vector<ModTier> tiers;
};

/**
 * @brief The modifier families before the caster weapons rework, ids 1 to this: the families it appends come after them,
 * and an item saved before then has only these.
 */
constexpr uint8_t LegacyModFamilyCount = 94;
/** @brief Families in memory stay below the desecrated families' ids (desecration.hpp). */
constexpr size_t MaxModFamilies = 239;

/** @brief POE2: a magic item has at most one prefix and one suffix, a rare three of each. */
constexpr int MaxMagicModsPerSide = 1;
constexpr int MaxRareModsPerSide = 3;
/** @brief A rare jewel has at most two prefixes and two suffixes. */
constexpr int MaxRareJewelModsPerSide = 2;

/** @brief Loads the modifier tables and the item bases (poe2/bases.hpp) and uniques (poe2/uniques.hpp) from the POE2 Mashup mod, or clears them outside POE2 mode. */
void LoadModifierData();

/** @brief Whether modifiers roll with the families' legacy scales (LegacyModScales). */
bool RollingLegacyModScales();

/**
 * @brief While one exists, modifiers roll with the scales they had before the bases took POE2's numbers, for
 * rebuilding an item saved before then (RestoreRareItemPowers, RebuildWithCraft). Nests; never touches the game's
 * random number generator.
 */
class LegacyModScales {
public:
	explicit LegacyModScales(bool active = true);
	~LegacyModScales();
	LegacyModScales(const LegacyModScales &) = delete;
	LegacyModScales &operator=(const LegacyModScales &) = delete;

private:
	bool previous_;
};
const std::vector<ModFamily> &ModifierFamilies();
std::optional<uint8_t> FindModifierFamily(std::string_view id);
/**
 * @brief The family an essence or a Profane modifier naming @p id gives an item of the slot: that family when it lists
 * the slot (every item from before the caster weapons rework), else the rework's first family of the same effect that
 * does (cast_speed on a wand: wand_cast_speed). With neither, the named family, which the slot can't take, as before.
 */
std::optional<uint8_t> FindFamilyForItem(std::string_view id, ModSlot slot);
std::optional<uint8_t> FindFamilyForItem(std::string_view id, const Item &item);

/** @brief Whether the item is a POE2 jewel, for the passive tree's jewel sockets. */
bool IsJewel(const Item &item);

/** @brief The slot of the itemization tables an item fills, if it has one. */
std::optional<ModSlot> ItemModSlot(const Item &item);
/**
 * @brief The defence tags of an armour base; a caster weapon of the rework's element tags (its lock, or all four);
 * 0 for other items.
 */
uint16_t ItemModBaseTags(const Item &item);
/**
 * @brief The base tags left for the next modifier once these are rolled: the first element modifier closes the other
 * elements (first-element exclusivity). The same tags when there are no element tags.
 */
uint16_t NarrowedModBaseTags(uint16_t baseTags, const std::vector<Poe2ItemMod> &rolled);

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
std::vector<Poe2ItemMod> RollItemMods(ModSlot slot, uint16_t baseTags, int itemLevel, int count, int perSide, uint32_t seed, uint32_t valueSeed, int unidentifiedTier = 1);

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
	/** @brief Effects the item already has from its implicits and modifiers, as ModEffectBits; crafting doesn't repeat them. */
	ModEffectMask avoidEffects;
};

/** @brief The effect as its bit of a ModEffectMask, a hybrid as its two parts; none for None. */
ModEffectMask ModEffectBits(ModEffect effect);
/** @brief The ModEffectBits of every one of the modifiers. */
ModEffectMask ModsEffectBits(const std::vector<Poe2ItemMod> &mods);

/**
 * @brief Picks one more modifier for an item that has these, like an Exalted Orb; its values are left at 0.
 * @param seed Seeds the pick, without touching the game's random number generator.
 */
std::optional<Poe2ItemMod> RollAddedMod(ModSlot slot, uint16_t baseTags, int itemLevel, const std::vector<Poe2ItemMod> &existing, int perSide, uint32_t seed, const ModRollFilter &filter);
/** @brief Rolls the values of a modifier's tier from a seed of its own. */
std::array<int16_t, 2> RollModValues(uint8_t family, uint8_t tier, uint32_t valueSeed);
/** @brief The item level a modifier's tier needs; 0 if there is no such tier. */
int ModTierItemLevel(uint8_t family, uint8_t tier);
/** @brief Whether two modifiers are of the same kind (life, defence, damage...). */
bool ModsShareTag(const Poe2ItemMod &a, const Poe2ItemMod &b);

/** @brief The roll RareModCountFromRoll takes is below this. */
constexpr int RareModCountRolls = 24;
/**
 * @brief A natural rare's modifier count from a roll below RareModCountRolls: 3 to 6 as in POE2, weighted 3:13:6:2
 * so most have 4 or more. The roll modulo 12 is the old 4-to-6 roll (8:3:1), so rares from before keep their 5 or
 * 6 modifiers and only some of the 4s lose their last one.
 */
constexpr int RareModCountFromRoll(int roll)
{
	const int oldRoll = roll % 12;
	if (oldRoll < 8)
		return roll >= 12 && oldRoll < 3 ? 3 : 4;
	return oldRoll < 11 ? 5 : 6;
}
/** @brief The number of modifiers a natural rare rolls (RareModCountFromRoll). */
int RareModCount(uint32_t seed);
/** @brief The number of modifiers a magic item rolls: 1 or 2. */
int MagicModCount(uint32_t seed);

/** @brief Changes the item by the modifier: base values, its own stats, or the POE2 stats the player adds up. */
void ApplyItemMod(Item &item, const Poe2ItemMod &mod);
/** @brief Changes the item by an effect with its values, for modifiers and base implicits alike. */
void ApplyModEffect(Item &item, ModEffect effect, int v0, int v1);

/** @brief The modifier's tooltip lines; its tier shows only in the Alt view. */
std::vector<std::string> ItemModText(const Poe2ItemMod &mod);
/**
 * @brief The tooltip lines of all the item's modifiers. Like POE2, a stat more than one modifier gives shows once with
 * the values added up: a hybrid's "12% increased Energy Shield" and a plain "29% increased Energy Shield" read
 * "41% increased Energy Shield". The Alt view keeps them apart, by affix.
 */
std::vector<std::string> CombinedItemModText(std::span<const Poe2ItemMod> mods);
/**
 * @brief The modifier's stats as POE2 shows them with Alt held: the tier's range after the rolled value, like
 * "+8(3-10) to Armour". The tooltip adds its side (Prefix or Suffix) and tier beside the lines.
 */
std::vector<std::string> ItemModAdvancedText(const Poe2ItemMod &mod);
/**
 * @brief One affix's lines in the Alt view, "side\tstat\ttier": the side ("Prefix" or "Suffix") once, on its first
 * line, so the labels count the affixes, and the tier ("T3") once, on its last. A line with no side continues the
 * affix above it: the tooltip joins a two-part (hybrid) affix's lines with a bracket.
 */
std::vector<std::string> AffixViewLines(std::string_view side, const std::vector<std::string> &stats, int tier);

/** @brief The family of a rolled modifier, or nullptr for an empty one. */
const ModFamily *ItemModFamily(const Poe2ItemMod &mod);

/** @brief The item's name with the words of its prefix and suffix, for a magic item. */
std::string MagicModItemName(const Item &item, std::string_view baseName);

} // namespace devilution
