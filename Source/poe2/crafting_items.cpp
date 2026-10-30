#include "poe2/crafting_items.hpp"

#include <algorithm>
#include <expected>
#include <optional>

#include <magic_enum/magic_enum.hpp>

#include "data/file.hpp"
#include "data/record_reader.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "poe2/desecration.hpp"
#include "poe2/loot.hpp"
#include "poe2/mix.hpp"
#include "poe2/weapon_kinds.hpp"
#include "utils/format.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/log.hpp"
#include "utils/str_cat.hpp"
#include "utils/utf8.hpp"

namespace devilution {

namespace {

std::vector<Essence> Essences;
std::vector<Omen> Omens;
std::vector<Augment> Augments;

/** What a table without a row for the id gives: the None row. */
const Essence NoEssence {};
const Omen NoOmen {};
const Augment NoAugment {};

/** A table's id column: the name of the id it is the row of, which must be the next id, as rows are in id order. */
template <typename Id>
bool ReadRowId(RecordReader &reader, std::string_view filename, size_t rows, Id &id)
{
	std::string text;
	reader.readString("id", text);
	const std::optional<Id> parsed = magic_enum::enum_cast<Id>(text);
	if (!parsed || static_cast<size_t>(*parsed) != rows || *parsed == Id::Count) {
		LogError("{}: \"{}\" is not the next id, {}", filename, text, magic_enum::enum_name(static_cast<Id>(rows)));
		return false;
	}
	id = *parsed;
	return true;
}

/** Whether the table, with its None row, has a row for every id. */
template <typename Id>
bool HasEveryRow(std::string_view filename, size_t rows, Id count)
{
	if (rows == static_cast<size_t>(count))
		return true;
	LogError("{}: rows for {} of the {} ids", filename, rows - 1, static_cast<size_t>(count) - 1);
	return false;
}

/** A becomes column: empty for a row of today, else the id of today a legacy row's items get on load. */
template <typename Id>
std::expected<Id, std::string> ParseBecomes(std::string_view text)
{
	if (text.empty())
		return Id::None;
	if (const std::optional<Id> id = magic_enum::enum_cast<Id>(text); id && *id != Id::Count)
		return *id;
	return std::unexpected("unknown id");
}

/** Whether every legacy row becomes a row of today, so a converted item never needs converting again. */
template <typename Row, typename Id>
bool BecomesRowsOfToday(std::string_view filename, const std::vector<Row> &rows, Id Row::*becomes)
{
	for (const Row &row : rows) {
		const Id target = row.*becomes;
		if (target == Id::None)
			continue;
		const auto index = static_cast<size_t>(target);
		if (index >= rows.size() || rows[index].*becomes != Id::None) {
			LogError("{}: a row becomes {}, which isn't a row of today", filename, magic_enum::enum_name(target));
			return false;
		}
	}
	return true;
}

/** An effect column; empty for none. */
std::expected<ModEffect, std::string> ParseEffect(std::string_view text)
{
	if (text.empty())
		return ModEffect::None;
	if (const std::optional<ModEffect> effect = magic_enum::enum_cast<ModEffect>(text))
		return *effect;
	return std::unexpected("unknown effect");
}

std::expected<UniqueRule, std::string> ParseRule(std::string_view text)
{
	if (text.empty())
		return UniqueRule::None;
	if (const std::optional<UniqueRule> rule = magic_enum::enum_cast<UniqueRule>(text))
		return *rule;
	return std::unexpected("unknown rule");
}

std::expected<OmenTrigger, std::string> ParseTrigger(std::string_view text)
{
	if (const std::optional<OmenTrigger> trigger = magic_enum::enum_cast<OmenTrigger>(text))
		return *trigger;
	return std::unexpected("unknown omen trigger");
}

bool LoadEssences()
{
	constexpr std::string_view Filename = "txtdata\\poe2\\essences.tsv";
	auto dataFile = DataFile::load(Filename);
	if (!dataFile.has_value()) {
		LogError("Couldn't load the essences: {}", Filename);
		return false;
	}
	dataFile->skipHeaderOrDie(Filename);
	Essences.emplace_back();
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		Essence essence;
		if (!ReadRowId(reader, Filename, Essences.size(), essence.kind))
			return false;
		reader.readString("name", essence.name);
		// The game has no spell damage or critical chance modifiers, so Sorcery and Precision, and the kinds they became,
		// guarantee the nearest ones. Warding keeps all elemental resistances; Ruin gives chaos resistance, which only
		// what names it gives (its family isn't natural), so it reaches the family through the roll filter.
		reader.readString("family", essence.family);
		reader.read("becomes", essence.becomes, ParseBecomes<EssenceKind>);
		Essences.push_back(std::move(essence));
	}
	return HasEveryRow(Filename, Essences.size(), EssenceKind::Count) && BecomesRowsOfToday(Filename, Essences, &Essence::becomes);
}

bool LoadOmens()
{
	constexpr std::string_view Filename = "txtdata\\poe2\\omens.tsv";
	auto dataFile = DataFile::load(Filename);
	if (!dataFile.has_value()) {
		LogError("Couldn't load the omens: {}", Filename);
		return false;
	}
	dataFile->skipHeaderOrDie(Filename);
	Omens.emplace_back();
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		Omen omen;
		if (!ReadRowId(reader, Filename, Omens.size(), omen.id))
			return false;
		reader.readString("name", omen.name);
		reader.read("trigger", omen.trigger, ParseTrigger);
		reader.readInt("dropLevel", omen.dropLevel);
		reader.readInt("weight", omen.weight);
		reader.readString("description", omen.description);
		Omens.push_back(std::move(omen));
	}
	return HasEveryRow(Filename, Omens.size(), OmenId::Count);
}

bool LoadAugments()
{
	constexpr std::string_view Filename = "txtdata\\poe2\\augments.tsv";
	auto dataFile = DataFile::load(Filename);
	if (!dataFile.has_value()) {
		LogError("Couldn't load the augments: {}", Filename);
		return false;
	}
	dataFile->skipHeaderOrDie(Filename);
	Augments.emplace_back();
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, Filename };
		Augment augment;
		if (!ReadRowId(reader, Filename, Augments.size(), augment.id))
			return false;
		reader.readString("name", augment.name);
		reader.readInt("tier", augment.tier);
		reader.readInt("dropLevel", augment.dropLevel);
		// An effect column, then its two values.
		const auto readEffect = [&reader](std::string_view column, ModEffect &effect, std::array<int16_t, 2> &values) {
			reader.read(StrCat(column, "Effect"), effect, ParseEffect);
			reader.readInt(StrCat(column, "Value1"), values[0]);
			reader.readInt(StrCat(column, "Value2"), values[1]);
		};
		readEffect("weapon", augment.weaponEffect, augment.weaponValues);
		readEffect("staff", augment.staffEffect, augment.staffValues);
		readEffect("armour", augment.armourEffect, augment.armourValues);
		reader.read("rule", augment.rule, ParseRule);
		reader.readInt("ruleValue", augment.ruleValue);
		reader.readString("weaponText", augment.weaponText);
		reader.readString("staffText", augment.staffText);
		reader.readString("armourText", augment.armourText);
		reader.read("becomes", augment.becomes, ParseBecomes<AugmentId>);
		Augments.push_back(std::move(augment));
	}
	return HasEveryRow(Filename, Augments.size(), AugmentId::Count) && BecomesRowsOfToday(Filename, Augments, &Augment::becomes);
}

/** @brief Picks one of the weighted choices with the roll. */
template <typename T>
T PickWeighted(const std::vector<std::pair<T, int>> &choices, uint32_t roll)
{
	if (choices.empty())
		return {};
	int total = 0;
	for (const auto &[choice, weight] : choices)
		total += weight;
	if (total <= 0)
		return choices.front().first;
	int pick = static_cast<int>(roll % static_cast<uint32_t>(total));
	for (const auto &[choice, weight] : choices) {
		if (pick < weight)
			return choice;
		pick -= weight;
	}
	return choices.back().first;
}

/** @brief The POE2 level the item dropped at, for which kinds it can be. */
int DropLevel(const Item &item)
{
	return Poe2ItemLevel(std::max(1, item._iCreateInfo & CF_LEVEL));
}

uint8_t RollVariant(const Item &item)
{
	const uint32_t roll = Mix(static_cast<uint32_t>(item._iSeed) ^ 0xE55E4CE5U);
	const int level = DropLevel(item);
	switch (item._iMiscId) {
	case IMISC_ESSENCE: {
		std::vector<std::pair<uint8_t, int>> choices;
		for (size_t kind = 1; kind < Essences.size(); kind++) {
			// Legacy kinds never drop; the kinds they became do, in their places.
			if (Essences[kind].becomes != EssenceKind::None)
				continue;
			for (size_t tier = 0; tier < EssenceDropLevels.size(); tier++) {
				// Lesser essences drop anywhere; better ones need their level.
				if (tier == 0 || EssenceDropLevels[tier] <= level)
					choices.emplace_back(EssenceVariant(static_cast<EssenceKind>(kind), static_cast<EssenceTier>(tier)), EssenceWeights[tier]);
			}
		}
		return PickWeighted(choices, roll);
	}
	case IMISC_OMEN: {
		std::vector<std::pair<uint8_t, int>> choices;
		for (const Omen &omen : Omens) {
			if (omen.id != OmenId::None && omen.dropLevel <= std::max(level, 40))
				choices.emplace_back(static_cast<uint8_t>(omen.id), omen.weight);
		}
		return PickWeighted(choices, roll);
	}
	case IMISC_AUGMENT: {
		std::vector<std::pair<uint8_t, int>> choices;
		for (const Augment &augment : Augments) {
			if (augment.id != AugmentId::None && augment.becomes == AugmentId::None && (augment.tier == 1 || augment.dropLevel <= level))
				choices.emplace_back(static_cast<uint8_t>(augment.id), 4 - augment.tier);
		}
		return PickWeighted(choices, roll);
	}
	case IMISC_ORB_CATALYST:
		return static_cast<uint8_t>(1 + (roll % (static_cast<uint32_t>(Catalyst::Count) - 1)));
	default:
		return 0;
	}
}

std::string_view EssenceTierName(EssenceTier tier)
{
	switch (tier) {
	case EssenceTier::Lesser:
		return "Lesser ";
	case EssenceTier::Greater:
		return "Greater ";
	case EssenceTier::Perfect:
		return "Perfect ";
	default:
		return "";
	}
}

} // namespace

uint8_t CraftingItemVariant(const Item &item)
{
	return static_cast<uint8_t>((item.dwBuff & CF_POE2_VARIANT) >> 16);
}

void SetCraftingItemVariant(Item &item, uint8_t variant)
{
	item.dwBuff = (item.dwBuff & ~CF_POE2_VARIANT) | (static_cast<uint32_t>(variant) << 16);
}

bool IsCraftingVariantItem(const Item &item)
{
	return !item.isEmpty() && IsAnyOf(item._iMiscId, IMISC_ESSENCE, IMISC_OMEN, IMISC_AUGMENT, IMISC_ORB_CATALYST);
}

void LoadCraftingItemData()
{
	Essences.clear();
	Omens.clear();
	Augments.clear();
	if (!gbIsPoe2)
		return;
	if (!LoadEssences())
		Essences.clear();
	if (!LoadOmens())
		Omens.clear();
	if (!LoadAugments())
		Augments.clear();
}

const Essence &GetEssence(EssenceKind kind)
{
	const auto index = static_cast<size_t>(kind);
	return index < Essences.size() ? Essences[index] : NoEssence;
}

uint8_t EssenceVariant(EssenceKind kind, EssenceTier tier)
{
	const auto k = static_cast<uint8_t>(kind);
	return static_cast<uint8_t>((k & 0xF) | (static_cast<uint8_t>(tier) << 4) | ((k & 0x10) << 2));
}

EssenceKind EssenceVariantKind(uint8_t variant)
{
	const auto kind = static_cast<uint8_t>((variant & 0xF) | ((variant >> 2) & 0x10));
	return kind < static_cast<uint8_t>(EssenceKind::Count) ? static_cast<EssenceKind>(kind) : EssenceKind::None;
}

EssenceTier EssenceVariantTier(uint8_t variant)
{
	return static_cast<EssenceTier>((variant >> 4) & 3);
}

EssenceKind GetEssenceKind(const Item &item)
{
	if (item.isEmpty() || item._iMiscId != IMISC_ESSENCE)
		return EssenceKind::None;
	return EssenceVariantKind(CraftingItemVariant(item));
}

EssenceTier GetEssenceTier(const Item &item)
{
	return EssenceVariantTier(CraftingItemVariant(item));
}

EssenceKind CurrentEssenceKind(EssenceKind kind)
{
	const EssenceKind becomes = GetEssence(kind).becomes;
	return becomes != EssenceKind::None ? becomes : kind;
}

const Omen &GetOmen(OmenId id)
{
	const auto index = static_cast<size_t>(id);
	return index < Omens.size() ? Omens[index] : NoOmen;
}

OmenId GetOmenId(const Item &item)
{
	if (item.isEmpty() || item._iMiscId != IMISC_OMEN)
		return OmenId::None;
	const uint8_t id = CraftingItemVariant(item);
	return id < static_cast<uint8_t>(OmenId::Count) ? static_cast<OmenId>(id) : OmenId::None;
}

const Augment &GetAugment(AugmentId id)
{
	const auto index = static_cast<size_t>(id);
	return index < Augments.size() ? Augments[index] : NoAugment;
}

AugmentId GetAugmentId(const Item &item)
{
	if (item.isEmpty() || item._iMiscId != IMISC_AUGMENT)
		return AugmentId::None;
	const uint8_t id = CraftingItemVariant(item);
	return id < static_cast<uint8_t>(AugmentId::Count) ? static_cast<AugmentId>(id) : AugmentId::None;
}

AugmentId CurrentAugmentId(AugmentId id)
{
	const AugmentId becomes = GetAugment(id).becomes;
	return becomes != AugmentId::None ? becomes : id;
}

bool ConvertLegacyCraftingIds(Item &item)
{
	if (item.isEmpty())
		return false;
	bool renamed = false;
	if (const AugmentId id = GetAugmentId(item); id != CurrentAugmentId(id)) {
		SetCraftingItemVariant(item, static_cast<uint8_t>(CurrentAugmentId(id)));
		renamed = true;
	}
	if (const EssenceKind kind = GetEssenceKind(item); kind != CurrentEssenceKind(kind)) {
		SetCraftingItemVariant(item, EssenceVariant(CurrentEssenceKind(kind), GetEssenceTier(item)));
		renamed = true;
	}
	if (renamed)
		FinishCraftingItem(item);
	bool socketed = false;
	Poe2Craft &craft = item._iPoe2Craft;
	for (size_t i = 0; i < craft.augments.size() && i < craft.sockets; i++) {
		const auto current = static_cast<uint8_t>(CurrentAugmentId(static_cast<AugmentId>(craft.augments[i])));
		if (current != craft.augments[i]) {
			craft.augments[i] = current;
			socketed = true;
		}
	}
	return renamed || socketed;
}

std::string_view CatalystName(Catalyst catalyst)
{
	switch (catalyst) {
	case Catalyst::Vital:
		return "Vital";
	case Catalyst::Arcane:
		return "Arcane";
	case Catalyst::Prismatic:
		return "Prismatic";
	case Catalyst::Martial:
		return "Martial";
	case Catalyst::Swift:
		return "Swift";
	case Catalyst::Critical:
		return "Critical";
	case Catalyst::Attribute:
		return "Attribute";
	case Catalyst::Spiritual:
		return "Spiritual";
	default:
		return "";
	}
}

Catalyst GetCatalyst(const Item &item)
{
	if (item.isEmpty() || item._iMiscId != IMISC_ORB_CATALYST)
		return Catalyst::None;
	const uint8_t id = CraftingItemVariant(item);
	return id < static_cast<uint8_t>(Catalyst::Count) ? static_cast<Catalyst>(id) : Catalyst::None;
}

bool CatalystBoosts(Catalyst catalyst, ModEffect effect)
{
	switch (catalyst) {
	case Catalyst::Vital:
		return IsAnyOf(effect, ModEffect::Life, ModEffect::LifeRegeneration);
	case Catalyst::Arcane:
		return IsAnyOf(effect, ModEffect::Mana, ModEffect::ManaRegeneration, ModEffect::CastSpeed, ModEffect::SpellSkillLevels, ModEffect::FireSkillLevels, ModEffect::LightningSkillLevels, ModEffect::SpellDamage,
		    ModEffect::SpellDamageAndMana, ModEffect::FireSpellDamage, ModEffect::ColdSpellDamage, ModEffect::LightningSpellDamage, ModEffect::PhysicalSpellDamage, ModEffect::ColdSkillLevels,
		    ModEffect::PhysicalSkillLevels, ModEffect::ManaOnKill, ModEffect::ChaosSpellDamage, ModEffect::ChaosSkillLevels);
	case Catalyst::Prismatic:
		return IsAnyOf(effect, ModEffect::FireResistance, ModEffect::MagicResistance, ModEffect::LightningResistance, ModEffect::AllResistances);
	case Catalyst::Martial:
		return IsAnyOf(effect, ModEffect::AddedPhysicalToAttacks, ModEffect::MeleeSkillLevels, ModEffect::ProjectileSkillLevels);
	case Catalyst::Swift:
		return IsAnyOf(effect, ModEffect::AttackSpeed, ModEffect::CastSpeed);
	case Catalyst::Critical:
		return effect == ModEffect::Accuracy;
	case Catalyst::Attribute:
		return IsAnyOf(effect, ModEffect::Strength, ModEffect::Dexterity, ModEffect::Intelligence, ModEffect::AllAttributes);
	case Catalyst::Spiritual:
		return IsAnyOf(effect, ModEffect::Spirit, ModEffect::MinionDamage, ModEffect::LocalSpiritPercent, ModEffect::SpiritAndMana, ModEffect::MinionSkillLevels, ModEffect::MinionLife,
		    ModEffect::MinionResistances, ModEffect::MinionAddedDamage, ModEffect::MinionLifeRegen);
	default:
		return false;
	}
}

int MaxAugmentSockets(const Item &item)
{
	if (item.isEmpty() || IsAnyOf(item._itype, ItemType::Ring, ItemType::Amulet) || item._iMiscId == IMISC_JEWEL)
		return 0;
	if (item._iClass == ICLASS_WEAPON)
		return item._iLoc == ILOC_TWOHAND ? 2 : 1;
	if (item._iClass == ICLASS_ARMOR)
		return item._iLoc == ILOC_ARMOR ? 2 : 1;
	return 0;
}

void ApplyAugments(Item &item)
{
	const Poe2Craft &craft = item._iPoe2Craft;
	const int percent = HasDesecratedMod(item, DesecratedModId::HollowSaint) ? HollowSaintAugmentPercent : 100;
	for (size_t i = 0; i < craft.augments.size() && i < craft.sockets; i++) {
		const Augment &augment = GetAugment(static_cast<AugmentId>(craft.augments[i]));
		if (item._iClass != ICLASS_WEAPON)
			ApplyModEffect(item, augment.armourEffect, augment.armourValues[0] * percent / 100, augment.armourValues[1] * percent / 100);
		else if (IsCasterWeapon(item))
			ApplyModEffect(item, augment.staffEffect, augment.staffValues[0] * percent / 100, augment.staffValues[1] * percent / 100);
		else
			ApplyModEffect(item, augment.weaponEffect, augment.weaponValues[0] * percent / 100, augment.weaponValues[1] * percent / 100);
	}
}

std::vector<std::string> AugmentSocketText(const Item &item)
{
	std::vector<std::string> lines;
	const Poe2Craft &craft = item._iPoe2Craft;
	for (size_t i = 0; i < craft.augments.size() && i < craft.sockets; i++) {
		const Augment &augment = GetAugment(static_cast<AugmentId>(craft.augments[i]));
		if (augment.id == AugmentId::None) {
			lines.emplace_back(_("Empty augment socket"));
			continue;
		}
		const std::string &text = item._iClass != ICLASS_WEAPON ? augment.armourText : (IsCasterWeapon(item) ? augment.staffText : augment.weaponText);
		lines.push_back(StrCat(augment.name, ": ", text));
	}
	return lines;
}

namespace {

struct EnchantData {
	CorruptionEnchant id;
	/** @brief Weapons, armour, shields or jewellery. */
	char kind;
	int minItemLevel;
	int weight;
	ModEffect effect;
	UniqueRule rule;
	int min;
	int max;
	std::string_view text;
};

const std::array<EnchantData, static_cast<size_t>(CorruptionEnchant::Count)> Enchants { {
	{ CorruptionEnchant::None, ' ', 0, 0, ModEffect::None, UniqueRule::None, 0, 0, "" },
	{ CorruptionEnchant::AttackSpeed, 'w', 35, 100, ModEffect::AttackSpeed, UniqueRule::None, 8, 12, "{:d}% increased attack speed" },
	{ CorruptionEnchant::CriticalDamage, 'w', 45, 80, ModEffect::None, UniqueRule::CritDamageBonus, 18, 25, "+{:d}% to critical damage bonus" },
	{ CorruptionEnchant::PhysicalAsFire, 'w', 55, 55, ModEffect::None, UniqueRule::PhysicalToFire, 8, 12, "{:d}% of physical damage converted to fire" },
	{ CorruptionEnchant::WeaponSkillLevel, 'w', 72, 18, ModEffect::None, UniqueRule::AllSkillLevels, 1, 1, "+{:d} to level of all skills" },
	{ CorruptionEnchant::LifePercent, 'a', 45, 70, ModEffect::LifePercent, UniqueRule::None, 3, 5, "{:d}% increased maximum life" },
	{ CorruptionEnchant::MaxResistance, 'a', 65, 30, ModEffect::None, UniqueRule::MaxElementalResistance, 1, 1, "+{:d}% to maximum elemental resistances" },
	{ CorruptionEnchant::MoreDefence, 'a', 55, 60, ModEffect::DefensePercent, UniqueRule::None, 10, 16, "{:d}% increased armour, evasion and energy shield" },
	{ CorruptionEnchant::ShieldSkillLevel, 's', 72, 20, ModEffect::None, UniqueRule::AllSkillLevels, 1, 1, "+{:d} to level of all skills" },
	{ CorruptionEnchant::AllResistances, 'j', 45, 80, ModEffect::AllResistances, UniqueRule::None, 4, 7, "+{:d}% to all resistances" },
	{ CorruptionEnchant::Attributes, 'j', 55, 60, ModEffect::AllAttributes, UniqueRule::None, 12, 18, "+{:d} to all attributes" },
} };

char EnchantKind(const Item &item)
{
	if (IsAnyOf(item._itype, ItemType::Ring, ItemType::Amulet))
		return 'j';
	if (item._iClass == ICLASS_WEAPON)
		return 'w';
	if (item._itype == ItemType::Shield)
		return 's';
	if (item._iClass == ICLASS_ARMOR)
		return 'a';
	return ' ';
}

const EnchantData *ItemEnchant(const Item &item)
{
	const uint8_t id = item._iPoe2Craft.enchant;
	if (id == 0 || id >= Enchants.size())
		return nullptr;
	return &Enchants[id];
}

int EnchantValue(const EnchantData &enchant, uint8_t roll)
{
	return enchant.min + ((enchant.max - enchant.min) * std::min<int>(roll, 7) / 7);
}

} // namespace

std::vector<std::pair<CorruptionEnchant, int>> CorruptionEnchantChoices(const Item &item)
{
	std::vector<std::pair<CorruptionEnchant, int>> choices;
	const char kind = EnchantKind(item);
	const int level = GetPoe2ItemLevel(item);
	for (const EnchantData &enchant : Enchants) {
		// Shields also take armour enchantments.
		const bool fits = enchant.kind == kind || (kind == 's' && enchant.kind == 'a');
		if (enchant.id != CorruptionEnchant::None && fits && enchant.minItemLevel <= std::max(level, 1))
			choices.emplace_back(enchant.id, enchant.weight);
	}
	return choices;
}

void ApplyCorruptionEnchant(Item &item)
{
	const EnchantData *enchant = ItemEnchant(item);
	if (enchant == nullptr || enchant->effect == ModEffect::None)
		return;
	ApplyModEffect(item, enchant->effect, EnchantValue(*enchant, item._iPoe2Craft.enchantRoll), 0);
}

std::string CorruptionEnchantText(const Item &item)
{
	const EnchantData *enchant = ItemEnchant(item);
	if (enchant == nullptr)
		return {};
	return FormatRuntime(enchant->text, EnchantValue(*enchant, item._iPoe2Craft.enchantRoll));
}

void AddCraftRules(const Item &item, UniqueRuleTotals &totals)
{
	const auto add = [&totals](UniqueRule rule, int value) {
		int16_t &total = totals[static_cast<size_t>(rule)];
		total = static_cast<int16_t>(total + value);
	};
	const Poe2Craft &craft = item._iPoe2Craft;
	for (size_t i = 0; i < craft.augments.size() && i < craft.sockets; i++) {
		const Augment &augment = GetAugment(static_cast<AugmentId>(craft.augments[i]));
		if (augment.rule != UniqueRule::None)
			add(augment.rule, augment.ruleValue);
	}
	if (const EnchantData *enchant = ItemEnchant(item); enchant != nullptr && enchant->rule != UniqueRule::None)
		add(enchant->rule, EnchantValue(*enchant, craft.enchantRoll));
	AddDesecratedRules(item, totals);
}

void FinishCraftingItem(Item &item)
{
	if (!IsCraftingVariantItem(item))
		return;
	if (CraftingItemVariant(item) == 0)
		SetCraftingItemVariant(item, RollVariant(item));
	std::string name;
	switch (item._iMiscId) {
	case IMISC_ESSENCE:
		name = StrCat(EssenceTierName(GetEssenceTier(item)), "Essence of ", GetEssence(GetEssenceKind(item)).name);
		break;
	case IMISC_OMEN:
		name = std::string(GetOmen(GetOmenId(item)).name);
		break;
	case IMISC_AUGMENT:
		name = std::string(GetAugment(GetAugmentId(item)).name);
		break;
	case IMISC_ORB_CATALYST:
		name = StrCat(CatalystName(GetCatalyst(item)), " Catalyst");
		break;
	default:
		return;
	}
	CopyUtf8(item._iName, name, ItemNameLength);
	CopyUtf8(item._iIName, name, ItemNameLength);
}

std::vector<std::string> CraftingItemText(const Item &item)
{
	std::vector<std::string> lines;
	switch (item._iMiscId) {
	case IMISC_ESSENCE: {
		const EssenceTier tier = GetEssenceTier(item);
		const Essence &essence = GetEssence(GetEssenceKind(item));
		const std::optional<uint8_t> family = FindModifierFamily(essence.family);
		const std::string_view familyName = family ? std::string_view(ModifierFamilies()[*family].name) : essence.name;
		if (tier == EssenceTier::Perfect)
			lines.push_back(FormatRuntime(_("Rare item: removes a random modifier and adds {:s}"), familyName));
		else
			lines.push_back(FormatRuntime(_("Magic item: makes it rare with {:s}"), familyName));
		if (EssenceModLevelFloors[static_cast<size_t>(tier)] > 0)
			lines.push_back(FormatRuntime(_("The modifier is at least level {:d}"), EssenceModLevelFloors[static_cast<size_t>(tier)]));
		break;
	}
	case IMISC_OMEN:
		lines.emplace_back(GetOmen(GetOmenId(item)).description);
		lines.emplace_back(_("Works while in your inventory, and is used up"));
		break;
	case IMISC_AUGMENT: {
		const Augment &augment = GetAugment(GetAugmentId(item));
		if (augment.weaponText == augment.armourText && augment.weaponText == augment.staffText) {
			lines.emplace_back(augment.weaponText);
		} else {
			lines.push_back(StrCat(_("Weapon: "), augment.weaponText));
			if (augment.staffText != augment.weaponText)
				lines.push_back(StrCat(_("Staff, wand or sceptre: "), augment.staffText));
			lines.push_back(StrCat(_("Armour: "), augment.armourText));
		}
		lines.emplace_back(_("Goes in an empty augment socket"));
		break;
	}
	case IMISC_ORB_CATALYST:
		lines.push_back(FormatRuntime(_("Adds quality to jewellery that boosts its {:s} modifiers"), CatalystName(GetCatalyst(item))));
		break;
	default:
		break;
	}
	return lines;
}

} // namespace devilution
