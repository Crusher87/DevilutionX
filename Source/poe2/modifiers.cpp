#include "poe2/modifiers.hpp"

#include <algorithm>
#include <string_view>

#include <magic_enum/magic_enum.hpp>

#include "data/file.hpp"
#include "data/record_reader.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "poe2/area_levels.hpp"
#include "poe2/bases.hpp"
#include "poe2/crafting_items.hpp"
#include "poe2/default_attack.hpp"
#include "poe2/defenses.hpp"
#include "poe2/minion_gems.hpp"
#include "poe2/player_data.hpp"
#include "poe2/uniques.hpp"
#include "poe2/weapon_kinds.hpp"
#include "poe2/weapon_skills.hpp"
#include "tables/spelldat.h"
#include "utils/format.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/log.hpp"
#include "utils/str_cat.hpp"
#include "utils/str_split.hpp"

namespace devilution {

namespace {

std::vector<ModFamily> Families;

/** Set while an item saved before the bases took POE2's numbers rebuilds (LegacyModScales). */
bool UseLegacyScales = false;

constexpr std::array<std::string_view, static_cast<size_t>(ModSlot::Count)> SlotNames = {
	"helmet", "body_armour", "gloves", "boots", "belt", "shield", "focus", "quiver", "ring", "amulet",
	"one_hand_melee", "two_hand_melee", "bow", "crossbow", "spear", "quarterstaff", "flail", "claw", "dagger",
	"wand", "staff", "sceptre", "jewel", "life_flask", "mana_flask", "charm", "ruby", "emerald", "sapphire",
	"legacy_staff", "legacy_sceptre"
};

std::optional<int> ParseInt(std::string_view text)
{
	int value = 0;
	bool negative = false;
	size_t i = 0;
	if (!text.empty() && text[0] == '-') {
		negative = true;
		i = 1;
	}
	if (i == text.size())
		return std::nullopt;
	for (; i < text.size(); i++) {
		if (text[i] < '0' || text[i] > '9')
			return std::nullopt;
		value = (value * 10) + (text[i] - '0');
	}
	return negative ? -value : value;
}

uint32_t ParseSlots(std::string_view text)
{
	uint32_t slots = 0;
	for (const std::string_view name : SplitByChar(text, ',')) {
		const auto it = std::find(SlotNames.begin(), SlotNames.end(), name);
		if (it != SlotNames.end())
			slots |= 1U << static_cast<uint32_t>(it - SlotNames.begin());
	}
	return slots;
}

uint16_t ParseBaseTags(std::string_view text)
{
	uint16_t tags = 0;
	for (const std::string_view name : SplitByChar(text, ',')) {
		if (name == "armour_base")
			tags |= ModBaseArmour;
		else if (name == "evasion_base")
			tags |= ModBaseEvasion;
		else if (name == "energy_shield_base")
			tags |= ModBaseEnergyShield;
		else if (name == "armour_evasion_base")
			tags |= ModBaseArmourEvasion;
		else if (name == "armour_es_base")
			tags |= ModBaseArmourEnergyShield;
		else if (name == "evasion_es_base")
			tags |= ModBaseEvasionEnergyShield;
		else if (name == "fire_spell")
			tags |= ModBaseFireSpell;
		else if (name == "cold_spell")
			tags |= ModBaseColdSpell;
		else if (name == "lightning_spell")
			tags |= ModBaseLightningSpell;
		else if (name == "physical_spell")
			tags |= ModBasePhysicalSpell;
	}
	return tags;
}

bool LoadFamilies(std::vector<std::vector<std::string>> &conflictIds)
{
	constexpr std::string_view Filename = "txtdata\\poe2\\modifier_families.tsv";
	auto dataFile = DataFile::load(Filename);
	if (!dataFile.has_value()) {
		LogError("Couldn't load the modifier families: {}", Filename);
		return false;
	}
	dataFile->skipHeaderOrDie(Filename);
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		ModFamily family {};
		std::string text;
		reader.readString("id", family.id);
		reader.readString("name", family.name);
		reader.readString("word", family.word);
		reader.readString("side", text);
		family.prefix = text == "prefix";
		reader.readString("group", family.group);
		reader.readString("tags", text);
		reader.readString("slots", text);
		family.slots = ParseSlots(text);
		reader.readString("baseTags", text);
		family.baseTags = ParseBaseTags(text);
		reader.readString("conflicts", text);
		std::vector<std::string> conflicts;
		for (const std::string_view id : SplitByChar(text, ',')) {
			if (!id.empty())
				conflicts.emplace_back(id);
		}
		conflictIds.push_back(std::move(conflicts));
		reader.readString("template", family.text);
		reader.readString("effect", text);
		family.effect = magic_enum::enum_cast<ModEffect>(text).value_or(ModEffect::None);
		reader.readString("scale1", text);
		family.scale[0] = static_cast<int16_t>(ParseInt(text).value_or(100));
		reader.readString("scale2", text);
		family.scale[1] = static_cast<int16_t>(ParseInt(text).value_or(100));
		reader.readString("enabled", text);
		family.enabled = text == "true" && family.effect != ModEffect::None;
		reader.readString("legacyScale1", text);
		family.legacyScale[0] = static_cast<int16_t>(ParseInt(text).value_or(family.scale[0]));
		reader.readString("legacyScale2", text);
		family.legacyScale[1] = static_cast<int16_t>(ParseInt(text).value_or(family.scale[1]));
		reader.readString("natural", text);
		family.natural = text == "true";
		Families.push_back(std::move(family));
		if (Families.size() > MaxModFamilies) {
			LogError("Too many modifier families in {}", Filename);
			return false;
		}
	}
	return true;
}

bool LoadTiers()
{
	constexpr std::string_view Filename = "txtdata\\poe2\\modifier_tiers.tsv";
	auto dataFile = DataFile::load(Filename);
	if (!dataFile.has_value()) {
		LogError("Couldn't load the modifier tiers: {}", Filename);
		return false;
	}
	dataFile->skipHeaderOrDie(Filename);
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		std::string text;
		reader.readString("family", text);
		const std::optional<uint8_t> family = FindModifierFamily(text);
		if (!family) {
			LogError("Modifier tier for unknown family \"{}\"", text);
			continue;
		}
		ModTier tier {};
		const auto readInt = [&reader, &text](std::string_view field) {
			reader.readString(field, text);
			return ParseInt(text).value_or(0);
		};
		tier.tier = static_cast<uint8_t>(std::clamp(readInt("tier"), 1, MaxModTier));
		tier.itemLevel = static_cast<uint8_t>(std::clamp(readInt("itemLevel"), 1, 100));
		tier.weight = static_cast<uint16_t>(std::clamp(readInt("weight"), 0, static_cast<int>(UINT16_MAX)));
		tier.min[0] = static_cast<int16_t>(readInt("min1"));
		tier.max[0] = static_cast<int16_t>(readInt("max1"));
		tier.min[1] = static_cast<int16_t>(readInt("min2"));
		tier.max[1] = static_cast<int16_t>(readInt("max2"));
		Families[*family].tiers.push_back(tier);
	}
	for (ModFamily &family : Families) {
		std::sort(family.tiers.begin(), family.tiers.end(), [](const ModTier &a, const ModTier &b) { return a.tier < b.tier; });
	}
	return true;
}

/** @brief A small generator of its own, so rolling modifiers leaves the game's sequence alone. */
class ModRandom {
public:
	explicit ModRandom(uint32_t seed)
	    : state_(seed)
	{
	}

	uint32_t next()
	{
		state_ += 0x9E3779B9U;
		uint32_t value = state_;
		value ^= value >> 16;
		value *= 0x7FEB352DU;
		value ^= value >> 15;
		value *= 0x846CA68BU;
		value ^= value >> 16;
		return value;
	}

	/** @brief A number from 0 to range - 1. */
	uint32_t below(uint32_t range)
	{
		return range == 0 ? 0 : next() % range;
	}

	int between(int min, int max)
	{
		if (max <= min)
			return min;
		return min + static_cast<int>(below(static_cast<uint32_t>(max - min + 1)));
	}

private:
	uint32_t state_;
};

int16_t ScaleValue(int value, int scale)
{
	if (scale == 100 || value == 0)
		return static_cast<int16_t>(value);
	const int scaled = ((value * scale) + (value > 0 ? 50 : -50)) / 100;
	if (scaled == 0)
		return static_cast<int16_t>(value > 0 ? 1 : -1);
	return static_cast<int16_t>(scaled);
}

bool Conflicts(uint8_t family, const std::vector<Poe2ItemMod> &rolled)
{
	const ModFamily &candidate = Families[family];
	for (const Poe2ItemMod &mod : rolled) {
		// Desecration-only families (and anything a shorter table no longer has) aren't in Families.
		if (mod.family == 0 || mod.family > Families.size())
			continue;
		const uint8_t other = mod.family - 1;
		if (other == family || Families[other].group == candidate.group)
			return true;
		if (std::find(candidate.conflicts.begin(), candidate.conflicts.end(), other) != candidate.conflicts.end())
			return true;
	}
	return false;
}

void AddPoe2Stat(Item &item, Poe2ItemStat stat, int value)
{
	int16_t &total = item._iPoe2Stats[static_cast<size_t>(stat)];
	total = static_cast<int16_t>(std::clamp(total + value, static_cast<int>(INT16_MIN), static_cast<int>(INT16_MAX)));
}

/** Adds to an item's 16-bit weapon damage, clamped to the field. */
uint16_t AddToDamage(uint16_t base, int value)
{
	return static_cast<uint16_t>(std::clamp(base + value, 0, static_cast<int>(UINT16_MAX)));
}

} // namespace

bool RollingLegacyModScales()
{
	return UseLegacyScales;
}

LegacyModScales::LegacyModScales(bool active)
    : previous_(UseLegacyScales)
{
	UseLegacyScales = UseLegacyScales || active;
}

LegacyModScales::~LegacyModScales()
{
	UseLegacyScales = previous_;
}

void LoadModifierData()
{
	Families.clear();
	LoadBaseData();
	LoadPoe2Uniques();
	// Clears the table in Diablo mode and never fails there.
	LoadAreaLevels();
	// The same.
	LoadCraftingItemData();
	if (!gbIsPoe2)
		return;

	std::vector<std::vector<std::string>> conflictIds;
	if (!LoadFamilies(conflictIds) || !LoadTiers()) {
		Families.clear();
		return;
	}
	// The tables list most conflicts one way only; an item can't have both whichever rolled first.
	for (size_t i = 0; i < Families.size(); i++) {
		for (const std::string &id : conflictIds[i]) {
			const std::optional<uint8_t> other = FindModifierFamily(id);
			if (!other || *other == i)
				continue;
			Families[i].conflicts.push_back(*other);
			Families[*other].conflicts.push_back(static_cast<uint8_t>(i));
		}
	}
	for (ModFamily &family : Families) {
		std::sort(family.conflicts.begin(), family.conflicts.end());
		family.conflicts.erase(std::unique(family.conflicts.begin(), family.conflicts.end()), family.conflicts.end());
		if (family.tiers.empty())
			family.enabled = false;
	}
}

const std::vector<ModFamily> &ModifierFamilies()
{
	return Families;
}

std::optional<uint8_t> FindModifierFamily(std::string_view id)
{
	for (size_t i = 0; i < Families.size(); i++) {
		if (Families[i].id == id)
			return static_cast<uint8_t>(i);
	}
	return std::nullopt;
}

std::optional<uint8_t> FindFamilyForItem(std::string_view id, ModSlot slot)
{
	const std::optional<uint8_t> named = FindModifierFamily(id);
	const uint32_t slotBit = 1U << static_cast<uint32_t>(slot);
	if (!named || (Families[*named].slots & slotBit) != 0)
		return named;
	// Only the rework's families stand in, and they list only its new slots, so no older item's result changes.
	const ModEffect effect = Families[*named].effect;
	for (size_t i = LegacyModFamilyCount; i < Families.size(); i++) {
		if (Families[i].effect == effect && effect != ModEffect::None && (Families[i].slots & slotBit) != 0)
			return static_cast<uint8_t>(i);
	}
	return named;
}

std::optional<uint8_t> FindFamilyForItem(std::string_view id, const Item &item)
{
	const std::optional<ModSlot> slot = ItemModSlot(item);
	return slot ? FindFamilyForItem(id, *slot) : FindModifierFamily(id);
}

bool IsJewel(const Item &item)
{
	return !item.isEmpty() && item._iMiscId == IMISC_JEWEL;
}

std::optional<ModSlot> ItemModSlot(const Item &item)
{
	if (item.isEmpty() || item.IDidx < 0 || static_cast<size_t>(item.IDidx) >= AllItemsList.size())
		return std::nullopt;
	const ItemData &base = AllItemsList[static_cast<size_t>(item.IDidx)];
	if (IsJewel(item)) {
		const std::string_view name = base.iName;
		if (name.starts_with("Ruby"))
			return ModSlot::Ruby;
		if (name.starts_with("Emerald"))
			return ModSlot::Emerald;
		if (name.starts_with("Sapphire"))
			return ModSlot::Sapphire;
		return std::nullopt;
	}
	const std::string_view shortName = base.iSName;
	const bool twoHanded = item._iLoc == ILOC_TWOHAND;
	switch (item._itype) {
	case ItemType::Helm:
		return ModSlot::Helmet;
	case ItemType::LightArmor:
	case ItemType::MediumArmor:
	case ItemType::HeavyArmor:
		return ModSlot::BodyArmour;
	case ItemType::Shield:
		return shortName == "Focus" ? ModSlot::Focus : ModSlot::Shield;
	case ItemType::Ring:
		return ModSlot::Ring;
	case ItemType::Amulet:
		return ModSlot::Amulet;
	case ItemType::Bow:
		return shortName == "Crossbow" ? ModSlot::Crossbow : ModSlot::Bow;
	case ItemType::Staff:
	case ItemType::Mace:
		// The caster weapons rework's kinds: its new bases roll their own pools, the older staves and sceptres theirs.
		switch (WeaponKindOf(item)) {
		case WeaponKind::Quarterstaff:
			return ModSlot::Quarterstaff;
		case WeaponKind::CasterStaff:
			return ModSlot::Staff;
		case WeaponKind::Wand:
			return ModSlot::Wand;
		case WeaponKind::Sceptre:
			return ModSlot::Sceptre;
		case WeaponKind::LegacyStaff:
			return ModSlot::LegacyStaff;
		case WeaponKind::LegacySceptre:
			return ModSlot::LegacySceptre;
		default:
			break;
		}
		if (item._itype == ItemType::Staff)
			return shortName == "Quarterstaff" ? ModSlot::Quarterstaff : ModSlot::LegacyStaff;
		if (shortName == "Sceptre")
			return ModSlot::LegacySceptre;
		return twoHanded ? ModSlot::TwoHandMelee : ModSlot::OneHandMelee;
	case ItemType::Sword:
		if (shortName == "Dagger")
			return ModSlot::Dagger;
		return twoHanded ? ModSlot::TwoHandMelee : ModSlot::OneHandMelee;
	case ItemType::Axe:
		return twoHanded ? ModSlot::TwoHandMelee : ModSlot::OneHandMelee;
	default:
		return std::nullopt;
	}
}

uint16_t ItemModBaseTags(const Item &item)
{
	// A caster weapon of the rework rolls the element modifiers of its element, or of any element.
	if (IsCasterWeapon(item)) {
		const BaseMeta *base = ItemBaseMeta(item);
		return base != nullptr && base->elementLock != 0 ? base->elementLock : ModBaseElementSpellBits;
	}
	if (item._iClass != ICLASS_ARMOR)
		return 0;
	switch (ItemDefenseType(item)) {
	case DefenseType::Armour:
		return ModBaseArmour;
	case DefenseType::Evasion:
		return ModBaseEvasion;
	case DefenseType::EnergyShield:
		return ModBaseEnergyShield;
	case DefenseType::ArmourEvasion:
		return ModBaseArmour | ModBaseEvasion | ModBaseArmourEvasion;
	case DefenseType::ArmourEnergyShield:
		return ModBaseArmour | ModBaseEnergyShield | ModBaseArmourEnergyShield;
	case DefenseType::EvasionEnergyShield:
		return ModBaseEvasion | ModBaseEnergyShield | ModBaseEvasionEnergyShield;
	case DefenseType::All:
		return ModBaseArmour | ModBaseEvasion | ModBaseEnergyShield | ModBaseArmourEvasion | ModBaseArmourEnergyShield | ModBaseEvasionEnergyShield;
	}
	return 0;
}

uint16_t NarrowedModBaseTags(uint16_t baseTags, const std::vector<Poe2ItemMod> &rolled)
{
	if ((baseTags & ModBaseElementSpellBits) == 0)
		return baseTags;
	for (const Poe2ItemMod &mod : rolled) {
		const ModFamily *family = ItemModFamily(mod);
		if (family != nullptr && (family->baseTags & ModBaseElementSpellBits) != 0)
			baseTags &= static_cast<uint16_t>(family->baseTags | ~ModBaseElementSpellBits);
	}
	return baseTags;
}

int UnidentifiedTier(uint32_t seed, int itemLevel)
{
	// Chances of tier 5, 4, 3 and 2, each once its item level is reached.
	constexpr std::array<uint32_t, 5> Chances = { 0, 20, 10, 5, 2 };
	ModRandom random { seed ^ 0x1F83D9ABU };
	const uint32_t roll = random.below(100);
	for (int tier = 5; tier >= 2; tier--) {
		if (itemLevel >= UnidentifiedTierItemLevels[tier - 1] && roll < Chances[tier - 1])
			return tier;
	}
	return 1;
}

namespace {

struct ModCandidate {
	uint8_t family;
	uint8_t tierIndex;
	uint16_t weight;
};

uint32_t ModSlotBit(ModSlot slot)
{
	uint32_t slotBit = 1U << static_cast<uint32_t>(slot);
	if (IsAnyOf(slot, ModSlot::Ruby, ModSlot::Emerald, ModSlot::Sapphire))
		slotBit |= 1U << static_cast<uint32_t>(ModSlot::Jewel);
	return slotBit;
}

/** @brief Broad kinds of modifiers, for the omens that want a shared kind or a different one. */
uint8_t ModEffectTags(ModEffect effect)
{
	constexpr uint8_t Life = 1 << 0;
	constexpr uint8_t Mana = 1 << 1;
	constexpr uint8_t Defence = 1 << 2;
	constexpr uint8_t Damage = 1 << 3;
	constexpr uint8_t Attribute = 1 << 4;
	constexpr uint8_t Resistance = 1 << 5;
	constexpr uint8_t Speed = 1 << 6;
	constexpr uint8_t Skill = 1 << 7;
	switch (effect) {
	case ModEffect::Life:
	case ModEffect::LifeRegeneration:
	case ModEffect::LifePercent:
	case ModEffect::LifeOnKill:
	case ModEffect::MinionLife:
	case ModEffect::MinionLifeRegen:
		return Life;
	case ModEffect::Mana:
	case ModEffect::ManaRegeneration:
	case ModEffect::ManaPercent:
	case ModEffect::Spirit:
	case ModEffect::LocalSpiritPercent:
	case ModEffect::SpiritAndMana:
	case ModEffect::ManaOnKill:
		return Mana;
	case ModEffect::FlatDefense:
	case ModEffect::FlatArmour:
	case ModEffect::FlatEvasion:
	case ModEffect::FlatEnergyShield:
	case ModEffect::DefensePercent:
	case ModEffect::ArmourPercent:
	case ModEffect::EvasionPercent:
	case ModEffect::EnergyShieldPercent:
	case ModEffect::BlockChance:
		return Defence;
	case ModEffect::LifeAndDefensePercent:
		return Life | Defence;
	case ModEffect::ManaAndDefensePercent:
		return Mana | Defence;
	case ModEffect::AddedPhysical:
	case ModEffect::PhysicalPercent:
	case ModEffect::PhysicalPercentAndAccuracy:
	case ModEffect::AddedFire:
	case ModEffect::AddedLightning:
	case ModEffect::MinionDamage:
	case ModEffect::AddedPhysicalToAttacks:
	case ModEffect::Accuracy:
	case ModEffect::DamagePercent:
	case ModEffect::SpellDamage:
	case ModEffect::FireSpellDamage:
	case ModEffect::ColdSpellDamage:
	case ModEffect::LightningSpellDamage:
	case ModEffect::PhysicalSpellDamage:
	case ModEffect::DazeChance:
	case ModEffect::MinionAddedDamage:
		return Damage;
	case ModEffect::SpellDamageAndMana:
		return Damage | Mana;
	case ModEffect::Strength:
	case ModEffect::Dexterity:
	case ModEffect::Intelligence:
	case ModEffect::AllAttributes:
	case ModEffect::ReducedRequirements:
		return Attribute;
	case ModEffect::FireResistance:
	case ModEffect::MagicResistance:
	case ModEffect::LightningResistance:
	case ModEffect::AllResistances:
	case ModEffect::MinionResistances:
	case ModEffect::ChaosResistance:
	case ModEffect::MaxChaosResistance:
		return Resistance;
	case ModEffect::AttackSpeed:
	case ModEffect::CastSpeed:
		return Speed;
	case ModEffect::MeleeSkillLevels:
	case ModEffect::ProjectileSkillLevels:
	case ModEffect::SpellSkillLevels:
	case ModEffect::FireSkillLevels:
	case ModEffect::LightningSkillLevels:
	case ModEffect::ColdSkillLevels:
	case ModEffect::PhysicalSkillLevels:
	case ModEffect::MinionSkillLevels:
		return Skill;
	default:
		return 0;
	}
}

uint8_t ModsTags(const std::vector<Poe2ItemMod> &mods)
{
	uint8_t tags = 0;
	for (const Poe2ItemMod &mod : mods) {
		if (const ModFamily *family = ItemModFamily(mod); family != nullptr)
			tags |= ModEffectTags(family->effect);
	}
	return tags;
}

/** @brief Every legal tier of every legal family the next modifier can take, by weight. */
uint32_t CollectModCandidates(std::vector<ModCandidate> &candidates, uint32_t slotBit, uint16_t baseTags, int itemLevel, const std::vector<Poe2ItemMod> &rolled, int perSide,
    int magicCull, int rareMinLevel, const ModRollFilter *filter)
{
	candidates.clear();
	// First-element exclusivity: no random number, and nothing changes for a base without element tags.
	baseTags = NarrowedModBaseTags(baseTags, rolled);
	int prefixes = 0;
	int suffixes = 0;
	for (const Poe2ItemMod &mod : rolled) {
		if (const ModFamily *family = ItemModFamily(mod); family != nullptr)
			(family->prefix ? prefixes : suffixes)++;
	}
	const bool isMagic = perSide <= MaxMagicModsPerSide;
	const uint8_t rolledTags = filter != nullptr && filter->tags != ModTagRule::Any ? ModsTags(rolled) : 0;
	uint32_t totalWeight = 0;
	for (size_t f = 0; f < Families.size(); f++) {
		const ModFamily &family = Families[f];
		if (!family.enabled || (family.slots & slotBit) == 0)
			continue;
		// A family that doesn't roll naturally comes only from an essence that names it, never from a seed or from
		// currency that adds a random modifier.
		if (!family.natural && (filter == nullptr || filter->family == 0))
			continue;
		if (family.baseTags != 0 && (family.baseTags & baseTags) == 0)
			continue;
		if ((family.prefix ? prefixes : suffixes) >= perSide)
			continue;
		if (Conflicts(static_cast<uint8_t>(f), rolled))
			continue;
		if (filter != nullptr) {
			if (filter->side != ModSide::Any && family.prefix != (filter->side == ModSide::Prefix))
				continue;
			if (filter->family != 0 && filter->family != f + 1)
				continue;
			if ((filter->avoidEffects & ModEffectBits(family.effect)).any())
				continue;
			const uint8_t tags = ModEffectTags(family.effect);
			if (filter->tags == ModTagRule::Shared && (tags & rolledTags) == 0)
				continue;
			if (filter->tags == ModTagRule::NotShared && (tags & rolledTags) != 0)
				continue;
		}
		// Tiers run from T1 (index 0) down to T8, so the eligible ones are the tail of the list.
		size_t best = 0;
		while (best < family.tiers.size() && (family.tiers[best].itemLevel > itemLevel || family.tiers[best].weight == 0))
			best++;
		if (best == family.tiers.size())
			continue;
		size_t end = family.tiers.size();
		if (isMagic) {
			// A high unidentified tier culls a share of the low tiers, keeping at least the best one.
			const size_t eligible = end - best;
			const size_t culled = eligible * static_cast<size_t>(magicCull) / 100;
			end = std::max(best + 1, end - culled);
		} else {
			// A high unidentified tier drops low-level modifiers; a family keeps its best tier if that would remove it.
			while (end > best + 1 && family.tiers[end - 1].itemLevel < rareMinLevel)
				end--;
		}
		for (size_t t = best; t < end; t++) {
			const ModTier &tier = family.tiers[t];
			if (tier.itemLevel > itemLevel || tier.weight == 0)
				continue;
			// Greater and Perfect orbs only add modifiers of a high enough level.
			if (filter != nullptr && tier.itemLevel < filter->minModLevel)
				continue;
			candidates.push_back({ static_cast<uint8_t>(f), static_cast<uint8_t>(t), tier.weight });
			totalWeight += tier.weight;
		}
	}
	return totalWeight;
}

const ModCandidate &PickModCandidate(const std::vector<ModCandidate> &candidates, uint32_t pick)
{
	for (const ModCandidate &candidate : candidates) {
		if (pick < candidate.weight)
			return candidate;
		pick -= candidate.weight;
	}
	return candidates.back();
}

std::array<int16_t, 2> RollTierValues(const ModFamily &family, const ModTier &tier, ModRandom &values)
{
	std::array<int16_t, 2> rolled {};
	const std::array<int16_t, 2> &scale = UseLegacyScales ? family.legacyScale : family.scale;
	for (size_t stat = 0; stat < 2; stat++)
		rolled[stat] = ScaleValue(values.between(tier.min[stat], tier.max[stat]), scale[stat]);
	// Added damage rolls its low and high end apart; keep them in order.
	if (IsAnyOf(family.effect, ModEffect::AddedPhysical, ModEffect::AddedFire, ModEffect::AddedLightning, ModEffect::AddedPhysicalToAttacks) && rolled[1] < rolled[0])
		rolled[1] = rolled[0];
	return rolled;
}

} // namespace

std::vector<Poe2ItemMod> RollItemMods(ModSlot slot, uint16_t baseTags, int itemLevel, int count, int perSide, uint32_t seed, uint32_t valueSeed, int unidentifiedTier)
{
	std::vector<Poe2ItemMod> rolled;
	ModRandom picks { seed };
	ModRandom values { valueSeed };
	const uint32_t slotBit = ModSlotBit(slot);
	const size_t unidIndex = static_cast<size_t>(std::clamp(unidentifiedTier, 1, 5) - 1);
	const int magicCull = UnidentifiedMagicCullPercent[unidIndex];
	const int rareMinLevel = UnidentifiedRareMinModLevel[unidIndex];

	std::vector<ModCandidate> candidates;
	while (static_cast<int>(rolled.size()) < count) {
		// POE2 picks among every legal tier of every legal family at once, by weight.
		const uint32_t totalWeight = CollectModCandidates(candidates, slotBit, baseTags, itemLevel, rolled, perSide, magicCull, rareMinLevel, nullptr);
		if (totalWeight == 0)
			break;
		const ModCandidate &chosen = PickModCandidate(candidates, picks.below(totalWeight));
		const ModFamily &family = Families[chosen.family];
		const ModTier &tier = family.tiers[chosen.tierIndex];
		rolled.push_back({ static_cast<uint8_t>(chosen.family + 1), tier.tier, RollTierValues(family, tier, values) });
	}
	return rolled;
}

ModEffectMask ModEffectBits(ModEffect effect)
{
	const auto bit = [](ModEffect part) { return ModEffectMask().set(static_cast<size_t>(part)); };
	switch (effect) {
	case ModEffect::None:
	case ModEffect::Count:
		return {};
	case ModEffect::PhysicalPercentAndAccuracy:
		return bit(ModEffect::PhysicalPercent) | bit(ModEffect::Accuracy);
	case ModEffect::LifeAndDefensePercent:
		return bit(ModEffect::Life) | bit(ModEffect::DefensePercent);
	case ModEffect::ManaAndDefensePercent:
		return bit(ModEffect::Mana) | bit(ModEffect::DefensePercent);
	case ModEffect::SpiritAndMana:
		return bit(ModEffect::LocalSpiritPercent) | bit(ModEffect::Mana);
	case ModEffect::SpellDamageAndMana:
		return bit(ModEffect::SpellDamage) | bit(ModEffect::Mana);
	default:
		return bit(effect);
	}
}

ModEffectMask ModsEffectBits(const std::vector<Poe2ItemMod> &mods)
{
	ModEffectMask bits;
	for (const Poe2ItemMod &mod : mods) {
		if (const ModFamily *family = ItemModFamily(mod); family != nullptr)
			bits |= ModEffectBits(family->effect);
	}
	return bits;
}

std::optional<Poe2ItemMod> RollAddedMod(ModSlot slot, uint16_t baseTags, int itemLevel, const std::vector<Poe2ItemMod> &existing, int perSide, uint32_t seed, const ModRollFilter &filter)
{
	std::vector<ModCandidate> candidates;
	const uint32_t totalWeight = CollectModCandidates(candidates, ModSlotBit(slot), baseTags, itemLevel, existing, perSide, 0, 0, &filter);
	if (totalWeight == 0)
		return std::nullopt;
	ModRandom picks { seed };
	const ModCandidate &chosen = PickModCandidate(candidates, picks.below(totalWeight));
	const ModFamily &family = Families[chosen.family];
	const ModTier &tier = family.tiers[chosen.tierIndex];
	return Poe2ItemMod { static_cast<uint8_t>(chosen.family + 1), tier.tier, {} };
}

std::array<int16_t, 2> RollModValues(uint8_t family, uint8_t tier, uint32_t valueSeed)
{
	if (family == 0 || family > Families.size())
		return {};
	const ModFamily &modFamily = Families[family - 1];
	const auto it = std::find_if(modFamily.tiers.begin(), modFamily.tiers.end(), [tier](const ModTier &modTier) { return modTier.tier == tier; });
	if (it == modFamily.tiers.end())
		return {};
	ModRandom values { valueSeed };
	return RollTierValues(modFamily, *it, values);
}

int ModTierItemLevel(uint8_t family, uint8_t tier)
{
	if (family == 0 || family > Families.size())
		return 0;
	const ModFamily &modFamily = Families[family - 1];
	const auto it = std::find_if(modFamily.tiers.begin(), modFamily.tiers.end(), [tier](const ModTier &modTier) { return modTier.tier == tier; });
	return it != modFamily.tiers.end() ? it->itemLevel : 0;
}

bool ModsShareTag(const Poe2ItemMod &a, const Poe2ItemMod &b)
{
	const ModFamily *familyA = ItemModFamily(a);
	const ModFamily *familyB = ItemModFamily(b);
	return familyA != nullptr && familyB != nullptr && (ModEffectTags(familyA->effect) & ModEffectTags(familyB->effect)) != 0;
}

int MagicModCount(uint32_t seed)
{
	ModRandom random { seed ^ 0x3C6EF372U };
	return 1 + static_cast<int>(random.below(2));
}

int RareModCount(uint32_t seed)
{
	ModRandom random { seed ^ 0x52A4E5U };
	return RareModCountFromRoll(static_cast<int>(random.below(RareModCountRolls)));
}

const ModFamily *ItemModFamily(const Poe2ItemMod &mod)
{
	if (mod.family == 0 || mod.family > Families.size())
		return nullptr;
	return &Families[mod.family - 1];
}

void ApplyItemMod(Item &item, const Poe2ItemMod &mod)
{
	const ModFamily *family = ItemModFamily(mod);
	if (family == nullptr)
		return;
	ApplyModEffect(item, family->effect, mod.values[0], mod.values[1]);
}

void ApplyModEffect(Item &item, ModEffect effect, int v0, int v1)
{
	switch (effect) {
	case ModEffect::None:
	case ModEffect::Count:
		break;
	case ModEffect::Life:
		item._iPLHP += Fixed10_6::fromInt(v0);
		break;
	case ModEffect::Mana:
		item._iPLMana += Fixed10_6::fromInt(v0);
		break;
	case ModEffect::FlatDefense:
		item._iAC = static_cast<int16_t>(item._iAC + v0);
		break;
	case ModEffect::FlatArmour:
		item._iAC = static_cast<int16_t>(item._iAC + v0);
		AddPoe2Stat(item, Poe2ItemStat::FlatArmour, v0);
		break;
	case ModEffect::FlatEvasion:
		item._iAC = static_cast<int16_t>(item._iAC + v0);
		AddPoe2Stat(item, Poe2ItemStat::FlatEvasion, v0);
		break;
	case ModEffect::FlatEnergyShield:
		item._iAC = static_cast<int16_t>(item._iAC + v0);
		AddPoe2Stat(item, Poe2ItemStat::FlatEnergyShield, v0);
		break;
	case ModEffect::DefensePercent:
		item._iPLAC = static_cast<int16_t>(item._iPLAC + v0);
		break;
	case ModEffect::LifeAndDefensePercent:
		item._iPLHP += Fixed10_6::fromInt(v0);
		item._iPLAC = static_cast<int16_t>(item._iPLAC + v1);
		break;
	case ModEffect::ManaAndDefensePercent:
		item._iPLMana += Fixed10_6::fromInt(v0);
		item._iPLAC = static_cast<int16_t>(item._iPLAC + v1);
		break;
	case ModEffect::Spirit:
		AddPoe2Stat(item, Poe2ItemStat::Spirit, v0);
		break;
	case ModEffect::AddedPhysical:
		item._iMinDam = AddToDamage(item._iMinDam, v0);
		item._iMaxDam = AddToDamage(item._iMaxDam, v1);
		break;
	case ModEffect::PhysicalPercent:
		item._iPLDam = static_cast<int16_t>(item._iPLDam + v0);
		break;
	case ModEffect::PhysicalPercentAndAccuracy:
		item._iPLDam = static_cast<int16_t>(item._iPLDam + v0);
		item._iPLToHit = static_cast<int16_t>(item._iPLToHit + v1);
		break;
	case ModEffect::AddedFire:
		item._iFlags |= ItemSpecialEffect::FireDamage;
		item._iFMinDam = static_cast<int16_t>(item._iFMinDam + v0);
		item._iFMaxDam = static_cast<int16_t>(item._iFMaxDam + v1);
		break;
	case ModEffect::AddedLightning:
		item._iFlags |= ItemSpecialEffect::LightningDamage;
		item._iLMinDam = static_cast<int16_t>(item._iLMinDam + v0);
		item._iLMaxDam = static_cast<int16_t>(item._iLMaxDam + v1);
		break;
	case ModEffect::MinionDamage:
		AddPoe2Stat(item, Poe2ItemStat::MinionDamage, v0);
		break;
	case ModEffect::AddedPhysicalToAttacks:
		// Diablo adds one fixed amount to each hit, so the range becomes its middle.
		item._iPLDamMod = static_cast<int16_t>(item._iPLDamMod + ((v0 + v1 + 1) / 2));
		break;
	case ModEffect::Strength:
		item._iPLStr = static_cast<int16_t>(item._iPLStr + v0);
		break;
	case ModEffect::Dexterity:
		item._iPLDex = static_cast<int16_t>(item._iPLDex + v0);
		break;
	case ModEffect::Intelligence:
		item._iPLMag = static_cast<int16_t>(item._iPLMag + v0);
		break;
	case ModEffect::AllAttributes:
		item._iPLStr = static_cast<int16_t>(item._iPLStr + v0);
		item._iPLDex = static_cast<int16_t>(item._iPLDex + v0);
		item._iPLMag = static_cast<int16_t>(item._iPLMag + v0);
		break;
	case ModEffect::FireResistance:
		item._iPLFR = static_cast<int16_t>(item._iPLFR + v0);
		break;
	case ModEffect::MagicResistance:
		item._iPLMR = static_cast<int16_t>(item._iPLMR + v0);
		break;
	case ModEffect::LightningResistance:
		item._iPLLR = static_cast<int16_t>(item._iPLLR + v0);
		break;
	case ModEffect::AllResistances:
		item._iPLFR = static_cast<int16_t>(item._iPLFR + v0);
		item._iPLLR = static_cast<int16_t>(item._iPLLR + v0);
		item._iPLMR = static_cast<int16_t>(item._iPLMR + v0);
		break;
	case ModEffect::AttackSpeed:
		AddPoe2Stat(item, Poe2ItemStat::AttackSpeed, v0);
		break;
	case ModEffect::CastSpeed:
		AddPoe2Stat(item, Poe2ItemStat::CastSpeed, v0);
		break;
	case ModEffect::Accuracy:
		item._iPLToHit = static_cast<int16_t>(item._iPLToHit + v0);
		break;
	case ModEffect::ReducedRequirements:
		item._iMinStr = static_cast<uint8_t>(item._iMinStr - (item._iMinStr * v0 / 100));
		item._iMinMag = static_cast<uint8_t>(item._iMinMag - (item._iMinMag * v0 / 100));
		item._iMinDex = static_cast<uint8_t>(item._iMinDex - (item._iMinDex * v0 / 100));
		break;
	case ModEffect::LifeRegeneration:
		item._iPLLifeRegen = static_cast<int16_t>(item._iPLLifeRegen + v0);
		break;
	case ModEffect::ManaRegeneration:
		item._iPLManaRegen = static_cast<int16_t>(item._iPLManaRegen + v0);
		break;
	case ModEffect::MeleeSkillLevels:
		AddPoe2Stat(item, Poe2ItemStat::MeleeSkillLevels, v0);
		break;
	case ModEffect::ProjectileSkillLevels:
		AddPoe2Stat(item, Poe2ItemStat::ProjectileSkillLevels, v0);
		break;
	case ModEffect::SpellSkillLevels:
		AddPoe2Stat(item, Poe2ItemStat::SpellSkillLevels, v0);
		break;
	case ModEffect::FireSkillLevels:
		AddPoe2Stat(item, Poe2ItemStat::FireSkillLevels, v0);
		break;
	case ModEffect::LightningSkillLevels:
		AddPoe2Stat(item, Poe2ItemStat::LightningSkillLevels, v0);
		break;
	case ModEffect::LifePercent:
		AddPoe2Stat(item, Poe2ItemStat::LifePercent, v0);
		break;
	case ModEffect::ManaPercent:
		AddPoe2Stat(item, Poe2ItemStat::ManaPercent, v0);
		break;
	case ModEffect::DamagePercent:
		item._iPLDam = static_cast<int16_t>(item._iPLDam + v0);
		break;
	case ModEffect::ArmourPercent:
		AddPoe2Stat(item, Poe2ItemStat::ArmourPercent, v0);
		break;
	case ModEffect::EvasionPercent:
		AddPoe2Stat(item, Poe2ItemStat::EvasionPercent, v0);
		break;
	case ModEffect::EnergyShieldPercent:
		AddPoe2Stat(item, Poe2ItemStat::EnergyShieldPercent, v0);
		break;
	case ModEffect::ItemRarity:
		AddPoe2Stat(item, Poe2ItemStat::ItemRarity, v0);
		break;
	case ModEffect::SpellDamage:
		AddPoe2Stat(item, Poe2ItemStat::SpellDamage, v0);
		break;
	case ModEffect::LocalSpiritPercent:
		AddPoe2Stat(item, Poe2ItemStat::LocalSpiritPercent, v0);
		break;
	case ModEffect::SpiritAndMana:
		AddPoe2Stat(item, Poe2ItemStat::LocalSpiritPercent, v0);
		item._iPLMana += Fixed10_6::fromInt(v1);
		break;
	case ModEffect::SpellDamageAndMana:
		AddPoe2Stat(item, Poe2ItemStat::SpellDamage, v0);
		item._iPLMana += Fixed10_6::fromInt(v1);
		break;
	case ModEffect::FireSpellDamage:
		AddPoe2Stat(item, Poe2ItemStat::FireSpellDamage, v0);
		break;
	case ModEffect::ColdSpellDamage:
		AddPoe2Stat(item, Poe2ItemStat::ColdSpellDamage, v0);
		break;
	case ModEffect::LightningSpellDamage:
		AddPoe2Stat(item, Poe2ItemStat::LightningSpellDamage, v0);
		break;
	case ModEffect::PhysicalSpellDamage:
		AddPoe2Stat(item, Poe2ItemStat::PhysicalSpellDamage, v0);
		break;
	case ModEffect::ColdSkillLevels:
		AddPoe2Stat(item, Poe2ItemStat::ColdSkillLevels, v0);
		break;
	case ModEffect::PhysicalSkillLevels:
		AddPoe2Stat(item, Poe2ItemStat::PhysicalSkillLevels, v0);
		break;
	case ModEffect::MinionSkillLevels:
		AddPoe2Stat(item, Poe2ItemStat::MinionSkillLevels, v0);
		break;
	case ModEffect::MinionLife:
		AddPoe2Stat(item, Poe2ItemStat::MinionLife, v0);
		break;
	case ModEffect::MinionResistances:
		AddPoe2Stat(item, Poe2ItemStat::MinionResistances, v0);
		break;
	case ModEffect::LifeOnKill:
		AddPoe2Stat(item, Poe2ItemStat::LifeOnKill, v0);
		break;
	case ModEffect::ManaOnKill:
		AddPoe2Stat(item, Poe2ItemStat::ManaOnKill, v0);
		break;
	case ModEffect::BlockChance:
		AddPoe2Stat(item, Poe2ItemStat::BlockChance, v0);
		break;
	case ModEffect::DazeChance:
		AddPoe2Stat(item, Poe2ItemStat::DazeChance, v0);
		break;
	case ModEffect::MinionAddedDamage:
		AddPoe2Stat(item, Poe2ItemStat::MinionAddedDamageMin, v0);
		AddPoe2Stat(item, Poe2ItemStat::MinionAddedDamageMax, std::max(v0, v1));
		break;
	case ModEffect::MinionLifeRegen:
		AddPoe2Stat(item, Poe2ItemStat::MinionLifeRegen, v0);
		break;
	case ModEffect::ChaosResistance:
		AddPoe2Stat(item, Poe2ItemStat::ChaosResistance, v0);
		break;
	case ModEffect::MaxChaosResistance:
		AddPoe2Stat(item, Poe2ItemStat::MaxChaosResistance, v0);
		break;
	}
}

namespace {

/** @brief A modifier's value as its line shows it: flat energy shield is rolled in defence points, each worth EnergyShieldPerDefense. */
int ShownModValue(const ModFamily &family, size_t stat, int value)
{
	return family.effect == ModEffect::FlatEnergyShield && stat == 0 ? value * EnergyShieldPerDefense : value;
}

} // namespace

std::vector<std::string> ItemModText(const Poe2ItemMod &mod)
{
	std::vector<std::string> lines;
	const ModFamily *family = ItemModFamily(mod);
	if (family == nullptr)
		return lines;
	const std::string text = FormatRuntime(family->text, ShownModValue(*family, 0, mod.values[0]), ShownModValue(*family, 1, mod.values[1]));
	// Hybrid modifiers show one line per stat, the tier on the last one.
	for (const std::string_view line : SplitByChar(text, ';')) {
		std::string_view trimmed = line;
		while (!trimmed.empty() && trimmed.front() == ' ')
			trimmed.remove_prefix(1);
		lines.emplace_back(trimmed);
	}
	return lines;
}

std::vector<std::string> CombinedItemModText(std::span<const Poe2ItemMod> mods)
{
	struct StatLine {
		/** The stat's text with its values as {0} and {1}, numbered in the order they appear. */
		std::string text;
		std::array<int, 2> values;
	};
	std::vector<StatLine> stats;
	for (const Poe2ItemMod &mod : mods) {
		const ModFamily *family = ItemModFamily(mod);
		if (family == nullptr)
			continue;
		for (std::string_view part : SplitByChar(family->text, ';')) {
			while (!part.empty() && part.front() == ' ')
				part.remove_prefix(1);
			// Numbered by position, a hybrid's second stat ("{1}% increased Energy Shield") matches a plain modifier's first.
			StatLine line { {}, {} };
			size_t count = 0;
			for (size_t i = 0; i < part.size(); i++) {
				if (count < 2 && part[i] == '{' && i + 2 < part.size() && IsAnyOf(part[i + 1], '0', '1') && part[i + 2] == '}') {
					const size_t stat = static_cast<size_t>(part[i + 1] - '0');
					line.values[count] = ShownModValue(*family, stat, mod.values[stat]);
					line.text += count == 0 ? "{0}" : "{1}";
					count++;
					i += 2;
				} else {
					line.text += part[i];
				}
			}
			const auto same = std::find_if(stats.begin(), stats.end(), [&line](const StatLine &other) { return other.text == line.text; });
			if (same == stats.end()) {
				stats.push_back(std::move(line));
				continue;
			}
			same->values[0] += line.values[0];
			same->values[1] += line.values[1];
		}
	}
	std::vector<std::string> lines;
	lines.reserve(stats.size());
	for (const StatLine &stat : stats)
		lines.push_back(FormatRuntime(stat.text, stat.values[0], stat.values[1]));
	return lines;
}

std::vector<std::string> ItemModAdvancedText(const Poe2ItemMod &mod)
{
	std::vector<std::string> lines;
	const ModFamily *family = ItemModFamily(mod);
	if (family == nullptr)
		return lines;
	const ModTier *tier = nullptr;
	for (const ModTier &candidate : family->tiers) {
		if (candidate.tier == mod.tier)
			tier = &candidate;
	}
	std::array<std::string, 2> shown;
	for (size_t stat = 0; stat < 2; stat++) {
		shown[stat] = StrCat(ShownModValue(*family, stat, mod.values[stat]));
		if (tier == nullptr)
			continue;
		const int low = ShownModValue(*family, stat, ScaleValue(tier->min[stat], family->scale[stat]));
		const int high = ShownModValue(*family, stat, ScaleValue(tier->max[stat], family->scale[stat]));
		if (low != high)
			shown[stat].append(StrCat("(", std::min(low, high), "-", std::max(low, high), ")"));
	}
	const std::string text = FormatRuntime(family->text, shown[0], shown[1]);
	for (const std::string_view line : SplitByChar(text, ';')) {
		std::string_view trimmed = line;
		while (!trimmed.empty() && trimmed.front() == ' ')
			trimmed.remove_prefix(1);
		lines.emplace_back(trimmed);
	}
	return lines;
}

std::vector<std::string> AffixViewLines(std::string_view side, const std::vector<std::string> &stats, int tier)
{
	std::vector<std::string> lines;
	for (size_t i = 0; i < stats.size(); i++)
		lines.push_back(StrCat(i == 0 ? side : std::string_view {}, "\t", stats[i], "\t", i + 1 == stats.size() ? StrCat("T", tier) : std::string {}));
	return lines;
}

int ItemSkillLevelBonus(const Poe2PlayerData &data, SkillSlot skill)
{
	const auto stat = [&data](Poe2ItemStat which) { return static_cast<int>(data.itemStats[static_cast<size_t>(which)]); };
	// Some uniques add levels to every skill while on Low Life.
	const int lowLife = skill.kind != SkillKind::None && data.uniqueState.lowLife ? data.rule(UniqueRule::LowLifeSkillLevels) : 0;
	switch (skill.kind) {
	case SkillKind::Attack:
		return lowLife + stat(IsRangedWeaponSkill(skill.attack()) ? Poe2ItemStat::ProjectileSkillLevels : Poe2ItemStat::MeleeSkillLevels);
	case SkillKind::Spell: {
		int bonus = stat(Poe2ItemStat::SpellSkillLevels);
		const SpellID spell = skill.spell();
		if (static_cast<size_t>(spell) < SpellsData.size()) {
			// The spell's element as its damage has it (skill_details): fire, lightning, or cold (magic).
			switch (GetSpellData(spell).type()) {
			case MagicType::Fire:
				bonus += stat(Poe2ItemStat::FireSkillLevels);
				break;
			case MagicType::Lightning:
				bonus += stat(Poe2ItemStat::LightningSkillLevels);
				break;
			default:
				bonus += stat(Poe2ItemStat::ColdSkillLevels);
				break;
			}
		}
		return lowLife + bonus;
	}
	case SkillKind::Persistent:
		// A sceptre's levels of the minion gems.
		return lowLife + (IsMinionSkill(skill.persistent()) ? stat(Poe2ItemStat::MinionSkillLevels) : 0);
	case SkillKind::Default:
		// The caster weapons' Staff Bolt is a fire spell (caster-weapons-plan-v2.md 6.10); the other default attacks have
		// no level.
		if (static_cast<DefaultAttack>(skill.id) == DefaultAttack::StaffBolt)
			return lowLife + stat(Poe2ItemStat::SpellSkillLevels) + stat(Poe2ItemStat::FireSkillLevels);
		return lowLife;
	default:
		return lowLife;
	}
}

std::string MagicModItemName(const Item &item, std::string_view baseName)
{
	std::string_view prefix;
	std::string_view suffix;
	for (const Poe2ItemMod &mod : item._iPoe2Mods) {
		const ModFamily *family = ItemModFamily(mod);
		if (family == nullptr)
			continue;
		if (family->prefix && prefix.empty())
			prefix = family->word;
		else if (!family->prefix && suffix.empty())
			suffix = family->word;
	}
	std::string name;
	if (!prefix.empty())
		name = StrCat(prefix, " ");
	name.append(baseName);
	if (!suffix.empty())
		name.append(StrCat(" ", suffix));
	return name;
}

} // namespace devilution
