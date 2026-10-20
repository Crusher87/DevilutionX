#include "poe2/crafting_items.hpp"

#include <algorithm>

#include "items.h"
#include "poe2/desecration.hpp"
#include "poe2/loot.hpp"
#include "poe2/mix.hpp"
#include "utils/format.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/str_cat.hpp"
#include "utils/utf8.hpp"

namespace devilution {

namespace {

const std::array<Essence, static_cast<size_t>(EssenceKind::Count)> Essences { {
	{ EssenceKind::None, "", "" },
	{ EssenceKind::Vigor, "Vigor", "maximum_life" },
	{ EssenceKind::Clarity, "Clarity", "maximum_mana" },
	{ EssenceKind::Ember, "Ember", "fire_resistance" },
	{ EssenceKind::Frost, "Frost", "cold_resistance" },
	{ EssenceKind::Storm, "Storm", "lightning_resistance" },
	{ EssenceKind::Might, "Might", "local_increased_physical" },
	{ EssenceKind::Celerity, "Celerity", "attack_speed" },
	// The game has no spell damage, critical chance or chaos resistance modifiers, so these guarantee the nearest ones.
	{ EssenceKind::Sorcery, "Sorcery", "cast_speed" },
	{ EssenceKind::Precision, "Precision", "accuracy_rating" },
	{ EssenceKind::Warding, "Warding", "all_elemental_resistance" },
	{ EssenceKind::Spirit, "Spirit", "spirit_flat" },
	{ EssenceKind::Summoning, "Summoning", "minion_damage" },
} };

const std::array<Omen, static_cast<size_t>(OmenId::Count)> Omens { {
	{ OmenId::None, "", OmenTrigger::Exalt, 0, 0, "" },
	{ OmenId::SinistralExaltation, "Omen of Sinistral Exaltation", OmenTrigger::Exalt, 40, 100, "Your next Exalted Orb adds only a prefix" },
	{ OmenId::DextralExaltation, "Omen of Dextral Exaltation", OmenTrigger::Exalt, 40, 100, "Your next Exalted Orb adds only a suffix" },
	{ OmenId::GreaterExaltation, "Omen of Greater Exaltation", OmenTrigger::Exalt, 65, 25, "Your next Exalted Orb adds two modifiers" },
	{ OmenId::HomogenisingExaltation, "Omen of Homogenising Exaltation", OmenTrigger::Exalt, 70, 18, "Your next Exalted Orb adds a modifier of a kind the item already has" },
	{ OmenId::DiscordantExaltation, "Omen of Discordant Exaltation", OmenTrigger::Exalt, 70, 18, "Your next Exalted Orb adds a modifier of a kind the item doesn't have" },
	{ OmenId::SinistralAnnulment, "Omen of Sinistral Annulment", OmenTrigger::Annul, 45, 85, "Your next Orb of Annulment removes only a prefix" },
	{ OmenId::DextralAnnulment, "Omen of Dextral Annulment", OmenTrigger::Annul, 45, 85, "Your next Orb of Annulment removes only a suffix" },
	{ OmenId::Whittling, "Omen of Whittling", OmenTrigger::Annul, 58, 45, "Your next Orb of Annulment removes the lowest level modifier" },
	{ OmenId::SinistralErasure, "Omen of Sinistral Erasure", OmenTrigger::Chaos, 50, 70, "Your next Chaos Orb removes only a prefix" },
	{ OmenId::DextralErasure, "Omen of Dextral Erasure", OmenTrigger::Chaos, 50, 70, "Your next Chaos Orb removes only a suffix" },
	{ OmenId::SinistralPreservation, "Omen of Sinistral Preservation", OmenTrigger::Chaos, 68, 25, "Your next Chaos Orb never removes a prefix" },
	{ OmenId::DextralPreservation, "Omen of Dextral Preservation", OmenTrigger::Chaos, 68, 25, "Your next Chaos Orb never removes a suffix" },
	{ OmenId::SinistralCrystallisation, "Omen of Sinistral Crystallisation", OmenTrigger::PerfectEssence, 68, 30, "Your next Perfect Essence removes only a prefix" },
	{ OmenId::DextralCrystallisation, "Omen of Dextral Crystallisation", OmenTrigger::PerfectEssence, 68, 30, "Your next Perfect Essence removes only a suffix" },
	{ OmenId::Severance, "Omen of Severance", OmenTrigger::PerfectEssence, 74, 14, "Your next Perfect Essence removes the highest level modifier" },
	{ OmenId::Mercy, "Omen of Mercy", OmenTrigger::Chance, 75, 8, "Your next failed Orb of Chance leaves the item whole" },
	{ OmenId::Corruption, "Omen of Corruption", OmenTrigger::Corrupt, 72, 12, "Your next Vaal Orb always changes the item" },
	{ OmenId::Artificer, "Omen of the Artificer", OmenTrigger::Corrupt, 78, 6, "Your next Vaal Orb favours adding a socket" },
	{ OmenId::SinistralNecromancy, "Omen of Sinistral Necromancy", OmenTrigger::Desecrate, 65, 20, "Your next desecration reveals a prefix" },
	{ OmenId::DextralNecromancy, "Omen of Dextral Necromancy", OmenTrigger::Desecrate, 65, 20, "Your next desecration reveals a suffix" },
	{ OmenId::Kinship, "Omen of Kinship", OmenTrigger::Desecrate, 75, 10, "One revealed choice shares a kind with the item's modifiers" },
} };

const std::array<Augment, static_cast<size_t>(AugmentId::Count)> Augments { {
	{ AugmentId::None, "", 0, 0, ModEffect::None, {}, ModEffect::None, {}, UniqueRule::None, 0, "", "" },
	{ AugmentId::EmberRune, "Ember Rune", 1, 10, ModEffect::AddedFire, { 4, 9 }, ModEffect::FireResistance, { 12, 0 }, UniqueRule::None, 0, "Adds 4 to 9 fire damage", "+12% to fire resistance" },
	{ AugmentId::FrostRune, "Frost Rune", 1, 10, ModEffect::MagicResistance, { 12, 0 }, ModEffect::MagicResistance, { 12, 0 }, UniqueRule::None, 0, "+12% to magic resistance", "+12% to magic resistance" },
	{ AugmentId::StormRune, "Storm Rune", 1, 10, ModEffect::AddedLightning, { 1, 14 }, ModEffect::LightningResistance, { 12, 0 }, UniqueRule::None, 0, "Adds 1 to 14 lightning damage", "+12% to lightning resistance" },
	{ AugmentId::IronRune, "Iron Rune", 1, 10, ModEffect::PhysicalPercent, { 18, 0 }, ModEffect::DefensePercent, { 18, 0 }, UniqueRule::None, 0, "18% increased physical damage", "18% increased armour, evasion and energy shield" },
	{ AugmentId::WindRune, "Wind Rune", 1, 15, ModEffect::AttackSpeed, { 5, 0 }, ModEffect::Dexterity, { 8, 0 }, UniqueRule::None, 0, "5% increased attack speed", "+8 to dexterity" },
	{ AugmentId::HeartCore, "Soul Core of Heart", 2, 30, ModEffect::Life, { 30, 0 }, ModEffect::Life, { 30, 0 }, UniqueRule::None, 0, "+30 to maximum life", "+30 to maximum life" },
	{ AugmentId::MindCore, "Soul Core of Mind", 2, 30, ModEffect::Mana, { 30, 0 }, ModEffect::Mana, { 30, 0 }, UniqueRule::None, 0, "+30 to maximum mana", "+30 to maximum mana" },
	{ AugmentId::KeenCore, "Soul Core of Keenness", 2, 30, ModEffect::Accuracy, { 12, 0 }, ModEffect::Accuracy, { 8, 0 }, UniqueRule::None, 0, "+12% chance to hit", "+8% chance to hit" },
	{ AugmentId::HunterTalisman, "Hunter's Talisman", 3, 45, ModEffect::None, {}, ModEffect::None, {}, UniqueRule::DamageVsRareUnique, 15, "15% more damage against rare and unique monsters", "15% more damage against rare and unique monsters" },
	{ AugmentId::GuardianTalisman, "Guardian's Talisman", 3, 45, ModEffect::None, {}, ModEffect::None, {}, UniqueRule::LessHitDamage, 6, "6% less damage taken from hits", "6% less damage taken from hits" },
} };

/** @brief Picks one of the weighted choices with the roll. */
template <typename T>
T PickWeighted(const std::vector<std::pair<T, int>> &choices, uint32_t roll)
{
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
			if (augment.id != AugmentId::None && (augment.tier == 1 || augment.dropLevel <= level))
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

const Essence &GetEssence(EssenceKind kind)
{
	return Essences[std::min(static_cast<size_t>(kind), Essences.size() - 1)];
}

uint8_t EssenceVariant(EssenceKind kind, EssenceTier tier)
{
	return static_cast<uint8_t>(static_cast<uint8_t>(kind) | (static_cast<uint8_t>(tier) << 4));
}

EssenceKind GetEssenceKind(const Item &item)
{
	if (item.isEmpty() || item._iMiscId != IMISC_ESSENCE)
		return EssenceKind::None;
	const uint8_t kind = CraftingItemVariant(item) & 0xF;
	return kind < static_cast<uint8_t>(EssenceKind::Count) ? static_cast<EssenceKind>(kind) : EssenceKind::None;
}

EssenceTier GetEssenceTier(const Item &item)
{
	return static_cast<EssenceTier>((CraftingItemVariant(item) >> 4) & 3);
}

const Omen &GetOmen(OmenId id)
{
	return Omens[std::min(static_cast<size_t>(id), Omens.size() - 1)];
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
	return Augments[std::min(static_cast<size_t>(id), Augments.size() - 1)];
}

AugmentId GetAugmentId(const Item &item)
{
	if (item.isEmpty() || item._iMiscId != IMISC_AUGMENT)
		return AugmentId::None;
	const uint8_t id = CraftingItemVariant(item);
	return id < static_cast<uint8_t>(AugmentId::Count) ? static_cast<AugmentId>(id) : AugmentId::None;
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
		return IsAnyOf(effect, ModEffect::Mana, ModEffect::ManaRegeneration, ModEffect::CastSpeed, ModEffect::SpellSkillLevels, ModEffect::FireSkillLevels, ModEffect::LightningSkillLevels);
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
		return IsAnyOf(effect, ModEffect::Spirit, ModEffect::MinionDamage);
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
		if (item._iClass == ICLASS_WEAPON)
			ApplyModEffect(item, augment.weaponEffect, augment.weaponValues[0] * percent / 100, augment.weaponValues[1] * percent / 100);
		else
			ApplyModEffect(item, augment.armourEffect, augment.armourValues[0] * percent / 100, augment.armourValues[1] * percent / 100);
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
		lines.push_back(StrCat(augment.name, ": ", item._iClass == ICLASS_WEAPON ? augment.weaponText : augment.armourText));
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
		if (augment.weaponText == augment.armourText) {
			lines.emplace_back(augment.weaponText);
		} else {
			lines.push_back(StrCat(_("Weapon: "), augment.weaponText));
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
